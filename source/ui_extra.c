// Edit Icon's Picture page, in the style of the Backups page: a strip of cards (three show in full, the next one peeks in from the screen's edge with a
// round button on it), the console's own pictures first and then the Extra icons game by game, and under it a sheet with the pictures of the card in
// focus. The cards slide on the Backups spring and the card in the middle lifts off the page, its tape peeling; the next card's pictures fade in as the
// old ones fade out. L and R go one card along, ZL and ZR several, the D-pad moves over the pictures, A uses one, X goes to "no picture" (the letter, the
// console card's first cell), R3 sorts the games (the console's card stays first).
// The pictures are drawn from small copies made on a thread of their own (ui_thumbs.c): a cell whose copy isn't there yet is a stand-in of the
// profile's colour, so the page never waits for a picture to be read; rows are drawn as they come into view and kept while they are near.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom_pics.h"
#include "icons.h"
#include "ui_motion.h"
#include "ui_scene.h"
#include "ui_spr.h"
#include "ui_thumbs.h"

#define BX 190.0f // the strip starts here
#define TILE_W 280.0f
#define TILE_H 176.0f
#define PITCH 310.0f
#define TY 172.0f // the cards' top
#define SHOWN 3
#define MID 1
#define SKIP 5
#define PEEK_FADE 1100.0f
#define BTN_R_ 28.0f
#define LP D_PAD
#define PANEL PAL(patch, 0xECE9F1)

// the sheet of pictures
#define SHX 190.0f
#define SHY 396.0f
#define SHW 900.0f
#define SHH 244.0f
#define COLS 9
#define CELL 76
#define STEP 90
#define WIN_W 858 // what shows of it: inside the sheet, left of the scroll cue
#define WIN_H 236
#define TOP_PAD 22
#define ROWS_KEPT 18 // rows drawn and kept round the window (each about 300 KB)
#define CHIP_X 976.0f
#define CHIP_Y 103.0f
#define CHIP_W 264.0f
#define FADE_MS 300

#define CONSOLE (-1) // the strip's first card: the console's own pictures, and the letter

static const char *const SORTS[3] = {"Name, A to Z", "Recently downloaded", "Most icons"};
static int g_sort; // (kept while the app runs)

// tests: where the strip and the sheet's focus were in the last frame composed (the card's place in the strip, the picture's place on its card)
int g_ui_extra_pos = -1, g_ui_extra_sel = -1;
int g_ui_extra_waiting; // tests: the rows showing in the last frame still had stand-ins for pictures on their way
long g_ui_extra_rebakes; // tests: card pictures drawn again because their small pictures had come

static float rad(float deg) { return deg * 3.14159265f / 180.0f; }
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }

// ---------------------------------------------------------------------------------------------------- the cards, in order
typedef struct {
    const IconSet *set;
    int ng;   // Extra icons games
    int n;    // cards: the console's, then the games
    int *ord; // the place in the strip -> CONSOLE or the game's number in the set
    int swatch;
    const char *name;
} Ex;

static const IconSet *s_set;
static int s_mode;

static int title_cmp(int a, int b)
{
    char x[96], y[96];
    icons_title(icons_slug(s_set, a), x, sizeof x);
    icons_title(icons_slug(s_set, b), y, sizeof y);
    for (int i = 0; x[i] || y[i]; i++) {
        int p = x[i] >= 'A' && x[i] <= 'Z' ? x[i] + 32 : x[i], q = y[i] >= 'A' && y[i] <= 'Z' ? y[i] + 32 : y[i];
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}

static int order_cmp(const void *pa, const void *pb)
{
    int a = *(const int *)pa, b = *(const int *)pb, c = 0;
    if (s_mode == 1) { // the most recently changed first
        int64_t ta = icons_added(s_set, a), tb = icons_added(s_set, b);
        c = ta > tb ? -1 : ta < tb ? 1 : 0;
    } else if (s_mode == 2) { // the most characters first
        int na = icons_count(s_set, a), nb = icons_count(s_set, b);
        c = na > nb ? -1 : na < nb ? 1 : 0;
    }
    return c ? c : title_cmp(a, b);
}

static void make_order(Ex *x)
{
    x->ord[0] = CONSOLE;
    for (int i = 0; i < x->ng; i++) x->ord[1 + i] = i;
    s_set = x->set;
    s_mode = g_sort;
    if (x->ng > 1) qsort(x->ord + 1, (size_t)x->ng, sizeof x->ord[0], order_cmp);
}

static int pos_of(const Ex *x, int game)
{
    for (int i = 0; i < x->n; i++)
        if (x->ord[i] == game) return i;
    return 0;
}

static int row_most(const Ex *x) { return x->n > SHOWN ? x->n - SHOWN : 0; }

// the slide that puts card p in the middle slot (at either end of the strip the cards can go no further, so it sits in an outer slot)
static int row_for(const Ex *x, int p)
{
    int s = p - MID;
    if (s > row_most(x)) s = row_most(x);
    return s < 0 ? 0 : s;
}

// how many pictures a card has (the console's: the letter, then its pictures)
static int card_count(const Ex *x, int game) { return game == CONSOLE ? 1 + skin_avatar_count() : icons_count(x->set, game); }

// picture i of a card, as a picture id (-1: the letter)
static int card_pic(const Ex *x, int game, int i)
{
    if (game == CONSOLE) return i == 0 ? -1 : skin_avatar_at(i - 1);
    char key[CUSTOM_KEY_MAX];
    snprintf(key, sizeof key, "%s/%s", icons_slug(x->set, game), icons_name(x->set, game, i));
    return custom_id(key);
}

// the three pictures on a card's patch (-2: fewer than three)
static void card_faces(const Ex *x, int game, int out[3])
{
    for (int i = 0; i < 3; i++) out[i] = -2;
    if (game == CONSOLE) {
        for (int i = 0; i < 3 && i < skin_avatar_count(); i++) out[i] = skin_avatar_at(i);
        if (skin_avatar_count() == 0) out[0] = -1; // (no pictures on this console: the letter)
        return;
    }
    for (int i = 0; i < 3 && i < icons_count(x->set, game); i++) out[i] = card_pic(x, game, i);
}

static void card_title(const Ex *x, int game, char *out, size_t n)
{
    if (game == CONSOLE) snprintf(out, n, "Console");
    else icons_title(icons_slug(x->set, game), out, n);
}

// ---------------------------------------------------------------------------------------------------- drawing a picture from its small copy
// the picture `id` in a size x size rounded square at (x, y), on the profile's colour; a stand-in while its copy is being made. 0 = it was the stand-in.
static int draw_square(Cx *c, float x, float y, float size, int id, const char *name, int swatch)
{
    int look = SKIN_LOOK(swatch, id);
    if (id < 0) {
        skin_avatar_square(c, x, y, size, name, SKIN_LOOK(swatch, -1), 10);
        return 1;
    }
    int st;
    const uint8_t *px = thumbs_get(id, &st);
    Col bg = skin_avatar_color(look);
    if (px) {
        gfx_rrect(c->s, x, y, size, size, 10, bg);
        gfx_blit_rrect_scaled(c->s, px, THUMB_PX, THUMB_PX, x, y, size, size, 10);
        return 1;
    }
    if (st == THUMB_NONE) {
        skin_avatar_square(c, x, y, size, name, SKIN_LOOK(swatch, -1), 10);
        return 1;
    }
    gfx_rrect(c->s, x, y, size, size, 10, with_a(bg, 0.30f)); // (its copy is on its way)
    return 0;
}

// the same in a circle of radius r about (cx, cy)
static int draw_round(Cx *c, float cx, float cy, float r, int id, const char *name, int swatch)
{
    int look = SKIN_LOOK(swatch, id);
    Col bg = skin_avatar_color(look);
    if (id < 0) {
        skin_avatar(c, cx, cy, r, name, SKIN_LOOK(swatch, -1));
        return 1;
    }
    int st;
    const uint8_t *px = thumbs_get(id, &st);
    if (px) {
        gfx_circle(c->s, cx, cy, r, bg);
        gfx_blit_circle(c->s, px, THUMB_PX, THUMB_PX, cx, cy, r, 1.0f);
        return 1;
    }
    if (st == THUMB_NONE) {
        skin_avatar(c, cx, cy, r, name, SKIN_LOOK(swatch, -1));
        return 1;
    }
    gfx_circle(c->s, cx, cy, r, with_a(bg, 0.30f));
    return 0;
}

// ---------------------------------------------------------------------------------------------------- the cards
static void round_btn(Surf *s, float cx, float cy, int left)
{
    gfx_shadow(s, cx - BTN_R_, cy - BTN_R_, BTN_R_ * 2, BTN_R_ * 2, BTN_R_, 3, 8, pal_disc_shadow(0.30f));
    gfx_circle(s, cx, cy, BTN_R_, hex(PAL(paper, PAPER_PAPER), 1));
    gfx_ring(s, cx, cy, BTN_R_ - 0.5f, 1.0f, pal_light(g_pal.paper) ? col(0, 0, 0, 0.10f) : col(1, 1, 1, 0.08f));
    float k = BTN_R_ * 0.32f, d = left ? 1 : -1;
    Col ink = hex(PAL(text, PAPER_TEXT), 1);
    gfx_line(s, cx + d * k * 0.5f, cy - k, cx - d * k * 0.55f, cy, 3.4f, ink);
    gfx_line(s, cx - d * k * 0.55f, cy, cx + d * k * 0.5f, cy + k, 3.4f, ink);
}

typedef struct {
    const Ex *x;
    int game, lifted;
    const PaperSpr *card;
    int *complete; // cleared when a picture on it was a stand-in
} CardJob;

static void draw_card_body(Surf *s, void *ud)
{
    const CardJob *k = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const Ex *x = k->x;
    const float px = LP, py = LP, w = TILE_W;
    paper_spr_put(s, k->card, LP, LP);
    // the pictures of a card: round, overlapping, each with a ring of the patch's colour
    gfx_rrect(s, px + 14, py + 14, w - 28, 84, 6, hex(PANEL, 1));
    int faces[3], n = 0;
    card_faces(x, k->game, faces);
    while (n < 3 && faces[n] != -2) n++;
    const float r = 26, step = r * 1.7f, total = 2 * r + (n - 1) * step, sx = px + 14 + (w - 28 - total) / 2 + r, cy = py + 14 + 42;
    for (int i = 0; i < n; i++) {
        float ax = sx + i * step;
        gfx_circle(s, ax, cy, r + 3, hex(PANEL, 1));
        if (!draw_round(&c, ax, cy, r, faces[i], x->name, x->swatch) && k->complete) *k->complete = 0;
    }
    char title[96], fit[96], sub[48];
    card_title(x, k->game, title, sizeof title);
    text_fit(u->semi, 22, -0.1f, title, w - 36, fit, sizeof fit);
    skin_txt(&c, u->semi, 22, -0.1f, px + 18, py + 128, 0, hex(PAL(text, PAPER_TEXT), 1), fit);
    int total_n = k->game == CONSOLE ? skin_avatar_count() : icons_count(x->set, k->game);
    if (k->game == CONSOLE) snprintf(sub, sizeof sub, "%d picture%s", total_n, total_n == 1 ? "" : "s");
    else snprintf(sub, sizeof sub, "%d icon%s", total_n, total_n == 1 ? "" : "s");
    skin_txt(&c, u->med, 16.5f, 0, px + 18, py + 154, 0, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), sub);
}

static const int KINDS[4] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG};

// One card with its top left at (px, py) on the surface of c: lying on the page (quiet shadow, tape stuck down, turned a hair) or lifted (risen off its
// deep shadow, the tape peeling). A tile of the Backups page, drawn the same way.
static void draw_card(Cx *c, void *ud, float px, float py)
{
    const CardJob *job = ud;
    Surf *dst = c->s;
    const int idx = job->game + 1; // (the console's card is 0)
    const float w = TILE_W, h = TILE_H, rr = 6.0f;
    CardJob k = *job;
    k.card = card_get((int)w, (int)h, rr, 70u + (uint32_t)(idx & 7), LP);
    PaperSpr *body = paper_bake((int)w + 2 * LP, (int)h + 2 * LP, draw_card_body, &k);
    const PaperShadows *sh = shadows_get((int)w, (int)h, rr, 0.0f);
    static const float TILTS[4] = {-0.7f, 0.8f, -0.6f, 0.9f};
    float tilt = rad(TILTS[idx & 3]), rise = job->lifted ? 7.0f : 0.0f;
    if (sh) paper_spr_put(dst, sh->s[job->lifted ? sh->n - 1 : 0], (int)px, (int)py);
    float cx = px + w * 0.5f, cy = py + h * 0.5f - rise, sc = job->lifted ? 1.012f : 1.0f;
    if (body) paper_spr_xform(dst, body, cx, cy, sc, tilt, 1.0f);
    PaperTape tp2[2];
    float tdx[2], tdy[2], stood[2], bx[2] = {-12.0f, w - 42.0f}, by[2] = {1.0f, 1.0f};
    tp2[0] = (PaperTape){54, 20, paper_tape_rgb[(idx + 5) & 7], KINDS[(idx + 1) & 3], PAPER_TAPE_ZIG, -40.0f, 300u + (uint32_t)idx};
    tp2[1] = (PaperTape){54, 20, paper_tape_rgb[(idx + 3) & 7], KINDS[(idx + 3) & 3], PAPER_TAPE_ZIG, 40.0f, 340u + (uint32_t)idx};
    for (int i = 0; i < 2; i++) {
        stood[i] = paper_tape_hand(&tp2[i], &bx[i], &by[i], (uint32_t)(idx * 2 + i) * 7919u + 11u, 1.0f);
        paper_tape_offset(w, h, bx[i], by[i], (float)tp2[i].len, (float)tp2[i].wid, &tdx[i], &tdy[i]);
        paper_tape_host(&tp2[i], w, h, rr, bx[i], by[i]);
    }
    for (int i = 0; i < 2; i++) {
        PaperXf cf = {cx, cy, sc, tilt}, tp = paper_xf_attach(cf, tdx[i], tdy[i]);
        if (job->lifted || stood[i] > 0.0f) {
            PaperPeel *pl = paper_tape_peel_lazy(&tp2[i], PAPER_PEEL_STATES);
            const PaperSpr *ts = pl ? paper_peel_at(pl, job->lifted ? 0.85f : stood[i]) : NULL;
            if (ts) paper_spr_xform(dst, ts, tp.x, tp.y, tp.scale, tp.angle, 1.0f);
            paper_peel_free(pl);
        } else {
            PaperSpr *ts = paper_tape(&tp2[i]);
            if (ts) paper_spr_xform(dst, ts, tp.x, tp.y, tp.scale, tp.angle, 1.0f);
            paper_spr_free(ts);
        }
    }
    paper_spr_free(body);
}

static void draw_btn(Cx *c, void *ud, float px, float py) { round_btn(c->s, px + BTN_R_, py + BTN_R_, *(int *)ud); }

// ---- the cached pictures of the cards (and the two round buttons), by card (the console's is 0, game g's is g + 1)
#define SPR_BUDGET (24 * 1024 * 1024)

typedef struct {
    Spr *rest, *lift;
    uint8_t *rest_ok, *lift_ok; // drawn with every picture on it final (not a stand-in)
    Spr btn[2];
    unsigned frame;
    int n;
} CardSprs;

static void cspr_free(CardSprs *p)
{
    for (int i = 0; i < p->n; i++) {
        if (p->rest) spr_free_one(&p->rest[i]);
        if (p->lift) spr_free_one(&p->lift[i]);
    }
    spr_free_one(&p->btn[0]);
    spr_free_one(&p->btn[1]);
    free(p->rest);
    free(p->lift);
    free(p->rest_ok);
    free(p->lift_ok);
    memset(p, 0, sizeof *p);
}

static int cspr_new(CardSprs *p, int n)
{
    memset(p, 0, sizeof *p);
    if (n <= 0 || n > 100000) return 0;
    size_t k = (size_t)n;
    p->n = n;
    p->rest = calloc(k, sizeof(Spr));
    p->lift = calloc(k, sizeof(Spr));
    p->rest_ok = calloc(k, 1);
    p->lift_ok = calloc(k, 1);
    return p->rest && p->lift && p->rest_ok && p->lift_ok;
}

static size_t cspr_held(const CardSprs *p)
{
    size_t held = 0;
    for (int i = 0; i < p->n; i++) held += ((size_t)p->rest[i].w * p->rest[i].h + (size_t)p->lift[i].w * p->lift[i].h) * 4;
    return held;
}

static void card_bake(const Ex *x, CardSprs *p, int game, int lifted)
{
    int ci = game + 1, ok = 1;
    CardJob job = {x, game, lifted, NULL, &ok};
    spr_bake(lifted ? &p->lift[ci] : &p->rest[ci], (int)TILE_W, (int)TILE_H, draw_card, &job);
    (lifted ? p->lift_ok : p->rest_ok)[ci] = (uint8_t)ok;
}

static const Spr *card_spr(const Ex *x, CardSprs *p, int game, int lifted)
{
    int ci = game + 1;
    Spr *s = lifted ? &p->lift[ci] : &p->rest[ci];
    if (!s->px) {
        if (cspr_held(p) > SPR_BUDGET)
            for (int i = 0; i < p->n; i++) {
                if (p->rest[i].px && p->rest[i].used != p->frame) spr_free_one(&p->rest[i]);
                if (p->lift[i].px && p->lift[i].used != p->frame) spr_free_one(&p->lift[i]);
            }
        card_bake(x, p, game, lifted);
    }
    s->used = p->frame;
    return s;
}

// The pictures of the cards came: a card drawn with stand-ins whose pictures are all here now is drawn again (when it is next wanted).
static void cspr_refresh(const Ex *x, CardSprs *p)
{
    for (int ci = 0; ci < p->n; ci++) {
        int stale_rest = p->rest[ci].px && !p->rest_ok[ci], stale_lift = p->lift[ci].px && !p->lift_ok[ci];
        if (!stale_rest && !stale_lift) continue;
        int faces[3], ready = 1;
        card_faces(x, ci - 1, faces);
        for (int i = 0; i < 3; i++)
            if (faces[i] >= 0 && !thumbs_ready(faces[i])) ready = 0;
        if (!ready) continue;
        if (stale_rest) spr_free_one(&p->rest[ci]);
        if (stale_lift) spr_free_one(&p->lift[ci]);
        g_ui_extra_rebakes++;
    }
}

// While nothing moves: the nearest card without its picture gets one (lying down; lifted too for the cards next to the focus), so a slide doesn't have
// to stop to draw. 1 = one was made.
static int cspr_prefetch(const Ex *x, CardSprs *p, int pos)
{
    if (cspr_held(p) > SPR_BUDGET * 3 / 4) return 0;
    for (int d = 0; d < x->n; d++)
        for (int k = 0; k < 2; k++) {
            int i = k ? pos + d : pos - d;
            if (i < 0 || i >= x->n || (k && !d)) continue;
            int g = x->ord[i], ci = g + 1;
            if (!p->rest[ci].px) {
                card_bake(x, p, g, 0);
                p->rest[ci].used = p->frame;
                return 1;
            }
            if (d <= 2 && !p->lift[ci].px) {
                card_bake(x, p, g, 1);
                p->lift[ci].used = p->frame;
                return 1;
            }
        }
    return 0;
}

// the pictures of every card's patch, asked for ahead of time
static void want_faces(const Ex *x)
{
    for (int p = 0; p < x->n; p++) {
        int faces[3];
        card_faces(x, x->ord[p], faces);
        for (int i = 0; i < 3; i++)
            if (faces[i] >= 0) thumbs_want(faces[i]);
    }
}

static float focus_on(const Hops *hp, int pos)
{
    float f = 0;
    for (int k = 0; k < hp->n; k++)
        if ((int)lroundf(hp->x[k]) == pos) f += hops_strength(hp->a[k]);
    return f > 1.0f ? 1.0f : f;
}

static void put_card(Cx *c, const Ex *x, CardSprs *p, int game, float cx, float cy, float focus)
{
    int xi = (int)lroundf(cx), yi = (int)lroundf(cy);
    if (focus <= 0.004f) {
        spr_blit(c->s, card_spr(x, p, game, 0), xi, yi, 256);
        return;
    }
    const PaperSpr *gh = ghost_get((int)TILE_W, (int)TILE_H, 7.0f);
    if (gh) paper_spr_xform(c->s, gh, xi + TILE_W * 0.5f, yi + TILE_H * 0.5f, 1.0f, 0.0f, focus);
    if (focus < 0.996f) spr_blit(c->s, card_spr(x, p, game, 0), xi, yi, (int)((1.0f - focus) * 256.0f));
    spr_blit(c->s, card_spr(x, p, game, 1), xi, yi, focus >= 0.996f ? 256 : (int)(focus * 256.0f));
}

// ---- the shuffle: the games put in a new order are carried from where they were to their new places, one after another, each hopping up over the
// cards it passes (the console's card stays first); a card from out of sight comes in from the screen's edge
#define CSH_MS 380.0
#define CSH_STEP_MS 22.0
#define CSH_HOP 30.0f
static struct {
    float *from; // by card (CONSOLE + 1 = 0, then the games): its x on the screen when the order changed (NULL: no shuffle)
    uint64_t t0;
    int first;   // the first card that shows in the new order
} g_csh;

static void csh_end(void)
{
    free(g_csh.from);
    memset(&g_csh, 0, sizeof g_csh);
}

static double csh_ease(double u) { return u < 0.5 ? 4 * u * u * u : 1 - pow(-2 * u + 2, 3) / 2; }

// card at place p (game) now: where its top left is; 1 while it moves
static int csh_card(int p, int game, float *x, float *y)
{
    if (!g_csh.from) return 0;
    int d = p - g_csh.first;
    d = d < 0 ? 0 : d > 6 ? 6 : d;
    double u = ((double)ui_now() - (double)g_csh.t0 - d * CSH_STEP_MS) / CSH_MS;
    if (u >= 1.0) return 0;
    const float lo = -TILE_W - 40, hi = SCREEN_W + 10;
    float x0 = clampf(g_csh.from[game + 1], lo, hi), xt = clampf(*x, lo, hi);
    if (fabsf(xt - x0) < 0.5f) return 0;
    float e = u <= 0 ? 0.0f : (float)csh_ease(u);
    *x = x0 + (xt - x0) * e;
    *y -= CSH_HOP * sinf(3.14159265f * e);
    return 1;
}

static int csh_running(int n)
{
    if (!g_csh.from) return 0;
    int d = n - g_csh.first;
    d = d < 0 ? 0 : d > 6 ? 6 : d;
    if ((double)ui_now() - (double)g_csh.t0 < d * CSH_STEP_MS + CSH_MS + 20) return 1;
    csh_end();
    return 0;
}

static void put_btn(Cx *c, CardSprs *p, int which, float cx, float cy, float k)
{
    if (k < 0.02f) return;
    Spr *s = &p->btn[which];
    if (!s->px) {
        int left = which == 0;
        spr_bake(s, (int)(BTN_R_ * 2), (int)(BTN_R_ * 2), draw_btn, &left);
    }
    spr_blit(c->s, s, (int)lroundf(cx - BTN_R_), (int)lroundf(cy - BTN_R_), k >= 1.0f ? 256 : (int)(k * 256.0f));
}

// ---------------------------------------------------------------------------------------------------- the sheet of pictures
typedef struct {
    int *ids; // the card's pictures, as picture ids (-1: the letter)
    int count, rows, game, inuse;
    PaperSpr **row;  // each row of cells as drawn (NULL until it comes near the window)
    uint8_t *full;   // the row was drawn with every picture final
    uint8_t *nfinal; // how many of its pictures were final when it was drawn
    uint64_t *at;    // when it was drawn
    int held;
    const Ex *x;
} Sheet;

typedef struct {
    const Sheet *s;
    int row, full, nfinal;
} RowBake;

#define ROW_REDRAW_MS 120 // a row waiting for pictures is drawn again when they are all here, or this often while they come in

static void draw_row(Surf *s, void *ud)
{
    RowBake *b = ud;
    Cx c = ui_cx(s);
    float left = (WIN_W - (COLS * STEP - 14)) / 2.0f;
    for (int col = 0; col < COLS; col++) {
        int i = b->row * COLS + col;
        if (i >= b->s->count) break;
        float x = left + col * STEP, y = 0;
        if (!draw_square(&c, x, y, CELL, b->s->ids[i], b->s->x->name, b->s->x->swatch)) b->full = 0;
        else b->nfinal++;
        if (i == b->s->inuse) { // the picture the profile has now: a check badge on its corner
            gfx_circle(s, x + CELL - 7, y + CELL - 7, 13, hex(PAL(paper, PAPER_PAPER), 1));
            gfx_circle(s, x + CELL - 7, y + CELL - 7, 10.5f, hex(PAL(ink, PAPER_INK), 1));
            gfx_line(s, x + CELL - 12, y + CELL - 6.5f, x + CELL - 8, y + CELL - 3, 2.4f, hex(0xFFFFFF, 1));
            gfx_line(s, x + CELL - 8, y + CELL - 3, x + CELL - 1.5f, y + CELL - 11, 2.4f, hex(0xFFFFFF, 1));
        }
    }
}

static void row_drop(Sheet *sh, int r)
{
    if (!sh->row || !sh->row[r]) return;
    paper_spr_free(sh->row[r]);
    sh->row[r] = NULL;
    sh->held--;
}

static void row_draw(Sheet *sh, int r)
{
    if (r < 0 || r >= sh->rows) return;
    RowBake rb = {sh, r, 1, 0};
    PaperSpr *spr = paper_bake(WIN_W, STEP, draw_row, &rb);
    if (!spr) return;
    if (sh->row[r]) paper_spr_free(sh->row[r]);
    else sh->held++;
    sh->row[r] = spr;
    sh->full[r] = (uint8_t)rb.full;
    sh->nfinal[r] = (uint8_t)rb.nfinal;
    sh->at[r] = ui_now();
}

// is row r worth drawing again: all its pictures have come, or some have and it was drawn a while ago
static int row_stale(const Sheet *sh, int r)
{
    int n = 0, cells = 0;
    for (int col = 0; col < COLS; col++) {
        int i = r * COLS + col;
        if (i >= sh->count) break;
        cells++;
        n += sh->ids[i] < 0 || thumbs_ready(sh->ids[i]);
    }
    if (n <= sh->nfinal[r]) return 0;
    return n == cells || ui_now() - sh->at[r] >= ROW_REDRAW_MS;
}

static void sheet_clear(Sheet *sh)
{
    if (sh->row)
        for (int r = 0; r < sh->rows; r++) paper_spr_free(sh->row[r]);
    free(sh->row);
    free(sh->full);
    free(sh->nfinal);
    free(sh->at);
    free(sh->ids);
    memset(sh, 0, sizeof *sh);
}

static void sheet_make(Sheet *sh, const Ex *x, int game, int inuse_pic)
{
    sheet_clear(sh);
    sh->x = x;
    sh->game = game;
    int n = card_count(x, game);
    sh->count = n;
    sh->rows = (n + COLS - 1) / COLS;
    sh->ids = malloc(sizeof(int) * (size_t)(n ? n : 1));
    sh->row = calloc((size_t)(sh->rows ? sh->rows : 1), sizeof *sh->row);
    sh->full = calloc((size_t)(sh->rows ? sh->rows : 1), 1);
    sh->nfinal = calloc((size_t)(sh->rows ? sh->rows : 1), 1);
    sh->at = calloc((size_t)(sh->rows ? sh->rows : 1), sizeof *sh->at);
    sh->inuse = -1;
    for (int i = 0; sh->ids && i < n; i++) {
        sh->ids[i] = card_pic(x, game, i);
        if (sh->ids[i] == inuse_pic) sh->inuse = i;
    }
    if (!sh->ids || !sh->row || !sh->full || !sh->nfinal || !sh->at) {
        sheet_clear(sh);
        sh->x = x;
        sh->game = game;
    }
}

static float sheet_max(const Sheet *sh)
{
    float tall = (float)(TOP_PAD + sh->rows * STEP + 8);
    return tall > WIN_H ? tall - WIN_H : 0;
}

static void rows_seen(int scroll, int *first, int *last)
{
    *first = (scroll - TOP_PAD) / STEP - (scroll < TOP_PAD ? 1 : 0);
    *last = (scroll + WIN_H - TOP_PAD) / STEP;
}

// Keeps the rows that show drawn: one not drawn yet is drawn at once (with stand-ins for the pictures still on their way, which takes no time), one
// drawn with stand-ins is drawn again when pictures have come (`fresh`). The rows ahead are asked for, the far ones let go. 1 = rows still wait for pictures.
static int sheet_fill(Sheet *sh, int scroll, int fresh)
{
    if (!sh->row) return 0;
    int first, last, waiting = 0;
    rows_seen(scroll, &first, &last);
    for (int r = first; r <= last; r++) {
        if (r < 0 || r >= sh->rows) continue;
        if (!sh->row[r] || (!sh->full[r] && (fresh || ui_now() - sh->at[r] >= ROW_REDRAW_MS) && row_stale(sh, r))) row_draw(sh, r);
        if (sh->row[r] && !sh->full[r]) waiting = 1;
    }
    for (int d = 1; d <= 3; d++) // (the next rows each way, ahead of time)
        for (int k = 0; k < 2; k++) {
            int r = k ? last + d : first - d;
            if (r < 0 || r >= sh->rows) continue;
            for (int col = 0; col < COLS; col++) {
                int i = r * COLS + col;
                if (i < sh->count && sh->ids[i] >= 0) thumbs_want(sh->ids[i]);
            }
        }
    if (sh->held > ROWS_KEPT)
        for (int r = 0; r < sh->rows; r++)
            if (r < first - 4 || r > last + 4) row_drop(sh, r);
    return waiting;
}

static void sheet_invalidate(Sheet *sh)
{
    if (!sh->row) return;
    for (int r = 0; r < sh->rows; r++) row_drop(sh, r);
}

// the tape thumb on the dashed line: as long as the window is of the sheet
static PaperSpr *thumb_new(const Sheet *sh, int *len)
{
    float track = SHY + SHH - 20 - (SHY + 26), tallh = (float)(TOP_PAD + sh->rows * STEP + 8);
    *len = (int)(tallh > WIN_H ? track * WIN_H / tallh : track);
    if (*len < 44) *len = 44;
    PaperTape tt = {*len, 14, paper_tape_rgb[4], PAT_STRIPES, PAPER_TAPE_ZIG, 90.0f, 17u};
    return paper_tape(&tt);
}

// the window of the sheet at `scroll`, put over the screen k (0..1) strong: the rows it shows, each cut to the window
static void sheet_put(Surf *dst, const Sheet *sh, int scroll, float k)
{
    if (!sh->row || k <= 0.003f) return;
    int first, last;
    rows_seen(scroll, &first, &last);
    const int x0 = (int)SHX + 4, wy0 = (int)SHY + 4, wy1 = wy0 + WIN_H;
    for (int r = first; r <= last; r++) {
        if (r < 0 || r >= sh->rows || !sh->row[r]) continue;
        const PaperSpr *rs = sh->row[r];
        int y = wy0 + TOP_PAD + r * STEP - scroll, top = y < wy0 ? wy0 - y : 0, bot = y + rs->h > wy1 ? y + rs->h - wy1 : 0, h = rs->h - top - bot;
        if (h <= 0) continue;
        const uint8_t *px = rs->px + (size_t)top * rs->w * 4;
        if (k >= 0.997f) {
            gfx_blit_over(dst, px, rs->w, h, x0, y + top);
        } else {
            PaperSpr view = {rs->w, h, 0, 0, (uint8_t *)px};
            paper_spr_xform(dst, &view, x0 + rs->w * 0.5f, y + top + h * 0.5f, 1.0f, 0.0f, k);
        }
    }
}

// The cell with the focus is a sticker coming unstuck: a dashed ink-blue cut line round it and its top right corner folded over. k is how far it is
// there: the focus arriving or leaving, and the sheet it is on fading in.
static void tri_fill(Surf *s, float x, float y, float size, Col k)
{
    for (int i = 0; i < (int)ceilf(size); i++) gfx_rect(s, x + i, y + i, size - i, 1.0f, k);
}

static void sticker_focus(Surf *s, float x, float y, float k)
{
    if (k <= 0.01f) return;
    scene_dashed_rrect(s, x - 6, y - 6, CELL + 12, CELL + 12, 15, 2.4f, 7.0f, 5.0f, hex(PAL(ink, PAPER_INK), k));
    float f = 21.0f * k, cx = x + CELL - f, cy = y;
    gfx_shadow(s, cx, cy, f, f, 2, 2, 5, pal_disc_shadow(0.30f * k));
    tri_fill(s, cx, cy, f, hex(PAL(paper, PAPER_PAPER), k));
    for (int i = 0; i < (int)ceilf(f); i++) gfx_rect(s, cx, cy + i, f - i, 1.0f, hex(PAL(flap, 0xF2ECDA), k));
    gfx_line(s, cx, cy, cx + f, cy + f, 1.3f, hex(PAL(flap_line, 0xB8AE92), k));
}

// the same, cut off where the sheet's window ends, as its pictures are
static void sticker_focus_in_window(Surf *s, float x, float y, float k)
{
    const int wy0 = (int)SHY + 4;
    UiKeep kp; // (its line, the corner's shadow; cut where its cell is)
    if (!ui_keep_cell_mark(&kp, s, (int)floorf(x) - 10, (int)floorf(y) - 10, (int)ceilf(x + CELL) + 14, (int)ceilf(y + CELL) + 14, y, y + CELL, wy0, wy0 + WIN_H)) return;
    sticker_focus(s, x, y, k);
    ui_put_back(&kp, s);
}

// ---------------------------------------------------------------------------------------------------- the page
// (R3 is on the order chip and the cards have their round buttons: the footer says what is not on the page)
static const Prompt PROMPTS[] = {{"A", "OK"}, {"B", "Back"}, {"X", "No picture"}, {"L+R", "Card"}};

static void render_base(const Ex *x)
{
    scene_chrome(G.base, "Picture", x->name, PROMPTS, x->n > 1 ? 4 : 3);
    if (x->ng >= 2) ui_order_chip(G.base, CHIP_X, CHIP_Y, CHIP_W, SORTS[g_sort], g_sort); // (the order is of the games: with one or none there is nothing to sort)
    SceneTape tp[2] = {{{96, 26, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, -34.0f, 61u}, -18, -12},
                       {{84, 26, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 34.0f, 62u}, SHW - 66, -14}};
    scene_card_on(G.base, SHX, SHY, SHW, SHH, 6.0f, 63u, 0.0f, 0, tp, 2, NULL, NULL);
    for (float y = SHY + 26; y < SHY + SHH - 20; y += 12) gfx_line(G.base, SHX + SHW - 22, y, SHX + SHW - 22, fminf(y + 6, SHY + SHH - 20), 1.8f, hex(PAL(thread, PAPER_THREAD), 0.55f));
    if (x->ng == 0) { // no Extra icons on the card yet: where the next card would be, a dashed outline that says where they come from
        float hx = BX + PITCH, hy = TY;
        scene_dashed_rrect(G.base, hx, hy, TILE_W, TILE_H, 8, 2.0f, 8.0f, 6.0f, hex(PAL(thread, PAPER_THREAD), 0.8f));
        Cx c = ui_cx(G.base);
        skin_txtc(&c, G.skin.semi, 20, 0, hx + TILE_W / 2, hy + TILE_H / 2 - 16, 1, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), "More pictures");
        skin_txtc(&c, G.skin.med, 16, 0, hx + TILE_W / 2, hy + TILE_H / 2 + 14, 1, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), "Admin \xE2\x86\x92 This app \xE2\x86\x92 Extra icons");
    }
}

// where a picture's cell is in the window (a cell's top left from the window's top left, with the sheet at scroll 0)
static void cell_xy(int i, float *x, float *y)
{
    *x = (WIN_W - (COLS * STEP - 14)) / 2.0f + (i % COLS) * STEP;
    *y = (float)(TOP_PAD + (i / COLS) * STEP);
}

// the card that has the picture `pic`, and where on it (*index); the console's for its own pictures and for the letter
static int card_of_pic(const IconSet *set, int pic, int *index)
{
    *index = 0;
    if (!custom_is(pic)) {
        if (pic >= 0 && skin_avatar_pos(pic) >= 0) *index = skin_avatar_pos(pic) + 1;
        return CONSOLE;
    }
    const char *k = custom_key(pic), *slash = k ? strchr(k, '/') : NULL;
    if (!slash || slash - k >= 64) return CONSOLE;
    char slug[64];
    memcpy(slug, k, (size_t)(slash - k));
    slug[slash - k] = 0;
    int g = icons_find(set, slug);
    if (g < 0) return CONSOLE;
    for (int i = 0; i < icons_count(set, g); i++)
        if (!strcmp(icons_name(set, g, i), slash + 1)) {
            *index = i;
            return g;
        }
    return CONSOLE;
}

// 1 = a picture was chosen (*picture is it: a console picture, an Extra icon or -1 for the letter), 0 = back
int ui_picture_strip(const char *name, int swatch, int *picture)
{
    Ex x = {ui_icon_set(0), 0, 0, NULL, swatch, name};
    x.ng = icons_games(x.set);
    x.n = 1 + x.ng;
    x.ord = malloc(sizeof(int) * (size_t)x.n);
    if (!x.ord) return 0;
    make_order(&x);
    render_base(&x);
    ui_transition();

    // where it opens: on the card of the picture in use, on that picture
    int sel = 0, cur_game = card_of_pic(x.set, *picture, &sel);
    int pos = pos_of(&x, cur_game);
    CardSprs sp;
    if (!cspr_new(&sp, x.n)) {
        cspr_free(&sp);
        free(x.ord);
        return 0;
    }
    Sheet sh, old;
    memset(&sh, 0, sizeof sh);
    memset(&old, 0, sizeof old);
    sheet_make(&sh, &x, cur_game, *picture);
    if (sel >= sh.count) sel = 0;
    want_faces(&x);
    uint64_t fade_t0 = 0; // when the sheet last changed card (0: not fading)
    int old_scroll = 0;   // where the sheet that is fading out had scrolled to
    unsigned serial = thumbs_serial();

    Mover row;
    mo_snap(&row, (float)row_for(&x, pos), 1.0f / PITCH);
    Hops hp, hf;
    memset(&hp, 0, sizeof hp);
    hops_snap(&hp, (float)pos, 0);
    float sx, sy;
    cell_xy(sel, &sx, &sy);
    hops_snap(&hf, sx, sy);
    Glide gs = {0};
    glide_snap(&gs, clampf(sy - 12, 0, sheet_max(&sh)), 0, 0, 0);
    Bump bump = {0};
    int lean_card = 0; // the lean is on the card (a press toward no card), else on the sheet's sticker
    int thumb_len = 0;
    PaperSpr *thumb = thumb_new(&sh, &thumb_len);
    uint64_t last = ui_now(), hop_t = last, pressed_at = 0;
    int touching = 0, result = -1;
    int most_pos = row_most(&x);

    for (;;) {
        uint64_t now = ui_now();
        float dt = now > last ? (float)(now - last) / 1000.0f : 0.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f;
        float hdt = now > hop_t ? (float)(now - hop_t) / 1000.0f : 0.0f;
        hop_t = now;
        if (!touching) {
            float want = gs.tgt[0], top = sy - 12, bot = sy + CELL + 14;
            if (top < want) want = top;
            if (bot > want + WIN_H) want = bot - WIN_H;
            glide_to(&gs, clampf(want, 0, sheet_max(&sh)), 0, 0, 0);
        }
        int moving = mo_step(&row, dt);
        moving |= hops_step(&hp, dt);
        moving |= csh_running(x.n);
        moving |= hops_step(&hf, hdt > 0.05f ? 0.05f : hdt);
        moving |= glide_step(&gs);
        float lox, loy;
        moving |= bump_offset(&bump, &lox, &loy);
        int lx = (int)lroundf(lox), ly = (int)lroundf(loy);
        float fade = 1.0f;
        if (fade_t0) {
            fade = (float)(now - fade_t0) / (float)FADE_MS;
            if (fade >= 1.0f) {
                fade_t0 = 0;
                fade = 1.0f;
                sheet_clear(&old);
            } else moving = 1;
        }
        int scroll = (int)floorf(gs.v[0] + 0.5f);
        unsigned ser = thumbs_serial();
        int fresh = ser != serial;
        serial = ser;
        if (fresh) cspr_refresh(&x, &sp);
        int pending = sheet_fill(&sh, scroll, fresh);
        if (fade_t0) sheet_fill(&old, old_scroll, fresh);

        // ---- the frame
        sp.frame++;
        surf_copy(G.work, G.base);
        Cx c = ui_cx(G.work);
        const int overflow = x.n > SHOWN;
        for (int pass = 0; pass < 2; pass++) // (the cards lying still, then the ones being carried over them)
            for (int p = 0; p < x.n; p++) {
                float cx = BX + (p - row.x) * PITCH, cy = TY;
                int mv = csh_card(p, x.ord[p], &cx, &cy);
                if (mv != pass) continue;
                if (cx + TILE_W < -40 || cx > SCREEN_W) continue;
                if (mv) { // (carried: as it lies, no patch left anywhere)
                    spr_blit(c.s, card_spr(&x, &sp, x.ord[p], 0), (int)lroundf(cx), (int)lroundf(cy), 256);
                    continue;
                }
                float f = focus_on(&hp, p);
                put_card(&c, &x, &sp, x.ord[p], cx + (lean_card && p == pos ? (float)lx : 0), cy + (lean_card && p == pos ? (float)ly : 0), f);
            }
        if (overflow) {
            int by0 = (int)(TY - 34), by1 = (int)(TY + TILE_H + 40);
            ground_cols(G.work, G.base, by0, by1, 0, (int)BX, 2);
            ground_cols(G.work, G.base, by0, by1, (int)PEEK_FADE, SCREEN_W, 1);
            float cy = TY + TILE_H / 2;
            put_btn(&c, &sp, 0, SCREEN_W - 1160.0f, cy, row.x * 2.0f);
            put_btn(&c, &sp, 1, 1160, cy, ((float)most_pos - row.x) * 2.0f);
        }
        {
            char title[96], line[160];
            card_title(&x, x.ord[pos], title, sizeof title);
            snprintf(line, sizeof line, "%s  \xC2\xB7  %d of %d", title, pos + 1, x.n);
            skin_txtc(&c, G.skin.med, 18, 0, BX + 450, TY + TILE_H + 26, 1, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), line);
        }
        // the pictures: the old card's go as the new card's come, and the focus comes with the new ones (it is on them)
        float k_new = 1.0f;
        if (fade_t0) {
            k_new = clampf((fade - 0.33f) / 0.67f, 0, 1);
            sheet_put(G.work, &old, old_scroll, clampf(1.0f - fade / 0.33f, 0, 1));
            sheet_put(G.work, &sh, scroll, k_new);
        } else {
            sheet_put(G.work, &sh, scroll, 1.0f);
        }
        if (!lean_card && (lx || ly) && sh.count > 0 && !fade_t0) { // a press toward nothing: the picture leaves its place and leans, its sticker with it
            float cx0 = SHX + 4 + sx, cy0 = SHY + 4 + sy - gs.v[0];
            if (cy0 >= SHY + 4 && cy0 + CELL <= SHY + 4 + WIN_H) {
                int rx = (int)cx0 - 8, ry = (int)cy0 - 8, rw = CELL + 16, rh = CELL + 16;
                if (ry < (int)SHY + 4) {
                    rh -= (int)SHY + 4 - ry;
                    ry = (int)SHY + 4;
                }
                if (ry + rh > (int)SHY + 4 + WIN_H) rh = (int)SHY + 4 + WIN_H - ry;
                for (int y = 0; y < rh; y++) memcpy(G.work->px + ((size_t)(ry + y) * G.work->w + rx) * 4, G.base->px + ((size_t)(ry + y) * G.base->w + rx) * 4, (size_t)rw * 4);
                draw_square(&c, cx0 + lx, cy0 + ly, CELL, sh.ids[sel], x.name, x.swatch);
            }
        }
        if (thumb) { // where the window is, on the dashed line
            float track0 = SHY + 26, track1 = SHY + SHH - 20, track = track1 - track0 - (float)thumb_len, max_s = sheet_max(&sh);
            float f = max_s > 0 ? clampf(gs.v[0] / max_s, 0, 1) : 0;
            paper_spr_put(G.work, thumb, (int)(SHX + SHW - 22), (int)(track0 + thumb_len * 0.5f + track * f));
        }
        if (sh.count > 0 && k_new > 0.01f)
            for (int i = 0; i < hf.n; i++) {
                float fy = SHY + 4 + hf.y[i] - gs.v[0], fx = SHX + 4 + hf.x[i];
                int newest = i == hf.n - 1 && !lean_card;
                if (fy > SHY + 4 - CELL && fy < SHY + 4 + WIN_H)
                    sticker_focus_in_window(G.work, fx + (newest ? (float)lx : 0), fy + (newest ? (float)ly : 0), hops_strength(hf.a[i]) * k_new);
            }
        g_ui_extra_pos = pos;
        g_ui_extra_sel = sel;
        g_ui_extra_waiting = pending;
        ui_present(G.work);
        if (result >= 0) break;

        int more = !moving && !touching && cspr_prefetch(&x, &sp, pos);
        int busy = moving || touching || pending || more;
        uint64_t b = ui_poll(busy ? (pending && !moving && !touching && !more ? 16 : 0) : -1);
        if (!busy) last = ui_now() - 16; // (it was waiting: what starts now starts a frame in, not catching up on the wait)
        if (b == UI_QUIT) {
            result = 0;
            break;
        }
        if (b & UI_REDRAW) { // the theme changed: the pictures are the old colours
            cspr_free(&sp);
            cspr_new(&sp, x.n);
            sheet_invalidate(&sh);
            sheet_invalidate(&old);
            paper_spr_free(thumb);
            thumb = NULL;
            render_base(&x);
            b &= ~UI_REDRAW;
        }

        int want_pos = -1, want_sort = 0, press_a = 0, shoulder_dx = 0, pdx = 0, pdy = 0, to_letter = 0;
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind == TOUCH_DRAG && te.y >= SHY) { // the sheet follows the finger
                    float s = clampf(gs.v[0] - te.dy, 0, sheet_max(&sh));
                    glide_snap(&gs, s, 0, 0, 0);
                    touching = 1;
                } else if (te.kind == TOUCH_END && touching) { // let go: settle on a whole row, with a visible cell selected
                    touching = 0;
                    float s = clampf(floorf((gs.v[0] - TOP_PAD) / STEP + 0.5f) * STEP + TOP_PAD - 12, 0, sheet_max(&sh));
                    glide_to(&gs, s, 0, 0, 0);
                    int first = (int)((s + 12 - TOP_PAD) / STEP + 0.5f), lastr = first + (int)(WIN_H / STEP) - 1;
                    if (sel / COLS < first) sel = first * COLS + sel % COLS;
                    if (sel / COLS > lastr) sel = lastr * COLS + sel % COLS;
                    if (sel >= sh.count) sel = sh.count - 1;
                    if (sel < 0) sel = 0;
                    cell_xy(sel, &sx, &sy);
                    hops_to(&hf, sx, sy);
                } else if (te.kind == TOUCH_TAP) {
                    float cy = TY + TILE_H / 2;
                    if (x.ng >= 2 && in_rect(te.x, te.y, CHIP_X, CHIP_Y, CHIP_W, 48)) want_sort = 1;
                    else if (overflow && in_rect(te.x, te.y, 1160 - BTN_R_, cy - BTN_R_, BTN_R_ * 2, BTN_R_ * 2) && pos < x.n - 1) want_pos = pos + 1;
                    else if (overflow && in_rect(te.x, te.y, SCREEN_W - 1160.0f - BTN_R_, cy - BTN_R_, BTN_R_ * 2, BTN_R_ * 2) && pos > 0) want_pos = pos - 1;
                    else if (te.y >= TY && te.y < TY + TILE_H) {
                        for (int p = 0; p < x.n; p++) {
                            float cx = BX + (p - row.x) * PITCH;
                            if (p != pos && in_rect(te.x, te.y, cx, TY, TILE_W, TILE_H)) want_pos = p;
                        }
                    } else if (in_rect(te.x, te.y, SHX + 4, SHY + 4, WIN_W, WIN_H)) {
                        float tx = te.x - (SHX + 4), ty = te.y - (SHY + 4) + gs.v[0] - TOP_PAD;
                        float left = (WIN_W - (COLS * STEP - 14)) / 2.0f;
                        int col = (int)((tx - left) / STEP), r = (int)(ty / STEP);
                        if (tx >= left && ty >= 0 && fmodf(tx - left, STEP) < CELL && fmodf(ty, STEP) < CELL && col < COLS && r < sh.rows && r * COLS + col < sh.count) {
                            sel = r * COLS + col;
                            cell_xy(sel, &sx, &sy);
                            hops_to(&hf, sx, sy);
                            press_a = 1;
                        }
                    }
                }
            }
        }
        if (b & (BTN_L | BTN_R | BTN_ZL | BTN_ZR)) { // the cards, as the Backups page steps through its tiles: L and R one, ZL and ZR several
            int by = (b & BTN_L) ? -1 : (b & BTN_R) ? 1 : (b & BTN_ZL) ? -SKIP : SKIP;
            int np = (int)clampf((float)(pos + by), 0, (float)(x.n - 1));
            if (np != pos) want_pos = np;
            else shoulder_dx = by < 0 ? -1 : 1; // (no card that way: the card leans)
        } else if (b & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) { // off an edge, round to the other end of the row or the column (held too)
            int dx = (b & BTN_LEFT) ? -1 : (b & BTN_RIGHT) ? 1 : 0, dy = dx ? 0 : (b & BTN_UP) ? -1 : 1;
            int to = ui_grid_nav(sel, sh.count, COLS, dx, dy);
            if (to >= 0) sel = to;
            else {
                pdx = dx;
                pdy = dy;
            }
        } else if (b & BTN_X) { // no picture: the letter, the console card's first cell
            if (pos != 0) want_pos = 0;
            to_letter = 1;
        } else if (b & BTN_R3) {
            want_sort = x.ng >= 2;
        } else if (b & BTN_A) {
            press_a = sh.count > 0;
        } else if (b & BTN_B) {
            result = 0;
        }
        if (result >= 0) break;
        if (want_sort) {
            int pick = ui_sort_menu(SORTS, 3, g_sort, CHIP_X, CHIP_Y, CHIP_W, 48, 0);
            if (pick >= 0 && pick != g_sort) {
                int g = x.ord[pos];
                csh_end();
                g_csh.from = malloc(sizeof(float) * (size_t)x.n);
                if (g_csh.from) // (where each card is now: the shuffle starts there)
                    for (int p = 0; p < x.n; p++) g_csh.from[x.ord[p] + 1] = BX + (p - row.x) * PITCH;
                g_sort = pick;
                make_order(&x);
                pos = pos_of(&x, g);
                mo_snap(&row, (float)row_for(&x, pos), 1.0f / PITCH);
                hops_snap(&hp, (float)pos, 0);
                render_base(&x);
                g_csh.t0 = ui_now();
                g_csh.first = (int)floorf(row.x);
                ui_sound(SND_RIFFLE, 0.42f, 0); // (cards being shuffled)
            }
            last = ui_now();
        }
        if (want_pos >= 0 && want_pos != pos) {
            uint64_t t = ui_now();
            float gap = (float)(t - pressed_at) / 1000.0f, run = !G.plat->repeated ? 0.0f : gap > 0.06f && gap < 0.16f ? gap : 0.1f;
            pressed_at = t;
            ui_lift_sfx(0, G.plat->repeated != 0, LIFT_TILE); // (the focus moves like a lift: the one that had it sticks back down, the new one peels up)
            ui_lift_sfx(1, G.plat->repeated != 0, LIFT_TILE);
            pos = want_pos;
            int game = x.ord[pos];
            sheet_clear(&old);
            old = sh; // (it fades out where it was)
            old_scroll = scroll;
            memset(&sh, 0, sizeof sh);
            sheet_make(&sh, &x, game, *picture);
            thumbs_drop_wants(); // (the old card's rows ahead aren't wanted now; the cards' pictures still are)
            want_faces(&x);
            sel = to_letter ? 0 : sh.inuse >= 0 ? sh.inuse : 0;
            cell_xy(sel, &sx, &sy);
            hops_snap(&hf, sx, sy);
            glide_snap(&gs, clampf(sy - 12, 0, sheet_max(&sh)), 0, 0, 0);
            fade_t0 = ui_now();
            mo_run(&row, (float)row_for(&x, pos), MO_PRESS_OM, MO_PRESS_ZETA, run);
            hops_to(&hp, (float)pos, 0);
            paper_spr_free(thumb);
            thumb = NULL;
            lean_card = 0;
        } else if (to_letter) {
            sel = 0;
        } else if (shoulder_dx) {
            lean_card = 1;
            bump_start(&bump, shoulder_dx, 0);
        } else if (pdx || pdy) {
            lean_card = 0;
            bump_start(&bump, pdx, pdy);
        }
        {
            float nx, ny;
            cell_xy(sel, &nx, &ny);
            if (nx != sx || ny != sy) {
                sx = nx;
                sy = ny;
                hops_to(&hf, sx, sy);
            }
        }
        if (!thumb) thumb = thumb_new(&sh, &thumb_len);
        if (press_a && sh.count > 0) { // the picture goes flat for a moment before it is chosen (the cells round it stay as they are)
            float cx0 = SHX + 4 + sx, cy0 = SHY + 4 + sy - gs.v[0];
            Rect rr = {(int)cx0 - 8, (int)cy0 - 8, CELL + 16, CELL + 16};
            Rect nb[8];
            int nn = 0;
            for (int i = 0; i < sh.count && nn < 8; i++) {
                int dr = i / COLS - sel / COLS, dc = i % COLS - sel % COLS;
                if (i == sel || dr < -1 || dr > 1 || dc < -1 || dc > 1) continue;
                float ix, iy;
                cell_xy(i, &ix, &iy);
                nb[nn++] = (Rect){(int)(SHX + 4 + ix) - 4, (int)(SHY + 4 + iy - gs.v[0]) - 4, CELL + 8, CELL + 8};
            }
            ui_press_keep(nb, nn);
            ui_press(G.work, rr);
            *picture = sh.ids[sel];
            result = 1;
        }
    }
    thumbs_drop_wants();
    cspr_free(&sp);
    sheet_clear(&sh);
    sheet_clear(&old);
    paper_spr_free(thumb);
    csh_end();
    free(x.ord);
    return result;
}
