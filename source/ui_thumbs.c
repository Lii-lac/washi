// Small copies of profile pictures, made on a thread of their own (ui_thumbs.h). A fixed table of slots holds them; the main thread asks (and is the
// only one that frees), the worker takes the newest request, reads and decodes the picture and shrinks it, and hands the copy back under the lock.
// A flush moves a generation on, so a copy that was being made for pictures that are gone is thrown away when it comes back.
#include "ui_thumbs.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom_pics.h"
#include "fsx.h"
#include "job_thread.h"
#include "ui_internal.h"

#define SLOTS 512 // 512 copies of 76 x 76: about 12 MB at most
#define FILE_MAX (4 * 1024 * 1024)

enum { S_EMPTY, S_QUEUED, S_BUSY, S_READY, S_NONE };

typedef struct {
    int id, state, prio;
    unsigned used, want;  // used: when it was last drawn (the oldest goes first when room is needed); want: when it was last asked for
    char path[FSX_PATH];  // an Extra icon's file ("" for the console's pictures)
    uint8_t *px;          // THUMB_PX x THUMB_PX x 4 once READY
} Slot;

static Slot g_s[SLOTS];
static JobLock g_lock;
static JobThread g_th;
static int g_inited, g_running, g_stop, g_busy; // g_busy: the worker is making a copy (reading a source) right now
static unsigned g_tick, g_serial, g_gen;
int g_thumbs_sync;

// to THUMB_PX square, each new pixel the average of the old ones it covers (weighted by how much of each, and by their alpha: a clear pixel has no colour)
static uint8_t *shrink(const uint8_t *src, int sw, int sh)
{
    uint8_t *out = malloc((size_t)THUMB_PX * THUMB_PX * 4);
    if (!out) return NULL;
    const float fx = (float)sw / THUMB_PX, fy = (float)sh / THUMB_PX;
    for (int y = 0; y < THUMB_PX; y++) {
        float y0 = y * fy, y1 = y0 + fy;
        int iy0 = (int)y0, iy1 = (int)ceilf(y1);
        if (iy1 > sh) iy1 = sh;
        for (int x = 0; x < THUMB_PX; x++) {
            float x0 = x * fx, x1 = x0 + fx;
            int ix0 = (int)x0, ix1 = (int)ceilf(x1);
            if (ix1 > sw) ix1 = sw;
            float acc[3] = {0, 0, 0}, asum = 0, wsum = 0;
            for (int j = iy0; j < iy1; j++) {
                float wy = fminf(y1, (float)(j + 1)) - fmaxf(y0, (float)j);
                if (wy <= 0) continue;
                for (int i = ix0; i < ix1; i++) {
                    float wx = fminf(x1, (float)(i + 1)) - fmaxf(x0, (float)i);
                    if (wx <= 0) continue;
                    const uint8_t *p = src + ((size_t)j * sw + i) * 4;
                    float w = wx * wy, a = p[3] * w;
                    acc[0] += p[0] * a;
                    acc[1] += p[1] * a;
                    acc[2] += p[2] * a;
                    asum += a;
                    wsum += w;
                }
            }
            uint8_t *o = out + ((size_t)y * THUMB_PX + x) * 4;
            for (int k = 0; k < 3; k++) o[k] = asum > 0 ? (uint8_t)fminf(255.0f, acc[k] / asum + 0.5f) : 0;
            o[3] = wsum > 0 ? (uint8_t)fminf(255.0f, asum / wsum + 0.5f) : 0;
        }
    }
    return out;
}

static uint8_t *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long len = ftell(f);
        if (len > 0 && len <= FILE_MAX && fseek(f, 0, SEEK_SET) == 0 && (buf = malloc((size_t)len))) {
            if (fread(buf, 1, (size_t)len, f) != (size_t)len) {
                free(buf);
                buf = NULL;
            } else {
                *n = (size_t)len;
            }
        }
    }
    fclose(f);
    return buf;
}

// the copy of one picture: an Extra icon from its file, a console picture from the console's own source
static uint8_t *make(int id, const char *path)
{
    int w = 0, h = 0;
    uint8_t *full = NULL;
    if (path[0]) {
        size_t n = 0;
        uint8_t *buf = read_all(path, &n);
        if (buf) full = gfx_decode_png(buf, n, &w, &h);
        free(buf);
    } else {
        full = skin_avatar_load_rgba(id, &w, &h);
    }
    if (!full) return NULL;
    uint8_t *t = w > 0 && h > 0 && w <= 4096 && h <= 4096 ? shrink(full, w, h) : NULL;
    free(full);
    return t;
}

static void worker(void *arg)
{
    (void)arg;
    for (;;) {
        job_lock(&g_lock);
        if (g_stop) {
            job_unlock(&g_lock);
            break;
        }
        int best = -1;
        for (int i = 0; i < SLOTS; i++)
            if (g_s[i].state == S_QUEUED && (best < 0 || g_s[i].prio > g_s[best].prio || (g_s[i].prio == g_s[best].prio && g_s[i].want > g_s[best].want))) best = i;
        int id = -1;
        unsigned gen = g_gen;
        char path[FSX_PATH];
        if (best >= 0) {
            g_s[best].state = S_BUSY;
            id = g_s[best].id;
            memcpy(path, g_s[best].path, sizeof path);
            g_busy = 1;
        }
        job_unlock(&g_lock);
        if (best < 0) {
            job_sleep_ms(4);
            continue;
        }
        uint8_t *t = make(id, path);
        job_lock(&g_lock);
        g_busy = 0;
        Slot *s = &g_s[best];
        if (gen == g_gen && s->id == id && s->state == S_BUSY) {
            s->px = t;
            s->state = t ? S_READY : S_NONE;
            g_serial++;
            t = NULL;
        }
        job_unlock(&g_lock);
        free(t);
    }
}

static void init(void)
{
    if (g_inited) return;
    job_lock_init(&g_lock);
    for (int i = 0; i < SLOTS; i++) g_s[i].id = -1;
    g_inited = 1;
}

static void start_thread(void)
{
    if (g_running || g_thumbs_sync) return;
    g_stop = 0;
    if (job_thread_start_on(&g_th, worker, NULL, 2) == 0) g_running = 1;
}

// the slot of picture id, or (create) a new one for it: an empty one, else the one drawn longest ago that isn't being made. Under the lock.
static Slot *slot_for(int id, int create)
{
    for (int i = 0; i < SLOTS; i++)
        if (g_s[i].state != S_EMPTY && g_s[i].id == id) return &g_s[i];
    if (!create) return NULL;
    int pick = -1;
    for (int i = 0; i < SLOTS && pick < 0; i++)
        if (g_s[i].state == S_EMPTY) pick = i;
    if (pick < 0) // (none empty: the one drawn longest ago)
        for (int i = 0; i < SLOTS; i++)
            if (g_s[i].state != S_BUSY && (pick < 0 || g_s[i].used < g_s[pick].used)) pick = i;
    if (pick < 0) return NULL;
    Slot *s = &g_s[pick];
    free(s->px);
    memset(s, 0, sizeof *s);
    s->id = id;
    s->state = S_EMPTY;
    return s;
}

// asks for picture id (prio 1: to draw now, 0: ahead of time); returns its slot. Under the lock.
static Slot *ask(int id, int prio)
{
    Slot *s = slot_for(id, 1);
    if (!s) return NULL;
    if (s->state == S_EMPTY) {
        s->path[0] = 0;
        if (custom_is(id)) {
            const char *key = custom_key(id), *dir = ui_icon_dir();
            if (!key || !dir[0] || snprintf(s->path, sizeof s->path, "%s/%s.png", dir, key) >= (int)sizeof s->path) {
                s->state = S_NONE;
                return s;
            }
        }
        s->state = S_QUEUED;
        s->prio = prio;
    }
    if (s->state == S_QUEUED) {
        s->want = ++g_tick;
        if (prio > s->prio) s->prio = prio;
    }
    if (s->state == S_QUEUED && g_thumbs_sync) { // (tests: now, here)
        s->px = make(s->id, s->path);
        s->state = s->px ? S_READY : S_NONE;
        g_serial++;
    }
    return s;
}

const uint8_t *thumbs_get(int id, int *state)
{
    *state = THUMB_NONE;
    if (id < 0) return NULL;
    init();
    start_thread();
    job_lock(&g_lock);
    Slot *s = ask(id, 1);
    const uint8_t *px = NULL;
    *state = THUMB_WAIT;
    if (s) {
        s->used = ++g_tick;
        if (s->state == S_READY) {
            px = s->px;
            *state = THUMB_READY;
        } else if (s->state == S_NONE) {
            *state = THUMB_NONE;
        }
    }
    job_unlock(&g_lock);
    return px;
}

void thumbs_want(int id)
{
    if (id < 0) return;
    init();
    start_thread();
    job_lock(&g_lock);
    ask(id, 0);
    job_unlock(&g_lock);
}

int thumbs_ready(int id)
{
    if (id < 0) return 1;
    init();
    job_lock(&g_lock);
    Slot *s = slot_for(id, 0);
    int r = s && (s->state == S_READY || s->state == S_NONE);
    job_unlock(&g_lock);
    return r;
}

void thumbs_drop_wants(void)
{
    if (!g_inited) return;
    job_lock(&g_lock);
    for (int i = 0; i < SLOTS; i++)
        if (g_s[i].state == S_QUEUED) {
            g_s[i].state = S_EMPTY;
            g_s[i].id = -1;
        }
    job_unlock(&g_lock);
}

unsigned thumbs_serial(void)
{
    if (!g_inited) return 0;
    job_lock(&g_lock);
    unsigned v = g_serial;
    job_unlock(&g_lock);
    return v;
}

// (and waits for a copy being made to be done with its source: the caller may be about to close it)
void thumbs_flush(void)
{
    if (!g_inited) return;
    job_lock(&g_lock);
    g_gen++;
    for (int i = 0; i < SLOTS; i++) {
        free(g_s[i].px);
        memset(&g_s[i], 0, sizeof g_s[i]);
        g_s[i].id = -1;
    }
    g_serial++;
    int busy = g_busy;
    job_unlock(&g_lock);
    while (busy) {
        job_sleep_ms(1);
        job_lock(&g_lock);
        busy = g_busy;
        job_unlock(&g_lock);
    }
}

void thumbs_shutdown(void)
{
    if (!g_inited) return;
    if (g_running) {
        job_lock(&g_lock);
        g_stop = 1;
        job_unlock(&g_lock);
        job_thread_join(&g_th);
        g_running = 0;
    }
    thumbs_flush();
}
