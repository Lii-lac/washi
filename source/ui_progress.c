// The "switching" screen, in Washi: a paper card on the board with the steps in crayon, the heading underlined, and ONE bar of washi tape in
// a crayon box. A roll runs along the bar laying a piece of tape per game as its saves are copied (paper_roll.c): the pieces lie end to
// end, each its own colour and pattern (never the same twice running), a little crooked, as wide as the game's share of the bytes (the
// plan the locker gives, ui_progress_plan; at least MIN_PIECE px), so the finished bar is the whole swap. Nothing peels away. When a game
// is done its roll is used up (it shrinks away where the tape ended) and the next game's roll drops in over the joint.
//
// How fast: the roll lays no more than ROLL_MAX_PX_S px of tape a second and a strip takes at least MIN_TAPE_MS (pace_cap), so a small save,
// copied in a blink, is still seen being laid; a big copy is slower than that and is not held back. When a game has reported all of it, or
// the locker has gone on to something else, the frames are made inside that call until the strip is laid and cut (progress_run).
//
// How a frame is made (the copy itself runs on this same thread, and the screen is only drawn from the progress callbacks, so a frame has
// to be cheap and rare). The board, the top bar, the card with its shadow and tapes, and the footer strip are made once (`chrome`; again after
// the theme or the board's ink changes). The
// steps, heading, icon, game name, guide and the list of finished games are drawn over a copy of that when something changes (the base,
// G.base). Everything that moves (strip, roll, the two figures under the bar) is drawn over the base in dirty rectangles only, and only
// those rectangles are presented. Frames are spaced by what the last one cost, so drawing never takes more than about a quarter of the copy's
// time. Only the effects at a game's end (peel, hop, the next roll appearing) are played out in a short run of frames, and they have a time
// budget per step so a long list of games is never slowed much.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dirty.h"
#include "gfx/paper.h"
#include "gfx/paper_roll.h"
#include "gfx/crayon.h"
#include "ui_internal.h"

#define MAX_STEPS 4
#define ICON_PX 80

// the card (the mockup's numbers), turned a hair; everything printed on it is drawn level
#define CARD_X 250
#define CARD_Y 130
#define CARD_W 780
#define CARD_H 450
#define TILT_DEG (-0.5f)
#define STEPS_X 298
#define STEPS_W 684
#define STEPS_CY 197
#define HEAD_X 298
#define HEAD_CY 270
#define UL_Y 302
#define MARK_X 298 // the icon square
#define MARK_Y 334
#define MARK_S 88
#define NAME_X 434
#define NAME_CY 337
#define GUIDE_X 434 // the crayon box that hugs the bar (as in washi-app.html)
#define GUIDE_Y 378
#define GUIDE_W 540
#define GUIDE_H 36
#define BAR_X 445 // the bar inside it: 11 px in at each end, 28 px of tape on a middle line of 396
#define BAR_W 518
#define BAR_CY 396
#define BAR_TH 28
#define MIN_PIECE 26 // a tiny save still gets a piece this wide (the others give up the width pro rata)
#define JOINT_OV 4   // a piece runs this far under the next one, so the joints just touch
#define LONG_PIECE 200 // a piece this long (px) is treated as a lone strip: it may be pressed down with a ridge, and lie a little more crooked
#define PIECES_MAX 256
#define PLAN_MAX 1024
#define TXT_CY 453 // the two figures under the guide
#define LIST_Y 482
#define LIST_X 298
#define LIST_R 982
#define FX_BUDGET_MS 1600 // time one step may spend on game-end effects
#define FX_MIN_GAME_MS 150 // a game that took less than this gets no effects at all: only its colour changes (none does now: see ROLL_MAX_PX_S)
#define MIN_GAP_MS 40
#define ROLL_MAX_PX_S 260.0f // the roll lays tape no faster than this, so a small save is still seen being laid (a big copy is slower anyway)
#define MIN_TAPE_MS 200.0f   // and a strip takes at least this long
#define SLIP_GAP_MS 90       // the taps of the turning roll are at least this far apart
// the two figures under the bar: "42%" at its left, "4.1 of 9.8 MB" at its right
static const Rect T_PCT = {430, 437, 130, 32};
static const Rect T_SIZE = {674, 437, 304, 32};

// ---- tests look at these
long g_prg_frames, g_prg_rect_frames, g_prg_full_frames, g_prg_fx_frames, g_prg_laid, g_prg_hops, g_prg_catches, g_prg_back, g_prg_strips, g_prg_drops;
int g_prg_colour[256], g_prg_kind[256];
int g_prg_nocopy[256];  // 1: that strip is a game with nothing to copy (no roll)
float g_prg_pace[256];  // the least time (ms) each strip's roll takes to lay it
// the pieces of the bar (the current step): where each one starts and ends on the screen, how crooked it is, and the bar's right edge
int g_prg_piece_x0[PIECES_MAX], g_prg_piece_x1[PIECES_MAX];
float g_prg_piece_rot[PIECES_MAX];
int g_prg_npieces;
float g_prg_last_disp; // the roll's progress as last drawn
long g_prg_zero_games;
double (*g_prg_clock_ms)(void); // tests: a real clock, to time the frames
double g_prg_ms_rect, g_prg_ms_full, g_prg_ms_rect_max, g_prg_ms_base, g_prg_ms_bake;
long g_prg_n_base, g_prg_n_bake;

typedef struct { char name[96]; uint64_t bytes; } Done;

static struct {
    int active;
    char title[96];
    char steps[MAX_STEPS][64];
    int nsteps, step;
    char game[96];
    uint64_t game_id;
    uint64_t cur, total;
    Done done[2]; // the last two finished (the list under the bar)
    int ndone;
    int dirty_base;

    RollStrip *S;       // this game's piece and roll
    RollStrip *laid[PIECES_MAX]; // the finished pieces of this step, left to right (drawn into the base)
    int nlaid;
    int kstep;          // the game's number within this step: its place in the plan
    uint64_t plan[PLAN_MAX]; // the bytes of each game this step will report (plan_n 0: not known) and where its piece starts
    int plan_n;
    unsigned plan_serial;
    int edge[PLAN_MAX + 1];  // edge[k]..edge[k + 1] is piece k's run on the screen (edge[plan_n] is the bar's right end)
    int next_x;         // where the next piece that is not in the plan starts
    uint64_t g_t0;      // when this game's piece started
    uint64_t prev_ms;   // how long the game before it took
    PaperSpr *mark;    // the icon square: scribble in the strip's colour, the icon or its initials, turned a little
    RollPick prev_pick;
    uint32_t seed;
    int gidx;
    float p_real;       // the real progress, 0..1, only ever grows within a strip
    float p_shown;      // the progress as paced (never ahead of p_real, nor of what the roll may have laid by now)
    float pace_ms;      // the least time this strip's roll takes to lay it (0: no pacing)
    uint64_t lay_t0;    // when the roll started laying (the pace runs from here)
    float disp;         // what was last drawn (the roll's place)
    int cut_drawn;      // the strip has been drawn finished
    int roll_off;       // the roll is not drawn (it has hopped off)
    int cidx;           // the catch being shown (-1: none) and when it began
    uint64_t ct0;
    int fx_drawn;       // the last frame drawn showed a catch
    char d_pct[16], d_size[64]; // what the two figures last showed
    uint64_t next_draw;
    int fx_left;        // ms of game-end effects this step may still play
} P;

enum { PRG_EV_ROLL_ON, PRG_EV_CATCH, PRG_EV_CUT };
static void progress_hook(int ev, int kind, float strength);
static float g_slip_disp; // how far the roll was when it last tapped
static uint64_t g_slip_t;  // and when
static int g_slip_n;

// ------------------------------------------------------------------------------------------------ small helpers

static float hashf(int x, int y, uint32_t s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)(h & 0xFFFFFF) / 16777216.0f;
}

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

// wax: the crayon's grain eats into the stroke (the same recipe as paper.c's marks)
static void wax(PaperSpr *p, float gain, float bias, float opacity, uint32_t seed)
{
    if (!p) return;
    for (int y = 0; y < p->h; y++)
        for (int x = 0; x < p->w; x++) {
            uint8_t *q = p->px + ((size_t)y * p->w + x) * 4;
            if (!q[3]) continue;
            float u = hashf(x, y, seed), v = hashf(x >> 1, y >> 1, seed + 17);
            float R = 0.5f + 0.45f * ((u - 0.5f) * 0.55f + (v - 0.5f) * 0.9f);
            q[3] = (uint8_t)clampf(q[3] * clampf(gain * R - bias, 0, 1) * opacity + 0.5f, 0, 255);
        }
}

static void dash_line(Surf *s, float x0, float y0, float x1, float y1, float dash, float gap, float w, Col c)
{
    float dx = x1 - x0, dy = y1 - y0, L = sqrtf(dx * dx + dy * dy);
    if (L < 1.0f) return;
    float ux = dx / L, uy = dy / L;
    for (float t = 0; t < L; t += dash + gap) {
        float e = t + dash < L ? t + dash : L;
        gfx_line(s, x0 + ux * t, y0 + uy * t, x0 + ux * e, y0 + uy * e, w, c);
    }
}

static int rects_touch(const Rect *a, const Rect *b)
{
    return a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}
static int rect_in(const Rect *inner, const Rect *outer)
{
    return inner->x >= outer->x && inner->y >= outer->y && inner->x + inner->w <= outer->x + outer->w && inner->y + inner->h <= outer->y + outer->h;
}
static Rect rect_of_roll(RollRect r)
{
    Rect o = {r.x, r.y, r.w, r.h};
    return o;
}

static void figures(char *pct, size_t pn, char *size, size_t sn)
{
    pct[0] = size[0] = 0;
    if (!P.game[0]) return;
    if (P.total == 0) {
        snprintf(size, sn, "Nothing to copy");
        return;
    }
    char a[24], b[24];
    snprintf(pct, pn, "%d%%", (int)(P.p_shown * 100.0f));
    // (what the roll has laid: nothing at the start, the real count when it is keeping up, else the share it has laid)
    uint64_t shown = P.p_shown <= 0.0f ? 0 : P.p_shown >= P.p_real ? P.cur : (uint64_t)((double)P.p_shown * (double)P.total);
    ui_format_bytes(shown > P.total ? P.total : shown, a, sizeof a);
    ui_format_bytes(P.total, b, sizeof b);
    snprintf(size, sn, "%s of %s", a, b);
}

// ------------------------------------------------------------------------------------------------ icons

// decoded icons, so a game's icon is only decoded once
// (the icons are painted over in crayon once, when first wanted, and kept for the rest of the run: backing up the same games again is free)
enum { ICON_CACHE = 24 };
static struct { uint64_t id; uint8_t *rgba; int tried; } icons[ICON_CACHE];

static const uint8_t *icon_for(uint64_t id)
{
    if (!id || !G.icon_fn) return NULL;
    int free_slot = -1;
    for (int i = 0; i < ICON_CACHE; i++) {
        if (icons[i].tried && icons[i].id == id) return icons[i].rgba;
        if (!icons[i].tried && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) { // full: forget the first
        free(icons[0].rgba);
        memmove(&icons[0], &icons[1], (ICON_CACHE - 1) * sizeof icons[0]);
        memset(&icons[ICON_CACHE - 1], 0, sizeof icons[0]);
        free_slot = ICON_CACHE - 1;
    }
    const uint8_t *jpeg = NULL;
    size_t n = 0;
    uint8_t *rgba = NULL;
    if (G.icon_fn(G.icon_user, id, &jpeg, &n) == 0 && jpeg && n > 0) rgba = gfx_decode_icon(jpeg, n, ICON_PX);
    if (rgba) { // the picture, redrawn in wax crayon
        uint8_t *painted = crayon_filter(rgba, ICON_PX, (uint32_t)(id ^ (id >> 32)) * 2654435761u + 11u);
        if (painted) {
            free(rgba);
            rgba = painted;
        }
    }
    icons[free_slot].id = id;
    icons[free_slot].rgba = rgba;
    icons[free_slot].tried = 1;
    return rgba;
}

// capitals and digits of the name (up to three: "MK8"), else its first letter
static void initials(const char *name, char *out, size_t cap)
{
    int n = 0;
    for (const char *p = name; *p && n < 3 && (size_t)n + 1 < cap; p++)
        if ((*p >= 'A' && *p <= 'Z') || (*p >= '0' && *p <= '9')) out[n++] = *p;
    if (n == 0) {
        const char *p = name;
        while (*p == ' ') p++;
        if (*p) {
            int len = 1;
            if ((unsigned char)*p >= 0xF0) len = 4;
            else if ((unsigned char)*p >= 0xE0) len = 3;
            else if ((unsigned char)*p >= 0xC0) len = 2;
            for (int i = 0; i < len && p[i] && (size_t)n + 1 < cap; i++) out[n++] = p[i];
            if (len == 1 && *p >= 'a' && *p <= 'z') out[0] = (char)(*p - 32);
        } else {
            out[n++] = '?';
        }
    }
    out[n] = 0;
}

enum { MARK_PAD = 24, MARK_W = MARK_S + 2 * MARK_PAD };

typedef struct {
    const PaperSpr *scribble;
    const uint8_t *icon;
    const char *name;
} MarkBake;

static void draw_mark(Surf *s, void *ud)
{
    const MarkBake *b = ud;
    Cx c = ui_cx(s);
    float mid = MARK_W * 0.5f;
    if (b->scribble) paper_spr_put(s, b->scribble, (int)mid, (int)mid);
    if (b->icon) {
        gfx_blit_rrect(s, b->icon, ICON_PX, mid - ICON_PX * 0.5f, mid - ICON_PX * 0.5f, ICON_PX * 0.23f);
        return;
    }
    if (!b->name[0]) return;
    char ini[16];
    initials(b->name, ini, sizeof ini);
    int n = (int)strlen(ini);
    skin_txtc(&c, c.skin->semi, n >= 3 ? 30.0f : n == 2 ? 36.0f : 42.0f, 0, mid, mid, 1, hex(PAPER_TEXT, 1), ini);
}

static void draw_turn(Surf *s, void *ud)
{
    const PaperSpr *p = ud;
    paper_spr_xform(s, p, MARK_W * 0.5f, MARK_W * 0.5f, 1.0f, -2.5f * 3.14159265f / 180.0f, 1.0f);
}

// the icon square for a game: the scribble in `rgb`, the picture (or initials) on it, turned -2.5 degrees like the mockup's
static PaperSpr *mark_make(const char *name, uint64_t id, uint32_t rgb, uint32_t seed)
{
    PaperSpr *scr = paper_scribble(MARK_S, rgb, &PAPER_CRAYON_STD, seed);
    MarkBake mb = {scr, name && name[0] ? icon_for(id) : NULL, name ? name : ""};
    PaperSpr *flat = paper_bake(MARK_W, MARK_W, draw_mark, &mb);
    PaperSpr *out = flat ? paper_bake(MARK_W, MARK_W, draw_turn, flat) : NULL;
    paper_spr_free(flat);
    paper_spr_free(scr);
    return out;
}

// ------------------------------------------------------------------------------------------------ crayon pieces, cached

typedef struct { int len; PaperSpr *s; } LenSpr;
static PaperSpr *m_cur[MAX_STEPS], *m_up[MAX_STEPS], *m_done[MAX_STEPS];
static LenSpr l_done[MAX_STEPS], l_todo[MAX_STEPS], ul;
static PaperSpr *g_guide;
static Surf *g_chrome;
static const void *g_chrome_key;

// The palette changed since the pieces were made (the theme did): they all go, to be made again in its colours.
static void prg_pal_check(void)
{
    static unsigned serial = 1; // (the palette the app starts with: nothing made yet to throw away)
    if (!paper_pal_stale(&serial)) return;
    for (int i = 0; i < MAX_STEPS; i++) {
        paper_spr_free(m_cur[i]);
        paper_spr_free(m_up[i]);
        paper_spr_free(m_done[i]);
        paper_spr_free(l_done[i].s);
        paper_spr_free(l_todo[i].s);
        m_cur[i] = m_up[i] = m_done[i] = NULL;
        l_done[i] = l_todo[i] = (LenSpr){0, NULL};
    }
    paper_spr_free(ul.s);
    ul = (LenSpr){0, NULL};
    paper_spr_free(g_guide);
    g_guide = NULL;
    g_chrome_key = NULL; // (the chrome is made again)
}

static void draw_disc(Surf *s, void *ud)
{
    (void)ud;
    gfx_circle(s, 22, 22, 16.4f, hex(PAL(ink, PAPER_INK), 1));
}

static PaperSpr *bake_done_node(uint32_t seed)
{
    PaperSpr *d = paper_bake(44, 44, draw_disc, NULL);
    if (!d) return NULL;
    wax(d, 4.6f, 1.1f, 1.0f, seed * 7 + 7);
    uint32_t tk = pal_light(PAL(ink, PAPER_INK)) ? PAL(ink_on, 0xFFFFFF) : PAPER_PAPER; // (Paper night's pale ink takes a dark tick)
    const PaperCrayon CRAYON_WHITE = {tk, tk, 0.95f, 0.5f, 0.9f};
    PaperSpr *tick = paper_tick(17.0f, &CRAYON_WHITE, seed);
    if (tick) paper_spr_over(d, tick, 22, 22);
    paper_spr_free(tick);
    d->ox = d->oy = 22;
    return d;
}

typedef struct { int len, done; uint32_t seed; } LinkBake;

static void draw_link(Surf *s, void *ud)
{
    const LinkBake *b = ud;
    PaperRng r;
    paper_rng_seed(&r, b->seed);
    float wave = (paper_rng_f(&r) - 0.5f) * 6.0f, x1 = (float)b->len;
    if (b->done) {
        Col k = hex(PAL(ink, PAPER_INK), 1);
        float px = 0, py = 17;
        for (int i = 1; i <= 16; i++) {
            float t = i / 16.0f, y = 17 + 8.0f * wave * t * (1 - t), x = x1 * t;
            gfx_line(s, px, py, x, y, 3.0f, k);
            px = x;
            py = y;
        }
        gfx_line(s, x1 - 8, 10, x1, 17, 3.0f, k);
        gfx_line(s, x1, 17, x1 - 8, 24, 3.0f, k);
    } else {
        dash_line(s, 0, 17 + wave * 0.5f, x1, 17 - wave * 0.5f, 7, 7, 2.3f, hex(PAL(thread, PAPER_THREAD), 1));
    }
}

static PaperSpr *bake_link(int len, int done, uint32_t seed)
{
    LinkBake lb = {len, done, seed};
    PaperSpr *p = paper_bake(len + 6, 34, draw_link, &lb);
    if (!p) return NULL;
    wax(p, 3.4f, 0.85f, done ? 1.0f : 0.85f, seed + 3);
    p->ox = 0;
    p->oy = 17;
    return p;
}

// the crayon box round the bar: four sides each drawn on its own so the corners cross, as in the mockup
static void draw_guide(Surf *s, void *ud)
{
    (void)ud;
    PaperRng r;
    paper_rng_seed(&r, 77);
    float x = 12, y = 12, w = (float)GUIDE_W, h = (float)GUIDE_H;
#define J (((int)((paper_rng_f(&r) - 0.5f) * 20.0f)) / 10.0f)
    Col k = hex(PAL(thread, PAPER_THREAD), 1);
    dash_line(s, x - 4, y + J, x + w + 3, y + J, 9, 7, 2.5f, k);
    dash_line(s, x + w + J, y - 3, x + w + J, y + h + 4, 9, 7, 2.5f, k);
    dash_line(s, x + w + 4, y + h + J, x - 3, y + h + J, 9, 7, 2.5f, k);
    dash_line(s, x + J, y + h + 3, x + J, y - 4, 9, 7, 2.5f, k);
#undef J
}

// ------------------------------------------------------------------------------------------------ chrome (made once)

static void chrome_build(Surf *dst)
{
    Cx c = ui_cx(dst);
    skin_board(&c);
    skin_top_bar(&c, "Please wait", NULL);
    float cx = CARD_X + CARD_W * 0.5f, cy = CARD_Y + CARD_H * 0.5f, tilt = TILT_DEG * 3.14159265f / 180.0f;
    gfx_shadow(dst, CARD_X, CARD_Y, CARD_W, CARD_H, 6, 10, 16, hex(PAL(shadow, PAPER_SHADOW), 0.34f * PAL(shadow_k, 1.0f)));
    PaperSpr *card = paper_card_ex(CARD_W, CARD_H, 6, 5u, PAPER_CARD_PAD);
    if (card) paper_spr_xform(dst, card, cx, cy, 1.0f, tilt, 1.0f);
    paper_spr_free(card);
    // the two strips that hold it (the mockup's: pink stripes on the left, blue dots on the right)
    PaperTape ta = {100, 28, paper_tape_rgb[0], PAT_STRIPES, PAPER_TAPE_ZIG, -36.0f, 41}, tb = {100, 28, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG, 36.0f, 42};
    paper_tape_host(&ta, (float)CARD_W, (float)CARD_H, 6.0f, -22.0f, -8.0f);
    paper_tape_host(&tb, (float)CARD_W, (float)CARD_H, 6.0f, (float)CARD_W - 78.0f, -8.0f);
    PaperSpr *sa = paper_tape(&ta), *sb = paper_tape(&tb);
    PaperXf cf = {cx, cy, 1.0f, tilt};
    float dxa, dya, dxb, dyb;
    paper_tape_offset((float)CARD_W, (float)CARD_H, -22.0f, -8.0f, 100.0f, 28.0f, &dxa, &dya);
    paper_tape_offset((float)CARD_W, (float)CARD_H, (float)CARD_W - 78.0f, -8.0f, 100.0f, 28.0f, &dxb, &dyb);
    if (sa) {
        PaperXf t = paper_xf_attach(cf, dxa, dya);
        paper_spr_xform(dst, sa, t.x, t.y, 1.0f, t.angle, 1.0f);
    }
    if (sb) {
        PaperXf t = paper_xf_attach(cf, dxb, dyb);
        paper_spr_xform(dst, sb, t.x, t.y, 1.0f, t.angle, 1.0f);
    }
    paper_spr_free(sa);
    paper_spr_free(sb);
    skin_footer(&c, NULL, 0);
    skin_txtc(&c, c.skin->med, 20, 0, 640, 683, 1, c.skin->th.text2, "Keep the console on. Don\xE2\x80\x99t close Washi.");
}

// ------------------------------------------------------------------------------------------------ the base

static void draw_steps(Cx *c)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    int n = P.nsteps;
    if (n < 1) return;
    char lab[MAX_STEPS][96];
    float lw[MAX_STEPS], tot = 0;
    for (int i = 0; i < n; i++) {
        snprintf(lab[i], sizeof lab[i], "%s", P.steps[i]);
        lw[i] = text_width(u->semi, 20, 0, lab[i]);
        tot += 44 + lw[i];
    }
    float room = STEPS_W - (n - 1) * 56.0f; // at least a short link between neighbours
    if (tot > room && tot > 44.0f * n) {    // long names: every label gives up the same share
        float k = (room - 44.0f * n) / (tot - 44.0f * n);
        if (k < 0.2f) k = 0.2f;
        for (int i = 0; i < n; i++) {
            char fit[96];
            text_fit(u->semi, 20, 0, P.steps[i], lw[i] * k, fit, sizeof fit);
            snprintf(lab[i], sizeof lab[i], "%s", fit);
            lw[i] = text_width(u->semi, 20, 0, lab[i]);
        }
        tot = 0;
        for (int i = 0; i < n; i++) tot += 44 + lw[i];
    }
    float space = n > 1 ? (STEPS_W - tot) / (n - 1) : 0, x = STEPS_X, cell[MAX_STEPS], cy = STEPS_CY;
    for (int i = 0; i < n; i++) {
        cell[i] = x;
        x += 44 + lw[i] + space;
    }
    for (int i = 0; i < n; i++) {
        float nx = cell[i] + 17;
        int st = i < P.step ? 2 : i == P.step ? 1 : 0;
        if (st == 2) {
            if (!m_done[i]) m_done[i] = bake_done_node(1000u + (uint32_t)i * 31u + 14u);
            if (m_done[i]) paper_spr_put(c->s, m_done[i], (int)nx, (int)cy);
        } else if (st == 1) {
            if (!m_cur[i]) m_cur[i] = paper_ring(15.4f, &PAPER_CRAYON_BLUE, 1000u + (uint32_t)i * 31u + 7u);
            if (m_cur[i]) paper_spr_put(c->s, m_cur[i], (int)nx, (int)cy);
        } else {
            const PaperCrayon CRAYON_THREAD = {PAL(thread, PAPER_THREAD), PAL(thread, PAPER_THREAD), 0.9f, 0.45f, 0.7f};
            if (!m_up[i]) m_up[i] = paper_ring(15.4f, &CRAYON_THREAD, 1000u + (uint32_t)i * 31u);
            if (m_up[i]) paper_spr_put(c->s, m_up[i], (int)nx, (int)cy);
        }
        if (st != 2) {
            char num[16];
            snprintf(num, sizeof num, "%d", i + 1);
            skin_txtc(c, u->semi, 19, 0, nx, cy, 1, st == 1 ? t->text : hex(PAL(step_dim, 0x6E6A80), 1), num);
        }
        skin_txtc(c, u->semi, 20, 0, cell[i] + 44, cy, 0, st == 1 ? t->text : t->text2, lab[i]);
    }
    for (int i = 0; i + 1 < n; i++) {
        float x0 = cell[i] + 44 + lw[i] + 12, x1 = cell[i + 1] - 12;
        int len = (int)(x1 - x0);
        if (len < 24) continue;
        int done = i < P.step;
        LenSpr *L = done ? &l_done[i] : &l_todo[i];
        if (L->len != len || !L->s) {
            paper_spr_free(L->s);
            L->s = bake_link(len, done, 500u + (uint32_t)i);
            L->len = len;
        }
        if (L->s) paper_spr_put(c->s, L->s, (int)x0, (int)cy);
    }
}

static void render_base_inner(void);
static void render_base(void)
{
    double t0 = g_prg_clock_ms ? g_prg_clock_ms() : 0;
    render_base_inner();
    if (g_prg_clock_ms) {
        g_prg_ms_base += g_prg_clock_ms() - t0;
        g_prg_n_base++;
    }
}

static void render_base_inner(void)
{
    Cx c = ui_cx(G.base);
    Skin *u = c.skin;
    const Theme *t = &u->th;
    prg_pal_check();
    static unsigned ink; // (the board's ink when the chrome was made: a stamp that missed its tile since then is on this board too)
    if (g_chrome_key != (const void *)u->semi || !g_chrome || ink != skin_ink_serial) {
        if (!g_chrome) g_chrome = surf_new(SCREEN_W, SCREEN_H);
        if (g_chrome) chrome_build(g_chrome);
        g_chrome_key = u->semi;
        ink = skin_ink_serial;
    }
    if (g_chrome) surf_copy(G.base, g_chrome);
    else chrome_build(G.base);
    draw_steps(&c);
    // the heading, underlined in crayon
    char fit[160];
    text_fit(u->semi, 40, -0.3f, P.title, STEPS_W, fit, sizeof fit);
    float tw = text_width(u->semi, 40, -0.3f, fit);
    skin_txtc(&c, u->semi, 40, -0.3f, HEAD_X, HEAD_CY, 0, t->text, fit);
    int ulen = (int)(tw * 0.86f);
    if (ulen >= 30) {
        if (ul.len != ulen || !ul.s) {
            paper_spr_free(ul.s);
            ul.s = paper_underline(ulen, &PAPER_CRAYON_BLUE, 91u);
            ul.len = ulen;
        }
        if (ul.s) paper_spr_put(c.s, ul.s, HEAD_X + (int)(tw * 0.02f), UL_Y);
    }
    // the guide, then the icon square and the game's name
    if (!g_guide) {
        g_guide = paper_bake(GUIDE_W + 24, GUIDE_H + 24, draw_guide, NULL);
        wax(g_guide, 2.0f, 0.3f, 0.92f, 321u);
    }
    if (g_guide) gfx_blit_over(c.s, g_guide->px, g_guide->w, g_guide->h, GUIDE_X - 12, GUIDE_Y - 12);
    for (int i = 0; i < P.nlaid; i++) roll_strip_draw(c.s, P.laid[i], 1.0f, NULL); // the finished pieces of the bar
    int mx = MARK_X + MARK_S / 2 - MARK_W / 2, my = MARK_Y + MARK_S / 2 - MARK_W / 2;
    if (P.mark) gfx_blit_over(c.s, P.mark->px, P.mark->w, P.mark->h, mx, my);
    if (P.game[0]) {
        char nm[120];
        text_fit(u->semi, 28, 0, P.game, (float)GUIDE_W, nm, sizeof nm);
        skin_txtc(&c, u->semi, 28, 0, NAME_X, NAME_CY, 0, t->text, nm);
    } else {
        skin_txtc(&c, u->semi, 28, 0, NAME_X, NAME_CY, 0, t->text2, "Getting ready");
    }
    // the last two finished games
    int first = P.ndone > 2 ? P.ndone - 2 : 0, shown = P.ndone - first;
    for (int k = 0; k < shown; k++) {
        const Done *d = &P.done[(first + k) & 1];
        float cy = LIST_Y + k * 40 + 17;
        PaperSpr *tick = paper_tick(20.0f, &PAPER_CRAYON_BLUE, 900u + (uint32_t)(first + k) * 13u);
        if (tick) paper_spr_put(c.s, tick, LIST_X + 14, (int)cy - 4);
        paper_spr_free(tick);
        char nm[120], sz[24];
        text_fit(u->med, 22, 0, d->name, LIST_R - LIST_X - 44 - 150, nm, sizeof nm);
        skin_txtc(&c, u->med, 22, 0, LIST_X + 44, cy, 0, t->text2, nm);
        ui_format_bytes(d->bytes, sz, sizeof sz);
        skin_txtc(&c, u->med, 20, 0, LIST_R, cy, 2, t->text2, sz);
    }
    P.dirty_base = 0;
}

// ------------------------------------------------------------------------------------------------ frames

typedef struct {
    const RollStrip *spent;
    float spent_t, drop_t; // each 0..1 while it runs, <0 when it is not
} Fx;

static const RollFx NO_FX = {0, 0, 0, 0};
static RollFx g_catch_fx; // the catch shown this frame (zero when none)

static void paint_figures(const Rect *R)
{
    Cx c = ui_cx(G.work);
    Skin *u = c.skin;
    char pct[16], size[64];
    figures(pct, sizeof pct, size, sizeof size);
    if (rect_in(&T_PCT, R) && pct[0]) skin_txtc(&c, u->semi, 20, 0, NAME_X, TXT_CY, 0, u->th.text, pct);
    if (rect_in(&T_SIZE, R) && size[0]) skin_txtc(&c, u->med, 20, 0, GUIDE_X + GUIDE_W, TXT_CY, 2, u->th.text2, size);
}

// the rectangle R of G.work made anew: the base with the strips, roll and figures drawn over it
static void paint_rect(const Rect *R, const Fx *fx)
{
    for (int y = 0; y < R->h; y++) {
        size_t o = ((size_t)(R->y + y) * SCREEN_W + R->x) * 4;
        memcpy(G.work->px + o, G.base->px + o, (size_t)R->w * 4);
    }
    RollRect clip = {R->x, R->y, R->w, R->h};
    if (P.S) {
        roll_strip_draw_fx(G.work, P.S, P.disp, &clip, &g_catch_fx);
        if (fx && fx->spent && fx->spent_t >= 0 && fx->spent_t < 1) roll_roll_spent(G.work, fx->spent, fx->spent_t);
        else if (fx && fx->drop_t >= 0 && fx->drop_t < 1 && P.disp <= 0.0f) roll_roll_drop(G.work, P.S, fx->drop_t);
        else if (!P.roll_off) roll_roll_draw_fx(G.work, P.S, P.disp, &clip, &g_catch_fx);
    }
    paint_figures(R);
}

// a rectangle that touches a figure is made to cover the whole of it (the figure is drawn whole or not at all)
static void ensure_figures(Dirty *d)
{
    for (int pass = 0; pass < 3; pass++) {
        const Rect *T[2] = {&T_PCT, &T_SIZE};
        int again = 0;
        for (int k = 0; k < 2 && !again; k++)
            for (int i = 0; i < d->n; i++)
                if (rects_touch(&d->r[i], T[k]) && !rect_in(T[k], &d->r[i])) {
                    dirty_add_rect(d, *T[k]);
                    again = 1;
                    break;
                }
        if (!again) break;
    }
}

// paints what `d` says and shows it
static void show(Dirty *d, const Fx *fx)
{
    g_prg_frames++;
    double t0 = g_prg_clock_ms ? g_prg_clock_ms() : 0;
    if (d->full) {
        Rect all = {0, 0, SCREEN_W, SCREEN_H};
        paint_rect(&all, fx);
        if (g_prg_clock_ms) g_prg_ms_full += g_prg_clock_ms() - t0;
        ui_present(G.work);
        g_prg_full_frames++;
        return;
    }
    ensure_figures(d);
    for (int i = 0; i < d->n; i++) paint_rect(&d->r[i], fx);
    if (g_prg_clock_ms) {
        double ms = g_prg_clock_ms() - t0;
        g_prg_ms_rect += ms;
        if (ms > g_prg_ms_rect_max) g_prg_ms_rect_max = ms;
    }
    ui_present_rects(G.work, d->r, d->n);
    g_prg_rect_frames++;
}

static void note_drawn(void)
{
    char pct[16], size[64];
    figures(pct, sizeof pct, size, sizeof size);
    snprintf(P.d_pct, sizeof P.d_pct, "%s", pct);
    snprintf(P.d_size, sizeof P.d_size, "%s", size);
    g_prg_last_disp = P.disp;
}

// everything again (the base changed)
static void frame_full(void)
{
    if (P.dirty_base) render_base();
    Dirty d;
    dirty_clear(&d);
    dirty_all(&d);
    memset(&g_catch_fx, 0, sizeof g_catch_fx);
    P.fx_drawn = 0;
    show(&d, NULL);
    note_drawn();
}

static Rect bounds_of(const RollStrip *r)
{
    return rect_of_roll(roll_bounds(r));
}

// the rectangle the current piece and its roll can touch, drawn plainly (no effect running)
static void plain_frame(void)
{
    Dirty d;
    dirty_clear(&d);
    if (P.S) dirty_add_rect(&d, bounds_of(P.S));
    memset(&g_catch_fx, 0, sizeof g_catch_fx);
    P.fx_drawn = 0;
    show(&d, NULL);
    note_drawn();
}

// A short run of frames for what happens at a game's end: its used-up roll shrinks away where the tape ended (`spent`, the current piece),
// and/or the next game's roll drops in over the joint (`drop`). Ends on the plain frame. `allow` is 0 after a tiny save, and `fx_left` keeps
// a long list of games from being slowed: when either says no, the change is just made.
static void play_fx(const RollStrip *spent, int drop, int allow)
{
    float dur = 0;
    if (allow && P.fx_left > 0) {
        if (spent) dur = ROLL_SPENT_MS;
        if (drop && dur < ROLL_DROP_MS) dur = ROLL_DROP_MS;
    }
    int full = P.dirty_base; // the base changed: the first frame is the whole screen
    if (full) render_base();
    if (dur <= 0.0f) {
        if (full) frame_full();
        else plain_frame();
        return;
    }
    uint64_t t0 = ui_now();
    int first = full;
    for (;;) {
        float ms = (float)(ui_now() - t0);
        Fx fx = {spent, spent ? ms / ROLL_SPENT_MS : -1.0f, drop ? ms / ROLL_DROP_MS : -1.0f};
        Dirty d;
        dirty_clear(&d);
        if (first) dirty_all(&d);
        else if (P.S) dirty_add_rect(&d, bounds_of(P.S));
        memset(&g_catch_fx, 0, sizeof g_catch_fx);
        show(&d, &fx);
        g_prg_fx_frames++;
        first = 0;
        if (ms >= dur) break;
    }
    P.fx_left -= (int)(ui_now() - t0);
    plain_frame(); // the frame the effects end on (the same pixels the normal path draws)
}

// ------------------------------------------------------------------------------------------------ the strip of a game

// The pieces' runs along the bar from the plan: each as wide as its share of the bytes, but never under MIN_PIECE (the tiny ones keep their
// minimum and the others give up the width pro rata), and together exactly the bar. A plan of nothing is shared equally.
static void layout_plan(void)
{
    int n = P.plan_n;
    static float w[PLAN_MAX];
    static char pinned[PLAN_MAX];
    float total = 0, minw = MIN_PIECE;
    if (n * minw > BAR_W * 0.9f) minw = BAR_W * 0.9f / (float)n;
    for (int i = 0; i < n; i++) {
        total += (float)P.plan[i];
        pinned[i] = 0;
    }
    for (int i = 0; i < n; i++) w[i] = total > 0 ? (float)P.plan[i] / total * BAR_W : (float)BAR_W / (float)n;
    for (int pass = 0; pass < n; pass++) { // pin the pieces under the minimum, share what is left among the others, until none is short
        int npin = 0, again = 0;
        float big = 0;
        for (int i = 0; i < n; i++) {
            if (w[i] < minw) pinned[i] = 1;
            npin += pinned[i];
            if (!pinned[i]) big += w[i];
        }
        float rest = BAR_W - minw * (float)npin;
        for (int i = 0; i < n; i++) {
            float nw = pinned[i] ? minw : (big > 0 ? w[i] * rest / big : rest / (float)(n - npin));
            if (!pinned[i] && nw < minw) again = 1;
            w[i] = nw;
        }
        if (!again) break;
    }
    float x = BAR_X;
    P.edge[0] = BAR_X;
    for (int i = 0; i < n; i++) {
        x += w[i];
        P.edge[i + 1] = i == n - 1 ? BAR_X + BAR_W : (int)(x + 0.5f);
    }
}

static void lay_piece(void) // the current piece is finished: it stays on the bar, in the base
{
    if (!P.S) return;
    if (P.nlaid < PIECES_MAX) P.laid[P.nlaid++] = P.S;
    else roll_strip_free(P.S);
    P.S = NULL;
    P.prev_ms = ui_now() - P.g_t0;
    g_prg_laid++;
}

static void free_pieces(void)
{
    roll_strip_free(P.S);
    P.S = NULL;
    for (int i = 0; i < P.nlaid; i++) roll_strip_free(P.laid[i]);
    P.nlaid = 0;
    P.kstep = 0;
    P.next_x = BAR_X;
}

// a piece: its run (x0, w), a little crooked (about 1.2..2.4 degrees, turning the other way from the piece before, never so much that a long
// piece's ends leave the box), its middle on the bar's line give or take a pixel or two, a roll that is used up as it goes
static RollStrip *piece_new(int k, int x0, int w, int last, RollPick pick, uint32_t seed)
{
    PaperRng r;
    paper_rng_seed(&r, seed ^ 0xBA7C4u);
    float mag = 1.2f + paper_rng_f(&r) * 1.2f, jit = (paper_rng_f(&r) * 2.0f - 1.0f) * 1.0f;
    int ov = last ? 0 : JOINT_OV;
    // a long piece (a bar of one or two games) may wander further across the bar's line and, mostly, is pressed down somewhere along it: the ridge and
    // the far half a little out of line that every lone strip has. The short pieces of a many-game bar stay plain: a ridge in one would read as a joint.
    float len = (float)(w + ov), lim = asinf(clampf((len >= LONG_PIECE ? 9.0f : 5.0f) / len, 0.0f, 1.0f)) * (180.0f / 3.14159265f);
    if (mag > lim) mag = lim;
    float rot = (((uint32_t)k + (P.seed >> 7)) & 1u ? 1.0f : -1.0f) * mag;
    RollGeo g;
    g.th = BAR_TH;
    g.dx = 0.0f;
    g.dy = -0.5f * len * sinf(rot * (3.14159265f / 180.0f)) + jit;
    g.rot = rot;
    g.ov = (float)ov;
    g.r0 = 19.0f + paper_rng_f(&r) * 2.0f;
    g.r1 = 14.0f;
    g.no_crimp = len < LONG_PIECE || paper_rng_f(&r) < 0.3f;
    g.plain_end = 0;
    RollRect slot = {x0, BAR_CY - 14, w, 28};
    RollStrip *S = roll_strip_new_geo(&slot, pick, seed, 0, &g);
    if (k < PIECES_MAX) {
        g_prg_piece_x0[k] = x0;
        g_prg_piece_x1[k] = x0 + w;
        g_prg_piece_rot[k] = rot;
        g_prg_npieces = k + 1;
    }
    return S;
}

static void strip_start(void)
{
    double t0 = g_prg_clock_ms ? g_prg_clock_ms() : 0;
    uint32_t seed = roll_game_seed(P.seed, P.gidx);
    RollPick pick = roll_pick(P.prev_pick, seed);
    P.prev_pick = pick;
    if (P.gidx < 256) {
        g_prg_colour[P.gidx] = pick.colour;
        g_prg_kind[P.gidx] = pick.kind;
    }
    g_prg_strips = P.gidx + 1;
    P.gidx++;
    int k = P.kstep++, x0, w, last = 0, end = BAR_X + BAR_W;
    if (P.plan_n > 0 && k < P.plan_n) {
        x0 = P.edge[k];
        w = P.edge[k + 1] - x0;
        last = k == P.plan_n - 1;
    } else if (k == 0) { // no plan: one game fills the bar
        x0 = BAR_X;
        w = BAR_W;
        last = 1;
    } else { // not in the plan: half of what is left, at least a minimum piece
        int left = end - P.next_x;
        w = left / 2 > MIN_PIECE ? left / 2 : (left > MIN_PIECE ? MIN_PIECE : (left > 8 ? left : 8));
        x0 = P.next_x + w > end ? end - w : P.next_x;
    }
    P.next_x = x0 + w;
    P.S = piece_new(k, x0, w, last, pick, seed);
    P.g_t0 = P.lay_t0 = ui_now();
    P.p_real = P.p_shown = P.disp = 0;
    P.pace_ms = P.S ? fmaxf(MIN_TAPE_MS, P.S->len / ROLL_MAX_PX_S * 1000.0f) : 0.0f;
    if (P.gidx - 1 < 256) {
        g_prg_nocopy[P.gidx - 1] = 0;
        g_prg_pace[P.gidx - 1] = P.pace_ms;
    }
    P.cut_drawn = P.roll_off = 0;
    P.cidx = -1;
    P.fx_drawn = 0;
    g_prg_last_disp = 0;
    g_slip_disp = 0;
    g_slip_t = 0;
    paper_spr_free(P.mark);
    P.mark = mark_make(P.game, P.game_id, paper_tape_rgb[pick.colour], seed ^ 0x5C21Bu);
    progress_hook(PRG_EV_ROLL_ON, 0, 0.0f);
    if (g_prg_clock_ms) {
        g_prg_ms_bake += g_prg_clock_ms() - t0;
        g_prg_n_bake++;
    }
}

// ------------------------------------------------------------------------------------------------ sound and feel hooks

// The one place the roll's moments are reported, and the sounds that go with them. kind and strength are the catch's (ROLL_CATCH_CRIMP or
// ROLL_CATCH_SNAG, 0.4..1); PRG_EV_ROLL_ON is the next game's roll setting down (the lower pat), PRG_EV_CUT the piece done and lying down on the
// paper (the soft pat). They are quiet on purpose: the first set (a tear at .9, taps up to .65) was "a little too punchy" on the console.
static void progress_hook(int ev, int kind, float strength)
{
    if (ev == PRG_EV_ROLL_ON) {
        ui_sound(SND_STICK, 0.3f, 0); // the next roll setting down
    } else if (ev == PRG_EV_CATCH) {
        if (kind == ROLL_CATCH_CRIMP) ui_sound(SND_CRINKLE, 0.2f + 0.22f * strength, 0);
        else ui_sound(SND_SLIP0 + (int)(strength * 7.0f) % 4, 0.25f + 0.18f * strength, 0);
    } else { // PRG_EV_CUT: the piece is cut and lies down
        ui_sound(SND_TEAR, 0.5f, 0);
        ui_sound(SND_STICK, 0.26f, 110);
    }
}


// ------------------------------------------------------------------------------------------------ the public functions

void ui_progress_begin(const char *title, const char *const *steps, int nsteps)
{
    free_pieces();
    paper_spr_free(P.mark);
    memset(&P, 0, sizeof P);
    P.active = 1;
    P.next_x = BAR_X;
    g_prg_npieces = 0;
    snprintf(P.title, sizeof P.title, "%s", title);
    if (nsteps > MAX_STEPS) nsteps = MAX_STEPS;
    for (int i = 0; i < nsteps; i++) snprintf(P.steps[i], sizeof P.steps[i], "%s", steps[i]);
    P.nsteps = nsteps;
    P.step = nsteps > 1 ? 1 : 0;
    P.dirty_base = 1;
    P.cidx = -1;
    P.prev_pick.colour = P.prev_pick.kind = -1;
    P.fx_left = FX_BUDGET_MS;
    P.seed = (uint32_t)ui_now() * 2654435761u + 0x9E37u;
    g_prg_frames = g_prg_rect_frames = g_prg_full_frames = g_prg_fx_frames = g_prg_laid = g_prg_hops = g_prg_catches = g_prg_back = g_prg_strips = g_prg_drops = g_prg_zero_games = 0;
    g_prg_ms_rect = g_prg_ms_full = g_prg_ms_rect_max = g_prg_ms_base = g_prg_ms_bake = 0;
    g_prg_n_base = g_prg_n_bake = 0;
    P.mark = mark_make("", 0, PAL(ghost_edge, PAPER_GHOST_EDGE), 7u); // the empty square while nothing has started
    ui_transition();
    frame_full();
}

static void close_strip(void);

void ui_progress_step(int step)
{
    if (!P.active || step == P.step || step < 0 || step >= P.nsteps) return;
    close_strip(); // (the step before is over: its last game is done)
    P.step = step;
    free_pieces(); // the next step starts with an empty bar
    paper_spr_free(P.mark);
    P.mark = mark_make("", 0, PAL(ghost_edge, PAPER_GHOST_EDGE), 7u);
    P.game[0] = 0; // a new step starts with a clean list
    P.game_id = 0;
    P.ndone = 0;
    P.cur = P.total = 0;
    P.dirty_base = 1;
    P.fx_left = FX_BUDGET_MS;
    P.cidx = -1;
    play_fx(NULL, 0, 0);
}

void ui_progress_plan(unsigned serial, const uint64_t *bytes, size_t n)
{
    if (!P.active || serial == P.plan_serial) return;
    close_strip(); // (a new run of games: the last game of the one before is done)
    P.plan_serial = serial;
    if (n > PLAN_MAX) n = PLAN_MAX;
    P.plan_n = (int)n;
    for (size_t i = 0; i < n; i++) P.plan[i] = bytes[i];
    if (n > 0) layout_plan();
    // a new plan is a new run of games (a new phase): the bar starts empty
    int had = P.S || P.nlaid || P.game[0];
    free_pieces();
    if (had) {
        paper_spr_free(P.mark);
        P.mark = mark_make("", 0, PAL(ghost_edge, PAPER_GHOST_EDGE), 7u);
        P.game[0] = 0;
        P.game_id = 0;
        P.ndone = 0;
        P.cur = P.total = 0;
        P.dirty_base = 1;
        P.cidx = -1;
    }
}

void ui_progress_title(const char *title)
{
    if (!P.active || !strcmp(title, P.title)) return;
    snprintf(P.title, sizeof P.title, "%s", title);
    P.dirty_base = 1;
    frame_full();
}

// How far the roll may have gone by `now`: it lays no more than ROLL_MAX_PX_S px of tape a second, and a strip takes MIN_TAPE_MS at least, so
// a small save, copied in a blink, is still seen being laid. A big copy is slower than that anyway and is not held back at all.
static float pace_cap(uint64_t now)
{
    if (P.pace_ms <= 0.0f) return 1.0f;
    double c = (double)(now - P.lay_t0) / (double)P.pace_ms;
    return c >= 1.0 ? 1.0f : (float)c;
}

// Draws the game with the roll at progress `p` (already paced) as of `now`: the roll held at a catch and then catching up, the two figures,
// the tape's cut when it is all laid. `force`: draw even if nothing seems to have changed. Returns 1 once the strip is cut.
static int show_game(float p, uint64_t now, int force)
{
    int finishing = p >= 1.0f && !P.cut_drawn;
    float disp = p, ms = 0;
    int fx_now = 0;
    if (P.cidx >= 0) {
        ms = (float)(now - P.ct0);
        if (p >= 1.0f) { // all of it laid: nothing may hold the roll back
            P.cidx = -1;
        } else if (roll_catch_active(ms)) {
            float pc = P.S->catches[P.cidx].progress;
            // the hold only when the progress is not far ahead (the picture waits for the next report, so it must never lag by much)
            if (p - pc < 0.05f) disp = roll_catch_progress(P.S, P.cidx, p, ms);
            fx_now = 1;
        } else {
            P.cidx = -1;
        }
    }
    if (disp < P.disp) {
        g_prg_back++; // (never expected: counted by the tests)
        disp = P.disp;
    }
    char pct[16], size[64];
    figures(pct, sizeof pct, size, sizeof size);
    int texts = strcmp(pct, P.d_pct) || strcmp(size, P.d_size);
    if (!force && !texts && disp == P.disp && !fx_now && !P.fx_drawn && !finishing) return P.cut_drawn; // nothing to show
    Dirty d;
    dirty_clear(&d);
    dirty_add_rect(&d, rect_of_roll(roll_dirty(P.S, P.disp, disp)));
    if (fx_now || P.fx_drawn) {
        dirty_add_rect(&d, rect_of_roll(roll_dirty_catch(P.S, P.disp)));
        dirty_add_rect(&d, rect_of_roll(roll_dirty_catch(P.S, disp)));
    }
    if (texts) {
        dirty_add_rect(&d, T_PCT);
        dirty_add_rect(&d, T_SIZE);
    }
    if (disp - g_slip_disp >= 0.05f && disp < 1.0f && now - g_slip_t >= SLIP_GAP_MS) { // the roll turns: a quiet tap every few per cent
        float k = disp - P.disp > 0.15f ? 1.0f : (disp - P.disp) / 0.15f;               // (a little louder the faster it goes)
        g_slip_disp = disp;
        g_slip_t = now;
        ui_sound(SND_SLIP0 + (g_slip_n++ & 3), 0.14f + 0.16f * k, 0);
    }
    P.disp = disp;
    g_catch_fx = fx_now ? roll_catch_fx(P.S, P.cidx, ms) : NO_FX;
    int cut_now = disp >= 1.0f && !P.cut_drawn;
    if (cut_now) {
        P.cut_drawn = 1;
        progress_hook(PRG_EV_CUT, 0, 0.0f);
    }
    uint64_t t0 = ui_now();
    show(&d, NULL);
    note_drawn();
    P.fx_drawn = fx_now;
    if (cut_now) { // the piece is done: its roll is used up and shrinks away, so a long check after the copy shows a finished piece
        P.roll_off = 1;
        g_prg_hops++;
        play_fx(P.S, 0, ui_now() - P.g_t0 >= FX_MIN_GAME_MS);
    }
    uint64_t t1 = ui_now(), cost = t1 - t0;
    uint64_t gap = cost * 3 > MIN_GAP_MS ? cost * 3 : MIN_GAP_MS; // drawing takes about a quarter of the time at most
    P.next_draw = t1 + gap;
    return P.cut_drawn;
}

// Brings the screen up to the real progress, paced (pace_cap). With a big copy that is one frame now and then, as the reports come. When the
// game has reported all of it (`closing`) the frames are made here, one after another, until the roll has laid the strip, cut it and been
// used up, however fast the copy was: a small save is seen being laid, and the next game starts from a finished one.
static void progress_run(int closing)
{
    for (;;) {
        if (closing && P.cut_drawn) return;
        uint64_t now = ui_now();
        float cap = pace_cap(now), p = P.p_real < cap ? P.p_real : cap;
        if (p < P.p_shown) p = P.p_shown; // only ever forward
        int crossed = roll_crossed_catch(P.S, P.p_shown, p);
        if (crossed) {
            P.cidx = crossed - 1;
            P.ct0 = now;
            g_prg_catches++;
            progress_hook(PRG_EV_CATCH, P.S->catches[P.cidx].kind, P.S->catches[P.cidx].strength);
        }
        P.p_shown = p;
        int finishing = p >= 1.0f && !P.cut_drawn;
        if (!closing && !crossed && !finishing && now < P.next_draw) return;
        int cut = show_game(p, now, closing);
        if (!closing || cut) return;
    }
}

// The locker has gone on to something else, so the game whose strip is on screen was copied, whatever its last report said (a small save
// may report nothing after its first line): the roll lays what is left of the strip, at its pace, and cuts it.
static void close_strip(void)
{
    if (!P.S || P.cut_drawn || P.total == 0) return;
    P.p_real = 1.0f;
    P.cur = P.total;
    progress_run(1);
}

void ui_progress_update(const char *game, uint64_t title_id, uint64_t cur, uint64_t total)
{
    if (!P.active) return;
    int changed = strcmp(game, P.game) != 0 || (P.S && cur < P.cur);
    if (changed) {
        close_strip();
        lay_piece(); // the finished piece stays on the bar
        if (P.game[0]) {
            Done *d = &P.done[P.ndone & 1];
            snprintf(d->name, sizeof d->name, "%s", P.game);
            d->bytes = P.total;
            P.ndone++;
        }
        snprintf(P.game, sizeof P.game, "%s", game);
        P.game_id = title_id;
        P.cur = cur;
        P.total = total;
        P.dirty_base = 1;
        int drop = 0;
        if (game[0]) {
            strip_start();
            if (total == 0) { // nothing to copy: its piece is simply there (no roll, no effects, no pace)
                g_prg_zero_games++;
                P.p_real = P.p_shown = P.disp = 1.0f;
                P.pace_ms = 0.0f;
                if (P.gidx - 1 < 256) {
                    g_prg_nocopy[P.gidx - 1] = 1;
                    g_prg_pace[P.gidx - 1] = 0.0f;
                }
                P.cut_drawn = P.roll_off = 1;
                g_prg_last_disp = 1.0f;
                progress_hook(PRG_EV_CUT, 0, 0.0f);
            } else {
                drop = 1;
                g_prg_drops++;
            }
        } else {
            paper_spr_free(P.mark);
            P.mark = mark_make("", 0, PAL(ghost_edge, PAPER_GHOST_EDGE), 7u);
        }
        play_fx(NULL, drop, drop && (P.kstep == 1 || P.prev_ms >= FX_MIN_GAME_MS));
        P.lay_t0 = ui_now(); // the roll has landed: its pace runs from here
        P.next_draw = 0;
    }
    if (!P.S) return;
    float real = total ? (float)((double)cur / (double)total) : 0.0f;
    if (real > 1.0f) real = 1.0f;
    if (real < P.p_real) real = P.p_real; // only ever forward
    P.cur = cur;
    P.total = total;
    P.p_real = real;
    progress_run(real >= 1.0f);
}

void ui_progress_end(void)
{
    if (!P.active) return;
    if (P.S && P.p_real >= 1.0f && !P.roll_off) { // the last piece stays; its roll is used up
        P.disp = 1.0f;
        P.roll_off = 1;
        g_prg_hops++;
        memset(&g_catch_fx, 0, sizeof g_catch_fx);
        play_fx(P.S, 0, ui_now() - P.g_t0 >= FX_MIN_GAME_MS);
    }
    if (P.step >= P.nsteps - 1) ui_sound(SND_DONE, 0.8f, 150); // (it got to the last step)
    free_pieces();
    paper_spr_free(P.mark);
    P.mark = NULL;
    P.active = 0;
}
