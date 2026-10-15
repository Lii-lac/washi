// The home screen, in Washi: who is loaded, and a grid of everyone, as paper cards pinned to a cream board with washi tape.
//
// How a frame is made. The board, the corner bar and the footer strip are the `base` (G.base) and never move. Everything else is an
// item: a card (or the tag, the rail) with a baked rest sprite that already holds its resting shadow and its stuck tape, and, for the
// focused one, a lifted pose (ghost patch where it sat, shadow, the card turned and scaled, tape peeling). A frame works out each
// item's pose, adds what changed (where it was, where it is) to a list of dirty rectangles, and for each rectangle copies the base
// into a scratch picture, draws every item that touches it in order (resting ones first, then the lifted ones, highest last),
// copies the result into G.work and tells the platform which rectangles to show. A frame costs what moved, not the screen.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#include "gfx/paper.h"
#include "gfx/pins.h"
#include "ui_internal.h"
#include "ui_stickfx.h"
#include "job_thread.h"

int g_home_oracle;
long g_home_oracle_frames, g_home_oracle_bad, g_home_oracle_soft;
int g_ui_debug_timing = -1;
float g_ui_home_lift = -1.0f, g_ui_home_lift_low = 2.0f;
float g_ui_home_pin_h = -1.0f, g_ui_home_pin_x, g_ui_home_pin_y;

enum { COLS = 7, VROWS = 2, NSLOT = COLS * VROWS };
// Top to bottom (the mockup's numbers): the corner bar, the loaded profile's card, one row with the search bar and the order chip,
// then two rows of the grid with the A-Z rail on the right; the footer strip is under it all.
static const float TX0 = 40, TY0 = 312, TW = 150, TH = 164, TPX = 164, TPY = 170;
static const float HERO_X = 40, HERO_Y = 100, HERO_W = 1200, HERO_H = 118;
static const float ROW_Y = 236, ROW_H = 48;
static const float SGN_X = 984, SGN_Y = 126, SGN_W = 228, SGN_H = 52;
static const float RAIL_X = 1196, RAIL_Y = 300, RAIL_W = 44, RAIL_H = 340;
static const float CARD_R = 6;

// what the D-pad can be on besides the grid: up from its first row come the chip, the search bar, the loaded profile and its
// Sign out button (right of it, when someone is loaded)
enum { Z_GRID, Z_CHIP, Z_SEARCH, Z_HERO, Z_SIGNOUT, Z_COUNT };

// The footer shows only what the page doesn't show already (A selects; B does nothing here: Washi is closed from the console's HOME button; X is on the Sign out button, which is there whenever someone
// can sign out, Y on the search bar, R3 on the order chip). Plus edits the selected profile, a tap on the logo also opens About, and the admin page is a
// chord (ZL + L + D-pad up + Plus), reported by the platform as BTN_ADMIN.
static const Prompt PROMPTS[] = {{G_MINUS, "About"}, {"+", "Edit profile"}};
// while a search narrows the grid, minus clears it (About is then the logo's tap only)
static const Prompt PROMPTS_SEARCH[] = {{G_MINUS, "Clear search"}, {"+", "Edit profile"}};

static int letter_of(const char *name)
{
    char ch = name[0];
    if (ch >= 'a' && ch <= 'z') return ch - 'a';
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    return -1;
}

static uint32_t fnv(uint32_t h, const void *p, size_t n)
{
    const uint8_t *b = p;
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}

static uint32_t fnv_str(uint32_t h, const char *s)
{
    return fnv(h, s, strlen(s) + 1);
}

static uint32_t fnv_int(uint32_t h, int v)
{
    return fnv(h, &v, sizeof v);
}

// ------------------------------------------------------------------------------------------------ geometry

// Touch scrolling: the grid follows a finger up and down and, let go, runs on and comes to rest on a whole row (ui_motion.h; its place is in rows).
// The rows showing are top_row and the next, moved up g_row_off px (down, past the top end), and while that is not 0 the row after them coming in
// under them; at rest it is 0 and the mover is at top_row. Moving tiles are cut off where the grid's window ends (under the search bar, over the footer).
static Mover g_gm;
static Track g_gt;
static float g_row_off, g_graw;
static int g_gdrag, g_gstopped; // a finger is dragging the grid; a finger came down on the grid while it ran (that touch chooses nothing)
static int g_gstep; // grid_step is moving it (its last step lands it at rest: what comes up there still peels up, after)
static int g_gfollow = 1; // the selection is kept on the rows showing (a finger moves the grid); 0: a press moved the selection and the grid goes to it
static uint64_t g_glast;
static const Throw GRID_THROW = {0.30f, 1.3f, 1.0f, 0.30f, 0.07f, MO_PRESS_OM, MO_PRESS_ZETA};
#define GRID_ROOM 0.6f  // the most an end of the grid gives to a pull (rows)
#define GRID_GRAB 14.0f // how far a finger goes before it is a drag and not a tap (ui_core.c)

static void tile_xy(const UiHome *h, int idx, float *x, float *y)
{
    int row = idx / COLS - h->top_row, col = idx % COLS;
    *x = TX0 + col * TPX;
    *y = TY0 + row * TPY - g_row_off;
}

// the order chip: R3's mark and the label, a sticker, the chip as wide as the longest order's sticker, whichever is on it (an even number of pixels, so its baked picture lands on whole pixels)
static void sort_rect(const UiHome *h, float *x, float *y, float *w, float *hh)
{
    int iw = (int)ceilf(54 + text_width(G.skin.semi, 20, 0, h->sort_label) + 26);
    for (int i = 0; i < h->nsorts; i++) { // (the sticker has its badge before the name)
        int w2 = (int)ceilf(text_width(G.skin.semi, 20, 0, h->sort_names[i]) + 112);
        if (w2 > iw) iw = w2;
    }
    if (iw < 168) iw = 168;
    iw += iw & 1;
    *w = (float)iw;
    *hh = ROW_H;
    *y = ROW_Y;
    *x = 1240 - *w;
}

static void search_rect(const UiHome *h, float *x, float *y, float *w, float *hh)
{
    float cx, cy, cw, ch; // (the chip ends the row)
    sort_rect(h, &cx, &cy, &cw, &ch);
    *x = 40;
    *y = ROW_Y;
    *w = cx - 16 - 40;
    *hh = ROW_H;
}

// the button at the search bar's right end that clears a search (it is there only while one narrows the grid)
static void search_clear_rect(const UiHome *h, float *x, float *y, float *w, float *hh)
{
    float sx, sy, sw, sh;
    search_rect(h, &sx, &sy, &sw, &sh);
    *w = 44;
    *hh = sh;
    *x = sx + sw - 30 - 22;
    *y = sy;
}

static void signout_rect(float *x, float *y, float *w, float *h)
{
    *x = SGN_X;
    *y = SGN_Y;
    *w = SGN_W;
    *h = SGN_H;
}

// ------------------------------------------------------------------------------------------------ caches of baked things

// washi tape: the strip as it sits (stuck), and the set of its peeling states (baked later, when nothing else is happening)
typedef struct {
    int used;
    unsigned gen;
    PaperTape t;
    PaperSpr *stuck;
    PaperPeel *peel;
    int tried;
    double peel_at; // when the peeling states were made (ms): a tape already up then peels in from flat (TAPE_IN_MS) instead of jumping up
} TapeEnt;
#define TAPE_IN_MS 260.0

enum { TAPE_CACHE = 64 };
static TapeEnt g_tp[TAPE_CACHE];
static unsigned g_tp_gen = 1;

static int tape_eq(const PaperTape *a, const PaperTape *b)
{
    return a->len == b->len && a->wid == b->wid && a->rgb == b->rgb && a->kind == b->kind && a->flags == b->flags && a->tilt == b->tilt &&
           a->seed == b->seed &&
           a->host_w == b->host_w && a->host_h == b->host_h && a->host_r == b->host_r && a->host_x == b->host_x && a->host_y == b->host_y;
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
    enum { N = 12 };
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
    enum { N = 10 };
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
    enum { N = 8 };
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

// Shadow sets, one per kind of thing (its size can change: the chip's label, and with it the search bar's width). A wide card's set is
// made from a narrow card's: the middle column of each level is repeated, so the blur (the expensive part) is done on a small picture.
enum { SC_TILE, SC_HERO, SC_SEARCH, SC_CHIP, SC_BTN, SC_RAIL, SC_COUNT };
typedef struct {
    int w, h;
    float r, grow;
    PaperShadows *sh;
} ShCls;
static ShCls g_shc[SC_COUNT];

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

static const PaperShadows *shadows_for(int cls, int w, int h, float r, float grow)
{
    ShCls *k = &g_shc[cls];
    if (!k->sh || k->w != w || k->h != h || k->r != r || k->grow != grow) {
        paper_shadows_free(k->sh);
        k->sh = shadows_make(w, h, r, grow, cls == SC_RAIL ? 2 : PAPER_SHADOW_LEVELS);
        k->w = w;
        k->h = h;
        k->r = r;
        k->grow = grow;
    }
    return k->sh;
}

// ------------------------------------------------------------------------------------------------ items

typedef struct {
    PaperSpr *body; // the card (and what is printed on it), straight, `pad` px of room all round: what is lifted and turned
    PaperSpr *rest; // the card as it sits: shadow, turned a little, tape stuck on. Its card's top left is at (rox, roy). NULL: use body
    int pad, rox, roy;
} Sprs;

typedef struct {
    PaperTape t;
    float dx, dy; // where its middle is, from the card's middle
} TapeUse;

// A push pin instead of tape: an admin pinned the person. The pin is the colour of the tape this card would have had.
typedef struct {
    int on;        // pinned
    int slot;      // the tape's colour slot (the pin's colour)
    float dx, dy;  // where it goes in, from the card's middle
} PinUse;

typedef struct {
    int active, kind;
    int cls, lk;       // shadow set, lift kind (-1: never lifts)
    int ghost;         // leaves a patch where it sat when it lifts
    float x, y, w, h;  // the card at rest
    float r, grow;
    float tilt;        // radians (the one it rests at now: a pinned card lies straighter than a taped one)
    float tilt_pin, tilt_tape; // the two a tile can rest at
    PinUse pn;         // tiles: the pin this person's card has (pn.on) or would have
    uint32_t pkey;     // tiles: who (person_key), for the pin animation
    double pin_t0;     // the pin's own motion (M3): it rises out of the card as the card takes the focus and goes home as it leaves
    float pin_from;
    int pin_up;
    PaperLift lift;
    Sprs sp;
    int own;           // the sprites are this item's to free (the tiles' belong to the tile cache)
    const PaperShadows *sh;
    const PaperSpr *ghost_spr;
    TapeUse tp[2];
    int ntp;
    uint32_t key;      // what the sprites were made from
    float ver;         // changes when they are made again
    int force;         // draw it again next frame whatever its pose
    // this frame
    int moving;        // lifted, or still settling
    PaperPose ps;
    float lx, ly;      // a lean (a press toward nothing)
    float cx, cy, sc, ang; // where the card's middle is, how big, how turned
    float tape_k;      // how far its tapes' peeling has come in since their peeling states were made (1: all the way)
    const PaperSpr *tsp[2]; // each tape as it is drawn this frame (picked before the painting, which may run on several cores)
    // this frame: what holds the card (a pin, or a strip of tape put on or taken off by the pin animation)
    int hold;          // a pinned card, or the one the pin animation is on
    int pin_on, tape_on, tape_air;
    float pin_h, pin_a, dent_a; // the pin's height off the card (px), its opacity, how deep the dent shows
    float piv;         // 0: the card turns about its middle, 1: about its pin
    float tape_p;      // the held strip's peel
    float tf_up, tf_dx, tf_dy, tf_da, tf_sc, tape_a; // a strip in flight: how far from the card (0..1), its offset, turn, scale, opacity
    const PaperSpr *hold_tape;
    float pk[24], pk_prev[24];
    Rect bb, bb_prev;
    int drawn;
    // a shuffle (the grid ordered or narrowed anew): carried from where it was (dx, dy, da from its place), risen in, or sunk away
    double shf_t0;     // ms it starts (0: none)
    float shf_dur, shf_dx, shf_dy, shf_da;
    int shf_mode;      // SHF_MOVE, SHF_IN, SHF_OUT
    float alpha;       // this frame: how much of it shows (1 but in a shuffle)
} HItem;

enum { IT_TILE0 = 0, IT_HERO = NSLOT + COLS, IT_SEARCH, IT_CHIP, IT_SIGN, IT_RAIL, IT_LEAVE0, IT_COUNT = IT_LEAVE0 + NSLOT }; // (LEAVE: tiles sinking away in a shuffle)
enum { SHF_MOVE, SHF_IN, SHF_OUT };
#define SHF_MOVE_MS 330.0
#define SHF_IN_MS 300.0
#define SHF_OUT_MS 220.0
#define SHF_STEP_MS 16.0 // (one tile's start after the one before it)
#define SHF_DROP 46.0f   // px under its place a tile rises in from, or sinks to
enum { IK_TILE, IK_HERO, IK_SEARCH, IK_CHIP, IK_SIGN, IK_RAIL };

// a picture that sits over everything: the toast, the frame-time readout
typedef struct {
    PaperSpr *spr;
    int x, y, on;
    uint32_t key, key_prev; // changes whenever it is set
    Rect bb, bb_prev;
    int drawn;
} Ovl;

typedef struct {
    int idx;
    uint32_t key;
    uint32_t bkey; // what its card (the body, unturned and untaped) shows: the same for its person wherever the tile is
    Sprs sp;
} TileCache;

typedef struct {
    HItem it[IT_COUNT];
    int cur;            // the item that has the focus (lifted), or -1
    Bump lean;          // a press toward nothing
    int lean_item;
    int force_full;
    int clip; // the grid is scrolling: tiles are cut off at its window
    Ovl toast, dbg;
    char toast_text[96];
    int toast_kind;
    uint64_t dbg_draw, dbg_pres, dbg_draw_max, dbg_pres_max;
    int dbg_n, dbg_on;
} HomeCtx;

static HomeCtx g_hc;
static TileCache g_tc[2 * NSLOT + COLS]; // (the rows showing, the one coming in under them while it scrolls, and one more each way)
static int g_rest_later; // the grid is set up for a shuffle: the tiles' pictures at rest are made once it has settled (until then they are drawn as lifted, at rest)
static int g_az; // the first letter the rail shows
enum { RAIL_ROWS = 12, RAIL_ROW_H = 22, RAIL_LIST_Y = 38 };

static const float TILTS[11] = {-1.0f, .8f, -.6f, 1.1f, -.9f, .7f, -.5f, .9f, -1.1f, .5f, -.8f};
static const int TILE_KINDS[4] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG};

static float rad(float deg) { return deg * 3.14159265f / 180.0f; }

// how a tile lies: a pinned card settles straighter on its pin than one on tape
#define PIN_TILT 0.55f
static float tile_tilt(int idx, int pinned) { return rad(TILTS[idx % 11]) * (pinned ? PIN_TILT : 1.0f); }

// ---- baking
typedef struct {
    const PaperSpr *sh0, *body;
    float tilt, w, h;
    int rpad, ntp;
    const PaperSpr *tape[2];
    float dx[2], dy[2];
    PinUse pn;
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
    if (b->pn.on) { // (a pinned card: the pin in its dent, as it lies with nobody's hand on it)
        PaperXf a = paper_xf_attach(cf, b->pn.dx, b->pn.dy);
        pins_draw_dent(s, a.x, a.y, 1.0f, 1.0f);
        pins_draw_shadow(s, a.x, a.y, 0.0f, 1.0f, 1.0f);
        pins_draw(s, a.x, a.y, 0.0f, 1.0f, b->pn.slot, 1.0f);
    }
}

// Bakes the body from `draw`, then the rest sprite from it (shadow `sh0`, the turn, the tapes). need_rest 0: the body is its own rest.
// the picture of it lying at rest (turned, its shadow and tape) made from its body, o->body
static void sprs_bake_rest(Sprs *o, float w, float h, int rpad, float tilt, const PaperSpr *sh0, const TapeUse *tp, int ntp, const PinUse *pn)
{
    RestBake rb;
    memset(&rb, 0, sizeof rb);
    if (pn) rb.pn = *pn;
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

static void sprs_bake(Sprs *o, float w, float h, int pad, int rpad, float tilt, const PaperSpr *sh0, const TapeUse *tp, int ntp, const PinUse *pn,
                      PaperDraw draw, void *ud, int need_rest)
{
    memset(o, 0, sizeof *o);
    o->pad = pad;
    o->body = paper_bake((int)w + 2 * pad, (int)h + 2 * pad, draw, ud);
    if (!o->body) return;
    if (!need_rest) {
        o->rox = o->roy = pad;
        return;
    }
    sprs_bake_rest(o, w, h, rpad, tilt, sh0, tp, ntp, pn);
}

static void sprs_free(Sprs *o)
{
    if (o->rest != o->body) paper_spr_free(o->rest);
    paper_spr_free(o->body);
    memset(o, 0, sizeof *o);
}

static const PaperSpr *sprs_rest(const Sprs *o) { return o->rest ? o->rest : o->body; }

// ---- the tiles

enum { PAD_BODY = 14, PAD_REST = 22, PAD_HERO_REST = 40 };

// ---- the stamp: a person who has just been loaded is stamped LOADED on their own tile (the banner at the top is not stamped). Ink on a tile
// belongs to that tile (it is on its picture: it lifts and turns with it, and the tape lies over it); what of the stamp missed the tile lands
// on the board and stays under every screen until the app closes (nothing is saved).
typedef struct {
    char who[40];    // whose stamp it is ("" = nobody is loaded)
    int jx, jy;      // where it came down on the tile: how far from its usual place (px)
    float rot;       // how it is turned (radians): a little different with every stamp
    unsigned serial; // changes with every new stamp: what the cached pictures were made from
    int pending;     // it has not come down yet (it is being played): the pictures show none
    int landed;      // the part of it that missed the tile has been put on the board
    PaperSpr *small, *tile_ink; // the impression, and its part on the tile
} Stamp;
static Stamp g_stamp;
static int g_home_shown; // the home screen has been up before in this run (the first one shows the stamp already there)

enum { STAMP_W = 112, STAMP_H = 28 }; // the stamp on a tile (the tile is 150 x 164): about three quarters of its width

static void stamp_sprites(void)
{
    if (g_stamp.small) return;
    Cx c = ui_cx(G.base);
    g_stamp.small = skin_stamp(&c, STAMP_W, STAMP_H, 17.3f, "LOADED", 9u + g_stamp.serial % 6u, PAL(ink, PAPER_INK)); // (the wear of the rubber differs from stamp to stamp too)
}

// 0, or a number for this stamp, when `who` is the person it is on and it has come down
static int stamp_state(const char *who)
{
    return !g_stamp.pending && g_stamp.who[0] && who && !strcmp(g_stamp.who, who) ? (int)(g_stamp.serial * 2u + 1u) : 0;
}

// where the stamp's middle is on a tile, from the tile's own middle (it lies across the foot of the card, over the line where "3 games" goes)
static void stamp_on_tile(float *dx, float *dy)
{
    *dx = (float)g_stamp.jx;
    *dy = 144.0f + (float)g_stamp.jy - TH / 2;
}

static void tile_ink_draw(Surf *s, void *ud)
{
    (void)ud;
    float dx, dy;
    stamp_on_tile(&dx, &dy);
    if (g_stamp.small) paper_spr_xform(s, g_stamp.small, PAD_BODY + TW / 2 + dx, PAD_BODY + TH / 2 + dy, 1.0f, g_stamp.rot, 1.0f);
}

// the stamp as it is on a tile (in the tile's own picture, clipped to the card)
static const PaperSpr *stamp_tile_ink(void)
{
    if (!g_stamp.tile_ink && g_stamp.small) {
        PaperSpr *l = paper_bake((int)TW + 2 * PAD_BODY, (int)TH + 2 * PAD_BODY, tile_ink_draw, NULL);
        if (l)
            for (int y = 0; y < l->h; y++)
                for (int x = 0; x < l->w; x++)
                    if (x < PAD_BODY || x >= PAD_BODY + (int)TW || y < PAD_BODY || y >= PAD_BODY + (int)TH) l->px[((size_t)y * l->w + x) * 4 + 3] = 0;
        g_stamp.tile_ink = l;
    }
    return g_stamp.tile_ink;
}

typedef struct {
    float cx, cy, ang;
} BoardInk;

static void board_ink_draw(Surf *s, void *ud)
{
    const BoardInk *b = ud;
    if (g_stamp.small) paper_spr_xform(s, g_stamp.small, b->cx, b->cy, 1.0f, b->ang, 1.0f);
}

// where the stamp's middle is on the screen when the tile is at its place in the grid
static void stamp_screen_pos(const UiHome *h, int idx, float *cx, float *cy, float *ang)
{
    float tx, ty, dx, dy;
    tile_xy(h, idx, &tx, &ty);
    stamp_on_tile(&dx, &dy);
    float a = tile_tilt(idx, idx < h->n && h->people[idx].pinned);
    *cx = tx + TW / 2 + dx * cosf(a) - dy * sinf(a);
    *cy = ty + TH / 2 + dx * sinf(a) + dy * cosf(a);
    *ang = a + g_stamp.rot;
}

// the part of the stamp that missed its tile goes on the board, once
static void stamp_board_ink(const UiHome *h)
{
    if (g_stamp.landed || h->loaded < 0 || h->loaded >= h->n || !g_stamp.who[0]) return;
    g_stamp.landed = 1;
    stamp_sprites();
    float cx, cy, ang, tx, ty;
    stamp_screen_pos(h, h->loaded, &cx, &cy, &ang);
    tile_xy(h, h->loaded, &tx, &ty);
    enum { BW = 190, BH = 110 };
    int ox = (int)lroundf(cx) - BW / 2, oy = (int)lroundf(cy) - BH / 2;
    BoardInk bi = {(float)(cx - ox), (float)(cy - oy), ang};
    PaperSpr *b = paper_bake(BW, BH, board_ink_draw, &bi);
    if (!b) return;
    int any = 0;
    for (int y = 0; y < BH; y++)
        for (int x = 0; x < BW; x++) {
            uint8_t *a = &b->px[((size_t)y * BW + x) * 4 + 3];
            float sx = (float)(ox + x), sy = (float)(oy + y);
            if (sx >= tx - 2 && sx < tx + TW + 2 && sy >= ty - 2 && sy < ty + TH + 2) *a = 0; // (what is on the tile stays on it)
            else if (*a) any = 1;
        }
    if (any) skin_ink_add(b, ox, oy);
    else paper_spr_free(b);
}

// the loaded person changed (or nobody is loaded now): a new stamp, if it has to come down. 1 = it is to be played (the home screen
// was up before in this run); else it is simply there.
static int stamp_update(const UiHome *h)
{
    const char *who = h->card_on ? h->card.name : "";
    if (!who[0]) { // signed out: the stamp goes with the paper it was on (the ink on the board stays)
        g_stamp.who[0] = 0;
        paper_spr_free(g_stamp.tile_ink);
        g_stamp.tile_ink = NULL;
        g_stamp.pending = 0;
        return 0;
    }
    if (!strcmp(g_stamp.who, who)) return 0;
    snprintf(g_stamp.who, sizeof g_stamp.who, "%s", who);
    uint32_t r = fnv_str(2166136261u, who) ^ (uint32_t)ui_now() * 2654435761u;
    r ^= r >> 15;
    r *= 2246822519u;
    r ^= r >> 13;
    // where it lands is anybody's guess: a hand puts it down off to either side (some land a little over the tile's edge), higher or lower, and
    // turned a few degrees either way
    g_stamp.jx = (int)(r % 47u) - 23;
    g_stamp.jy = (int)((r >> 8) % 21u) - 12;
    g_stamp.rot = rad(-4.0f + ((float)((r >> 16) % 201u) - 100.0f) * 0.055f); // -9.5 .. +1.5 degrees
    g_stamp.serial++;
    g_stamp.landed = 0;
    paper_spr_free(g_stamp.tile_ink);
    g_stamp.tile_ink = NULL;
    paper_spr_free(g_stamp.small);
    g_stamp.small = NULL;
    stamp_sprites();
    g_stamp.pending = g_home_shown && g_stamp.small;
    return g_stamp.pending;
}

typedef struct {
    const UiPerson *p;
    int loaded, isnew, idx, stamped;
    const PaperSpr *card;
} TileBake;

static void draw_tile_body(Surf *s, void *ud)
{
    const TileBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Theme *t = &u->th;
    const float pad = PAD_BODY, cx = pad + TW / 2;
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    if (b->isnew) {
        gfx_circle(s, cx, pad + 74, 33, hex(PAL(new_disc, 0xFBE8F6), 1));
        skin_icon_plus(&c, cx, pad + 74, 23, 3.2f, hex(PAL(new_ink, PAPER_TEXT_SOFT), 1));
        skin_txtc(&c, u->semi, 22, 0, cx, pad + 133, 1, t->text2, "New profile");
        return;
    }
    const UiPerson *p = b->p;
#ifdef WASHI_PAL_LAB
    if (g_pal.card_index) { // (mockups: a playing card: its border line, and the person's initial and a suit in the corner)
        gfx_rrect_stroke(s, pad + 7, pad + 7, TW - 14, TH - 14, CARD_R - 2, 1.2f, with_a(t->text2, 0.45f));
        char ini[2] = {(char)(p->name[0] >= 'a' && p->name[0] <= 'z' ? p->name[0] - 32 : p->name[0]), 0};
        int suit = (int)(fnv_str(2166136261u, p->name) % 4u);
        Col sc = suit == 1 || suit == 2 ? hex(0xE0564C, 1) : t->text;
        skin_txtc(&c, u->semi, 17, 0, pad + 21, pad + 23, 1, sc, ini);
        pal_suit(s, pad + 21, pad + 42, 13, suit, sc);
    }
#endif
    skin_avatar(&c, cx, pad + 70, 33, p->name, p->color);
    const PaperSpr *ring = ring_get(34.0f, 3u + fnv_str(2166136261u, p->name) % 5u); // (its person's, wherever the tile is: the card can go with them)
    if (ring) paper_spr_put(s, ring, (int)cx, (int)pad + 70);
    char nm[64], fit[64];
    text_fit(u->semi, 24, 0, p->name, TW - 24, nm, sizeof nm);
    skin_txtc(&c, u->semi, 24, 0, cx, pad + 121, 1, t->text, nm);
    if (b->loaded) {
        const PaperSpr *ink = b->stamped ? stamp_tile_ink() : NULL; // (the stamp stands where "3 games" would be)
        if (ink) gfx_blit_over(s, ink->px, ink->w, ink->h, 0, 0);
    } else {
        char one[48]; // ("3 games  ·  2d ago": the mockup's gaps are single)
        size_t k = 0;
        for (const char *q = p->detail; *q && k + 1 < sizeof one; q++)
            if (*q != ' ' || k == 0 || one[k - 1] != ' ') one[k++] = *q;
        one[k] = 0;
        text_fit(u->med, 17, 0, one, TW - 6, fit, sizeof fit);
        skin_txtc(&c, u->med, 17, 0, cx, pad + 145, 1, t->text2, fit);
    }
    if (p->locked) skin_icon_lock(&c, pad + TW - 22, pad + 24, 16, t->text3);
}

static uint32_t tile_key(int idx, const UiPerson *p, int loaded)
{
    uint32_t k = 2166136261u;
    k = fnv_int(k, idx);
    k = fnv_int(k, loaded);
    if (loaded && p) k = fnv_int(k, stamp_state(p->name));
    if (!p) return fnv_int(k, -1);
    k = fnv_str(k, p->name);
    k = fnv_str(k, p->detail);
    k = fnv_int(k, p->locked);
    k = fnv_int(k, p->pinned != 0); // (a pinned card is held by a pin: its picture at rest differs)
    return fnv_int(k, p->color);
}

// the pin of a tile: its colour is the tape's, it goes in near the top, a little off the middle (a hand never puts it dead centre)
static void tile_pin(const UiPerson *p, PinUse *o)
{
    uint32_t hsh = p ? fnv_str(2166136261u, p->name) : 7u;
    o->on = p && p->pinned != 0;
    if (o->on) pins_ready(); // (the pictures are made ready here, on the app's own thread, before a card is baked or painted with its pin: the bands are painted on several cores)
    o->slot = (int)(hsh % PAPER_TAPE_COUNT);
    o->dx = ((float)(hsh % 9u) - 4.0f) * 1.1f;
    o->dy = -TH / 2 + 21.0f; // (the flange 3 px in from the top edge)
}

// the tape on a tile: its colour from the name, its pattern from the place (as in the mockup), crooked one way or the other by turns
static void tile_tape(int idx, const UiPerson *p, TapeUse *o)
{
    int par = idx & 1;
    uint32_t hsh = p ? fnv_str(2166136261u, p->name) : 7u;
    o->t.len = 76;
    o->t.wid = 24;
    o->t.rgb = p ? paper_tape_rgb[hsh % PAPER_TAPE_COUNT] : paper_tape_rgb[7];
    o->t.kind = p ? TILE_KINDS[idx % 4] : PAT_ZIG;
    o->t.flags = PAPER_TAPE_ZIG;
    // across the middle of the top edge, each one a little different: a bit to one side or the other, higher or lower, more or less crooked
    o->t.tilt = (par ? 4.0f : -4.0f) + ((float)((hsh >> 16) % 9u) - 4.0f) * 0.9f;
    o->t.seed = 100u + (uint32_t)(hsh & 0xFFF);
    float tx = 37.0f + (par ? 6.0f : -6.0f) + (float)((hsh >> 8) % 21u) - 10.0f, ty = -4.0f - (float)((hsh >> 24) % 4u);
    paper_tape_offset(TW, TH, tx, ty, 76.0f, 24.0f, &o->dx, &o->dy);
    paper_tape_host(&o->t, TW, TH, CARD_R, tx, ty);
}

// what a tile's card shows (not where it is: its turn and tape are its place's)
static uint32_t tile_body_key(const UiPerson *p, int loaded, int stamped, int isnew)
{
    uint32_t k = fnv_int(fnv_int(fnv_int(2166136261u, isnew), loaded), stamped);
    if (!p) return k;
    k = fnv_str(k, p->name);
    k = fnv_str(k, p->detail);
    k = fnv_int(k, p->locked);
    return fnv_int(k, p->color);
}

// the entry no longer shows what its place holds now (it would be made again before use)
static int tile_stale(const TileCache *f, const UiHome *h)
{
    if (!f->sp.body || f->idx < 0 || f->idx > h->n) return 1;
    const UiPerson *q = f->idx < h->n ? &h->people[f->idx] : NULL;
    return f->key != tile_key(f->idx, q, f->idx == h->loaded);
}

static void rest_drop(Sprs *o)
{
    if (o->rest && o->rest != o->body) paper_spr_free(o->rest);
    o->rest = NULL;
}

// The grid ordered anew puts people at new places: each one's card is taken from where it was made (an entry whose place now holds someone
// else) and only turned and taped anew for its new place; the entry it came from gets this one's old card in exchange, for whoever needs that.
// in_layout: the whole grid is being set up now, so a card can be taken from a place on the screen too (its tile is set up again in the same pass);
// otherwise (a row made ahead of time) only from places off the screen, whose pictures nothing is drawing
static void tile_build(TileCache *e, const UiHome *h, int idx, const UiPerson *p, int loaded, int isnew, int in_layout)
{
    const int vis0 = h->top_row * COLS, vis1 = vis0 + NSLOT + (g_row_off > 0 ? COLS : 0);
    const int stamped = loaded && p && stamp_state(p->name) != 0;
    const uint32_t bk = tile_body_key(p, loaded, stamped, isnew);
    TileCache *f = NULL;
    if (!(e->sp.body && e->bkey == bk))
        for (int i = 0; i < (int)(sizeof g_tc / sizeof g_tc[0]); i++)
            if (&g_tc[i] != e && g_tc[i].sp.body && g_tc[i].bkey == bk && tile_stale(&g_tc[i], h) &&
                (in_layout || g_tc[i].idx < vis0 || g_tc[i].idx >= vis1)) {
                f = &g_tc[i];
                break;
            }
    rest_drop(&e->sp);
    if (f) { // (swapped: f's card here, this one's old card there, to be turned for wherever it is wanted)
        rest_drop(&f->sp);
        PaperSpr *mine = e->sp.body;
        uint32_t mk = e->bkey;
        e->sp.body = f->sp.body;
        e->bkey = bk;
        f->sp.body = mine;
        f->bkey = mk;
        f->key = 0;
        f->idx = -1;
    } else if (e->sp.body && e->bkey != bk) {
        sprs_free(&e->sp);
    }
    e->idx = idx;
    e->key = tile_key(idx, p, loaded);
    TapeUse tp;
    tile_tape(idx, p, &tp);
    PinUse pn;
    tile_pin(p, &pn);
    const float tilt = tile_tilt(idx, pn.on);
    const PaperShadows *sh = shadows_for(SC_TILE, (int)TW, (int)TH, CARD_R, paper_lift_k[LIFT_TILE].scale);
    const int later = in_layout && g_rest_later; // (its picture at rest is not needed until it lands)
    if (!e->sp.body) {
        TileBake tb = {p, loaded, isnew, idx, stamped, card_get((int)TW, (int)TH, CARD_R, 11u, PAD_BODY)};
        sprs_bake(&e->sp, TW, TH, PAD_BODY, PAD_REST, tilt, sh ? sh->s[0] : NULL, &tp, pn.on ? 0 : 1, &pn, draw_tile_body, &tb, !later);
        e->bkey = bk;
        return;
    }
    e->sp.pad = PAD_BODY;
    if (!later) sprs_bake_rest(&e->sp, TW, TH, PAD_REST, tilt, sh ? sh->s[0] : NULL, &tp, pn.on ? 0 : 1, &pn);
}

static void item_use(HItem *it, const Sprs *sp, uint32_t key, int own);

// a tile on the screen whose picture at rest was left for later (a shuffle), now that it lies still: made (1 if there is one)
static int rest_wanted(HomeCtx *c, const UiHome *h, int pass)
{
    for (int s = 0; s < NSLOT; s++) {
        HItem *it = &c->it[IT_TILE0 + s];
        if (!it->active || it->sp.rest || it->shf_t0 > 0) continue;
        int idx = h->top_row * COLS + s;
        TileCache *e = &g_tc[idx % (int)(sizeof g_tc / sizeof g_tc[0])];
        if (e->idx != idx || !e->sp.body || e->sp.rest) continue;
        if (!pass) return 1;
        TapeUse tp;
        PinUse pn;
        const UiPerson *q = idx < h->n ? &h->people[idx] : NULL;
        tile_tape(idx, q, &tp);
        tile_pin(q, &pn);
        const PaperShadows *sh = shadows_for(SC_TILE, (int)TW, (int)TH, CARD_R, paper_lift_k[LIFT_TILE].scale);
        sprs_bake_rest(&e->sp, TW, TH, PAD_REST, tile_tilt(idx, pn.on), sh ? sh->s[0] : NULL, &tp, pn.on ? 0 : 1, &pn);
        item_use(it, &e->sp, e->key, 0);
        return 1;
    }
    return 0;
}

// ---- the loaded profile's card

typedef struct {
    int nobody, guest, color;
    char name[64], sub[160];
    const PaperSpr *card;
} HeroBake;

static void hero_text(const UiHome *h, HeroBake *b)
{
    Skin *u = &G.skin;
    memset(b, 0, sizeof *b);
    b->nobody = !h->card_on;
    b->guest = b->nobody && h->guest;
    b->color = b->nobody ? (b->guest ? h->guest_look : 0) : h->card.color;
    const char *nm = b->nobody ? (h->guest ? "Guest" : "Nobody") : h->card.name;
    text_fit(u->semi, 44, 0, nm, 640, b->name, sizeof b->name);
    char raw[170];
    if (b->nobody && !h->loaded_sub[0]) snprintf(raw, sizeof raw, "Pick your name below to load your saves.");
    else if (b->guest) snprintf(raw, sizeof raw, "Miku  \xC2\xB7  %s", h->loaded_sub);
    else snprintf(raw, sizeof raw, "%s", h->loaded_sub);
    text_fit(u->med, 20, 0, raw, 740, b->sub, sizeof b->sub);
}

// a doodle that was put down with its anchor at (ax, ay) of the banner's picture is cut off where the paper ends
static void cut_at_card(PaperSpr *sp, int ax, int ay)
{
    for (int y = 0; y < sp->h; y++)
        for (int x = 0; x < sp->w; x++) {
            int sx = ax - sp->ox + x, sy = ay - sp->oy + y;
            if (sx < PAD_BODY || sx >= PAD_BODY + (int)HERO_W || sy < PAD_BODY || sy >= PAD_BODY + (int)HERO_H) sp->px[((size_t)y * sp->w + x) * 4 + 3] = 0;
        }
}

// The banner is doodled on in pastel crayon: a big shape behind the picture, a highlighter stroke under the name and a small doodle towards the Sign
// out button. There are several looks for it (the shapes of paper.h); the app picks one at random when it starts and keeps it until it closes.
enum { SMALL_SQUARE = -1 }; // (the small one is the mint scribbled square)
typedef struct {
    int big, big_size;
    uint32_t big_rgb;
    int small, small_size; // PAPER_SHAPE_* or SMALL_SQUARE
    uint32_t small_rgb, hi_rgb;
} BannerLook;
static const BannerLook BANNERS[] = {
    {PAPER_SHAPE_STAR, 136, 0xC7A4F6, SMALL_SQUARE, 36, 0xBDEBB0, 0xFFCDB0}, // (the first banner: a lilac star, a mint square)
    {PAPER_SHAPE_HEART, 132, 0xF6A6BD, PAPER_SHAPE_SPARKLE, 48, 0xFFDF7A, 0xE4CCFE},
    {PAPER_SHAPE_FLOWER, 142, 0xFFD36E, PAPER_SHAPE_BADGE, 44, 0xA9E5C3, 0xFFC9D4},
    {PAPER_SHAPE_CLOUD, 150, 0xA5CFF3, PAPER_SHAPE_BURST, 46, 0xFFE48A, 0xFFD8B8},
    {PAPER_SHAPE_MOON, 136, 0xCDB8F5, PAPER_SHAPE_STAR, 44, 0xFFE08A, 0xFFC4CE},
    {PAPER_SHAPE_SPARKLE, 146, 0x9ADDBE, PAPER_SHAPE_HEART, 44, 0xF8B0C4, 0xFFE7A0},
    {PAPER_SHAPE_BURST, 140, 0xFFC08A, PAPER_SHAPE_FLOWER, 46, 0xD7BFFB, 0xE4CCFE},
    {PAPER_SHAPE_BADGE, 136, 0xF4A9D8, PAPER_SHAPE_STAR, 44, 0xA8E6CF, 0xFFEFA8},
};
enum { NBANNERS = (int)(sizeof BANNERS / sizeof BANNERS[0]) };
int g_ui_banner_variant = -1; // -1: choose at random the first time it is wanted; tests set one

static int banner_variant(void)
{
    if (g_ui_banner_variant < 0 || g_ui_banner_variant >= NBANNERS) {
        uint32_t r = (uint32_t)ui_now() * 2654435761u;
        r ^= r >> 15;
        r *= 2246822519u;
        r ^= r >> 13;
        g_ui_banner_variant = (int)(r % (uint32_t)NBANNERS);
    }
    return g_ui_banner_variant;
}

static void draw_hero_body(Surf *s, void *ud)
{
    const HeroBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Theme *t = &u->th;
    const float pad = PAD_BODY;
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    // doodled on the card in pastel crayon, under everything else: a big shape behind the picture, a highlighter stroke under the name, and a
    // small doodle towards the Sign out button
    const BannerLook *bl = &BANNERS[banner_variant()];
    uint32_t hi_rgb = bl->hi_rgb;
    hi_rgb = pal_mix(hi_rgb, g_pal.paper, PAL(hi_mix, 0.0f)); // (Paper night: towards the paper, so the light name over it still reads)
    PaperSpr *star = paper_shape(bl->big, bl->big_size, bl->big_rgb, 31u), *hi = paper_hatch((int)text_width(u->semi, 44, 0, b->name) + 20, 16, hi_rgb, 0.85f, 32u),
             *mint = bl->small == SMALL_SQUARE ? paper_scribble(bl->small_size, bl->small_rgb, &PAPER_CRAYON_STD, 33u) : paper_shape(bl->small, bl->small_size, bl->small_rgb, 33u);
    if (star) {
        cut_at_card(star, (int)(pad + 60), (int)(pad + 60));
        paper_spr_put(s, star, (int)(pad + 60), (int)(pad + 60));
    }
    if (hi) paper_spr_put(s, hi, (int)(pad + 158), (int)(pad + 77));
    if (mint) paper_spr_put(s, mint, (int)(pad + 884), (int)(pad + 34));
    paper_spr_free(star);
    paper_spr_free(hi);
    paper_spr_free(mint);
    float ax = pad + 90, ay = pad + 60;
    if (b->guest) {
        skin_avatar(&c, ax, ay, 40, "Miku", b->color);
    } else if (b->nobody) {
        gfx_circle(s, ax, ay, 40, t->panel_alt);
        skin_icon_person(&c, ax, ay + 2, 42, with_a(t->text3, 0.95f));
    } else {
        skin_avatar(&c, ax, ay, 40, b->name, b->color);
    }
    const PaperSpr *ring = ring_get(41.0f, 21u);
    if (ring) paper_spr_put(s, ring, (int)ax, (int)ay);
    skin_txtc(&c, u->semi, 44, 0, pad + 166, pad + 64, 0, b->nobody ? t->text2 : t->text, b->name);
    skin_txtc(&c, u->med, 20, 0, pad + 166, pad + 99, 0, t->text2, b->sub);
}

static uint32_t hero_key(const HeroBake *b)
{
    uint32_t k = 2166136261u;
    k = fnv_int(k, b->nobody);
    k = fnv_int(k, b->guest);
    k = fnv_int(k, b->color);
    k = fnv_int(k, banner_variant());
    k = fnv_str(k, b->name);
    return fnv_str(k, b->sub);
}

static void hero_tapes(TapeUse tp[2])
{
    tp[0].t = (PaperTape){84, 26, paper_tape_rgb[6], PAT_STRIPES, PAPER_TAPE_ZIG, -36.0f, 201};
    tp[1].t = (PaperTape){84, 26, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG, 36.0f, 202};
    paper_tape_offset(HERO_W, HERO_H, -2, -4, 84, 26, &tp[0].dx, &tp[0].dy);
    paper_tape_offset(HERO_W, HERO_H, HERO_W - 70, -8, 84, 26, &tp[1].dx, &tp[1].dy);
    paper_tape_host(&tp[0].t, HERO_W, HERO_H, CARD_R, -2, -4);
    paper_tape_host(&tp[1].t, HERO_W, HERO_H, CARD_R, HERO_W - 70, -8);
}

// ---- the Sign out button (it sits on the loaded profile's card, lifts on its own, and goes where the card goes)

static void draw_sign_body(Surf *s, void *ud)
{
    (void)ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const float pad = PAD_BODY;
    gfx_rrect(s, pad, pad, SGN_W, SGN_H, 8, hex(PAL(sign_fill, 0xE8F8E0), 1));
    float cy = pad + SGN_H / 2, bx = pad + 16 + 15;
    gfx_circle(s, bx, cy, 15, hex(PAL(sign_disc, PAPER_PAPER), 1));
    gfx_ring(s, bx, cy, 13.75f, 2.5f, hex(PAL(glyph_ring, PAPER_TEXT_SOFT), 1));
    skin_txtc(&c, u->semi, 16, 0, bx, cy, 1, hex(PAL(glyph_ink, PAPER_TEXT), 1), "X");
    float ix = pad + 58, iy = cy - 15, k = 0.6f; // the door and the arrow
    Col ink = hex(PAL(sign_ink, PAPER_TEXT), 1);
    gfx_line(s, ix + 24 * k, iy + 8 * k, ix + 11 * k, iy + 8 * k, 2.7f, ink);
    gfx_line(s, ix + 11 * k, iy + 8 * k, ix + 11 * k, iy + 42 * k, 2.7f, ink);
    gfx_line(s, ix + 11 * k, iy + 42 * k, ix + 24 * k, iy + 42 * k, 2.7f, ink);
    gfx_line(s, ix + 21 * k, iy + 25 * k, ix + 41 * k, iy + 25 * k, 2.7f, ink);
    gfx_line(s, ix + 33 * k, iy + 17 * k, ix + 41 * k, iy + 25 * k, 2.7f, ink);
    gfx_line(s, ix + 41 * k, iy + 25 * k, ix + 33 * k, iy + 33 * k, 2.7f, ink);
    skin_txtc(&c, u->semi, 22, 0, pad + 100, cy, 0, hex(PAL(sign_ink, PAPER_TEXT), 1), "Sign out");
}

// ---- the search bar and the order chip

typedef struct {
    const UiHome *h;
    float w;
    const PaperSpr *card;
    char count[32], query[64], ph[48];
} SearchBake;

static void draw_search_body(Surf *s, void *ud)
{
    const SearchBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Theme *t = &u->th;
    const float pad = PAD_BODY, cy = pad + ROW_H / 2;
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    gfx_ring(s, pad + 30 + 12, pad + 11 + 12, 7.5f, 3.0f, t->text2); // the magnifier
    gfx_line(s, pad + 30 + 19, pad + 11 + 19, pad + 30 + 26, pad + 11 + 26, 3.0f, t->text2);
    float bx = pad + b->w - 30; // the mark at the right end: the Y button's, or (with a search on) the minus button's, which clears it
    if (b->query[0]) {
        gfx_circle(s, bx, cy, 14, PAL(glyph_flat, 0) ? t->panel : hex(paper_tape_rgb[5], 1));
        gfx_ring(s, bx, cy, 13.75f, 2.5f, hex(PAL(glyph_ring, PAPER_TEXT_SOFT), 1));
        gfx_line(s, bx - 6, cy, bx + 6, cy, 2.2f, hex(PAL(glyph_ink, PAPER_TEXT), 1));
        skin_txtc(&c, u->med, 18, 0, bx - 14 - 16, cy, 2, t->text3, b->count);
        skin_txtc(&c, u->semi, 21, 0, pad + 70, cy, 0, t->text, b->query);
        return;
    }
    gfx_circle(s, bx, cy, 14, PAL(glyph_flat, 0) ? t->panel : hex(paper_tape_rgb[2], 1));
    gfx_ring(s, bx, cy, 13.75f, 2.5f, hex(PAL(glyph_ring, PAPER_TEXT_SOFT), 1));
    skin_txtc(&c, u->semi, 15, 0, bx, cy, 1, hex(PAL(glyph_ink, PAPER_TEXT), 1), "Y");
    skin_txtc(&c, u->med, 21, 0, pad + 70, cy, 0, t->text2, b->ph);
}

static uint32_t search_key(const SearchBake *b)
{
    uint32_t k = 2166136261u;
    k = fnv_int(k, (int)b->w);
    k = fnv_str(k, b->query);
    k = fnv_str(k, b->count);
    return fnv_str(k, b->ph);
}

static void search_bake_text(const UiHome *h, float w, SearchBake *b)
{
    Skin *u = &G.skin;
    memset(b, 0, sizeof *b);
    b->h = h;
    b->w = w;
    if (h->query[0]) {
        snprintf(b->count, sizeof b->count, "%d of %d", h->n, h->total);
        float room = w - 70 - 30 - 14 - 16 - text_width(u->med, 18, 0, b->count) - 24;
        text_fit(u->semi, 21, 0, h->query, room, b->query, sizeof b->query);
    }
    snprintf(b->ph, sizeof b->ph, "Search %d profile%s", h->total, h->total == 1 ? "" : "s");
}

typedef struct {
    float w;
    const PaperSpr *card;
    char label[32];
    int idx; // (the order's place in the menu: the sticker's colour)
} ChipBake;

static void draw_chip_body(Surf *s, void *ud)
{
    const ChipBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Theme *t = &u->th;
    const float pad = PAD_BODY, cy = pad + ROW_H / 2;
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    gfx_circle(s, pad + CHIP_R3_CX, cy, 15, PAL(glyph_flat, 0) ? t->panel : hex(paper_tape_rgb[5], 1));
    gfx_ring(s, pad + CHIP_R3_CX, cy, 13.75f, 2.5f, hex(PAL(glyph_ring, PAPER_TEXT_SOFT), 1));
    skin_txtc(&c, u->semi, 12.5f, 0, pad + CHIP_R3_CX, cy, 1, hex(PAL(glyph_ink, PAPER_TEXT), 1), "R3");
    stk_sticker_toned(&c, pad + STK_X, pad + STK_Y, stk_w(b->w), STK_H, b->label, b->idx); // (the sticker out of the sleeve)
}

// ---- the A-Z rail

typedef struct {
    int have[26];
    float cur, az; // the letter with the ink disc and the first letter in the window, part of the way between letters while the rail scrolls
    const PaperSpr *card;
} RailBake;

static void draw_rail_body(Surf *s, void *ud)
{
    const RailBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const float pad = PAD_BODY;
    paper_spr_put(s, b->card, PAD_BODY, PAD_BODY);
    Col arrow = hex(PAL(rail_arrow, 0x4A4658), 1);
    float cx = pad + RAIL_W / 2;
    Col up = with_a(arrow, b->az <= 0.01f ? 0.25f : 1.0f), dn = with_a(arrow, b->az + RAIL_ROWS >= 25.99f ? 0.25f : 1.0f);
    gfx_line(s, cx - 6, pad + 24, cx, pad + 18, 3.0f, up);
    gfx_line(s, cx, pad + 18, cx + 6, pad + 24, 3.0f, up);
    gfx_line(s, cx - 6, pad + RAIL_H - 24, cx, pad + RAIL_H - 18, 3.0f, dn);
    gfx_line(s, cx, pad + RAIL_H - 18, cx + 6, pad + RAIL_H - 24, 3.0f, dn);
    // the window of letters slides (its first letter is b->az), and the ones going out of it or coming in fade at its ends
    const float top = pad + RAIL_LIST_Y, bot = top + RAIL_ROWS * RAIL_ROW_H, half = RAIL_ROW_H / 2.0f;
    if (b->cur >= 0) { // the ink disc, on its letter or on its way to the next one
        float dy = top + (b->cur - b->az) * RAIL_ROW_H + half;
        if (dy > top - half && dy < bot + half) gfx_circle(s, cx, dy, 12, hex(PAL(ink, PAPER_INK), 1));
    }
    int first = (int)floorf(b->az) - 1;
    for (int l = first; l <= first + RAIL_ROWS + 2; l++) {
        if (l < 0 || l > 25) continue;
        float cy = top + (l - b->az) * RAIL_ROW_H + half;
        float k = cy < top + half ? (cy - top) / half : cy > bot - half ? (bot - cy) / half : 1.0f;
        if (k <= 0.02f) continue;
        if (k > 1) k = 1;
        char ch[2] = {(char)('A' + l), 0};
        if (b->cur >= 0 && fabsf(l - b->cur) < 0.5f)
            skin_txtc(&c, u->semi, 16, 0, cx, cy, 1, hex(PAL(ink_on, 0xFFFFFF), k), ch);
        else
            skin_txtc(&c, b->have[l] ? u->semi : u->med, 16, 0, cx, cy, 1, b->have[l] ? hex(PAL(rail_on, PAPER_TEXT), k) : hex(PAL(rail_off, 0xAEA9BE), k), ch);
    }
}

// The rail scrolls to a new letter instead of jumping there: the ink disc runs through the letters between and the window slides with it, quickly for
// a step to the next letter and a little longer for a far jump (B to O), easing out.
static struct {
    float cur, az, from_cur, from_az, to_cur, to_az;
    double t0, dur;
    int on, set;
} g_rail;

static void rail_snap(int cur)
{
    g_rail.cur = g_rail.to_cur = (float)cur;
    g_rail.az = g_rail.to_az = (float)g_az;
    g_rail.on = 0;
    g_rail.set = 1;
}

static void rail_aim(int cur, double now)
{
    if (!g_rail.set || cur < 0 || g_rail.cur < 0) {
        rail_snap(cur);
        return;
    }
    if ((float)cur == g_rail.to_cur && (float)g_az == g_rail.to_az) return;
    g_rail.from_cur = g_rail.cur;
    g_rail.from_az = g_rail.az;
    g_rail.to_cur = (float)cur;
    g_rail.to_az = (float)g_az;
    float d = fabsf(g_rail.to_cur - g_rail.from_cur);
    g_rail.dur = fmin(300.0, 90.0 + 16.0 * d);
    g_rail.t0 = now;
    g_rail.on = 1;
}

char g_ui_home_names[3][40]; // tests: the first three on the grid in the last frame
int g_ui_home_focus; // tests: where the focus was in the last frame (0: the cards)
float g_ui_home_scroll; // tests: where the grid was in the last frame (rows: top_row, and the part of a row it had moved past it)
float g_ui_home_rail_cur = -1; // tests: where the rail's ink disc was in the last frame (a letter number, part way while it scrolls)

// 1 while it is scrolling
static int rail_tick(double now)
{
    if (!g_rail.on) return 0;
    double p = (now - g_rail.t0) / g_rail.dur;
    if (p >= 1.0) {
        g_rail.cur = g_rail.to_cur;
        g_rail.az = g_rail.to_az;
        g_rail.on = 0;
        return 1;
    }
    if (p < 0) p = 0;
    float e = (float)(1.0 - (1.0 - p) * (1.0 - p) * (1.0 - p));
    g_rail.cur = g_rail.from_cur + (g_rail.to_cur - g_rail.from_cur) * e;
    g_rail.az = g_rail.from_az + (g_rail.to_az - g_rail.from_az) * e;
    return 1;
}

// keep the current letter inside the rail's window (as the mockup does)
static void rail_follow(int cur)
{
    if (cur < 0) return;
    if (cur < g_az) g_az = cur;
    else if (cur >= g_az + RAIL_ROWS) g_az = cur - RAIL_ROWS + 1;
    if (g_az < 0) g_az = 0;
    if (g_az > 26 - RAIL_ROWS) g_az = 26 - RAIL_ROWS;
}

static int rail_cur(const UiHome *h)
{
    return h->sel < h->n ? letter_of(h->people[h->sel].name) : -1;
}

// ---- overlays: baked text on a card (the toast) and plain text (the readout)

typedef struct {
    const char *msg;
    int kind;
    float w;
} ToastBake;

static void draw_toast(Surf *s, void *ud)
{
    const ToastBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Theme *t = &u->th;
    const float pad = 20, h = 48;
    gfx_shadow(s, pad, pad, b->w, h, 6, 4, 8, with_a(t->shadow, 0.30f));
    gfx_rrect(s, pad, pad, b->w, h, 6, t->panel);
    gfx_rrect_stroke(s, pad + 0.5f, pad + 0.5f, b->w - 1, h - 1, 6, 1.0f, col(0, 0, 0, 0.08f));
    float cy = pad + h / 2;
    gfx_circle(s, pad + 30, cy, 13, hex(PAL(ink, PAPER_INK), 1));
    if (b->kind == 1) skin_icon_info(&c, pad + 30, cy, 22, hex(PAL(ink_on, 0xFFFFFF), 1));
    else skin_icon_check(&c, pad + 30, cy, 17, 2.6f, hex(PAL(ink_on, 0xFFFFFF), 1));
    skin_txtc(&c, u->semi, 20, 0, pad + 54, cy, 0, t->text, b->msg);
}

static void draw_dbg(Surf *s, void *ud)
{
    Cx c = ui_cx(s);
    skin_txtc(&c, c.skin->semi, 15, 0, 4, 12, 0, c.skin->th.text2, ud);
}

static void ovl_free(Ovl *o)
{
    paper_spr_free(o->spr);
    o->spr = NULL;
}

// ------------------------------------------------------------------------------------------------ setting the items up

static void item_geom(HItem *it, int kind, float x, float y, float w, float h)
{
    it->kind = kind;
    it->x = x;
    it->y = y;
    it->w = w;
    it->h = h;
    it->r = CARD_R;
    it->grow = 0;
    it->tilt = 0;
    it->ntp = 0;
    it->ghost = 0;
    it->cls = SC_TILE;
    it->lk = -1;
}

static void item_lift_kind(HItem *it, int lk)
{
    it->lk = lk;
    it->grow = paper_lift_k[lk].scale;
}

// uses the sprites `sp` for the item; a change makes it draw again
static void item_use(HItem *it, const Sprs *sp, uint32_t key, int own)
{
    if (it->sp.rest != sp->rest || it->sp.body != sp->body || it->key != key) {
        if (it->own) sprs_free(&it->sp);
        it->sp = *sp;
        it->own = own;
        it->key = key;
        it->force = 1;
        it->ver += 1.0f;
    }
}

static void item_finish(HItem *it)
{
    it->sh = it->lk >= 0 ? shadows_for(it->cls, (int)it->w, (int)it->h, it->r, it->grow) : NULL;
    it->ghost_spr = it->ghost ? ghost_get((int)it->w, (int)it->h, it->r + 1.0f) : NULL;
}

static uint32_t person_key(const UiHome *h, int idx);
static void pin_set(HItem *it, int up);

static void tile_setup(HomeCtx *c, HItem *it, const UiHome *h, int idx)
{
    int isnew = idx == h->n;
    const UiPerson *p = isnew ? NULL : &h->people[idx];
    int loaded = !isnew && idx == h->loaded;
    float x, y;
    tile_xy(h, idx, &x, &y);
    item_geom(it, IK_TILE, x, y, TW, TH);
    item_lift_kind(it, LIFT_TILE);
    it->cls = SC_TILE;
    it->ghost = 1;
    tile_pin(p, &it->pn);
    it->tilt_pin = tile_tilt(idx, 1);
    it->tilt_tape = tile_tilt(idx, 0);
    it->tilt = it->pn.on ? it->tilt_pin : it->tilt_tape;
    if (it->pkey != person_key(h, idx)) pin_set(it, 0);
    it->pkey = person_key(h, idx);
    it->ntp = it->pn.on ? 0 : 1; // (a pinned card has no tape on; tp[0] is still its tape, for the animation that puts one on or takes it off)
    tile_tape(idx, p, &it->tp[0]);
    TileCache *e = &g_tc[idx % (int)(sizeof g_tc / sizeof g_tc[0])];
    uint32_t key = tile_key(idx, p, loaded);
    if (!e->sp.body || e->idx != idx || e->key != key) tile_build(e, h, idx, p, loaded, isnew, 1);
    item_use(it, &e->sp, key, 0);
    item_finish(it);
    (void)c;
}

static void hero_setup(HomeCtx *c, HItem *it, const UiHome *h)
{
    HeroBake b;
    hero_text(h, &b);
    item_geom(it, IK_HERO, HERO_X, HERO_Y, HERO_W, HERO_H);
    item_lift_kind(it, LIFT_ROW);
    it->cls = SC_HERO;
    it->ghost = 1;
    it->tilt = rad(-0.3f);
    it->ntp = 2;
    hero_tapes(it->tp);
    uint32_t key = hero_key(&b);
    if (!it->sp.body || it->key != key) {
        b.card = card_get((int)HERO_W, (int)HERO_H, CARD_R, 12u, PAD_BODY);
        const PaperShadows *sh = shadows_for(SC_HERO, (int)HERO_W, (int)HERO_H, CARD_R, paper_lift_k[LIFT_ROW].scale);
        Sprs sp;
        sprs_bake(&sp, HERO_W, HERO_H, PAD_BODY, PAD_HERO_REST, it->tilt, sh ? sh->s[0] : NULL, it->tp, 2, NULL, draw_hero_body, &b, 1);
        item_use(it, &sp, key, 1);
    }
    item_finish(it);
    (void)c;
}

static void sign_setup(HomeCtx *c, HItem *it, const UiHome *h)
{
    float x, y, w, hh;
    signout_rect(&x, &y, &w, &hh);
    item_geom(it, IK_SIGN, x, y, w, hh);
    it->r = 8;
    item_lift_kind(it, LIFT_BTN);
    it->cls = SC_BTN;
    it->active = h->card_on;
    if (!it->sp.body) {
        Sprs sp;
        sprs_bake(&sp, w, hh, PAD_BODY, 0, 0, NULL, NULL, 0, NULL, draw_sign_body, NULL, 0);
        item_use(it, &sp, 1, 1);
    }
    item_finish(it);
    (void)c;
}

static void search_setup(HomeCtx *c, HItem *it, const UiHome *h)
{
    float x, y, w, hh;
    search_rect(h, &x, &y, &w, &hh);
    item_geom(it, IK_SEARCH, x, y, w, hh);
    item_lift_kind(it, LIFT_ROW);
    it->cls = SC_SEARCH;
    it->ghost = 1;
    SearchBake b;
    search_bake_text(h, w, &b);
    uint32_t key = search_key(&b);
    if (!it->sp.body || it->key != key) {
        b.card = card_get((int)w, (int)hh, CARD_R, 13u, PAD_BODY);
        const PaperShadows *sh = shadows_for(SC_SEARCH, (int)w, (int)hh, CARD_R, paper_lift_k[LIFT_ROW].scale);
        Sprs sp;
        sprs_bake(&sp, w, hh, PAD_BODY, PAD_REST, 0, sh ? sh->s[0] : NULL, NULL, 0, NULL, draw_search_body, &b, 1);
        item_use(it, &sp, key, 1);
    }
    item_finish(it);
    (void)c;
}

static void chip_setup(HomeCtx *c, HItem *it, const UiHome *h)
{
    float x, y, w, hh;
    sort_rect(h, &x, &y, &w, &hh);
    item_geom(it, IK_CHIP, x, y, w, hh);
    item_lift_kind(it, LIFT_BTN);
    it->cls = SC_CHIP;
    it->ghost = 1;
    ChipBake b;
    memset(&b, 0, sizeof b);
    b.w = w;
    snprintf(b.label, sizeof b.label, "%s", h->sort_label);
    b.idx = h->sort_mode;
    uint32_t key = fnv_str(fnv_int(fnv_int(2166136261u, (int)w), b.idx), b.label);
    if (!it->sp.body || it->key != key) {
        b.card = card_get((int)w, (int)hh, CARD_R, 14u, PAD_BODY);
        const PaperShadows *sh = shadows_for(SC_CHIP, (int)w, (int)hh, CARD_R, paper_lift_k[LIFT_BTN].scale);
        Sprs sp;
        sprs_bake(&sp, w, hh, PAD_BODY, PAD_REST, 0, sh ? sh->s[0] : NULL, NULL, 0, NULL, draw_chip_body, &b, 1);
        item_use(it, &sp, key, 1);
    }
    item_finish(it);
    (void)c;
}

static void rail_setup(HomeCtx *c, HItem *it, const UiHome *h)
{
    item_geom(it, IK_RAIL, RAIL_X, RAIL_Y, RAIL_W, RAIL_H);
    it->r = 22;
    it->cls = SC_RAIL;
    it->active = h->rail;
    if (!h->rail) return;
    RailBake b;
    memset(&b, 0, sizeof b);
    for (int i = 0; i < h->n; i++) {
        int l = letter_of(h->people[i].name);
        if (l >= 0) b.have[l] = 1;
    }
    if (!g_rail.set) rail_snap(rail_cur(h));
    b.cur = g_rail.cur;
    b.az = g_rail.az;
    uint32_t key = fnv(fnv_int(fnv_int(2166136261u, (int)lroundf(b.cur * 64)), (int)lroundf(b.az * 64)), b.have, sizeof b.have);
    if (!it->sp.body || it->key != key) {
        b.card = card_get((int)RAIL_W, (int)RAIL_H, 22.0f, 15u, PAD_BODY);
        const PaperShadows *sh = shadows_for(SC_RAIL, (int)RAIL_W, (int)RAIL_H, 22.0f, 0.0f);
        Sprs sp;
        sprs_bake(&sp, RAIL_W, RAIL_H, PAD_BODY, PAD_REST, 0, sh ? sh->s[0] : NULL, NULL, 0, NULL, draw_rail_body, &b, 1);
        item_use(it, &sp, key, 1);
    }
    (void)c;
}

// everything that depends on what the grid shows: call after the list, the scroll position, the selection or the order changed
static void layout(HomeCtx *c, const UiHome *h)
{
    g_tp_gen++;
    for (int s = 0; s < NSLOT + COLS; s++) { // (the last row of items: the row coming in under the two while the grid scrolls)
        HItem *it = &c->it[IT_TILE0 + s];
        int idx = h->top_row * COLS + s;
        it->active = idx <= h->n && (s < NSLOT || g_row_off > 0);
        if (it->active) tile_setup(c, it, h, idx);
    }
    c->it[IT_HERO].active = 1;
    hero_setup(c, &c->it[IT_HERO], h);
    search_setup(c, &c->it[IT_SEARCH], h);
    chip_setup(c, &c->it[IT_CHIP], h);
    sign_setup(c, &c->it[IT_SIGN], h);
    rail_setup(c, &c->it[IT_RAIL], h);
    c->it[IT_SEARCH].active = 1;
    c->it[IT_CHIP].active = 1;
}

// ---- the grid as it was when the home screen was left to be ordered or narrowed anew: the next one shuffles out of it

typedef struct {
    uint32_t key; // who: the name and look (or the New profile tile)
    float x, y, tilt;
    int slot;
    PaperSpr *body; // a copy of the tile's card (for one that leaves the screen)
    TapeUse tp;
    PinUse pn;
} ShufWas;
static struct {
    int n, armed, fade; // fade: the next screen comes in over something else (a keyboard) first
    ShufWas w[NSLOT];
    int rail;           // the A-Z rail showed
    PaperSpr *rail_body; // (a copy of it, to slide away if the next order has none)
} g_was;
#define RAIL_SLIDE 70.0f // px right of its place the rail slides in from, or away to

static uint32_t person_key(const UiHome *h, int idx)
{
    if (idx < 0 || idx >= h->n) return 0x51EEu;
    return fnv_int(fnv_str(2166136261u, h->people[idx].name), h->people[idx].color);
}

static PaperSpr *spr_dup(const PaperSpr *p)
{
    PaperSpr *o = p ? paper_spr_new(p->w, p->h) : NULL;
    if (!o) return NULL;
    memcpy(o->px, p->px, (size_t)p->w * p->h * 4);
    o->ox = p->ox;
    o->oy = p->oy;
    return o;
}

static void shuf_forget(void)
{
    for (int i = 0; i < g_was.n; i++) paper_spr_free(g_was.w[i].body);
    paper_spr_free(g_was.rail_body);
    memset(&g_was, 0, sizeof g_was);
}

static void shuf_remember(HomeCtx *c, const UiHome *h, int fade)
{
    shuf_forget();
    for (int s = 0; s < NSLOT; s++) {
        const HItem *it = &c->it[IT_TILE0 + s];
        if (!it->active) continue;
        ShufWas *w = &g_was.w[g_was.n++];
        w->key = person_key(h, h->top_row * COLS + s);
        w->x = it->x;
        w->y = it->y;
        w->tilt = it->tilt;
        w->slot = s;
        w->body = spr_dup(it->sp.body);
        w->tp = it->tp[0];
        w->pn = it->pn;
    }
    g_was.rail = c->it[IT_RAIL].active;
    if (g_was.rail) g_was.rail_body = spr_dup(c->it[IT_RAIL].sp.body);
    g_was.armed = 1;
    g_was.fade = fade;
}

// tiles still sinking away from a shuffle the last visit left in the middle: gone
static void shuf_clear(HomeCtx *c)
{
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        it->shf_t0 = 0;
        if (i < IT_LEAVE0) continue;
        if (it->own) sprs_free(&it->sp);
        memset(it, 0, sizeof *it);
    }
}

// The grid as laid out now shuffles out of the one remembered (its first frame is that one, as the screen still shows it).
int g_ui_home_shuf_n; // tests: how many tiles the last shuffle carried, brought in or sent away

static void shuf_start(HomeCtx *c, const UiHome *h, double now)
{
    if (!g_was.armed) return;
    int used[NSLOT] = {0}, match[NSLOT];
    for (int s = 0; s < NSLOT; s++) {
        match[s] = -1;
        if (!c->it[IT_TILE0 + s].active) continue;
        uint32_t key = person_key(h, h->top_row * COLS + s);
        for (int j = 0; j < g_was.n; j++)
            if (!used[j] && g_was.w[j].key == key) {
                match[s] = j;
                used[j] = 1;
                break;
            }
    }
    const double t = now + 30; // (a moment for the first frame to be up)
    int nl = 0, k = 0;
    for (int j = 0; j < g_was.n; j++) { // the ones that leave the screen sink away first
        ShufWas *w = &g_was.w[j];
        if (used[j] || !w->body) continue;
        HItem *it = &c->it[IT_LEAVE0 + nl++];
        item_geom(it, IK_TILE, w->x, w->y, TW, TH);
        item_lift_kind(it, LIFT_TILE);
        it->cls = SC_TILE;
        it->tilt = w->tilt;
        it->tilt_pin = it->tilt_tape = w->tilt;
        it->pn = w->pn;
        it->ntp = w->pn.on ? 0 : 1;
        it->tp[0] = w->tp;
        it->sp.body = w->body;
        it->sp.pad = PAD_BODY;
        it->sp.rox = it->sp.roy = PAD_BODY;
        it->own = 1;
        w->body = NULL;
        it->key = 0x1EA7u + (uint32_t)j;
        it->force = 1;
        item_finish(it);
        paper_lift_init(&it->lift, LIFT_TILE);
        it->active = 1;
        it->shf_mode = SHF_OUT;
        it->shf_dx = 0, it->shf_dy = SHF_DROP;
        it->shf_dur = (float)SHF_OUT_MS;
        it->shf_t0 = t + w->slot * SHF_STEP_MS * 0.5;
    }
    HItem *rl = &c->it[IT_RAIL];
    if (rl->active && !g_was.rail) { // the rail comes with name order: it slides in from the right
        rl->shf_mode = SHF_IN;
        rl->shf_dx = RAIL_SLIDE, rl->shf_dy = 0;
        rl->shf_dur = (float)SHF_IN_MS;
        rl->shf_t0 = t + 120.0;
    } else if (!rl->active && g_was.rail && g_was.rail_body && nl < NSLOT) { // and goes with it: it slides away
        HItem *it = &c->it[IT_LEAVE0 + nl++];
        item_geom(it, IK_RAIL, RAIL_X, RAIL_Y, RAIL_W, RAIL_H);
        it->r = 22;
        it->cls = SC_RAIL;
        it->sp.body = g_was.rail_body;
        it->sp.pad = PAD_BODY;
        it->sp.rox = it->sp.roy = PAD_BODY;
        it->own = 1;
        g_was.rail_body = NULL;
        it->key = 0x1EA7u + 99u;
        it->force = 1;
        it->active = 1;
        it->shf_mode = SHF_OUT;
        it->shf_dx = RAIL_SLIDE, it->shf_dy = 0;
        it->shf_dur = (float)SHF_OUT_MS;
        it->shf_t0 = t;
    }
    const double t2 = t + (nl ? 70.0 : 0.0);
    for (int s = 0; s < NSLOT; s++) {
        HItem *it = &c->it[IT_TILE0 + s];
        if (!it->active) continue;
        if (match[s] >= 0) {
            const ShufWas *w = &g_was.w[match[s]];
            float dx = w->x - it->x, dy = w->y - it->y, da = w->tilt - it->tilt;
            if (fabsf(dx) < 0.5f && fabsf(dy) < 0.5f && fabsf(da) < 1e-4f) continue; // (where it was)
            it->shf_mode = SHF_MOVE;
            it->shf_dx = dx, it->shf_dy = dy, it->shf_da = da;
            it->shf_dur = (float)SHF_MOVE_MS;
            it->shf_t0 = t2 + k++ * SHF_STEP_MS;
        } else {
            it->shf_mode = SHF_IN;
            it->shf_dx = 0, it->shf_dy = SHF_DROP;
            it->shf_dur = (float)SHF_IN_MS;
            it->shf_t0 = t2 + 60.0 + k++ * SHF_STEP_MS;
        }
    }
    g_ui_home_shuf_n = k + nl;
    if (k || nl) ui_sound(SND_RIFFLE, 0.42f, 30); // (cards being shuffled)
    shuf_forget();
}

// ------------------------------------------------------------------------------------------------ the pin
// M3: a pinned card takes the focus like every card (it lifts and wiggles, the wiggle turned about its pin and smaller), and its pin rises out of it,
// ~14 px, a moment after (eased out, a little past, back); when the focus leaves, the pin goes home quickly.
#define PIN_UP_H 14.0f   // how far the pin comes up out of a card in focus (px)
#define PIN_AIR_H 34.0f  // how high it is held as it is pushed in or pulled out
#define PIN_RISE_AT 40.0 // ms after the card is grabbed that the pin starts to rise
#define PIN_RISE_MS 220.0
#define PIN_HOME_MS 90.0

static float sm01(float u) { u = u < 0 ? 0 : u > 1 ? 1 : u; return u * u * (3.0f - 2.0f * u); }
static float eout3(float u) { u = u < 0 ? 0 : u > 1 ? 1 : u; return 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u); }
static float ein2(float u) { u = u < 0 ? 0 : u > 1 ? 1 : u; return u * u; }

static float pin_height(const HItem *it, double now)
{
    if (!it->pn.on) return 0.0f;
    if (it->pin_up) {
        float u = (float)((now - it->pin_t0 - PIN_RISE_AT) / PIN_RISE_MS);
        if (u <= 0.0f) return it->pin_from;
        if (u >= 1.0f) return PIN_UP_H;
        return it->pin_from + (PIN_UP_H - it->pin_from) * eout3(u) * (1.0f + 0.12f * sinf(3.14159265f * u));
    }
    float u = (float)((now - it->pin_t0) / PIN_HOME_MS);
    if (u >= 1.0f) return 0.0f;
    return it->pin_from * (1.0f - ein2(u));
}

static int pin_busy(const HItem *it, double now)
{
    return it->pn.on && now - it->pin_t0 < (it->pin_up ? PIN_RISE_AT + PIN_RISE_MS : PIN_HOME_MS);
}

// the pin goes up / home from wherever it is at `now`
static void pin_go(HItem *it, double now, int up)
{
    it->pin_from = pin_height(it, now);
    it->pin_t0 = now;
    it->pin_up = up;
}

// the pin is up / home already
static void pin_set(HItem *it, int up)
{
    it->pin_from = up ? PIN_UP_H : 0.0f;
    it->pin_t0 = -1e9;
    it->pin_up = up;
}

// what the lift of this item sounds like: 0 taped, 1 nothing on it (the search bar, the order chip, Sign out), 2 a pin through it
static int lift_kind_of(const HItem *it) { return it->pn.on ? 2 : it->ntp == 0; }

// The pin animation. A pin put in or pulled out plays on the person's card after the grid has shuffled to the new order (and there they are, on the
// screen, in focus): they play on this one person's tile (found by who it is, so a layout in the middle keeps it).
// The card is in focus (the person was just pinned or unpinned on it), so it stays up the whole time: the pin never goes all the way in only to come out
// again, and there is no second lift afterwards. It plays WHILE the shuffle carries the card to its new place (the card is lifted and carried, its strip comes
// off and its pin comes down on the way, and both land together): one motion, not a move and then a pinning.
//  IN  (u ms from t0): the strip comes off (up and away to the left, fading, 0-260); the pin, held 34 px up, comes down 40-300 to the height it has in focus
//      (14 px; slowly at first: on the console 4 frames of it were all that showed at 150 ms) and stays there, its sound at 300; the card turns from how it lay on
//      tape to how it hangs on its pin 60-300; at 360 the card is handed back to the focus as it is.
//  OUT: the pin is pulled up out of the card 0-160 (34 px, fading) as the card sets off; a new strip comes down out of the air 170-330 (end curled) onto the
//      card as it is in focus, its stick at 330 as the card lands; at 370 the card is handed back to the focus as it is.
enum { PA_NONE, PA_IN, PA_OUT };
#define PA_IN_MS 360.0
#define PA_IN_IMP 300.0 // the pin has come down to its height in focus
#define PA_OUT_STRIP 170.0 // the new strip starts to come down
#define PA_OUT_IMP 330.0 // and is down (as the card is set down)
#define PA_OUT_MS 370.0
#define PA_LEAD_MS 20.0 // after the card starts to be carried to its new place, before the animation starts (it plays during the carry)
static struct {
    int kind;
    uint32_t key;
    double t0;
    unsigned cues;
    PaperSpr *tape_sh; // the flying strip's shadow
} g_pa;

static void pa_clear(void)
{
    paper_spr_free(g_pa.tape_sh);
    memset(&g_pa, 0, sizeof g_pa);
}

static int pa_on(const HItem *it) { return g_pa.kind && it->kind == IK_TILE && it->pkey && it->pkey == g_pa.key; }

static void pa_cue(unsigned bit, double u, double at, int snd, float gain)
{
    if (u < at || (g_pa.cues & bit)) return;
    g_pa.cues |= bit;
    ui_sound(snd, gain, 0);
}

// ------------------------------------------------------------------------------------------------ focus and lifting

static int focus_item(const UiHome *h)
{
    switch (h->focus) {
    case Z_CHIP: return IT_CHIP;
    case Z_SEARCH: return IT_SEARCH;
    case Z_HERO: return IT_HERO;
    case Z_SIGNOUT: return IT_SIGN;
    default: {
        int s = h->sel - h->top_row * COLS;
        return s >= 0 && s < NSLOT ? IT_TILE0 + s : -1;
    }
    }
}

static void lifts_reset(HomeCtx *c, const UiHome *h)
{
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        paper_lift_init(&it->lift, it->lk >= 0 ? it->lk : LIFT_BTN);
        it->lift.tapeless = lift_kind_of(it); // (the search bar, the order chip, Sign out: their own sound; a pinned card: the pin's)
        pin_set(it, 0);
    }
    c->cur = focus_item(h);
    if (c->cur >= 0 && !c->it[c->cur].active) c->cur = -1;
    if (c->cur >= 0) { // up, and has finished its wiggle
        PaperLift *l = &c->it[c->cur].lift;
        l->target = 1;
        l->from = 1;
        l->dur = 0;
        l->wig_t0 = -1;
        l->wig_rest = -0.4f;
        pin_set(&c->it[c->cur], 1);
    }
    memset(&c->lean, 0, sizeof c->lean);
}

// the focus moved (or not): the old one sticks back down, the new one peels up
static void focus_sync(HomeCtx *c, const UiHome *h, double now, int held)
{
    int want = focus_item(h);
    if (want >= 0 && !c->it[want].active) want = -1;
    if (want == c->cur) return;
    if (c->cur >= 0 && !pa_on(&c->it[c->cur])) { // (the card the pin animation is on is driven by it: it takes the focus at the end if it still has it)
        paper_lift_release(&c->it[c->cur].lift, now, held);
        pin_go(&c->it[c->cur], now, 0);
    }
    if (want >= 0 && !pa_on(&c->it[want])) {
        paper_lift_grab(&c->it[want].lift, now, held);
        pin_go(&c->it[want], now, 1);
    }
    c->cur = want;
}

// The grid scrolled with the focus (a press past the bottom or the top): every tile is a different person now, so the lifts start over, but the focus
// still moves like any other: the tile that had it (a row up or down now, if it is still on the screen: old_item) sticks back down and the new one
// peels up, with their sounds.
static void lifts_rescroll(HomeCtx *c, const UiHome *h, int old_item, double now, int held)
{
    lifts_reset(c, h); // all at rest, the focus up
    int want = c->cur;
    if (want < 0 || want == old_item) return;
    HItem *nw = &c->it[want];
    paper_lift_init(&nw->lift, nw->lk >= 0 ? nw->lk : LIFT_BTN); // (the new one is not up yet)
    nw->lift.tapeless = lift_kind_of(nw);
    pin_set(nw, 0);
    c->cur = -1;
    if (old_item >= 0) { // the old one is up and has finished its wiggle: from here it sticks back
        PaperLift *l = &c->it[old_item].lift;
        l->target = 1;
        l->from = 1;
        l->dur = 0;
        l->wig_t0 = -1;
        l->wig_rest = -0.4f;
        pin_set(&c->it[old_item], 1);
        c->cur = old_item;
    }
    focus_sync(c, h, now, held);
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
    HItem *w = it;
    if (!it->active) {
        memset(&w->bb, 0, sizeof w->bb);
        return;
    }
    const PaperSpr *rest = sprs_rest(&it->sp);
    if (!it->moving && it->kind != IK_SIGN) {
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
            TapeEnt *e = tape_ent(&it->tp[i].t);
            PaperXf t = paper_xf_attach(cf, it->tp[i].dx, it->tp[i].dy);
            int tw = e->peel ? e->peel->s[0]->w : e->stuck ? e->stuck->w : 0, th = e->peel ? e->peel->s[0]->h : e->stuck ? e->stuck->h : 0;
            if (tw) box_xform(b, t.x, t.y, (float)tw, (float)th, t.scale, t.angle);
        }
        if (it->hold) {
            PaperXf a = paper_xf_attach(cf, it->pn.dx, it->pn.dy);
            float x0, y0, x1, y1;
            pins_reach(PIN_AIR_H, a.scale, &x0, &y0, &x1, &y1);
            box_add(b, a.x + x0, a.y + y0, a.x + x1, a.y + y1);
            if (pa_on(it)) { // (the strip being taken off, or put on, and its shadow: up to ~30 px off the card, a sprite ~100 px across)
                PaperXf t = paper_xf_attach(cf, it->tp[0].dx, it->tp[0].dy);
                box_add(b, t.x - 110, t.y - 110, t.x + 110, t.y + 120);
            }
        }
    }
    w->bb.x = b[0];
    w->bb.y = b[1];
    w->bb.w = b[2] - b[0];
    w->bb.h = b[3] - b[1];
}

// The boxes of what is stuck to or stands in a lifted card besides the card: its strips and its pin. The press carries these in with the card, so the
// rectangles kept still for the neighbours (which reach well past the neighbours' own cards) must not cover them.
static int item_extras(HItem *f, Rect out[4])
{
    int n = 0;
    PaperXf cf = {f->cx, f->cy, f->sc, f->ang};
    for (int i = 0; i < f->ntp && n < 3; i++) {
        TapeEnt *e = tape_ent(&f->tp[i].t);
        PaperXf t = paper_xf_attach(cf, f->tp[i].dx, f->tp[i].dy);
        int tw = e->peel ? e->peel->s[0]->w : e->stuck ? e->stuck->w : 0, th = e->peel ? e->peel->s[0]->h : e->stuck ? e->stuck->h : 0;
        int b[4] = {0, 0, 0, 0};
        if (tw) box_xform(b, t.x, t.y, (float)tw, (float)th, t.scale, t.angle);
        if (!box_empty(b)) out[n++] = (Rect){b[0], b[1], b[2] - b[0], b[3] - b[1]};
    }
    if (f->hold && f->pin_on && n < 4) {
        PaperXf a = paper_xf_attach(cf, f->pn.dx, f->pn.dy);
        float x0, y0, x1, y1;
        pins_reach(PIN_AIR_H, a.scale, &x0, &y0, &x1, &y1);
        out[n++] = (Rect){(int)(a.x + x0), (int)(a.y + y0), (int)(x1 - x0), (int)(y1 - y0)};
    }
    return n;
}

// ---- the shuffle: the grid ordered or narrowed anew. Each tile that was on the screen and still is is picked up where it was and carried to its
// new place (it rises a little on the way, its tape peeling, and is set down turned as its new place turns it); one that comes onto the screen rises
// in from a little under its place; one that goes sinks away and fades. They go one after another, the leaving ones first, by where they sit.

static double shf_ease(double u) { return u < 0.5 ? 4 * u * u * u : 1 - pow(-2 * u + 2, 3) / 2; }

static void shf_pose(HItem *it, double now, int *anim)
{
    double u = (now - it->shf_t0) / it->shf_dur;
    if (u >= 1.0) {
        it->shf_t0 = 0;
        it->force = 1;
        if (it->shf_mode == SHF_OUT) { // (gone: its picture was its own copy)
            it->active = 0;
            if (it->own) sprs_free(&it->sp);
            it->own = 0;
        }
        return;
    }
    *anim = 1;
    it->moving = 1;
    if (u < 0) u = 0;
    float e = (float)shf_ease(u), hump = sinf(3.14159265f * e), k = 1.0f - e;
    float up = hump * 0.85f;
    if (it->shf_mode == SHF_MOVE) {
        it->ps.x += it->shf_dx * k;
        it->ps.y += it->shf_dy * k - 10.0f * hump;
        it->ps.angle += it->shf_da * k;
        it->ps.ghost *= e;
    } else if (it->shf_mode == SHF_IN) { // (from dx, dy off its place)
        it->ps.x += it->shf_dx * k;
        it->ps.y += it->shf_dy * k;
        it->alpha = e;
        it->ps.ghost = 0;
        up = 0.6f * k;
    } else { // (to dx, dy off it)
        it->ps.x += it->shf_dx * e;
        it->ps.y += it->shf_dy * e;
        it->alpha = k;
        it->ps.ghost = 0;
        up = 0.6f * (1.0f - k * k);
    }
    if (it->kind == IK_RAIL) up = 0; // (the rail slides on the board)
    if (up > it->ps.p) { // (up off the board on the way: its shadow and its tape as a lift that high would have them)
        it->ps.scale += (paper_lift_k[LIFT_TILE].scale) * (up - it->ps.p);
        it->ps.p = up;
    }
}

// ---- the pin animation and the cards the pin holds: poses (the pin's height, the strip's place), found each frame

// the lift's pose at a lift of p, driven by hand (the pin animation presses the card down and lifts it back without the lift's own sounds)
static void pa_pose(HItem *it, float p)
{
    const PaperLiftK *k = &paper_lift_k[it->lk];
    it->ps.p = p;
    it->ps.x = k->dx * p;
    it->ps.y = -k->rise * p;
    it->ps.scale = 1.0f + k->scale * p;
    it->ps.angle = 0.0f;
    it->ps.ghost = p * 1.6f > 1.0f ? 1.0f : p * 1.6f;
}

// the animation is over: the card goes back to the focus exactly as the animation held it (up, its pin up if it has one), silently; if the focus has left it
// meanwhile, it goes down (and its pin home) the way any card does
static void pa_handover(HItem *it, double now)
{
    if (!pa_on(it)) return;
    const double t1 = g_pa.t0 + (g_pa.kind == PA_IN ? PA_IN_MS : PA_OUT_MS);
    if (now < t1) return;
    const int mine = g_hc.cur == (int)(it - g_hc.it);
    paper_lift_init(&it->lift, it->lk);
    it->lift.tapeless = lift_kind_of(it);
    PaperLift *l = &it->lift;
    if (mine) {
        l->target = 1;
        l->dur = 0.0;
        l->wig_rest = -0.4f; // (as at rest in focus, level but for that)
        pin_set(it, 1);
    } else {
        l->from = 1.0f;
        l->target = 0;
        l->t0 = now;
        l->dur = PAPER_LIFT_DOWN_MS;
        if (it->pn.on) {
            it->pin_from = PIN_UP_H;
            it->pin_t0 = now;
            it->pin_up = 0;
        } else {
            pin_set(it, 0);
        }
    }
    it->tilt = it->pn.on ? it->tilt_pin : it->tilt_tape;
    it->force = 1;
    pa_clear();
}

// Works out what holds the card this frame: for a pinned card its pin (and the dent, the smaller wiggle, the turn about the pin); while the pin animation
// plays on it, the pin or the strip as the timeline says. `tilt` is the card's turn at rest, which the animation changes.
static void hold_pose(HItem *it, double now, float *tilt, int *anim)
{
    it->hold = it->pin_on = it->tape_on = it->tape_air = 0;
    it->pin_h = it->pin_a = it->dent_a = it->tape_p = it->tf_up = it->tf_dx = it->tf_dy = it->tf_da = it->tape_a = 0.0f;
    it->tf_sc = 1.0f;
    it->piv = 0.0f;
    if (!it->active || it->kind != IK_TILE) return;
    const int pa = pa_on(it);
    if (!it->pn.on && !pa) return;
    it->hold = 1;
    if (!pa) { // a pinned card: the pin rises with the focus
        it->piv = 1.0f;
        it->pin_on = 1;
        it->pin_a = 1.0f;
        it->pin_h = pin_height(it, now);
        it->dent_a = 1.0f - sm01(it->pin_h / PIN_UP_H);
        return;
    }
    const double u = now - g_pa.t0;
    it->moving = 1;
    *anim = 1;
    if (g_pa.kind == PA_IN) {
        if (u < 0) { // (waiting for the shuffle to put it down: still the taped card it was)
            it->tape_on = 1;
            it->tape_a = 1.0f;
            it->tape_p = it->ps.p;
            *tilt = it->tilt_tape;
            return;
        }
        pa_pose(it, 1.0f); // (the card stays up: it is in focus)
        it->piv = sm01((float)(u / PA_IN_IMP)); // (it comes to hang on its pin)
        *tilt = it->tilt_pin + (it->tilt_tape - it->tilt_pin) * (1.0f - sm01((float)((u - 60.0) / (PA_IN_IMP - 60.0)))); // (it settles on its pin, straighter than it lay on tape)
        pa_cue(1u, u, 0.0, SND_PEEL, 0.8f);
        if (u < 260.0) { // the strip is taken off: lifted toward the eye and away up to the left, fading, its shadow falling behind
            const float f = (float)(u / 260.0), up = eout3(f);
            it->tape_on = it->tape_air = 1;
            it->tape_p = 1.0f;
            it->tf_up = up;
            it->tf_dx = -26.0f * up;
            it->tf_dy = -30.0f * up;
            it->tf_da = -rad(14.0f) * up;
            it->tf_sc = 1.0f + 0.14f * up;
            it->tape_a = 1.0f - sm01((f - 0.45f) / 0.55f);
        }
        if (u >= 40.0) { // the pin, pushed down from 34 px up (quicker and quicker) to its height in focus, where it stays
            const float w = u >= PA_IN_IMP ? 1.0f : (float)((u - 40.0) / (PA_IN_IMP - 40.0));
            it->pin_on = 1;
            it->pin_h = PIN_UP_H + (PIN_AIR_H - PIN_UP_H) * (1.0f - powf(w, 1.7f));
            it->pin_a = sm01((float)((u - 40.0) / 60.0));
            it->dent_a = 1.0f - sm01(it->pin_h / PIN_UP_H);
        }
        pa_cue(2u, u, PA_IN_IMP, SND_PIN_IN, 1.0f);
        return;
    }
    // PA_OUT
    if (u < 0) { // (the pinned card it was)
        it->pin_on = 1;
        it->pin_a = 1.0f;
        it->pin_h = PIN_UP_H * it->ps.p;
        it->dent_a = 1.0f - sm01(it->pin_h / PIN_UP_H);
        it->piv = 1.0f;
        *tilt = it->tilt_pin;
        return;
    }
    pa_pose(it, 1.0f); // (the card stays up: it is in focus)
    it->piv = 1.0f - sm01((float)(u / PA_OUT_IMP)); // (it comes off its pin)
    *tilt = it->tilt_pin + (it->tilt_tape - it->tilt_pin) * sm01((float)(u / PA_OUT_IMP));
    pa_cue(1u, u, 0.0, SND_PIN_OUT, 0.9f);
    if (u < 160.0) { // the pin is pulled up out of the card and away
        it->pin_on = 1;
        it->pin_h = PIN_UP_H + (PIN_AIR_H - PIN_UP_H) * eout3((float)(u / 160.0));
        it->pin_a = u < 60.0 ? 1.0f : 1.0f - sm01((float)((u - 60.0) / 100.0));
        it->dent_a = 0.0f;
    }
    it->tape_on = u >= PA_OUT_STRIP; // a new strip: down out of the air with its end curled onto the card as it is in focus, then stuck
    it->tape_p = 1.0f;
    it->tape_a = 1.0f;
    if (u >= PA_OUT_STRIP && u < PA_OUT_IMP) {
        const float v = (float)((u - PA_OUT_STRIP) / (PA_OUT_IMP - PA_OUT_STRIP)), k = 1.0f - ein2(v);
        it->tape_air = 1;
        it->tf_up = k;
        it->tf_dx = -22.0f * k;
        it->tf_dy = -26.0f * k;
        it->tf_da = -rad(12.0f) * k;
        it->tf_sc = 1.0f + 0.16f * k;
        it->tape_a = sm01((float)((u - PA_OUT_STRIP) / 50.0));
    }
    pa_cue(2u, u, PA_OUT_IMP, SND_STICK, 0.8f);
}

static void item_pose(HItem *it, double now, int lean_on, int lx, int ly, int *anim)
{
    pa_handover(it, now);
    it->lx = it->ly = 0;
    memset(&it->ps, 0, sizeof it->ps);
    it->ps.scale = 1.0f;
    it->moving = 0;
    if (it->lk >= 0) {
        paper_lift_pose(&it->lift, now, it->w, &it->ps);
        if (it->pn.on || pa_on(it)) it->ps.angle *= 0.6f; // (it swings on its pin, less than a card on tape wiggles)
        if (lean_on) {
            it->lx = (float)lx;
            it->ly = (float)ly;
        }
        int busy = paper_lift_busy(&it->lift, now) || lean_on || pin_busy(it, now);
        it->moving = it->ps.p > 0.002f || busy;
        if (busy && it->active) *anim = 1;
    }
    // a tape whose peeling states were made while its item was already up (the focus at the first home screen of a run, which the intro ends on) peels in
    // from flat, as if the item had just been picked up
    it->tape_k = 1;
    for (int i = 0; it->active && i < it->ntp; i++) {
        const TapeEnt *e = tape_ent(&it->tp[i].t);
        if (!e->peel || e->peel_at <= 0 || now - e->peel_at >= TAPE_IN_MS) continue;
        float u = now <= e->peel_at ? 0.0f : (float)((now - e->peel_at) / TAPE_IN_MS), k = 1.0f - (1.0f - u) * (1.0f - u) * (1.0f - u);
        if (k < it->tape_k) it->tape_k = k;
        if (it->active && it->ps.p > 0.002f) *anim = 1;
    }
    float tilt = it->tilt;
    hold_pose(it, now, &tilt, anim);
    it->alpha = 1.0f;
    const float a_before = it->ps.angle;
    if (it->shf_t0 > 0 && it->active) shf_pose(it, now, anim);
    const float a_shuf = it->ps.angle - a_before; // (how far a shuffle has it turned from its new rest: its pin stays where the card was shown)
    if (it->active && it->kind == IK_TILE && !it->sp.rest) it->moving = 1; // (its picture at rest is still to be made: drawn as lifted, lying flat)
    it->cx = it->x + it->w * 0.5f + it->ps.x + it->lx;
    it->cy = it->y + it->h * 0.5f + it->ps.y + it->ly;
    it->sc = it->ps.scale;
    it->ang = tilt + it->ps.angle;
    if (it->piv > 0.0f) { // a card on a pin turns and grows about the pin (which only rises with the card), not about its own middle
        const float ca = cosf(it->tilt_pin + a_shuf), sa = sinf(it->tilt_pin + a_shuf), cb = cosf(it->ang), sb = sinf(it->ang), dx = it->pn.dx, dy = it->pn.dy;
        const float px = it->x + it->w * 0.5f + dx * ca - dy * sa + it->ps.x + it->lx, py = it->y + it->h * 0.5f + dx * sa + dy * ca + it->ps.y + it->ly;
        it->cx += it->piv * (px - it->sc * (dx * cb - dy * sb) - it->cx);
        it->cy += it->piv * (py - it->sc * (dx * sb + dy * cb) - it->cy);
    }
}

// works out every item's pose for time `now` (ms); returns 1 while anything is still moving
static int frame_poses(HomeCtx *c, const UiHome *h, double now)
{
    (void)h;
    if (g_pa.kind && now > g_pa.t0 + 1500.0) pa_clear(); // (the person is not on the screen any more: nobody took the animation up)
    float lox, loy;
    int lean_on = bump_offset(&c->lean, &lox, &loy);
    int lx = (int)lroundf(lox), ly = (int)lroundf(loy), moving = lean_on;
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        item_pose(it, now, lean_on && i == c->lean_item, lx, ly, &moving);
    }
    HItem *ho = &c->it[IT_HERO], *sg = &c->it[IT_SIGN];
    if (sg->active) { // it rides on the loaded profile's card
        PaperXf hx = {ho->cx, ho->cy, ho->sc, ho->ang};
        PaperXf a = paper_xf_attach(hx, sg->x + sg->w * 0.5f - (ho->x + ho->w * 0.5f), sg->y + sg->h * 0.5f - (ho->y + ho->h * 0.5f));
        sg->cx = a.x + sg->ps.x + sg->lx;
        sg->cy = a.y + sg->ps.y + sg->ly;
        sg->sc = a.scale * sg->ps.scale;
        sg->ang = a.angle + sg->ps.angle;
        sg->moving = 1; // (always drawn turned to where the card is)
    }
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        for (int j = 0; j < it->ntp; j++) { // (here, on the app's own thread: a peeling state not made yet is made now)
            TapeEnt *e = tape_ent(&it->tp[j].t);
            // (carried in a shuffle the tape stays down: its peeling states would be made in the middle of the move, for every tile at once)
            it->tsp[j] = it->active && it->moving && !it->hold ? (e->peel && !(it->shf_t0 > 0 && e->stuck) ? paper_peel_at(e->peel, it->ps.p * it->tape_k) : e->stuck) : NULL;
        }
        it->hold_tape = NULL;
        if (it->active && it->tape_on) { // (the strip the pin animation holds: stuck, curled or in the air)
            TapeEnt *e = tape_ent(&it->tp[0].t);
            it->hold_tape = e->peel ? paper_peel_at(e->peel, it->tape_p) : e->stuck;
        }
        item_bounds(it);
    }
    return moving;
}

// ------------------------------------------------------------------------------------------------ painting

static int rect_hit(const Rect *a, const Rect *b)
{
    return a->w > 0 && b->w > 0 && a->x < b->x + b->w && b->x < a->x + a->w && a->y < b->y + b->h && b->y < a->y + a->h;
}

static void put(Surf *s, const PaperSpr *p, int x, int y, int ox, int oy)
{
    gfx_blit_over(s, p->px, p->w, p->h, x - ox, y - oy);
}

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
    const float al = it->alpha;
    if (al <= 0.004f) return;
    if (it->sh && (it->ps.p > 0.004f || it->kind != IK_SIGN)) {
        const PaperSpr *sp = it->sh->s[paper_shadow_level(it->sh, it->ps.p)];
        int ax, ay;
        shadow_pos(it, sp, &ax, &ay);
        if (it->kind == IK_SIGN) { // a button only casts a shadow once it is up
            float a = it->ps.p * 4.0f > 1.0f ? 1.0f : it->ps.p * 4.0f;
            paper_spr_xform(s, sp, (float)(ax + sp->w / 2 - ox), (float)(ay + sp->h / 2 - oy), 1.0f, 0.0f, a);
        } else if (al < 0.996f) {
            paper_spr_xform(s, sp, (float)(ax + sp->w / 2 - ox), (float)(ay + sp->h / 2 - oy), 1.0f, 0.0f, al);
        } else {
            put(s, sp, ax, ay, ox, oy);
        }
    }
    if (it->sp.body) paper_spr_xform(s, it->sp.body, it->cx - ox, it->cy - oy, it->sc, it->ang, al);
    PaperXf cf = {it->cx, it->cy, it->sc, it->ang};
    for (int i = 0; i < it->ntp; i++) {
        const PaperSpr *sp = it->tsp[i];
        if (!sp) continue;
        PaperXf t = paper_xf_attach(cf, it->tp[i].dx, it->tp[i].dy);
        paper_spr_xform(s, sp, t.x - ox, t.y - oy, t.scale, t.angle, al);
    }
    if (!it->hold) return;
    if (it->hold_tape && it->tape_a > 0.004f) { // the strip put on or taken off by the pin animation
        PaperXf t = paper_xf_attach(cf, it->tp[0].dx, it->tp[0].dy);
        const float x = t.x + it->tf_dx - ox, y = t.y + it->tf_dy - oy, sc = t.scale * it->tf_sc, an = t.angle + it->tf_da;
        if (it->tape_air && g_pa.tape_sh) // (its shadow falls further behind and softer the higher it is)
            paper_spr_xform(s, g_pa.tape_sh, x + 6.0f + 10.0f * it->tf_up, y + 10.0f + 26.0f * it->tf_up, sc, an, it->tape_a * al * (1.0f - 0.5f * it->tf_up));
        paper_spr_xform(s, it->hold_tape, x, y, sc, an, it->tape_a * al);
    }
    if (it->pin_on && it->pin_a > 0.004f) {
        PaperXf a = paper_xf_attach(cf, it->pn.dx, it->pn.dy);
        if (it->dent_a > 0.004f) pins_draw_dent(s, a.x - ox, a.y - oy, a.scale, it->dent_a * al);
        pins_draw_shadow(s, a.x - ox, a.y - oy, it->pin_h, a.scale, it->pin_a * al);
        pins_draw(s, a.x - ox, a.y - oy, it->pin_h, a.scale, it->pn.slot, it->pin_a * al);
    }
}

static void draw_ovl(Surf *s, int ox, int oy, const Rect *R, const Ovl *o)
{
    if (o->on && o->spr && rect_hit(&o->bb, R)) put(s, o->spr, o->x, o->y, ox, oy);
}

// while the grid scrolls, a tile that reaches past the grid's window is cut off at it: what is round the window under it is kept first
// (ui_put_back after drawing it); 0 = no room to keep that, so the tile is not drawn
static const float GW_Y0 = ROW_Y + ROW_H + 8, GW_Y1 = 660;
static int tile_clip(const HomeCtx *c, const HItem *it, const Surf *s, int ox, int oy, UiKeep *k)
{
    k->keep = NULL;
    if (!c->clip || it->kind != IK_TILE || (it->bb.y >= (int)GW_Y0 && it->bb.y + it->bb.h <= (int)GW_Y1)) return 1;
    return ui_keep_outside(k, s, it->bb.x - ox, it->bb.y - oy, it->bb.x + it->bb.w - ox, it->bb.y + it->bb.h - oy, (int)GW_Y0 - oy, (int)GW_Y1 - oy);
}

// the items that touch R, in order, drawn on `s` whose top left is (R.x, R.y) on the screen
static void paint_items(HomeCtx *c, Surf *s, const Rect *R)
{
    int ox = R->x, oy = R->y;
    HItem *ho = &c->it[IT_HERO], *sg = &c->it[IT_SIGN];
    // 1. everything lying still (the hero and its Sign out button belong together)
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        if (!it->active || it->moving || i == IT_SIGN || !rect_hit(&it->bb, R)) continue;
        UiKeep k;
        if (!tile_clip(c, it, s, ox, oy, &k)) continue;
        draw_resting(s, ox, oy, it);
        ui_put_back(&k, s);
    }
    // 2. the patches left where cards were lifted
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        UiKeep k;
        if (it->active && it->moving && it->ghost && rect_hit(&it->bb, R) && tile_clip(c, it, s, ox, oy, &k)) {
            draw_ghost(s, ox, oy, it);
            ui_put_back(&k, s);
        }
    }
    // 3. what is lifted or settling, the lowest first
    int order[IT_COUNT], n = 0;
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        if (!it->active || !it->moving) continue;
        if (i == IT_SIGN && ho->moving) continue; // (drawn right after its card)
        order[n++] = i;
    }
    for (int a = 1; a < n; a++) { // (insertion sort by lift, stable)
        int v = order[a], b = a;
        while (b > 0 && c->it[order[b - 1]].ps.p > c->it[v].ps.p) {
            order[b] = order[b - 1];
            b--;
        }
        order[b] = v;
    }
    for (int k = 0; k < n; k++) {
        HItem *it = &c->it[order[k]];
        if (order[k] == IT_HERO) {
            if (rect_hit(&it->bb, R)) draw_lifted(s, ox, oy, it);
            if (sg->active && rect_hit(&sg->bb, R)) draw_lifted(s, ox, oy, sg);
        } else if (rect_hit(&it->bb, R)) {
            UiKeep kp;
            if (!tile_clip(c, it, s, ox, oy, &kp)) continue;
            draw_lifted(s, ox, oy, it);
            ui_put_back(&kp, s);
        }
    }
    draw_ovl(s, ox, oy, R, &c->toast);
    draw_ovl(s, ox, oy, R, &c->dbg);
}

static Surf *scratch_for(int w, int h)
{
    static Surf scr;
    if (!scr.px) scr.px = malloc((size_t)SCREEN_W * SCREEN_H * 4);
    scr.w = w;
    scr.h = h;
    return &scr;
}

// A big rectangle is painted in bands, one on each of the console's three cores: the painting only reads the sprites (the tapes are picked
// beforehand, frame_poses) and each band writes only its own rows, so the picture is the same to the pixel as one painted in one go.
#define PAINT_BANDS 3
typedef struct {
    HomeCtx *c;
    Surf s;
    Rect r;
} PaintBand;

static void band_paint(void *arg)
{
    PaintBand *b = arg;
    paint_items(b->c, &b->s, &b->r);
}

// The two other cores' painters: started the first time they are wanted, then each sleeps until it is handed a band and says when it is done.
static struct {
    int state; // 0: not started, 1: ready, -1: couldn't be had (everything is painted here)
    JobThread th[PAINT_BANDS];
    JobSem go[PAINT_BANDS], done;
    PaintBand *band[PAINT_BANDS];
} g_crew;

static void crew_main(void *arg)
{
    int k = (int)(intptr_t)arg;
    for (;;) {
        job_sem_wait(&g_crew.go[k]);
        band_paint(g_crew.band[k]);
        job_sem_post(&g_crew.done);
    }
}

static int crew_ready(void)
{
    if (g_crew.state) return g_crew.state > 0;
    g_crew.state = -1;
    job_sem_init(&g_crew.done, 0);
    for (int k = 1; k < PAINT_BANDS; k++) {
        job_sem_init(&g_crew.go[k], 0);
        if (job_thread_start_on(&g_crew.th[k], crew_main, (void *)(intptr_t)k, k) != 0) return 0; // (cores 1 and 2; the app is on 0)
    }
    g_crew.state = 1;
    return 1;
}

// the rectangle R of `dst` made anew: the base with every item that touches it drawn over
static void paint_rect(HomeCtx *c, Surf *dst, const Rect *R)
{
    Surf *scr = scratch_for(R->w, R->h);
    if (!scr->px) return;
    for (int y = 0; y < R->h; y++)
        memcpy(scr->px + (size_t)y * R->w * 4, G.base->px + ((size_t)(R->y + y) * SCREEN_W + R->x) * 4, (size_t)R->w * 4);
    int nb = R->h >= 3 * 48 && R->w * R->h >= 120000 ? PAINT_BANDS : 1; // (a small one is quicker on one core than handed out)
    if (nb == 1) {
        paint_items(c, scr, R);
    } else {
        PaintBand b[PAINT_BANDS];
        const int crew = crew_ready();
        for (int k = 0; k < nb; k++) {
            int y0 = R->h * k / nb, y1 = R->h * (k + 1) / nb;
            b[k].c = c;
            b[k].s = (Surf){R->w, y1 - y0, scr->px + (size_t)y0 * R->w * 4};
            b[k].r = (Rect){R->x, R->y + y0, R->w, y1 - y0};
            if (k > 0 && crew) {
                g_crew.band[k] = &b[k];
                job_sem_post(&g_crew.go[k]);
            }
        }
        band_paint(&b[0]);
        for (int k = 1; k < nb; k++) {
            if (crew) job_sem_wait(&g_crew.done);
            else band_paint(&b[k]); // (no painters to be had: painted here)
        }
    }
    for (int y = 0; y < R->h; y++)
        memcpy(dst->px + ((size_t)(R->y + y) * SCREEN_W + R->x) * 4, scr->px + (size_t)y * R->w * 4, (size_t)R->w * 4);
}

// ---- the overlays

static void ovl_set(Ovl *o, PaperSpr *spr, int x, int y)
{
    ovl_free(o);
    o->spr = spr;
    o->x = x;
    o->y = y;
    o->on = spr != NULL;
    if (spr) {
        o->bb.x = x;
        o->bb.y = y;
        o->bb.w = spr->w;
        o->bb.h = spr->h;
    } else {
        memset(&o->bb, 0, sizeof o->bb);
    }
    o->key++;
}

static void toast_sync(HomeCtx *c, const char *msg, int kind)
{
    if (!msg || !msg[0]) {
        if (c->toast.on) ovl_set(&c->toast, NULL, 0, 0);
        c->toast_text[0] = 0;
        return;
    }
    if (c->toast.on && !strcmp(c->toast_text, msg) && c->toast_kind == kind) return;
    snprintf(c->toast_text, sizeof c->toast_text, "%s", msg);
    c->toast_kind = kind;
    ToastBake tb = {c->toast_text, kind, text_width(G.skin.semi, 20, 0, msg) + 54 + 28};
    PaperSpr *spr = paper_bake((int)tb.w + 40, 48 + 40, draw_toast, &tb);
    ovl_set(&c->toast, spr, (SCREEN_W - (int)tb.w) / 2 - 20, 20 - 20 + 4);
}

static void dbg_flush(HomeCtx *c)
{
    char line[96];
    snprintf(line, sizeof line, "draw %.1f/%d ms   present %.1f/%d ms   %d frames", (double)c->dbg_draw / c->dbg_n, (int)c->dbg_draw_max,
             (double)c->dbg_pres / c->dbg_n, (int)c->dbg_pres_max, c->dbg_n);
    PaperSpr *spr = paper_bake((int)text_width(G.skin.semi, 15, 0, line) + 12, 24, draw_dbg, line);
    ovl_set(&c->dbg, spr, 330, 4);
    c->dbg_draw = c->dbg_pres = c->dbg_draw_max = c->dbg_pres_max = 0;
    c->dbg_n = 0;
}

static void dbg_count(HomeCtx *c, uint64_t draw_ms, uint64_t pres_ms)
{
    if (!c->dbg_on) return;
    c->dbg_draw += draw_ms;
    c->dbg_pres += pres_ms;
    if (draw_ms > c->dbg_draw_max) c->dbg_draw_max = draw_ms;
    if (pres_ms > c->dbg_pres_max) c->dbg_pres_max = pres_ms;
    if (++c->dbg_n >= 120) dbg_flush(c);
}

// ---- what changed this frame

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
    k[10] = it->tape_k;
    k[11] = it->alpha;
    k[12] = it->pin_h;
    k[13] = it->pin_a;
    k[14] = it->dent_a;
    k[15] = (float)it->tape_on + 2.0f * (float)it->tape_air;
    k[16] = it->tape_p;
    k[17] = it->tf_up;
    k[18] = it->tape_a;
    k[19] = it->tf_sc;
    k[20] = (float)it->pin_on;
    k[21] = it->piv;
    k[22] = it->tf_da;
    k[23] = (float)it->hold;
}

static void collect_dirty(HomeCtx *c, Dirty *d)
{
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        pose_key(it, it->pk);
        int changed = it->force || !!it->active != !!it->drawn || (it->active && memcmp(it->pk, it->pk_prev, sizeof it->pk));
        if (!changed) continue;
        if (it->drawn) dirty_add_rect(d, it->bb_prev);
        if (it->active) dirty_add_rect(d, it->bb);
    }
    Ovl *os[2] = {&c->toast, &c->dbg};
    for (int i = 0; i < 2; i++) {
        Ovl *o = os[i];
        if (o->on == o->drawn && o->key == o->key_prev) continue;
        if (o->drawn) dirty_add_rect(d, o->bb_prev);
        if (o->on) dirty_add_rect(d, o->bb);
    }
}

static void commit_frame(HomeCtx *c)
{
    for (int i = 0; i < IT_COUNT; i++) {
        HItem *it = &c->it[i];
        memcpy(it->pk_prev, it->pk, sizeof it->pk);
        it->bb_prev = it->bb;
        it->drawn = it->active;
        it->force = 0;
    }
    Ovl *os[2] = {&c->toast, &c->dbg};
    for (int i = 0; i < 2; i++) {
        os[i]->key_prev = os[i]->key;
        os[i]->bb_prev = os[i]->bb;
        os[i]->drawn = os[i]->on;
    }
}

// the whole screen made from scratch and compared with what the rectangles made (tests)
static void oracle_check(HomeCtx *c)
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
static int home_frame(HomeCtx *c, const UiHome *h, double now, const char *toast, int toast_kind)
{
    uint64_t t0 = c->dbg_on ? ui_now() : 0; // (the clock is read for the timing readout only)
    int rail_moving = rail_tick(now);
    if (rail_moving) rail_setup(c, &c->it[IT_RAIL], h);
    g_ui_home_rail_cur = h->rail ? g_rail.cur : -1;
    g_ui_home_scroll = (float)h->top_row + g_row_off / TPY;
    g_ui_home_focus = h->focus;
    for (int i = 0; i < 3; i++) snprintf(g_ui_home_names[i], sizeof g_ui_home_names[i], "%s", i < h->n ? h->people[i].name : "");
    int moving = frame_poses(c, h, now) | g_rail.on;
    g_ui_home_lift = c->cur >= 0 ? paper_lift_value(&c->it[c->cur].lift, now) : -1.0f;
    g_ui_home_frames++;
    g_ui_home_pa_on = g_pa.kind;
    g_ui_home_tape_air = 0;
    for (int i = 0; i < IT_COUNT; i++) g_ui_home_tape_air += c->it[i].active && c->it[i].tape_air;
    g_ui_home_pin_h = -1.0f;
    for (int i = 0; i < IT_COUNT; i++) { // (the pin of the card with the focus, or else of any card that has one in the air)
        const HItem *it = &c->it[i];
        if (!it->active || !it->pin_on || (i != c->cur && g_ui_home_pin_h >= 0.0f)) continue;
        PaperXf a = paper_xf_attach((PaperXf){it->cx, it->cy, it->sc, it->ang}, it->pn.dx, it->pn.dy);
        g_ui_home_pin_h = it->pin_h;
        g_ui_home_pin_x = a.x;
        g_ui_home_pin_y = a.y;
    }
    if (g_ui_home_lift >= 0.0f && g_ui_home_lift < g_ui_home_lift_low) g_ui_home_lift_low = g_ui_home_lift;
    toast_sync(c, toast, toast_kind);
    int clip = g_row_off != 0 || mo_moving(&g_gm) || g_gm.mode == MO_HELD;
    if (clip != c->clip) { // (cut off or not: every tile at the window's edges is drawn again)
        c->clip = clip;
        c->force_full = 1;
    }
    Dirty d;
    dirty_clear(&d);
    if (c->force_full) dirty_all(&d);
    else collect_dirty(c, &d);
    if (d.full || d.n) {
        Rect full = {0, 0, SCREEN_W, SCREEN_H};
        if (d.full) {
            paint_rect(c, G.work, &full);
        } else {
            for (int i = 0; i < d.n; i++) paint_rect(c, G.work, &d.r[i]);
        }
        uint64_t t1 = c->dbg_on ? ui_now() : 0;
        if (d.full) ui_present_rects(G.work, &full, 1);
        else ui_present_rects(G.work, d.r, d.n);
        if (c->dbg_on) dbg_count(c, t1 - t0, ui_now() - t1);
        if (g_home_oracle) oracle_check(c);
    }
    commit_frame(c);
    c->force_full = 0;
    return moving;
}

// ------------------------------------------------------------------------------------------------ the part that never moves

static void chevron_v(Cx *c, float cx, float cy, int down)
{
    float d = down ? 1.0f : -1.0f;
    Col k = hex(PAL(board_thread, PAPER_THREAD), 1); // (on the board: the theme's thread colour)
    gfx_line(c->s, cx - 9, cy - 4 * d, cx, cy + 4 * d, 2.6f, k);
    gfx_line(c->s, cx, cy + 4 * d, cx + 9, cy - 4 * d, 2.6f, k);
}

static void render_base(const UiHome *h)
{
    Cx c = ui_cx(G.base);
    skin_board(&c);
    SkinStatus st;
    ui_status(&st);
    skin_top_bar(&c, NULL, &st);
    int rows = (h->n + 1 + COLS - 1) / COLS;
    if (h->top_row > 0) chevron_v(&c, 640, 297, 0); // more above / below the two rows showing
    if (h->top_row + VROWS < rows) chevron_v(&c, 640, 653, 1);
    if (h->query[0]) skin_footer(&c, PROMPTS_SEARCH, 2);
    else skin_footer(&c, PROMPTS, 2);
}

// ---- idle-time baking: the tape's peeling states, and the rows next to the ones showing

static int peel_wanted(HomeCtx *c, const UiHome *h, int pass, TapeEnt **out)
{
    (void)h;
    int order[IT_COUNT], n = 0;
    if (c->cur >= 0) order[n++] = c->cur;
    for (int i = 0; i < IT_COUNT; i++)
        if (i != c->cur && i != IT_SIGN && i != IT_RAIL) order[n++] = i;
    for (int k = 0; k < n; k++) {
        HItem *it = &c->it[order[k]];
        if (!it->active) continue;
        for (int j = 0; j < it->ntp; j++) {
            TapeEnt *e = tape_ent(&it->tp[j].t);
            if (!e->peel && !e->tried) {
                if (pass) *out = e;
                return 1;
            }
        }
    }
    return 0;
}

static int prefetch_wanted(HomeCtx *c, const UiHome *h, int pass)
{
    (void)c;
    for (int dr = -1; dr <= VROWS + 1; dr++) { // (the row above, and the two below: one of them comes in as the grid scrolls)
        if (dr >= 0 && dr < VROWS) continue;
        int row = h->top_row + dr;
        for (int col = 0; col < COLS; col++) {
            int idx = row * COLS + col;
            if (row < 0 || idx > h->n) continue;
            const UiPerson *p = idx == h->n ? NULL : &h->people[idx];
            int loaded = idx != h->n && idx == h->loaded;
            TileCache *e = &g_tc[idx % (int)(sizeof g_tc / sizeof g_tc[0])];
            if (e->sp.body && e->idx == idx && e->key == tile_key(idx, p, loaded)) continue;
            if (pass) tile_build(e, h, idx, p, loaded, idx == h->n, 0);
            return 1;
        }
    }
    return 0;
}

// 1 if something is left to bake; with `do_one` it bakes one piece first
static int bake_work(HomeCtx *c, const UiHome *h, int do_one)
{
    if (rest_wanted(c, h, do_one)) return 1;
    TapeEnt *e = NULL;
    if (peel_wanted(c, h, 1, &e) && e) {
        if (do_one) {
            e->tried = 1;
            e->peel = paper_tape_peel_lazy(&e->t, PAPER_PEEL_STATES);
            e->peel_at = (double)ui_now();
            for (int i = 0; i < IT_COUNT; i++) // whatever wears this tape draws again with its peeling states
                for (int j = 0; j < c->it[i].ntp; j++)
                    if (tape_eq(&c->it[i].tp[j].t, &e->t)) c->it[i].force = 1;
            return 1;
        }
        return 1;
    }
    if (prefetch_wanted(c, h, do_one)) return 1;
    return 0;
}

// ------------------------------------------------------------------------------------------------ input

// the last row the grid can start with (rows from 0)
static int grid_most(const UiHome *h)
{
    int rows = (h->n + 1 + COLS - 1) / COLS;
    return rows > VROWS ? rows - VROWS : 0;
}

// the grid at rest where the selection keeps it (a press, a new visit): the mover is there too
static void grid_rest_at(const UiHome *h)
{
    mo_snap(&g_gm, (float)h->top_row, 1.0f / TPY);
    g_row_off = 0;
    g_gdrag = 0;
}

static int grid_scrolling(void) { return g_gm.mode == MO_HELD || mo_moving(&g_gm); }

// what has the focus lies down (a finger has taken the grid): it sticks back (now > 0), or is down at once (0); the focus peels up on the grid once it is at rest
static void grid_lay_down(HomeCtx *c, double now)
{
    if (c->cur < 0) return;
    HItem *it = &c->it[c->cur];
    if (now > 0) {
        paper_lift_release(&it->lift, now, 0);
        pin_go(it, now, 0);
    } else {
        paper_lift_init(&it->lift, it->lk >= 0 ? it->lk : LIFT_BTN);
        it->lift.tapeless = lift_kind_of(it);
        pin_set(it, 0);
    }
    c->cur = -1;
}

// The grid's place from the mover: the first row showing and how far the rows have moved past it. When the first row changes every
// tile is another person, so the screen is set up again, the selection kept among the rows showing (as a scroll by a press does).
static void grid_moved(HomeCtx *c, UiHome *h, double now)
{
    int most = grid_most(h), top = (int)floorf(g_gm.x);
    if (top < 0) top = 0;
    if (top > most) top = most;
    float off = roundf((g_gm.x - (float)top) * TPY);
    if (top == h->top_row && off == g_row_off) return;
    const int old_top = h->top_row, old_sel = h->sel;
    h->top_row = top;
    g_row_off = off;
    if (top == old_top) {
        layout(c, h);
        return;
    }
    int row = h->sel / COLS;
    if (!g_gfollow) row = top; // (a press chose it: the grid is on its way to it)
    if (row < top) h->sel = top * COLS;
    else if (row >= top + VROWS) h->sel = (top + VROWS) * COLS - 1 > h->n ? h->n : (top + VROWS) * COLS - 1;
    rail_follow(h->rail ? rail_cur(h) : -1);
    rail_aim(h->rail ? rail_cur(h) : -1, now);
    render_base(h);
    layout(c, h);
    if (grid_scrolling() || g_gstep) { // (all lying down while it moves: the focus peels up again once it is at rest)
        lifts_reset(c, h);
        grid_lay_down(c, 0);
    } else if (h->sel == old_sel || h->focus != Z_GRID) {
        lifts_reset(c, h);
    } else { // (the focus kept on the rows showing moved to another tile: the old one, if it is still on the screen, sticks back)
        int os = old_sel - top * COLS;
        lifts_rescroll(c, h, os >= 0 && os < NSLOT && c->it[IT_TILE0 + os].active ? IT_TILE0 + os : -1, now, 0);
    }
    c->force_full = 1;
}

// the grid running on after a throw: one step; 1 while it still moves
static int grid_step(HomeCtx *c, UiHome *h, uint64_t now)
{
    if (!mo_moving(&g_gm)) return 0;
    float dt = now > g_glast ? (float)(now - g_glast) / 1000.0f : 0.0f;
    g_glast = now;
    if (dt > 0.05f) dt = 0.05f; // (a slow frame never leaps)
    int on = mo_step(&g_gm, dt);
    g_gstep = 1;
    grid_moved(c, h, (double)now);
    g_gstep = 0;
    if (!on) focus_sync(c, h, (double)now, 0); // (at rest on its row: the focus peels up)
    return on;
}

// The selection moved by a press (or a letter of the rail): the grid springs to the rows that show it, from where it is at the speed it has (the
// focus lies down on the way and peels up where it lands). 1 while the grid is on its way.
static int grid_aim(HomeCtx *c, UiHome *h, double now)
{
    const int moving = grid_scrolling(), most = grid_most(h), row = h->sel / COLS;
    int base = moving ? (int)lroundf(g_gm.tgt) : h->top_row, to = base;
    if (row < to) to = row;
    if (row >= to + VROWS) to = row - VROWS + 1;
    if (to > most) to = most;
    if (to < 0) to = 0;
    g_gfollow = 0;
    if (to == base) return moving;
    if (!moving) mo_snap(&g_gm, (float)h->top_row + g_row_off / TPY, 1.0f / TPY);
    g_glast = (uint64_t)now;
    grid_lay_down(c, now);
    mo_spring(&g_gm, (float)to, MO_PRESS_OM, MO_PRESS_ZETA);
    return 1;
}

// a press while the grid runs or a finger holds it: it is at once where it was going (the nearest row, for a hold)
static void grid_settle(HomeCtx *c, UiHome *h)
{
    if (!mo_moving(&g_gm) && g_gm.mode != MO_HELD && g_row_off == 0) return;
    int most = grid_most(h);
    float to = g_gm.mode == MO_HELD ? roundf(g_gm.x) : g_gm.tgt;
    if (to < 0) to = 0;
    if (to > (float)most) to = (float)most;
    mo_snap(&g_gm, to, 1.0f / TPY);
    g_gdrag = 0;
    grid_moved(c, h, (double)ui_now());
    focus_sync(c, h, (double)ui_now(), 0);
}

// keep the selection on screen; returns 1 if the grid scrolled
static int fit_scroll(UiHome *h)
{
    int rows = (h->n + 1 + COLS - 1) / COLS, row = h->sel / COLS, old = h->top_row;
    if (row < h->top_row) h->top_row = row;
    if (row >= h->top_row + VROWS) h->top_row = row - VROWS + 1;
    if (h->top_row > rows - VROWS) h->top_row = rows - VROWS;
    if (h->top_row < 0) h->top_row = 0;
    return h->top_row != old;
}

static int jump_letter(const UiHome *h, int dir)
{
    if (h->n == 0) return h->sel;
    int i = h->sel < h->n ? h->sel : h->n - 1;
    int cur = letter_of(h->people[i].name);
    if (dir > 0) {
        int j = i;
        while (j < h->n - 1 && letter_of(h->people[j].name) == cur) j++;
        return j;
    }
    int j = i;
    while (j > 0 && letter_of(h->people[j].name) == cur) j--;
    int prev = letter_of(h->people[j].name);
    while (j > 0 && letter_of(h->people[j - 1].name) == prev) j--;
    return j;
}

static int sort_hit(const UiHome *h, float px, float py)
{
    float x, y, w, hh;
    sort_rect(h, &x, &y, &w, &hh);
    return in_rect(px, py, x - 6, y - 6, w + 12, hh + 12);
}

// Touch on the home screen: a tap on a tile loads it (the New profile tile starts a new one), on the Sign out button signs out, on a
// letter of the rail jumps to that letter; a drag up or down moves the rows with the finger, and letting go throws them (g_gm). Returns a
// UiHomeEvent or -1; *rescroll = 1 if the selection jumped (the rail), *rail = 1 if only the rail's window moved, *dragged = 1 if the
// finger moved the grid or let it go.
static int home_touch(UiHome *h, int *rescroll, int *rail, int *dragged)
{
    UiTouchEvt e;
    int ev = -1;
    float sx, sy, sw, sh, qx, qy, qw, qh;
    signout_rect(&sx, &sy, &sw, &sh);
    search_rect(h, &qx, &qy, &qw, &qh);
    float cbx, cby, cbw, cbh;
    search_clear_rect(h, &cbx, &cby, &cbw, &cbh);
    const int most = grid_most(h);
    while (ev < 0 && ui_touch_next(&e)) {
        uint64_t t = ui_now();
        if (e.kind == TOUCH_DOWN) { // a finger landing on the grid as it runs stops it where it is (and that touch then chooses nothing)
            track_start(&g_gt, t, e.y);
            g_gfollow = 1;
            g_gstopped = 0;
            if (mo_moving(&g_gm) && (fabsf(g_gm.x - g_gm.tgt) > 0.05f || fabsf(g_gm.v) > 0.6f)) {
                mo_hold(&g_gm);
                g_gstopped = 1;
            }
        } else if (e.kind == TOUCH_DRAG) { // the rows follow the finger, giving less and less past an end
            float dy = e.dy;
            if (!g_gdrag) {
                g_gdrag = 1;
                mo_hold(&g_gm);
                g_graw = mo_unband(g_gm.x, 0, (float)most, GRID_ROOM);
                float len = hypotf(e.dx, e.dy), k = len > GRID_GRAB ? 1.0f - GRID_GRAB / len : 0.0f; // (the way it took to tell a drag from a tap)
                dy *= k;
            }
            g_graw -= dy / TPY;
            g_gm.x = mo_band(g_graw, 0, (float)most, GRID_ROOM);
            track_add(&g_gt, t, e.y);
            *dragged = 1;
        } else if (e.kind == TOUCH_END) { // let go: it runs on at the finger's speed and comes to rest on a row
            if (g_gdrag) {
                mo_throw(&g_gm, -track_speed(&g_gt, t) / TPY * mo_band_rate(g_graw, 0, (float)most, GRID_ROOM), &GRID_THROW, 0, (float)most);
                g_glast = t;
                g_gdrag = 0;
                *dragged = 1;
            } else if (g_gm.mode == MO_HELD) {
                mo_throw(&g_gm, 0, &GRID_THROW, 0, (float)most);
                g_glast = t;
            }
        } else if (e.kind == TOUCH_TAP) {
            int had_stopped = g_gstopped;
            g_gstopped = 0;
            if (g_gm.mode == MO_HELD) { // (it had only stopped the grid: that settles on the nearest row)
                mo_throw(&g_gm, 0, &GRID_THROW, 0, (float)most);
                g_glast = t;
                *dragged = 1;
            }
            if (had_stopped) continue;
            if (in_rect(e.x, e.y, 30, 10, 360, 90)) { // the mark and name in the corner
                ev = HOME_ABOUT;
            } else if (h->query[0] && in_rect(e.x, e.y, cbx, cby, cbw, cbh)) { // the search bar's clear button
                ev = HOME_CLEAR;
            } else if (in_rect(e.x, e.y, qx, qy, qw, qh)) { // the search bar
                ev = HOME_SEARCH;
                h->focus = Z_SEARCH;
            } else if (sort_hit(h, e.x, e.y)) { // the order chip
                ev = HOME_SORT;
                h->focus = Z_CHIP;
            } else if (h->card_on && in_rect(e.x, e.y, sx, sy, sw, sh)) {
                ev = HOME_DONE;
                h->focus = Z_SIGNOUT;
            } else if (in_rect(e.x, e.y, HERO_X, HERO_Y, HERO_W, HERO_H)) { // the loaded profile (or the guest)
                ev = HOME_BANNER;
                h->focus = Z_HERO;
            } else if (h->rail && in_rect(e.x, e.y, RAIL_X, RAIL_Y, RAIL_W, RAIL_H)) { // the letter rail
                if (e.y < RAIL_Y + RAIL_LIST_Y) { // its arrows move the window of letters
                    if (g_az > 0) g_az--;
                    *rail = 1;
                } else if (e.y >= RAIL_Y + RAIL_H - RAIL_LIST_Y) {
                    if (g_az < 26 - RAIL_ROWS) g_az++;
                    *rail = 1;
                } else {
                    int letter = g_az + (int)((e.y - (RAIL_Y + RAIL_LIST_Y)) / RAIL_ROW_H), pick = -1;
                    if (letter < 0) letter = 0;
                    for (int i = 0; i < h->n && pick < 0; i++)
                        if (letter_of(h->people[i].name) >= letter) pick = i;
                    if (pick < 0 && h->n > 0) pick = h->n - 1;
                    if (pick >= 0) {
                        h->sel = pick;
                        *rescroll = 1;
                    }
                }
            } else {
                for (int slot = 0; slot < NSLOT; slot++) {
                    int idx = h->top_row * COLS + slot;
                    if (idx > h->n) break;
                    float x, y;
                    tile_xy(h, idx, &x, &y);
                    if (in_rect(e.x, e.y, x, y, TW, TH)) {
                        h->sel = idx;
                        h->focus = Z_GRID;
                        ev = idx == h->n ? HOME_NEW : HOME_CHOOSE;
                        break;
                    }
                }
            }
        }
    }
    return ev;
}

// ---- the chip's menu: a glassine sleeve slides out from under the chip with a sticker in it for each of the other orders; the focus slides
// one out of its mouth (Up and Down: the next or the one before), A peels it off its liner and sticks it on the chip (ui_stickfx.c).

int g_ui_sort_anim = SFX_SLEEVE; // (which of ui_stickfx.c's ways: the sleeve, the user's pick)

// The order on the chip at the end, or -1; -2 if there was no memory for it.
static int sticker_menu(int v, const char *const *names, int n, int sort_mode, float chip_x, float chip_y, float chip_w, float chip_h,
                        int chip_lifted)
{
    static StickFx g_sf;
    StickFx *f = &g_sf;
    if (!sfx_init(f, v, n, names, sort_mode, chip_x, chip_y, chip_w, chip_h, chip_lifted)) return -2;
    const Rect R = sfx_region(f);
    surf_copy(G.dim, G.work); // what is under it
    uint64_t prev = ui_now();
    int quit = 0;
    for (;;) {
        uint64_t now = ui_now();
        sfx_step(f, (double)(now - prev) / 1000.0);
        prev = now;
        for (int y = 0; y < R.h; y++)
            memcpy(G.work->px + ((size_t)(R.y + y) * SCREEN_W + R.x) * 4, G.dim->px + ((size_t)(R.y + y) * SCREEN_W + R.x) * 4, (size_t)R.w * 4);
        sfx_draw(f, G.work, R, G.dim);
        ui_present_rects(G.work, &R, 1);
        if (f->finished) break;
        uint64_t b = ui_poll(sfx_moving(f) ? 0 : -1);
        if (b == UI_QUIT) {
            quit = 1;
            break;
        }
        if (b & UI_REDRAW) b = BTN_B; // (the theme changed under it)
        if (b & UI_TOUCH) { // (a tap: on the chip, R3; anywhere else, B)
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind != TOUCH_TAP) continue;
                b |= in_rect(te.x, te.y, chip_x, chip_y, chip_w, chip_h) ? BTN_R3 : BTN_B;
            }
        }
        if (f->closing) continue;
        if (b & BTN_R3) {
            sfx_close(f, -1);
        } else if (b & BTN_A) {
            sfx_close(f, f->focus);
        } else if (b & BTN_B) {
            sfx_close(f, -1);
        } else if (b & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) {
            int dx = (b & BTN_LEFT) ? -1 : (b & BTN_RIGHT) ? 1 : 0, dy = (b & BTN_UP) ? -1 : (b & BTN_DOWN) ? 1 : 0;
            if (!sfx_nav(f, dx, dy)) ui_sound(SND_REFUSE, 0.25f, 0);
        }
    }
    int pick = quit ? -1 : sfx_result(f);
    sfx_free(f);
    return pick;
}

// Opens the menu over the screen as it is (G.work) and returns the order picked, or -1 (B, a tap outside it, R3 again, or no memory for it).
int ui_sort_menu(const char *const *names, int nnames, int sort_mode, float chip_x, float chip_y, float chip_w, float chip_h, int chip_lifted)
{
    if (!names || nnames <= 0) return -1;
    int n = nnames > SFX_MAX ? SFX_MAX : nnames;
    int p = sticker_menu(g_ui_sort_anim, names, n, sort_mode, chip_x, chip_y, chip_w, chip_h, chip_lifted);
    return p == -2 ? -1 : p;
}

static int sort_menu(const UiHome *h)
{
    float cx, cy, cw, ch;
    sort_rect(h, &cx, &cy, &cw, &ch);
    return ui_sort_menu(h->sort_names, h->nsorts, h->sort_mode, cx, cy, cw, ch, h->focus == Z_CHIP);
}

// ------------------------------------------------------------------------------------------------ the screen

static void debug_timing_probe(void)
{
    if (g_ui_debug_timing >= 0) return;
    FILE *f = fopen("sdmc:/switch/washi/debug-timing", "rb");
    g_ui_debug_timing = f != NULL;
    if (f) fclose(f);
}

// the screen as it should be shown now (nothing in flight, no toast), whole
static void home_settle(HomeCtx *c, UiHome *h, int toast)
{
    layout(c, h);
    lifts_reset(c, h);
    c->force_full = 1;
    home_frame(c, h, (double)ui_now(), toast ? h->toast : NULL, h->toast_kind);
}

// The focused item is up from the first frame, so its tape is made peeled now, not in idle time after (when it would peel up late, on a card already in the
// air: the intro hands over to this very frame).
static void focus_peels(HomeCtx *c)
{
    if (c->cur < 0) return;
    HItem *it = &c->it[c->cur];
    for (int j = 0; it->active && j < it->ntp; j++) {
        TapeEnt *e = tape_ent(&it->tp[j].t);
        if (e->peel || e->tried) continue;
        e->tried = 1;
        e->peel = paper_tape_peel_lazy(&e->t, PAPER_PEEL_STATES);
        e->peel_at = 0; // (shown peeled from the start)
    }
}

// The palette changed since the home screen's pictures were made (the theme did): every one of them goes, to be made again in the new colours by the
// home_begin that must follow (the items still point at them until then). 1 if it did.
static int home_pal_check(HomeCtx *c)
{
    static unsigned serial = 1; // (the palette the app starts with: nothing made yet to throw away)
    if (!paper_pal_stale(&serial)) return 0;
    for (int k = 0; k < SC_COUNT; k++) {
        paper_shadows_free(g_shc[k].sh);
        memset(&g_shc[k], 0, sizeof g_shc[k]);
    }
    for (int i = 0; i < TAPE_CACHE; i++) {
        if (g_tp[i].used) {
            paper_spr_free(g_tp[i].stuck);
            paper_peel_free(g_tp[i].peel);
        }
        memset(&g_tp[i], 0, sizeof g_tp[i]);
    }
    for (int i = 0; i < (int)(sizeof g_tc / sizeof g_tc[0]); i++) {
        sprs_free(&g_tc[i].sp);
        memset(&g_tc[i], 0, sizeof g_tc[i]);
    }
    for (int i = 0; i < IT_COUNT; i++) { // (a tile's pictures were the tile cache's, the others the item's own)
        HItem *it = &c->it[i];
        if (it->own) sprs_free(&it->sp);
        memset(&it->sp, 0, sizeof it->sp);
        it->key = 0;
        it->sh = NULL;
        it->ghost_spr = NULL;
    }
    paper_spr_free(g_stamp.small);
    paper_spr_free(g_stamp.tile_ink);
    g_stamp.small = g_stamp.tile_ink = NULL;
    if (g_stamp.who[0]) stamp_sprites(); // (the same stamp, inked in the new colour; ink already on the board stays as it dried)
    ovl_set(&c->toast, NULL, 0, 0);
    c->toast_text[0] = 0;
    return 1;
}

// Brings the state up to date with the list (a new visit, or after the grid scrolled). Does not draw.
static void home_begin(HomeCtx *c, UiHome *h)
{
    home_pal_check(c);
    debug_timing_probe();
    c->dbg_on = g_ui_debug_timing > 0;
    if (!c->dbg_on && c->dbg.on) ovl_set(&c->dbg, NULL, 0, 0);
    c->dbg_n = 0;
    c->dbg_draw = c->dbg_pres = c->dbg_draw_max = c->dbg_pres_max = 0;
    pa_clear();
    rail_follow(h->rail ? rail_cur(h) : -1);
    rail_snap(h->rail ? rail_cur(h) : -1);
    render_base(h);
    shuf_clear(c);
    g_rest_later = g_was.armed;
    layout(c, h);
    g_rest_later = 0;
    lifts_reset(c, h);
    focus_peels(c);
    c->force_full = 1;
    c->toast.drawn = c->dbg.drawn = 0;
    for (int i = 0; i < IT_COUNT; i++) c->it[i].drawn = 0;
}

// The stamp coming down on the person's own tile, over the home screen as it is (nothing on it moves): from 320 ms it shrinks from big to its
// size, landing at 470 ms with its sound a moment before, and then the ink is on the paper and on the board. Anything pressed meanwhile is dropped.
static void stamp_play(HomeCtx *c, UiHome *h)
{
    Surf *clean = surf_clone(G.work);
    if (!clean) {
        g_stamp.pending = 0;
        home_begin(c, h);
        return;
    }
    float tcx = 0, tcy = 0, tang = 0;
    if (h->loaded >= 0 && h->loaded < h->n) stamp_screen_pos(h, h->loaded, &tcx, &tcy, &tang);
    uint64_t t0 = ui_now();
    int quit = 0, hit = 0;
    for (;;) {
        uint64_t t = ui_now() - t0;
        if (t >= 470) break;
        surf_copy(G.work, clean);
        if (t >= 430 && !hit) {
            hit = 1;
            ui_sound(SND_STAMP, 1.0f, 0); // it lands
        }
        if (t >= 320 && tcx > 0) {
            float p = (float)(t - 320) / 150.0f, e = 1.0f - (1.0f - (p > 1 ? 1 : p)) * (1.0f - (p > 1 ? 1 : p)) * (1.0f - (p > 1 ? 1 : p));
            paper_spr_xform(G.work, g_stamp.small, tcx, tcy, 2.3f - 1.3f * e, tang + rad(-10.0f + 10.0f * e), e);
        }
        ui_present(G.work);
        if (ui_poll(0) == UI_QUIT) {
            quit = 1;
            break;
        }
    }
    surf_free(clean);
    g_stamp.pending = 0;
    stamp_board_ink(h);
    if (!quit) home_begin(c, h);
}

// The pin animation for the person the pin was just put in / pulled out (h->pin_anim +1 / -1): the tile shows the other state until the shuffle starts to
// carry it to its new place, then it plays along with the carry (see g_pa). Nothing plays if the pins are not there (the card just has no tape) or the person is not on the screen.
long g_ui_home_frames; // tests: how many frames the home screen has composed (a film of Home keeps only those)
int g_ui_home_tape_air; // tests: how many cards had a strip in flight (put on or taken off by the pin animation) in the last frame composed
int g_ui_home_pa_on;   // tests: the animation playing in the last frame composed (PA_IN, PA_OUT, 0)
int g_ui_home_pa_dir;  // tests: the direction (+1 in, -1 out) of the animation the last visit began, 0 none
// before the shuffle is worked out: the person the animation is for is carried to their new place as the card they were (a taped card for a pin going in,
// a pinned one for a pin coming out), so that a change of tilt alone does not count as a move
static void pin_anim_tilt(HomeCtx *c, const UiHome *h)
{
    if (!h->pin_anim || h->sel < 0 || h->sel >= h->n || !pins_ready()) return;
    const int sl = h->sel - h->top_row * COLS;
    if (sl < 0 || sl >= NSLOT) return;
    HItem *it = &c->it[IT_TILE0 + sl];
    if (it->active && (h->pin_anim > 0) == (it->pn.on != 0)) it->tilt = h->pin_anim > 0 ? it->tilt_tape : it->tilt_pin;
}

static void pin_anim_begin(HomeCtx *c, UiHome *h)
{
    const int dir = h->pin_anim;
    h->pin_anim = 0;
    g_ui_home_pa_dir = 0;
    if (!dir || h->sel < 0 || h->sel >= h->n) return;
    const int sl = h->sel - h->top_row * COLS;
    if (sl < 0 || sl >= NSLOT) return;
    HItem *it = &c->it[IT_TILE0 + sl];
    if (!it->active || !pins_ready() || (dir > 0) != (it->pn.on != 0)) return;
    TapeEnt *e = tape_ent(&it->tp[0].t);
    if (!e->peel && !e->tried) { // (its peeling states are made now, like the focused card's: the strip curls from the first frame)
        e->tried = 1;
        e->peel = paper_tape_peel_lazy(&e->t, PAPER_PEEL_STATES);
        e->peel_at = 0;
    }
    const PaperSpr *top = e->peel ? paper_peel_at(e->peel, 1.0f) : NULL;
    if (!top) return;
    pa_clear();
    g_pa.tape_sh = paper_shadow_of(top, 0, 0, 3.0f, PAPER_SHADOW, 0.30f);
    g_pa.kind = dir > 0 ? PA_IN : PA_OUT;
    g_pa.key = it->pkey;
    g_pa.t0 = (it->shf_t0 > 0 ? it->shf_t0 : (double)ui_now()) + PA_LEAD_MS; // (with the carry: one motion)
    g_ui_home_pa_dir = dir;
    it->force = 1;
}

int ui_home(UiHome *h)
{
    if (G.quit) return -1;
    if (h->sel < 0) h->sel = 0;
    if (h->sel > h->n) h->sel = h->n;
    int someone = h->card_on;
    if (h->focus < 0 || h->focus >= Z_COUNT) h->focus = Z_GRID;
    if (h->focus == Z_SIGNOUT && !someone) h->focus = Z_HERO; // signed out since
    fit_scroll(h);
    grid_rest_at(h);
    HomeCtx *c = &g_hc;
    int slam = stamp_update(h); // (a person loaded since the home screen was last up has to be stamped)
    if (!slam) stamp_board_ink(h);
    home_begin(c, h);

    const int shuffle = g_was.armed; // (the grid ordered or narrowed anew: it shuffles from how it was, instead of fading in)
    if (!shuffle || g_was.fade) ui_transition();
    else ui_wait_finish();
    pin_anim_tilt(c, h);
    if (shuffle) shuf_start(c, h, (double)ui_now());
    pin_anim_begin(c, h);
    uint64_t toast_end = h->toast[0] ? ui_now() + (uint64_t)(h->toast_ms > 0 ? h->toast_ms : 3500) : 0;
    SkinStatus st0;
    ui_status(&st0);
    g_home_shown = 1;
    if (slam) {
        home_frame(c, h, (double)ui_now(), NULL, 0); // (the screen without the stamp, as it fades in)
        stamp_play(c, h);
    }

    for (;;) {
        double now = (double)ui_now();
        int gliding = grid_step(c, h, (uint64_t)now);
        int moving = home_frame(c, h, now, h->toast, h->toast_kind) | gliding;
        if (gliding || g_gm.mode == MO_HELD) prefetch_wanted(c, h, 1); // (the row coming in next is made a tile a frame, not all at once as it arrives)
        if (!G.started_ok) { // the home screen up for 2 s: this version got going (an update's trial is over)
            if (!G.home_up_at) G.home_up_at = (uint64_t)now;
            else if ((uint64_t)now - G.home_up_at >= 2000) {
                G.started_ok = 1;
                if (G.plat->started_ok) G.plat->started_ok(G.plat);
            }
        }

        int pending = !moving && bake_work(c, h, 0);
        uint64_t b = ui_poll(moving || pending ? 0 : 1000);
        if (b == UI_QUIT) return -1;
        int held = G.plat->repeated != 0;
        if (b & UI_REDRAW) { // the theme changed under it: the screen again (and, with a new palette, every picture on it made again in its colours)
            if (home_pal_check(c)) home_begin(c, h);
            else {
                render_base(h);
                c->force_full = 1;
            }
            b &= ~UI_REDRAW;
        }
        int touch_ev = -1, rescroll = 0, rail_moved = 0, dragged = 0;
        if (b & UI_TOUCH) {
            b &= ~UI_TOUCH;
            touch_ev = home_touch(h, &rescroll, &rail_moved, &dragged);
            if (dragged) {
                h->focus = Z_GRID;
                if (grid_scrolling()) grid_lay_down(c, (double)ui_now());
                grid_moved(c, h, (double)ui_now());
                if (!grid_scrolling()) focus_sync(c, h, (double)ui_now(), 0);
            }
            if (rescroll) { // (a letter of the rail: the grid glides to it)
                if (g_gm.mode == MO_HELD) grid_settle(c, h);
                h->focus = Z_GRID;
                grid_aim(c, h, (double)ui_now());
                rail_follow(h->rail ? rail_cur(h) : -1);
                rail_aim(h->rail ? rail_cur(h) : -1, (double)ui_now());
                layout(c, h);
                if (!grid_scrolling()) focus_sync(c, h, (double)ui_now(), 0);
            } else if (rail_moved) {
                rail_aim(h->rail ? rail_cur(h) : -1, (double)ui_now());
                layout(c, h);
            }
        }
        if (!b && touch_ev < 0) {
            if (toast_end && ui_now() >= toast_end) {
                h->toast[0] = 0;
                toast_end = 0;
            }
            if (!moving) { // the clock or battery may have changed
                SkinStatus st;
                ui_status(&st);
                if (strcmp(st.clock, st0.clock) || st.battery != st0.battery || st.charging != st0.charging) {
                    st0 = st;
                    render_base(h);
                    c->force_full = 1;
                } else if (pending) {
                    bake_work(c, h, 1); // a quiet moment: bake something
                }
            }
            continue;
        }

        if (b && g_gm.mode == MO_HELD) grid_settle(c, h); // (a press takes the grid out of the finger's hand: it is on its row)
        int old = h->sel, old_focus = h->focus, ev = -1, z = h->focus;
        int lx = 0, ly = 0; // a press toward nothing: which way to lean
        if (b & BTN_ADMIN) ev = HOME_ADMIN;
        else if (b & BTN_PIN) { // (ZL + ZR + A: an admin pins or unpins the profile in focus)
            if (z == Z_GRID && h->sel < h->n) ev = HOME_PIN;
            else ly = 1;
        }
        else if (z != Z_GRID && (b & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT | BTN_A))) { // above the grid
            // the card (Sign out to its right) above the search bar (the chip to its right): Up and Down keep to their side
            if (b & BTN_UP) { if (z == Z_SEARCH || z == Z_CHIP) h->focus = z == Z_CHIP && someone ? Z_SIGNOUT : Z_HERO; else ly = -1; }
            else if (b & BTN_DOWN) h->focus = z == Z_HERO ? Z_SEARCH : z == Z_SIGNOUT ? Z_CHIP : Z_GRID;
            else if (b & BTN_RIGHT) { if (z == Z_HERO && someone) h->focus = Z_SIGNOUT; else if (z == Z_SEARCH) h->focus = Z_CHIP; else lx = 1; }
            else if (b & BTN_LEFT) { if (z == Z_SIGNOUT) h->focus = Z_HERO; else if (z == Z_CHIP) h->focus = Z_SEARCH; else lx = -1; }
            else ev = z == Z_CHIP ? HOME_SORT : z == Z_SEARCH ? HOME_SEARCH : z == Z_SIGNOUT ? HOME_DONE : HOME_BANNER;
        }
        else if (b & BTN_RIGHT) { if (h->sel < h->n) h->sel++; else lx = 1; }
        else if (b & BTN_LEFT) { if (h->sel > 0) h->sel--; else lx = -1; }
        else if (b & BTN_DOWN) {
            int t = h->sel + COLS;
            if (t > h->n) t = (h->sel / COLS < h->n / COLS) ? h->n : h->sel;
            if (t == h->sel) ly = 1;
            h->sel = t;
        } else if (b & BTN_UP) {
            if (h->sel - COLS >= 0) {
                h->sel -= COLS;
            } else { // up from the first row: the search bar or the chip, whichever is above the tile
                float cx, cy, cw, ch, tx, ty;
                sort_rect(h, &cx, &cy, &cw, &ch);
                tile_xy(h, h->sel, &tx, &ty);
                h->focus = tx + TW / 2 >= cx - 8 ? Z_CHIP : Z_SEARCH;
            }
        }
        else if (b & (BTN_L | BTN_R)) { // (from anywhere on Home: onto the cards, at the next or the last letter, or a row on)
            int d = (b & BTN_R) ? 1 : -1;
            h->focus = Z_GRID;
            if (h->rail) h->sel = jump_letter(h, d);
            else h->sel = d > 0 ? (h->sel + COLS > h->n ? h->n : h->sel + COLS) : (h->sel - COLS < 0 ? 0 : h->sel - COLS);
        }
        else if (b & BTN_A) {
            float tx, ty;
            tile_xy(h, h->sel, &tx, &ty);
            Rect rr = {(int)tx, (int)ty, (int)TW, (int)TH};
            Rect nb[12]; // (the tiles round it, their tape standing above them, stay as they are: only this one is pushed flat)
            int nn = 0;
            for (int i = 0; i <= h->n && nn < 8; i++) {
                int dr = i / COLS - h->sel / COLS, dc = i % COLS - h->sel % COLS;
                if (i == h->sel || dr < -1 || dr > 1 || dc < -1 || dc > 1) continue;
                float ox, oy;
                tile_xy(h, i, &ox, &oy);
                Rect k = {(int)ox - 8, (int)oy - 16, (int)TW + 16, (int)TH + 24};
                const int sl = i - h->top_row * COLS; // (and all of what it has drawn: its tape stands well above it, and its shadow reaches round it)
                if (sl >= 0 && sl < NSLOT && c->it[IT_TILE0 + sl].active && c->it[IT_TILE0 + sl].bb.w > 0) {
                    const Rect bb = c->it[IT_TILE0 + sl].bb;
                    const int kx0 = bb.x < k.x ? bb.x : k.x, ky0 = bb.y < k.y ? bb.y : k.y;
                    const int kx1 = bb.x + bb.w > k.x + k.w ? bb.x + bb.w : k.x + k.w, ky1 = bb.y + bb.h > k.y + k.h ? bb.y + bb.h : k.y + k.h;
                    k = (Rect){kx0, ky0, kx1 - kx0, ky1 - ky0};
                }
                nb[nn++] = k;
            }
            Rect bars[5]; // (the bars and buttons: their own bodies stay as they are whatever the card's tape lies over; their shadows too, but not where its tape is)
            int nbar = 0;
            static const int others[] = {IT_HERO, IT_SEARCH, IT_CHIP, IT_SIGN, IT_RAIL};
            for (size_t k = 0; k < sizeof others / sizeof others[0]; k++) {
                const HItem *o = &c->it[others[k]];
                if (!o->active) continue;
                bars[nbar++] = (Rect){(int)o->x - 2, (int)o->y - 2, (int)o->w + 4, (int)o->h + 4};
                if (nn < 12 && o->bb.w > 0) nb[nn++] = o->bb;
            }
            ui_press_keep(nb, nn);
            ui_press_keep_hard(bars, nbar);
            if (c->cur >= 0 && c->it[c->cur].active) { // (the neighbours' rectangles stop where this card's own tape and pin begin: those go in with it)
                HItem *f = &c->it[c->cur];
                Rect ex[4];
                const int ne = item_extras(f, ex);
                ui_press_free(ex, ne);
            }
            Rect whole = {rr.x - 24, rr.y - 24, rr.w + 48, rr.h + 48}; // (the card, its tape, its pin and its shadow, wherever the lift has put them)
            if (c->cur >= 0 && c->it[c->cur].active && c->it[c->cur].bb.w > 0) {
                const Rect bb = c->it[c->cur].bb;
                const int wx0 = bb.x < rr.x ? bb.x : rr.x, wy0 = bb.y < rr.y ? bb.y : rr.y;
                const int wx1 = bb.x + bb.w > rr.x + rr.w ? bb.x + bb.w : rr.x + rr.w, wy1 = bb.y + bb.h > rr.y + rr.h ? bb.y + bb.h : rr.y + rr.h;
                whole = (Rect){wx0 - 4, wy0 - 4, wx1 - wx0 + 8, wy1 - wy0 + 8};
            }
            ui_press_whole(whole);
            if (c->cur >= 0 && c->it[c->cur].ghost && c->it[c->cur].ghost_spr) { // (the dashed patch where the tile sat stays put)
                const HItem *f = &c->it[c->cur];
                ui_press_ghost(f->ghost_spr, f->x + f->w * 0.5f, f->y + f->h * 0.5f, f->tilt, f->ps.ghost);
                if (f->moving) { // (and what of it the lifted card covers stays covered: the card as drawn, turned and bigger than at rest. In Paper night the card is hardly
                                 // lighter than its patch, so without this the dashed outline showed round the card and over it)
                    const float ca = fabsf(cosf(f->ang)), sa = fabsf(sinf(f->ang));
                    const float ex = (ca * f->w + sa * f->h) * 0.5f * f->sc, ey = (sa * f->w + ca * f->h) * 0.5f * f->sc;
                    const int x0 = (int)ceilf(f->cx - ex), y0 = (int)ceilf(f->cy - ey), x1 = (int)floorf(f->cx + ex), y1 = (int)floorf(f->cy + ey);
                    ui_press_cover((Rect){x0, y0, x1 - x0, y1 - y0});
                }
            }
            ui_press(G.work, rr);
            ev = h->sel == h->n ? HOME_NEW : HOME_CHOOSE;
        }
        else if (b & BTN_X) ev = HOME_DONE;
        else if (b & BTN_Y) ev = HOME_SEARCH;
        else if (b & BTN_MINUS) ev = h->query[0] ? HOME_CLEAR : HOME_ABOUT;
        else if (b & BTN_PLUS) ev = HOME_PICTURE;
        else if (b & BTN_R3) ev = HOME_SORT;

        if (touch_ev >= 0) ev = touch_ev;
        if (ev == HOME_SORT) { // the chip's menu drops down; the grid is ordered anew only if another order is picked
            home_settle(c, h, 0);
            int pick = sort_menu(h);
            if (G.quit) return -1;
            if (pick >= 0 && pick != h->sort_mode) { // (the chip says the new order at once: Home is drawn once more before it is ordered anew)
                h->sort_mode = pick;
                snprintf(h->sort_label, sizeof h->sort_label, "%s", h->sort_names[pick]);
            } else {
                ev = -1;
            }
            c->force_full = 1; // (the menu's last frames are what is on screen)
        }
        if (ev >= 0) {
            if (g_row_off != 0 || mo_moving(&g_gm)) { // (whatever comes next starts from the grid on its row)
                grid_settle(c, h);
                home_frame(c, h, (double)ui_now(), h->toast, h->toast_kind);
            }
            if (ev == HOME_SORT || ev == HOME_CLEAR || ev == HOME_SEARCH || ev == HOME_PIN) shuf_remember(c, h, ev == HOME_SEARCH);
            home_settle(c, h, 0); // dialogs dim the screen without the toast
            ui_screen_done();
            return ev;
        }
        if (lx || ly) {
            bump_start(&c->lean, lx, ly);
            c->lean_item = focus_item(h);
        }
        if (h->sel != old) { // (past the rows showing, the grid glides to the selection: L and R through the letters, the D-pad a row on)
            grid_aim(c, h, (double)ui_now());
            rail_follow(h->rail ? rail_cur(h) : -1);
            rail_aim(h->rail ? rail_cur(h) : -1, (double)ui_now());
            layout(c, h);
        }
        if (!grid_scrolling() && (h->focus != old_focus || h->sel != old)) focus_sync(c, h, (double)ui_now(), held);
    }
}

void ui_home_backdrop(UiHome *h)
{
    if (G.quit) return;
    if (h->sel < 0) h->sel = 0;
    if (h->sel > h->n) h->sel = h->n;
    fit_scroll(h);
    if (h->focus < 0 || h->focus >= Z_COUNT || (h->focus == Z_SIGNOUT && !h->card_on)) h->focus = Z_GRID;
    HomeCtx *c = &g_hc;
    home_begin(c, h);
    home_frame(c, h, (double)ui_now(), NULL, 0);
    ui_screen_done();
}

// The intro (ui_intro.c) ends on the home screen, so it needs that screen before it is shown: the first frame ui_home will draw, set up the way ui_home sets it
// up (the stamp already on the loaded person's tile, as at every first home screen of a run), painted into `full`; and the same on a black board and on a white
// one, from which the screen's parts can be had without the board. Nothing is shown and nothing changes that ui_home would do differently afterwards.
void ui_home_capture(UiHome *h, Surf *full, Surf *black, Surf *white, UiHomeGeo *geo)
{
    if (h->sel < 0) h->sel = 0;
    if (h->sel > h->n) h->sel = h->n;
    if (h->focus < 0 || h->focus >= Z_COUNT) h->focus = Z_GRID;
    if (h->focus == Z_SIGNOUT && !h->card_on) h->focus = Z_HERO;
    fit_scroll(h);
    HomeCtx *c = &g_hc;
    if (!stamp_update(h)) stamp_board_ink(h); // (as ui_home does first: a second call changes nothing)
    home_begin(c, h);
    frame_poses(c, h, (double)ui_now());
    Rect all = {0, 0, SCREEN_W, SCREEN_H};
    if (full) paint_rect(c, full, &all);
    int flat = g_paper_board_flat, nomark = g_skin_no_mark;
    g_skin_no_mark = 1; // (the parts come without the corner's mark and name: the intro stamps those on the board itself)
    if (black) {
        g_paper_board_flat = 2;
        render_base(h);
        paint_rect(c, black, &all);
    }
    if (white) {
        g_paper_board_flat = 3;
        render_base(h);
        paint_rect(c, white, &all);
    }
    g_paper_board_flat = flat;
    g_skin_no_mark = nomark;
    if (black || white) render_base(h);
    if (geo) {
        memset(geo, 0, sizeof *geo);
        int first = h->top_row * COLS;
        for (int i = first; i <= h->n && geo->ntiles < NSLOT; i++) {
            float x, y;
            tile_xy(h, i, &x, &y);
            geo->cx[geo->ntiles] = x + TW * 0.5f;
            geo->cy[geo->ntiles] = y + TH * 0.5f;
            geo->row[geo->ntiles] = (i - first) / COLS;
            geo->col[geo->ntiles] = (i - first) % COLS;
            geo->row_n[(i - first) / COLS]++;
            geo->ntiles++;
        }
        geo->pitch_x = TPX;
        geo->pitch_y = TPY;
        geo->x0 = TX0 + TW * 0.5f;
        geo->y0 = TY0 + TH * 0.5f;
        geo->rail = h->rail;
    }
}
