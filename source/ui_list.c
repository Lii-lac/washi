// Washi's list pages (the admin's Profiles, Deleted profiles, Unclaimed saves, the settings pages ...), a text viewer for the log and the
// "please wait" page.
//
// A page is a title slip on the board, an optional header block, and one taped sheet of paper that holds the rows. Each row is a paper
// slip stuck on the sheet with its own piece of washi tape. The focused slip is unstuck: lifted, its tape peeling, a dashed ghost patch
// where it was, a short wiggle; it sticks back when the focus leaves (interruptibly, and lighter on a held D-pad). When the focus leaves
// the sheet the list jumps to the next page, and the slips settle into place one after another.
//
// How a frame is made (the same way as the home screen): the board, the corner bar, the title, the sheet and the footer are the `base`
// (G.base) and are not touched again. Every row is a baked sprite (what is printed on it, its resting shadow, its stuck tape) kept in a
// small cache, plus a lifted pose (ghost patch, shadow, the slip turned and scaled, peeling tape). A frame works out each row's pose,
// adds what changed to a list of dirty rectangles, paints those from the base with every row that touches them, and presents just
// those rectangles. A frame costs what moved, not the screen.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/paper.h"
#include "tint.h"
#include "ui_internal.h"

static const Prompt DEFAULT_PROMPTS[] = {{"A", "Choose"}, {"B", "Back"}};

enum {
    SHEET_X = 200,
    SHEET_W = 880,
    SLIP_INSET = 14,    // the slips sit this far in from the sheet's sides and top
    SHEET_PAD_Y = 28,   // the sheet is the rows plus this much (14 above, the rest below, less the gap after the last slip)
    SHEET_BOTTOM = 640, // the sheet never ends lower than this (the footer strip starts at 660)
    SLIP_GAP = 6,
    TITLE_Y = 104,
    TITLE_H = 46,
    HEADER_Y = 164,
    HEADER_GAP = 24,
    PAD_BODY = 14,
    PAD_REST = 22,
    MAXR = 16,
    NLIFT = 8
};

// A list that jumps to a new page (the focus left the sheet) does not glide: the slips are at their places at once and settle one after
// another. Each starts CASC_AMP px out of line, the way the list went, and eases in over CASC_MS; a slip starts CASC_STAGGER_MS after the one
// before it, the wave running toward the focus.
#define CASC_AMP 9.0f
#define CASC_MS 170.0
#define CASC_STAGGER_MS 26.0

int g_ui_list_off; // tests: the most any slip was out of line in the frame last made (px)

static float rad(float deg) { return deg * 3.14159265f / 180.0f; }

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static uint32_t fnv_str(uint32_t h, const char *s) { return fnv(h, s ? s : "", s ? strlen(s) + 1 : 1); }
static uint32_t fnv_int(uint32_t h, int v) { return fnv(h, &v, sizeof v); }
static uint32_t fnv_f(uint32_t h, float v) { return fnv(h, &v, sizeof v); }

// ------------------------------------------------------------------------------------------------ caches of baked things

typedef struct {
    int used;
    unsigned gen;
    PaperTape t;
    PaperSpr *stuck;
    PaperPeel *peel;
    int tried;
} TapeEnt;

enum { TAPE_CACHE = 48 };
static TapeEnt g_tp[TAPE_CACHE];
static unsigned g_tp_gen = 1;

static int tape_eq(const PaperTape *a, const PaperTape *b)
{
    return a->len == b->len && a->wid == b->wid && a->rgb == b->rgb && a->kind == b->kind && a->flags == b->flags && a->tilt == b->tilt &&
           a->seed == b->seed && a->host_w == b->host_w && a->host_h == b->host_h && a->host_r == b->host_r && a->host_x == b->host_x && a->host_y == b->host_y;
}

static TapeEnt *tape_ent(const PaperTape *t)
{
    TapeEnt *spare = NULL, *oldest = NULL;
    for (int i = 0; i < TAPE_CACHE; i++) {
        TapeEnt *e = &g_tp[i];
        if (e->used && tape_eq(&e->t, t)) {
            e->gen = g_tp_gen;
            return e;
        }
        if (!e->used) {
            if (!spare) spare = e;
        } else if (!oldest || e->gen < oldest->gen) {
            oldest = e;
        }
    }
    TapeEnt *e = spare ? spare : oldest;
    if (e->used) {
        paper_spr_free(e->stuck);
        paper_peel_free(e->peel);
    }
    memset(e, 0, sizeof *e);
    e->used = 1;
    e->gen = g_tp_gen;
    e->t = *t;
    e->stuck = paper_tape(t);
    return e;
}

// the tape's peeling states are baked the first time a row is lifted (a few small pictures)
static void peel_ensure(TapeEnt *e)
{
    if (e->peel || e->tried) return;
    e->tried = 1;
    e->peel = paper_tape_peel_lazy(&e->t, PAPER_PEEL_STATES);
}

static const PaperSpr *card_get(int w, int h, float r, uint32_t seed, int pad)
{
    enum { N = 10 };
    static struct {
        int w, h, pad;
        float r;
        uint32_t seed;
        PaperSpr *s;
    } c[N];
    static int next;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: new paper)
        for (int i = 0; i < N; i++) paper_spr_free(c[i].s);
        memset(c, 0, sizeof c);
    }
    for (int i = 0; i < N; i++)
        if (c[i].s && c[i].w == w && c[i].h == h && c[i].pad == pad && c[i].r == r && c[i].seed == seed) return c[i].s;
    int k = next;
    next = (next + 1) % N;
    paper_spr_free(c[k].s);
    c[k].w = w;
    c[k].h = h;
    c[k].pad = pad;
    c[k].r = r;
    c[k].seed = seed;
    c[k].s = paper_card_ex(w, h, r, seed, pad);
    return c[k].s;
}

static const PaperSpr *ring_get(float radius, uint32_t seed)
{
    enum { N = 8 };
    static struct {
        float r;
        uint32_t seed;
        PaperSpr *s;
    } c[N];
    static int next;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: new wax)
        for (int i = 0; i < N; i++) paper_spr_free(c[i].s);
        memset(c, 0, sizeof c);
    }
    for (int i = 0; i < N; i++)
        if (c[i].s && c[i].r == radius && c[i].seed == seed) return c[i].s;
    int k = next;
    next = (next + 1) % N;
    paper_spr_free(c[k].s);
    c[k].r = radius;
    c[k].seed = seed;
    c[k].s = paper_ring(radius, &PAPER_CRAYON_STD, seed);
    return c[k].s;
}

static const PaperSpr *ghost_get(int w, int h, float r)
{
    enum { N = 4 };
    static struct {
        int w, h;
        float r;
        PaperSpr *s;
    } c[N];
    static int next;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: a new board under the patch)
        for (int i = 0; i < N; i++) paper_spr_free(c[i].s);
        memset(c, 0, sizeof c);
    }
    for (int i = 0; i < N; i++)
        if (c[i].s && c[i].w == w && c[i].h == h && c[i].r == r) return c[i].s;
    int k = next;
    next = (next + 1) % N;
    paper_spr_free(c[k].s);
    c[k].w = w;
    c[k].h = h;
    c[k].r = r;
    c[k].s = paper_ghost(w, h, r);
    return c[k].s;
}

// A wide card's shadow set is made from a narrow card's: the middle column of each level is repeated, so the blur (the expensive
// part) is done on a small picture (the same trick as the home screen's wide cards).
static PaperShadows *shadows_make(int w, int h, float r, float grow, int n)
{
    const int wb = 240;
    if (w <= wb + 20) return paper_shadows(w, h, r, grow, n);
    PaperShadows *b = paper_shadows(wb, h, r, grow, n);
    if (!b) return NULL;
    PaperShadows *o = calloc(1, sizeof *o);
    if (!o) {
        paper_shadows_free(b);
        return NULL;
    }
    int Wb = b->s[0]->w, Hb = b->s[0]->h, half = Wb / 2;
    int ins_max = (int)ceilf((w - wb) * (1.0f + grow));
    int W2 = Wb + ins_max;
    if ((W2 - w) & 1) W2++;
    for (int i = 0; i < b->n; i++) {
        float k = 1.0f + grow * i / (float)(b->n - 1);
        int ins = (int)lroundf((w - wb) * k), L0 = (W2 - (Wb + ins)) / 2;
        PaperSpr *s = paper_spr_new(W2, Hb);
        if (!s) {
            paper_shadows_free(o);
            paper_shadows_free(b);
            return NULL;
        }
        s->ox = (W2 - w) / 2;
        s->oy = b->s[0]->oy;
        for (int y = 0; y < Hb; y++) {
            const uint8_t *src = b->s[i]->px + (size_t)y * Wb * 4;
            uint8_t *dst = s->px + (size_t)y * W2 * 4;
            memcpy(dst + (size_t)L0 * 4, src, (size_t)half * 4);
            for (int j = 0; j < ins; j++) memcpy(dst + (size_t)(L0 + half + j) * 4, src + (size_t)half * 4, 4);
            memcpy(dst + (size_t)(L0 + half + ins) * 4, src + (size_t)half * 4, (size_t)(Wb - half) * 4);
        }
        o->s[o->n++] = s;
    }
    paper_shadows_free(b);
    return o;
}

static const PaperShadows *shadows_get(int w, int h, float r, float grow, int n)
{
    enum { N = 6 };
    static struct {
        int w, h, n;
        float r, grow;
        unsigned gen;
        PaperShadows *sh;
    } c[N];
    static unsigned gen, serial;
    if (paper_pal_stale(&serial)) { // (a new palette: shadows of a new colour)
        for (int i = 0; i < N; i++) paper_shadows_free(c[i].sh);
        memset(c, 0, sizeof c);
    }
    gen++;
    int oldest = 0;
    for (int i = 0; i < N; i++) {
        if (c[i].sh && c[i].w == w && c[i].h == h && c[i].r == r && c[i].grow == grow && c[i].n == n) {
            c[i].gen = gen;
            return c[i].sh;
        }
        if (!c[i].sh || c[i].gen < c[oldest].gen) oldest = i;
    }
    paper_shadows_free(c[oldest].sh);
    c[oldest].sh = shadows_make(w, h, r, grow, n);
    c[oldest].w = w;
    c[oldest].h = h;
    c[oldest].r = r;
    c[oldest].grow = grow;
    c[oldest].n = n;
    c[oldest].gen = gen;
    return c[oldest].sh;
}

// ---- a baked item: the body (the paper and what is printed on it, `pad` px of room all round: what is lifted and turned) and the rest
// sprite (its resting shadow, the body turned a little, the tape stuck on)
typedef struct {
    PaperSpr *body, *rest;
    int pad, rox, roy;
} Sprs;

typedef struct {
    const PaperSpr *sh0, *body, *tape;
    float tilt, w, h, tdx, tdy;
    int rpad;
} RestBake;

// Turns and scales a sprite about its middle (which lands on (cx, cy)) and puts it over `s`: what gfx_blit_xform does at full strength,
// in fixed point and with a short way for the many pixels whose four neighbours are all solid (a slip this wide is ~90,000 pixels,
// and the float version spends about 10 times as long on them). The sprite must have a transparent edge (all of ours do).
static void xform_fast(Surf *s, const PaperSpr *p, float cx, float cy, float scale, float angle)
{
    if (!p || !p->px || scale <= 0.0f) return;
    const int sw = p->w, sh = p->h;
    float ca = cosf(angle), sa = sinf(angle), inv = 1.0f / scale;
    float hw = sw * 0.5f, hh = sh * 0.5f;
    float ex = (fabsf(ca) * sw + fabsf(sa) * sh) * 0.5f * scale + 1.0f;
    float ey = (fabsf(sa) * sw + fabsf(ca) * sh) * 0.5f * scale + 1.0f;
    int x0 = (int)floorf(cx - ex), x1 = (int)ceilf(cx + ex), y0 = (int)floorf(cy - ey), y1 = (int)ceilf(cy + ey);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    const int32_t du = (int32_t)lroundf(ca * inv * 65536.0f), dv = (int32_t)lroundf(-sa * inv * 65536.0f);
    const uint8_t *src = p->px;
    const int stride = sw * 4;
    for (int y = y0; y < y1; y++) {
        float fx = x0 + 0.5f - cx, fy = y + 0.5f - cy;
        float u = hw + (fx * ca + fy * sa) * inv - 0.5f, v = hh + (-fx * sa + fy * ca) * inv - 0.5f;
        int32_t U = (int32_t)lroundf(u * 65536.0f), V = (int32_t)lroundf(v * 65536.0f);
        uint8_t *d = s->px + ((size_t)y * s->w + x0) * 4;
        for (int x = x0; x < x1; x++, U += du, V += dv, d += 4) {
            int iu = U >> 16, iv = V >> 16;
            if (iu < 0 || iv < 0 || iu + 1 >= sw || iv + 1 >= sh) continue; // (the sprite's edge is clear: nothing to draw out there)
            const uint8_t *t00 = src + (size_t)iv * stride + (size_t)iu * 4, *t10 = t00 + 4, *t01 = t00 + stride, *t11 = t01 + 4;
            unsigned a00 = t00[3], a10 = t10[3], a01 = t01[3], a11 = t11[3];
            if (!(a00 | a10 | a01 | a11)) continue;
            unsigned fu = (unsigned)(U >> 8) & 255u, fv = (unsigned)(V >> 8) & 255u;
            unsigned u0 = 256u - fu, v0 = 256u - fv;
            unsigned w00 = u0 * v0, w10 = fu * v0, w01 = u0 * fv, w11 = fu * fv; // (they add up to 65536)
            if ((a00 & a10 & a01 & a11) == 255u) {
                d[0] = (uint8_t)((t00[0] * w00 + t10[0] * w10 + t01[0] * w01 + t11[0] * w11 + 32768u) >> 16);
                d[1] = (uint8_t)((t00[1] * w00 + t10[1] * w10 + t01[1] * w01 + t11[1] * w11 + 32768u) >> 16);
                d[2] = (uint8_t)((t00[2] * w00 + t10[2] * w10 + t01[2] * w01 + t11[2] * w11 + 32768u) >> 16);
                d[3] = 255;
                continue;
            }
            w00 >>= 4; w10 >>= 4; w01 >>= 4; w11 >>= 4; // (now up to 4096 and adding up to 4096)
            const unsigned K = 255u * 4096u;
            unsigned A = a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11;
            if (A < 8u) continue;
            for (int c = 0; c < 3; c++) {
                unsigned C = t00[c] * a00 * w00 + t10[c] * a10 * w10 + t01[c] * a01 * w01 + t11[c] * a11 * w11;
                unsigned o = ((unsigned)d[c] * (K - A) + C + K / 2u) / K;
                d[c] = (uint8_t)(o > 255u ? 255u : o);
            }
            d[3] = 255;
        }
    }
}

static void draw_rest(Surf *s, void *ud)
{
    const RestBake *b = ud;
    float cx = b->rpad + b->w * 0.5f, cy = b->rpad + b->h * 0.5f;
    if (b->sh0) paper_spr_put(s, b->sh0, b->rpad, b->rpad);
    if (b->tilt == 0.0f) { // (not turned: a plain copy; the body has the same room all round)
        int bpad = (b->body->w - (int)b->w) / 2;
        gfx_blit_over(s, b->body->px, b->body->w, b->body->h, b->rpad - bpad, b->rpad - bpad);
    } else {
        paper_spr_xform(s, b->body, cx, cy, 1.0f, b->tilt, 1.0f);
    }
    if (b->tape) {
        PaperXf cf = {cx, cy, 1.0f, b->tilt};
        PaperXf t = paper_xf_attach(cf, b->tdx, b->tdy);
        paper_spr_xform(s, b->tape, t.x, t.y, t.scale, t.angle, 1.0f);
    }
}

static void sprs_bake(Sprs *o, float w, float h, int pad, int rpad, float tilt, const PaperSpr *sh0, const PaperSpr *tape, float tdx, float tdy,
                      PaperDraw draw, void *ud)
{
    memset(o, 0, sizeof *o);
    o->pad = pad;
    o->body = paper_bake((int)w + 2 * pad, (int)h + 2 * pad, draw, ud);
    if (!o->body) return;
    RestBake rb = {sh0, o->body, tape, tilt, w, h, tdx, tdy, rpad};
    o->rest = paper_bake((int)w + 2 * rpad, (int)h + 2 * rpad, draw_rest, &rb);
    o->rox = o->roy = rpad;
}

static void sprs_free(Sprs *o)
{
    paper_spr_free(o->rest);
    paper_spr_free(o->body);
    memset(o, 0, sizeof *o);
}

// ------------------------------------------------------------------------------------------------ geometry

static float g_slip_w, g_slip_h; // the size of a slip, set by geometry(): what a row's tape is stuck to

typedef struct {
    float y0, h; // the sheet: top, height (x and width are SHEET_X, SHEET_W)
    float pitch; // from one slip to the next
    float slip_x, slip_w, slip_h;
    float header_y;
    int vis; // rows that fit
} Geo;

static void geometry(const UiList *l, Geo *g)
{
    int avatars = 0; // (or a second line: those rows are as tall)
    for (int i = 0; i < l->n; i++)
        if (l->items[i].avatar_name || l->items[i].sub) avatars = 1;
    g->pitch = l->row_h > 0 ? l->row_h : (avatars ? 84.0f : 72.0f);
    float y = l->title ? (l->subtitle ? 216.0f : 190.0f) : 108.0f; // under the title slip (and the line under it)
    g->header_y = 0;
    if (l->header_h > 0) {
        g->header_y = l->title ? (float)HEADER_Y : 108.0f;
        y = g->header_y + l->header_h + HEADER_GAP;
    }
    g->y0 = y;
    int vis = (int)floorf((SHEET_BOTTOM - y - SHEET_PAD_Y) / g->pitch);
    if (vis < 1) vis = 1;
    if (vis > l->n) vis = l->n;
    g->vis = vis;
    g->h = l->n == 0 ? 130.0f : vis * g->pitch + SHEET_PAD_Y;
    g->slip_x = SHEET_X + SLIP_INSET;
    g->slip_w = SHEET_W - 2 * SLIP_INSET;
    g->slip_h = g->pitch - SLIP_GAP;
    g_slip_w = g->slip_w; // (the card the slips' tape is stuck over the edge of)
    g_slip_h = g->slip_h;
}

// ------------------------------------------------------------------------------------------------ what is printed on a row

// the toggle's track, dashed when it is off: dots along the outline of a pill (a dash 7 px long, a gap of 4)
static void dashed_pill(Surf *s, float x, float y, float w, float h, Col k)
{
    float r = h * 0.5f - 1.0f, len_s = w - h, len_c = 3.14159265f * r, total = 2 * len_s + 2 * len_c;
    float cx0 = x + h * 0.5f, cx1 = x + w - h * 0.5f, cy = y + h * 0.5f;
    for (float d = 0; d < total; d += 1.0f) {
        if (fmodf(d, 11.0f) >= 7.0f) continue;
        float px, py;
        if (d < len_s) {
            px = cx0 + d;
            py = cy - r;
        } else if (d < len_s + len_c) {
            float a = (d - len_s) / r - 1.5707963f;
            px = cx1 + r * cosf(a);
            py = cy + r * sinf(a);
        } else if (d < 2 * len_s + len_c) {
            px = cx1 - (d - len_s - len_c);
            py = cy + r;
        } else {
            float a = (d - 2 * len_s - len_c) / r + 1.5707963f;
            px = cx0 + r * cosf(a);
            py = cy + r * sinf(a);
        }
        gfx_circle(s, px, py, 1.0f, k);
    }
}

// a paper knob in a dashed track (off), or in an ink-blue one (on)
static void draw_toggle(Surf *s, float x, float cy, int on)
{
    float w = 58, h = 30;
    if (on) {
        gfx_rrect(s, x, cy - h / 2, w, h, h / 2, hex(PAL(toggle_on, PAPER_INK), 1));
    } else {
        gfx_rrect(s, x, cy - h / 2, w, h, h / 2, hex(PAL(pill_off, 0xE7E1D1), 1));
        dashed_pill(s, x + 1, cy - h / 2 + 1, w - 2, h - 2, hex(PAL(pill_edge, 0xB9AE92), 1));
    }
    float kx = on ? x + 41 : x + 15;
    gfx_circle(s, kx, cy + 2.0f, 11.5f, hex(PAL(shadow, PAPER_SHADOW), 0.30f));
    gfx_circle(s, kx, cy, 11, hex(PAL(knob, PAPER_PAPER), 1));
}

typedef struct {
    const UiItem *it;
    const PaperSpr *card, *ring, *stamp;
    int w, h, stamp_w;
} RowBake;

static void draw_stamp(Surf *s, void *ud)
{
    const RowBake *b = ud;
    Cx c = ui_cx(s);
    float w = (float)b->stamp_w, h = 30;
    Col ink = hex(PAL(ink, PAPER_INK), 0.92f);
    gfx_rrect_stroke(s, 4.5f, 4.5f, w - 1, h - 1, 5, 3.0f, ink);
    gfx_rrect_stroke(s, 10.0f, 10.0f, w - 12, h - 12, 3, 1.4f, hex(PAL(ink, PAPER_INK), 0.55f));
    skin_txtc(&c, c.skin->semi, 14, 2.0f, 4 + w / 2, 4 + h / 2 + 0.5f, 1, ink, "LOADED");
}

static void draw_row_body(Surf *s, void *ud)
{
    const RowBake *b = ud;
    const UiItem *it = b->it;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Theme *t = &u->th;
    const float pad = PAD_BODY, W = (float)b->w, H = (float)b->h;
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    float cy = pad + H / 2, lx = pad + 44;
    Col soft = t->text2;
    Col ink = it->style == ITEM_DANGER ? t->danger : it->style == ITEM_DIM ? hex(PAL(dim, 0x8A857A), 1) : t->text;
    Col det_ink = it->style == ITEM_DIM ? hex(PAL(dim, 0x8A857A), 1) : soft;
    if (it->avatar_name) {
        skin_avatar(&c, pad + 46, cy, 26, it->avatar_name, it->avatar_color);
        if (b->ring) paper_spr_put(s, b->ring, (int)(pad + 46), (int)cy);
        lx = pad + 92;
    }
    float right = pad + W - 40;
    if (it->chevron) {
        skin_icon_chev(&c, pad + W - 44, cy, 22, 2.6f, hex(PAL(text_soft, PAPER_TEXT_SOFT), 0.95f));
        right = pad + W - 84;
    }
    if (it->toggle >= 0) {
        draw_toggle(s, pad + W - 92, cy, it->toggle);
        right = pad + W - 108;
    }
    char det[96] = "";
    float det_w = 0, dy = it->has_bar ? cy - 12 : cy; // (with a bar, the size or count sits level with the label)
    if (it->detail && it->detail[0]) {
        text_fit(u->med, 20, 0, it->detail, 330, det, sizeof det);
        det_w = text_width(u->med, 20, 0, det);
        skin_txtc(&c, u->med, 20, 0, right, dy, 2, det_ink, det);
    }
    if (it->swatch >= 0) // a dot of the avatar colour, then the colour's name
        gfx_circle(s, right - det_w - (det_w ? 22 : 0) - 11, cy, 11, skin_avatar_color(it->swatch));
    if (it->has_bar) { // under the label, as long as the row allows: the track, then its share in the row's colour
        float bw = right - lx, f = it->bar < 0 ? 0 : it->bar > 1 ? 1 : it->bar;
        gfx_rrect(s, lx, cy + 7, bw, 10, 5, hex(PAL(pill_off, 0xE7E1D1), 1));
        if (f > 0) gfx_rrect(s, lx, cy + 7, 10 + (bw - 10) * f, 10, 5, hex(tint_for_theme(it->bar_rgb, 0), 1));
    }
    int loaded = it->badge && !strcmp(it->badge, "Loaded");
    float badge_w = 0;
    if (it->badge && it->badge[0] && !loaded) badge_w = text_width(u->semi, 14, 0.4f, it->badge) + 28 + 12;
    if (loaded) badge_w = (float)b->stamp_w + 28;
    char lab[160];
    float maxlab = right - (det_w ? det_w + 24 : 0) - lx - badge_w - 6;
    float ly = it->has_bar ? cy - 12 : it->sub && it->sub[0] ? cy - 12 : cy; // with a second line, the two are centred as a pair
    text_fit(u->semi, 26, 0, it->label, maxlab, lab, sizeof lab);
    skin_txtc(&c, u->semi, 26, 0, lx, ly, 0, ink, lab);
    if (it->sub && it->sub[0] && !it->has_bar) {
        char sub[160];
        text_fit(u->med, 18, 0, it->sub, right - (det_w ? det_w + 24 : 0) - lx - 6, sub, sizeof sub);
        skin_txtc(&c, u->med, 18, 0, lx, cy + 15, 0, soft, sub);
    }
    if (loaded && b->stamp) { // the rubber stamp, a little crooked, after the name
        float sx = lx + text_width(u->semi, 26, 0, lab) + 30 + b->stamp->w * 0.5f;
        paper_spr_xform(s, b->stamp, sx, ly, 1.0f, rad(-3.0f), 1.0f);
    } else if (badge_w > 0) { // a small paper label
        float bx = lx + text_width(u->semi, 26, 0, lab) + 14, bw = badge_w - 12;
        gfx_rrect(s, bx, ly - 14, bw, 28, 5, hex(0xFFE3CD, 1));
        skin_txtc(&c, u->semi, 14, 0.4f, bx + bw / 2, ly + 0.5f, 1, hex(PAPER_TEXT, 1), it->badge);
    }
}

// the tape on slip number `idx`: its colour and pattern go round (as in the mockup), crooked the same way every time
static void row_tape(int idx, PaperTape *t)
{
    static const int SEC[5] = {5, 4, 3, 2, 0};
    static const int KINDS[4] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG};
    t->len = 64;
    t->wid = 24;
    t->rgb = paper_tape_rgb[SEC[idx % 5]];
    t->kind = KINDS[idx % 4];
    t->flags = PAPER_TAPE_ZIG;
    t->tilt = -24.0f;
    t->seed = 300u + (uint32_t)(idx % 20);
    paper_tape_host(t, g_slip_w, g_slip_h, 6.0f, -15.0f, -9.0f);
}

static void tape_for_row(int idx, TapeEnt **te)
{
    PaperTape t;
    row_tape(idx, &t);
    *te = tape_ent(&t);
}

// ---- the rows' sprites, kept between frames (and between visits to a page) by what they show
typedef struct {
    int used;
    unsigned gen;
    uint32_t key;
    Sprs sp;
} RowEnt;

enum { ROW_CACHE = 16 };
static RowEnt g_re[ROW_CACHE];
static unsigned g_re_gen = 1;

static uint32_t row_key(const UiItem *it, int idx, const Geo *g)
{
    uint32_t k = 2166136261u;
    k = fnv_str(k, it->label);
    k = fnv_str(k, it->detail);
    k = fnv_str(k, it->sub);
    k = fnv_str(k, it->badge);
    k = fnv_str(k, it->avatar_name);
    k = fnv_int(k, it->style);
    k = fnv_int(k, it->toggle);
    k = fnv_int(k, it->avatar_color);
    k = fnv_int(k, it->chevron);
    k = fnv_int(k, it->swatch);
    k = fnv_int(k, it->has_bar);
    k = fnv_f(k, it->bar);
    k = fnv_int(k, (int)it->bar_rgb);
    k = fnv_int(k, idx % 20);
    k = fnv_f(k, g->slip_w);
    return fnv_f(k, g->slip_h);
}

static RowEnt *row_find(uint32_t key)
{
    for (int i = 0; i < ROW_CACHE; i++)
        if (g_re[i].used && g_re[i].key == key) {
            g_re[i].gen = g_re_gen;
            return &g_re[i];
        }
    return NULL;
}

static RowEnt *row_build(const UiList *l, const Geo *g, int idx, uint32_t key)
{
    RowEnt *e = NULL, *oldest = NULL;
    for (int i = 0; i < ROW_CACHE; i++) {
        if (!g_re[i].used) {
            e = &g_re[i];
            break;
        }
        if (!oldest || g_re[i].gen < oldest->gen) oldest = &g_re[i];
    }
    if (!e) {
        e = oldest;
        sprs_free(&e->sp);
    }
    const UiItem *it = &l->items[idx];
    int w = (int)g->slip_w, h = (int)g->slip_h;
    RowBake rb;
    memset(&rb, 0, sizeof rb);
    rb.it = it;
    rb.w = w;
    rb.h = h;
    rb.card = card_get(w, h, 6.0f, 41u, PAD_BODY);
    rb.ring = it->avatar_name ? ring_get(27.0f, 3u + (uint32_t)(idx % 5)) : NULL;
    PaperSpr *stamp = NULL;
    if (it->badge && !strcmp(it->badge, "Loaded")) {
        rb.stamp_w = (int)ceilf(text_width(G.skin.semi, 14, 2.0f, "LOADED")) + 34;
        stamp = paper_bake(rb.stamp_w + 8, 38, draw_stamp, &rb);
        rb.stamp = stamp;
    }
    TapeEnt *te;
    tape_for_row(idx, &te);
    float tdx, tdy;
    paper_tape_offset((float)w, (float)h, -15.0f, -9.0f, 64.0f, 24.0f, &tdx, &tdy);
    const PaperShadows *sh = shadows_get(w, h, 6.0f, paper_lift_k[LIFT_ROW].scale, PAPER_SHADOW_LEVELS);
    memset(&e->sp, 0, sizeof e->sp);
    sprs_bake(&e->sp, (float)w, (float)h, PAD_BODY, PAD_REST, 0.0f, sh ? sh->s[0] : NULL, te->stuck, tdx, tdy, draw_row_body, &rb);
    paper_spr_free(stamp);
    e->used = 1;
    e->key = key;
    e->gen = g_re_gen;
    if (!e->sp.body || !e->sp.rest) {
        sprs_free(&e->sp);
        e->used = 0;
        return NULL;
    }
    return e;
}

// The palette changed since the rows and their tape were made (the theme did): they all go, to be made again in the new colours. 1 if it did.
static int list_pal_check(void)
{
    static unsigned serial = 1; // (the palette the app starts with: nothing made yet to throw away)
    if (!paper_pal_stale(&serial)) return 0;
    for (int i = 0; i < ROW_CACHE; i++) sprs_free(&g_re[i].sp);
    memset(g_re, 0, sizeof g_re);
    for (int i = 0; i < TAPE_CACHE; i++) {
        if (g_tp[i].used) {
            paper_spr_free(g_tp[i].stuck);
            paper_peel_free(g_tp[i].peel);
        }
        memset(&g_tp[i], 0, sizeof g_tp[i]);
    }
    return 1;
}

static RowEnt *row_get(const UiList *l, const Geo *g, int idx, int bake)
{
    uint32_t key = row_key(&l->items[idx], idx, g);
    RowEnt *e = row_find(key);
    if (!e && bake) e = row_build(l, g, idx, key);
    return e;
}

// ------------------------------------------------------------------------------------------------ the page's fixed part

// The title slip: a small paper card, crooked by 0.6 degrees, with the page's name on it (the mockup's `sub`)
typedef struct {
    const char *text;
    const PaperSpr *card;
} TitleBake;

static void draw_title_body(Surf *s, void *ud)
{
    const TitleBake *b = ud;
    Cx c = ui_cx(s);
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    skin_txtc(&c, c.skin->semi, 30, 0, PAD_BODY + 28, PAD_BODY + TITLE_H / 2 + 0.5f, 0, hex(PAL(text, PAPER_TEXT), 1), b->text);
}

static void draw_title(Cx *c, const char *title, float x, float y)
{
    char fit[160];
    text_fit(c->skin->semi, 30, 0, title, 860, fit, sizeof fit);
    int w = (int)ceilf(text_width(c->skin->semi, 30, 0, fit)) + 56;
    w += w & 1;
    TitleBake tb = {fit, card_get(w, TITLE_H, 6.0f, 43u, PAD_BODY)};
    const PaperShadows *sh = shadows_get(w, TITLE_H, 6.0f, 0.0f, 2);
    Sprs sp;
    sprs_bake(&sp, (float)w, TITLE_H, PAD_BODY, PAD_REST, rad(0.6f), sh ? sh->s[0] : NULL, NULL, 0, 0, draw_title_body, &tb);
    if (sp.rest) paper_spr_put(c->s, sp.rest, (int)x - PAD_REST, (int)y - PAD_REST);
    sprs_free(&sp);
}

// the sheet: its shadow, the paper, and two strips of tape holding its top edge; (xl, xr) are where the strips start, from the sheet's
// left edge, and y0 their top from the sheet's top
static void draw_sheet(Cx *c, float x, float y, float w, float h, float xl, float xr, float y0)
{
    const PaperShadows *sh = shadows_get((int)w, (int)h, 6.0f, 0.0f, 2);
    if (sh) paper_spr_put(c->s, sh->s[0], (int)x, (int)y);
    const PaperSpr *card = card_get((int)w, (int)h, 6.0f, 45u, PAD_BODY);
    if (card) paper_spr_put(c->s, card, (int)x, (int)y);
    PaperTape tp[2] = {{84, 24, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, 34.0f, 61}, {84, 24, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 34.0f, 62}}; // (across two opposite corners)
    float tx[2] = {xl, xr}, ty[2] = {h - 18.0f, y0}; // (opposite corners: bottom left and top right, so the first slip's own tape is not buried)
    for (int i = 0; i < 2; i++) {
        paper_tape_host(&tp[i], w, h, 6.0f, tx[i], ty[i]);
        PaperSpr *t = paper_tape(&tp[i]);
        if (t) paper_spr_put(c->s, t, (int)(x + tx[i] + tp[i].len * 0.5f), (int)(y + ty[i] + tp[i].wid * 0.5f));
        paper_spr_free(t);
    }
}

static void render_base(const UiList *l, const Geo *g)
{
    Cx c = ui_cx(G.base);
    Skin *u = c.skin;
    skin_board(&c);
    SkinStatus st;
    ui_status(&st);
    skin_top_bar(&c, l->right, l->right ? NULL : &st);
    if (l->title) {
        draw_title(&c, l->title, SHEET_X, TITLE_Y);
        if (l->subtitle) skin_txtc(&c, u->med, 19, 0, SHEET_X + 6, 174, 0, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), l->subtitle);
    }
    if (l->draw_header && l->header_h > 0) l->draw_header(&c, l->user, g->header_y);
    draw_sheet(&c, SHEET_X, g->y0, SHEET_W, g->h, -22, SHEET_W - 82, -12);
    if (l->n == 0)
        skin_txtc(&c, u->med, 21, 0, SHEET_X + SHEET_W / 2.0f, g->y0 + g->h / 2, 1, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), l->empty ? l->empty : "Nothing here yet.");
    if (l->prompts) skin_footer(&c, l->prompts, l->nprompts);
    else skin_footer(&c, DEFAULT_PROMPTS, 2);
}

// ------------------------------------------------------------------------------------------------ lifts and poses

typedef struct {
    int idx;
    PaperLift lift;
} LiftEnt;

// what one row looks like this frame (and what it covered)
typedef struct {
    int idx, moving, ry, cached;
    float p, cx, cy, sc, ang, gh;
    uint32_t rkey;
    const RowEnt *re;
    const void *peel;
    TapeEnt *te;
    Rect bb;
} RowFr;

typedef struct {
    LiftEnt le[NLIFT];
    int nle;
    int focus;
    RowFr cur[MAXR], prev[MAXR];
    int ncur, nprev;
    int marks, marks_prev;
    int force_full;
    struct {
        int on, dir; // running; +1 when the list went down (the slips rise into place), -1 when it went up
        double t0;
    } casc;
} LCtx;

static void ent_up(LiftEnt *e)
{
    paper_lift_init(&e->lift, LIFT_ROW);
    e->lift.target = 1;
    e->lift.from = 1;
    e->lift.dur = 0;
    e->lift.wig_t0 = -1;
    e->lift.wig_rest = -0.4f;
}

static LiftEnt *lift_find(LCtx *c, int idx)
{
    for (int i = 0; i < c->nle; i++)
        if (c->le[i].idx == idx) return &c->le[i];
    return NULL;
}

static LiftEnt *lift_get(LCtx *c, int idx, double now)
{
    LiftEnt *e = lift_find(c, idx);
    if (e) return e;
    if (c->nle == NLIFT) { // full: let go of the one that is lowest (not the focus)
        int worst = -1;
        float lo = 2.0f;
        for (int i = 0; i < c->nle; i++) {
            float v = paper_lift_value(&c->le[i].lift, now);
            if (c->le[i].idx != c->focus && v < lo) {
                lo = v;
                worst = i;
            }
        }
        if (worst < 0) worst = 0;
        c->le[worst] = c->le[--c->nle];
    }
    e = &c->le[c->nle++];
    e->idx = idx;
    paper_lift_init(&e->lift, LIFT_ROW);
    return e;
}

static void lift_prune(LCtx *c, double now)
{
    for (int i = 0; i < c->nle;) {
        LiftEnt *e = &c->le[i];
        if (e->idx != c->focus && !paper_lift_busy(&e->lift, now) && paper_lift_value(&e->lift, now) < 0.002f) c->le[i] = c->le[--c->nle];
        else i++;
    }
}

// the focus starts up, finished with its wiggle, nothing else lifted
static void lifts_reset(LCtx *c, const UiList *l)
{
    c->nle = 0;
    c->focus = l->n > 0 ? l->sel : -1;
    if (c->focus >= 0) {
        LiftEnt *e = &c->le[c->nle++];
        e->idx = c->focus;
        ent_up(e);
    }
}

// the focus moved: the old slip sticks back down, the new one peels up (from wherever each is now)
static void focus_sync(LCtx *c, const UiList *l, int old, int now_sel, double now, int held)
{
    if (old == now_sel) return;
    if (old >= 0 && old < l->n) {
        LiftEnt *e = lift_find(c, old);
        if (!e) {
            e = lift_get(c, old, now);
            ent_up(e);
        }
        paper_lift_release(&e->lift, now, held);
    }
    if (now_sel >= 0 && now_sel < l->n) {
        TapeEnt *te;
        tape_for_row(now_sel, &te);
        peel_ensure(te);
        LiftEnt *e = lift_get(c, now_sel, now);
        paper_lift_grab(&e->lift, now, held);
    }
    c->focus = now_sel;
}

// ---- boxes

static int box_empty(const int b[4]) { return b[0] >= b[2]; }

static void box_add(int b[4], float x0, float y0, float x1, float y1)
{
    int ax = (int)floorf(x0) - 1, ay = (int)floorf(y0) - 1, bx = (int)ceilf(x1) + 1, by = (int)ceilf(y1) + 1;
    if (box_empty(b)) {
        b[0] = ax; b[1] = ay; b[2] = bx; b[3] = by;
        return;
    }
    if (ax < b[0]) b[0] = ax;
    if (ay < b[1]) b[1] = ay;
    if (bx > b[2]) b[2] = bx;
    if (by > b[3]) b[3] = by;
}

static void box_xform(int b[4], float cx, float cy, float w, float h, float sc, float ang)
{
    float ca = fabsf(cosf(ang)), sa = fabsf(sinf(ang));
    float ex = (ca * w + sa * h) * 0.5f * sc + 2.0f, ey = (sa * w + ca * h) * 0.5f * sc + 2.0f;
    box_add(b, cx - ex, cy - ey, cx + ex, cy + ey);
}

static int rect_hit(const Rect *a, const Rect *b)
{
    return a->w > 0 && b->w > 0 && a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static void shadow_pos(const RowFr *f, const Geo *g, const PaperSpr *sp, int *ax, int *ay)
{
    *ax = (int)lroundf(f->cx - g->slip_w * 0.5f) - sp->ox;
    *ay = (int)lroundf(f->cy - g->slip_h * 0.5f) - sp->oy;
}

typedef struct {
    const PaperShadows *sh;
    const PaperSpr *ghost;
    float tdx, tdy;
} RowSets;

static void rows_sets(const Geo *g, RowSets *rs)
{
    rs->sh = shadows_get((int)g->slip_w, (int)g->slip_h, 6.0f, paper_lift_k[LIFT_ROW].scale, PAPER_SHADOW_LEVELS);
    rs->ghost = ghost_get((int)g->slip_w, (int)g->slip_h, 7.0f);
    paper_tape_offset(g->slip_w, g->slip_h, -15.0f, -9.0f, 64.0f, 24.0f, &rs->tdx, &rs->tdy);
}

// the box of a lifted slip: its body, shadow and tape (and the ghost patch left where it sat)
static void lifted_box(int b[4], const RowFr *f, const Geo *g, const RowSets *rs, int with_ghost)
{
    const Sprs *sp = &f->re->sp;
    box_xform(b, f->cx, f->cy, (float)sp->body->w, (float)sp->body->h, f->sc, f->ang);
    if (rs->sh) {
        const PaperSpr *s = rs->sh->s[paper_shadow_level(rs->sh, f->p)];
        int ax, ay;
        shadow_pos(f, g, s, &ax, &ay);
        box_add(b, (float)ax, (float)ay, (float)(ax + s->w), (float)(ay + s->h));
    }
    if (rs->ghost && with_ghost)
        box_xform(b, g->slip_x + g->slip_w * 0.5f, (float)f->ry + g->slip_h * 0.5f, (float)rs->ghost->w, (float)rs->ghost->h, 1.0f, 0.0f);
    PaperXf cf = {f->cx, f->cy, f->sc, f->ang};
    PaperXf t = paper_xf_attach(cf, rs->tdx, rs->tdy);
    const TapeEnt *e = f->te;
    int tw = e->peel ? e->peel->s[0]->w : e->stuck ? e->stuck->w : 0, th = e->peel ? e->peel->s[0]->h : e->stuck ? e->stuck->h : 0;
    if (tw) box_xform(b, t.x, t.y, (float)tw, (float)th, t.scale, t.angle);
}

static void row_bounds(RowFr *f, const Geo *g, const RowSets *rs)
{
    int b[4] = {0, 0, 0, 0};
    const Sprs *sp = &f->re->sp;
    if (!f->moving) {
        int x = (int)lroundf(g->slip_x) - sp->rox, y = f->ry - sp->roy;
        box_add(b, (float)x, (float)y, (float)(x + sp->rest->w), (float)(y + sp->rest->h));
    } else {
        lifted_box(b, f, g, rs, 1);
    }
    Rect r = {b[0], b[1], b[2] - b[0], b[3] - b[1]};
    f->bb = r;
}

// The settled lifted slip (shadow, body turned and scaled, peeled tape) made once into one picture, which is then put down with whole
// pixel offsets as the sheet scrolls (turning and scaling a slip this wide is the most expensive thing a frame can do). It is baked
// when nothing else is happening, for the slip that has the focus; until it is ready the slip is turned and scaled as usual.
typedef struct {
    uint32_t key;
    PaperSpr *spr;
    int ox, oyr;                // where its top left lands: x, and y from the slip's row top (ry)
    float px, py, sc, ang;      // the pose it was made for
} LiftCache;
static LiftCache g_lc;

typedef struct {
    const RowFr *f;
    const Geo *g;
    const RowSets *rs;
    int Ox, Oy;
} LcBake;

static void draw_lc(Surf *s, void *ud)
{
    const LcBake *b = ud;
    const RowFr *f = b->f;
    if (b->rs->sh) {
        const PaperSpr *sp = b->rs->sh->s[paper_shadow_level(b->rs->sh, f->p)];
        int ax, ay;
        shadow_pos(f, b->g, sp, &ax, &ay);
        gfx_blit_over(s, sp->px, sp->w, sp->h, ax - b->Ox, ay - b->Oy);
    }
    xform_fast(s, f->re->sp.body, f->cx - b->Ox, f->cy - b->Oy, f->sc, f->ang);
    PaperXf cf = {f->cx, f->cy, f->sc, f->ang};
    const PaperSpr *ts = f->te->peel ? paper_peel_at(f->te->peel, f->p) : f->te->stuck;
    if (ts) {
        PaperXf t = paper_xf_attach(cf, b->rs->tdx, b->rs->tdy);
        paper_spr_xform(s, ts, t.x - b->Ox, t.y - b->Oy, t.scale, t.angle, 1.0f);
    }
}

// can this frame's pose of the focused slip be drawn from the cache? (settled: all the way up, nothing wiggling, not leaning)
static int lc_usable(const RowFr *f, const PaperPose *ps, int settled)
{
    return settled && g_lc.spr && g_lc.key == f->rkey && g_lc.px == ps->x && g_lc.py == ps->y && g_lc.sc == ps->scale && g_lc.ang == ps->angle &&
           f->te->peel;
}

// ---- painting

static void put(Surf *s, const PaperSpr *p, int x, int y, int ox, int oy) { gfx_blit_over(s, p->px, p->w, p->h, x - ox, y - oy); }

static void draw_resting(Surf *s, int ox, int oy, const RowFr *f, const Geo *g)
{
    const Sprs *sp = &f->re->sp;
    put(s, sp->rest, (int)lroundf(g->slip_x) - sp->rox, f->ry - sp->roy, ox, oy);
}

static void draw_ghost(Surf *s, int ox, int oy, const RowFr *f, const Geo *g, const RowSets *rs)
{
    if (!rs->ghost || f->gh <= 0.004f) return;
    // a plain copy at full strength, never faded in: the slip still covers it at the start of the lift (fading a patch this wide in is
    // a whole turned-and-scaled picture a frame; the picture is even in both ways, so the copy lands on whole pixels)
    put(s, rs->ghost, (int)lroundf(g->slip_x + g->slip_w * 0.5f - rs->ghost->w * 0.5f), (int)lroundf((float)f->ry + g->slip_h * 0.5f - rs->ghost->h * 0.5f), ox, oy);
}

static void draw_lifted(Surf *s, int ox, int oy, const RowFr *f, const Geo *g, const RowSets *rs)
{
    if (f->cached) {
        put(s, g_lc.spr, g_lc.ox, g_lc.oyr + f->ry, ox, oy);
        return;
    }
    if (rs->sh) {
        const PaperSpr *sp = rs->sh->s[paper_shadow_level(rs->sh, f->p)];
        int ax, ay;
        shadow_pos(f, g, sp, &ax, &ay);
        put(s, sp, ax, ay, ox, oy);
    }
    xform_fast(s, f->re->sp.body, f->cx - ox, f->cy - oy, f->sc, f->ang);
    PaperXf cf = {f->cx, f->cy, f->sc, f->ang};
    const TapeEnt *e = f->te;
    const PaperSpr *sp = e->peel ? paper_peel_at(e->peel, f->p) : e->stuck;
    if (!sp) return;
    PaperXf t = paper_xf_attach(cf, rs->tdx, rs->tdy);
    paper_spr_xform(s, sp, t.x - ox, t.y - oy, t.scale, t.angle, 1.0f);
}

typedef struct {
    Rect up, down;
} MarkRects;

static void mark_rects(const Geo *g, MarkRects *m)
{
    int cx = SHEET_X + SHEET_W / 2;
    m->up = (Rect){cx - 14, (int)g->y0 - 20, 28, 16};
    m->down = (Rect){cx - 14, (int)(g->y0 + g->h) + 6, 28, 16};
}

// small arrows where there is more above or below the sheet
static void draw_marks(Surf *s, int ox, int oy, const Geo *g, int marks)
{
    float cx = SHEET_X + SHEET_W / 2.0f - ox;
    Col k = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);
    if (marks & 1) {
        float y = g->y0 - oy;
        gfx_line(s, cx - 9, y - 8, cx, y - 14, 2.6f, k);
        gfx_line(s, cx, y - 14, cx + 9, y - 8, 2.6f, k);
    }
    if (marks & 2) {
        float y = g->y0 + g->h - oy + 10;
        gfx_line(s, cx - 9, y, cx, y + 6, 2.6f, k);
        gfx_line(s, cx, y + 6, cx + 9, y, 2.6f, k);
    }
}

static Surf *scratch_for(int w, int h)
{
    static Surf scr;
    if (!scr.px) scr.px = malloc((size_t)SCREEN_W * SCREEN_H * 4);
    scr.w = w;
    scr.h = h;
    return &scr;
}

// the rectangle R of `dst` made anew: the base with every row that touches it drawn over (rows scrolling under an edge of the sheet
// are cut off there)
static void paint_rect(LCtx *c, const Geo *g, const RowSets *rs, Surf *dst, const Rect *R)
{
    Surf *scr = scratch_for(R->w, R->h);
    if (!scr->px) return;
    for (int y = 0; y < R->h; y++)
        memcpy(scr->px + (size_t)y * R->w * 4, G.base->px + ((size_t)(R->y + y) * SCREEN_W + R->x) * 4, (size_t)R->w * 4);
    int ox = R->x, oy = R->y;
    for (int i = 0; i < c->ncur; i++) { // 1. what lies still
        const RowFr *f = &c->cur[i];
        if (!f->moving && rect_hit(&f->bb, R)) draw_resting(scr, ox, oy, f, g);
    }
    for (int i = 0; i < c->ncur; i++) { // 2. the patches left where slips were lifted
        const RowFr *f = &c->cur[i];
        if (f->moving && rect_hit(&f->bb, R)) draw_ghost(scr, ox, oy, f, g, rs);
    }
    int order[MAXR], n = 0;
    for (int i = 0; i < c->ncur; i++)
        if (c->cur[i].moving && rect_hit(&c->cur[i].bb, R)) order[n++] = i;
    for (int a = 1; a < n; a++) { // 3. what is lifted or settling, the lowest first
        int v = order[a], b = a;
        while (b > 0 && c->cur[order[b - 1]].p > c->cur[v].p) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = v;
    }
    for (int k = 0; k < n; k++) draw_lifted(scr, ox, oy, &c->cur[order[k]], g, rs);
    MarkRects mr;
    mark_rects(g, &mr);
    if (c->marks && (rect_hit(&mr.up, R) || rect_hit(&mr.down, R))) draw_marks(scr, ox, oy, g, c->marks);
    for (int y = 0; y < R->h; y++)
        memcpy(dst->px + ((size_t)(R->y + y) * SCREEN_W + R->x) * 4, scr->px + (size_t)y * R->w * 4, (size_t)R->w * 4);
}

// the whole screen made from scratch and compared with what the rectangles made (tests)
static void oracle_check(LCtx *c, const Geo *g, const RowSets *rs)
{
    static Surf *full;
    if (!full) full = surf_new(SCREEN_W, SCREEN_H);
    Rect all = {0, 0, SCREEN_W, SCREEN_H};
    paint_rect(c, g, rs, full, &all);
    long bad = 0, soft = 0;
    for (size_t i = 0, n = (size_t)SCREEN_W * SCREEN_H; i < n; i++) {
        const uint8_t *a = full->px + i * 4, *b = G.work->px + i * 4;
        int d0 = abs(a[0] - b[0]), d1 = abs(a[1] - b[1]), d2 = abs(a[2] - b[2]);
        if (d0 | d1 | d2) soft++;
        if (d0 > 1 || d1 > 1 || d2 > 1) bad++;
    }
    g_home_oracle_frames++;
    g_home_oracle_soft += soft;
    g_home_oracle_bad += bad;
}

static int row_same(const RowFr *a, const RowFr *b)
{
    return a->moving == b->moving && a->ry == b->ry && a->cached == b->cached && a->p == b->p && a->cx == b->cx && a->cy == b->cy &&
           a->sc == b->sc && a->ang == b->ang && a->gh == b->gh && a->rkey == b->rkey && a->peel == b->peel;
}

// How far (px) the slip in slot `slot` of `nvis` is out of line at `now`, in the wave after a jump to a new page. The wave starts at the
// far end from the focus (the top when the list went down), and each slip rises into place from CASC_AMP below (or falls from above).
static int casc_dy(const LCtx *c, int slot, int nvis, double now)
{
    if (!c->casc.on) return 0;
    int k = c->casc.dir > 0 ? slot : nvis - 1 - slot;
    double t = now - c->casc.t0 - k * CASC_STAGGER_MS; // (times are in ms)
    if (t >= CASC_MS) return 0;
    double u = 1.0 - (t > 0 ? t : 0.0) / CASC_MS;
    return (int)lround(c->casc.dir * CASC_AMP * u * u * u);
}

static int casc_running(const LCtx *c, int nvis, double now)
{
    return c->casc.on && now - c->casc.t0 < CASC_MS + (nvis - 1) * CASC_STAGGER_MS;
}

// Makes this frame at time `now`: every visible row's pose, what changed, the changed rectangles painted into G.work and shown.
// (lean_idx, lx, ly) is a row that leans. Returns 1 while any slip is still moving (lifting, wiggling).
static int list_frame(LCtx *c, const UiList *l, const Geo *g, double now, int lean_idx, int lx, int ly)
{
    int busy_any = 0;
    for (int i = 0; i < c->nle; i++)
        if (paper_lift_busy(&c->le[i].lift, now)) busy_any = 1;
    RowSets rs;
    rows_sets(g, &rs);
    g_re_gen++;
    g_tp_gen++;
    int i0 = l->scroll, i1 = l->scroll + g->vis - 1;
    if (i0 < 0) i0 = 0;
    if (i1 > l->n - 1) i1 = l->n - 1;
    c->ncur = 0;
    g_ui_list_off = 0;
    for (int idx = i0; idx <= i1 && c->ncur < MAXR; idx++) {
        RowFr *f = &c->cur[c->ncur];
        memset(f, 0, sizeof *f);
        f->idx = idx;
        int dy = idx == c->focus ? 0 : casc_dy(c, idx - l->scroll, g->vis, now);
        if (abs(dy) > g_ui_list_off) g_ui_list_off = abs(dy);
        f->ry = (int)lroundf(g->y0 + SLIP_INSET + (idx - l->scroll) * g->pitch) + dy;
        PaperPose ps;
        memset(&ps, 0, sizeof ps);
        ps.scale = 1.0f;
        LiftEnt *e = lift_find(c, idx);
        int mv = 0;
        if (e) {
            paper_lift_pose(&e->lift, now, g->slip_w, &ps);
            mv = ps.p > 0.002f || paper_lift_busy(&e->lift, now);
        }
        int lean = idx == lean_idx && (lx || ly);
        if (lean) mv = 1;
        f->re = row_get(l, g, idx, 1);
        if (!f->re) continue;
        f->rkey = f->re->key;
        f->moving = mv;
        f->p = ps.p;
        f->cx = g->slip_x + g->slip_w * 0.5f + ps.x + (lean ? (float)lx : 0.0f);
        f->cy = f->ry + g->slip_h * 0.5f + ps.y + (lean ? (float)ly : 0.0f);
        f->sc = ps.scale;
        f->ang = ps.angle;
        f->gh = ps.ghost;
        tape_for_row(idx, &f->te);
        if (mv) {
            peel_ensure(f->te);
            f->peel = f->te->peel;
        }
        f->cached = lc_usable(f, &ps, e && mv && !lean && !paper_lift_busy(&e->lift, now) && ps.p >= 0.9999f);
        row_bounds(f, g, &rs);
        c->ncur++;
    }
    lift_prune(c, now);
    // what changed
    Dirty d;
    dirty_clear(&d);
    if (c->force_full) {
        dirty_all(&d);
    } else {
        for (int i = 0; i < c->ncur; i++) {
            const RowFr *f = &c->cur[i];
            const RowFr *pv = NULL;
            for (int j = 0; j < c->nprev; j++)
                if (c->prev[j].idx == f->idx) pv = &c->prev[j];
            if (pv && row_same(f, pv)) continue;
            dirty_add_rect(&d, f->bb);
            if (pv) dirty_add_rect(&d, pv->bb);
        }
        for (int j = 0; j < c->nprev; j++) {
            int still = 0;
            for (int i = 0; i < c->ncur; i++)
                if (c->cur[i].idx == c->prev[j].idx) still = 1;
            if (!still) dirty_add_rect(&d, c->prev[j].bb);
        }
        if (c->marks != c->marks_prev) {
            MarkRects mr;
            mark_rects(g, &mr);
            dirty_add_rect(&d, mr.up);
            dirty_add_rect(&d, mr.down);
        }
    }
    if (d.full || d.n) {
        Rect full = {0, 0, SCREEN_W, SCREEN_H};
        if (d.full) {
            paint_rect(c, g, &rs, G.work, &full);
            ui_present_rects(G.work, &full, 1);
        } else {
            for (int i = 0; i < d.n; i++) paint_rect(c, g, &rs, G.work, &d.r[i]);
            ui_present_rects(G.work, d.r, d.n);
        }
        if (g_home_oracle) oracle_check(c, g, &rs);
    }
    memcpy(c->prev, c->cur, sizeof c->cur);
    c->nprev = c->ncur;
    c->marks_prev = c->marks;
    c->force_full = 0;
    return busy_any;
}

// the screen as it should be shown when the page is left (nothing in flight), whole: what dialogs dim and the next page fades from
static void list_leave(LCtx *c, const UiList *l, const Geo *g)
{
    if (l->n > 0) {
        lifts_reset(c, l);
        c->casc.on = 0;
        c->force_full = 1;
        c->marks = (l->scroll > 0 ? 1 : 0) | (l->scroll + g->vis < l->n ? 2 : 0);
        list_frame(c, l, g, (double)ui_now(), -1, 0, 0);
    }
    ui_screen_done();
}

// ---- idle-time baking: the rows next to the ones showing, and the peeling states of the tape of the rows the focus can reach next
static int lc_bake(LCtx *c, const UiList *l, const Geo *g, int do_one)
{
    if (c->focus < 0 || c->focus >= l->n) return 0;
    double now = (double)ui_now();
    LiftEnt *e = lift_find(c, c->focus);
    if (!e || paper_lift_busy(&e->lift, now)) return 0;
    PaperPose ps;
    paper_lift_pose(&e->lift, now, g->slip_w, &ps);
    if (ps.p < 0.9999f) return 0;
    RowEnt *re = row_get(l, g, c->focus, 0);
    TapeEnt *te;
    tape_for_row(c->focus, &te);
    if (!re || !te->peel) return 0; // (the rows and the peeling states come first)
    if (g_lc.spr && g_lc.key == re->key && g_lc.px == ps.x && g_lc.py == ps.y && g_lc.sc == ps.scale && g_lc.ang == ps.angle) return 0;
    if (!do_one) return 1;
    RowSets rs;
    rows_sets(g, &rs);
    RowFr f;
    memset(&f, 0, sizeof f);
    f.idx = c->focus;
    f.moving = 1;
    f.ry = (int)lroundf(g->y0 + SLIP_INSET + c->focus * g->pitch - l->scroll * g->pitch);
    f.p = ps.p;
    f.cx = g->slip_x + g->slip_w * 0.5f + ps.x;
    f.cy = f.ry + g->slip_h * 0.5f + ps.y;
    f.sc = ps.scale;
    f.ang = ps.angle;
    f.gh = ps.ghost;
    f.re = re;
    f.rkey = re->key;
    f.te = te;
    int b[4] = {0, 0, 0, 0};
    lifted_box(b, &f, g, &rs, 0);
    LcBake lb = {&f, g, &rs, b[0], b[1]};
    PaperSpr *spr = paper_bake(b[2] - b[0], b[3] - b[1], draw_lc, &lb);
    if (!spr) return 0;
    paper_spr_free(g_lc.spr);
    g_lc = (LiftCache){re->key, spr, b[0], b[1] - f.ry, ps.x, ps.y, ps.scale, ps.angle};
    return 1;
}

static int bake_work(LCtx *c, const UiList *l, const Geo *g, int do_one)
{
    int a = l->scroll - 1, b = l->scroll + g->vis;
    for (int idx = a; idx <= b; idx++) {
        if (idx < 0 || idx >= l->n) continue;
        if (!row_get(l, g, idx, 0)) {
            if (do_one) row_get(l, g, idx, 1);
            return 1;
        }
    }
    for (int d = 0; d <= 2; d++) {
        for (int sgn = -1; sgn <= 1; sgn += 2) {
            int idx = l->sel + sgn * d;
            if (idx < 0 || idx >= l->n || (d == 0 && sgn > 0)) continue;
            TapeEnt *te;
            tape_for_row(idx, &te);
            if (!te->peel && !te->tried) {
                if (do_one) peel_ensure(te);
                return 1;
            }
        }
    }
    return lc_bake(c, l, g, do_one);
}

// ------------------------------------------------------------------------------------------------ the page

int ui_list(UiList *l, uint64_t *pressed)
{
    if (pressed) *pressed = 0;
    if (G.quit) return -1;
    if (l->n > 0) {
        if (l->sel >= l->n) l->sel = l->n - 1;
        if (l->sel < 0) l->sel = 0;
    }
    Geo g;
    geometry(l, &g);
    if (l->sel < l->scroll) l->scroll = l->sel;
    if (l->sel >= l->scroll + g.vis) l->scroll = l->sel - g.vis + 1;
    if (l->scroll > l->n - g.vis) l->scroll = l->n - g.vis;
    if (l->scroll < 0) l->scroll = 0;
    list_pal_check();
    render_base(l, &g);
    ui_transition();

    static LCtx ctxs;
    LCtx *c = &ctxs;
    memset(c, 0, sizeof *c);
    lifts_reset(c, l);
    c->force_full = 1;

    Bump bump = {0};
    int lean_row = -1; // the row that leans after a press that leads nowhere
    SkinStatus st0;
    ui_status(&st0);
    for (;;) {
        double now = (double)ui_now();
        float lox, loy;
        int bmoving = bump_offset(&bump, &lox, &loy);
        int lx = (int)lroundf(lox), ly = (int)lroundf(loy), leaning = bump.on && (lx || ly);
        c->marks = (l->scroll > 0 ? 1 : 0) | (l->scroll + g.vis < l->n ? 2 : 0);
        int moving = 0;
        if (l->n > 0) {
            moving = list_frame(c, l, &g, now, leaning ? lean_row : -1, lx, ly);
        } else if (c->force_full) {
            Rect full = {0, 0, SCREEN_W, SCREEN_H};
            surf_copy(G.work, G.base);
            ui_present_rects(G.work, &full, 1);
            c->force_full = 0;
        }
        moving |= casc_running(c, g.vis, now) | bmoving;

        int pending = !moving && l->n > 0 && bake_work(c, l, &g, 0);
        uint64_t b = ui_poll(moving || pending ? 0 : 1000);
        int held = G.plat->repeated != 0;
        if (b == UI_QUIT) {
            if (pressed) *pressed = UI_QUIT;
            list_leave(c, l, &g);
            return -1;
        }
        if (b & UI_REDRAW) { // the theme changed under it (with a new palette, the rows are made again in its colours)
            if (list_pal_check()) c->ncur = c->nprev = 0; // (the last frames' rows were pictures that are gone)
            render_base(l, &g);
            c->force_full = 1;
            b &= ~UI_REDRAW;
        }
        int old = l->sel, pdx = 0, pdy = 0; // (pdx, pdy: a direction that leads nowhere)
        int touched = 0;
        if (b & UI_TOUCH) { // tap a row to choose it; drag to scroll
            static float acc;
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind == TOUCH_END) {
                    acc = 0;
                } else if (te.kind == TOUCH_DRAG && l->n > g.vis) {
                    acc -= te.dy;
                    while (acc >= g.pitch && l->scroll + g.vis < l->n) { l->scroll++; acc -= g.pitch; }
                    while (acc <= -g.pitch && l->scroll > 0) { l->scroll--; acc += g.pitch; }
                    if (l->sel < l->scroll) l->sel = l->scroll;
                    if (l->sel >= l->scroll + g.vis) l->sel = l->scroll + g.vis - 1;
                    c->casc.on = 0; // (a drag steps through rows all the time: no wave)
                    touched = 1;
                } else if (te.kind == TOUCH_TAP) {
                    for (int i = 0; i < g.vis && l->scroll + i < l->n; i++)
                        if (in_rect(te.x, te.y, SHEET_X, g.y0 + SLIP_INSET - 3 + i * g.pitch, SHEET_W, g.pitch)) {
                            l->sel = l->scroll + i;
                            b |= BTN_A;
                        }
                }
            }
        }
        if (!b && !touched) {
            if (!moving) { // the clock or battery may have changed
                SkinStatus st;
                ui_status(&st);
                if (strcmp(st.clock, st0.clock) || st.battery != st0.battery || st.charging != st0.charging) {
                    st0 = st;
                    render_base(l, &g);
                    c->force_full = 1;
                } else if (pending) {
                    bake_work(c, l, &g, 1); // a quiet moment: bake something
                }
            }
            continue;
        }
        if (b & BTN_DOWN) { if (l->sel < l->n - 1) l->sel++; else if (l->n > 1 && wrap_ok()) l->sel = 0; else pdy = 1; }
        else if (b & BTN_UP) { if (l->sel > 0) l->sel--; else if (l->n > 1 && wrap_ok()) l->sel = l->n - 1; else pdy = -1; }
        else if (b & (BTN_RIGHT | BTN_R)) {
            if (l->n) l->sel = l->sel + g.vis < l->n ? l->sel + g.vis : l->n - 1;
            if (l->sel == old && (b & BTN_RIGHT)) pdx = 1;
        } else if (b & (BTN_LEFT | BTN_L)) {
            if (l->n) l->sel = l->sel - g.vis > 0 ? l->sel - g.vis : 0;
            if (l->sel == old && (b & BTN_LEFT)) pdx = -1;
        }
        else if (b & BTN_A) {
            if (l->n > 0) {
                if (pressed) *pressed = BTN_A;
                {
                    Rect rr = {SHEET_X, (int)(g.y0 + SLIP_INSET - 3 + (l->sel - l->scroll) * g.pitch), SHEET_W, (int)g.pitch};
                    Rect nb[8]; // (the slips above and below stay as they are: only this one is pushed flat)
                    int nn = 0;
                    for (int i = 0; i < c->ncur && nn < 8; i++)
                        if (c->cur[i].idx != l->sel && abs(c->cur[i].idx - l->sel) <= 2) nb[nn++] = c->cur[i].bb;
                    ui_press_keep(nb, nn);
                    RowSets rs0; // (the slip's own tape goes in with it: it is not the neighbours' to keep still)
                    rows_sets(&g, &rs0);
                    for (int i = 0; i < c->ncur; i++)
                        if (c->cur[i].idx == l->sel && c->cur[i].moving && c->cur[i].te) {
                            const RowFr *f = &c->cur[i];
                            const PaperXf cf = {f->cx, f->cy, f->sc, f->ang}, t = paper_xf_attach(cf, rs0.tdx, rs0.tdy);
                            const TapeEnt *e = f->te;
                            const int tw = e->peel ? e->peel->s[0]->w : e->stuck ? e->stuck->w : 0, th = e->peel ? e->peel->s[0]->h : e->stuck ? e->stuck->h : 0;
                            int tb[4] = {0, 0, 0, 0};
                            if (tw) box_xform(tb, t.x, t.y, (float)tw, (float)th, t.scale, t.angle);
                            if (tb[0] < tb[2]) {
                                const Rect tr = {tb[0], tb[1], tb[2] - tb[0], tb[3] - tb[1]};
                                ui_press_free(&tr, 1);
                            }
                        }
                    Rect whole = {rr.x - 4, rr.y - 4, rr.w + 8, rr.h + 8}; // (the slip, its tape at the ends and its shadow, wherever the lift has put them)
                    for (int i = 0; i < c->ncur; i++)
                        if (c->cur[i].idx == l->sel && c->cur[i].bb.w > 0) {
                            const Rect bb = c->cur[i].bb;
                            const int wx0 = bb.x < whole.x ? bb.x : whole.x, wy0 = bb.y < whole.y ? bb.y : whole.y;
                            const int wx1 = bb.x + bb.w > whole.x + whole.w ? bb.x + bb.w : whole.x + whole.w;
                            const int wy1 = bb.y + bb.h > whole.y + whole.h ? bb.y + bb.h : whole.y + whole.h;
                            whole = (Rect){wx0, wy0, wx1 - wx0, wy1 - wy0};
                        }
                    ui_press_whole(whole);
                    RowSets rs; // (the dashed patch where the slip sat stays put: drawn at full strength, as draw_ghost does)
                    rows_sets(&g, &rs);
                    for (int i = 0; i < c->ncur; i++)
                        if (c->cur[i].idx == l->sel && rs.ghost && c->cur[i].gh > 0.004f) {
                            const RowFr *f = &c->cur[i];
                            ui_press_ghost(rs.ghost, g.slip_x + g.slip_w * 0.5f, (float)f->ry + g.slip_h * 0.5f, 0.0f, 1.0f);
                            if (f->moving) { // (and what the lifted slip covers of it stays covered)
                                const float ca = fabsf(cosf(f->ang)), sa = fabsf(sinf(f->ang));
                                const float ex = (ca * g.slip_w + sa * g.slip_h) * 0.5f * f->sc, ey = (sa * g.slip_w + ca * g.slip_h) * 0.5f * f->sc;
                                const int x0 = (int)ceilf(f->cx - ex), y0 = (int)ceilf(f->cy - ey), x1 = (int)floorf(f->cx + ex), y1 = (int)floorf(f->cy + ey);
                                ui_press_cover((Rect){x0, y0, x1 - x0, y1 - y0});
                            }
                        }
                    ui_press(G.work, rr);
                }
                list_leave(c, l, &g);
                return l->sel;
            }
        } else if (b & BTN_B) {
            if (pressed) *pressed = BTN_B;
            list_leave(c, l, &g);
            return -1;
        } else if (b & l->extra) {
            if (pressed) *pressed = b & l->extra;
            list_leave(c, l, &g);
            return l->n > 0 ? l->sel : -1;
        }
        if (l->sel != old) {
            int was = l->scroll;
            if (l->sel < l->scroll) l->scroll = l->sel;
            if (l->sel >= l->scroll + g.vis) l->scroll = l->sel - g.vis + 1;
            if (l->scroll != was) { // a jump to a new page: the slips settle in a wave (not while a D-pad is held, or the wave never ends)
                c->casc.on = !held && !touched;
                c->casc.dir = l->scroll > was ? 1 : -1;
                c->casc.t0 = (double)ui_now();
            }
            focus_sync(c, l, old, l->sel, (double)ui_now(), held || touched);
        }
        if ((pdx || pdy) && l->n > 0) {
            bump_start(&bump, pdx, pdy);
            lean_row = l->sel;
        }
    }
}

// ------------------------------------------------------------------------------------------------ the waiting page

// A note that ends in an ellipsis keeps it moving while the work goes on: its dots come one at a time, then go, over and over. The work
// gives it its moments (ui_busy_tick, from the long loops' pumps and from the waits on a worker); only the dots' own little place is shown again.
#define BUSY_DOT_MS 380
static struct {
    int on;
    float x, y, dw; // where the first dot goes (the text's left, its middle line), how far apart they are
    Rect r;         // the dots' place
    int shown;      // how many are showing
} g_bd;

void ui_busy_stop(void) { g_bd.on = 0; }

void ui_busy_tick(void)
{
    if (!g_bd.on || G.quit || !G.plat || !G.work || !G.base) return;
    int k = (int)((ui_now() / BUSY_DOT_MS) % 4); // 0, 1, 2, 3 dots
    if (k == g_bd.shown) return;
    g_bd.shown = k;
    for (int y = 0; y < g_bd.r.h; y++)
        memcpy(G.work->px + ((size_t)(g_bd.r.y + y) * SCREEN_W + g_bd.r.x) * 4, G.base->px + ((size_t)(g_bd.r.y + y) * SCREEN_W + g_bd.r.x) * 4, (size_t)g_bd.r.w * 4);
    Cx c = ui_cx(G.work);
    for (int i = 0; i < k; i++) skin_txtc(&c, c.skin->med, 21, 0, g_bd.x + i * g_bd.dw, g_bd.y, 0, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), ".");
    Surf *wf = ui_wait_frame(); // the "still working" crayon is drawing on the footer: the dots go on its copy of the page and are shown from it, so it carries on
    if (wf && G.plat->present_rects) {
        for (int y = 0; y < g_bd.r.h; y++)
            memcpy(wf->px + ((size_t)(g_bd.r.y + y) * SCREEN_W + g_bd.r.x) * 4, G.work->px + ((size_t)(g_bd.r.y + y) * SCREEN_W + g_bd.r.x) * 4, (size_t)g_bd.r.w * 4);
        G.plat->present_rects(G.plat, wf, &g_bd.r, 1);
        return;
    }
    ui_present_rects(G.work, &g_bd.r, 1);
}

void ui_busy(const char *title, const char *note)
{
    if (G.quit) return;
    Cx c = ui_cx(G.base);
    Skin *u = c.skin;
    skin_board(&c);
    skin_top_bar(&c, "Please wait", NULL);
    draw_title(&c, title, SHEET_X, TITLE_Y);
    draw_sheet(&c, SHEET_X, 190, SHEET_W, 130, -22, SHEET_W - 82, -12);
    const Col ink = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);
    const float cx = SHEET_X + SHEET_W / 2.0f, cy = 190 + 65;
    static const char ELL[] = "\xE2\x80\xA6";
    size_t n = strlen(note);
    char text[160];
    int dots = n > 3 && n < sizeof text && strcmp(note + n - 3, ELL) == 0;
    if (dots) { // (laid out as the whole note, so nothing shifts as the dots come and go)
        memcpy(text, note, n - 3);
        text[n - 3] = 0;
        float wt = text_width(u->med, 21, 0, text), w3 = text_width(u->med, 21, 0, "..."), x0 = cx - (wt + w3) / 2;
        skin_txtc(&c, u->med, 21, 0, x0, cy, 0, ink, text);
        g_bd.x = x0 + wt;
        g_bd.y = cy;
        g_bd.dw = w3 / 3;
        g_bd.r = (Rect){(int)floorf(g_bd.x) - 2, (int)floorf(cy - 20), (int)ceilf(w3) + 6, 40};
    } else {
        skin_txtc(&c, u->med, 21, 0, cx, cy, 1, ink, note);
    }
    skin_footer(&c, NULL, 0); // just the rule: nothing can be pressed
    ui_transition();
    surf_copy(G.work, G.base);
    ui_present(G.work);
    g_bd.on = dots;
    g_bd.shown = 0;
    ui_busy_tick();
    ui_screen_done();
}

// ------------------------------------------------------------------------------------------------ text viewer

enum { LOG_X = 100, LOG_Y = 178, LOG_W = 1080, LOG_H = 454, LOG_LINE = 38, LOG_VIS = 11 };

// a ruled notebook page taped at two corners, a line of text on every rule
static void viewer_base(const char *title, char **lines, int n, int top, int vis)
{
    Cx c = ui_cx(G.base);
    Skin *u = c.skin;
    skin_board(&c);
    SkinStatus st;
    ui_status(&st);
    skin_top_bar(&c, NULL, &st);
    draw_title(&c, title, LOG_X, TITLE_Y);
    draw_sheet(&c, LOG_X, LOG_Y, LOG_W, LOG_H, -22, LOG_W - 82, -12);
    float x = LOG_X, y = LOG_Y;
    for (int i = 0; i < vis; i++) // the rules, under every line
        gfx_rect(c.s, x + 4, y + 37 + i * LOG_LINE + 11, LOG_W - 8, 2, hex(PAL(rule, 0xE3DFEA), 1));
    for (int i = 0; i < vis && top + i < n; i++) {
        char ln[200];
        text_fit(u->med, 19, 0, lines[top + i], LOG_W - 100, ln, sizeof ln);
        skin_txtc(&c, u->med, 19, 0, x + 36, y + 37 + i * LOG_LINE, 0, hex(PAL(text, PAPER_TEXT), 1), ln);
    }
    if (n > vis) { // a quiet scroll bar
        float h = LOG_H - 60, th = h * vis / (float)n, ty = y + 30 + (h - th) * (n - vis > 0 ? top / (float)(n - vis) : 0);
        gfx_rrect(c.s, x + LOG_W - 20, ty, 6, th, 3, hex(PAL(thread, PAPER_THREAD), 0.5f));
    }
    static const Prompt pr[] = {{"B", "Back"}};
    skin_footer(&c, pr, 1);
}

void ui_text_viewer(const char *title, char **lines, int n, int start_at_end)
{
    if (G.quit) return;
    int vis = LOG_VIS;
    int top = start_at_end && n > vis ? n - vis : 0;
    viewer_base(title, lines, n, top, vis);
    ui_transition();
    float acc = 0;
    for (;;) {
        surf_copy(G.work, G.base);
        ui_present(G.work);
        uint64_t b = ui_poll(-1);
        if (b == UI_QUIT || (b & BTN_B)) break;
        int redraw = (b & UI_REDRAW) != 0;
        b &= ~UI_REDRAW;
        int old = top, maxtop = n > vis ? n - vis : 0;
        if (b & UI_TOUCH) { // drag the text
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind == TOUCH_END) acc = 0;
                else if (te.kind == TOUCH_DRAG) {
                    acc -= te.dy;
                    while (acc >= LOG_LINE && top < maxtop) { top++; acc -= LOG_LINE; }
                    while (acc <= -LOG_LINE && top > 0) { top--; acc += LOG_LINE; }
                }
            }
        }
        if (b & BTN_DOWN) top = top + 1 > maxtop ? maxtop : top + 1;
        else if (b & BTN_UP) top = top > 0 ? top - 1 : 0;
        else if (b & (BTN_RIGHT | BTN_R)) top = top + vis > maxtop ? maxtop : top + vis;
        else if (b & (BTN_LEFT | BTN_L)) top = top - vis < 0 ? 0 : top - vis;
        if (top != old || redraw) viewer_base(title, lines, n, top, vis);
    }
}
