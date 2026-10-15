// Backups: one row of tiles that slides sideways as a whole - "Back up now" first, then the backups made by hand pinned as starred
// tiles (three show in full and the next peeks in from the screen's edge, with a round button on it) - and the automatic ones as rows
// underneath. Each shows the profile pictures of the people in it, when it was made and what it holds.
//
// The row works like one of the reminder's wheels laid on its side: the tile in focus is brought to the middle of the three slots
// (at either end of the row, where the tiles can go no further, it sits in an outer slot instead). The frame is on that tile and
// goes wherever the tile goes; when the focus moves it hops to the next tile, as on a grid. A press pulls the row along with a
// spring: L and R and the D-pad one tile, ZL and ZR two, Y back to "Back up now"; a held D-pad runs it at an even speed. A finger
// drags it and it stays under the finger; flicked, it runs on, slowing, and comes to rest with a tile in the middle; pulled past
// an end it gives a little and springs back; a finger on it while it runs stops it. The page scrolls the same way. X deletes the backup in focus, a press that leads nowhere leans. Every item is drawn once into a small cached picture
// (with its shadow) and the page only moves those around, so sliding and scrolling stay cheap.
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "ui_motion.h"
#include "ui_scene.h"
#include "ui_spr.h"

#define BX 190.0f      // the column's left edge: "Back up now" starts here
#define BW 900.0f      // the rows' width
#define TILE_W 280.0f
#define TILE_H 176.0f
#define PITCH 310.0f   // from one tile to the next
#define PEEK_FADE 1100.0f // from here to the screen's right edge the peeking tile fades out (and mirrored on the left: 0 to BX)
#define SHOWN 3        // tiles (Back up now counts) that show in full: the row's three slots
#define MID 1          // the slot the frame keeps to while the row can slide under it
#define SKIP 2         // what ZL and ZR skip
#define ROW_H 66.0f
#define ROW_PY 74.0f
#define TOP 192.0f      // where the content starts on the screen (scroll 0)
#define VIEW_TOP 172.0f // what is shown of it: from here ...
#define VIEW_BOT 638.0f // ... to here (the footer's rule is just under)
#define FADE 14
#define BTN_R_ 28.0f    // the round < > buttons

// tests: the left edge of the tile that has the focus in the last frame composed (-1 when it is on a row, or on none)
int g_ui_backups_focus_x = -1;

typedef struct {
    int nman, naut, ntiles, overflow;
    float tiles_end, lab_gap, height;
} Geo;

static Geo geo_of(const UiBackups *b)
{
    Geo g;
    memset(&g, 0, sizeof g);
    for (int i = 0; i < b->n; i++) g.nman += b->items[i].manual != 0;
    g.naut = b->n - g.nman;
    g.ntiles = 1 + g.nman;
    g.overflow = g.ntiles > SHOWN;
    g.tiles_end = TILE_H;
    g.lab_gap = g.overflow ? 78.0f : 52.0f; // (a count line sits under the strip when it can slide)
    g.height = g.naut ? g.tiles_end + g.lab_gap + g.naut * ROW_PY - (ROW_PY - ROW_H) : g.tiles_end;
    return g;
}

// the rect of item `idx` (0 = Back up now, then the pinned tiles, then the automatic rows) in content coordinates; `sc` = how far
// the strip has slid, in tiles
static void item_rect(const Geo *g, int idx, float sc, float r[4])
{
    if (idx <= g->nman) {
        r[0] = BX + (idx - sc) * PITCH;
        r[1] = 0;
    } else {
        r[0] = BX;
        r[1] = g->tiles_end + g->lab_gap + (idx - 1 - g->nman) * ROW_PY;
        r[2] = BW;
        r[3] = ROW_H;
        return;
    }
    r[2] = TILE_W;
    r[3] = TILE_H;
}

static int is_tile(const Geo *g, int idx) { return idx <= g->nman; }

static float item_radius(const Geo *g, int idx) { return is_tile(g, idx) ? 20.0f : 16.0f; }

static float view_h(void) { return VIEW_BOT - TOP; }

// how far the row can slide, in tiles
static int row_most(const Geo *g) { return g->overflow ? g->ntiles - SHOWN : 0; }

// the slide that puts tile p (0 = Back up now) in the middle of the three, unless it is one of the ends of the row: then as near the
// middle as the row allows (the first tile on the left, the last on the right)
static int row_for(const Geo *g, int p)
{
    int s = p - MID;
    if (s > row_most(g)) s = row_most(g);
    return s < 0 ? 0 : s;
}

// how far the page can scroll, in pixels
static float page_most(const Geo *g)
{
    float max = g->height - view_h() + 24;
    return max < 0 ? 0 : max;
}

// where to scroll the page so item `idx` is in view (the first automatic row brings its heading)
static float scroll_for(const Geo *g, int idx, float cur)
{
    float r[4], max = page_most(g);
    if (is_tile(g, idx)) return 0;
    item_rect(g, idx, 0, r);
    float y0 = r[1] - 14, y1 = r[1] + r[3] + 14;
    if (idx == 1 + g->nman) y0 -= 38; // the heading
    float s = cur;
    if (y1 > s + view_h()) s = y1 - view_h();
    if (y0 < s) s = y0;
    if (s > max) s = max;
    if (s < 0) s = 0;
    return s;
}

#define LP D_PAD // the room round an item's card in its pictures
// a filled five-point star (the mark on the ones you made yourself)
static void star(Cx *c, float cx, float cy, float r, Col k)
{
    float px[10], py[10];
    for (int i = 0; i < 10; i++) {
        float a = -3.14159265f / 2 + i * 3.14159265f / 5, rr = i % 2 ? r * 0.46f : r;
        px[i] = cx + cosf(a) * rr;
        py[i] = cy + sinf(a) * rr;
    }
    for (int y = (int)(cy - r); y <= (int)(cy + r) + 1; y++) {
        float xs[10], fy = y + 0.5f;
        int n = 0;
        for (int i = 0; i < 10; i++) {
            int j = (i + 1) % 10;
            if ((py[i] <= fy && py[j] > fy) || (py[j] <= fy && py[i] > fy)) xs[n++] = px[i] + (fy - py[i]) / (py[j] - py[i]) * (px[j] - px[i]);
        }
        for (int a = 0; a < n; a++)
            for (int b = a + 1; b < n; b++)
                if (xs[b] < xs[a]) {
                    float t = xs[a];
                    xs[a] = xs[b];
                    xs[b] = t;
                }
        for (int a = 0; a + 1 < n; a += 2) gfx_rect(c->s, xs[a], (float)y, xs[a + 1] - xs[a], 1, k);
    }
    for (int i = 0; i < 10; i++) gfx_line(c->s, px[i], py[i], px[(i + 1) % 10], py[(i + 1) % 10], 1.2f, k);
}

static float rad(float deg) { return deg * 3.14159265f / 180.0f; }

#define PANEL PAL(patch, 0xECE9F1) // the soft patch the pictures sit on

// the people in a backup: round pictures overlapping, each with a ring of the patch's colour, on a soft patch
static void collage(Cx *c, float x, float y, float w, float h, const UiBackup *it, float r)
{
    gfx_rrect(c->s, x, y, w, h, 6, hex(PANEL, 1));
    int show = it->npeople;
    if (show <= 0) {
        skin_icon_person(c, x + w / 2, y + h / 2, r * 1.6f, hex(PAL(thread, PAPER_THREAD), 1));
        return;
    }
    float step = r * 1.7f, total = 2 * r + (show - 1) * step, sx = x + (w - total) / 2 + r, cy = y + h / 2;
    for (int i = 0; i < show; i++) {
        float ax = sx + i * step;
        gfx_circle(c->s, ax, cy, r + 3, hex(PANEL, 1));
        skin_avatar(c, ax, cy, r, it->name[i], it->look[i]);
    }
}

typedef struct {
    const UiBackups *b;
    const Geo *g;
    int idx;
    const PaperSpr *card;
    float w, h;
    uint32_t chip; // the tape colour of an automatic row's label
} BodyCtx;

// what is printed on the card of item idx, its top left at (LP, LP)
static void draw_body(Surf *s, void *ud)
{
    const BodyCtx *k = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    const UiBackups *b = k->b;
    const float x = LP, y = LP, w = k->w, h = k->h;
    const Col ink = hex(PAL(text, PAPER_TEXT), 1), soft = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);
    paper_spr_put(s, k->card, LP, LP);
    if (k->idx == 0) {
        gfx_circle(s, x + w / 2, y + 62, 34, hex(0xD7F4EA, 1));
        skin_icon_plus(&c, x + w / 2, y + 62, 28, 4.6f, hex(PAPER_INK, 1));
        skin_txtc(&c, u->semi, 25, -0.2f, x + w / 2, y + 122, 1, ink, "Back up now");
        skin_txtc(&c, u->med, 17, 0, x + w / 2, y + 150, 1, soft, "A copy of everything");
        return;
    }
    const UiBackup *it = &b->items[k->idx - 1];
    char fit[96];
    if (it->manual) {
        collage(&c, x + 14, y + 14, w - 28, 84, it, 26);
        text_fit(u->semi, 22, -0.1f, it->when, w - 36, fit, sizeof fit);
        skin_txt(&c, u->semi, 22, -0.1f, x + 18, y + 128, 0, ink, fit);
        char sub[96];
        snprintf(sub, sizeof sub, "%d profile%s \xC2\xB7 %s", it->nprofiles, it->nprofiles == 1 ? "" : "s", it->size);
        text_fit(u->med, 16.5f, 0, sub, w - 36, fit, sizeof fit);
        skin_txt(&c, u->med, 16.5f, 0, x + 18, y + 154, 0, soft, fit);
    } else {
        collage(&c, x + 12, y + 8, 116, h - 16, it, 17);
        text_fit(u->semi, 21, -0.1f, it->when, 440, fit, sizeof fit);
        skin_txt(&c, u->semi, 21, -0.1f, x + 150, y + 29, 0, ink, fit);
        text_fit(u->med, 16.5f, 0, it->what, 440, fit, sizeof fit);
        skin_txt(&c, u->med, 16.5f, 0, x + 150, y + 51, 0, soft, fit);
        skin_txtc(&c, u->med, 17, 0, x + w - 240, y + h / 2, 2, soft, it->size);
        for (float dy = y + 9; dy < y + h - 10; dy += 8) gfx_line(s, x + w - 214, dy, x + w - 214, dy + 4.5f, 1.8f, hex(PAL(thread, PAPER_THREAD), 0.85f));
        gfx_rrect(s, x + w - 194, y + h / 2 - 16, 160, 32, 8, hex(k->chip, 1));
        skin_txtc(&c, u->semi, 17, 0, x + w - 114, y + h / 2 + 0.5f, 1, hex(PAPER_TEXT, 1), "Autosave"); // (on a pastel chip: dark in Paper night too)
    }
}

static const int KINDS[4] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG};

// One item at (x, y), as a picture of its own with its shadow and tape: lying on the page (a quiet shadow, the tape stuck down, turned a
// hair this way or that) or, `lifted`, unstuck (it has risen 7 px off its shadow, which is deep, and the tape is peeling).
static void draw_item(Cx *c, const UiBackups *b, const Geo *g, int idx, float x, float y, int lifted)
{
    float r[4];
    item_rect(g, idx, 0, r);
    float w = r[2], h = r[3], rr = is_tile(g, idx) ? 6.0f : 6.0f;
    int tile = is_tile(g, idx);
    BodyCtx k = {b, g, idx, card_get((int)w, (int)h, rr, 70u + (uint32_t)(idx & 7), LP), w, h, paper_tape_rgb[(idx * 3 + 3) & 7]};
    PaperSpr *body = paper_bake((int)w + 2 * LP, (int)h + 2 * LP, draw_body, &k);
    const PaperShadows *sh = shadows_get((int)w, (int)h, rr, 0.0f);
    static const float TILTS[4] = {-0.7f, 0.8f, -0.6f, 0.9f};
    float tilt = tile ? rad(TILTS[idx & 3]) : 0.0f, rise = lifted ? 7.0f : 0.0f; // (the rows lie straight: the page scrolls them by pixels)
    if (sh) paper_spr_put(c->s, sh->s[lifted ? sh->n - 1 : 0], (int)x, (int)y);
    float cx = x + w * 0.5f, cy = y + h * 0.5f - rise, sc = lifted ? 1.012f : 1.0f;
    if (body) paper_spr_xform(c->s, body, cx, cy, sc, tilt, 1.0f);
    // Tape lightly pressed on: a tile has a short strip across each top corner (it hangs over the edge on both sides), a row one across its left end.
    // Each is put on by hand: no two lie alike (off the corner, crooked, cut differently), a few have a ridge or an end that did not take.
    PaperTape tp2[2];
    float tdx[2], tdy[2], stood[2];
    int nt = tile ? 2 : 1;
    if (tile) {
        float bx[2] = {-12.0f, w - 42.0f}, by[2] = {1.0f, 1.0f};
        tp2[0] = (PaperTape){54, 20, paper_tape_rgb[idx == 0 ? 4 : (idx + 5) & 7], KINDS[(idx + 1) & 3], PAPER_TAPE_ZIG, -40.0f, 300u + (uint32_t)idx};
        tp2[1] = (PaperTape){54, 20, paper_tape_rgb[(idx + 3) & 7], KINDS[(idx + 3) & 3], PAPER_TAPE_ZIG, 40.0f, 340u + (uint32_t)idx};
        for (int i = 0; i < 2; i++) {
            stood[i] = paper_tape_hand(&tp2[i], &bx[i], &by[i], (uint32_t)(idx * 2 + i) * 7919u + 11u, 1.0f);
            paper_tape_offset(w, h, bx[i], by[i], (float)tp2[i].len, (float)tp2[i].wid, &tdx[i], &tdy[i]);
            paper_tape_host(&tp2[i], w, h, rr, bx[i], by[i]);
        }
    } else {
        float bx = -12.0f, by = -7.0f;
        tp2[0] = (PaperTape){52, 20, paper_tape_rgb[(idx * 3 + 3) & 7], KINDS[idx & 3], PAPER_TAPE_ZIG, -24.0f, 300u + (uint32_t)idx};
        stood[0] = paper_tape_hand(&tp2[0], &bx, &by, (uint32_t)idx * 7919u + 5u, 0.8f);
        paper_tape_offset(w, h, bx, by, (float)tp2[0].len, (float)tp2[0].wid, &tdx[0], &tdy[0]);
        paper_tape_host(&tp2[0], w, h, rr, bx, by);
    }
    for (int i = 0; i < nt; i++) {
        PaperXf cf = {cx, cy, sc, tilt}, tp = paper_xf_attach(cf, tdx[i], tdy[i]);
        if (lifted || stood[i] > 0.0f) {
            PaperPeel *pl = paper_tape_peel_lazy(&tp2[i], PAPER_PEEL_STATES);
            const PaperSpr *ts = pl ? paper_peel_at(pl, lifted ? 0.85f : stood[i]) : NULL;
            if (ts) paper_spr_xform(c->s, ts, tp.x, tp.y, tp.scale, tp.angle, 1.0f);
            paper_peel_free(pl);
        } else {
            PaperSpr *ts = paper_tape(&tp2[i]);
            if (ts) paper_spr_xform(c->s, ts, tp.x, tp.y, tp.scale, tp.angle, 1.0f);
            paper_spr_free(ts);
        }
    }
    paper_spr_free(body);
}

// a round "<" or ">" button: a paper disc
static void round_btn(Cx *c, float cx, float cy, int left)
{
    gfx_shadow(c->s, cx - BTN_R_, cy - BTN_R_, BTN_R_ * 2, BTN_R_ * 2, BTN_R_, 3, 8, pal_disc_shadow(0.30f));
    gfx_circle(c->s, cx, cy, BTN_R_, hex(PAL(paper, PAPER_PAPER), 1));
    gfx_ring(c->s, cx, cy, BTN_R_ - 0.5f, 1.0f, pal_light(g_pal.paper) ? col(0, 0, 0, 0.10f) : col(1, 1, 1, 0.08f));
    float s = BTN_R_ * 0.32f, d = left ? 1 : -1;
    Col ink = hex(PAL(text, PAPER_TEXT), 1);
    gfx_line(c->s, cx + d * s * 0.5f, cy - s, cx - d * s * 0.55f, cy, 3.4f, ink);
    gfx_line(c->s, cx - d * s * 0.55f, cy, cx + d * s * 0.5f, cy + s, 3.4f, ink);
}

static const Prompt PROMPTS[] = {{"Y", "Back up now"}, {"X", "Delete"}, {"A", "Choose"}, {"B", "Back"}};

static void render_base(const UiBackups *b, const Geo *g)
{
    (void)b;
    (void)g;
    scene_chrome(G.base, "Backups", NULL, PROMPTS, 4);
}

// the label over the automatic rows: a small paper tag, baked when first wanted
static PaperSpr *g_label;

typedef struct {
    const PaperSpr *card;
} LabelBake;

static void draw_label(Surf *s, void *ud)
{
    const LabelBake *l = ud;
    Cx c = ui_cx(s);
    const PaperShadows *sh = shadows_get(168, 36, 6.0f, 0.0f);
    if (sh) paper_spr_put(s, sh->s[0], 30, 30);
    paper_spr_put(s, l->card, 30, 30);
    skin_txtc(&c, c.skin->semi, 17, 3.0f, 30 + 84, 30 + 18.5f, 1, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), "AUTOMATIC");
}

static const PaperSpr *label_get(void)
{
    if (!g_label) {
        LabelBake lb = {card_get(168, 36, 6.0f, 88u, 30)};
        g_label = paper_bake(168 + 2 * 30, 36 + 2 * 30, draw_label, &lb);
    }
    return g_label;
}

// ---- cached pictures of the items. An item is drawn twice, on black and on white, and the difference gives each pixel's coverage, so
// the picture (card, shadow and all) can be laid over the page anywhere. Most of an item is solid: those runs are copied as they are.

enum { SPR_PREV = -1, SPR_NEXT = -2 }; // what a picture is of, when it is not an item: one of the round buttons

static Spr *g_spr, *g_lift; // the pictures of the items lying on the page, and (made when wanted) of the one that is up
static int g_nspr;
static Spr g_btn[2];
static unsigned g_frame;

static void spr_free_all(void)
{
    for (int i = 0; i < g_nspr; i++) {
        spr_free_one(&g_spr[i]);
        if (g_lift) spr_free_one(&g_lift[i]);
    }
    paper_spr_free(g_label);
    g_label = NULL;
    spr_free_one(&g_btn[0]);
    spr_free_one(&g_btn[1]);
    free(g_spr);
    free(g_lift);
    g_spr = g_lift = NULL;
    g_nspr = 0;
}

typedef struct {
    const UiBackups *b;
    const Geo *g;
    int idx, lifted;
} SprJob;

static void spr_draw(Cx *c, void *ud, float x, float y)
{
    const SprJob *j = ud;
    if (j->idx >= 0) draw_item(c, j->b, j->g, j->idx, x, y, j->lifted);
    else round_btn(c, x + BTN_R_, y + BTN_R_, j->idx == SPR_PREV);
}

static void spr_build(Spr *s, const UiBackups *b, const Geo *g, int idx, int lifted)
{
    float r[4] = {0, 0, BTN_R_ * 2, BTN_R_ * 2};
    if (idx >= 0) item_rect(g, idx, 0, r);
    SprJob j = {b, g, idx, lifted};
    spr_bake(s, (int)r[2], (int)r[3], spr_draw, &j);
}

// total bytes held; past this, pictures not wanted this frame are let go (a long row of pinned tiles)
#define SPR_BUDGET (14 * 1024 * 1024)

static void spr_ensure(const UiBackups *b)
{
    if (g_spr) return;
    g_nspr = b->n + 1;
    g_spr = calloc((size_t)g_nspr, sizeof *g_spr);
    g_lift = calloc((size_t)g_nspr, sizeof *g_lift);
}

static const Spr *spr_get(const UiBackups *b, const Geo *g, int idx)
{
    spr_ensure(b);
    Spr *s = &g_spr[idx];
    if (!s->px) {
        size_t held = 0;
        for (int i = 0; i < g_nspr; i++) held += (size_t)g_spr[i].w * g_spr[i].h * 4;
        if (held > SPR_BUDGET)
            for (int i = 0; i < g_nspr; i++)
                if (g_spr[i].px && g_spr[i].used != g_frame) spr_free_one(&g_spr[i]);
        spr_build(s, b, g, idx, 0);
    }
    s->used = g_frame;
    return s;
}

// the picture of item idx unstuck
static const Spr *spr_get_lift(const UiBackups *b, const Geo *g, int idx)
{
    spr_ensure(b);
    Spr *s = &g_lift[idx];
    if (!s->px) spr_build(s, b, g, idx, 1);
    s->used = g_frame;
    return s;
}

// While nothing moves: draws the picture of the nearest item to the anchor that has none yet, so the next slide or scroll doesn't have to.
// 1 = one was drawn (call again after a look at the buttons), 0 = all the near ones are there.
static int spr_prefetch(const UiBackups *b, const Geo *g, int anchor)
{
    spr_ensure(b);
    size_t held = 0;
    for (int i = 0; i < g_nspr; i++) held += (size_t)g_spr[i].w * g_spr[i].h * 4;
    if (held > SPR_BUDGET / 2) return 0;
    for (int d = 0; d <= 6; d++)
        for (int k = 0; k < 2; k++) {
            int i = k ? anchor + d : anchor - d;
            if (i < 0 || i >= g_nspr || (k && !d) || g_spr[i].px) continue;
            spr_build(&g_spr[i], b, g, i, 0);
            g_spr[i].used = g_frame;
            return 1;
        }
    return 0;
}

// One item on the page at (x, y); `focus` (0..1) is how much of the focus is on it: an item with the focus is unstuck, so the patch it
// left on the page shows under it and it comes up off that, its tape peeling. The two pictures of it cross over as the focus arrives.
static void put_item(Cx *c, const UiBackups *b, const Geo *g, int idx, float x, float y, float focus)
{
    int xi = (int)lroundf(x), yi = (int)lroundf(y);
    float r[4];
    item_rect(g, idx, 0, r);
    if (focus <= 0.004f) {
        spr_blit(c->s, spr_get(b, g, idx), xi, yi, 256);
        if (is_tile(g, idx) && idx > 0 && b->items[idx - 1].manual) star(c, xi + r[2] - 28, yi + 22, 9.5f, hex(0xF2A900, 1));
        return;
    }
    const PaperSpr *gh = ghost_get((int)r[2], (int)r[3], 7.0f);
    if (gh) paper_spr_xform(c->s, gh, xi + r[2] * 0.5f, yi + r[3] * 0.5f, 1.0f, 0.0f, focus);
    if (focus < 0.996f) spr_blit(c->s, spr_get(b, g, idx), xi, yi, (int)((1.0f - focus) * 256.0f));
    spr_blit(c->s, spr_get_lift(b, g, idx), xi, yi, focus >= 0.996f ? 256 : (int)(focus * 256.0f));
    if (is_tile(g, idx) && idx > 0 && b->items[idx - 1].manual) star(c, xi + r[2] - 28, yi + 22, 9.5f, hex(0xF2A900, 1)); // (a sticker, put on straight: it stays put)
}

// a round button at (cx, cy), k (0..1) as strong: it fades away as the row comes to that end
static void put_btn(Cx *c, const UiBackups *b, const Geo *g, int which, float cx, float cy, float k)
{
    if (k < 0.02f) return;
    Spr *s = &g_btn[which == SPR_PREV ? 0 : 1];
    if (!s->px) spr_build(s, b, g, which, 0);
    spr_blit(c->s, s, (int)lroundf(cx - BTN_R_), (int)lroundf(cy - BTN_R_), k >= 1.0f ? 256 : (int)(k * 256.0f));
}

// how much of the frame is on item idx (0..1): Hops holds the items the frame is on, or is fading from, in its x
static float focus_on(const Hops *hp, int idx)
{
    float f = 0;
    for (int k = 0; k < hp->n; k++)
        if ((int)lroundf(hp->x[k]) == idx) f += hops_strength(hp->a[k]);
    return f > 1.0f ? 1.0f : f;
}

// scroll = how far the page has scrolled (pixels), sc = how far the row has slid (tiles); the item in focus and its frame are
// drawn (ox, oy) off their places (the lean)
static void compose(const UiBackups *b, const Geo *g, float scroll, float sc, const Hops *hp, int sel, float ox, float oy)
{
    g_frame++;
    surf_copy(G.work, G.base);
    Cx c = ui_cx(G.work);
    const Theme *t = &c.skin->th;
    float band_y = TOP - scroll; // the row of tiles' top on the screen

    // 1. the row of tiles (Back up now and the pinned ones) with the frame on its slot, then its ends fade out into the page at the
    //    screen's edges
    float best = 0.3f;
    g_ui_backups_focus_x = -1;
    for (int i = 0; i <= g->nman; i++) {
        float r[4];
        item_rect(g, i, sc, r);
        if (r[0] + r[2] < -40 || r[0] > SCREEN_W) continue;
        float f = focus_on(hp, i);
        if (f > best) {
            best = f;
            g_ui_backups_focus_x = (int)lroundf(r[0] + (i == sel ? ox : 0));
        }
        put_item(&c, b, g, i, r[0] + (i == sel ? ox : 0), band_y + (i == sel ? oy : 0), f);
    }
    float most = (float)row_most(g), past = sc < 0 ? sc : sc > most ? sc - most : 0; // (past: how far the row is beyond an end)
    if (g->overflow) {
        int by0 = (int)(band_y - 34), by1 = (int)(band_y + TILE_H + 40);
        ground_cols(G.work, G.base, by0, by1, 0, (int)BX, 2);
        ground_cols(G.work, G.base, by0, by1, (int)PEEK_FADE, SCREEN_W, 1);
        float cy = band_y + TILE_H / 2;
        put_btn(&c, b, g, SPR_PREV, SCREEN_W - 1160.0f, cy, sc * 2.0f); // (gone by the time the row is at that end)
        put_btn(&c, b, g, SPR_NEXT, 1160, cy, (most - sc) * 2.0f);
        char cnt[96];
        int first = (int)lroundf(sc - past);
        int last = first + SHOWN - 1 > g->nman ? g->nman : first + SHOWN - 1;
        if (first < 1) first = 1;
        snprintf(cnt, sizeof cnt, "%d \xE2\x80\x93 %d of %d pinned", first, last, g->nman);
        skin_txtc(&c, c.skin->med, 16, 0, BX + (SHOWN * PITCH - (PITCH - TILE_W)) / 2, band_y + TILE_H + 28, 1, t->text3, cnt);
    }

    // 2. the automatic rows, with their heading
    if (g->naut) {
        float ly = band_y + g->tiles_end + g->lab_gap - 20;
        if (ly > VIEW_TOP && ly < VIEW_BOT + 20 && label_get()) paper_spr_put(G.work, label_get(), (int)BX + 4 - 30, (int)(ly - 27) - 30);
        for (int i = 1 + g->nman; i <= b->n; i++) {
            float r[4];
            item_rect(g, i, sc, r);
            float sy = TOP + r[1] - scroll;
            if (sy > VIEW_BOT + 30 || sy + r[3] < VIEW_TOP - 30) continue;
            put_item(&c, b, g, i, r[0] + (i == sel ? ox : 0), sy + (i == sel ? oy : 0), focus_on(hp, i));
        }
    }
    // what runs past the window goes back under the title and the footer, and its edges fade into the page instead of cutting
    const Surf *s = G.base;
    size_t row = (size_t)G.work->w * 4;
    memcpy(G.work->px, s->px, row * (size_t)VIEW_TOP);
    memcpy(G.work->px + row * (size_t)VIEW_BOT, s->px + row * (size_t)VIEW_BOT, row * (size_t)(G.work->h - (int)VIEW_BOT));
    for (int k = 0; k < FADE; k++) {
        float keep = (k + 0.5f) / FADE;
        fade_row(G.work, s, (int)VIEW_TOP + k, keep);
        fade_row(G.work, s, (int)VIEW_BOT - 1 - k, keep);
    }
}

// the tile (0 = Back up now, then the pinned ones) a step of `by` from `from`, or -1 if there is none that way
static int tile_step(const Geo *g, int from, int by)
{
    int to = from + by;
    if (to > g->nman) to = g->nman;
    if (to < 0) to = 0;
    return to == from ? -1 : to;
}

// How letting go of the row and of the page behaves (ui_motion.h; the row is measured in tiles, the page in pixels). A flick of the
// row is good for 0.3 s of its speed, and the glide onto the tile that takes it to lasts 0.3 s plus 0.07 s a tile at most.
static const Throw ROW_THROW = {0.30f, 1.3f, 1.0f, 0.30f, 0.07f, MO_PRESS_OM, MO_PRESS_ZETA};
static const Throw PAGE_THROW = {0.30f, 80.0f, 0.0f, 0.66f, 0.0f, MO_PRESS_OM, MO_PRESS_ZETA};
#define ROW_ROOM 3.0f    // the most an end of the row gives to a pull (tiles)
#define PAGE_ROOM 400.0f // and an end of the page (pixels)
#define GRAB 14.0f       // how far a finger goes before it is a drag and not a tap (ui_core.c)

// where the row will be once it has come to rest: a whole number of tiles
static int row_rest(const Geo *g, const Mover *row)
{
    int f = (int)lroundf(row->mode == MO_HELD ? row->x : row->tgt);
    return f < 0 ? 0 : f > row_most(g) ? row_most(g) : f;
}

static float page_rest(const Geo *g, const Mover *page)
{
    float y = page->mode == MO_HELD ? page->x : page->tgt;
    return y < 0 ? 0 : y > page_most(g) ? page_most(g) : y;
}

// 1 if half of item idx or more shows with the page scrolled this far
static int in_view(const Geo *g, int idx, float scroll)
{
    float r[4];
    item_rect(g, idx, 0, r);
    float mid = TOP + r[1] - scroll + r[3] / 2;
    return mid > VIEW_TOP && mid < VIEW_BOT;
}

// what the focus goes to when a finger has scrolled the item it was on out of view: the nearest one that still shows
static int shown_near(const UiBackups *b, const Geo *g, int sel, int last_tile, float scroll)
{
    if (in_view(g, sel, scroll)) return sel;
    int best = in_view(g, 0, scroll) ? last_tile : -1;
    for (int i = 1 + g->nman; i <= b->n; i++)
        if (in_view(g, i, scroll) && (best < 0 || abs(i - sel) < abs(best - sel))) best = i;
    return best < 0 ? sel : best;
}

int ui_backups(UiBackups *b)
{
    if (G.quit) return 0;
    Geo g = geo_of(b);
    if (b->sel < 0) b->sel = 0;
    if (b->sel > b->n) b->sel = b->n;
    int sel = b->sel, last_tile = is_tile(&g, sel) ? sel : 0;
    const float most = (float)row_most(&g), deep = page_most(&g);
    // the row as it was left, if the tile in focus is one of the three showing there; else with that tile in the middle
    int first = b->first < 0 ? 0 : b->first > row_most(&g) ? row_most(&g) : b->first;
    if (is_tile(&g, sel) && (sel < first || sel >= first + SHOWN)) first = row_for(&g, sel);
    spr_free_all();
    render_base(b, &g);
    ui_transition();

    Mover row, page; // how far the row of tiles has slid (tiles), and how far the page has scrolled (pixels)
    int home = first; // where the row rests, or is heading, as the focus has it (a finger that takes it elsewhere takes the focus along)
    mo_snap(&row, (float)first, 1.0f / PITCH);
    mo_snap(&page, scroll_for(&g, sel, 0), 1.0f);
    Hops hp;
    memset(&hp, 0, sizeof hp);
    hops_snap(&hp, (float)sel, 0);
    Bump bump = {0};
    // the finger: what it has hold of; whether landing stopped something that was moving; where it has dragged what it holds to
    // (before an end gives); and its last few places, for its speed when it lifts
    enum { F_UP, F_DOWN, F_ROW, F_PAGE, F_OFF };
    int finger = F_UP, stopped = 0;
    float raw = 0, down_y = 0;
    Track tx, ty;
    memset(&tx, 0, sizeof tx);
    memset(&ty, 0, sizeof ty);
    uint64_t last = ui_now(), pressed_at = 0;
    int result = -1;
    for (;;) {
        uint64_t now = ui_now();
        float dt = now > last ? (float)(now - last) / 1000.0f : 0.0f;
        last = now;
        if (dt > 0.05f) dt = 0.05f; // a slow frame never leaps
        int moving = mo_step(&row, dt);
        moving |= mo_step(&page, dt);
        moving |= hops_step(&hp, dt);
        float ox, oy;
        moving |= bump_offset(&bump, &ox, &oy);
        compose(b, &g, page.x, row.x, &hp, sel, (float)lroundf(ox), (float)lroundf(oy));
        ui_present(G.work);
        if (result >= 0) break;

        // (while a finger holds something the page keeps drawing, so what it holds stays under it; idle, the neighbours' pictures
        // are got ready, with a look at the buttons in between)
        int busy = moving || finger == F_ROW || finger == F_PAGE || row.mode == MO_HELD || page.mode == MO_HELD;
        int more = !busy && spr_prefetch(b, &g, sel);
        uint64_t in = ui_poll(busy || more ? 0 : -1);
        if (!busy && !more) last = ui_now() - 16; // (it was waiting: what starts now starts a frame in, not catching up on the wait)
        if (in == UI_QUIT) {
            result = 0;
            break;
        }
        if (in & UI_REDRAW) {
            spr_free_all(); // (the theme changed: the pictures are the old colours)
            render_base(b, &g);
            in &= ~UI_REDRAW;
        }
        int leave = 0, to = -1, nowhere = 0, pdx = 0, pdy = 0, shoulder = 0;
        if (in & UI_TOUCH) {
            UiTouchEvt te;
            in &= ~UI_TOUCH;
            int row_let_go = 0, page_moved = 0, row_to = -1;
            while (ui_touch_next(&te)) {
                uint64_t t = ui_now();
                float band_y = TOP - page.x;
                if (te.kind == TOUCH_DOWN) {
                    finger = te.y >= VIEW_TOP && te.y < VIEW_BOT ? F_DOWN : F_OFF;
                    stopped = 0;
                    down_y = te.y;
                    track_start(&tx, t, te.x);
                    track_start(&ty, t, te.y);
                    if (finger == F_OFF) continue;
                    // a finger landing on what is moving stops it where it is (and its tap then chooses nothing)
                    if (te.y >= band_y - 10 && te.y < band_y + TILE_H + 10 && mo_moving(&row) && (fabsf(row.x - row.tgt) > 0.03f || fabsf(row.v) > 0.5f)) {
                        mo_hold(&row);
                        stopped = 1;
                    }
                    if (mo_moving(&page) && (fabsf(page.x - page.tgt) > 8.0f || fabsf(page.v) > 100.0f)) {
                        mo_hold(&page);
                        stopped = 1;
                    }
                } else if (te.kind == TOUCH_DRAG) {
                    float dx = te.dx, dy = te.dy;
                    if (finger == F_DOWN) { // its first move says what it takes hold of: the row (sideways, on the row), or the page
                        finger = down_y >= band_y - 10 && down_y < band_y + TILE_H + 10 && fabsf(dx) > fabsf(dy) ? F_ROW : F_PAGE;
                        if (finger == F_ROW) {
                            if (page.mode == MO_HELD) mo_throw(&page, 0, &PAGE_THROW, 0, deep); // (it had only stopped the page)
                            mo_hold(&row);
                            raw = mo_unband(row.x, 0, most, ROW_ROOM);
                        } else {
                            if (row.mode == MO_HELD) mo_throw(&row, 0, &ROW_THROW, 0, most);
                            mo_hold(&page);
                            raw = mo_unband(page.x, 0, deep, PAGE_ROOM);
                        }
                        // (the way it took to tell a drag from a tap doesn't count, so nothing jumps when the drag begins)
                        float len = hypotf(dx, dy), k = len > GRAB ? 1.0f - GRAB / len : 0.0f;
                        dx *= k;
                        dy *= k;
                    }
                    if (finger == F_ROW) {
                        raw -= dx / PITCH;
                        row.x = mo_band(raw, 0, most, ROW_ROOM);
                        track_add(&tx, t, te.x);
                    } else if (finger == F_PAGE) {
                        raw -= dy;
                        page.x = mo_band(raw, 0, deep, PAGE_ROOM);
                        track_add(&ty, t, te.y);
                    }
                } else if (te.kind == TOUCH_END) { // let go: it runs on at the finger's speed and comes to rest on a tile
                    // (past an end it was only following part of the finger's way: it has that part of the finger's speed)
                    if (finger == F_ROW) {
                        mo_throw(&row, -track_speed(&tx, t) / PITCH * mo_band_rate(raw, 0, most, ROW_ROOM), &ROW_THROW, 0, most);
                        row_let_go = 1;
                    } else if (finger == F_PAGE) {
                        mo_throw(&page, -track_speed(&ty, t) * mo_band_rate(raw, 0, deep, PAGE_ROOM), &PAGE_THROW, 0, deep);
                        page_moved = 1;
                    }
                    finger = F_UP;
                } else if (te.kind == TOUCH_TAP) {
                    int was = finger, had_stopped = stopped;
                    finger = F_UP;
                    stopped = 0;
                    if (row.mode == MO_HELD) mo_throw(&row, 0, &ROW_THROW, 0, most); // (it had stopped the row: that settles on the nearest tile)
                    if (page.mode == MO_HELD) mo_throw(&page, 0, &PAGE_THROW, 0, deep);
                    if (had_stopped || was == F_OFF || te.y < VIEW_TOP || te.y >= VIEW_BOT) continue;
                    float cy = band_y + TILE_H / 2;
                    int rest = row_rest(&g, &row);
                    // the round buttons bring the next tile, or the one before, to the middle
                    if (g.overflow && in_rect(te.x, te.y, 1160 - BTN_R_, cy - BTN_R_, BTN_R_ * 2, BTN_R_ * 2) && rest < row_most(&g)) {
                        row_to = rest + 1;
                        continue;
                    }
                    if (g.overflow && in_rect(te.x, te.y, SCREEN_W - 1160.0f - BTN_R_, cy - BTN_R_, BTN_R_ * 2, BTN_R_ * 2) && rest > 0) {
                        row_to = rest - 1;
                        continue;
                    }
                    for (int i = 0; i <= b->n; i++) {
                        float r[4];
                        item_rect(&g, i, row.x, r);
                        if (!in_rect(te.x, te.y, r[0], is_tile(&g, i) ? band_y : TOP + r[1] - page.x, r[2], r[3])) continue;
                        to = i;
                        // a row, or a tile that shows in full, is chosen; a tile peeking in at an edge is only brought into view
                        leave = !is_tile(&g, i) || (i >= rest && i < rest + SHOWN);
                    }
                }
            }
            if (row_to >= 0) mo_spring(&row, (float)row_to, MO_PRESS_OM, MO_PRESS_ZETA);
            // A finger has taken the row somewhere else (or sent it there): the tile in the middle slot, or the one that will be,
            // is the one in focus. Where it only gave and came back, nothing changes.
            if (g.overflow && to < 0 && row_rest(&g, &row) != home) {
                home = row_rest(&g, &row);
                sel = last_tile = home + MID;
                hops_to(&hp, (float)sel, 0);
            }
            if ((row_let_go || row_to >= 0) && is_tile(&g, sel)) mo_spring(&page, scroll_for(&g, sel, page_rest(&g, &page)), MO_PRESS_OM, MO_PRESS_ZETA);
            // the page scrolled by a finger: the focus goes to something that still shows
            if (page_moved && to < 0) {
                int near = shown_near(b, &g, sel, last_tile, page_rest(&g, &page));
                if (near != sel) {
                    sel = near;
                    if (is_tile(&g, sel)) last_tile = sel;
                    hops_to(&hp, (float)sel, 0);
                }
            }
        }
        if (leave) {
            sel = to; // (tapped)
        } else if (to < 0) {
            int from = is_tile(&g, sel) ? sel : last_tile;
            int by = 0;
            if (in & (BTN_LEFT | BTN_L)) by = -1;
            else if (in & (BTN_RIGHT | BTN_R)) by = 1;
            else if (in & BTN_ZL) by = -SKIP;
            else if (in & BTN_ZR) by = SKIP;
            if (by) {
                // Left and Right only move along the tiles; L, R, ZL and ZR also bring the focus up from the rows
                int tiles_only = in & (BTN_LEFT | BTN_RIGHT) && !(in & (BTN_L | BTN_R | BTN_ZL | BTN_ZR));
                if (tiles_only && !is_tile(&g, sel)) {
                    nowhere = 1;
                    pdx = by < 0 ? -1 : 1;
                } else {
                    int t2 = tile_step(&g, from, by);
                    if (t2 < 0) {
                        nowhere = 1;
                        pdx = by < 0 ? -1 : 1;
                    } else {
                        to = t2;
                        shoulder = !tiles_only; // (the shoulder buttons swing the row in with a bounce)
                    }
                }
            } else if (in & BTN_DOWN) {
                if (is_tile(&g, sel) && g.naut) to = 1 + g.nman;
                else if (!is_tile(&g, sel) && sel < b->n) to = sel + 1;
                else if (!is_tile(&g, sel) && wrap_ok()) to = last_tile; // (below the last row: back up to the tiles)
                else nowhere = 1, pdy = 1;
            } else if (in & BTN_UP) {
                if (!is_tile(&g, sel)) to = sel > 1 + g.nman ? sel - 1 : last_tile;
                else if (g.naut && wrap_ok()) to = b->n; // (above the tiles: round to the last row)
                else nowhere = 1, pdy = -1;
            } else if (in & BTN_Y) {
                to = 0; // straight to Back up now
            } else if (in & BTN_X) {
                if (sel >= 1) leave = 2; // delete this one
                else nowhere = 1, pdy = 1; // (Back up now can't be deleted)
            } else if (in & BTN_A) {
                leave = 1;
            } else if (in & BTN_B) {
                result = 0;
                break;
            }
        }
        if (leave) { // one more frame, with everything at rest and the frame on what was picked
            home = row_rest(&g, &row);
            if (is_tile(&g, sel) && (sel < home || sel >= home + SHOWN)) home = row_for(&g, sel);
            mo_snap(&row, (float)home, row.px);
            mo_snap(&page, scroll_for(&g, sel, page_rest(&g, &page)), page.px);
            hops_snap(&hp, (float)sel, 0);
            result = leave;
        } else if (to >= 0 && to != sel) {
            // The focus moves: the row slides to bring its tile to the middle slot, the page scrolls to show its row. A held
            // D-pad's steps run together into one motion, each taking the time since the one before.
            uint64_t t = ui_now();
            float gap = (float)(t - pressed_at) / 1000.0f, run = !G.plat->repeated ? 0.0f : gap > 0.06f && gap < 0.16f ? gap : 0.1f;
            pressed_at = t;
            ui_lift_sfx(0, G.plat->repeated != 0, is_tile(&g, sel) ? LIFT_TILE : LIFT_ROW); // (the focus moves like a lift: the one that had it sticks
            ui_lift_sfx(1, G.plat->repeated != 0, is_tile(&g, to) ? LIFT_TILE : LIFT_ROW);  // back down, the new one peels up, with their sounds)
            sel = to;
            if (is_tile(&g, sel)) last_tile = sel;
            if (row.mode == MO_HELD) mo_throw(&row, 0, &ROW_THROW, 0, most); // (a press takes things out of the finger's hand)
            if (page.mode == MO_HELD) mo_throw(&page, 0, &PAGE_THROW, 0, deep);
            if (finger != F_UP) finger = F_OFF;
            if (is_tile(&g, sel)) mo_run(&row, (float)row_for(&g, sel), shoulder ? MO_BUMP_OM : MO_PRESS_OM, shoulder ? MO_BUMP_ZETA : MO_PRESS_ZETA, run);
            mo_run(&page, scroll_for(&g, sel, page_rest(&g, &page)), MO_PRESS_OM, MO_PRESS_ZETA, run);
            home = row_rest(&g, &row);
            hops_to(&hp, (float)sel, 0);
        } else if (nowhere) {
            bump_start(&bump, pdx, pdy);
        }
    }
    if (result == 1 && !G.quit) { // A (or a tap) on it: the item pushes flat for a moment before what it does
        float r[4];
        item_rect(&g, sel, row.x, r);
        Rect rr = {(int)lroundf(r[0]), (int)lroundf(is_tile(&g, sel) ? TOP - page.x : TOP + r[1] - page.x), (int)r[2], (int)r[3]};
        Rect nb[4]; // (the rows above and below, their tape and shadow with them, stay as they are; so do the tiles beside a tile)
        int nn = 0;
        for (int i = sel - 2; i <= sel + 2 && !is_tile(&g, sel); i++) {
            if (i == sel || i <= g.nman || i > g.nman + g.naut) continue;
            float r2[4];
            item_rect(&g, i, row.x, r2);
            nb[nn++] = (Rect){(int)lroundf(r2[0]) - 20, (int)lroundf(TOP + r2[1] - page.x) - 14, (int)r2[2] + 40, (int)r2[3] + 24};
        }
        for (int i = sel - 1; i <= sel + 1 && is_tile(&g, sel); i += 2) {
            if (i < 0 || i > g.nman) continue;
            float r2[4];
            item_rect(&g, i, row.x, r2);
            nb[nn++] = (Rect){(int)lroundf(r2[0]) - 6, (int)lroundf(TOP - page.x) - 6, (int)r2[2] + 12, (int)r2[3] + 12};
        }
        ui_press_keep(nb, nn);
        const Rect tape_zone = {rr.x - 24, rr.y - 30, rr.w + 48, 36}; // (its own tape stands up from its top edge, and goes in with it: the rows above do not keep it still)
        ui_press_free(&tape_zone, 1);
        ui_press_whole((Rect){rr.x - SPR_M, rr.y - SPR_M, rr.w + 2 * SPR_M, rr.h + 2 * SPR_M}); // (its tape and its deep shadow are in the sprite's margin)
        const PaperSpr *gh = ghost_get(rr.w, rr.h, 7.0f); // (the dashed patch where it sat stays put, and what the lifted card covers of it stays covered)
        if (gh) {
            ui_press_ghost(gh, rr.x + rr.w * 0.5f, rr.y + rr.h * 0.5f, 0.0f, 1.0f);
            ui_press_cover((Rect){rr.x + 2, rr.y + 2, rr.w - 4, rr.h - 4});
        }
        ui_press(G.work, rr);
    }
    b->sel = sel;
    b->first = row_rest(&g, &row);
    spr_free_all();
    ui_screen_done();
    return result;
}
