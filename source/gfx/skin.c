#include "skin.h"

#include "paper.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265f
#define PANEL_RIM hex(g_pal.panel_rim, g_pal.panel_rim_a) // the faint line round a flat panel (the footer's strip, the status tag)

// A flat panel (the footer's strip, the status tag) made of the cards' own paper instead, when the palette asks for it (Paper night); 1 if it drew one.
static int paper_panel(Surf *s, float x, float y, float w, float h, float r)
{
    static struct { int w, h; PaperSpr *spr; } cache[6];
    static unsigned serial;
    if (!g_pal.panels_paper) return 0;
    if (paper_pal_stale(&serial)) { // (the palette changed: the paper did)
        for (int i = 0; i < 6; i++) paper_spr_free(cache[i].spr);
        memset(cache, 0, sizeof cache);
    }
    int iw = (int)(w + 0.5f), ih = (int)(h + 0.5f), k;
    for (k = 0; k < 6 && cache[k].spr; k++)
        if (cache[k].w == iw && cache[k].h == ih) break;
    if (k == 6) k = 5;
    if (!cache[k].spr || cache[k].w != iw || cache[k].h != ih) {
        paper_spr_free(cache[k].spr);
        cache[k].spr = paper_card_ex(iw, ih, r, 0x5151u + (uint32_t)iw, 14);
        cache[k].w = iw;
        cache[k].h = ih;
    }
    if (cache[k].spr) paper_spr_put(s, cache[k].spr, (int)(x + 0.5f), (int)(y + 0.5f));
    return cache[k].spr != NULL;
}

#ifdef WASHI_PAL_LAB
// mockups only: old writing on the chalkboard, rubbed out: drawn faintly in chalk, then smeared sideways where the eraser went (the board's own grain
// smears with it)
static void smear_x(Surf *s, int x0, int y0, int x1, int y1, int rad)
{
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    int n = x1 - x0;
    if (n <= 0) return;
    uint8_t *row = malloc((size_t)n * 4);
    if (!row) return;
    for (int y = y0; y < y1; y++) {
        uint8_t *p = s->px + ((size_t)y * s->w + x0) * 4;
        memcpy(row, p, (size_t)n * 4);
        for (int x = 0; x < n; x++)
            for (int c = 0; c < 3; c++) {
                int sum = 0, cnt = 0;
                for (int d = -rad; d <= rad; d++)
                    if (x + d >= 0 && x + d < n) sum += row[(x + d) * 4 + c], cnt++;
                p[x * 4 + c] = (uint8_t)(sum / cnt);
            }
    }
    free(row);
}

static void chalk_ghosts(Surf *s, Font *f)
{
    static const struct { float px, x, y, a; const char *t; } G[] = {
        {34, 716, 532, 0.09f, "Smash night  Fri 7pm"}, {26, 742, 584, 0.07f, "bring your own controller"}, {30, 728, 632, 0.065f, "GG  ||||  ||||  ||"},
        {40, 372, 64, 0.055f, "sign out when done!"},
    };
    for (size_t i = 0; i < sizeof G / sizeof G[0]; i++) {
        float tw = text_width(f, G[i].px, 0, G[i].t);
        text_draw(s, f, G[i].px, 0, G[i].x, G[i].y, 0, col(0.90f, 0.91f, 0.88f, G[i].a), G[i].t);
        smear_x(s, (int)G[i].x - 10, (int)(G[i].y - G[i].px), (int)(G[i].x + tw) + 10, (int)(G[i].y + G[i].px * 0.35f), 3);
    }
}
// mockups only: old printing on the cardboard, faded and rubbed: drawn, then worn back towards the board in patches
static unsigned box_hash(int x, int y)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static void box_text(Surf *s, Font *f, float px, float x, float y, Col k, const char *t)
{
    int x0 = (int)x - 4, y0 = (int)(y - px), x1 = (int)(x + text_width(f, px, px * 0.08f, t)) + 4, y1 = (int)(y + px * 0.3f);
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x1 <= x0 || y1 <= y0) return;
    size_t n = (size_t)(x1 - x0) * (y1 - y0) * 4;
    uint8_t *keep = malloc(n);
    if (!keep) return;
    for (int yy = y0; yy < y1; yy++) memcpy(keep + (size_t)(yy - y0) * (x1 - x0) * 4, s->px + ((size_t)yy * s->w + x0) * 4, (size_t)(x1 - x0) * 4);
    text_draw(s, f, px, px * 0.08f, x, y, 0, k, t);
    for (int yy = y0; yy < y1; yy++)
        for (int xx = x0; xx < x1; xx++) {
            // worn: fine scuffs and broader rubbed patches where the box was handled
            float fine = (box_hash(xx, yy) & 255) / 255.0f, broad = (box_hash(xx / 9, yy / 7) & 255) / 255.0f;
            float back = fine < 0.22f ? 0.85f : broad < 0.3f ? 0.55f : 0.0f;
            uint8_t *o = s->px + ((size_t)yy * s->w + xx) * 4;
            const uint8_t *b = keep + ((size_t)(yy - y0) * (x1 - x0) + (xx - x0)) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)(o[c] + (b[c] - o[c]) * back + 0.5f);
        }
    free(keep);
}

static void box_arrow(Surf *s, float x, float y, float h, Col k)
{
    gfx_line(s, x, y + h, x, y, 4.0f, k);
    gfx_line(s, x - h * 0.32f, y + h * 0.34f, x, y, 4.0f, k);
    gfx_line(s, x + h * 0.32f, y + h * 0.34f, x, y, 4.0f, k);
}

static void box_print(Surf *s, Font *f)
{
    Col ink = col(0.10f, 0.07f, 0.05f, 0.40f), red = col(0.62f, 0.17f, 0.13f, 0.45f);
    box_arrow(s, 384, 30, 44, ink);
    box_arrow(s, 410, 30, 44, ink);
    box_text(s, f, 30, 432, 66, ink, "THIS SIDE UP");
    box_text(s, f, 52, 716, 548, red, "FRAGILE");
    box_text(s, f, 22, 720, 588, ink, "HANDLE WITH CARE");
    box_text(s, f, 18, 722, 630, ink, "NU ESPORTS  BOX 3 OF 4");
}
// mockups only: what a game table has printed into its felt, in gold: two arcs over the empty places, and the night's name
static void felt_print(Surf *s, Font *f)
{
    Col gold = col(0.82f, 0.68f, 0.35f, 0.70f);
    gfx_arc(s, 940, 900, 400, 2.2f, 3.86f, 5.56f, gold);
    gfx_arc(s, 940, 900, 391, 1.0f, 3.86f, 5.56f, gold);
    text_draw(s, f, 30, 30 * 0.18f, 318, 62, 0, gold, "GAME NIGHT");
    text_draw(s, f, 16, 16 * 0.30f, 566, 58, 0, gold, "NU ESPORTS");
}
// mockups only: the passport page's visa stamps, each pressed a little crooked in its own ink, worn as stamps are
static void ink_to(PaperSpr *p, uint32_t rgb)
{
    for (int i = 0; p && i < p->w * p->h; i++) {
        p->px[i * 4] = (rgb >> 16) & 255;
        p->px[i * 4 + 1] = (rgb >> 8) & 255;
        p->px[i * 4 + 2] = rgb & 255;
    }
}

static void visa(Cx *c, float x, float y, int w, int h, float size, const char *word, uint32_t rgb, float deg, uint32_t seed)
{
    PaperSpr *st = skin_stamp(c, w, h, size, word, seed, PAL(ink, PAPER_INK));
    if (!st) return;
    ink_to(st, rgb);
    paper_spr_xform(c->s, st, x, y, 1.0f, deg * (PI_F / 180.0f), 0.85f);
    paper_spr_free(st);
}

typedef struct { Font *f; const char *top, *mid; uint32_t rgb; } RoundStamp;

static void round_stamp_draw(Surf *s, void *ud)
{
    const RoundStamp *b = ud;
    float cx = s->w * 0.5f, cy = s->h * 0.5f;
    Col k = hex(b->rgb, 1);
    gfx_ring(s, cx, cy, cx - 3, 3.0f, k);
    gfx_ring(s, cx, cy, cx - 9, 1.2f, k);
    text_draw(s, b->f, 11, 0.6f, cx, cy - 5, 1, k, b->top);
    text_draw(s, b->f, 20, 1.0f, cx, cy + 16, 1, k, b->mid);
}

static void passport_print(Surf *s, Font *reg, Font *med, Font *semi)
{
    Skin sk = {reg, med, semi, theme_make(0)};
    Cx c = {&sk, s};
    visa(&c, 412, 50, 150, 44, 22, "ENTRY", 0x6DB585, -7, 3);
    RoundStamp rs = {semi, "GAME NIGHT", "NU", 0xA391E6};
    PaperSpr *ring = paper_bake(96, 96, round_stamp_draw, &rs);
    if (ring) {
        ring->ox = ring->oy = 48;
        paper_stamp_weather(ring, 21, 0.8f);
        paper_spr_xform(s, ring, 652, 50, 1.0f, 10.0f * (PI_F / 180.0f), 0.85f);
        paper_spr_free(ring);
    }
    visa(&c, 822, 545, 190, 52, 24, "ADMITTED", 0xE2685C, 6, 4);
    text_draw(s, semi, 16, 1.5f, 742, 592, 0, col(0.89f, 0.41f, 0.36f, 0.65f), "10 OCT 2026");
    visa(&c, 1060, 612, 180, 46, 21, "SMASH CUP", 0x86A2F0, -5, 5);
    text_draw(s, semi, 14, 1.0f, 1152, 506, 0, col(0.85f, 0.72f, 0.42f, 0.55f), "07");
}

// mockups only: the club room's plan drawn on the blueprint, in white: its walls, a door, the TV and the Switch's dock, the couch, the snacks, the title
// block, and a title and a dimension along the top
static Font *g_bp_font;

static void bp_text(Surf *s, float px, float x, float y, int align, const char *t)
{
    text_draw(s, g_bp_font, px, px * 0.08f, x, y, align, col(0.95f, 0.97f, 1.0f, 0.82f), t);
}

static void blueprint_print(Surf *s, Font *f)
{
    g_bp_font = f;
    Col w = col(0.95f, 0.97f, 1.0f, 0.78f), w2 = col(0.95f, 0.97f, 1.0f, 0.55f);
    // along the top: the sheet's title and a dimension
    bp_text(s, 22, 322, 44, 0, "CLUB ROOM \xE2\x80\x94 PLAN");
    gfx_line(s, 322, 78, 770, 78, 1.2f, w2);
    gfx_line(s, 322, 70, 322, 86, 1.2f, w2);
    gfx_line(s, 770, 70, 770, 86, 1.2f, w2);
    gfx_line(s, 317, 83, 327, 73, 1.6f, w);
    gfx_line(s, 765, 83, 775, 73, 1.6f, w);
    bp_text(s, 14, 546, 72, 1, "24'-0\"");
    // the wall along the top of the room, broken for the door
    for (int k = 0; k < 2; k++) {
        float y = 488.0f + k * 6.0f;
        gfx_line(s, 690, y, 1010, y, 2.0f, w);
        gfx_line(s, 1060, y, 1185, y, 2.0f, w);
    }
    gfx_line(s, 1010, 494, 1010, 544, 1.6f, w); // the door and its swing
    gfx_arc(s, 1010, 494, 50, 1.0f, 0.0f, PI_F * 0.5f, w2);
    // the TV on the wall, the Switch's dock under it
    gfx_rrect_stroke(s, 720, 500, 140, 10, 1, 1.6f, w);
    gfx_rrect_stroke(s, 772, 514, 30, 12, 2, 1.4f, w);
    gfx_line(s, 802, 520, 838, 532, 1.0f, w2);
    bp_text(s, 12, 866, 510, 0, "TV");
    bp_text(s, 12, 842, 537, 0, "SWITCH (DOCK)");
    // the couch, facing the TV
    gfx_rrect_stroke(s, 720, 572, 170, 54, 6, 1.6f, w);
    gfx_rrect_stroke(s, 720, 612, 170, 14, 3, 1.4f, w);
    gfx_rrect_stroke(s, 720, 572, 14, 54, 3, 1.4f, w);
    gfx_rrect_stroke(s, 876, 572, 14, 54, 3, 1.4f, w);
    gfx_line(s, 777, 572, 777, 612, 1.0f, w2);
    gfx_line(s, 833, 572, 833, 612, 1.0f, w2);
    bp_text(s, 12, 805, 560, 1, "LOUNGE");
    // the snacks
    gfx_rrect_stroke(s, 930, 504, 44, 40, 2, 1.6f, w);
    gfx_line(s, 930, 504, 974, 544, 1.0f, w2);
    gfx_line(s, 974, 504, 930, 544, 1.0f, w2);
    bp_text(s, 12, 952, 560, 1, "SNACKS");
    // the title block
    gfx_rrect_stroke(s, 968, 584, 212, 62, 1, 1.6f, w);
    gfx_line(s, 968, 606, 1180, 606, 1.0f, w2);
    bp_text(s, 13, 976, 600, 0, "NU ESPORTS \xC2\xB7 CLUB ROOM");
    bp_text(s, 10, 976, 622, 0, "SHEET A-1   SCALE 1/4\" = 1'-0\"");
    bp_text(s, 10, 976, 638, 0, "DRAWN: WASHI   10.09.2026");
}
#endif

// ------------------------------------------------------------------------------------------------ theme and fonts

// The colours are the palette's (paper_pal_set: cream, or Paper night for the dark theme), so `dark` itself is not used: t.dark stays 0, the console's
// own flat dark look being gone. The cyan accent stays for the screens that have not been restyled.
Theme theme_make(int dark)
{
    (void)dark;
    Theme t;
    memset(&t, 0, sizeof t);
    t.dark = 0;
    t.bg_top = hex(PAL(board, PAPER_BOARD), 1); t.bg_bot = hex(PAL(board, PAPER_BOARD), 1); t.glow = hex(PAL(board, PAPER_BOARD), 0);
    t.panel = hex(PAL(paper, PAPER_PAPER), 1); t.panel_alt = hex(PAL(panel_alt, 0xF3EEE0), 1); t.line = hex(PAL(line, 0xDDD5C3), 1);
    t.text = hex(PAL(text, PAPER_TEXT), 1); t.text2 = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1); t.text3 = hex(PAL(thread, PAPER_THREAD), 1);
    t.accent = hex(0x00B8E6, 1); t.accent_text = hex(0x007FB0, 1); t.accent_soft = hex(0xDDF4FB, 1);
    t.danger = hex(0xE60012, 1); t.warn = hex(0xEE8A1E, 1);
    t.scrim = col(PAL3(scrim, 0, 0.945f), PAL3(scrim, 1, 0.922f), PAL3(scrim, 2, 0.867f), PAL3(scrim, 3, 0.62f)); // the board's cream, as a veil
    t.shadow = hex(PAL(shadow, PAPER_SHADOW), 1);                                                                 // warm brown
    t.sel = t.panel; t.plate = t.accent_soft; t.rule = t.line;
    return t;
}

static Font *g_reg, *g_med, *g_semi, *g_fallback;

void skin_set_fallback(Font *fallback)
{
    g_fallback = fallback;
    if (g_reg) font_set_fallback(g_reg, fallback);
    if (g_med) font_set_fallback(g_med, fallback);
    if (g_semi) font_set_fallback(g_semi, fallback);
}

void skin_init(Skin *s, int dark)
{
    if (!g_reg) {
        g_reg = font_load(font_regular_ttf);
        g_med = font_load(font_medium_ttf);
        g_semi = font_load(font_semibold_ttf);
        if (g_fallback) skin_set_fallback(g_fallback);
    }
    s->reg = g_reg;
    s->med = g_med;
    s->semi = g_semi;
    s->th = theme_make(dark);
}

// ------------------------------------------------------------------------------------------------ text

void skin_txt(Cx *c, Font *f, float px, float tracking, float x, float baseline, int align, Col k, const char *str)
{
    text_draw(c->s, f, px, tracking, x, baseline, align, k, str);
}

void skin_txtc(Cx *c, Font *f, float px, float tracking, float x, float cy, int align, Col k, const char *str)
{
    text_draw(c->s, f, px, tracking, x, cy + px * 0.34f, align, k, str);
}

// ------------------------------------------------------------------------------------------------ surfaces

#define BOARD_SEED 8u // the mockup's own noise seed: the fleck pattern is then the mockup's, to the pixel

// The ground every screen starts from: the pinboard, flecked as in the mockup (paper_board_fill). Made once and copied
// after that (it is the most expensive thing on a screen, and the same every time).
static const Surf *bg_cached(const Theme *t)
{
    (void)t;
    static Surf *cache;
    static int made, made_flat; // (a test may ask for a plain board, or for the real one back: then it is made again)
    static unsigned made_serial; // (and so is a new palette's)
    if (!cache) cache = surf_new(SCREEN_W, SCREEN_H);
    if (cache && (!made || made_flat != g_paper_board_flat || made_serial != paper_pal_serial)) {
        if (made && made_serial != paper_pal_serial) skin_ink_serial++; // (screens that keep a copy of the board take the new one)
        made_serial = paper_pal_serial;
        paper_board_fill(cache, BOARD_SEED);
#ifdef WASHI_PAL_LAB
        if (g_pal.board_chalk && !g_paper_board_flat && g_semi) chalk_ghosts(cache, g_semi);
        if (g_pal.board_box && !g_paper_board_flat && g_semi) box_print(cache, g_semi);
        if (g_pal.board_felt && !g_paper_board_flat && g_semi) felt_print(cache, g_semi);
        if (g_pal.board_passport && !g_paper_board_flat && g_semi) passport_print(cache, g_reg, g_med, g_semi);
        if (g_pal.board_blueprint && !g_paper_board_flat && g_semi) blueprint_print(cache, g_semi);
#endif
        made = 1;
        made_flat = g_paper_board_flat;
    }
    return cache;
}

// ---- ink on the board: a stamp that missed its paper lands on the board and stays under every screen until the app closes (nothing is saved)
enum { INK_MAX = 48 };
static struct { PaperSpr *spr; int x, y; } g_ink[INK_MAX];
static int g_nink;
unsigned skin_ink_serial; // changes whenever the board's ink does, for screens that keep a copy of the board

void skin_ink_add(PaperSpr *spr, int x, int y)
{
    if (!spr) return;
    if (g_nink == INK_MAX) { // the oldest goes
        paper_spr_free(g_ink[0].spr);
        memmove(g_ink, g_ink + 1, (INK_MAX - 1) * sizeof g_ink[0]);
        g_nink--;
    }
    g_ink[g_nink].spr = spr;
    g_ink[g_nink].x = x;
    g_ink[g_nink].y = y;
    g_nink++;
    skin_ink_serial++;
}

void skin_ink_clear(void)
{
    for (int i = 0; i < g_nink; i++) paper_spr_free(g_ink[i].spr);
    g_nink = 0;
    skin_ink_serial++;
}

void skin_board(Cx *c)
{
    const Surf *bg = c->s->w == SCREEN_W && c->s->h == SCREEN_H ? bg_cached(&c->skin->th) : NULL;
    if (bg) surf_copy(c->s, bg);
    else paper_board_fill(c->s, BOARD_SEED);
    for (int i = 0; i < g_nink; i++) paper_spr_put(c->s, g_ink[i].spr, g_ink[i].x, g_ink[i].y);
}

// ---- the stamp: a rubber stamp's ink impression, `word` in a double frame, in ink blue with wax grain (the mockup's "LOADED")
typedef struct {
    Cx *c;
    float size;
    const char *word;
    int w, h;
    uint32_t ink;
} StampText;

static void draw_stamp_text(Surf *s, void *ud)
{
    const StampText *t = ud;
    Cx c = {t->c->skin, s};
    skin_txtc(&c, t->c->skin->semi, t->size, t->size * 0.14f, t->w * 0.5f + t->size * 0.07f, t->h * 0.5f, 1, hex(t->ink, 1), t->word);
}

PaperSpr *skin_stamp(Cx *c, int w, int h, float size, const char *word, uint32_t seed, uint32_t ink)
{
    PaperSpr *res = paper_stamp_frame(w, h, seed, ink);
    StampText st = {c, size, word, w, h, ink};
    PaperSpr *tx = paper_bake(w, h, draw_stamp_text, &st);
    if (!res || !tx) {
        paper_spr_free(res);
        paper_spr_free(tx);
        return NULL;
    }
    for (int i = 0; i < w * h; i++) tx->px[i * 4 + 3] = (uint8_t)(tx->px[i * 4 + 3] * 0.92f + 0.5f); // (ink at the frame's strength)
    paper_spr_over(res, tx, 0, 0);
    paper_spr_free(tx);
    paper_stamp_weather(res, seed, h >= 40 ? 1.0f : 0.55f); // clean shapes, old ink: the same wear on frame and letters
    return res;
}

void skin_background(Cx *c)
{
    skin_board(c);
}

// A white card with the soft shadow the Switch puts under its tiles and dialogs. (The dark theme is flat: no shadows.)
void skin_card_shadow(Cx *c, float x, float y, float w, float h, float r, float lift, float k)
{
    const Theme *t = &c->skin->th;
    if (t->dark) return;
    float a = 0.14f * (1.0f + 0.45f * lift) * k;
    gfx_shadow(c->s, x, y, w, h, r, 4 + 8 * lift, 9 + 11 * lift, with_a(t->shadow, a));
}

void skin_card(Cx *c, float x, float y, float w, float h, float r, float lift)
{
    const Theme *t = &c->skin->th;
    skin_card_shadow(c, x, y, w, h, r, lift, 1.0f);
    gfx_rrect(c->s, x, y, w, h, r, t->panel);
    if (!t->dark) gfx_rrect_stroke(c->s, x + 0.5f, y + 0.5f, w - 1, h - 1, r, 1.0f, with_a(t->line, 0.60f));
}

// The same card drawn again, bigger, over its own resting copy: only the extra shadow of being picked up (growing with lift,
// 0..1) is added, so a tile can rise and settle again without its shadow popping in or doubling.
void skin_card_raised(Cx *c, float x, float y, float w, float h, float r, float lift)
{
    const Theme *t = &c->skin->th;
    if (lift > 0.004f && !t->dark) gfx_shadow(c->s, x, y, w, h, r, 4 + 8 * lift, 9 + 11 * lift, with_a(t->shadow, 0.14f * 1.45f * lift * lift));
    gfx_rrect(c->s, x, y, w, h, r, t->panel);
    if (!t->dark) gfx_rrect_stroke(c->s, x + 0.5f, y + 0.5f, w - 1, h - 1, r, 1.0f, with_a(t->line, 0.60f));
}

// a key: like a card, with a much lighter shadow so a field of keys stays clean (dark: an outline, as keys sit on a card)
void skin_key_card(Cx *c, float x, float y, float w, float h, float r)
{
    const Theme *t = &c->skin->th;
    if (!t->dark) gfx_shadow(c->s, x, y, w, h, r, 3, 5, with_a(t->shadow, 0.10f));
    gfx_rrect(c->s, x, y, w, h, r, t->panel);
    gfx_rrect_stroke(c->s, x + 0.5f, y + 0.5f, w - 1, h - 1, r, 1.0f, with_a(t->line, t->dark ? 1.0f : 0.85f));
}

// the line between rows: 2 px in the light theme, the console's 1 px in the dark one
void skin_hairline(Cx *c, float x, float y, float w)
{
    const Theme *t = &c->skin->th;
    gfx_rect(c->s, x, y, w, t->dark ? 1 : 2, with_a(t->line, 0.95f));
}

void skin_vline(Cx *c, float x, float y, float h)
{
    const Theme *t = &c->skin->th;
    gfx_rect(c->s, x, y, t->dark ? 1 : 2, h, with_a(t->line, 0.95f));
}

// The Switch's focus frame: a thick cyan line hugging the item, with a soft glow (the dark theme's is crisper, with hardly any
// glow, as the console draws it). The item is the rect passed in.
void skin_focus_a(Cx *c, float x, float y, float w, float h, float r, float k)
{
    if (k <= 0.004f) return;
    if (k > 1.0f) k = 1.0f;
    const Theme *t = &c->skin->th;
    if (t->dark) {
        gfx_halo(c->s, x - 5, y - 5, w + 10, h + 10, r + 5, 4, with_a(t->accent, 0.22f * k));
        gfx_rrect_stroke(c->s, x - 4.5f, y - 4.5f, w + 9, h + 9, r + 4.5f, 4.5f, with_a(t->accent, k));
        return;
    }
    gfx_halo(c->s, x - 4, y - 4, w + 8, h + 8, r + 4, 9, with_a(t->accent, 0.40f * k));
    gfx_rrect_stroke(c->s, x - 4, y - 4, w + 8, h + 8, r + 4, 4.0f, with_a(t->accent, k));
    gfx_rrect_stroke(c->s, x - 0.5f, y - 0.5f, w + 1, h + 1, r + 0.5f, 1.4f, with_a(t->panel, 0.9f * k));
}

// The fill inside the focus frame: the same place taken from `sel`, a copy of the screen with its items drawn on the theme's `sel`
// colour. Nothing to do when that is the card's own colour (the light theme).
void skin_focus_fill(Cx *c, const Surf *sel, float x, float y, float w, float h, float r, float k)
{
    skin_focus_fill_off(c, sel, x, y, w, h, r, k, 0, 0);
}

void skin_focus_fill_off(Cx *c, const Surf *sel, float x, float y, float w, float h, float r, float k, int dx, int dy)
{
    const Theme *t = &c->skin->th;
    if (!sel || k <= 0.004f || !memcmp(&t->sel, &t->panel, sizeof t->sel)) return;
    gfx_mix_card(c->s, sel, x, y, w, h, r, dx, dy, k);
}

void skin_erase(Cx *c, float x, float y, float w, float h, float r)
{
    const Surf *bg = c->s->w == SCREEN_W && c->s->h == SCREEN_H ? bg_cached(&c->skin->th) : NULL;
    if (bg) gfx_mix_card(c->s, bg, x - 1, y - 1, w + 2, h + 2, r + 1, 0, 0, 1.0f);
    else gfx_rrect(c->s, x - 1, y - 1, w + 2, h + 2, r + 1, c->skin->th.bg_top);
}

void skin_focus(Cx *c, float x, float y, float w, float h, float r)
{
    skin_focus_a(c, x, y, w, h, r, 1.0f);
}

void skin_dim(Cx *c)
{
    gfx_rect(c->s, 0, 0, SCREEN_W, SCREEN_H, c->skin->th.scrim);
}

void skin_dialog(Cx *c, float x, float y, float w, float h)
{
    skin_card(c, x, y, w, h, 26, 2.0f);
}

// ------------------------------------------------------------------------------------------------ avatars

Col skin_avatar_color(int index)
{
    int i = SKIN_LOOK_SWATCH(index) % SKIN_COLORS;
    if (i < 0) i += SKIN_COLORS;
    return hex(SWATCH_RGB[i], 1);
}

// the first character (UTF-8 aware) of a name, upper-cased if it is plain ASCII
static void first_glyph(const char *name, char *out, size_t cap)
{
    size_t n = 1;
    unsigned char b = (unsigned char)name[0];
    if (b >= 0xF0) n = 4;
    else if (b >= 0xE0) n = 3;
    else if (b >= 0xC0) n = 2;
    if (n >= cap) n = cap - 1;
    memcpy(out, name, n);
    out[n] = 0;
    if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 32);
}

// ---- the packed pictures
static const uint8_t *g_pack;
static size_t g_pack_size;
// A decoded picture and its smaller copies (each half the size of the one before), so a picture drawn small is read from a copy that
// is already about the right size instead of averaging the big one over and over every frame.
enum { PIC_CACHE = 40, PIC_LEVELS = 4 };
static struct {
    int idx;
    uint8_t *px[PIC_LEVELS];
    int w[PIC_LEVELS], h[PIC_LEVELS], levels;
    int full; // the first copy is the picture at full size (not its thumbnail)
    unsigned used;
} g_pic[PIC_CACHE];
static int g_pack_ver;
static unsigned g_pic_tick;
enum { MAX_PICS = 512 };
static uint16_t g_order[MAX_PICS], g_pos[MAX_PICS]; // grid position -> picture id, and back
static SkinPictureSource g_src; // when g_src.load is set the pictures come from it (a picture at a time), not from g_pack
static void *g_src_ctx;
static int g_src_n;
static uint8_t *(*g_custom)(void *ctx, int id); // pictures with numbers from SKIN_CUSTOM_BASE up come from this (256 x 256 RGBA), whatever else is set
static void *g_custom_ctx;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

// forget the decoded pictures
static void pics_flush(void)
{
    for (int i = 0; i < PIC_CACHE; i++) {
        for (int l = 0; l < PIC_LEVELS; l++) free(g_pic[i].px[l]);
        memset(&g_pic[i], 0, sizeof g_pic[i]);
        g_pic[i].idx = -1;
    }
}

void skin_set_custom_source(uint8_t *(*load)(void *ctx, int id), void *ctx)
{
    pics_flush(); // (the pictures behind the numbers may have changed)
    g_custom = load;
    g_custom_ctx = ctx;
}

// forget every picture: the decoded ones, the packed file and the other source (not the custom ones' source)
static void pics_reset(void)
{
    pics_flush();
    g_pack = NULL;
    g_pack_size = 0;
    g_pack_ver = 0;
    memset(&g_src, 0, sizeof g_src);
    g_src_ctx = NULL;
    g_src_n = 0;
}

void skin_set_avatar_source(const SkinPictureSource *src, void *ctx)
{
    pics_reset();
    if (!src || !src->count || !src->at || !src->load) return;
    int n = src->count(ctx);
    if (n <= 0 || n > MAX_PICS) return;
    unsigned char seen[MAX_PICS] = {0};
    int ok = 1;
    for (int pos = 0; pos < n && ok; pos++) {
        int id = src->at(ctx, pos);
        if (id < 0 || id >= n || seen[id]) ok = 0; // (the grid must show every picture once)
        else seen[id] = 1;
    }
    for (int pos = 0; pos < n; pos++) {
        int id = ok ? src->at(ctx, pos) : pos; // (otherwise the pictures' own order)
        g_order[pos] = (uint16_t)id;
        g_pos[id] = (uint16_t)pos;
    }
    g_src = *src;
    g_src_ctx = ctx;
    g_src_n = n;
}

void skin_set_avatar_pack(const uint8_t *data, size_t size)
{
    pics_reset();
    if (data && size >= 16 && !memcmp(data, "SLAV", 4) && rd32(data + 8) <= MAX_PICS) {
        uint32_t ver = rd32(data + 4), n = rd32(data + 8);
        size_t need = 16 + 8 * (size_t)n + (ver >= 2 ? 2 * (size_t)n : 0) + (ver == 3 ? 8 * (size_t)n : 0);
        if ((ver == 1 || ver == 2 || ver == 3) && size >= need) {
            g_pack = data;
            g_pack_size = size;
            g_pack_ver = (int)ver;
            for (uint32_t i = 0; i < n; i++) {
                uint32_t id = i; // version 1 had no order: the grid is the pack's own order
                if (ver >= 2) id = data[16 + 8 * (size_t)n + 2 * i] | (uint32_t)data[17 + 8 * (size_t)n + 2 * i] << 8;
                if (id >= n) id = i;
                g_order[i] = (uint16_t)id;
                g_pos[id] = (uint16_t)i;
            }
        }
    }
}

int skin_avatar_count(void)
{
    if (g_src.load) return g_src_n;
    return g_pack ? (int)rd32(g_pack + 8) : 0;
}

int skin_avatar_at(int pos)
{
    return pos >= 0 && pos < skin_avatar_count() ? g_order[pos] : -1;
}

int skin_avatar_pos(int id)
{
    return id >= 0 && id < skin_avatar_count() ? g_pos[id] : -1;
}

// half the size, averaging each 2x2 block with the transparent parts counting for nothing
static uint8_t *halve(const uint8_t *src, int w, int h, int *ow, int *oh)
{
    int nw = w / 2, nh = h / 2;
    uint8_t *out = malloc((size_t)nw * nh * 4);
    if (!out) return NULL;
    for (int y = 0; y < nh; y++)
        for (int x = 0; x < nw; x++) {
            unsigned acc[3] = {0, 0, 0}, asum = 0;
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    const uint8_t *p = src + ((size_t)(2 * y + j) * w + 2 * x + i) * 4;
                    for (int k = 0; k < 3; k++) acc[k] += (unsigned)p[k] * p[3];
                    asum += p[3];
                }
            uint8_t *o = out + ((size_t)y * nw + x) * 4;
            for (int k = 0; k < 3; k++) o[k] = asum ? (uint8_t)((acc[k] + asum / 2) / asum) : 0;
            o[3] = (uint8_t)((asum + 2) / 4);
        }
    *ow = nw;
    *oh = nh;
    return out;
}

// The slot holding picture i. Small pictures are served from its thumbnail (a fraction of the work to decode); a slot that holds
// only the thumbnail is filled in with the full picture the first time something wants it big.
static int pic_slot(int i, int want_full)
{
    int lru = 0, found = -1;
    for (int k = 0; k < PIC_CACHE; k++) {
        if (g_pic[k].px[0] && g_pic[k].idx == i) {
            found = k;
            break;
        }
        if (!g_pic[k].px[0]) lru = k; // an empty slot wins
        else if (g_pic[lru].px[0] && g_pic[k].used < g_pic[lru].used) lru = k;
    }
    if (found >= 0) {
        g_pic[found].used = ++g_pic_tick;
        if (!want_full || g_pic[found].full) return found;
        lru = found; // a thumbnail only: fill in the full picture
    }
    int pw = 0, ph = 0, thumb = 0;
    uint8_t *px;
    if (i >= SKIN_CUSTOM_BASE) {
        px = g_custom ? g_custom(g_custom_ctx, i) : NULL;
        pw = ph = 256;
    } else if (g_src.load) {
        px = g_src.load(g_src_ctx, i); // (always the whole 256 x 256 picture)
        pw = ph = 256;
    } else {
        thumb = !want_full && g_pack_ver >= 3;
        size_t tab = thumb ? 16 + 10 * (size_t)skin_avatar_count() : 16;
        uint32_t off = rd32(g_pack + tab + 8 * (size_t)i), len = rd32(g_pack + tab + 4 + 8 * (size_t)i);
        if ((size_t)off + len > g_pack_size) return -1;
        px = gfx_decode_png(g_pack + off, len, &pw, &ph);
    }
    if (!px) return -1;
    for (int l = 0; l < PIC_LEVELS; l++) {
        free(g_pic[lru].px[l]);
        g_pic[lru].px[l] = NULL;
    }
    g_pic[lru].px[0] = px;
    g_pic[lru].w[0] = pw;
    g_pic[lru].h[0] = ph;
    g_pic[lru].levels = 1;
    g_pic[lru].full = !thumb;
    while (g_pic[lru].levels < PIC_LEVELS) {
        int l = g_pic[lru].levels;
        if (g_pic[lru].w[l - 1] < 48 || g_pic[lru].h[l - 1] < 48) break;
        uint8_t *m = halve(g_pic[lru].px[l - 1], g_pic[lru].w[l - 1], g_pic[lru].h[l - 1], &g_pic[lru].w[l], &g_pic[lru].h[l]);
        if (!m) break;
        g_pic[lru].px[l] = m;
        g_pic[lru].levels++;
    }
    g_pic[lru].idx = i;
    g_pic[lru].used = ++g_pic_tick;
    return lru;
}

// is there a picture to be had under this number (not that it can be loaded)
static int pic_known(int i)
{
    if (i >= SKIN_CUSTOM_BASE) return g_custom != NULL;
    return (g_pack || g_src.load) && i >= 0 && i < skin_avatar_count();
}

const uint8_t *skin_avatar_rgba(int i, int *w, int *h)
{
    if (!pic_known(i)) return NULL;
    int k = pic_slot(i, 1);
    if (k < 0) return NULL;
    *w = g_pic[k].w[0];
    *h = g_pic[k].h[0];
    return g_pic[k].px[0];
}

// One of the console's pictures (not a custom one) decoded fresh, without the cache: for a worker thread making small copies (ui_thumbs.c). It only reads
// the source and the pack, so it is safe while they stay set; the packed file's 128 px thumbnail is enough for anything up to that size.
uint8_t *skin_avatar_load_rgba(int i, int *w, int *h)
{
    if (i < 0 || i >= SKIN_CUSTOM_BASE || i >= skin_avatar_count()) return NULL;
    if (g_src.load) {
        *w = *h = 256;
        return g_src.load(g_src_ctx, i);
    }
    if (!g_pack) return NULL;
    size_t tab = g_pack_ver >= 3 ? 16 + 10 * (size_t)skin_avatar_count() : 16;
    uint32_t off = rd32(g_pack + tab + 8 * (size_t)i), len = rd32(g_pack + tab + 4 + 8 * (size_t)i);
    if ((size_t)off + len > g_pack_size) return NULL;
    return gfx_decode_png(g_pack + off, len, w, h);
}

const uint8_t *skin_avatar_rgba_fit(int i, float size, int *w, int *h)
{
    if (!pic_known(i)) return NULL;
    int k = pic_slot(i, i >= SKIN_CUSTOM_BASE || g_src.load || g_pack_ver < 3 || size > (float)(rd32(g_pack + 12) / 2));
    if (k < 0) return NULL;
    int l = 0; // the smallest copy that is still at least as big as it will be drawn
    while (l + 1 < g_pic[k].levels && (float)g_pic[k].w[l + 1] >= size) l++;
    *w = g_pic[k].w[l];
    *h = g_pic[k].h[l];
    return g_pic[k].px[l];
}

// white letters on the darker swatches, dark ones on the pale
static Col letter_on(Col k)
{
    float lum = 0.299f * k.r + 0.587f * k.g + 0.114f * k.b;
    return lum > 0.62f ? hex(0x2A2F38, 1) : col(1, 1, 1, 1);
}

void skin_avatar_square(Cx *c, float x, float y, float size, const char *name, int look, float radius)
{
    Col k = skin_avatar_color(look);
    gfx_rrect(c->s, x, y, size, size, radius, k);
    int pic = SKIN_LOOK_PICTURE(look), pw = 0, ph = 0;
    if (pic >= 0) {
        const uint8_t *px = skin_avatar_rgba_fit(pic, size, &pw, &ph);
        if (px) {
            gfx_blit_rrect_scaled(c->s, px, pw, ph, x, y, size, size, radius);
            return;
        }
    }
    char one[8];
    first_glyph(name, one, sizeof one);
    skin_txtc(c, c->skin->semi, size * 0.52f, 0, x + size / 2, y + size / 2, 1, letter_on(k), one);
}

void skin_avatar(Cx *c, float cx, float cy, float rad, const char *name, int color_index)
{
    Col k = skin_avatar_color(color_index);
    gfx_circle(c->s, cx, cy, rad, k);
    int pic = SKIN_LOOK_PICTURE(color_index), pw = 0, ph = 0;
    if (pic >= 0) {
        const uint8_t *px = skin_avatar_rgba_fit(pic, 2 * rad, &pw, &ph);
        if (px) {
            gfx_blit_circle(c->s, px, pw, ph, cx, cy, rad, 1.0f);
            return;
        }
    }
    char one[8];
    first_glyph(name, one, sizeof one);
    skin_txtc(c, c->skin->semi, rad * 0.98f, 0, cx, cy, 1, letter_on(k), one);
}

void skin_game_monogram(Cx *c, float x, float y, float size, const char *name, int color_index)
{
    Col k = skin_avatar_color(color_index);
    gfx_rrect_grad(c->s, x, y, size, size, size * 0.23f, mixc(k, col(1, 1, 1, 1), 0.10f), mixc(k, col(0, 0, 0, 1), 0.10f));
    // initials of the first two words
    char ini[16] = {0};
    int n = 0, newword = 1;
    for (const char *p = name; *p && n < 2; p++) {
        if (*p == ' ') { newword = 1; continue; }
        if (newword && (unsigned char)*p < 0x80) {
            ini[n++] = (*p >= 'a' && *p <= 'z') ? (char)(*p - 32) : *p;
            newword = 0;
        } else if (newword) {
            newword = 0;
        }
    }
    if (n == 0) ini[0] = '?';
    skin_txtc(c, c->skin->semi, size * (n > 1 ? 0.34f : 0.42f), 0, x + size / 2, y + size / 2, 1, col(1, 1, 1, 1), ini);
}

// ------------------------------------------------------------------------------------------------ icons

void skin_icon_lock(Cx *c, float cx, float cy, float sz, Col k)
{
    gfx_rrect(c->s, cx - sz * 0.42f, cy - sz * 0.06f, sz * 0.84f, sz * 0.60f, sz * 0.15f, k);
    gfx_arc(c->s, cx, cy - sz * 0.10f, sz * 0.22f, sz * 0.11f, PI_F, PI_F * 2.0f, k);
    gfx_line(c->s, cx - sz * 0.22f, cy - sz * 0.10f, cx - sz * 0.22f, cy + sz * 0.02f, sz * 0.11f, k);
    gfx_line(c->s, cx + sz * 0.22f, cy - sz * 0.10f, cx + sz * 0.22f, cy + sz * 0.02f, sz * 0.11f, k);
}

void skin_icon_check(Cx *c, float cx, float cy, float sz, float w, Col k)
{
    gfx_line(c->s, cx - sz * 0.34f, cy + sz * 0.02f, cx - sz * 0.10f, cy + sz * 0.27f, w, k);
    gfx_line(c->s, cx - sz * 0.10f, cy + sz * 0.27f, cx + sz * 0.36f, cy - sz * 0.26f, w, k);
}

void skin_icon_info(Cx *c, float cx, float cy, float sz, Col k)
{
    gfx_ring(c->s, cx, cy, sz * 0.42f, sz * 0.09f, k);
    gfx_line(c->s, cx, cy - sz * 0.02f, cx, cy + sz * 0.20f, sz * 0.10f, k);
    gfx_circle(c->s, cx, cy - sz * 0.19f, sz * 0.065f, k);
}

void skin_icon_alert(Cx *c, float cx, float cy, float sz, Col k)
{
    gfx_line(c->s, cx, cy - sz * 0.30f, cx, cy + sz * 0.06f, sz * 0.13f, k);
    gfx_circle(c->s, cx, cy + sz * 0.27f, sz * 0.085f, k);
}

void skin_icon_chev(Cx *c, float cx, float cy, float sz, float w, Col k)
{
    gfx_line(c->s, cx - sz * 0.18f, cy - sz * 0.34f, cx + sz * 0.16f, cy, w, k);
    gfx_line(c->s, cx + sz * 0.16f, cy, cx - sz * 0.18f, cy + sz * 0.34f, w, k);
}

void skin_icon_person(Cx *c, float cx, float cy, float sz, Col k)
{
    gfx_circle(c->s, cx, cy - sz * 0.16f, sz * 0.20f, k);
    gfx_arc(c->s, cx, cy + sz * 0.50f, sz * 0.40f, sz * 0.16f, PI_F * 1.12f, PI_F * 1.88f, k);
}

void skin_icon_battery(Cx *c, float x, float cy, float level, Col k)
{
    if (level < 0) level = 0;
    if (level > 1) level = 1;
    gfx_rrect_stroke(c->s, x, cy - 7, 29, 14, 4, 1.7f, k);
    if (level > 0.03f) gfx_rrect(c->s, x + 3.2f, cy - 3.8f, 22.6f * level, 7.6f, 2, k);
    gfx_rrect(c->s, x + 30.4f, cy - 2.8f, 2.6f, 5.6f, 1.3f, k);
}

void skin_icon_plus(Cx *c, float cx, float cy, float sz, float w, Col k)
{
    gfx_line(c->s, cx - sz / 2, cy, cx + sz / 2, cy, w, k);
    gfx_line(c->s, cx, cy - sz / 2, cx, cy + sz / 2, w, k);
}

// ---- admin icons

// half of a disc, made of thin rows so its straight edge is clean (an arc that thick has round ends that bulge)
static void half_disc(Surf *d, float cx, float cy, float r, int left, Col c)
{
    for (float dy = -r; dy < r; dy += 0.5f) {
        float hw = sqrtf(r * r - (dy + 0.25f) * (dy + 0.25f));
        gfx_rect(d, left ? cx - hw : cx, cy + dy, hw, 0.5f, c);
    }
}

// a glyph as outlines, in the box of side s around (cx, cy), with lines w wide
static void admin_glyph(Surf *d, float cx, float cy, float s, int kind, Col ink, float w)
{
#define U(v) ((v) * s)
    switch (kind) {
    case SKIN_ICON_PERSON:
        gfx_ring(d, cx, cy - U(0.13f), U(0.13f), w, ink);
        gfx_arc(d, cx, cy + U(0.3f), U(0.25f), w, PI_F, PI_F * 2.0f, ink);
        gfx_line(d, cx - U(0.25f), cy + U(0.3f), cx + U(0.25f), cy + U(0.3f), w, ink);
        break;
    case SKIN_ICON_GROUP:
        gfx_ring(d, cx - U(0.1f), cy - U(0.12f), U(0.11f), w, ink);
        gfx_arc(d, cx - U(0.1f), cy + U(0.27f), U(0.22f), w, PI_F, PI_F * 2.0f, ink);
        gfx_arc(d, cx + U(0.14f), cy - U(0.14f), U(0.09f), w, -1.9f, 1.4f, ink);
        gfx_arc(d, cx + U(0.18f), cy + U(0.22f), U(0.17f), w, -2.4f, 0.0f, ink);
        break;
    case SKIN_ICON_TRASH:
        gfx_line(d, cx - U(0.25f), cy - U(0.18f), cx + U(0.25f), cy - U(0.18f), w, ink);
        gfx_rrect_stroke(d, cx - U(0.07f), cy - U(0.28f), U(0.14f), U(0.1f), U(0.03f), w, ink);
        gfx_rrect_stroke(d, cx - U(0.18f), cy - U(0.18f), U(0.36f), U(0.46f), U(0.06f), w, ink);
        gfx_line(d, cx - U(0.065f), cy - U(0.05f), cx - U(0.065f), cy + U(0.17f), w, ink);
        gfx_line(d, cx + U(0.065f), cy - U(0.05f), cx + U(0.065f), cy + U(0.17f), w, ink);
        break;
    case SKIN_ICON_LOCK:
        gfx_arc(d, cx, cy - U(0.06f), U(0.12f), w, PI_F, PI_F * 2.0f, ink);
        gfx_line(d, cx - U(0.12f), cy - U(0.06f), cx - U(0.12f), cy + U(0.01f), w, ink);
        gfx_line(d, cx + U(0.12f), cy - U(0.06f), cx + U(0.12f), cy + U(0.01f), w, ink);
        gfx_rrect_stroke(d, cx - U(0.22f), cy, U(0.44f), U(0.31f), U(0.07f), w, ink);
        gfx_circle(d, cx, cy + U(0.15f), w * 0.8f, ink);
        break;
    case SKIN_ICON_GAMES:
        gfx_rrect_stroke(d, cx - U(0.32f), cy - U(0.17f), U(0.64f), U(0.34f), U(0.17f), w, ink);
        gfx_line(d, cx - U(0.2f), cy, cx - U(0.1f), cy, w, ink);
        gfx_line(d, cx - U(0.15f), cy - U(0.05f), cx - U(0.15f), cy + U(0.05f), w, ink);
        gfx_circle(d, cx + U(0.12f), cy - U(0.02f), w * 0.75f, ink);
        gfx_circle(d, cx + U(0.2f), cy + U(0.04f), w * 0.75f, ink);
        break;
    case SKIN_ICON_DRIVE:
        gfx_rrect_stroke(d, cx - U(0.28f), cy - U(0.19f), U(0.56f), U(0.38f), U(0.08f), w, ink);
        gfx_line(d, cx - U(0.18f), cy + U(0.07f), cx + U(0.04f), cy + U(0.07f), w, ink);
        gfx_circle(d, cx + U(0.17f), cy + U(0.07f), w * 0.7f, ink);
        break;
    case SKIN_ICON_LOG:
        gfx_rrect_stroke(d, cx - U(0.21f), cy - U(0.28f), U(0.42f), U(0.56f), U(0.06f), w, ink);
        for (int i = 0; i < 3; i++)
            gfx_line(d, cx - U(0.11f), cy - U(0.12f) + i * U(0.13f), cx + U(i == 2 ? 0.0f : 0.11f), cy - U(0.12f) + i * U(0.13f), w, ink);
        break;
    case SKIN_ICON_CONSOLE:
        gfx_rrect_stroke(d, cx - U(0.33f), cy - U(0.2f), U(0.66f), U(0.4f), U(0.09f), w, ink);
        gfx_line(d, cx - U(0.2f), cy - U(0.2f), cx - U(0.2f), cy + U(0.2f), w, ink);
        gfx_line(d, cx + U(0.2f), cy - U(0.2f), cx + U(0.2f), cy + U(0.2f), w, ink);
        break;
    case SKIN_ICON_CLOCK:
        gfx_ring(d, cx, cy, U(0.27f), w, ink);
        gfx_line(d, cx, cy, cx, cy - U(0.15f), w, ink);
        gfx_line(d, cx, cy, cx + U(0.11f), cy + U(0.06f), w, ink);
        break;
    case SKIN_ICON_CONTRAST:
        gfx_ring(d, cx, cy, U(0.27f), w, ink);
        half_disc(d, cx, cy, U(0.24f), 1, ink);
        break;
    case SKIN_ICON_INBOX: { // a tray with a dip in its rim
        float l = cx - U(0.29f), r = cx + U(0.29f), rim = cy + U(0.03f), dip = cy + U(0.11f);
        gfx_rrect_stroke(d, l, cy - U(0.21f), U(0.58f), U(0.42f), U(0.08f), w, ink);
        gfx_line(d, l, rim, cx - U(0.12f), rim, w, ink);
        gfx_line(d, cx - U(0.12f), rim, cx - U(0.08f), dip, w, ink);
        gfx_line(d, cx - U(0.08f), dip, cx + U(0.08f), dip, w, ink);
        gfx_line(d, cx + U(0.08f), dip, cx + U(0.12f), rim, w, ink);
        gfx_line(d, cx + U(0.12f), rim, r, rim, w, ink);
        break;
    }
    case SKIN_ICON_INFO:
        gfx_ring(d, cx, cy, U(0.27f), w, ink);
        gfx_circle(d, cx, cy - U(0.12f), w * 0.7f, ink);
        gfx_line(d, cx, cy - U(0.04f), cx, cy + U(0.14f), w, ink);
        break;
    case SKIN_ICON_UPDATE: // an arrow down into a tray (a new version coming in)
        gfx_line(d, cx, cy - U(0.27f), cx, cy + U(0.05f), w, ink);
        gfx_line(d, cx - U(0.12f), cy - U(0.07f), cx, cy + U(0.05f), w, ink);
        gfx_line(d, cx + U(0.12f), cy - U(0.07f), cx, cy + U(0.05f), w, ink);
        gfx_line(d, cx - U(0.27f), cy + U(0.06f), cx - U(0.27f), cy + U(0.22f), w, ink);
        gfx_line(d, cx - U(0.27f), cy + U(0.22f), cx + U(0.27f), cy + U(0.22f), w, ink);
        gfx_line(d, cx + U(0.27f), cy + U(0.22f), cx + U(0.27f), cy + U(0.06f), w, ink);
        break;
    default:
        break;
    }
#undef U
}

void skin_admin_icon(Cx *c, float x, float y, float size, int kind, int hue)
{
    static const uint32_t TOP[SKIN_HUE_COUNT] = {0x38C7F5, 0xFFB34D, 0x6BD980, 0xA885FA};
    static const uint32_t BOT[SKIN_HUE_COUNT] = {0x0A99D6, 0xED801A, 0x26A652, 0x754DDB};
    static const uint32_t SOFT[SKIN_HUE_COUNT] = {0xDBF3FC, 0xFFEDD6, 0xDEF5E3, 0xEBE3FC};
    const Theme *t = &c->skin->th;
    if (hue < 0 || hue >= SKIN_HUE_COUNT) hue = 0;
    Col top = hex(TOP[hue], 1);
    // light: a pale tile with the glyph in the hue's deep tone; dark: a tile just lifted off the card, the glyph in the bright tone
    Col tile = t->dark ? mixc(t->panel_alt, top, 0.22f) : hex(SOFT[hue], 1);
    Col ink = t->dark ? top : mixc(top, hex(BOT[hue], 1), 0.85f);
    gfx_rrect(c->s, x, y, size, size, size * 0.28f, tile);
    if (kind == SKIN_ICON_CART) skin_logo_mark(c, x + size * 0.33f, y + size * 0.15f, size * 0.7f);
    else admin_glyph(c->s, x + size / 2, y + size / 2, size * 0.9f, kind, ink, size * 0.062f);
}

void skin_admin_glyph(Cx *c, float cx, float cy, float size, int kind, Col ink)
{
    admin_glyph(c->s, cx, cy, size * 0.9f, kind, ink, size * 0.062f);
}

// ------------------------------------------------------------------------------------------------ prompts

static int is_pill(const char *g) { return !strcmp(g, "ZL") || !strcmp(g, "ZR") || !strcmp(g, "L") || !strcmp(g, "R"); }

// "ZR+X": two buttons pressed together, drawn side by side (a lone "+" is the Plus button)
static const char *glyph_plus(const char *g) { return g[0] && g[1] ? strchr(g + 1, '+') : NULL; }

float skin_glyph_w(const char *g)
{
    const char *plus = glyph_plus(g);
    if (plus) {
        char a[8];
        snprintf(a, sizeof a, "%.*s", (int)(plus - g), g);
        return skin_glyph_w(a) + 6 + skin_glyph_w(plus + 1);
    }
    return is_pill(g) ? (strlen(g) > 1 ? 42.0f : 34.0f) : 30.0f;
}

void skin_glyph(Cx *c, float x, float cy, const char *g, Col ink)
{
    const char *plus = glyph_plus(g);
    if (plus) {
        char a[8];
        snprintf(a, sizeof a, "%.*s", (int)(plus - g), g);
        skin_glyph(c, x, cy, a, ink);
        skin_glyph(c, x + skin_glyph_w(a) + 6, cy, plus + 1, ink);
        return;
    }
    if (is_pill(g)) {
        float w = skin_glyph_w(g);
        gfx_rrect_stroke(c->s, x, cy - 12, w, 24, 8, 2.0f, ink);
        skin_txtc(c, c->skin->semi, 13.5f, 0, x + w / 2, cy, 1, ink, g);
        return;
    }
    float cx = x + 15;
    gfx_ring(c->s, cx, cy, 14, 2.1f, ink);
    if (!strcmp(g, "+")) {
        gfx_line(c->s, cx - 6, cy, cx + 6, cy, 2.2f, ink);
        gfx_line(c->s, cx, cy - 6, cx, cy + 6, 2.2f, ink);
    } else if (!strcmp(g, G_MINUS)) {
        gfx_line(c->s, cx - 6, cy, cx + 6, cy, 2.2f, ink);
    } else {
        skin_txtc(c, c->skin->semi, 16, 0, cx, cy, 1, ink, g);
    }
}

float skin_prompt_w(Cx *c, const Prompt *p, float px)
{
    return skin_glyph_w(p->g) + 10 + text_width(c->skin->semi, px, 0, p->label);
}

static struct {
    float x0, x1;
    char g[8];
} g_foot[12];
static int g_nfoot;

void skin_footer_forget(void)
{
    g_nfoot = 0;
}

const char *skin_footer_glyph_at(float x, float y)
{
    if (y < 650) return NULL;
    for (int i = 0; i < g_nfoot; i++)
        if (x >= g_foot[i].x0 - 14 && x < g_foot[i].x1 + 14) return g_foot[i].g;
    return NULL;
}

// the paper the buttons' round marks are coloured with, as in the mockup's footer strip
static Col glyph_fill(const char *g)
{
    if (!strcmp(g, "X")) return hex(PAL(x_fill, 0xE8F8E0), 1);
    if (!strcmp(g, "Y")) return hex(paper_tape_rgb[2], 1);
    if (!strcmp(g, G_MINUS)) return hex(paper_tape_rgb[5], 1);
    if (!strcmp(g, "+")) return hex(paper_tape_rgb[0], 1);
    if (!strcmp(g, "A")) return hex(paper_tape_rgb[3], 1);
    if (!strcmp(g, "B")) return hex(paper_tape_rgb[1], 1);
    if (!strcmp(g, "ZR") || !strcmp(g, "ZL")) return hex(paper_tape_rgb[5], 1);
    if (!strcmp(g, "L") || !strcmp(g, "R")) return hex(paper_tape_rgb[4], 1);
    return hex(paper_tape_rgb[6], 1);
}

// like skin_glyph, on a coloured disc (or pill)
static void foot_glyph(Cx *c, float x, float cy, const char *g)
{
    const Theme *t = &c->skin->th;
    const char *plus = glyph_plus(g);
    if (plus) {
        char a[8];
        snprintf(a, sizeof a, "%.*s", (int)(plus - g), g);
        foot_glyph(c, x, cy, a);
        foot_glyph(c, x + skin_glyph_w(a) + 6, cy, plus + 1);
        return;
    }
    // (the letters and rings stay dark on the pastel discs in Paper night too; a palette may draw the marks as rings on the paper, as the console does)
    Col fill = PAL(glyph_flat, 0) ? t->panel : glyph_fill(g), ring = hex(PAL(glyph_ring, PAPER_TEXT_SOFT), 1), ink = hex(PAL(glyph_ink, PAPER_TEXT), 1);
    if (is_pill(g)) {
        float w = skin_glyph_w(g);
        gfx_rrect(c->s, x, cy - 12, w, 24, 8, fill);
        gfx_rrect_stroke(c->s, x, cy - 12, w, 24, 8, 2.2f, ring);
        skin_txtc(c, c->skin->semi, 13.5f, 0, x + w / 2, cy, 1, ink, g);
        return;
    }
    float cx = x + 15;
    gfx_circle(c->s, cx, cy, 14, fill);
    gfx_ring(c->s, cx, cy, 13.75f, 2.5f, ring);
    if (!strcmp(g, "+")) {
        gfx_line(c->s, cx - 6, cy, cx + 6, cy, 2.2f, ink);
        gfx_line(c->s, cx, cy - 6, cx, cy + 6, 2.2f, ink);
    } else if (!strcmp(g, G_MINUS)) {
        gfx_line(c->s, cx - 6, cy, cx + 6, cy, 2.2f, ink);
    } else {
        skin_txtc(c, c->skin->semi, 16, 0, cx, cy, 1, ink, g);
    }
}

// A strip of paper along the bottom with a blue dotted tape across its left corner and the prompts on it, as in the mockup.
void skin_footer(Cx *c, const Prompt *p, int n)
{
    const Theme *t = &c->skin->th;
    float px = 20, gap = 34, cy = 683;
    gfx_shadow(c->s, 40, 660, 1200, 46, 6, 4, 8, with_a(t->shadow, 0.30f));
    if (!paper_panel(c->s, 40, 660, 1200, 46, 6)) {
        gfx_rrect(c->s, 40, 660, 1200, 46, 6, t->panel);
        gfx_rrect_stroke(c->s, 40.5f, 660.5f, 1199, 45, 6, 1.0f, PANEL_RIM);
    }
    static PaperSpr *tape;
    static unsigned tape_serial;
    if (paper_pal_stale(&tape_serial)) { // (a new palette: the strip lets a new paper through)
        paper_spr_free(tape);
        tape = NULL;
    }
    if (!tape) {
        PaperTape tp = {64, 22, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG, -20.0f, 71};
        paper_tape_host(&tp, 1200, 46, 6.0f, -2.0f, -8.0f); // (the strip's middle is at 70, 663: 30, 3 from the strip of paper's corner)
        tape = paper_tape(&tp);
    }
    if (tape) paper_spr_put(c->s, tape, 70, 663);
    float total = 0;
    for (int i = 0; i < n; i++) total += skin_prompt_w(c, &p[i], px) + (i ? gap : 0);
    float x = 1240 - 30 - total;
    g_nfoot = 0;
    for (int i = 0; i < n; i++) {
        foot_glyph(c, x, cy, p[i].g);
        skin_txtc(c, c->skin->semi, px, 0, x + skin_glyph_w(p[i].g) + 10, cy, 0, t->text, p[i].label);
        if (g_nfoot < 12) {
            g_foot[g_nfoot].x0 = x;
            g_foot[g_nfoot].x1 = x + skin_prompt_w(c, &p[i], px);
            snprintf(g_foot[g_nfoot].g, sizeof g_foot[0].g, "%s", p[i].g);
            g_nfoot++;
        }
        x += skin_prompt_w(c, &p[i], px) + gap;
    }
}

// ------------------------------------------------------------------------------------------------ chrome

// The icon of the app, drawn the way gfx/tools/make_icon.c draws it (a 1024 unit square), at any size: a light tile, a game card
// with a person on its label, and its gold contacts.
void skin_logo(Cx *c, float x, float y, float size)
{
    float k = size / 1024.0f;
    Surf *s = c->s;
    gfx_rrect_grad(s, x, y, size, size, size * 0.23f, hex(0xE9F7FD, 1), hex(0xBFE5F5, 1));
    float cx = x + 252 * k, cy = y + 120 * k, w = 520 * k, h = 784 * k;
    gfx_shadow(s, cx, cy, w, h, 64 * k, 28 * k, 44 * k, hex(0x0B3550, 0.30f));
    gfx_rrect_grad(s, cx, cy, w, h, 64 * k, hex(0x4B525D, 1), hex(0x2C3139, 1));
    gfx_rrect_grad(s, cx + 44 * k, cy + 56 * k, w - 88 * k, 470 * k, 36 * k, hex(0x34CBF5, 1), hex(0x0A9CD6, 1));
    float px = x + 512 * k, py = cy + (56 + 205) * k, ps = 270 * k; // the person: head and shoulders
    gfx_circle(s, px, py - ps * 0.24f, ps * 0.24f, col(1, 1, 1, 1));
    gfx_rrect(s, px - ps * 0.42f, py + ps * 0.06f, ps * 0.84f, ps * 0.42f, ps * 0.21f, col(1, 1, 1, 1));
    gfx_rrect(s, cx + (44 + 60) * k, cy + (56 + 380) * k, w - 208 * k, 26 * k, 13 * k, col(1, 1, 1, 0.55f));
    gfx_rrect(s, cx + 44 * k, cy + 566 * k, w - 88 * k, 150 * k, 28 * k, hex(0x23272E, 1));
    int pins = size < 60 ? 5 : 9;
    float pitch = 46.0f * 9 / pins * k, pw = (size < 60 ? 46 : 28) * k;
    for (int i = 0; i < pins; i++) gfx_rrect(s, cx + 76 * k + i * pitch, cy + 600 * k, pw > pitch - 1 ? pitch - 1 : pw, 100 * k, 6 * k, hex(0xD9B36A, 1));
}

// the cartridge on its own: the dark card, its cyan label with the person, its gold contacts
void skin_logo_mark(Cx *c, float x, float y, float h)
{
    float k = h / 784.0f, w = 520 * k;
    Surf *s = c->s;
    gfx_shadow(s, x, y, w, h, 64 * k, 14 * k, 26 * k, hex(0x000000, c->skin->th.dark ? 0.5f : 0.28f));
    gfx_rrect_grad(s, x, y, w, h, 64 * k, hex(0x555C67, 1), hex(0x23272E, 1));
    gfx_rrect_grad(s, x + 44 * k, y + 56 * k, w - 88 * k, 470 * k, 36 * k, hex(0x34CBF5, 1), hex(0x0A9CD6, 1));
    float px = x + w / 2, py = y + (56 + 205) * k, ps = 270 * k;
    gfx_circle(s, px, py - ps * 0.24f, ps * 0.24f, col(1, 1, 1, 1));
    gfx_rrect(s, px - ps * 0.42f, py + ps * 0.06f, ps * 0.84f, ps * 0.42f, ps * 0.21f, col(1, 1, 1, 1));
    gfx_rrect(s, x + 44 * k, y + 566 * k, w - 88 * k, 150 * k, 28 * k, hex(0x15181D, 1));
    for (int i = 0; i < 5; i++) gfx_rrect(s, x + 76 * k + i * 82 * k, y + 600 * k, 52 * k, 100 * k, 6 * k, hex(0xD9B36A, 1));
}

// ---- the corner of every screen: the mark, the name on its printed strip, and a paper tag with the console's status (the mockup's top bar)

// the mark (the logo): the seal pressed by hand, turned a few degrees. (It had a strip of pink tape across its top left and a blue dotted one near its bottom right;
// the user wants no tape on the stamp anywhere, so only a test can ask for them back: g_skin_mark_tape.) The sprite is 74 x 86 at k = 1; `k` bakes it bigger for About.
static void mark_draw(Surf *s, void *ud)
{
    float k = *(const float *)ud, R = 26.0f * k, cx = 37.0f * k, cy = 40.0f * k;
    PaperSpr *seal = paper_seal(R, PAL(mark_ink, PAPER_INK), 5u);
    if (seal) paper_spr_xform(s, seal, cx, cy, 1.0f, -9.0f * 3.14159265f / 180.0f, 1.0f);
    paper_spr_free(seal);
    if (!g_skin_mark_tape) return;
    PaperTape a = {(int)(34 * k), (int)(13 * k), 0xFED5D4, PAT_STRIPES, PAPER_TAPE_ZIG, -38.0f, 5}, b = {(int)(32 * k), (int)(13 * k), 0xAED6F9, PAT_DOTS, PAPER_TAPE_ZIG, -38.0f, 6};
    PaperSpr *ta = paper_tape(&a), *tb = paper_tape(&b);
    if (ta) paper_spr_put(s, ta, (int)(cx - 0.74f * R), (int)(cy - 0.78f * R));
    if (tb) paper_spr_put(s, tb, (int)(cx + 0.78f * R), (int)(cy + 0.80f * R));
    paper_spr_free(ta);
    paper_spr_free(tb);
}

static PaperSpr *mark_make(float k) { return paper_bake((int)(74 * k), (int)(86 * k), mark_draw, &k); }

typedef struct {
    Cx *c;
    const PaperSpr *strip, *tab, *strip_sh, *tab_sh;
    int len;
    const char *name;
} TitleBake;

// the printed strip with its name and the folded end, as title-fold.svg lays them: the strip's shadow, the strip, the name, the fold's big
// soft shadow (it falls 7 px, over the strip), the fold, and a faint line where the strip ends under it
static void title_stage1(Surf *s, void *ud)
{
    TitleBake *b = ud;
    Cx c = {b->c->skin, s};
    if (b->strip_sh) paper_spr_put(s, b->strip_sh, 12 + b->len / 2, 46);
    if (b->strip) paper_spr_put(s, b->strip, 12 + b->len / 2, 46);
    skin_txtc(&c, c.skin->semi, 40, 0, 26, 45, 0, hex(PAL(title_ink, PAPER_TEXT), 1), b->name);
    if (b->tab_sh) paper_spr_put(s, b->tab_sh, 12 + b->len - 9, 46);
    if (b->tab) paper_spr_put(s, b->tab, 12 + b->len - 9, 46);
    gfx_line(s, 12.0f + b->len, 19, 12.0f + b->len, 73, 2.0f, col(0, 0, 0, 0.12f));
}

static void title_stage2(Surf *s, void *ud)
{
    const PaperSpr *p = ud;
    paper_spr_xform(s, p, p->w * 0.5f, p->h * 0.5f, 1.0f, -1.2f * 3.14159265f / 180.0f, 1.0f);
}

static void bar_sprites(Cx *c, PaperSpr **mark, PaperSpr **title)
{
    static PaperSpr *g_mark, *g_title;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: the seal's ink and the name's are the palette's)
        paper_spr_free(g_mark);
        paper_spr_free(g_title);
        g_mark = g_title = NULL;
    }
    if (!g_mark) g_mark = mark_make(1.12f);
    if (!g_title) {
        const char *name = "Washi";
        int len = (int)ceilf(text_width(c->skin->semi, 40, 0, name)) + 40;
        len += len & 1;
        // the mockup's: stripes laid at half strength on a printed (opaque, a hair grained) strip with five teeth, and a plain opaque fold
        PaperTape sp = {len, 54, PAL(title_strip, PAPER_TITLE_STRIP), PAT_STRIPES, PAPER_TAPE_ZIG_L | PAPER_TAPE_OPAQUE | PAPER_TAPE_GRAINY | PAPER_TAPE_COARSE | PAPER_TAPE_PAT(8), 0.0f, 31},
                  tp = {18, 50, PAL(title_fold, PAPER_TITLE_FOLD), PAT_STRIPES, PAPER_TAPE_ZIG_L | PAPER_TAPE_OPAQUE | PAPER_TAPE_PLAIN | PAPER_TAPE_COARSE, 0.0f, 32};
        PaperSpr *strip = paper_tape(&sp), *tab = paper_tape(&tp);
        PaperSpr *strip_sh = paper_shadow_of(strip, 0.5f, 1.5f, 1.2f, 0x2B2340, 0.30f), *tab_sh = paper_shadow_of(tab, 1.0f, 7.0f, 4.0f, 0x4A3A1A, 0.40f);
        TitleBake tb = {c, strip, tab, strip_sh, tab_sh, len, name};
        PaperSpr *st = paper_bake(len + 24, 84, title_stage1, &tb);
        if (st) g_title = paper_bake(len + 24, 84, title_stage2, st);
        paper_spr_free(st);
        paper_spr_free(strip);
        paper_spr_free(tab);
        paper_spr_free(strip_sh);
        paper_spr_free(tab_sh);
    }
    *mark = g_mark;
    *title = g_title;
}

// the corner mark again, `scale` times as big, its middle at (cx, cy): for pages that show the app's face (About)
void skin_washi_mark(Cx *c, float cx, float cy, float scale)
{
    static PaperSpr *big;
    static float big_k;
    static unsigned serial;
    if (paper_pal_stale(&serial) || !big || big_k != scale) {
        paper_spr_free(big);
        big = mark_make(scale);
        big_k = scale;
    }
    if (big) paper_spr_put(c->s, big, (int)(cx - 37.0f * scale + 0.5f), (int)(cy - 40.0f * scale + 0.5f));
    (void)c;
}

// lays out the status (account, battery, percent, clock) from x, 16 px apart; draws it too when `draw`; returns where it ends
static float status_run(Cx *c, const SkinStatus *st, float x, float cy, int draw)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    int first = 1;
    if (st->account[0]) {
        if (draw) skin_txtc(c, u->med, 20, 0, x, cy, 0, t->text2, st->account);
        x += text_width(u->med, 20, 0, st->account);
        first = 0;
    }
    if (st->battery >= 0) {
        char pct[16];
        snprintf(pct, sizeof pct, "%d%%", st->battery);
        if (!first) x += 16;
        if (draw) skin_icon_battery(c, x, cy, st->battery / 100.0f, t->text2);
        x += 33 + 16;
        if (draw) skin_txtc(c, u->med, 20, 0, x, cy, 0, t->text2, pct);
        x += text_width(u->med, 20, 0, pct);
        first = 0;
    }
    if (st->clock[0]) {
        if (!first) x += 16;
        if (draw) skin_txtc(c, u->semi, 22, 0, x, cy, 0, t->text, st->clock);
        x += text_width(u->semi, 22, 0, st->clock);
    }
    return x;
}

// a paper tag at the top right holding either one label or the status
static void bar_tag(Cx *c, float w, const char *label_or_null, const SkinStatus *st)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    float x = 1240 - w, y = 36, h = 44, cy = y + h / 2;
    gfx_shadow(c->s, x, y, w, h, 6, 4, 8, with_a(t->shadow, 0.30f));
    if (!paper_panel(c->s, x, y, w, h, 6)) {
        gfx_rrect(c->s, x, y, w, h, 6, t->panel);
        gfx_rrect_stroke(c->s, x + 0.5f, y + 0.5f, w - 1, h - 1, 6, 1.0f, PANEL_RIM);
    }
    if (label_or_null) skin_txtc(c, u->semi, 20, 0, x + 24, cy, 0, t->text2, label_or_null);
    else status_run(c, st, x + 24, cy, 1);
}

int g_skin_mark_tape; // tests only: 1 = the seal with its two small tapes again (the user does not want tape on the stamp, so 0 everywhere)

// ---- the intro (ui_intro.c): the logo made on its card `S` times as big as the corner's, and the corner's own pieces

typedef struct {
    Cx *c;
    const PaperSpr *strip, *tab, *strip_sh, *tab_sh;
    int len;
    float S;
    const char *name;
    int mode; // 1 the strip and its fold without the name, 2 only the name
} TitlePart;

static void title_part_draw(Surf *s, void *ud)
{
    const TitlePart *b = ud;
    Cx c = {b->c->skin, s};
    float S = b->S;
    if (b->mode == 2) {
        skin_txtc(&c, c.skin->semi, 40.0f * S, 0, 26 * S, 45 * S, 0, hex(PAPER_TEXT, 1), b->name);
        return;
    }
    if (b->strip_sh) paper_spr_put(s, b->strip_sh, (int)(12 * S + b->len / 2), (int)(46 * S));
    if (b->strip) paper_spr_put(s, b->strip, (int)(12 * S + b->len / 2), (int)(46 * S));
    if (b->tab_sh) paper_spr_put(s, b->tab_sh, (int)(12 * S + b->len - 9 * S), (int)(46 * S));
    if (b->tab) paper_spr_put(s, b->tab, (int)(12 * S + b->len - 9 * S), (int)(46 * S));
    gfx_line(s, 12.0f * S + b->len, 19 * S, 12.0f * S + b->len, 73 * S, 2.0f * S, col(0, 0, 0, 0.12f));
}

static void part_turned(Surf *s, void *ud)
{
    const PaperSpr *p = ud;
    paper_spr_xform(s, p, p->w * 0.5f, p->h * 0.5f, 1.0f, -1.2f * 3.14159265f / 180.0f, 1.0f);
}

typedef struct {
    float R;
    uint32_t ink;
} SealAlone;

static void seal_alone(Surf *s, void *ud)
{
    const SealAlone *a = ud;
    PaperSpr *seal = paper_seal(a->R, a->ink, 5u);
    if (seal) paper_spr_xform(s, seal, s->w * 0.5f, s->h * 0.5f, 1.0f, -9.0f * 3.14159265f / 180.0f, 1.0f);
    paper_spr_free(seal);
}

PaperSpr *skin_logo_seal(float S, uint32_t ink)
{
    SealAlone a = {26.0f * 1.12f * S, ink};
    int SW = (int)ceilf(2 * a.R) + 24;
    PaperSpr *seal = paper_bake(SW, SW, seal_alone, &a);
    if (seal) {
        seal->ox = SW / 2;
        seal->oy = SW / 2;
    }
    return seal;
}

// the name's strip without the name (*blank) and the name alone (*text), S times the corner's, the strip in `strip_rgb` with its fold in `fold_rgb`; either may be NULL
static void logo_title(Cx *c, float S, uint32_t strip_rgb, uint32_t fold_rgb, PaperSpr **blank, PaperSpr **text)
{
    const char *name = "Washi";
    int len1 = (int)ceilf(text_width(c->skin->semi, 40, 0, name)) + 40;
    len1 += len1 & 1;
    int len = (int)lroundf(len1 * S);
    len += len & 1;
    float keep = g_paper_pat_scale;
    g_paper_pat_scale = S; // (the stripes printed on the strip grow with it)
    PaperTape sp = {len, (int)(54 * S), strip_rgb, PAT_STRIPES, PAPER_TAPE_ZIG_L | PAPER_TAPE_OPAQUE | PAPER_TAPE_GRAINY | PAPER_TAPE_COARSE | PAPER_TAPE_PAT(8), 0.0f, 31},
              tp = {(int)(18 * S), (int)(50 * S), fold_rgb, PAT_STRIPES, PAPER_TAPE_ZIG_L | PAPER_TAPE_OPAQUE | PAPER_TAPE_PLAIN | PAPER_TAPE_COARSE, 0.0f, 32};
    PaperSpr *strip = blank ? paper_tape(&sp) : NULL, *tab = blank ? paper_tape(&tp) : NULL;
    PaperSpr *strip_sh = paper_shadow_of(strip, 0.5f * S, 1.5f * S, 1.2f * S, 0x2B2340, 0.30f), *tab_sh = paper_shadow_of(tab, 1.0f * S, 7.0f * S, 4.0f * S, 0x4A3A1A, 0.40f);
    TitlePart tb = {c, strip, tab, strip_sh, tab_sh, len, S, name, 1};
    int W = (int)((len1 + 24) * S), H = (int)(84 * S);
    for (int md = 1; md <= 2; md++) {
        PaperSpr **out = md == 1 ? blank : text;
        if (!out) continue;
        tb.mode = md;
        PaperSpr *p1 = paper_bake(W, H, title_part_draw, &tb);
        PaperSpr *p2 = p1 ? paper_bake(W, H, part_turned, p1) : NULL;
        if (p2) {
            p2->ox = W / 2;
            p2->oy = H / 2;
        }
        *out = p2;
        paper_spr_free(p1);
    }
    paper_spr_free(strip);
    paper_spr_free(tab);
    paper_spr_free(strip_sh);
    paper_spr_free(tab_sh);
    g_paper_pat_scale = keep;
}

void skin_logo_parts(Cx *c, float S, PaperSpr **blank, PaperSpr **text, PaperSpr **seal, uint32_t seal_ink, uint32_t strip_rgb, uint32_t fold_rgb)
{
    *blank = *text = NULL;
    logo_title(c, S, strip_rgb, fold_rgb, blank, text);
    *seal = skin_logo_seal(S, seal_ink);
}

PaperSpr *skin_logo_strip(Cx *c, float S, uint32_t strip_rgb, uint32_t fold_rgb)
{
    PaperSpr *blank = NULL;
    logo_title(c, S, strip_rgb, fold_rgb, &blank, NULL);
    return blank;
}

void skin_bar_logo(Cx *c, const PaperSpr **mark, const PaperSpr **title)
{
    PaperSpr *m, *t;
    bar_sprites(c, &m, &t);
    *mark = m;
    *title = t;
}

void skin_board_plain(Cx *c)
{
    const Surf *bg = c->s->w == SCREEN_W && c->s->h == SCREEN_H ? bg_cached(&c->skin->th) : NULL;
    if (bg) surf_copy(c->s, bg);
    else paper_board_fill(c->s, BOARD_SEED);
}
int g_skin_no_mark; // mockups only: 1 = the top bar without the mark and the name (the intro draws them itself)

void skin_top_bar(Cx *c, const char *right_label, const SkinStatus *st)
{
    Skin *u = c->skin;
    PaperSpr *mark, *title;
    bar_sprites(c, &mark, &title);
    if (mark && !g_skin_no_mark) paper_spr_put(c->s, mark, 42, 9);
    if (title && !g_skin_no_mark) paper_spr_put(c->s, title, 112, 12);
    if (right_label) {
        if (right_label[0]) bar_tag(c, text_width(u->semi, 20, 0, right_label) + 48, right_label, NULL);
        return;
    }
    if (!st) return;
    float w = status_run(c, st, 0, 0, 0);
    if (w <= 0) return;
    bar_tag(c, w + 48, NULL, st);
}

void skin_toast(Cx *c, const char *msg, int kind)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    float w = text_width(u->semi, 18.5f, 0, msg) + 78, h = 50, x = (SCREEN_W - w) / 2, y = 20;
    if (!t->dark) gfx_shadow(c->s, x, y, w, h, h / 2, 5, 12, col(0, 0, 0, 0.22f));
    gfx_rrect(c->s, x, y, w, h, h / 2, t->dark ? hex(0x505050, 1) : hex(0x333333, 1));
    gfx_circle(c->s, x + 29, y + h / 2, 12, t->accent);
    if (kind == 1) skin_icon_info(c, x + 29, y + h / 2, 21, col(1, 1, 1, 1));
    else skin_icon_check(c, x + 29, y + h / 2, 17, 2.6f, col(1, 1, 1, 1));
    skin_txtc(c, u->semi, 18.5f, 0, x + 51, y + h / 2, 0, col(1, 1, 1, 1), msg);
}

void skin_choices(Cx *c, float x, float y, float w, float h, const Prompt *p, int n, int focus_idx, const Col *inks)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    skin_hairline(c, x, y, w);
    float cw = w / n;
    for (int i = 0; i < n; i++) {
        if (i) skin_vline(c, x + cw * i - 1, y + 2, h - 2);
        float cx = x + cw * i + cw / 2, cy = y + h / 2;
        float lw = skin_glyph_w(p[i].g) + 12 + text_width(u->semi, 23, 0, p[i].label);
        float gx = cx - lw / 2;
        int hot = i == focus_idx;
        Col ink = inks ? inks[i] : (hot ? t->text : t->text2);
        skin_glyph(c, gx, cy, p[i].g, ink);
        skin_txtc(c, u->semi, 23, 0, gx + skin_glyph_w(p[i].g) + 12, cy, 0, ink, p[i].label);
        if (hot) skin_focus(c, x + cw * i + 14, y + 14, cw - 28, h - 28, 16);
    }
}
