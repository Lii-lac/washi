// Extra icons (Admin, This app, Extra icons): the character pictures of Nintendo Switch Online and the games, from the public repository henry-debruin/nso-icons
// (icons.h). The page asks GitHub which games have icons and lists every one with what is already on the SD card. A downloads the game in focus (or looks for new
// icons in one that is there), X every game that is missing, into <washi folder>/pictures/<game>/<name>.png, a picture at a time with a bar on the game's row and
// Stop (B). Y removes a game's icons from the card, R3 sorts. The picture picker shows what is there in a tab of its own (ui_extra.c). Nothing here is shipped
// with Washi: they are Nintendo's artwork, kept on the SD card only.
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom_pics.h"
#include "fsx.h"
#include "icons.h"
#include "job_thread.h"
#include "ui_motion.h"
#include "ui_scene.h"
#include "ui_spr.h"

#define DOT "\xC2\xB7"

static IconRepo *g_repo; // GitHub's list of the icons, for as long as the page is open

// ---- what the repository says and the card has
static void game_title(int g, char *out, size_t n) { icons_title(icons_repo_slug(g_repo, g), out, n); }

static int have_count(const IconSet *set, const char *slug)
{
    int gi = icons_find(set, slug);
    return gi >= 0 ? icons_count(set, gi) : 0;
}

static void path_of(char *out, size_t n, const char *slug, const char *name)
{
    if (name) snprintf(out, n, "%s/%s/%s.png", ui_icon_dir(), slug, name);
    else snprintf(out, n, "%s/%s", ui_icon_dir(), slug);
}

// How many of game g's icons are not on the card yet
static int missing_in(int g)
{
    int miss = 0;
    char path[FSX_PATH];
    for (int i = 0; i < icons_repo_count(g_repo, g); i++) {
        path_of(path, sizeof path, icons_repo_slug(g_repo, g), icons_repo_name(g_repo, g, i));
        if (!fsx_exists(path)) miss++;
    }
    return miss;
}

typedef struct {
    int total, done, failed, stopped, gave_up;
} Fetch;

// One icon: asked for, checked to be a PNG, and put on the card (written whole or not at all). Returns whether it is there now; *rc is http_get's answer
// (non-zero: no answer at all, as opposed to an answer that was not a picture).
static int fetch_one(Platform *p, const char *slug, const char *name, int *rc)
{
    char path[FSX_PATH], url[300];
    path_of(path, sizeof path, slug, name);
    icons_file_url(url, sizeof url, slug, name);
    uint8_t *body = NULL;
    size_t len = 0;
    int status = 0;
    *rc = p->http_get(p, url, &body, &len, &status, NULL, NULL, NULL);
    int ok = *rc == 0 && status == 200 && icons_png_ok(body, len) && fsx_write_atomic(path, body, len, NULL, NULL) == 0;
    free(body);
    return ok;
}

// The icons of a game for several workers at once (platform.h: http_parallel): each takes the next one that is not there yet. A worker keeps its connection
// to GitHub from icon to icon, so the time goes on the files, not on connecting. The page's own thread only draws and listens for B.
typedef struct {
    int g, n;
    const char *slug;
    atomic_int next, seen, done, failed, row_failed, gave_up, stop, running;
} Crew;

static void crew_work(void *arg)
{
    Crew *c = arg;
    Platform *p = G.plat;
    for (;;) {
        if (atomic_load(&c->stop) || atomic_load(&c->gave_up)) break;
        int i = atomic_fetch_add(&c->next, 1);
        if (i >= c->n) break;
        const char *name = icons_repo_name(g_repo, c->g, i);
        char path[FSX_PATH];
        path_of(path, sizeof path, c->slug, name);
        if (!fsx_exists(path)) {
            int rc;
            int ok = fetch_one(p, c->slug, name, &rc);
            atomic_fetch_add(&c->done, 1);
            if (ok) {
                atomic_store(&c->row_failed, 0);
            } else {
                atomic_fetch_add(&c->failed, 1);
                if (rc != 0 && atomic_fetch_add(&c->row_failed, 1) + 1 >= 3) atomic_store(&c->gave_up, 1); // (no answer three times running: the connection is gone)
            }
        }
        atomic_fetch_add(&c->seen, 1);
    }
    atomic_fetch_sub(&c->running, 1);
}

// Gets the icons of game g that are not there yet. `f` carries the running totals (for "Get all", the bar is over everything).
static void page_progress(int g, float frac); // (the page's, below: a download shows itself on it)
static void fetch_game(int g, Fetch *f)
{
    Platform *p = G.plat;
    const char *slug = icons_repo_slug(g_repo, g);
    char dir[FSX_PATH];
    path_of(dir, sizeof dir, slug, NULL);
    fsx_mkdir_p(dir);
    int n = icons_repo_count(g_repo, g);
    enum { MAX_CREW = 8 };
    int want = p->http_parallel < n ? p->http_parallel : n;
    if (want > MAX_CREW) want = MAX_CREW;
    if (want > 1) {
        Crew c;
        memset(&c, 0, sizeof c);
        c.g = g;
        c.n = n;
        c.slug = slug;
        JobThread th[MAX_CREW];
        int started = 0;
        atomic_store(&c.running, want); // (counted before they start: the first may be done before the last has begun)
        for (; started < want; started++)
            if (job_thread_start(&th[started], crew_work, &c) != 0) break;
        if (started < want) atomic_fetch_sub(&c.running, want - started);
        if (started > 0) {
            while (atomic_load(&c.running) > 0) {
                page_progress(g, (float)atomic_load(&c.seen) / (float)n);
                uint64_t b = ui_poll(0);
                if (b == UI_QUIT || G.quit || (b & BTN_B)) {
                    f->stopped = 1;
                    atomic_store(&c.stop, 1); // (each worker finishes the icon it is on)
                }
            }
            for (int i = 0; i < started; i++) job_thread_join(&th[i]);
            f->done += atomic_load(&c.done);
            f->failed += atomic_load(&c.failed);
            if (atomic_load(&c.gave_up)) f->gave_up = 1;
            return;
        }
        // (no thread could be made: one at a time, below)
    }
    int seen = 0, in_row_failed = 0;
    for (int i = 0; i < n && !f->stopped && !f->gave_up; i++) {
        const char *name = icons_repo_name(g_repo, g, i);
        char path[FSX_PATH];
        path_of(path, sizeof path, slug, name);
        if (fsx_exists(path)) { // (already there: an interrupted download carries on where it stopped)
            seen++;
            continue;
        }
        page_progress(g, (float)seen / (float)n);
        uint64_t b = ui_poll(0);
        if (b == UI_QUIT || G.quit || (b & BTN_B)) {
            f->stopped = 1;
            break;
        }
        int rc;
        int ok = fetch_one(p, slug, name, &rc);
        seen++;
        f->done++;
        if (ok) {
            in_row_failed = 0;
        } else {
            f->failed++;
            if (rc != 0 && ++in_row_failed >= 3) f->gave_up = 1; // (no answer three times running: the connection is gone)
        }
    }
}

static void report(const Fetch *f)
{
    char head[96], detail[200];
    int got = f->done - f->failed;
    if (f->gave_up) {
        ui_message(UI_WARN, "The connection dropped", "What was downloaded is on the SD card. Open this page again to carry on where it stopped.");
    } else if (f->stopped) {
        snprintf(detail, sizeof detail, "%d icon%s %s on the SD card. Open this page again to carry on.", got, got == 1 ? "" : "s", got == 1 ? "is" : "are");
        ui_message(UI_INFO, "Stopped", detail);
    } else if (f->failed) {
        snprintf(head, sizeof head, "%d icon%s couldn\xE2\x80\x99t be downloaded", f->failed, f->failed == 1 ? "" : "s");
        ui_message(UI_WARN, head, "The rest are on the SD card. Open this page again to try the missing ones.");
    } else if (got > 0) {
        snprintf(head, sizeof head, "Got %d icon%s", got, got == 1 ? "" : "s");
        ui_message(UI_OK, head, "Pick them in Edit Icon: Picture, then Y for Extra icons.");
    } else {
        ui_message(UI_INFO, "Nothing to get", "Everything for that is already on the SD card.");
    }
}

// ---- asking GitHub which games have icons
// The question is asked from a worker, so the waiting page's dots keep moving while GitHub answers (the app has one thread of its own).
typedef struct {
    Platform *p;
    const char *url;
    uint8_t *body;
    size_t len;
    int status, rc;
    atomic_int done;
} RepoAsk;

static void repo_ask(void *arg)
{
    RepoAsk *a = arg;
    a->rc = a->p->http_get(a->p, a->url, &a->body, &a->len, &a->status, NULL, NULL, NULL);
    atomic_store(&a->done, 1);
}

static int fetch_repo(void)
{
    Platform *p = G.plat;
    if (!p->http_get) {
        ui_message(UI_INFO, "Extra icons need the internet", "This version of Washi can\xE2\x80\x99t reach it.");
        return 0;
    }
    if (p->online && p->online(p) == 0) {
        ui_message(UI_WARN, "No internet", "Connect to Wi-Fi in System Settings, Internet, then open this page again.");
        return 0;
    }
    ui_busy("Extra icons", "Asking GitHub which games have icons\xE2\x80\xA6");
    char url[256];
    icons_tree_url(url, sizeof url);
    RepoAsk ask = {p, url, NULL, 0, 0, -1, 0};
    JobThread th;
    if (job_thread_start(&th, repo_ask, &ask) == 0) {
        while (!atomic_load(&ask.done)) {
            ui_busy_tick();
            gfx_pump(); // (and the "still working" crayon, after a moment)
            job_sleep_ms(15);
        }
        job_thread_join(&th);
    } else {
        repo_ask(&ask); // (no thread to be had: asked here, the dots standing still)
    }
    uint8_t *body = ask.body;
    size_t len = ask.len;
    int status = ask.status, truncated = 0, rc = ask.rc;
    if (rc != 0) {
        free(body);
        ui_message(UI_WARN, "Couldn\xE2\x80\x99t reach GitHub", "Check the internet connection and try again.");
        return 0;
    }
    if (status != 200) {
        char d[120];
        snprintf(d, sizeof d, "GitHub answered with an error (%d). Try again in a few minutes.", status);
        free(body);
        ui_message(UI_WARN, "Couldn\xE2\x80\x99t get the list of icons", d);
        return 0;
    }
    g_repo = icons_repo_parse((const char *)body, len, &truncated);
    free(body);
    if (!g_repo) {
        ui_message(UI_WARN, "Couldn\xE2\x80\x99t read the list of icons", "GitHub\xE2\x80\x99s answer wasn\xE2\x80\x99t the list this page expects.");
        return 0;
    }
    return 1;
}


// ---------------------------------------------------------------------------------------------------- the page
// A summary slip (how many games are on the card, Get all, and a strip of washi tape with a piece for every game), then a row for every game in the
// order the chip says. The rows are the Backups page's automatic rows: a patch with the game's characters (or ghosts when it is not here), its name, how
// many icons and how big, and what it is: on the card, to get, or coming now with a bar. The page scrolls under the slip.
#define BX 190.0f
#define BW 900.0f
#define SUM_Y 166.0f
#define SUM_H 140.0f
#define ROW_H 66.0f
#define ROW_P 74.0f
#define ROW_Y0 328.0f   // the first row's top at scroll 0
#define VIEW_TOP 316.0f // what shows of the rows: from here ...
#define VIEW_BOT 644.0f // ... to here
#define FADE 14
#define LP D_PAD
#define PANEL PAL(patch, 0xECE9F1)
#define CHIP_X 976.0f
#define CHIP_Y 103.0f
#define CHIP_W 264.0f

static const char *const SORTS[4] = {"Name, A to Z", "Recently downloaded", "Most icons", "On your card first"};
static int g_sort;
int g_ui_icons_first = -1; // tests: the game (its number in the repository's list) in the first row of the last frame composed

typedef struct {
    int ng;
    int *ord;      // the row -> the game's number in the repository's list
    int *have;     // by game: icons on the card
    int *total;    // by game: icons the repository has
    int64_t *added;
    char (*title)[96];
    char (*size)[32];
    int mode, sel;
    int dl_game;   // the game coming now, -1 none
    float dl_frac; // how far it is
    int on_card;   // games with every icon here
} Page;

static Page *g_pg; // while the page is open: what a download shows its progress on

static int page_state(const Page *p, int g)
{
    if (g == p->dl_game) return 2;
    if (p->have[g] >= p->total[g]) return 0;
    return p->have[g] > 0 ? 3 : 1;
}

static const Page *s_page;
static int s_mode;

static int row_cmp(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb, c = 0;
    const Page *p = s_page;
    if (s_mode == 1) c = p->added[a] > p->added[b] ? -1 : p->added[a] < p->added[b] ? 1 : 0;
    else if (s_mode == 2) c = p->total[a] > p->total[b] ? -1 : p->total[a] < p->total[b] ? 1 : 0;
    else if (s_mode == 3) {
        int sa = p->have[a] >= p->total[a] ? 0 : p->have[a] > 0 ? 1 : 2, sb = p->have[b] >= p->total[b] ? 0 : p->have[b] > 0 ? 1 : 2;
        c = sa < sb ? -1 : sa > sb ? 1 : 0;
    }
    if (c) return c;
    for (int i = 0; p->title[a][i] || p->title[b][i]; i++) {
        int x = p->title[a][i], y = p->title[b][i];
        x = x >= 'A' && x <= 'Z' ? x + 32 : x;
        y = y >= 'A' && y <= 'Z' ? y + 32 : y;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

static void page_order(Page *p)
{
    for (int i = 0; i < p->ng; i++) p->ord[i] = i;
    s_page = p;
    s_mode = p->mode;
    qsort(p->ord, (size_t)p->ng, sizeof p->ord[0], row_cmp);
}

static void page_free(Page *p)
{
    free(p->ord), free(p->have), free(p->total), free(p->added), free(p->title), free(p->size);
    memset(p, 0, sizeof *p);
}

// reads what is on the card now
static int page_load(Page *p)
{
    const IconSet *set = ui_icon_set(1);
    int keep = p->ng ? p->ord[p->sel] : -1, mode = p->mode, sel = p->sel;
    page_free(p);
    p->mode = mode;
    int ng = icons_repo_games(g_repo);
    p->ng = ng;
    p->ord = calloc((size_t)(ng ? ng : 1), sizeof(int));
    p->have = calloc((size_t)(ng ? ng : 1), sizeof(int));
    p->total = calloc((size_t)(ng ? ng : 1), sizeof(int));
    p->added = calloc((size_t)(ng ? ng : 1), sizeof(int64_t));
    p->title = calloc((size_t)(ng ? ng : 1), sizeof *p->title);
    p->size = calloc((size_t)(ng ? ng : 1), sizeof *p->size);
    if (!p->ord || !p->have || !p->total || !p->added || !p->title || !p->size) return 0;
    p->dl_game = -1;
    for (int g = 0; g < ng; g++) {
        const char *slug = icons_repo_slug(g_repo, g);
        int gi = icons_find(set, slug);
        p->total[g] = icons_repo_count(g_repo, g);
        p->have[g] = gi >= 0 ? icons_count(set, gi) : 0;
        p->added[g] = gi >= 0 ? icons_added(set, gi) : 0;
        game_title(g, p->title[g], sizeof p->title[g]);
        ui_format_bytes(icons_repo_bytes(g_repo, g), p->size[g], sizeof p->size[g]);
        if (p->have[g] >= p->total[g]) p->on_card++;
    }
    page_order(p);
    p->sel = 0;
    for (int i = 0; i < ng; i++)
        if (keep >= 0 && p->ord[i] == keep) p->sel = i;
    if (keep < 0) p->sel = sel < ng ? sel : 0;
    return 1;
}

// ---- the picture of the first characters of a game on the card (the patch of its row)
static void icon_key(const IconSet *set, int gi, int i, char *key, size_t n) { snprintf(key, n, "%s/%s", icons_slug(set, gi), icons_name(set, gi, i)); }

typedef struct {
    const Page *p;
    int g, lifted;
} RowJob;

static void check_mark(Surf *s, float x, float y, float k, Col ink)
{
    gfx_line(s, x - k * 0.55f, y, x - k * 0.1f, y + k * 0.45f, 2.6f, ink);
    gfx_line(s, x - k * 0.1f, y + k * 0.45f, x + k * 0.6f, y - k * 0.5f, 2.6f, ink);
}

static void tray_arrow(Surf *s, float cx, float cy, float k, Col ink)
{
    gfx_line(s, cx, cy - k, cx, cy + k * 0.5f, 3.4f, ink);
    gfx_line(s, cx - k * 0.6f, cy - k * 0.1f, cx, cy + k * 0.55f, 3.4f, ink);
    gfx_line(s, cx + k * 0.6f, cy - k * 0.1f, cx, cy + k * 0.55f, 3.4f, ink);
    gfx_line(s, cx - k * 0.85f, cy + k, cx + k * 0.85f, cy + k, 3.4f, ink);
}

static void draw_row_body(Surf *s, void *ud)
{
    const RowJob *k = ud;
    const Page *p = k->p;
    const int g = k->g, st = page_state(p, g);
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Col ink = hex(PAL(text, PAPER_TEXT), 1), soft = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);
    const float x = LP, y = LP, w = BW, h = ROW_H;
    paper_spr_put(s, card_get((int)w, (int)h, 6.0f, 90u, LP), LP, LP);
    gfx_rrect(s, x + 12, y + 8, 116, h - 16, 6, hex(PANEL, 1));
    const IconSet *set = ui_icon_set(0);
    int gi = set ? icons_find(set, icons_repo_slug(g_repo, g)) : -1, n = gi >= 0 ? icons_count(set, gi) : 0;
    if (n > 3) n = 3;
    if (n == 0) { // not here yet: ghosts of the people
        for (int i = 0; i < 3; i++) gfx_circle(s, x + 12 + 58 + (i - 1) * 29.0f, y + h / 2, 20, hex(PAL(thread, PAPER_THREAD), 0.35f));
    } else {
        const float r = 17, step = r * 1.7f, total = 2 * r + (n - 1) * step, sx = x + 12 + (116 - total) / 2 + r, cy = y + h / 2;
        for (int i = 0; i < n; i++) {
            char key[CUSTOM_KEY_MAX];
            icon_key(set, gi, i, key, sizeof key);
            gfx_circle(s, sx + i * step, cy, r + 3, hex(PANEL, 1));
            skin_avatar(&c, sx + i * step, cy, r, "", SKIN_LOOK(0, custom_id(key)));
        }
    }
    char fit[96], sub[64];
    text_fit(u->semi, 21, -0.1f, p->title[g], 400, fit, sizeof fit);
    skin_txt(&c, u->semi, 21, -0.1f, x + 150, y + 29, 0, ink, fit);
    if (st == 3) snprintf(sub, sizeof sub, "%d of %d icons", p->have[g], p->total[g]);
    else snprintf(sub, sizeof sub, "%d icon%s", p->total[g], p->total[g] == 1 ? "" : "s");
    skin_txt(&c, u->med, 16.5f, 0, x + 150, y + 51, 0, soft, sub);
    skin_txtc(&c, u->med, 17, 0, x + w - 254, y + h / 2, 2, soft, p->size[g]);
    for (float dy = y + 9; dy < y + h - 10; dy += 8) gfx_line(s, x + w - 234, dy, x + w - 234, dy + 4.5f, 1.8f, hex(PAL(thread, PAPER_THREAD), 0.85f));
    float bx = x + w - 214, by = y + h / 2 - 16;
    if (st == 0) {
        gfx_rrect(s, bx, by, 170, 32, 8, hex(0xD3F0DF, 1));
        check_mark(s, bx + 38, by + 16, 15, hex(0x2F7D5B, 1));
        skin_txtc(&c, u->semi, 17, 0, bx + 108, by + 16.5f, 1, hex(PAPER_TEXT, 1), "On the card");
    } else if (st == 1 || st == 3) {
        gfx_rrect(s, bx, by, 170, 32, 8, hex(paper_tape_rgb[(g * 3 + 3) & 7], 1));
        tray_arrow(s, bx + 34, by + 15, 7, hex(PAPER_TEXT, 1));
        skin_txtc(&c, u->semi, 17, 0, bx + 100, by + 16.5f, 1, hex(PAPER_TEXT, 1), st == 3 ? "Get the rest" : "Get");
    } else {
        char t[40];
        snprintf(t, sizeof t, "%d of %d", (int)(p->total[g] * p->dl_frac), p->total[g]);
        skin_txtc(&c, u->med, 14.5f, 0, bx + 85, by + 3, 1, soft, t);
        gfx_rrect(s, bx, by + 14, 170, 14, 7, hex(PAL(thread, PAPER_THREAD), 0.45f));
        if (p->dl_frac > 0.01f) gfx_rrect(s, bx, by + 14, 170 * p->dl_frac, 14, 7, hex(paper_tape_rgb[3], 1));
    }
}

static const int KINDS[4] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG};

// one row with its top left at (px, py): lying on the page, or lifted (risen off its deep shadow, its tape peeling)
static void draw_row(Cx *c, void *ud, float px, float py)
{
    const RowJob *job = ud;
    Surf *dst = c->s;
    const int idx = job->g;
    RowJob k = *job;
    PaperSpr *body = paper_bake((int)BW + 2 * LP, (int)ROW_H + 2 * LP, draw_row_body, &k);
    const PaperShadows *sh = shadows_get((int)BW, (int)ROW_H, 6.0f, 0.0f);
    float rise = job->lifted ? 7.0f : 0.0f, sc = job->lifted ? 1.012f : 1.0f, cx = px + BW / 2, cy = py + ROW_H / 2 - rise;
    if (sh) paper_spr_put(dst, sh->s[job->lifted ? sh->n - 1 : 0], (int)px, (int)py);
    if (body) paper_spr_xform(dst, body, cx, cy, sc, 0.0f, 1.0f);
    paper_spr_free(body);
    PaperTape tp = {52, 20, paper_tape_rgb[(idx * 3 + 3) & 7], KINDS[idx & 3], PAPER_TAPE_ZIG, -24.0f, 300u + (uint32_t)idx};
    float tx = -12.0f, ty = -7.0f, tdx, tdy;
    float stood = paper_tape_hand(&tp, &tx, &ty, (uint32_t)idx * 7919u + 5u, 0.8f);
    paper_tape_offset(BW, ROW_H, tx, ty, 52, 20, &tdx, &tdy);
    paper_tape_host(&tp, BW, ROW_H, 6.0f, tx, ty);
    PaperXf cf = {cx, cy, sc, 0}, at = paper_xf_attach(cf, tdx, tdy);
    if (job->lifted || stood > 0.0f) {
        PaperPeel *pl = paper_tape_peel_lazy(&tp, PAPER_PEEL_STATES);
        const PaperSpr *ts = pl ? paper_peel_at(pl, job->lifted ? 0.85f : stood) : NULL;
        if (ts) paper_spr_xform(dst, ts, at.x, at.y, at.scale, at.angle, 1.0f);
        paper_peel_free(pl);
    } else {
        PaperSpr *ts = paper_tape(&tp);
        if (ts) paper_spr_xform(dst, ts, at.x, at.y, at.scale, at.angle, 1.0f);
        paper_spr_free(ts);
    }
}

// ---- the cached pictures of the rows
#define SPR_BUDGET (14 * 1024 * 1024)

typedef struct {
    Spr *rest, *lift; // by the game's number in the repository's list
    unsigned frame;
} RowSprs;

static void rowsprs_free(RowSprs *r, int ng)
{
    for (int i = 0; r->rest && i < ng; i++) spr_free_one(&r->rest[i]);
    for (int i = 0; r->lift && i < ng; i++) spr_free_one(&r->lift[i]);
    free(r->rest);
    free(r->lift);
    memset(r, 0, sizeof *r);
}

static void rowsprs_new(RowSprs *r, int ng)
{
    r->rest = calloc((size_t)(ng ? ng : 1), sizeof(Spr));
    r->lift = calloc((size_t)(ng ? ng : 1), sizeof(Spr));
}

static const Spr *row_spr(const Page *p, RowSprs *r, int g, int lifted)
{
    Spr *s = lifted ? &r->lift[g] : &r->rest[g];
    if (!s->px) {
        size_t held = 0;
        for (int i = 0; i < p->ng; i++) held += ((size_t)r->rest[i].w * r->rest[i].h + (size_t)r->lift[i].w * r->lift[i].h) * 4;
        if (held > SPR_BUDGET)
            for (int i = 0; i < p->ng; i++) {
                if (r->rest[i].px && r->rest[i].used != r->frame) spr_free_one(&r->rest[i]);
                if (r->lift[i].px && r->lift[i].used != r->frame) spr_free_one(&r->lift[i]);
            }
        RowJob job = {p, g, lifted};
        spr_bake(s, (int)BW, (int)ROW_H, draw_row, &job);
    }
    s->used = r->frame;
    return s;
}

static float focus_on(const Hops *hp, int idx)
{
    float f = 0;
    for (int k = 0; k < hp->n; k++)
        if ((int)lroundf(hp->x[k]) == idx) f += hops_strength(hp->a[k]);
    return f > 1.0f ? 1.0f : f;
}

// ---- the footer: A gets a game that is missing icons and looks for new ones on a game that is all there
// (X is on the "Get all missing" button and R3 on the order chip: the footer doesn't say them again)
static const Prompt PROMPTS_GET[] = {{"A", "Get"}, {"Y", "Remove"}, {"B", "Back"}};
static const Prompt PROMPTS_UPDATE[] = {{"A", "Update"}, {"Y", "Remove"}, {"B", "Back"}};

static const Prompt *prompts_for(const Page *p)
{
    if (p->ng <= 0) return PROMPTS_UPDATE;
    int g = p->ord[p->sel];
    return p->have[g] < p->total[g] ? PROMPTS_GET : PROMPTS_UPDATE;
}

// The base's rows from FOOT_Y0 down as they were before the footer went on, so a changed footer is painted on clean board (painting it over the old one darkens
// its shadow every time)
#define FOOT_Y0 630
static uint8_t *g_foot_under;
const char *g_ui_icons_a_says; // tests: what the footer's A says now

static void put_footer(const Page *p, int keep_under)
{
    size_t off = (size_t)FOOT_Y0 * (size_t)G.base->w * 4, n = (size_t)(G.base->h - FOOT_Y0) * (size_t)G.base->w * 4;
    if (keep_under) {
        if (!g_foot_under) g_foot_under = malloc(n);
        if (g_foot_under) memcpy(g_foot_under, G.base->px + off, n);
    } else if (g_foot_under) {
        memcpy(G.base->px + off, g_foot_under, n);
    }
    Cx c = ui_cx(G.base);
    skin_footer(&c, prompts_for(p), 3);
    g_ui_icons_a_says = prompts_for(p)[0].label;
}

// ---- the slip

typedef struct {
    const Page *p;
} SumJob;

static void summary_body(Surf *s, float ox, float oy, void *ud)
{
    const Page *p = ((const SumJob *)ud)->p;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Col ink = hex(PAL(text, PAPER_TEXT), 1), soft = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);
    char line[128], size[32], more[64];
    snprintf(line, sizeof line, "%d of %d games on your card", p->on_card, p->ng);
    skin_txt(&c, u->semi, 29, -0.2f, ox + 28, oy + 48, 0, ink, line);
    uint64_t on_card_bytes = 0, missing_bytes = 0;
    int missing_games = 0;
    for (int g = 0; g < p->ng; g++) {
        uint64_t b = icons_repo_bytes(g_repo, g);
        if (p->total[g] > 0) {
            on_card_bytes += b / (uint64_t)p->total[g] * (uint64_t)p->have[g];
            if (p->have[g] < p->total[g]) {
                missing_games++;
                missing_bytes += b / (uint64_t)p->total[g] * (uint64_t)(p->total[g] - p->have[g]);
            }
        }
    }
    ui_format_bytes(on_card_bytes, size, sizeof size);
    snprintf(line, sizeof line, "From github.com/" ICONS_REPO "  " DOT "  %s on the SD card", size);
    skin_txt(&c, u->med, 17, 0, ox + 28, oy + 74, 0, soft, line);
    // Get all: a paper button with the X mark
    float bx = ox + BW - 262, by = oy + 22;
    gfx_rrect(s, bx, by, 236, 46, 8, hex(0xD3F0DF, missing_games ? 1 : 0.5f));
    gfx_circle(s, bx + 28, by + 23, 14, hex(PAL(x_fill, 0xE8F8E0), 1));
    gfx_ring(s, bx + 28, by + 23, 13.75f, 2.5f, hex(PAL(glyph_ring, PAPER_TEXT_SOFT), 1));
    skin_txtc(&c, u->semi, 16, 0, bx + 28, by + 23, 1, hex(PAL(glyph_ink, PAPER_TEXT), 1), "X");
    skin_txtc(&c, u->semi, 19, 0, bx + 52, by + 15, 0, hex(PAPER_TEXT, 1), "Get all missing");
    ui_format_bytes(missing_bytes, size, sizeof size);
    if (missing_games) snprintf(more, sizeof more, "%d game%s " DOT " %s", missing_games, missing_games == 1 ? "" : "s", size);
    else snprintf(more, sizeof more, "nothing missing");
    skin_txtc(&c, u->med, 14.5f, 0, bx + 52, by + 34, 0, hex(PAPER_TEXT_SOFT, 1), more);
    // one piece of tape for every game, in the order of the rows: stuck down where it is on the card, an empty outline where it is not
    const float sy = oy + 112, gap = p->ng > 24 ? 2.0f : 6.0f, segw = p->ng > 0 ? (BW - 56 - (p->ng - 1) * gap) / (float)p->ng : 0;
    for (int i = 0; i < p->ng; i++) {
        const int g = p->ord[i], st = page_state(p, g);
        float sx = ox + 28 + i * (segw + gap);
        float frac = st == 0 ? 1.0f : st == 3 ? (float)p->have[g] / (float)p->total[g] : st == 2 ? p->dl_frac : 0.0f;
        if (st != 0) scene_dashed_rrect(s, sx, sy, segw, 16, segw < 14 ? 2.0f : 4.0f, 1.4f, segw < 14 ? 3.0f : 5.0f, segw < 14 ? 2.0f : 4.0f, hex(PAL(thread, PAPER_THREAD), 1));
        if (frac > 0.01f) {
            float w = segw * frac;
            if (segw >= 24 && w >= 24) {
                PaperTape t = {(int)w, 16, paper_tape_rgb[(i * 3 + 1) & 7], KINDS[i & 3], PAPER_TAPE_ZIG, 0.0f, 500u + (uint32_t)i};
                PaperSpr *sp = paper_tape(&t);
                if (sp) paper_spr_put(s, sp, (int)(sx + w / 2), (int)(sy + 8));
                paper_spr_free(sp);
            } else {
                gfx_rrect(s, sx, sy, w, 16, 3, hex(paper_tape_rgb[(i * 3 + 1) & 7], 1));
            }
        }
    }
}

// the little caret under the piece of tape of the row with the focus (drawn on every frame, over the slip)
static void put_caret(Surf *s, const Page *p)
{
    if (p->ng <= 0) return;
    const float gap = p->ng > 24 ? 2.0f : 6.0f, segw = (BW - 56 - (p->ng - 1) * gap) / (float)p->ng;
    float mx = BX + 28 + p->sel * (segw + gap) + segw / 2, sy = SUM_Y + 112;
    gfx_line(s, mx - 5, sy + 25, mx, sy + 19, 2.4f, hex(PAL(ink, PAPER_INK), 1));
    gfx_line(s, mx + 5, sy + 25, mx, sy + 19, 2.4f, hex(PAL(ink, PAPER_INK), 1));
}

static void render_base(const Page *p)
{
    scene_chrome(G.base, "Extra icons", NULL, NULL, 0);
    put_footer(p, 1);
    ui_order_chip(G.base, CHIP_X, CHIP_Y, CHIP_W, SORTS[g_sort], g_sort);
    SceneTape tp[2] = {{{70, 24, paper_tape_rgb[4], PAT_STRIPES, PAPER_TAPE_ZIG, -36.0f, 301u}, -16, -6}, {{70, 24, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 36.0f, 302u}, BW - 54, -6}};
    SumJob sj = {p};
    scene_card_on(G.base, BX, SUM_Y, BW, SUM_H, 6.0f, 71u, 0.0f, 0, tp, 2, summary_body, &sj);
}

// ---- the shuffle: a new order carries every row that shows from where it was to its new place, one after another, each lifting a little and bowing
// out to one side on the way so they pass each other; a row from out of sight comes in from the window's edge
#define RSH_MS 340.0
#define RSH_STEP_MS 18.0
#define RSH_BOW 26.0f
static struct {
    float *from; // by game: the screen y its row was at when the order changed (NULL: no shuffle)
    uint64_t t0;
    int first;   // the first row that shows in the new order (the others start after it, one by one)
} g_shf;

static void shf_end(void)
{
    free(g_shf.from);
    memset(&g_shf, 0, sizeof g_shf);
}

static double shf_ease(double u) { return u < 0.5 ? 4 * u * u * u : 1 - pow(-2 * u + 2, 3) / 2; }

// row i (game g) now: where it is drawn and how far up it is; 0 when it is not moving
static int shf_row(int i, int g, float *y, float *x, float *lift)
{
    if (!g_shf.from) return 0;
    int d = i - g_shf.first;
    d = d < 0 ? 0 : d > 10 ? 10 : d;
    double u = ((double)ui_now() - (double)g_shf.t0 - d * RSH_STEP_MS) / RSH_MS;
    if (u >= 1.0) return 0;
    const float lo = VIEW_TOP - ROW_H - 12, hi = VIEW_BOT + 12;
    float y0 = g_shf.from[g], y1 = *y;
    y0 = y0 < lo ? lo : y0 > hi ? hi : y0;
    float yt = y1 < lo ? lo : y1 > hi ? hi : y1;
    if (fabsf(yt - y0) < 0.5f) return 0;
    float e = u <= 0 ? 0.0f : (float)shf_ease(u), hump = sinf(3.14159265f * e);
    *y = y0 + (yt - y0) * e;
    *x = BX + (yt > y0 ? RSH_BOW : -RSH_BOW) * hump;
    *lift = hump * 0.9f;
    return 1;
}

static int shf_running(const Page *p)
{
    if (!g_shf.from) return 0;
    int d = p->ng - g_shf.first;
    d = d < 0 ? 0 : d > 10 ? 10 : d;
    if ((double)ui_now() - (double)g_shf.t0 < d * RSH_STEP_MS + RSH_MS + 20) return 1;
    shf_end();
    return 0;
}

// scroll = how far the rows have scrolled (pixels); the lifted one is drawn (lx, ly) off its place (the lean)
static void page_frame(const Page *p, RowSprs *rs, float scroll, const Hops *hp, int lx, int ly)
{
    rs->frame++;
    surf_copy(G.work, G.base);
    Cx c = ui_cx(G.work);
    for (int pass = 0; pass < 2; pass++) // (the rows lying still first, then the ones on their way, over them)
        for (int i = 0; i < p->ng; i++) {
            const int g = p->ord[i];
            float y = ROW_Y0 + i * ROW_P - scroll, x = BX, lift = 0;
            int mv = shf_row(i, g, &y, &x, &lift);
            if (mv != pass) continue;
            if (y > VIEW_BOT + 20 || y + ROW_H < VIEW_TOP - 20) continue;
            float f = focus_on(hp, i), ox = i == p->sel && !mv ? (float)lx : 0, oy = i == p->sel && !mv ? (float)ly : 0;
            int xi = (int)lroundf(x + ox), yi = (int)lroundf(y + oy);
            if (mv) { // (on its way: up off the page, no patch left anywhere)
                // (the lifted look, its card risen 7 px, let down to where the row is by as much as it is not up; faded in over the row as it
                // sits while it is still low, so nothing is see-through and the writing never shows twice)
                f = f > lift ? f : lift;
                int ly2 = yi + (int)lroundf(7.0f * (1.0f - f)), a = f * 4.0f >= 1.0f ? 256 : (int)(f * 4.0f * 256.0f);
                if (a < 256) spr_blit(c.s, row_spr(p, rs, g, 0), xi, ly2 - 7, 256); // (risen with it, so the two cards are one)
                spr_blit(c.s, row_spr(p, rs, g, 1), xi, ly2, a);
            } else if (g == p->dl_game) { // (it changes every frame: drawn whole)
                RowJob job = {p, g, f > 0.5f};
                draw_row(&c, &job, (float)xi, (float)yi);
            } else if (f <= 0.004f) {
                spr_blit(c.s, row_spr(p, rs, g, 0), xi, yi, 256);
            } else {
                const PaperSpr *gh = ghost_get((int)BW, (int)ROW_H, 7.0f);
                if (gh) paper_spr_xform(c.s, gh, xi + BW * 0.5f, yi + ROW_H * 0.5f, 1.0f, 0.0f, f);
                if (f < 0.996f) spr_blit(c.s, row_spr(p, rs, g, 0), xi, yi, (int)((1.0f - f) * 256.0f));
                spr_blit(c.s, row_spr(p, rs, g, 1), xi, yi, f >= 0.996f ? 256 : (int)(f * 256.0f));
            }
        }
    // what runs past the window goes back under the slip and the footer, and its edges fade into the page
    const Surf *s = G.base;
    size_t row = (size_t)G.work->w * 4;
    memcpy(G.work->px + row * (size_t)SUM_Y, s->px + row * (size_t)SUM_Y, row * (size_t)(VIEW_TOP - SUM_Y));
    memcpy(G.work->px + row * (size_t)VIEW_BOT, s->px + row * (size_t)VIEW_BOT, row * (size_t)(G.work->h - (int)VIEW_BOT));
    for (int k = 0; k < FADE; k++) {
        float keep = (k + 0.5f) / FADE;
        fade_row(G.work, s, (int)VIEW_TOP + k, keep);
        fade_row(G.work, s, (int)VIEW_BOT - 1 - k, keep);
    }
    put_caret(G.work, p);
}

// where to scroll so row i shows whole (the first row brings the top of the page)
static float scroll_for(const Page *p, int i, float cur)
{
    float y0 = i * ROW_P - 12, y1 = i * ROW_P + ROW_H + 12, view = VIEW_BOT - ROW_Y0, max = p->ng * ROW_P - view + 24, s = cur;
    if (max < 0) max = 0;
    if (y1 > s + view) s = y1 - view;
    if (y0 < s) s = y0;
    return s < 0 ? 0 : s > max ? max : s;
}

static float page_max(const Page *p)
{
    float max = p->ng * ROW_P - (VIEW_BOT - ROW_Y0) + 24;
    return max < 0 ? 0 : max;
}

// a download shows itself on the page: the row's bar, and the game's piece of tape filling
static RowSprs g_rs;
static float g_scroll;
static Hops *g_hp;
static int g_drawn_q = -1;

static void page_progress(int g, float frac)
{
    Page *p = g_pg;
    if (!p) return;
    p->dl_game = g;
    p->dl_frac = frac;
    int q = (int)(frac * 16.0f) + 100 * g;
    if (q != g_drawn_q) { // (the slip's tape is made again a step at a time, not at every icon)
        g_drawn_q = q;
        render_base(p);
    }
    page_frame(p, &g_rs, g_scroll, g_hp, 0, 0);
    ui_present(G.work);
}

static void page_progress_end(void)
{
    if (g_pg) g_pg->dl_game = -1;
    g_drawn_q = -1;
}

// ---------------------------------------------------------------------------------------------------- the screen
// what is on the card now (a download or a removal changed it): read again, and the page shows it at once (the picker shows it too)
static void page_refresh(Page *pg, Hops *hp, Mover *page)
{
    page_load(pg);
    rowsprs_free(&g_rs, pg->ng);
    rowsprs_new(&g_rs, pg->ng);
    hops_snap(hp, (float)pg->sel, 0);
    mo_snap(page, scroll_for(pg, pg->sel, page->x), 1.0f);
    g_scroll = page->x;
    render_base(pg);
    page_frame(pg, &g_rs, page->x, hp, 0, 0);
    ui_present(G.work);
}

void ui_icons_admin(void)
{
    if (G.quit || !ui_icon_dir()[0]) return;
    if (!g_repo && !fetch_repo()) return;
    if (icons_repo_games(g_repo) == 0) {
        ui_message(UI_INFO, "Extra icons", "GitHub lists no icons.");
        icons_repo_free(g_repo);
        g_repo = NULL;
        return;
    }
    Page pg;
    memset(&pg, 0, sizeof pg);
    pg.mode = g_sort;
    if (!page_load(&pg)) {
        page_free(&pg);
        return;
    }
    g_pg = &pg;
    rowsprs_new(&g_rs, pg.ng);
    Hops hp;
    memset(&hp, 0, sizeof hp);
    hops_snap(&hp, (float)pg.sel, 0);
    g_hp = &hp;
    Mover page;
    mo_snap(&page, scroll_for(&pg, pg.sel, 0), 1.0f);
    g_scroll = page.x;
    render_base(&pg);
    ui_transition();
    Bump bump = {0};
    uint64_t last = ui_now(), pressed_at = 0;
    int touching = 0;
    for (;;) {
        uint64_t now = ui_now();
        float dt = now > last ? (float)(now - last) / 1000.0f : 0.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        int moving = mo_step(&page, dt);
        moving |= hops_step(&hp, dt);
        moving |= shf_running(&pg);
        float lox, loy;
        moving |= bump_offset(&bump, &lox, &loy);
        g_scroll = page.x;
        g_ui_icons_first = pg.ord[0];
        page_frame(&pg, &g_rs, page.x, &hp, (int)lroundf(lox), (int)lroundf(loy));
        ui_present(G.work);
        uint64_t b = ui_poll(moving || touching ? 0 : -1);
        if (!moving && !touching) last = ui_now() - 16; // (it was waiting: what starts now starts a frame in, not catching up on the wait)
        if (b == UI_QUIT || G.quit) break;
        if (b & UI_REDRAW) { // the theme changed: the pictures are the old colours
            rowsprs_free(&g_rs, pg.ng);
            rowsprs_new(&g_rs, pg.ng);
            render_base(&pg);
            b &= ~UI_REDRAW;
        }
        int to = -1, act = 0, want_sort = 0, pdy = 0;
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind == TOUCH_DRAG && te.y >= VIEW_TOP && te.y < VIEW_BOT) { // the rows follow the finger
                    mo_hold(&page);
                    page.x = page.x - te.dy < 0 ? 0 : page.x - te.dy > page_max(&pg) ? page_max(&pg) : page.x - te.dy;
                    touching = 1;
                } else if (te.kind == TOUCH_END && touching) { // let go: the focus goes to a row that still shows
                    touching = 0;
                    mo_spring(&page, page.x, MO_PRESS_OM, MO_PRESS_ZETA);
                    int first = (int)ceilf((page.x + 12) / ROW_P), lastr = (int)((page.x + (VIEW_BOT - ROW_Y0) - ROW_H) / ROW_P);
                    int s = pg.sel < first ? first : pg.sel > lastr ? lastr : pg.sel;
                    if (s >= 0 && s < pg.ng && s != pg.sel) to = s;
                } else if (te.kind == TOUCH_TAP) {
                    if (in_rect(te.x, te.y, CHIP_X, CHIP_Y, CHIP_W, 48)) want_sort = 1;
                    else if (te.y >= VIEW_TOP && te.y < VIEW_BOT && te.x >= BX && te.x < BX + BW) {
                        int i = (int)((te.y - ROW_Y0 + page.x) / ROW_P);
                        if (i >= 0 && i < pg.ng && fmodf(te.y - ROW_Y0 + page.x, ROW_P) < ROW_H) {
                            to = i;
                            act = 1;
                        }
                    }
                }
            }
        }
        if (b & BTN_DOWN) {
            if (pg.sel < pg.ng - 1) to = pg.sel + 1;
            else if (pg.ng > 1 && wrap_ok()) to = 0;
            else pdy = 1;
        } else if (b & BTN_UP) {
            if (pg.sel > 0) to = pg.sel - 1;
            else if (pg.ng > 1 && wrap_ok()) to = pg.ng - 1;
            else pdy = -1;
        } else if (b & (BTN_L | BTN_R | BTN_ZL | BTN_ZR)) { // several rows at once
            int by = (b & (BTN_L | BTN_ZL)) ? -5 : 5, t = pg.sel + by;
            t = t < 0 ? 0 : t > pg.ng - 1 ? pg.ng - 1 : t;
            if (t != pg.sel) to = t;
            else pdy = by < 0 ? -1 : 1;
        } else if (b & BTN_A) {
            act = 1;
        } else if (b & BTN_X) {
            act = 2;
        } else if (b & BTN_Y) {
            act = 3;
        } else if (b & BTN_R3) {
            want_sort = 1;
        } else if (b & BTN_B) {
            break;
        }
        if (to >= 0 && to != pg.sel) { // the focus moves: the row slides into view, the one that had it sticks back, the new one peels up
            uint64_t t = ui_now();
            float gap = (float)(t - pressed_at) / 1000.0f, run = !G.plat->repeated ? 0.0f : gap > 0.06f && gap < 0.16f ? gap : 0.1f;
            pressed_at = t;
            ui_lift_sfx(0, G.plat->repeated != 0, LIFT_ROW);
            ui_lift_sfx(1, G.plat->repeated != 0, LIFT_ROW);
            const Prompt *was = prompts_for(&pg);
            pg.sel = to;
            if (prompts_for(&pg) != was) put_footer(&pg, 0);
            if (page.mode == MO_HELD) mo_spring(&page, page.x, MO_PRESS_OM, MO_PRESS_ZETA);
            mo_run(&page, scroll_for(&pg, pg.sel, page.mode == MO_REST ? page.x : page.tgt), MO_PRESS_OM, MO_PRESS_ZETA, run);
            hops_to(&hp, (float)pg.sel, 0);
        } else if (pdy) {
            bump_start(&bump, 0, pdy);
        }
        if (want_sort) {
            int pick = ui_sort_menu(SORTS, 4, g_sort, CHIP_X, CHIP_Y, CHIP_W, 48, 0);
            if (pick >= 0 && pick != g_sort) {
                int g = pg.ord[pg.sel];
                shf_end();
                g_shf.from = malloc(sizeof(float) * (size_t)icons_repo_games(g_repo));
                if (g_shf.from) // (where each row is now: the shuffle starts there)
                    for (int i = 0; i < pg.ng; i++) g_shf.from[pg.ord[i]] = ROW_Y0 + i * ROW_P - page.x;
                g_sort = pg.mode = pick;
                page_order(&pg);
                for (int i = 0; i < pg.ng; i++)
                    if (pg.ord[i] == g) pg.sel = i;
                mo_snap(&page, scroll_for(&pg, pg.sel, 0), 1.0f);
                hops_snap(&hp, (float)pg.sel, 0);
                render_base(&pg);
                g_shf.t0 = ui_now();
                g_shf.first = (int)ceilf((page.x - 12) / ROW_P);
                ui_sound(SND_RIFFLE, 0.42f, 0); // (cards being shuffled)
            }
            last = ui_now();
        }
        if (!act) continue;
        // ---- A (get this game's missing icons, or look for new ones), X (every game that is missing), Y (remove this game's icons)
        ui_screen_done(); // (the dialogs dim this page)
        const int c = pg.ord[pg.sel];
        const char *slug = icons_repo_slug(g_repo, c);
        Fetch f;
        memset(&f, 0, sizeof f);
        if (act == 3) {
            if (pg.have[c] > 0) {
                char head[160], dir[FSX_PATH];
                snprintf(head, sizeof head, "Remove the icons of %s?", pg.title[c]);
                if (ui_confirm(head, "They are deleted from the SD card. A profile that uses one shows its first letter until the icons are downloaded again.", "Remove", "Cancel", 1, NULL, 0)) {
                    ui_busy("Extra icons", "Removing\xE2\x80\xA6");
                    path_of(dir, sizeof dir, slug, NULL);
                    fsx_rmtree(dir);
                }
            } else {
                bump_start(&bump, 0, 1);
                continue;
            }
        } else if (act == 2) { // every game that is missing icons
            uint64_t bytes = 0;
            int games = 0;
            for (int g = 0; g < pg.ng; g++) {
                int m = missing_in(g);
                f.total += m;
                if (m > 0) {
                    games++;
                    bytes += icons_repo_count(g_repo, g) ? icons_repo_bytes(g_repo, g) / (uint64_t)icons_repo_count(g_repo, g) * (uint64_t)m : 0;
                }
            }
            if (f.total == 0) {
                ui_message(UI_INFO, "Nothing to get", "Every game\xE2\x80\x99s icons are already on the SD card.");
                continue;
            }
            char head[120], size[32], d[200];
            ui_format_bytes(bytes, size, sizeof size);
            snprintf(head, sizeof head, "Get the icons of %d game%s?", games, games == 1 ? "" : "s");
            snprintf(d, sizeof d, "%d icons, about %s. It takes a while: Stop (B) keeps what has been downloaded.", f.total, size);
            if (!ui_confirm(head, d, "Get them", "Cancel", 0, NULL, 0)) continue;
            for (int g = 0; g < pg.ng && !f.stopped && !f.gave_up; g++)
                if (missing_in(g) > 0) fetch_game(g, &f);
            page_progress_end();
            page_refresh(&pg, &hp, &page);
            ui_screen_done();
            report(&f);
        } else {
            f.total = missing_in(c);
            if (f.total == 0) {
                ui_message(UI_INFO, "Nothing to get", "All of that game\xE2\x80\x99s icons are already on the SD card.");
                continue;
            }
            fetch_game(c, &f);
            page_progress_end();
            page_refresh(&pg, &hp, &page);
            ui_screen_done();
            report(&f);
        }
        if (act == 3) page_refresh(&pg, &hp, &page);
    }
    shf_end();
    rowsprs_free(&g_rs, pg.ng);
    page_free(&pg);
    free(g_foot_under);
    g_foot_under = NULL;
    g_pg = NULL;
    g_hp = NULL;
    icons_repo_free(g_repo);
    g_repo = NULL;
}
