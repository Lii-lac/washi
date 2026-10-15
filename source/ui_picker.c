// Edit Icon, in Washi: a sheet with two slips (Picture, Background) and a mint OK button on the left, and on the right the picture as a
// taped print with the small round one below it. Each slip opens a grid on a taped sheet: the pictures are the console's own (a square),
// the backgrounds are its 72 colours (swatches.h), so what is chosen here is what the console shows for the same profile. In a grid the
// cell with the focus is a sticker: a dashed ink-blue cut line round it and its corner curling up; the print on the right is
// re-pinned a little crooked at every step. Everything can be touched: tap a slip or a cell, drag a grid to scroll it.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom_pics.h"
#include "icons.h"
#include "ui_scene.h"

enum { PCOLS = 6, CELL = 74, GAP = 16, STEP = CELL + GAP };
static const float WX = 55, WY = 181, WW = 524, WH = 448; // where the cells show (a cell's left, the window's top, its size)
static const float GS_X = 40, GS_Y = 168, GS_W = 574, GS_H = 466; // the grid's sheet
static const float TRACK_X = 600, TRACK_Y0 = 192, TRACK_Y1 = 612; // the scroll cue: a dashed line with a tape thumb on it

typedef struct {
    const char *name;
    int picture; // a picture id; -1 = the first letter of the name
    int swatch;
} Edit;

static int look(const Edit *e) { return SKIN_LOOK(e->swatch, e->picture); }
static float rad(float deg) { return deg * 3.14159265f / 180.0f; }

// ---------------------------------------------------------------------------------------------------- the print on the right

enum { PH_X = 781, PH_Y = 187, PH_W = 296, PH_H = 298, PH_IMG = 256, PH_IMG_X = 20, PH_IMG_Y = 21 };
enum { PVX = 756, PVY = 160, PVW = 360, PVH = 495 }; // what the print (and the round picture and words under it) may touch
static Surf *pv_clean;                               // the page under it
static Surf *pv_img;                                 // the picture as drawn, before it is turned

typedef struct {
    const PaperSpr *card, *tape;
} PhBake;

static void draw_photo_body(Surf *s, void *ud)
{
    const PhBake *b = ud;
    paper_spr_put(s, b->card, SCENE_PAD, SCENE_PAD);
    if (b->tape) paper_spr_put(s, b->tape, SCENE_PAD - 18 + 36, SCENE_PAD - 8 + 11);
}

static void draw_preview(Surf *dst, const Edit *e, float tilt_deg)
{
    static PaperSpr *body;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: new paper)
        paper_spr_free(body);
        body = NULL;
    }
    if (!body) {
        PaperTape t = {72, 22, paper_tape_rgb[4], PAT_STRIPES, PAPER_TAPE_ZIG, -36.0f, 77u};
        paper_tape_host(&t, PH_W, PH_H, 5.0f, -18.0f, -8.0f);
        PaperSpr *tp = paper_tape(&t);
        PhBake pb = {card_get(PH_W, PH_H, 5.0f, 78u, SCENE_PAD), tp};
        body = paper_bake(PH_W + 2 * SCENE_PAD, PH_H + 2 * SCENE_PAD, draw_photo_body, &pb);
        paper_spr_free(tp);
    }
    float a = rad(tilt_deg), cx0 = PH_X + PH_W * 0.5f, cy0 = PH_Y + PH_H * 0.5f;
    const PaperShadows *sh = shadows_get(PH_W, PH_H, 5.0f, 0.0f);
    if (sh) paper_spr_put(dst, sh->s[1], PH_X, PH_Y);
    if (body) paper_spr_xform(dst, body, cx0, cy0, 1.0f, a, 1.0f);
    // the picture, drawn once flat and then turned with the print
    if (!pv_img) pv_img = surf_new(PH_IMG, PH_IMG);
    if (pv_img) {
        gfx_fill(pv_img, hex(PAL(paper, PAPER_PAPER), 1));
        Cx c = ui_cx(pv_img);
        skin_avatar_square(&c, 0, 0, PH_IMG, e->name, look(e), 5);
        for (int i = 3; i < PH_IMG * PH_IMG * 4; i += 4) pv_img->px[i] = 255;
        PaperSpr ps = {PH_IMG, PH_IMG, 0, 0, pv_img->px};
        float dx = PH_IMG_X + PH_IMG * 0.5f - PH_W * 0.5f, dy = PH_IMG_Y + PH_IMG * 0.5f - PH_H * 0.5f;
        paper_spr_xform(dst, &ps, cx0 + dx * cosf(a) - dy * sinf(a), cy0 + dx * sinf(a) + dy * cosf(a), 1.0f, a, 1.0f);
    }
    // the small round one, with a crayon ring, and what it is
    Cx c = ui_cx(dst);
    float rx = 930, ry = 561;
    skin_avatar(&c, rx, ry, 33, e->name, look(e));
    const PaperSpr *ring = ring_get(36.0f, 12u, &PAPER_CRAYON_STD);
    if (ring) paper_spr_put(dst, ring, (int)rx, (int)ry);
    skin_txt(&c, c.skin->med, 18, 0, rx, 631, 1, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), "How it looks on Home");
}

static void region_copy(Surf *dst, int dx, int dy, const Surf *src, int sx, int sy, int w, int h)
{
    for (int y = 0; y < h; y++) memcpy(dst->px + ((size_t)(dy + y) * dst->w + dx) * 4, src->px + ((size_t)(sy + y) * src->w + sx) * 4, (size_t)w * 4);
}

// call right after the page is built: remembers what is under the print, then pins it
static void pv_bake(const Edit *e, float tilt)
{
    if (!pv_clean) pv_clean = surf_new(PVW, PVH);
    if (!pv_clean) return;
    region_copy(pv_clean, 0, 0, G.base, PVX, PVY, PVW, PVH);
    draw_preview(G.base, e, tilt);
}

// the choice changed: put the page back under the print and pin the new one
static void pv_redraw(const Edit *e, float tilt)
{
    if (!pv_clean) return;
    region_copy(G.base, PVX, PVY, pv_clean, 0, 0, PVW, PVH);
    draw_preview(G.base, e, tilt);
}

// ---------------------------------------------------------------------------------------------------- the two grids

// A grid is drawn into a tall picture of all its rows, with transparency (a row only when it is about to show), and slid past a window,
// so scrolling is a smooth glide and the paper of the sheet shows between the cells. The picture is 4 px wider than the window on each
// side, for the ring round the edge columns.
typedef struct {
    PaperSpr *tall;
    uint8_t done[64]; // rows already drawn
    int count, rows, color_mode;
    int current; // the cell that is set now (ringed)
    const Edit *e;
} Grid;

static int grid_count(int color_mode) { return color_mode ? SWATCH_COUNT : 1 + skin_avatar_count(); }

static int cell_look(const Edit *e, const Grid *g, int idx)
{
    if (g->color_mode) return SKIN_LOOK(idx, e->picture);
    return SKIN_LOOK(e->swatch, idx == 0 ? -1 : skin_avatar_at(idx - 1));
}

typedef struct {
    const Grid *g;
    int row;
} RowBake;

// the cell idx with its top left at (x, y), with the ring round it if it is the one set now
static void grid_cell(Cx *c, const Grid *g, int idx, float x, float y)
{
    if (g->color_mode) {
        gfx_rrect(c->s, x, y, CELL, CELL, 10, hex(SWATCH_RGB[idx], 1));
        gfx_rrect_stroke(c->s, x + 0.5f, y + 0.5f, CELL - 1, CELL - 1, 10, 1.0f, col(0, 0, 0, 0.10f));
    } else {
        skin_avatar_square(c, x, y, CELL, g->e->name, cell_look(g->e, g, idx), 10);
    }
    if (idx == g->current) gfx_rrect_stroke(c->s, x - 3.5f, y - 3.5f, CELL + 7, CELL + 7, 13, 3.0f, hex(PAL(crayon_1, PAPER_CRAYON_1), 0.9f));
}

static void draw_grid_row(Surf *s, void *ud)
{
    const RowBake *b = ud;
    Cx c = ui_cx(s);
    for (int col = 0; col < PCOLS; col++) {
        int idx = b->row * PCOLS + col;
        if (idx >= b->g->count) break;
        grid_cell(&c, b->g, idx, 4 + col * STEP, 4);
    }
}

static void grid_row(Grid *g, int row)
{
    if (row < 0 || row >= g->rows || g->done[row]) return;
    g->done[row] = 1;
    RowBake rb = {g, row};
    PaperSpr *spr = paper_bake(g->tall->w, STEP, draw_grid_row, &rb);
    if (spr) paper_spr_over(g->tall, spr, 0, row * STEP);
    paper_spr_free(spr);
}

static void grid_invalidate(Grid *g)
{
    memset(g->done, 0, sizeof g->done);
    memset(g->tall->px, 0, (size_t)g->tall->w * g->tall->h * 4);
}

// the print's page: board, top bar, title, the sheet on its tape, the dashed cue for scrolling, and the footer
static void grid_base(const Edit *e, const Grid *g, const Prompt *pr, int np)
{
    scene_chrome(G.base, g->color_mode ? "Background" : "Picture", e->name, pr, np);
    SceneTape tp[2] = {{{96, 26, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, -34.0f, 61u}, -18, -12},
                       {{84, 26, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 34.0f, 62u}, GS_W - 66, -14}};
    scene_card_on(G.base, GS_X, GS_Y, GS_W, GS_H, 6.0f, 63u, 0.0f, 0, tp, 2, NULL, NULL);
    for (float y = TRACK_Y0; y < TRACK_Y1; y += 12) gfx_line(G.base, TRACK_X, y, TRACK_X, fminf(y + 6, TRACK_Y1), 1.8f, hex(PAL(thread, PAPER_THREAD), 0.55f));
}

// Draws the rows that show (or are about to) without holding a frame up for long: at most a few milliseconds of rows per call, the
// visible ones first, then (when nothing is moving) a row or two further ahead. Returns 1 while rows are still waiting.
static int grid_fill(Grid *g, int scroll, int idle)
{
    int first = scroll / STEP - 1, last = (scroll + (int)WH) / STEP + 1;
    int ahead = idle ? 5 : 0;
    uint64_t t0 = ui_now();
    int drawn = 0;
    for (int pass = 0; pass < 2; pass++) {
        int a = pass == 0 ? first : first - ahead, b = pass == 0 ? last : last + ahead;
        for (int r = a; r <= b; r++) {
            if (r < 0 || r >= g->rows || g->done[r]) continue;
            if (drawn && ui_now() - t0 >= 6) return 1; // out of time: the rest next frame
            grid_row(g, r);
            drawn++;
            if (pass == 1) return 1; // lookahead goes one row at a time
        }
    }
    for (int r = first; r <= last; r++)
        if (r >= 0 && r < g->rows && !g->done[r]) return 1;
    return 0;
}

static void blit_window(Surf *dst, const PaperSpr *tall, int scroll)
{
    if (scroll < 0) scroll = 0;
    if (scroll >= tall->h) return;
    int h = tall->h - scroll < (int)WH ? tall->h - scroll : (int)WH;
    gfx_blit_over(dst, tall->px + (size_t)scroll * tall->w * 4, tall->w, h, (int)WX - 4, (int)WY);
}

// a filled triangle (flat edges at the top and the right) made of one-pixel rows: the curled corner is built from these
static void tri_fill(Surf *s, float x, float y, float size, Col k)
{
    for (int i = 0; i < (int)ceilf(size); i++) gfx_rect(s, x + i, y + i, size - i, 1.0f, k);
}

// The cell with the focus is a sticker coming unstuck: a dashed ink-blue cut line round it, and its top right corner folded over.
// k (0..1) is how far the corner has curled (it curls as the focus arrives and un-curls as it leaves).
static void sticker_focus(Surf *s, float x, float y, float k)
{
    if (k <= 0.01f) return;
    scene_dashed_rrect(s, x - 6, y - 6, CELL + 12, CELL + 12, 15, 2.4f, 7.0f, 5.0f, hex(PAL(ink, PAPER_INK), k));
    float f = 21.0f * k, cx = x + CELL - f, cy = y;
    gfx_shadow(s, cx, cy, f, f, 2, 2, 5, pal_disc_shadow(0.30f * k));
    tri_fill(s, cx, cy, f, hex(PAL(paper, PAPER_PAPER), 1)); // the cut-off corner shows the sheet
    for (int i = 0; i < (int)ceilf(f); i++)                  // the flap: the back of the sticker, lying over the corner of the picture
        gfx_rect(s, cx, cy + i, f - i, 1.0f, hex(PAL(flap, 0xF2ECDA), 1));
    gfx_line(s, cx, cy, cx + f, cy + f, 1.3f, hex(PAL(flap_line, 0xB8AE92), k));
}

// 1 = chosen (A; e is updated), 0 = back (B), 2 = Y: on to the Extra icons tab (the Picture grid only; Background has no tab)
int g_ui_grid_sel = -1, g_ui_grid_count; // tests: the picture/colour grid's focus and how many cells it has, as of the last frame shown

static int pick_grid_once(Edit *e, int color_mode)
{
    static const Prompt PICTURE_PR[] = {{"A", "OK"}, {"B", "Back"}, {"X", "No picture"}};
    static const Prompt COLOR_PR[] = {{"A", "OK"}, {"B", "Back"}};
    const Prompt *pr = color_mode ? COLOR_PR : PICTURE_PR;
    int np = color_mode ? 2 : 3;
    Grid g;
    memset(&g, 0, sizeof g);
    g.e = e;
    g.color_mode = color_mode;
    g.count = grid_count(color_mode);
    g.current = color_mode ? e->swatch : (e->picture < 0 ? 0 : skin_avatar_pos(e->picture) + 1);
    if (g.current < 0 || g.current >= g.count) g.current = 0;
    g.rows = (g.count + PCOLS - 1) / PCOLS;
    if (g.rows > 64) return 0;
    float tall_h = (float)((g.rows ? g.rows : 1) * STEP - GAP + 8);
    ui_transition();
    g.tall = paper_spr_new((int)WW + 8, (int)tall_h);
    if (!g.tall) return 0;
    float max_s = tall_h > WH ? tall_h - WH : 0;
    grid_invalidate(&g);
    grid_base(e, &g, pr, np);
    Edit hover = *e; // the print follows the cursor, as on the console
    int hover_key = -2;
    PaperSpr *thumb = NULL;
    int thumb_len = 0;
    {
        float track = TRACK_Y1 - TRACK_Y0;
        thumb_len = (int)(max_s > 0 ? track * WH / tall_h : track);
        if (thumb_len < 44) thumb_len = 44;
        PaperTape tt = {thumb_len, 14, paper_tape_rgb[4], PAT_STRIPES, PAPER_TAPE_ZIG, 90.0f, 17u};
        thumb = paper_tape(&tt);
    }

    int sel = g.current < 0 ? 0 : g.current;
    uint64_t hop_t = ui_now();
    float s_target = (float)((sel / PCOLS - 2) * STEP);
    if (s_target > max_s) s_target = max_s;
    if (s_target < 0) s_target = 0;
    Glide gs = {0};
    Hops hf;
    hops_snap(&hf, (float)((sel % PCOLS) * STEP), (float)((sel / PCOLS) * STEP));
    glide_snap(&gs, s_target, 0, 0, 0);
    int touching = 0; // a finger is dragging the grid: it follows the finger, not the spring
    int result = 0;
    Bump bump = {0};
    int lean_i = 0; // the cell that leans after a press that leads nowhere
    for (;;) {
        // keep the selected row in the window, and slide there
        if (!touching) {
            float top = (float)((sel / PCOLS) * STEP), want = gs.tgt[0];
            if (top < want) want = top;
            if (top + CELL + 12 > want + WH) want = top + CELL + 12 - WH;
            if (want > max_s) want = max_s;
            if (want < 0) want = 0;
            glide_to(&gs, want, 0, 0, 0);
        }
        hops_to(&hf, (float)((sel % PCOLS) * STEP), (float)((sel / PCOLS) * STEP));
        uint64_t tnow = ui_now();
        float hdt = tnow > hop_t ? (float)(tnow - hop_t) / 1000.0f : 0.0f;
        hop_t = tnow;
        int moving = glide_step(&gs);
        moving |= hops_step(&hf, hdt > 0.05f ? 0.05f : hdt);
        float lox, loy;
        moving |= bump_offset(&bump, &lox, &loy);
        int lx = (int)lroundf(lox), ly = (int)lroundf(loy);
        int scroll = (int)floorf(gs.v[0] + 0.5f);
        int pending = grid_fill(&g, scroll, !moving && !touching);

        if (hover_key != sel) { // the print follows the cursor, and lands a little differently each time
            hover = *e;
            if (color_mode) hover.swatch = sel;
            else hover.picture = sel == 0 ? -1 : skin_avatar_at(sel - 1);
            float tilt = (float)((sel * 7 + 2) % 9 - 4) * 0.55f;
            if (hover_key == -2) pv_bake(&hover, tilt);
            else pv_redraw(&hover, tilt);
            hover_key = sel;
        }
        surf_copy(G.work, G.base);
        blit_window(G.work, g.tall, scroll);
        if (bump.on && (lx || ly)) { // the cell leaves its place on the sheet and leans, ring and all (the sheet shows where it was)
            float cx = WX + (lean_i % PCOLS) * STEP, cy = WY + 4 + (lean_i / PCOLS) * STEP - scroll;
            if (cy > WY - CELL && cy < WY + WH - 10) {
                int rx = (int)cx - 8, ry = (int)cy - 8, rw = CELL + 16, rh = CELL + 16;
                if (ry < (int)WY) { rh -= (int)WY - ry; ry = (int)WY; }
                if (ry + rh > (int)(WY + WH)) rh = (int)(WY + WH) - ry;
                if (rh > 0) region_copy(G.work, rx, ry, G.base, rx, ry, rw, rh);
                Cx lc = ui_cx(G.work);
                grid_cell(&lc, &g, lean_i, cx + lx, cy + ly);
            }
        }
        if (thumb) { // where the window is, on the dashed line
            float track = TRACK_Y1 - TRACK_Y0 - thumb_len, f = max_s > 0 ? gs.v[0] / max_s : 0;
            if (f < 0) f = 0;
            if (f > 1) f = 1;
            paper_spr_put(G.work, thumb, (int)TRACK_X, (int)(TRACK_Y0 + thumb_len * 0.5f + track * f));
        }
        for (int i = 0; i < hf.n; i++) {
            float fy = WY + 4 + hf.y[i] - gs.v[0], ox = i == hf.n - 1 && lean_i == sel ? (float)lx : 0, oy = i == hf.n - 1 && lean_i == sel ? (float)ly : 0;
            if (fy > WY - CELL && fy < WY + WH) { // (cut off where the window ends, as the cells are)
                float fx = WX + hf.x[i] + ox;
                UiKeep kp;
                if (!ui_keep_cell_mark(&kp, G.work, (int)floorf(fx) - 10, (int)floorf(fy + oy) - 10, (int)ceilf(fx + CELL) + 14, (int)ceilf(fy + oy + CELL) + 14, fy, fy + CELL, (int)WY, (int)(WY + WH))) continue;
                sticker_focus(G.work, fx, fy + oy, hops_strength(hf.a[i]));
                ui_put_back(&kp, G.work);
            }
        }
        ui_present(G.work);
        g_ui_grid_sel = sel;
        g_ui_grid_count = g.count;

        uint64_t b = ui_poll(moving || touching || pending ? 0 : -1);
        if (b == UI_QUIT) break;
        if (b & UI_REDRAW) {
            grid_invalidate(&g);
            grid_base(e, &g, pr, np);
            hover_key = -2;
            b &= ~UI_REDRAW;
        }
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind == TOUCH_DRAG) { // the grid follows the finger
                    float s = gs.v[0] - te.dy;
                    if (s < 0) s = 0;
                    if (s > max_s) s = max_s;
                    glide_snap(&gs, s, 0, 0, 0);
                    touching = 1;
                } else if (te.kind == TOUCH_END) { // let go: settle on a whole row, with a visible cell selected
                    touching = 0;
                    float s = floorf(gs.v[0] / STEP + 0.5f) * STEP;
                    if (s > max_s) s = max_s;
                    glide_to(&gs, s, 0, 0, 0);
                    int first = (int)(s / STEP), last = first + (int)(WH / STEP) - 1;
                    if (sel / PCOLS < first) sel = first * PCOLS + sel % PCOLS;
                    if (sel / PCOLS > last) sel = last * PCOLS + sel % PCOLS;
                    if (sel >= g.count) sel = g.count - 1;
                } else if (te.kind == TOUCH_TAP && in_rect(te.x, te.y, WX - 6, WY, WW + 12, WH)) {
                    float tx = te.x - WX, ty = te.y - (WY + 4) + gs.v[0];
                    int col = (int)(tx / STEP), row = (int)(ty / STEP);
                    if (tx >= 0 && ty >= 0 && fmodf(tx, STEP) < CELL && fmodf(ty, STEP) < CELL && col < PCOLS && row < g.rows && row * PCOLS + col < g.count) {
                        sel = row * PCOLS + col;
                        b |= BTN_A;
                    }
                }
            }
        }
        int pdx = 0, pdy = 0; // a press toward nothing
        // off an edge, round to the other end of the row or the column (held too)
        if (b & (BTN_LEFT | BTN_RIGHT | BTN_UP | BTN_DOWN)) {
            int dx = (b & BTN_LEFT) ? -1 : (b & BTN_RIGHT) ? 1 : 0, dy = dx ? 0 : (b & BTN_UP) ? -1 : 1;
            int to = ui_grid_nav(sel, g.count, PCOLS, dx, dy);
            if (to >= 0) sel = to;
            else {
                pdx = dx;
                pdy = dy;
            }
        }
        else if (b & BTN_X) { if (!color_mode) sel = 0; }
        else if ((b & BTN_A) && g.count > 0) {
            Rect rr = {(int)WX + (sel % PCOLS) * STEP - 8, (int)(WY + 4) + (sel / PCOLS) * STEP - (int)gs.v[0] - 8, CELL + 16, CELL + 16};
            Rect nb[8]; // (the cells round it stay as they are: only this one is pushed flat)
            int nn = 0;
            for (int i = 0; i < g.count && nn < 8; i++) {
                int dr = i / PCOLS - sel / PCOLS, dc = i % PCOLS - sel % PCOLS;
                if (i == sel || dr < -1 || dr > 1 || dc < -1 || dc > 1) continue;
                nb[nn++] = (Rect){(int)WX + (i % PCOLS) * STEP - 4, (int)(WY + 4) + (i / PCOLS) * STEP - (int)gs.v[0] - 4, CELL + 8, CELL + 8};
            }
            ui_press_keep(nb, nn);
            ui_press(G.work, rr);
            if (color_mode) e->swatch = sel;
            else e->picture = sel == 0 ? -1 : skin_avatar_at(sel - 1);
            result = 1;
            break;
        } else if (b & BTN_B) {
            break;
        }
        if (pdx || pdy) {
            bump_start(&bump, pdx, pdy);
            lean_i = sel;
        }
    }
    paper_spr_free(g.tall);
    paper_spr_free(thumb);
    return result;
}

// 1 = chosen, 0 = back. Pictures are a strip of cards, the console's own first and then the Extra icons game by game (ui_extra.c); backgrounds are
// the grid of the console's colours.
static int pick_grid(Edit *e, int color_mode)
{
    if (G.quit) return 0;
    if (!color_mode) return ui_picture_strip(e->name, e->swatch, &e->picture);
    return pick_grid_once(e, 1);
}

// ---------------------------------------------------------------------------------------------------- the main page

enum { ROW_PICTURE = 0, ROW_BACKGROUND, ROW_OK, NITEMS };

enum { SHEET_X = 52, SHEET_Y = 176, SHEET_W = 560, SHEET_H = 198, SLIP_X = 66, SLIP_W = 532, SLIP_H = 82, OK_Y = 408, OK_H = 74 };

typedef struct {
    const Edit *e;
    int which;
} RowPaint;

static void paint_row(Cx *c, void *ud, float ox, float oy)
{
    const RowPaint *p = ud;
    Skin *u = c->skin;
    skin_txtc(c, u->semi, 29, 0, ox + 30, oy + SLIP_H * 0.5f, 0, hex(PAL(text, PAPER_TEXT), 1), p->which == ROW_PICTURE ? "Picture" : "Background");
    float sz = 56, x = ox + SLIP_W - 30 - sz, y = oy + (SLIP_H - sz) * 0.5f;
    if (p->which == ROW_PICTURE) {
        skin_avatar_square(c, x, y, sz, p->e->name, look(p->e), 9);
    } else {
        gfx_rrect(c->s, x, y, sz, sz, 9, hex(SWATCH_RGB[p->e->swatch % SWATCH_COUNT], 1));
        gfx_rrect_stroke(c->s, x + 0.5f, y + 0.5f, sz - 1, sz - 1, 9, 1.0f, col(0, 0, 0, 0.12f));
    }
}

// the page and its three items (the two slips and OK); the focus is on `focus`
static Scene *main_build(const Edit *e, int focus, RowPaint *rp)
{
    static const Prompt rows_pr[] = {{"A", "Choose"}, {"B", "Cancel"}};
    static const Prompt ok_pr[] = {{"A", "Save"}, {"B", "Cancel"}};
    scene_chrome(G.base, "Edit Icon", e->name, focus == ROW_OK ? ok_pr : rows_pr, 2);
    SceneTape tp[2] = {{{100, 26, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, -34.0f, 31u}, -18, -16},
                       {{84, 26, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 34.0f, 32u}, SHEET_W - 66, -16}};
    scene_card_on(G.base, SHEET_X, SHEET_Y, SHEET_W, SHEET_H, 6.0f, 33u, 0.0f, 0, tp, 2, NULL, NULL);
    pv_bake(e, 1.1f);
    Scene *sc = scene_new(G.base);
    if (!sc) return NULL;
    for (int i = 0; i < 2; i++) {
        rp[i].e = e;
        rp[i].which = i;
        DItem *it = item_add(sc, SLIP_X, SHEET_Y + 12 + i * 94, SLIP_W, SLIP_H, 6.0f, LIFT_ROW, paper_lift_k[LIFT_ROW].scale);
        if (!it) continue;
        it->tilt = rad(i ? -0.3f : 0.3f);
        PaperTape t = {56, 22, paper_tape_rgb[i ? 4 : 5], i ? PAT_DOTS : PAT_STRIPES, PAPER_TAPE_ZIG, i ? -20.0f : -38.0f, 35u + (uint32_t)i};
        item_tape(it, &t, 2, -10);
        IBake ib = {.w = SLIP_W, .h = SLIP_H, .r = 6.0f, .card = card_get(SLIP_W, SLIP_H, 6.0f, 36u + (uint32_t)i, D_PAD), .paint = paint_row, .pud = &rp[i]};
        item_bake(it, 0, &ib);
    }
    DItem *ok = item_add(sc, SHEET_X, OK_Y, SHEET_W, OK_H, 10.0f, LIFT_BTN, paper_lift_k[LIFT_BTN].scale);
    if (ok) {
        IBake ib = {.w = SHEET_W, .h = OK_H, .r = 10.0f, .fill = PAL(go, 0xD4FAC7), .label = "OK", .font = G.skin.semi, .px = 29, .ink = hex(PAPER_TEXT, 1)};
        item_bake(ok, 0, &ib);
    }
    return sc;
}

int ui_picture_picker(const char *name, int *picture, int *swatch)
{
    if (G.quit) return 0;
    Edit e = {name, *picture, *swatch % SWATCH_COUNT};
    if (e.swatch < 0) e.swatch = 0;
    if (e.picture < -1 || (custom_is(e.picture) ? custom_key(e.picture) == NULL : e.picture >= skin_avatar_count())) e.picture = -1;
    int focus = ROW_PICTURE;
    RowPaint rp[2];
    Scene *sc = main_build(&e, focus, rp);
    if (!sc) return 0;
    ui_transition();
    scene_frame(sc, 1);
    scene_focus(sc, focus, 0);
    int rc = 0;
    for (;;) {
        int anim = scene_frame(sc, 0);
        uint64_t b = ui_poll(anim ? 0 : -1);
        if (b == UI_QUIT) break;
        if (b & UI_REDRAW) {
            int cur = sc->cur;
            scene_free(sc);
            sc = main_build(&e, focus, rp);
            if (!sc) return 0;
            scene_frame(sc, 1);
            scene_start_up(sc, cur >= 0 ? cur : focus);
            b &= ~UI_REDRAW;
        }
        if (b & UI_TOUCH) { // tap a slip or OK
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te))
                if (te.kind == TOUCH_TAP) {
                    int i = scene_hit(sc, te.x, te.y);
                    if (i >= 0) {
                        focus = i;
                        b |= BTN_A;
                    }
                }
        }
        int held = G.plat->repeated != 0, old_focus = focus, pdx = 0, pdy = 0; // (pdx, pdy: a press toward nothing)
        if (b & BTN_DOWN) { if (focus < ROW_OK) focus++; else if (wrap_ok()) focus = 0; else pdy = 1; }
        else if (b & BTN_UP) { if (focus > 0) focus--; else if (wrap_ok()) focus = ROW_OK; else pdy = -1; }
        else if (b & BTN_LEFT) pdx = -1;
        else if (b & BTN_RIGHT) pdx = 1;
        else if (b & BTN_B) break;
        else if (b & BTN_A) {
            scene_focus(sc, focus, 0);
            scene_press(sc, focus);
            if (focus == ROW_OK) {
                *picture = e.picture;
                *swatch = e.swatch;
                rc = 1;
                break;
            }
            pick_grid(&e, focus == ROW_BACKGROUND);
            if (G.quit) {
                scene_free(sc);
                return 0;
            }
            scene_free(sc);
            sc = main_build(&e, focus, rp);
            if (!sc) return 0;
            ui_transition();
            scene_frame(sc, 1);
            scene_start_up(sc, focus);
            continue;
        }
        if (pdx || pdy) scene_lean(sc, focus, pdx, pdy);
        if (focus != old_focus) {
            scene_focus(sc, focus, held);
            // the footer says Save on OK and Choose on the slips
            if ((focus == ROW_OK) != (old_focus == ROW_OK)) {
                static const Prompt rows_pr[] = {{"A", "Choose"}, {"B", "Cancel"}};
                static const Prompt ok_pr[] = {{"A", "Save"}, {"B", "Cancel"}};
                Cx c = ui_cx(G.base);
                skin_footer(&c, focus == ROW_OK ? ok_pr : rows_pr, 2);
                scene_inval(sc, 0, 650, SCREEN_W, 70);
            }
        }
    }
    scene_free(sc);
    ui_screen_done();
    return rc;
}
