#define _GNU_SOURCE
#include "platform_host.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "../source/gfx/stb_image_write.h"

#include "../source/fsx.h"
#include "../source/ui.h"

static HostPlat *H(Platform *p) { return (HostPlat *)p; }

static double g_bench_ms[4096]; // BENCH=1: how long frames take to draw
static int g_bench_n;
static double last_real;

void hp_save_png(HostPlat *h, const char *name)
{
    if (!h->last) return;
    last_real = 0; // saving a picture takes a while: not part of a frame
    if (getenv("NOPNG")) return; // for benchmarks and profiles: skip the picture files
    char dir[400], path[500];
    snprintf(dir, sizeof dir, "%s/%s", h->outdir, h->scenario);
    fsx_mkdir_p(dir);
    snprintf(path, sizeof path, "%s/%02d-%s.png", dir, ++h->snaps, name);
    stbi_write_png(path, SCREEN_W, SCREEN_H, 4, h->last->px, SCREEN_W * 4);
}

// left edge of the focus frame in a screenshot: the first column with a long run of the accent colour (the frame's left side is a
// line as tall as a tile; an avatar that happens to be that blue is a short curve; the rail's dot lives far right)
static int focus_left(const Surf *f)
{
    for (int x = 0; x < 1000; x++) {
        int run = 0;
        for (int y = 240; y < 470; y++) {
            const uint8_t *q = f->px + ((size_t)y * SCREEN_W + x) * 4;
            if (q[0] < 14 && abs(q[1] - 184) < 16 && abs(q[2] - 230) < 16) run++;
        }
        if (run >= 100) return x;
    }
    return -1;
}

// right edge of what changed against a reference picture, within the tile row (Washi's Home has no frame to find: focus is a lift)
static int changed_right(const Surf *ref, const Surf *f)
{
    for (int x = 1000; x >= 0; x--)
        for (int y = 300; y < 500; y++) {
            const uint8_t *a = ref->px + ((size_t)y * SCREEN_W + x) * 4, *b = f->px + ((size_t)y * SCREEN_W + x) * 4;
            if (abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]) > 60) return x;
        }
    return -1;
}

int hp_focus_left(HostPlat *h) { return h->last ? focus_left(h->last) : -1; }

// one point of a scripted touch per call, whatever the timeout, so a drag keeps moving while a screen animates
static uint64_t touch_step(HostPlat *h)
{
    if (h->touch_idx < h->touch_step->ntouch) {
        h->touch_is_down = 1;
        h->touch_x = h->touch_step->tx[h->touch_idx];
        h->touch_y = h->touch_step->ty[h->touch_idx];
        h->touch_idx++;
    } else {
        h->touch_is_down = 0;
        h->touch_active = 0;
        h->input_at = h->presents;
    }
    return UI_TOUCH;
}

static int p_touch(Platform *p, float *x, float *y)
{
    HostPlat *h = H(p);
    *x = h->touch_x;
    *y = h->touch_y;
    return h->touch_is_down;
}

static uint64_t p_wait_input(Platform *p, int timeout_ms)
{
    HostPlat *h = H(p);
    h->p.repeated = 0;
    h->p.stick = 0;
    if (h->touch_active) return touch_step(h);
    // polls never see scripted input, so animations run to completion; except input timed with hp_in, when its moment has come
    if (timeout_ms == 0 && (h->pos >= h->nsteps || !h->steps[h->pos].after || h->presents - h->input_at < h->steps[h->pos].after)) return 0;
    for (;;) {
        if (h->pos >= h->nsteps) return UI_QUIT;
        HpStep *s = &h->steps[h->pos++];
        switch (s->kind) {
        case ST_BTN:
            h->p.repeated = s->n;
            h->p.stick = s->stick;
            h->input_at = h->presents;
            return s->buttons;
        case ST_TOUCH:
            h->input_at = h->presents;
            h->touch_active = 1;
            h->touch_idx = 0;
            h->touch_step = s;
            return touch_step(h);
        case ST_WAIT:
            h->t_ms += 1000;
            h->ticks++;
            return 0;
        case ST_SNAP:
            hp_save_png(h, s->str);
            break;
        case ST_TEXT:
            if (h->ntexts_q < 8) h->texts_q[h->ntexts_q++] = s->str;
            break;
        case ST_SNAP_AFTER:
            for (int i = 0; i < 6; i++)
                if (h->snap_after[i] <= 0) {
                    h->snap_after[i] = s->n;
                    snprintf(h->snap_after_name[i], sizeof h->snap_after_name[i], "%s", s->str);
                    break;
                }
            break;
        case ST_CALL:
            s->fn(h);
            break;
        }
    }
}

// BENCH=1: how long each frame took to draw (real time between two presents with no waiting in between), for finding what is slow
static double now_real_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

static int cmp_d(const void *a, const void *b)
{
    double x = *(const double *)a, y = *(const double *)b;
    return x < y ? -1 : x > y;
}

void hp_bench_report(const char *name)
{
    if (!getenv("BENCH") || g_bench_n < 3) return;
    qsort(g_bench_ms, (size_t)g_bench_n, sizeof g_bench_ms[0], cmp_d);
    double sum = 0;
    for (int i = 0; i < g_bench_n; i++) sum += g_bench_ms[i];
    printf("  bench %-22s %4d frames  mean %5.1f ms  median %5.1f  p90 %5.1f  max %5.1f\n", name, g_bench_n, sum / g_bench_n,
           g_bench_ms[g_bench_n / 2], g_bench_ms[(int)(g_bench_n * 0.9)], g_bench_ms[g_bench_n - 1]);
    if (getenv("BENCH_TOP")) {
        printf("    slowest:");
        for (int i = 1; i <= 6 && i <= g_bench_n; i++) printf(" %.0f", g_bench_ms[g_bench_n - i]);
        printf(" ms\n");
    }
    g_bench_n = 0;
}

// (the lie detector itself is dirty_violations: pixels that changed outside the claimed rectangles)

// Copies what the console would into the next simulated image, then checks that image is the frame.
static void fb_show(HostPlat *h, const Surf *frame, const Dirty *now)
{
    if (!h->fbimg[0]) {
        if (h->fb_images < 1 || h->fb_images > 3) h->fb_images = 3;
        fbsim_init(&h->fbsim, h->fb_images);
        for (int i = 0; i < 3; i++) {
            h->fbimg[i] = malloc((size_t)HP_FB_STRIDE * SCREEN_H);
            memset(h->fbimg[i], 0x5A, (size_t)HP_FB_STRIDE * SCREEN_H);
        }
    }
    Dirty copy;
    int slot = fbsim_slot(&h->fbsim);
    fbsim_present(&h->fbsim, now, &copy);
    dirty_copy(frame, h->fbimg[slot], HP_FB_STRIDE, &copy);
    h->copied_pixels += dirty_area(&copy);
    for (int y = 0; y < SCREEN_H; y++) {
        const uint8_t *a = frame->px + (size_t)y * SCREEN_W * 4, *b = h->fbimg[slot] + (size_t)y * HP_FB_STRIDE;
        if (!memcmp(a, b, SCREEN_W * 4)) continue;
        for (int x = 0; x < SCREEN_W; x++) h->fb_mismatches += memcmp(a + x * 4, b + x * 4, 4) != 0;
    }
}

// rects = 0: a whole-frame present; otherwise the n rectangles are what changed since the frame before
static void host_present(Platform *p, const Surf *frame, const Rect *r, int n, int rects)
{
    HostPlat *h = H(p);
    {
        double t = now_real_ms();
        if (last_real > 0 && t - last_real < 150 && g_bench_n < 4096 && !h->touch_active) g_bench_ms[g_bench_n++] = t - last_real;
        last_real = t;
    }
    Dirty now;
    dirty_clear(&now);
    if (!rects || !h->last) { // (the very first frame has nothing before it to be relative to)
        dirty_all(&now);
        h->full_presents++;
    } else {
        h->dirty_violations += dirty_violations(h->last, frame, r, n);
        for (int i = 0; i < n; i++) dirty_add_rect(&now, r[i]);
        h->rect_presents++;
    }
    fb_show(h, frame, &now);
    if (!h->last) h->last = surf_new(SCREEN_W, SCREEN_H);
    surf_copy(h->last, frame);
    __atomic_add_fetch(&h->t_ms, 16, __ATOMIC_RELAXED);
    if (!ui_in_transition()) { // the frames of a fade between two screens don't count as screens
        h->presents++;
        for (int i = 0; i < 6; i++)
            if (h->snap_after[i] > 0 && --h->snap_after[i] == 0) hp_save_png(h, h->snap_after_name[i]);
    }
    if (h->trace_on) {
        if (h->ntrace < 256) { // (the trace keeps the first 256 frames; a frame hook sees every frame)
            h->trace_t[h->ntrace] = h->t_ms;
            h->trace_x[h->ntrace++] = h->trace_ref ? changed_right(h->trace_ref, frame) : focus_left(frame);
            if (h->trace_png) hp_save_png(h, "frame");
        }
        if (h->frame_hook) h->frame_hook(h, frame);
    }
}

static void p_present(Platform *p, const Surf *frame) { host_present(p, frame, NULL, 0, 0); }
static void p_present_rects(Platform *p, const Surf *frame, const Rect *r, int n) { host_present(p, frame, r, n, 1); }

static uint64_t p_now(Platform *p)
{
    HostPlat *h = H(p); // (atomic: the intro may read the clock from another thread)
    return __atomic_add_fetch(&h->t_ms, (uint64_t)h->now_step_ms, __ATOMIC_RELAXED);
}
static int p_dark(Platform *p) { return H(p)->dark_sys; }

static int p_device_name(Platform *p, char *out, size_t cap)
{
    (void)p;
    snprintf(out, cap, "%s", "Esports Club Switch");
    return 0;
}

static int p_local_time(Platform *p, time_t t, struct tm *out)
{
    time_t shifted = t + H(p)->tz_offset_s;
    gmtime_r(&shifted, out);
    return 0;
}

// the sounds are counted, not played
static void p_play(Platform *p, int snd, float gain, int delay_ms)
{
    HostPlat *h = (HostPlat *)p;
    if (snd < 0 || snd >= SND_COUNT) return;
    if (h->nplog < 512) {
        h->plog[h->nplog].t = (uint64_t)__atomic_load_n(&h->t_ms, __ATOMIC_RELAXED);
        h->plog[h->nplog].snd = snd;
        h->plog[h->nplog].delay = delay_ms;
        h->plog[h->nplog++].gain = gain;
    }
    h->sounds[snd]++;
    h->sounds_total++;
    h->last_gain[snd] = gain;
    if (gain > h->max_gain[snd]) h->max_gain[snd] = gain;
}

static void p_hush(Platform *p) { H(p)->hushes++; }
static void p_fade_off(Platform *p) { H(p)->fade_offs++; }

// ---- updates: a pretend GitHub. The release API answers as h->net says (a 404 for a repo with no release, a 403, the running version, a newer one with
// its washi.nro), and the download is a small file that is an NRO (its magic at 0x10) or a short one that is not whole
static int p_online(Platform *p) { return H(p)->net == HP_NET_OFFLINE ? 0 : 1; }

static int p_http_get_locked(Platform *p, const char *url, uint8_t **body, size_t *len, int *status, const volatile int *cancel,
                             void (*progress)(void *, uint64_t, uint64_t), void *ud)
{
    HostPlat *h = H(p);
    h->net_gets++;
    *body = NULL;
    *len = 0;
    *status = 0;
    if (h->net == HP_NET_OFFLINE || (cancel && *cancel)) return -1;
    if (h->http_hook && h->http_hook(url, body, len, status)) return 0;
    char *txt = malloc(2048);
    if (!txt) return -1;
    if (strstr(url, "api.github.com")) {
        if (h->net == HP_NET_MISSING) {
            *status = 404;
            snprintf(txt, 2048, "{\"message\":\"Not Found\",\"status\":\"404\"}");
        } else if (h->net == HP_NET_HTTPERR) {
            *status = 403;
            snprintf(txt, 2048, "{\"message\":\"API rate limit exceeded\"}");
        } else {
            *status = 200;
            int newer = h->net == HP_NET_NEWER || h->net == HP_NET_BADDL;
            snprintf(txt, 2048,
                     "{\"tag_name\":\"v%s\",\"published_at\":\"2026-10-12T18:00:00Z\",\"body\":\"## What's new\\r\\n- **Updates** from This app\\r\\n- A [stamp](https://x) on "
                     "skip\\r\\n* `Faster` tear-off\\r\\nSome words that are not a list.\\r\\n\",\"assets\":[{\"name\":\"washi.nro\",\"size\":4096,"
                     "\"browser_download_url\":\"https://github.com/lii-lac/washi/releases/download/v9.9.9/washi.nro\"}]}",
                     newer ? "9.9.9" : APP_VERSION);
        }
        *body = (uint8_t *)txt;
        *len = strlen(txt);
        return 0;
    }
    free(txt);
    size_t n = h->net == HP_NET_BADDL ? 1000 : 4096;
    uint8_t *b = calloc(1, n);
    if (!b) return -1;
    memcpy(b + 0x10, "NRO0", 4);
    for (size_t got = 0; got <= n; got += 1024)
        if (progress) progress(ud, got, 4096);
    *body = b;
    *len = n;
    *status = 200;
    return 0;
}

// (the pretend GitHub is one thing at a time, as its counters and hook are not made for several threads: the downloads of the Extra icons come from a few)
static int p_http_get(Platform *p, const char *url, uint8_t **body, size_t *len, int *status, const volatile int *cancel,
                      void (*progress)(void *, uint64_t, uint64_t), void *ud)
{
    static pthread_mutex_t mx = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_lock(&mx);
    int rc = p_http_get_locked(p, url, body, len, status, cancel, progress, ud);
    pthread_mutex_unlock(&mx);
    return rc;
}

static int p_install_update(Platform *p, const uint8_t *nro, size_t len)
{
    (void)nro;
    H(p)->installs++;
    H(p)->installed_len = len;
    return 0;
}

static int p_restart(Platform *p)
{
    H(p)->restarts++;
    return 1;
}

static int p_previous(Platform *p, char *ver, size_t cap)
{
    const char *k = H(p)->kept_ver;
    snprintf(ver, cap, "%s", k ? k : "");
    return k != NULL;
}

static int p_roll_back(Platform *p)
{
    H(p)->roll_backs++;
    return H(p)->kept_ver ? 0 : -1;
}

static void p_started_ok(Platform *p) { H(p)->started_oks++; }

static int p_rolled_back(Platform *p, char *ver, size_t cap)
{
    const char *r = H(p)->rolled_ver;
    H(p)->rolled_ver = NULL; // (once)
    snprintf(ver, cap, "%s", r ? r : "");
    return r != NULL;
}

static int p_text_input(Platform *p, const char *prompt, const char *initial, char *out, size_t cap)
{
    HostPlat *h = H(p);
    (void)initial;
    if (h->ntexts_seen < 8) snprintf(h->texts_seen[h->ntexts_seen++], sizeof h->texts_seen[0], "%s", prompt);
    if (!h->keyboard_available) return -1;
    if (h->ntexts_q == 0) return 0;
    const char *t = h->texts_q[0];
    memmove(h->texts_q, h->texts_q + 1, (size_t)(--h->ntexts_q) * sizeof h->texts_q[0]);
    if (!t[0]) return 0;
    snprintf(out, cap, "%s", t);
    return 1;
}

void hp_init(HostPlat *h, const char *outdir, const char *scenario)
{
    memset(h, 0, sizeof *h);
    h->p.wait_input = p_wait_input;
    h->p.present = p_present;
    h->p.present_rects = p_present_rects;
    h->fb_images = 3;
    h->p.now_ms = p_now;
    h->p.system_dark = p_dark;
    h->p.local_time = p_local_time;
    h->p.device_name = p_device_name;
    h->p.touch = p_touch;
    h->p.text_input = p_text_input;
    h->p.play = p_play;
    h->p.hush = p_hush;
    h->p.fade_off = p_fade_off;
    h->p.online = p_online;
    h->p.http_get = p_http_get;
    h->p.install_update = p_install_update;
    h->p.restart = p_restart;
    h->p.previous = p_previous;
    h->p.roll_back = p_roll_back;
    h->p.started_ok = p_started_ok;
    h->p.rolled_back = p_rolled_back;
    h->dark_sys = 0;
    h->keyboard_available = 1;
    h->t_ms = 100000;
    h->now_step_ms = 5;
    snprintf(h->outdir, sizeof h->outdir, "%s", outdir);
    snprintf(h->scenario, sizeof h->scenario, "%s", scenario);
}

void hp_free(HostPlat *h)
{
    surf_free(h->last);
    h->last = NULL;
    for (int i = 0; i < 3; i++) {
        free(h->fbimg[i]);
        h->fbimg[i] = NULL;
    }
}

static HpStep *push(HostPlat *h, HpStepKind k)
{
    if (h->nsteps >= HP_MAX_STEPS) {
        fprintf(stderr, "script too long\n");
        exit(2);
    }
    HpStep *s = &h->steps[h->nsteps++];
    memset(s, 0, sizeof *s);
    s->kind = k;
    return s;
}

void hp_btn(HostPlat *h, uint64_t mask) { push(h, ST_BTN)->buttons = mask; }

void hp_btn_rep(HostPlat *h, uint64_t mask)
{
    HpStep *s = push(h, ST_BTN);
    s->buttons = mask;
    s->n = 1;
}

void hp_stick(HostPlat *h, uint64_t dirs)
{
    HpStep *s = push(h, ST_BTN);
    s->buttons = dirs;
    s->stick = 1;
}

void hp_in(HostPlat *h, int frames)
{
    if (h->nsteps > 0) h->steps[h->nsteps - 1].after = frames;
}

void hp_btn_n(HostPlat *h, uint64_t mask, int times)
{
    for (int i = 0; i < times; i++) hp_btn(h, mask);
}

void hp_wait(HostPlat *h, int ticks)
{
    for (int i = 0; i < ticks; i++) push(h, ST_WAIT);
}

void hp_tap(HostPlat *h, float x, float y)
{
    HpStep *s = push(h, ST_TOUCH);
    s->tx[0] = x;
    s->ty[0] = y;
    s->ntouch = 1;
}

void hp_drag(HostPlat *h, float x0, float y0, float x1, float y1, int points)
{
    if (points < 2) points = 2;
    if (points > 24) points = 24;
    HpStep *s = push(h, ST_TOUCH);
    for (int i = 0; i < points; i++) {
        s->tx[i] = x0 + (x1 - x0) * i / (points - 1);
        s->ty[i] = y0 + (y1 - y0) * i / (points - 1);
    }
    s->ntouch = points;
}

void hp_snap(HostPlat *h, const char *name) { push(h, ST_SNAP)->str = name; }

void hp_snap_after(HostPlat *h, int presents, const char *name)
{
    HpStep *s = push(h, ST_SNAP_AFTER);
    s->n = presents;
    s->str = name;
}

void hp_text(HostPlat *h, const char *text) { push(h, ST_TEXT)->str = text; }

void hp_call(HostPlat *h, void (*fn)(HostPlat *)) { push(h, ST_CALL)->fn = fn; }

void hp_pin(HostPlat *h, const char *digits)
{
    int r = 1, c = 1; // the pad opens with focus on 5
    for (const char *p = digits; *p; p++) {
        int tr, tc;
        if (*p == '0') { tr = 3; tc = 1; }
        else { tr = (*p - '1') / 3; tc = (*p - '1') % 3; }
        while (r < tr) { hp_btn(h, BTN_DOWN); r++; }
        while (r > tr) { hp_btn(h, BTN_UP); r--; }
        while (c < tc) { hp_btn(h, BTN_RIGHT); c++; }
        while (c > tc) { hp_btn(h, BTN_LEFT); c--; }
        hp_btn(h, BTN_A);
    }
}

void hp_keys(HostPlat *h, const char *text)
{
    int r = 1, c = 4; // the on-screen keyboard opens on row 1, column 4
    for (const char *p = text; *p; p++) {
        int tr, tc;
        if (!ui_keyboard_pos(*p, &tr, &tc)) continue;
        while (r < tr) { hp_btn(h, BTN_DOWN); r++; }
        while (r > tr) { hp_btn(h, BTN_UP); r--; }
        while (c < tc) { hp_btn(h, BTN_RIGHT); c++; }
        while (c > tc) { hp_btn(h, BTN_LEFT); c--; }
        hp_btn(h, BTN_A);
    }
}
