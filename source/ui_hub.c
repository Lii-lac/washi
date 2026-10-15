// The admin hub, in Washi: four coloured paper tabs on the left (the chosen one is white and slides 12 px right while the focus is on
// the tabs), and on the right a white sheet taped to the board, with the rows of the chosen tab as slips on it. The focused slip is
// unstuck: it rises, its tape peels, a ghost patch stays where it sat, and it wiggles.
//
// A frame is made the way the home screen's is (see ui_home.c): the board, the top bar, the title, the sheet and the footer are the
// `base` and never move; the tabs and the slips are items with a baked resting picture and, when they move, a lifted pose. A frame
// works out each item's pose, collects the rectangles that changed, paints only those (base, then every item that touches them) and
// tells the platform which rectangles to show.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/paper.h"
#include "ui_internal.h"

static const Prompt HUB_PROMPTS[] = {{"A", "Open"}, {"B", "Back"}};

// ------------------------------------------------------------------------------------------------ geometry (the mockup's numbers)

enum { NTAB = 4, MAXROW = 5, NIT = NTAB + MAXROW };

#define TAB_X 40.0f
#define TAB_Y0 176.0f
#define TAB_DY 78.0f
#define TAB_H 62.0f
#define TAB_W 318.0f
#define TAB_WSEL 350.0f
#define SHEET_X 420.0f
#define SHEET_Y 168.0f
#define SHEET_W 820.0f
#define ROW_X 440.0f
#define ROW_W 780.0f
#define CARD_R 6.0f
enum { PAD_BODY = 14, PAD_REST = 22, PAD_SHEET = 44 };

// the tab colours (tape colours, the mockup's): blue, mint, yellow, pink
static const int TAB_TAPE[NTAB] = {5, 4, 2, 0};
static const uint32_t DISC[3] = {0xCFE4FB, 0xFFD8D6, 0xE9DCFC};
static const int TILE_KINDS[4] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG};

// A section of up to three rows has the mockup's slips (88 tall); one with more has smaller ones, so five fit above the footer. The
// sheet ends 22 px below its last slip, like the 20 px at its sides, so a lifted last slip and its shadow stay on the paper.
typedef struct {
    float rh, pitch, top, sheet_h;
    int compact;
} Met;

#define SHEET_FOOT 22.0f

static Met met(int n)
{
    Met m;
    m.compact = n > 3;
    m.rh = m.compact ? 68.0f : 88.0f;
    m.pitch = m.compact ? 78.0f : 100.0f;
    m.top = m.compact ? 40.0f : 42.0f;
    m.sheet_h = m.top + (n > 0 ? n - 1 : 0) * m.pitch + m.rh + SHEET_FOOT;
    return m;
}

static float tab_y(int i) { return TAB_Y0 + i * TAB_DY; }
static float row_y(const Met *m, int i) { return SHEET_Y + m->top + i * m->pitch; }

static int nsec(const UiHub *h) { return h->nsections > NTAB ? NTAB : h->nsections; }
static int nrows_of(const UiHubSection *s) { return s->nrows > MAXROW ? MAXROW : s->nrows < 0 ? 0 : s->nrows; }

static float rad(float deg) { return deg * 3.14159265f / 180.0f; }

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static uint32_t fnv_str(uint32_t h, const char *s) { return fnv(h, s ? s : "", s ? strlen(s) + 1 : 1); }
static uint32_t fnv_int(uint32_t h, int v) { return fnv(h, &v, sizeof v); }

// ------------------------------------------------------------------------------------------------ caches of baked things

typedef struct {
    int used;
    unsigned gen;
    PaperTape t;
    PaperSpr *stuck;
    PaperPeel *peel;
    int tried;
} TapeEnt;

enum { TAPE_CACHE = 24 };
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

static const PaperSpr *card_get(int w, int h, float r, uint32_t seed, int pad)
{
    enum { N = 16 };
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

// Shadow sets by size. A wide card's set is made from a narrow card's: the middle column of each level is repeated, so the blur (the
// expensive part) is done on a small picture.
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

static const PaperShadows *shadows_for(int w, int h, float r, float grow)
{
    enum { N = 8 };
    static struct {
        int w, h;
        float r, grow;
        PaperShadows *sh;
    } c[N];
    static int next;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: shadows of a new colour)
        for (int i = 0; i < N; i++) paper_shadows_free(c[i].sh);
        memset(c, 0, sizeof c);
    }
    for (int i = 0; i < N; i++)
        if (c[i].sh && c[i].w == w && c[i].h == h && c[i].r == r && c[i].grow == grow) return c[i].sh;
    int k = next;
    next = (next + 1) % N;
    paper_shadows_free(c[k].sh);
    c[k].w = w;
    c[k].h = h;
    c[k].r = r;
    c[k].grow = grow;
    c[k].sh = shadows_make(w, h, r, grow, PAPER_SHADOW_LEVELS);
    return c[k].sh;
}

// ------------------------------------------------------------------------------------------------ baking

typedef struct {
    PaperSpr *body; // the card (and what is printed on it), straight, `pad` px of room all round: what is lifted and turned
    PaperSpr *rest; // the card as it sits: shadow, turned a little, tape stuck on. Its card's top left is at (rox, roy)
    int pad, rox, roy;
} Sprs;

typedef struct {
    PaperTape t;
    float dx, dy; // where its middle is, from the card's middle
} TapeUse;

typedef struct {
    const PaperSpr *sh0, *body;
    float tilt, w, h;
    int rpad, ntp;
    const PaperSpr *tape[2];
    float dx[2], dy[2];
} RestBake;

static void draw_rest(Surf *s, void *ud)
{
    const RestBake *b = ud;
    float cx = b->rpad + b->w * 0.5f, cy = b->rpad + b->h * 0.5f;
    if (b->sh0) paper_spr_put(s, b->sh0, b->rpad, b->rpad);
    paper_spr_xform(s, b->body, cx, cy, 1.0f, b->tilt, 1.0f);
    PaperXf cf = {cx, cy, 1.0f, b->tilt};
    for (int i = 0; i < b->ntp; i++) {
        if (!b->tape[i]) continue;
        PaperXf t = paper_xf_attach(cf, b->dx[i], b->dy[i]);
        paper_spr_xform(s, b->tape[i], t.x, t.y, 1.0f, t.angle, 1.0f);
    }
}

// Bakes the body from `draw`, then the rest sprite from it (shadow `sh0`, the turn, the tapes).
static void sprs_bake(Sprs *o, float w, float h, int pad, int rpad, float tilt, const PaperSpr *sh0, const TapeUse *tp, int ntp, PaperDraw draw, void *ud)
{
    memset(o, 0, sizeof *o);
    o->pad = pad;
    o->body = paper_bake((int)w + 2 * pad, (int)h + 2 * pad, draw, ud);
    if (!o->body) return;
    RestBake rb;
    memset(&rb, 0, sizeof rb);
    rb.sh0 = sh0;
    rb.body = o->body;
    rb.tilt = tilt;
    rb.w = w;
    rb.h = h;
    rb.rpad = rpad;
    rb.ntp = ntp;
    for (int i = 0; i < ntp; i++) {
        rb.tape[i] = tape_ent(&tp[i].t)->stuck;
        rb.dx[i] = tp[i].dx;
        rb.dy[i] = tp[i].dy;
    }
    o->rest = paper_bake((int)w + 2 * rpad, (int)h + 2 * rpad, draw_rest, &rb);
    o->rox = o->roy = rpad;
}

static void sprs_free(Sprs *o)
{
    if (o->rest != o->body) paper_spr_free(o->rest);
    paper_spr_free(o->body);
    memset(o, 0, sizeof *o);
}

static const PaperSpr *sprs_rest(const Sprs *o) { return o->rest ? o->rest : o->body; }

// ---- a tab

typedef struct {
    const PaperSpr *card;
    const char *label;
    int sel;
    uint32_t rgb;
    int dyed; // (Paper night: a closed tab is paper of its colour, its writing pale)
    float w;
} TabBake;

static void draw_tab_body(Surf *s, void *ud)
{
    const TabBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const float pad = PAD_BODY;
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    if (b->sel) { // white paper with a strip of its colour down the left edge
        gfx_rrect(s, pad, pad, 30, TAB_H, CARD_R, hex(b->rgb, 1));
        gfx_rect(s, pad + 22, pad, 8, TAB_H, hex(PAL(paper, PAPER_PAPER), 1));
    } else if (b->dyed) { // paper of a colour from the night scene
        gfx_rrect(s, pad, pad, b->w, TAB_H, CARD_R, hex(b->rgb, 1));
    } else { // tinted paper (a pastel)
        gfx_rrect(s, pad, pad, b->w, TAB_H, CARD_R, hex(b->rgb, 0.8f));
    }
    char fit[96];
    float px = b->sel ? 26.0f : 24.0f;
    text_fit(u->semi, px, 0, b->label, b->w - 44 - 14, fit, sizeof fit);
    uint32_t ink = b->sel || b->dyed ? PAL(text, PAPER_TEXT) : PAPER_TEXT_SOFT;
    skin_txtc(&c, u->semi, px, 0, pad + 44, pad + TAB_H / 2, 0, hex(ink, 1), fit);
}

typedef struct {
    uint32_t key;
    Sprs sel, un;
} TabCache;
static TabCache g_tab[NTAB];

static uint32_t tab_key(const UiHubSection *s, int i) { return fnv_int(fnv_str(2166136261u, s->label), i); }

static void tab_build(const UiHubSection *s, int i)
{
    TabCache *e = &g_tab[i];
    uint32_t key = tab_key(s, i);
    if (e->key == key && e->sel.body && e->un.body) return;
    sprs_free(&e->sel);
    sprs_free(&e->un);
    e->key = key;
    int dyed = g_pal.tab_fill[i] != 0; // (Paper night's own tab colours; the light theme's are the tape pastels)
    for (int v = 0; v < 2; v++) {
        uint32_t rgb = !dyed ? paper_tape_rgb[TAB_TAPE[i]] : v ? g_pal.tab_strip[i] : g_pal.tab_fill[i];
        float w = v ? TAB_WSEL : TAB_W;
        TabBake tb = {card_get((int)w, (int)TAB_H, CARD_R, 30u + (uint32_t)i * 2 + (uint32_t)v, PAD_BODY), s->label, v, rgb, dyed, w};
        const PaperShadows *sh = shadows_for((int)w, (int)TAB_H, CARD_R, 0.0f);
        sprs_bake(v ? &e->sel : &e->un, w, TAB_H, PAD_BODY, PAD_REST, rad(v ? 0.2f : -0.6f), sh ? sh->s[0] : NULL, NULL, 0, draw_tab_body, &tb);
    }
}

// ---- a section: the sheet and the slips on it

typedef struct {
    const UiHubRow *r;
    const PaperSpr *card;
    int compact;
    float h;
    uint32_t rgb;
} RowBake;

static void draw_row_body(Surf *s, void *ud)
{
    const RowBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const float pad = PAD_BODY, cy = pad + b->h * 0.5f - (b->compact ? 0.0f : 2.0f);
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    float dr = b->compact ? 22.0f : 28.0f, dx = pad + (b->compact ? 28.0f : 36.0f) + dr;
    gfx_circle(s, dx, cy, dr, hex(b->rgb, 1));
    skin_admin_glyph(&c, dx, cy, dr * 2.0f, b->r->icon, hex(PAPER_TEXT, 1));
    float lx = pad + (b->compact ? 92.0f : 120.0f), right = pad + ROW_W - (b->compact ? 82.0f : 84.0f);
    float lpx = b->compact ? 24.0f : 28.0f, vpx = b->compact ? 20.0f : 22.0f, det_w = 0;
    if (b->r->detail && b->r->detail[0]) {
        char det[96];
        text_fit(u->med, vpx, 0, b->r->detail, 330, det, sizeof det);
        det_w = text_width(u->med, vpx, 0, det);
        skin_txtc(&c, u->med, vpx, 0, right, cy, 2, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), det);
    }
    char lab[120];
    text_fit(u->semi, lpx, 0, b->r->label, right - (det_w ? det_w + 24 : 0) - lx, lab, sizeof lab);
    skin_txtc(&c, u->semi, lpx, 0, lx, cy, 0, hex(PAL(text, PAPER_TEXT), 1), lab);
    skin_icon_chev(&c, pad + ROW_W - 58, cy, 24, 3.2f, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1));
}

typedef struct {
    int used, n;
    uint32_t key;
    Sprs row[MAXROW];
    TapeUse tp[MAXROW];
    Sprs sheet; // only its rest picture is kept
} SecCache;
static SecCache g_sec[NTAB];

static uint32_t sec_key(const UiHubSection *s, int i)
{
    uint32_t k = fnv_int(fnv_str(2166136261u, s->label), i);
    int n = nrows_of(s);
    k = fnv_int(k, n);
    for (int j = 0; j < n; j++) {
        k = fnv_str(k, s->rows[j].label);
        k = fnv_str(k, s->rows[j].detail);
        k = fnv_int(k, s->rows[j].icon);
    }
    return k;
}

static void sec_free(SecCache *sc)
{
    for (int i = 0; i < MAXROW; i++) sprs_free(&sc->row[i]);
    sprs_free(&sc->sheet);
    memset(sc, 0, sizeof *sc);
}

static void draw_sheet_body(Surf *s, void *ud) { paper_spr_put(s, ud, PAD_BODY, PAD_BODY); }

static int sec_ok(const UiHub *h, int i) { return g_sec[i].used && g_sec[i].key == sec_key(&h->sections[i], i); }

static void sec_ensure(const UiHub *h, int si)
{
    SecCache *sc = &g_sec[si];
    const UiHubSection *s = &h->sections[si];
    if (sec_ok(h, si)) return;
    sec_free(sc);
    sc->used = 1;
    sc->key = sec_key(s, si);
    int n = nrows_of(s);
    sc->n = n;
    Met m = met(n);
    uint32_t rgb = paper_tape_rgb[TAB_TAPE[si]];
    const PaperShadows *rsh = shadows_for((int)ROW_W, (int)m.rh, CARD_R, paper_lift_k[LIFT_ROW].scale);
    for (int i = 0; i < n; i++) {
        TapeUse *tp = &sc->tp[i];
        tp->t = (PaperTape){56, 22, rgb, TILE_KINDS[i % 4], PAPER_TAPE_ZIG, -20.0f, 300u + (uint32_t)(si * 8 + i)};
        float tx = 4.0f, ty = -8.0f; // (put on by hand: no two rows' strips lie alike)
        paper_tape_hand(&tp->t, &tx, &ty, (uint32_t)(si * 8 + i) * 7919u + 3u, 0.7f);
        paper_tape_offset(ROW_W, m.rh, tx, ty, (float)tp->t.len, (float)tp->t.wid, &tp->dx, &tp->dy);
        paper_tape_host(&tp->t, ROW_W, m.rh, CARD_R, tx, ty);
        RowBake rb = {&s->rows[i], card_get((int)ROW_W, (int)m.rh, CARD_R, 40u + (uint32_t)i, PAD_BODY), m.compact, m.rh, DISC[i % 3]};
        sprs_bake(&sc->row[i], ROW_W, m.rh, PAD_BODY, PAD_REST, rad(0.3f), rsh ? rsh->s[0] : NULL, tp, 1, draw_row_body, &rb);
    }
    if (n > 0) { // the sheet, its shadow and its two strips of tape
        const PaperSpr *card = card_get((int)SHEET_W, (int)m.sheet_h, CARD_R, 50u, PAD_BODY);
        PaperShadows *ssh = shadows_make((int)SHEET_W, (int)m.sheet_h, CARD_R, 0.0f, 2);
        TapeUse tt[2];
        tt[0].t = (PaperTape){96, 28, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, -34.0f, 401u};
        tt[1].t = (PaperTape){96, 28, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 34.0f, 402u};
        float sx[2] = {-20.0f, SHEET_W - 76.0f}, sy[2] = {-8.0f, -8.0f};
        for (int k = 0; k < 2; k++) {
            paper_tape_hand(&tt[k].t, &sx[k], &sy[k], (uint32_t)(si * 2 + k) * 7919u + 29u, 0.6f);
            paper_tape_offset(SHEET_W, m.sheet_h, sx[k], sy[k], (float)tt[k].t.len, (float)tt[k].t.wid, &tt[k].dx, &tt[k].dy);
            paper_tape_host(&tt[k].t, SHEET_W, m.sheet_h, CARD_R, sx[k], sy[k]);
        }
        sprs_bake(&sc->sheet, SHEET_W, m.sheet_h, PAD_BODY, PAD_SHEET, rad(0.3f), ssh ? ssh->s[0] : NULL, tt, 2, draw_sheet_body, (void *)card);
        paper_spr_free(sc->sheet.body); // (it is never lifted)
        sc->sheet.body = NULL;
        paper_shadows_free(ssh);
    }
}

// ---- the title: a small paper tag under the mark, as the mockup's `sub`

typedef struct {
    const PaperSpr *card;
    const char *title;
} TagBake;

static void draw_tag_body(Surf *s, void *ud)
{
    const TagBake *b = ud;
    Cx c = ui_cx(s);
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    skin_txtc(&c, c.skin->semi, 30, 0, PAD_BODY + 28, PAD_BODY + 23, 0, hex(PAL(text, PAPER_TEXT), 1), b->title);
}

// ------------------------------------------------------------------------------------------------ the part that never moves

#define TAG_X 52.0f
#define TAG_Y 104.0f
#define TAG_H 46.0f

static void draw_chrome(Surf *dst, const UiHub *h)
{
    Cx c = ui_cx(dst);
    Skin *u = c.skin;
    skin_board(&c);
    SkinStatus st;
    ui_status(&st);
    skin_top_bar(&c, NULL, &st);
    char title[64];
    text_fit(u->semi, 30, 0, h->title ? h->title : "", 360, title, sizeof title);
    float w = ceilf(text_width(u->semi, 30, 0, title)) + 56;
    w += (int)w & 1;
    Sprs tag;
    TagBake tb = {card_get((int)w, (int)TAG_H, CARD_R, 60u, PAD_BODY), title};
    const PaperShadows *sh = shadows_for((int)w, (int)TAG_H, CARD_R, 0.0f);
    sprs_bake(&tag, w, TAG_H, PAD_BODY, PAD_REST, rad(0.6f), sh ? sh->s[0] : NULL, NULL, 0, draw_tag_body, &tb);
    if (sprs_rest(&tag)) paper_spr_put(dst, sprs_rest(&tag), (int)TAG_X - tag.rox, (int)TAG_Y - tag.roy);
    sprs_free(&tag);
    if (h->summary && h->summary[0]) {
        float sx = TAG_X + w + 20;
        char fit[200];
        text_fit(u->med, 18, 0, h->summary, 1240 - sx, fit, sizeof fit);
        skin_txtc(&c, u->med, 18, 0, sx, TAG_Y + TAG_H / 2, 0, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), fit);
    }
    skin_footer(&c, HUB_PROMPTS, 2);
}

// the section's sheet and the line that says what it holds, on top of the chrome
static void draw_sheet(Surf *dst, const UiHub *h, int si)
{
    SecCache *sc = &g_sec[si];
    if (!sc->used || sc->n <= 0) return;
    const UiHubSection *s = &h->sections[si];
    Met m = met(sc->n);
    if (sc->sheet.rest) paper_spr_put(dst, sc->sheet.rest, (int)SHEET_X - sc->sheet.rox, (int)SHEET_Y - sc->sheet.roy);
    if (s->blurb && s->blurb[0]) {
        Cx c = ui_cx(dst);
        char fit[200];
        text_fit(c.skin->med, 17, 0, s->blurb, 1240 - (SHEET_X + 8), fit, sizeof fit);
        skin_txtc(&c, c.skin->med, 17, 0, SHEET_X + 8, SHEET_Y + m.sheet_h + 26, 0, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), fit);
    }
}

// ------------------------------------------------------------------------------------------------ items

enum { IK_TAB, IK_ROW };

typedef struct {
    int active, kind;
    int lk, ghost;
    float x, y, w, h;  // the card at rest
    float r, grow;
    float tilt;        // radians
    PaperLift lift;
    Sprs sp;
    const PaperShadows *sh;
    const PaperSpr *ghost_spr;
    TapeUse tp;
    int ntp;
    uint32_t key;      // what the sprites were made from
    float ver;         // changes when they are made again
    int force;         // draw it again next frame whatever its pose
    // this frame
    int moving;        // lifted, or still settling
    PaperPose ps;
    float lx, ly;      // a lean (a press toward nothing)
    float cx, cy, sc, ang;
    float pk[10], pk_prev[10];
    Rect bb, bb_prev;
    int drawn;
} HItem;

typedef struct {
    HItem it[NIT];
    int cur;           // the item that has the focus (lifted), or -1
    Bump lean;         // a press toward nothing
    int lean_item;
    int force_full;
} HubCtx;

static HubCtx g_hc;

static void item_set(HItem *it, const Sprs *sp, uint32_t key)
{
    if (it->sp.rest != sp->rest || it->sp.body != sp->body || it->key != key) {
        it->sp = *sp;
        it->key = key;
        it->force = 1;
        it->ver += 1.0f;
    }
}

static void item_off(HItem *it)
{
    if (it->active || it->sp.rest || it->sp.body) it->ver += 1.0f;
    it->active = 0;
    memset(&it->sp, 0, sizeof it->sp);
    it->key = 0;
    it->sh = NULL;
    it->ghost_spr = NULL;
    it->ntp = 0;
}

static void tab_setup(HubCtx *c, const UiHub *h, int t)
{
    HItem *it = &c->it[t];
    if (t >= nsec(h)) {
        item_off(it);
        return;
    }
    int sel = t == h->sel_section;
    tab_build(&h->sections[t], t);
    TabCache *e = &g_tab[t];
    it->active = 1;
    it->kind = IK_TAB;
    it->x = TAB_X;
    it->y = tab_y(t);
    it->w = sel ? TAB_WSEL : TAB_W;
    it->h = TAB_H;
    it->r = CARD_R;
    it->grow = 0;
    it->lk = LIFT_TAB;
    it->ghost = 0;
    it->ntp = 0;
    it->tilt = rad(sel ? 0.2f : -0.6f);
    item_set(it, sel ? &e->sel : &e->un, fnv_int(e->key, sel));
    it->sh = shadows_for((int)it->w, (int)it->h, it->r, 0.0f);
    it->ghost_spr = NULL;
}

static void row_setup(HubCtx *c, const UiHub *h, int r)
{
    HItem *it = &c->it[NTAB + r];
    SecCache *sc = &g_sec[h->sel_section];
    if (!sc->used || r >= sc->n) {
        item_off(it);
        return;
    }
    Met m = met(sc->n);
    it->active = 1;
    it->kind = IK_ROW;
    it->x = ROW_X;
    it->y = row_y(&m, r);
    it->w = ROW_W;
    it->h = m.rh;
    it->r = CARD_R;
    it->lk = LIFT_ROW;
    it->grow = paper_lift_k[LIFT_ROW].scale;
    it->ghost = 1;
    it->tilt = rad(0.3f);
    it->ntp = 1;
    it->tp = sc->tp[r];
    item_set(it, &sc->row[r], fnv_int(sc->key, r));
    it->sh = shadows_for((int)it->w, (int)it->h, it->r, it->grow);
    it->ghost_spr = ghost_get((int)it->w, (int)it->h, it->r + 1.0f);
}

// everything that depends on the section shown: call after the selection of a section changed
static void layout(HubCtx *c, const UiHub *h)
{
    g_tp_gen++;
    sec_ensure(h, h->sel_section);
    for (int t = 0; t < NTAB; t++) tab_setup(c, h, t);
    for (int r = 0; r < MAXROW; r++) row_setup(c, h, r);
}

// ------------------------------------------------------------------------------------------------ focus and lifting

static int focus_item(const UiHub *h) { return h->in_rows ? NTAB + h->sel_row : h->sel_section; }

static void lifts_reset(HubCtx *c, const UiHub *h)
{
    for (int i = 0; i < NIT; i++) {
        paper_lift_init(&c->it[i].lift, i < NTAB ? LIFT_TAB : LIFT_ROW);
        c->it[i].lift.tapeless = i < NTAB; // (the tabs have no tape: their own sound)
    }
    c->cur = focus_item(h);
    if (c->cur >= 0 && !c->it[c->cur].active) c->cur = -1;
    if (c->cur >= 0) { // up, and has finished its wiggle
        PaperLift *l = &c->it[c->cur].lift;
        l->target = 1;
        l->from = 1;
        l->dur = 0;
        l->wig_t0 = -1;
        l->wig_rest = c->cur < NTAB ? 0.0f : -0.4f;
    }
    memset(&c->lean, 0, sizeof c->lean);
}

// the focus moved (or not): the old one sticks back down, the new one peels up (tabs always the quick way, as in the mockup)
static void focus_sync(HubCtx *c, const UiHub *h, double now, int held)
{
    int want = focus_item(h);
    if (want >= 0 && !c->it[want].active) want = -1;
    if (want == c->cur) return;
    if (c->cur >= 0) paper_lift_release(&c->it[c->cur].lift, now, held || c->cur < NTAB);
    if (want >= 0) paper_lift_grab(&c->it[want].lift, now, held || want < NTAB);
    c->cur = want;
}

// ------------------------------------------------------------------------------------------------ the poses and what each covers

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

// the box of a w x h picture turned and scaled about the middle (cx, cy), as gfx_blit_xform covers it
static void box_xform(int b[4], float cx, float cy, float w, float h, float sc, float ang)
{
    float ca = fabsf(cosf(ang)), sa = fabsf(sinf(ang));
    float ex = (ca * w + sa * h) * 0.5f * sc + 2.0f, ey = (sa * w + ca * h) * 0.5f * sc + 2.0f;
    box_add(b, cx - ex, cy - ey, cx + ex, cy + ey);
}

static void shadow_pos(const HItem *it, const PaperSpr *sp, int *ax, int *ay)
{
    *ax = (int)lroundf(it->cx - it->w * 0.5f) - sp->ox;
    *ay = (int)lroundf(it->cy - it->h * 0.5f) - sp->oy;
}

static void item_bounds(HItem *it)
{
    int b[4] = {0, 0, 0, 0};
    if (!it->active) {
        memset(&it->bb, 0, sizeof it->bb);
        return;
    }
    const PaperSpr *rest = sprs_rest(&it->sp);
    if (!it->moving) {
        if (rest) box_add(b, (float)((int)lroundf(it->x) - it->sp.rox), (float)((int)lroundf(it->y) - it->sp.roy),
                          (float)((int)lroundf(it->x) - it->sp.rox + rest->w), (float)((int)lroundf(it->y) - it->sp.roy + rest->h));
    } else {
        if (it->sp.body) box_xform(b, it->cx, it->cy, (float)it->sp.body->w, (float)it->sp.body->h, it->sc, it->ang);
        if (it->sh) {
            const PaperSpr *sp = it->sh->s[paper_shadow_level(it->sh, it->ps.p)];
            int ax, ay;
            shadow_pos(it, sp, &ax, &ay);
            box_add(b, (float)ax, (float)ay, (float)(ax + sp->w), (float)(ay + sp->h));
        }
        if (it->ghost_spr) {
            float gx = it->x + it->w * 0.5f, gy = it->y + it->h * 0.5f;
            box_xform(b, gx, gy, (float)it->ghost_spr->w, (float)it->ghost_spr->h, 1.0f, it->tilt);
        }
        PaperXf cf = {it->cx, it->cy, it->sc, it->ang};
        for (int i = 0; i < it->ntp; i++) {
            TapeEnt *e = tape_ent(&it->tp.t);
            PaperXf t = paper_xf_attach(cf, it->tp.dx, it->tp.dy);
            int tw = e->peel ? e->peel->s[0]->w : e->stuck ? e->stuck->w : 0, th = e->peel ? e->peel->s[0]->h : e->stuck ? e->stuck->h : 0;
            if (tw) box_xform(b, t.x, t.y, (float)tw, (float)th, t.scale, t.angle);
        }
    }
    it->bb.x = b[0];
    it->bb.y = b[1];
    it->bb.w = b[2] - b[0];
    it->bb.h = b[3] - b[1];
}

static void item_pose(HItem *it, double now, int lean_on, int lx, int ly, int *anim)
{
    it->lx = it->ly = 0;
    memset(&it->ps, 0, sizeof it->ps);
    it->ps.scale = 1.0f;
    it->moving = 0;
    if (it->active) {
        paper_lift_pose(&it->lift, now, it->w, &it->ps);
        if (lean_on) {
            it->lx = (float)lx;
            it->ly = (float)ly;
        }
        int busy = paper_lift_busy(&it->lift, now) || lean_on;
        it->moving = it->ps.p > 0.002f || busy;
        if (busy) *anim = 1;
    }
    it->cx = it->x + it->w * 0.5f + it->ps.x + it->lx;
    it->cy = it->y + it->h * 0.5f + it->ps.y + it->ly;
    it->sc = it->ps.scale;
    it->ang = it->tilt + it->ps.angle;
}

// works out every item's pose for time `now` (ms); returns 1 while anything is still moving
static int frame_poses(HubCtx *c, double now)
{
    float lox, loy;
    int lean_on = bump_offset(&c->lean, &lox, &loy);
    int lx = (int)lroundf(lox), ly = (int)lroundf(loy), moving = lean_on;
    for (int i = 0; i < NIT; i++) item_pose(&c->it[i], now, lean_on && i == c->lean_item, lx, ly, &moving);
    for (int i = 0; i < NIT; i++) item_bounds(&c->it[i]);
    return moving;
}

// ------------------------------------------------------------------------------------------------ painting

static int rect_hit(const Rect *a, const Rect *b)
{
    return a->w > 0 && b->w > 0 && a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static void put(Surf *s, const PaperSpr *p, int x, int y, int ox, int oy) { gfx_blit_over(s, p->px, p->w, p->h, x - ox, y - oy); }

static void draw_resting(Surf *s, int ox, int oy, const HItem *it)
{
    const PaperSpr *p = sprs_rest(&it->sp);
    if (p) put(s, p, (int)lroundf(it->x) - it->sp.rox, (int)lroundf(it->y) - it->sp.roy, ox, oy);
}

static void draw_ghost(Surf *s, int ox, int oy, const HItem *it)
{
    if (!it->ghost_spr || it->ps.ghost <= 0.004f) return;
    paper_spr_xform(s, it->ghost_spr, it->x + it->w * 0.5f - ox, it->y + it->h * 0.5f - oy, 1.0f, it->tilt, it->ps.ghost);
}

static void draw_lifted(Surf *s, int ox, int oy, const HItem *it)
{
    if (it->sh) {
        const PaperSpr *sp = it->sh->s[paper_shadow_level(it->sh, it->ps.p)];
        int ax, ay;
        shadow_pos(it, sp, &ax, &ay);
        put(s, sp, ax, ay, ox, oy);
    }
    if (it->sp.body) paper_spr_xform(s, it->sp.body, it->cx - ox, it->cy - oy, it->sc, it->ang, 1.0f);
    PaperXf cf = {it->cx, it->cy, it->sc, it->ang};
    for (int i = 0; i < it->ntp; i++) {
        TapeEnt *e = tape_ent(&it->tp.t);
        const PaperSpr *sp = e->peel ? paper_peel_at(e->peel, it->ps.p) : e->stuck;
        if (!sp) continue;
        PaperXf t = paper_xf_attach(cf, it->tp.dx, it->tp.dy);
        paper_spr_xform(s, sp, t.x - ox, t.y - oy, t.scale, t.angle, 1.0f);
    }
}

// the items that touch R, in order, drawn on `s` whose top left is (R.x, R.y) on the screen
static void paint_items(HubCtx *c, Surf *s, const Rect *R)
{
    int ox = R->x, oy = R->y;
    for (int i = 0; i < NIT; i++) { // 1. everything lying still
        HItem *it = &c->it[i];
        if (it->active && !it->moving && rect_hit(&it->bb, R)) draw_resting(s, ox, oy, it);
    }
    for (int i = 0; i < NIT; i++) { // 2. the patches left where slips were lifted
        HItem *it = &c->it[i];
        if (it->active && it->moving && it->ghost && rect_hit(&it->bb, R)) draw_ghost(s, ox, oy, it);
    }
    int order[NIT], n = 0; // 3. what is lifted or settling, the lowest first
    for (int i = 0; i < NIT; i++)
        if (c->it[i].active && c->it[i].moving) order[n++] = i;
    for (int a = 1; a < n; a++) {
        int v = order[a], b = a;
        while (b > 0 && c->it[order[b - 1]].ps.p > c->it[v].ps.p) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = v;
    }
    for (int k = 0; k < n; k++) {
        HItem *it = &c->it[order[k]];
        if (rect_hit(&it->bb, R)) draw_lifted(s, ox, oy, it);
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

// the rectangle R of `dst` made anew: the base with every item that touches it drawn over
static void paint_rect(HubCtx *c, Surf *dst, const Rect *R)
{
    Surf *scr = scratch_for(R->w, R->h);
    if (!scr->px) return;
    for (int y = 0; y < R->h; y++)
        memcpy(scr->px + (size_t)y * R->w * 4, G.base->px + ((size_t)(R->y + y) * SCREEN_W + R->x) * 4, (size_t)R->w * 4);
    paint_items(c, scr, R);
    for (int y = 0; y < R->h; y++)
        memcpy(dst->px + ((size_t)(R->y + y) * SCREEN_W + R->x) * 4, scr->px + (size_t)y * R->w * 4, (size_t)R->w * 4);
}

static void pose_key(const HItem *it, float *k)
{
    k[0] = (float)it->active;
    k[1] = (float)it->moving;
    k[2] = it->ps.p;
    k[3] = it->cx;
    k[4] = it->cy;
    k[5] = it->sc;
    k[6] = it->ang;
    k[7] = it->ps.ghost;
    k[8] = it->ver;
    k[9] = it->moving ? 1.0f : 0.0f;
}

static void collect_dirty(HubCtx *c, Dirty *d)
{
    for (int i = 0; i < NIT; i++) {
        HItem *it = &c->it[i];
        pose_key(it, it->pk);
        int changed = it->force || !!it->active != !!it->drawn || (it->active && memcmp(it->pk, it->pk_prev, sizeof it->pk));
        if (!changed) continue;
        if (it->drawn) dirty_add_rect(d, it->bb_prev);
        if (it->active) dirty_add_rect(d, it->bb);
    }
}

static void commit_frame(HubCtx *c)
{
    for (int i = 0; i < NIT; i++) {
        HItem *it = &c->it[i];
        memcpy(it->pk_prev, it->pk, sizeof it->pk);
        it->bb_prev = it->bb;
        it->drawn = it->active;
        it->force = 0;
    }
}

// the whole screen made from scratch and compared with what the rectangles made (tests)
static void oracle_check(HubCtx *c)
{
    static Surf *full;
    if (!full) full = surf_new(SCREEN_W, SCREEN_H);
    Rect all = {0, 0, SCREEN_W, SCREEN_H};
    paint_rect(c, full, &all);
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

// Makes this frame: poses, what changed, the changed rectangles painted into G.work and shown. Returns 1 while anything moves.
static int hub_frame(HubCtx *c, double now)
{
    int moving = frame_poses(c, now);
    Dirty d;
    dirty_clear(&d);
    if (c->force_full) dirty_all(&d);
    else collect_dirty(c, &d);
    if (d.full || d.n) {
        Rect full = {0, 0, SCREEN_W, SCREEN_H};
        if (d.full) {
            paint_rect(c, G.work, &full);
            ui_present_rects(G.work, &full, 1);
        } else {
            for (int i = 0; i < d.n; i++) paint_rect(c, G.work, &d.r[i]);
            ui_present_rects(G.work, d.r, d.n);
        }
        if (g_home_oracle) oracle_check(c);
    }
    commit_frame(c);
    c->force_full = 0;
    return moving;
}

// ------------------------------------------------------------------------------------------------ the base

// G.base = the chrome (kept in `chrome` when there was memory for it) with the shown section's sheet on it
static void render_base(HubCtx *c, const UiHub *h, Surf *chrome, int redo_chrome)
{
    if (chrome) {
        if (redo_chrome) draw_chrome(chrome, h);
        surf_copy(G.base, chrome);
    } else {
        draw_chrome(G.base, h);
    }
    draw_sheet(G.base, h, h->sel_section);
    c->force_full = 1;
}

// ---- idle-time baking: the tape's peeling states, and the sections not shown yet

static int bake_work(HubCtx *c, const UiHub *h, int do_one)
{
    // 1. the peeling states of the slips on show, the focused one first
    int order[MAXROW], n = 0;
    if (h->in_rows && c->it[NTAB + h->sel_row].active) order[n++] = h->sel_row;
    for (int r = 0; r < MAXROW; r++)
        if (c->it[NTAB + r].active && !(h->in_rows && r == h->sel_row)) order[n++] = r;
    for (int k = 0; k < n; k++) {
        TapeEnt *e = tape_ent(&c->it[NTAB + order[k]].tp.t);
        if (!e->peel && !e->tried) {
            if (do_one) {
                e->tried = 1;
                e->peel = paper_tape_peel_lazy(&e->t, PAPER_PEEL_STATES);
                for (int i = 0; i < NIT; i++) // whatever wears this tape draws again with its peeling states
                    if (c->it[i].ntp && tape_eq(&c->it[i].tp.t, &e->t)) c->it[i].force = 1;
            }
            return 1;
        }
    }
    // 2. a section that has not been made yet, the nearest first
    for (int d = 1; d < nsec(h); d++)
        for (int sgn = 1; sgn >= -1; sgn -= 2) {
            int si = h->sel_section + sgn * d;
            if (si < 0 || si >= nsec(h) || sec_ok(h, si)) continue;
            if (do_one) sec_ensure(h, si);
            return 1;
        }
    // 3. the peeling states of the other sections' slips
    for (int si = 0; si < nsec(h); si++) {
        if (si == h->sel_section || !g_sec[si].used) continue;
        for (int r = 0; r < g_sec[si].n; r++) {
            TapeEnt *e = tape_ent(&g_sec[si].tp[r].t);
            if (!e->peel && !e->tried) {
                if (do_one) {
                    e->tried = 1;
                    e->peel = paper_tape_peel_lazy(&e->t, PAPER_PEEL_STATES);
                }
                return 1;
            }
        }
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------ input

static void clamp_sel(UiHub *h)
{
    int ns = nsec(h);
    if (h->sel_section >= ns) h->sel_section = ns - 1;
    if (h->sel_section < 0) h->sel_section = 0;
    int n = nrows_of(&h->sections[h->sel_section]);
    if (h->sel_row >= n) h->sel_row = n - 1;
    if (h->sel_row < 0) h->sel_row = 0;
    if (n <= 0) h->in_rows = 0;
}

// the screen as it should be shown now (nothing in flight), whole
static void hub_settle(HubCtx *c, const UiHub *h)
{
    layout(c, h);
    lifts_reset(c, h);
    c->force_full = 1;
    hub_frame(c, (double)ui_now());
}

static int tab_hit(float x, float y)
{
    for (int i = 0; i < NTAB; i++)
        if (in_rect(x, y, TAB_X, tab_y(i) - 8, TAB_WSEL + 12, TAB_DY)) return i;
    return -1;
}

// The palette changed since the tabs, the sections and their tape were made (the theme did): they all go, to be made again in the new colours (the
// items must let go of them too). 1 if it did.
static int hub_pal_check(void)
{
    static unsigned serial = 1; // (the palette the app starts with: nothing made yet to throw away)
    if (!paper_pal_stale(&serial)) return 0;
    for (int i = 0; i < NTAB; i++) {
        sprs_free(&g_tab[i].sel);
        sprs_free(&g_tab[i].un);
        g_tab[i].key = 0;
        sec_free(&g_sec[i]);
    }
    for (int i = 0; i < TAPE_CACHE; i++) {
        if (g_tp[i].used) {
            paper_spr_free(g_tp[i].stuck);
            paper_peel_free(g_tp[i].peel);
        }
        memset(&g_tp[i], 0, sizeof g_tp[i]);
    }
    return 1;
}

int ui_hub(UiHub *h)
{
    if (G.quit || h->nsections <= 0) return 0;
    clamp_sel(h);
    HubCtx *c = &g_hc;
    memset(c, 0, sizeof *c); // (the pictures belong to the caches; nothing is carried over from the last visit)
    hub_pal_check();
    Surf *chrome = surf_new(SCREEN_W, SCREEN_H);
    layout(c, h);
    render_base(c, h, chrome, 1);
    lifts_reset(c, h);
    ui_transition();

    SkinStatus st0;
    ui_status(&st0);
    int result = -1;
    while (result < 0) {
        double now = (double)ui_now();
        int moving = hub_frame(c, now);
        int pending = !moving && bake_work(c, h, 0);
        uint64_t b = ui_poll(moving || pending ? 0 : 1000);
        if (b == UI_QUIT) {
            result = 0;
            break;
        }
        int held = G.plat->repeated != 0;
        if (b & UI_REDRAW) { // the theme changed under it (with a new palette, the tabs and the slips are made again in its colours)
            if (hub_pal_check()) {
                memset(c, 0, sizeof *c);
                layout(c, h);
                lifts_reset(c, h);
            }
            render_base(c, h, chrome, 1);
            b &= ~UI_REDRAW;
        }
        int sec = h->sel_section, chosen = 0, pdx = 0, pdy = 0, touched = 0; // (pdx, pdy: a press toward nothing)
        const UiHubSection *s = &h->sections[sec];
        int nrows = nrows_of(s);
        if (b & UI_TOUCH) { // tap a section to see what it holds; tap a row to open it
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            Met m = met(nrows);
            while (ui_touch_next(&te)) {
                if (te.kind != TOUCH_TAP) continue;
                touched = 1;
                int ti = tab_hit(te.x, te.y);
                if (ti >= 0 && ti < nsec(h)) {
                    sec = ti;
                    h->in_rows = 0;
                }
                for (int i = 0; i < nrows && sec == h->sel_section; i++)
                    if (in_rect(te.x, te.y, ROW_X, row_y(&m, i) - 6, ROW_W, m.rh + 12)) {
                        h->sel_row = i;
                        h->in_rows = 1;
                        chosen = 1;
                    }
            }
        }
        if (!b && !touched) { // the clock or battery may have changed; or a quiet moment: bake something
            if (!moving) {
                SkinStatus st;
                ui_status(&st);
                if (strcmp(st.clock, st0.clock) || st.battery != st0.battery || st.charging != st0.charging || strcmp(st.account, st0.account)) {
                    st0 = st;
                    render_base(c, h, chrome, 1);
                } else if (pending) {
                    bake_work(c, h, 1);
                }
            }
            continue;
        }
        int ns = nsec(h);
        if (!h->in_rows) {
            if (b & BTN_DOWN) { if (sec < ns - 1) sec++; else if (ns > 1 && wrap_ok()) sec = 0; else pdy = 1; }
            else if (b & BTN_UP) { if (sec > 0) sec--; else if (ns > 1 && wrap_ok()) sec = ns - 1; else pdy = -1; }
            else if (b & BTN_LEFT) pdx = -1;
            else if (b & (BTN_A | BTN_RIGHT)) {
                if (nrows > 0 && !chosen && sec == h->sel_section) h->in_rows = 1;
                else if ((b & BTN_RIGHT) && nrows <= 0) pdx = 1;
            }
            else if (b & BTN_B) result = 0;
        } else if (!chosen) {
            if (b & BTN_DOWN) { if (h->sel_row < nrows - 1) h->sel_row++; else if (nrows > 1 && wrap_ok()) h->sel_row = 0; else pdy = 1; }
            else if (b & BTN_UP) { if (h->sel_row > 0) h->sel_row--; else if (nrows > 1 && wrap_ok()) h->sel_row = nrows - 1; else pdy = -1; }
            else if (b & BTN_RIGHT) pdx = 1;
            else if (b & BTN_A) chosen = 1;
            else if (b & (BTN_B | BTN_LEFT)) h->in_rows = 0;
        }
        if (result == 0) break;
        if (sec != h->sel_section) { // another section: its sheet shows at once, starting from the first row
            c->lean.on = 0; // (a row that was leaning has gone)
            h->sel_section = sec;
            h->sel_row = 0;
            layout(c, h);
            for (int r = 0; r < MAXROW; r++) paper_lift_init(&c->it[NTAB + r].lift, LIFT_ROW);
            if (c->cur >= NTAB) c->cur = -1; // (the slip that was lifted is not there any more)
            render_base(c, h, chrome, 0);
        }
        if (pdx || pdy) {
            bump_start(&c->lean, pdx, pdy);
            c->lean_item = focus_item(h);
        }
        focus_sync(c, h, (double)ui_now(), held);
        if (chosen) { // leave the slip lifted and still: it is what a dialog dims
            int fi = focus_item(h);
            if (fi >= 0 && c->it[fi].active) {
                Rect nb[8]; // (the other slips and tabs stay as they are: only this one is pushed flat)
                int nn = 0;
                for (int i = 0; i < NIT && nn < 8; i++)
                    if (i != fi && c->it[i].active) nb[nn++] = c->it[i].bb;
                ui_press_keep(nb, nn);
                const HItem *f = &c->it[fi]; // (the slip lifted: a little bigger and higher than at rest; the tape of the one under it reaches over its bottom edge)
                ui_press_keep_off((Rect){(int)f->x - 4, (int)f->y - 8, (int)f->w + 8, (int)f->h + 12});
                if (f->moving) { // (and its own tape, which goes in with it, is not the neighbours' to keep still)
                    const TapeEnt *e = tape_ent(&f->tp.t);
                    const PaperXf cf = {f->cx, f->cy, f->sc, f->ang}, t = paper_xf_attach(cf, f->tp.dx, f->tp.dy);
                    const int tw = e->peel ? e->peel->s[0]->w : e->stuck ? e->stuck->w : 0, th = e->peel ? e->peel->s[0]->h : e->stuck ? e->stuck->h : 0;
                    int tb[4] = {0, 0, 0, 0};
                    if (tw) box_xform(tb, t.x, t.y, (float)tw, (float)th, t.scale, t.angle);
                    if (!box_empty(tb)) {
                        const Rect tr = {tb[0], tb[1], tb[2] - tb[0], tb[3] - tb[1]};
                        ui_press_free(&tr, 1);
                    }
                }
                if (f->ghost && f->ghost_spr) { // (its dashed patch stays put, and what the lifted card covers of it stays covered)
                    ui_press_ghost(f->ghost_spr, f->x + f->w * 0.5f, f->y + f->h * 0.5f, f->tilt, f->ps.ghost);
                    if (f->moving) {
                        const float ca = fabsf(cosf(f->ang)), sa = fabsf(sinf(f->ang));
                        const float ex = (ca * f->w + sa * f->h) * 0.5f * f->sc, ey = (sa * f->w + ca * f->h) * 0.5f * f->sc;
                        const int x0 = (int)ceilf(f->cx - ex), y0 = (int)ceilf(f->cy - ey), x1 = (int)floorf(f->cx + ex), y1 = (int)floorf(f->cy + ey);
                        ui_press_cover((Rect){x0, y0, x1 - x0, y1 - y0});
                    }
                }
                ui_press_whole(c->it[fi].bb);
                ui_press(G.work, c->it[fi].bb);
            }
            hub_settle(c, h);
            result = 1;
        }
    }
    if (result == 0 && !G.quit) hub_settle(c, h);
    ui_screen_done();
    surf_free(chrome);
    return result;
}
