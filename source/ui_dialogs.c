// Dialogs, Washi style: a taped paper card over the cream-dimmed screen. Buttons and keys are paper that lifts when it has the focus
// (a ghost patch where it sat, a deeper shadow, a short wiggle), exactly as on the home screen; no frame or glow. Everything that
// moves is painted by dirty rectangles over a still picture of the dialog (a Scene), so a lift costs a few small copies.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dirty.h"
#include "gfx/paper.h"
#include "ui_internal.h"

// ------------------------------------------------------------------------------------------------ colours of the dialogs

#define COL_CANCEL PAL(quiet, 0xF1EFF4) // the quiet button and the pad's keys (Paper night: a night slip, its writing pale)
#define COL_GO PAL(go, 0xD4FAC7) // the button that says yes (Paper night: the lit window's gold)
#define COL_OK_BADGE 0xD4FAC7    // the tick's badge
#define COL_BAD 0xF9D7D3        // a yes that can't be undone, and the "!" badge
#define COL_BAD_INK 0xB3261E    // the "!" and a wrong-password note
#define COL_BAD_TEXT 0x8E1B14   // text on COL_BAD
#define COL_WAIT_DIM 0xF4DAD7   // the red button while it is sealed
#define COL_WAIT_DIM_INK 0x7D6A68
#define COL_WAIT_RED 0xC6362F
#define COL_BADGE_LILAC 0xE9DCFC
#define COL_BADGE_WARN 0xFFE3CD
#define COL_BADGE_WARN_INK 0x8A4B00
#define COL_BADGE_OK_INK 0x1E6B34
#define COL_BADGE_INFO 0xDCEBFA
#define COL_SLIP PAL(slip, 0xF7F3E8) // a row in a choice list

#include "ui_scene.h"


// ------------------------------------------------------------------------------------------------ the card of a dialog

typedef struct {
    const PaperSpr *card;
    PaperTape a, b;
    const PaperSpr *ta, *tb;
    float w, h;
} CardBake;

enum { CARD_PAD = 40 };

static void draw_card_bake(Surf *s, void *ud)
{
    const CardBake *c = ud;
    paper_spr_put(s, c->card, CARD_PAD, CARD_PAD);
    if (c->ta) paper_spr_put(s, c->ta, CARD_PAD - 24 + c->a.len / 2, CARD_PAD - 10 + c->a.wid / 2);
    if (c->tb) paper_spr_put(s, c->tb, CARD_PAD + (int)c->w - 76 + c->b.len / 2, CARD_PAD - 10 + c->b.wid / 2);
}

// Opens a dialog over the screen: G.dim = the cream-dimmed screen with the paper card (x, y, w, h) on it, turned a hair, shadow and
// two strips of tape; the dialog's first frame rises over it. Returns a context to print on G.dim.
Cx dlg_open(float x, float y, float w, float h, float tilt_deg, uint32_t seed)
{
    ui_dialog_begin(x, y - 18, w, h + 18); // (the dimmed screen in G.dim, and the rise: the tape on the top edge rises with the card)
    CardBake cb;
    memset(&cb, 0, sizeof cb);
    cb.w = w;
    cb.h = h;
    cb.card = card_get((int)w, (int)h, 6.0f, seed, CARD_PAD);
    PaperTape a = {100, 28, paper_tape_rgb[0], PAT_STRIPES, PAPER_TAPE_ZIG, -36.0f, 41u + seed},
              b = {100, 28, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG, 36.0f, 43u + seed};
    paper_tape_host(&a, w, h, 6.0f, -24.0f, -10.0f);
    paper_tape_host(&b, w, h, 6.0f, w - 76.0f, -10.0f);
    cb.a = a;
    cb.b = b;
    PaperSpr *ta = paper_tape(&a), *tb = paper_tape(&b);
    cb.ta = ta;
    cb.tb = tb;
    PaperSpr *spr = paper_bake((int)w + 2 * CARD_PAD, (int)h + 2 * CARD_PAD, draw_card_bake, &cb);
    PaperShadows *sh = paper_shadows((int)w, (int)h, 6.0f, 0.0f, 2);
    if (sh) paper_spr_put(G.dim, sh->s[1], (int)lroundf(x), (int)lroundf(y));
    if (spr) paper_spr_xform(G.dim, spr, x + w * 0.5f, y + h * 0.5f, 1.0f, tilt_deg * 3.14159265f / 180.0f, 1.0f);
    paper_shadows_free(sh);
    paper_spr_free(spr);
    paper_spr_free(ta);
    paper_spr_free(tb);
    return ui_cx(G.dim);
}

// a round badge with its crayon ring: a "!" , a tick, an "i" or a lock
static void badge(Cx *c, float cx, float cy, float r, UiKind kind, int lock)
{
    uint32_t fill;
    if (lock) fill = COL_BADGE_LILAC;
    else if (kind == UI_OK) fill = COL_OK_BADGE;
    else if (kind == UI_INFO) fill = COL_BADGE_INFO;
    else if (kind == UI_WARN) fill = COL_BADGE_WARN;
    else fill = COL_BAD;
    gfx_circle(c->s, cx, cy, r, hex(fill, 1));
    float k = r / 31.0f;
    if (lock) skin_icon_lock(c, cx, cy + 1, 34 * k, hex(PAPER_TEXT, 1));
    else if (kind == UI_OK) skin_icon_check(c, cx, cy, 44 * k, 5.2f * k, hex(COL_BADGE_OK_INK, 1));
    else if (kind == UI_INFO) skin_icon_info(c, cx, cy, 52 * k, hex(PAPER_TEXT, 1));
    else skin_icon_alert(c, cx, cy, 50 * k, hex(kind == UI_WARN ? COL_BADGE_WARN_INK : COL_BAD_INK, 1));
    const PaperSpr *ring = ring_get(r + 1.0f, 3u + (uint32_t)kind, &PAPER_CRAYON_STD);
    if (ring) paper_spr_put(c->s, ring, (int)lroundf(cx), (int)lroundf(cy));
}

// ------------------------------------------------------------------------------------------------ shared text pieces

#define DLG_BTN_W 250.0f
#define DLG_BTN_H 64.0f
#define DLG_BTN_GAP 20.0f
#define DLG_BELOW 30.0f // from the words to the buttons, and from the buttons to the card's edge

// Detail text: the first paragraph in the normal tone, anything after a blank line quieter.
typedef struct {
    char lines[14][160];
    int n, second; // line index where the quieter paragraph starts (n if none)
} Detail;

static void detail_build(Detail *d, const char *text, float maxw)
{
    memset(d, 0, sizeof *d);
    if (!text || !text[0]) return;
    Skin *u = &G.skin;
    char first[600], rest[600];
    const char *split = strstr(text, "\n\n");
    if (split) {
        snprintf(first, sizeof first, "%.*s", (int)(split - text), text);
        snprintf(rest, sizeof rest, "%s", split + 2);
    } else {
        snprintf(first, sizeof first, "%s", text);
        rest[0] = 0;
    }
    d->n = ui_wrap(u->med, 20, maxw, first, d->lines, 7);
    d->second = d->n;
    if (rest[0]) d->n += ui_wrap(u->reg, 18.5f, maxw, rest, d->lines + d->n, 14 - d->n);
}

static float detail_end(const Detail *d, float y_first)
{
    float y = y_first;
    for (int i = 1; i < d->n; i++) y += (i == d->second ? 36 : (i > d->second ? 26 : 28));
    return d->n ? y : y_first - 28;
}

static void detail_draw(Cx *c, const Detail *d, float cx, float y_first)
{
    Skin *u = c->skin;
    const Theme *t = &u->th;
    float y = y_first;
    for (int i = 0; i < d->n; i++) {
        if (i > 0) y += (i == d->second ? 36 : (i > d->second ? 26 : 28));
        if (i < d->second) skin_txt(c, u->med, 20, 0, cx, y, 1, t->text2, d->lines[i]);
        else skin_txt(c, u->reg, 18.5f, 0, cx, y, 1, t->text3, d->lines[i]);
    }
}

// A headline: one line at full size if it fits, otherwise up to two lines a little smaller.
typedef struct {
    char lines[2][160];
    int n;
    float px;
} Headline;

static void headline_build(Headline *hl, const char *headline, float maxw)
{
    Skin *u = &G.skin;
    memset(hl, 0, sizeof *hl);
    if (text_width(u->semi, 34, -0.3f, headline) <= maxw) {
        hl->n = 1;
        hl->px = 34;
        snprintf(hl->lines[0], sizeof hl->lines[0], "%.159s", headline);
        return;
    }
    hl->px = 29;
    char all[8][160];
    int n = ui_wrap(u->semi, hl->px, maxw, headline, all, 8);
    hl->n = n > 2 ? 2 : (n < 1 ? 1 : n);
    for (int i = 0; i < hl->n; i++) snprintf(hl->lines[i], sizeof hl->lines[i], "%.159s", all[i]);
    if (n > 2) text_fit(u->semi, hl->px, -0.3f, all[1], maxw - 30, hl->lines[1], sizeof hl->lines[1]); // never more than two lines
}

static float headline_extra(const Headline *hl) { return (hl->n - 1) * 36.0f; }

static void headline_draw(Cx *c, float cx, float y, const Headline *hl)
{
    for (int i = 0; i < hl->n; i++) skin_txt(c, c->skin->semi, hl->px, -0.3f, cx, y + i * 36.0f, 1, c->skin->th.text, hl->lines[i]);
}

// a button of the dialog: its place, its looks
static DItem *dlg_button(Scene *sc, float x, float y, int var, const char *label, const char *glyph, uint32_t fill, Col ink, uint32_t gfill, Col gring)
{
    Skin *u = &G.skin;
    DItem *it = item_add(sc, x, y, DLG_BTN_W, DLG_BTN_H, 10.0f, LIFT_BTN, paper_lift_k[LIFT_BTN].scale);
    if (!it) return NULL;
    IBake ib = {DLG_BTN_W, DLG_BTN_H, 10.0f, fill, NULL, label, u->semi, 24, ink, glyph, gfill, gring, NULL, NULL, NULL};
    item_bake(it, var, &ib);
    return it;
}

// ------------------------------------------------------------------------------------------------ message / confirm

// Runs a dialog whose only focused thing is one button: it leans when a D-pad direction leads nowhere. Returns the buttons pressed
// (as ui_poll), a tap on a button as A (the right one) or B (the left one of two).
static uint64_t buttons_wait(Scene *sc, int two)
{
    for (;;) {
        int anim = scene_frame(sc, 0);
        uint64_t b = ui_poll(anim ? 0 : -1);
        int dx, dy;
        if (b != UI_QUIT && !(b & ~(uint64_t)(BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) && lean_dir(b, &dx, &dy)) {
            scene_lean(sc, sc->cur, dx, dy);
            continue;
        }
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind != TOUCH_TAP) continue;
                int i = scene_hit(sc, te.x, te.y);
                if (i >= 0) b |= (two && i == 0) ? BTN_B : BTN_A;
            }
        }
        if (b) return b;
    }
}

void ui_message(UiKind kind, const char *headline, const char *detail)
{
    if (G.quit) return;
    Skin *u = &G.skin;
    Detail d;
    detail_build(&d, detail, 540);
    Headline hl;
    headline_build(&hl, headline, 560);
    float w = 640, y_head = 176, y_det = 220 + headline_extra(&hl);
    float y_end = d.n ? detail_end(&d, y_det) : y_head + headline_extra(&hl) - 8;
    float h = y_end + DLG_BELOW + DLG_BTN_H + DLG_BELOW;
    float x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 6;

    Cx c = dlg_open(x, y, w, h, -0.4f, 5);
    badge(&c, 640, y + 78, 31, kind, 0);
    headline_draw(&c, 640, y + y_head, &hl);
    detail_draw(&c, &d, 640, y + y_det);
    Scene *sc = scene_new(G.dim);
    if (!sc) return;
    DItem *ok = dlg_button(sc, 640 - DLG_BTN_W / 2, y + h - DLG_BELOW - DLG_BTN_H, 0, "OK", "A", COL_GO, hex(PAPER_TEXT, 1), PAPER_PAPER, u->th.text2);
    if (!ok) {
        scene_free(sc);
        return;
    }
    scene_frame(sc, 1);
    scene_focus(sc, 0, 0);
    for (;;) {
        uint64_t b = buttons_wait(sc, 0);
        if (b == UI_QUIT || (b & (BTN_A | BTN_B))) {
            if (b != UI_QUIT && (b & BTN_A)) scene_press(sc, sc->cur);
            break;
        }
    }
    scene_free(sc);
}

int ui_confirm(const char *headline, const char *detail, const char *yes, const char *no, int destructive,
               const char *avatar_name, int avatar_color)
{
    if (G.quit) return 0;
    Skin *u = &G.skin;
    const Theme *t = &u->th;
    Detail d;
    detail_build(&d, detail, 500);
    Headline hl;
    headline_build(&hl, headline, 540);
    float w = 620;
    // as much room over the picture (or the headline) as under the words; a short dialog keeps them in the middle
    float y_head = avatar_name ? 192 : 72, y_det = y_head + 44 + headline_extra(&hl);
    float y_end = d.n ? detail_end(&d, y_det) : y_head + headline_extra(&hl);
    float h = y_end + DLG_BELOW + DLG_BTN_H + DLG_BELOW;
    if (h < 300) {
        y_head += (300 - h) / 2;
        y_det += (300 - h) / 2;
        h = 300;
    }
    float x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 6;

    Cx c = dlg_open(x, y, w, h, -0.4f, 7);
    if (avatar_name) {
        skin_avatar(&c, 640, y + 92, 48, avatar_name, avatar_color);
        const PaperSpr *ring = ring_get(49.0f, 21u, &PAPER_CRAYON_STD);
        if (ring) paper_spr_put(c.s, ring, 640, (int)lroundf(y + 92));
    }
    headline_draw(&c, 640, y + y_head, &hl);
    detail_draw(&c, &d, 640, y + y_det);
    Scene *sc = scene_new(G.dim);
    if (!sc) return 0;
    float by = y + h - DLG_BELOW - DLG_BTN_H, bx0 = 640 - DLG_BTN_W - DLG_BTN_GAP / 2, bx1 = 640 + DLG_BTN_GAP / 2;
    DItem *no_b = dlg_button(sc, bx0, by, 0, no, "B", COL_CANCEL, t->text, paper_tape_rgb[0], t->text2);
    DItem *yes_b = dlg_button(sc, bx1, by, 0, yes, "A", destructive ? COL_BAD : COL_GO, hex(destructive ? COL_BAD_TEXT : PAPER_TEXT, 1), PAPER_PAPER, t->text2);
    if (!no_b || !yes_b) {
        scene_free(sc);
        return 0;
    }
    scene_frame(sc, 1);
    scene_focus(sc, 1, 0); // the Yes button peels up
    int rc = 0;
    for (;;) {
        uint64_t b = buttons_wait(sc, 1);
        if (b == UI_QUIT) break;
        if (b & BTN_A) {
            scene_press(sc, sc->cur);
            rc = 1;
            break;
        }
        if (b & BTN_B) break;
    }
    scene_free(sc);
    return rc;
}

// ------------------------------------------------------------------------------------------------ confirm after a wait

// Like ui_confirm for something that can't be undone: a caution badge, Cancel up, and a red button that is taped shut. After
// `hold_ms` the button goes solid red, lifts and the tape peels off (the console's own "Delete This User" waits the same way). A does
// nothing until it is solid and has settled; B cancels at any time. 1 = confirmed.
#define HOLD_SETTLE_MS 380 // after the button is solid, A is ignored this long (a held-down or mashed A can't confirm)

static float ease_out(float k)
{
    k = k < 0 ? 0 : k > 1 ? 1 : k;
    return 1 - (1 - k) * (1 - k) * (1 - k);
}

int ui_confirm_wait(const char *headline, const char *detail, const char *yes, const char *no, int hold_ms)
{
    if (G.quit) return 0;
    Skin *u = &G.skin;
    const Theme *t = &u->th;
    Detail d;
    detail_build(&d, detail, 540);
    Headline hl;
    headline_build(&hl, headline, 560);
    float w = 660, y_head = 176, y_det = 220 + headline_extra(&hl);
    float y_end = d.n ? detail_end(&d, y_det) : y_head + headline_extra(&hl) - 8;
    float h = y_end + DLG_BELOW + 12 + DLG_BTN_H + DLG_BELOW; // (a little more air over the button, for its tape)
    float x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 6;

    Cx c = dlg_open(x, y, w, h, -0.4f, 9);
    badge(&c, 640, y + 78, 31, UI_ERROR, 0);
    headline_draw(&c, 640, y + y_head, &hl);
    detail_draw(&c, &d, 640, y + y_det);
    Scene *sc = scene_new(G.dim);
    if (!sc) return 0;
    float by = y + h - DLG_BELOW - DLG_BTN_H, bx0 = 640 - DLG_BTN_W - DLG_BTN_GAP / 2, bx1 = 640 + DLG_BTN_GAP / 2;
    DItem *cancel = dlg_button(sc, bx0, by, 0, no, "B", COL_CANCEL, t->text, paper_tape_rgb[0], t->text2);
    // the red button: tape across its top seam first (the strip is part of how it rests), then its two looks
    DItem *del = item_add(sc, bx1, by, DLG_BTN_W, DLG_BTN_H, 10.0f, LIFT_BTN, paper_lift_k[LIFT_BTN].scale);
    if (!cancel || !del) {
        scene_free(sc);
        return 0;
    }
    PaperTape seal = {160, 26, paper_tape_rgb[2], PAT_STRIPES, PAPER_TAPE_ZIG, -2.5f, 61u};
    item_tape(del, &seal, 44, -12);
    IBake dim = {DLG_BTN_W, DLG_BTN_H, 10.0f, COL_WAIT_DIM, NULL, yes, u->semi, 24, hex(COL_WAIT_DIM_INK, 1), "A", 0xF9EDEB, hex(COL_WAIT_DIM_INK, 1), NULL, NULL, NULL};
    IBake solid = {DLG_BTN_W, DLG_BTN_H, 10.0f, COL_WAIT_RED, NULL, yes, u->semi, 24, col(1, 1, 1, 1), "A", COL_WAIT_RED, col(1, 1, 1, 1), NULL, NULL, NULL};
    item_bake(del, 0, &dim);
    del->blend = body_bake(del, &solid);

    scene_frame(sc, 1);
    scene_focus(sc, 0, 0); // Cancel is the one that is up
    uint64_t t0 = ui_now(), t_ready = 0; // (the dialog's rise has played by now: the count starts from here)
    for (;;) {
        uint64_t now = ui_now();
        float p = (float)(now - t0) / (float)hold_ms;
        if (p >= 1 && !t_ready) {
            t_ready = now; // the wait is over: the button goes solid, lifts, and its tape peels; Cancel lets go
            scene_focus(sc, 1, 0);
        }
        del->mix = t_ready ? ease_out((float)(now - t_ready) / 150.0f) : 0;
        int armed = t_ready && now - t_ready >= HOLD_SETTLE_MS;
        int anim = scene_frame(sc, 0);
        int animating = anim || !t_ready || now - t_ready < HOLD_SETTLE_MS + 40;
        uint64_t b = ui_poll(animating ? 16 : -1);
        if (b == UI_QUIT) {
            scene_free(sc);
            return 0;
        }
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind != TOUCH_TAP) continue;
                int i = scene_hit(sc, te.x, te.y);
                if (i >= 0) b |= i == 0 ? BTN_B : BTN_A;
            }
        }
        if (b & BTN_B) {
            scene_free(sc);
            return 0;
        }
        if ((b & BTN_A) && armed) {
            scene_press(sc, sc->cur);
            scene_free(sc);
            return 1;
        }
        int dx, dy;
        if (!(b & ~(uint64_t)(BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT)) && lean_dir(b, &dx, &dy)) scene_lean(sc, sc->cur, dx, dy);
    }
}

// ------------------------------------------------------------------------------------------------ choice list

// A row of a list that has a switch in it: the words at the left, a chevron (it opens something) or the switch at the right. The switch is a washi-tape guide, dashed,
// where a strip would go: off, it is empty and the paper knob rests at its left; on, a strip of tape is stuck down in it, the knob has slid to the right and carries a
// check, and the word says ON. k, 0..1, is how far it has moved: the slide bakes the row again for each of its frames.
typedef struct {
    const char *title, *sub, *err;
    int dim, sw;
    float k, w, h;
} SwRow;

enum { SW_W = 96, SW_H = 40 };

static float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }

static void paint_sw_row(Cx *c, void *ud, float ox, float oy)
{
    const SwRow *r = ud;
    Surf *s = c->s;
    const Theme *t = &c->skin->th;
    float cy = oy + r->h * 0.5f, rx = ox + r->w - 30;
    Col ink = r->dim ? t->text2 : t->text;
    if (r->sub || r->err) {
        skin_txtc(c, c->skin->semi, 23, 0, ox + 26, cy - 13, 0, ink, r->title);
        skin_txtc(c, c->skin->med, 16.5f, 0, ox + 26, cy + 17, 0, r->err ? hex(PAL(bad_ink, COL_BAD_INK), 1) : t->text2, r->err ? r->err : r->sub);
    } else {
        skin_txtc(c, c->skin->semi, 23, 0, ox + 26, cy, 0, ink, r->title);
    }
    if (!r->sw) { // it opens something
        gfx_line(s, rx - 9, cy - 10, rx, cy, 3.0f, t->text2);
        gfx_line(s, rx, cy, rx - 9, cy + 10, 3.0f, t->text2);
        return;
    }
    float k = r->k, tx = rx - SW_W + 6, ty = cy - SW_H * 0.5f;
    scene_dashed_rrect(s, tx, ty, SW_W, SW_H, 9.0f, 1.7f, 6.0f, 4.0f, PAL(sw_guide, col(0.45f, 0.40f, 0.30f, 0.62f))); // where the tape goes
    if (k > 0.01f) { // the strip of tape, stuck down in it (it shows as the knob arrives)
        PaperTape tp = {SW_W + 4, SW_H - 4, paper_tape_rgb[4], PAT_DOTS, PAPER_TAPE_ZIG, -1.2f, 77u};
        PaperSpr *sp = paper_tape(&tp);
        if (sp) {
            paper_spr_xform(s, sp, tx + SW_W * 0.5f, ty + SW_H * 0.5f, 1.0f, 0.0f, clamp01(k * 1.6f));
            paper_spr_free(sp);
        }
    }
    float kr = 14.5f, kx = tx + 7.0f + kr + (SW_W - 14.0f - 2 * kr) * k, ky = ty + SW_H * 0.5f; // the knob: a paper disc
    gfx_shadow(s, kx - kr, ky - kr, kr * 2, kr * 2, kr, 3, 7, pal_disc_shadow(0.36f));
    gfx_circle(s, kx, ky, kr, hex(PAL(knob, PAPER_PAPER), 1));
    gfx_ring(s, kx, ky, kr - 0.5f, 1.0f, col(0, 0, 0, 0.14f));
    float on_a = clamp01((k - 0.35f) / 0.65f), off_a = clamp01((0.65f - k) / 0.65f);
    if (on_a > 0) { // a check, drawn in the ink
        gfx_line(s, kx - 6.0f, ky + 0.5f, kx - 1.5f, ky + 5.0f, 3.2f, hex(COL_BADGE_OK_INK, on_a));
        gfx_line(s, kx - 1.5f, ky + 5.0f, kx + 6.5f, ky - 5.0f, 3.2f, hex(COL_BADGE_OK_INK, on_a));
    }
    if (off_a > 0) gfx_line(s, kx - 4.5f, ky, kx + 4.5f, ky, 2.6f, hex(0x9A927E, off_a)); // and a dash when off
    skin_txtc(c, c->skin->semi, 16.5f, 1.4f, tx - 14, cy, 2, k > 0.5f ? hex(PAL(ok_ink, COL_BADGE_OK_INK), 1) : t->text2, k > 0.5f ? "ON" : "OFF");
}

static void sw_bake(DItem *it, SwRow *r)
{
    IBake ib = {.w = r->w, .h = r->h, .r = 8.0f, .fill = COL_SLIP, .paint = paint_sw_row, .pud = r};
    item_bake(it, 0, &ib);
}

int ui_choice(const char *headline, const char *detail, const UiChoice *items, int n, int sel)
{
    return ui_choice_sw(headline, detail, items, n, sel, NULL, NULL);
}

int ui_choice_sw(const char *headline, const char *detail, const UiChoice *items, int n, int sel, int (*flip)(void *user, int index, int want), void *user)
{
    if (G.quit) return -1;
    Skin *u = &G.skin;
    const Theme *t = &u->th;
    char lines[6][160];
    int nl = detail && detail[0] ? ui_wrap(u->reg, 19, 560, detail, lines, 6) : 0;
    Headline hl;
    headline_build(&hl, headline, 600);
    float hx = headline_extra(&hl);
    float w = 700;
    float y_rows = nl ? 126 + hx + (nl - 1) * 26 + 32 : 122 + hx;
    float rh_plain = 72, rh_sub = 96, rows_h = 0;
    if (n > 16) n = 16;
    int any_sw = 0, state[16];
    for (int i = 0; i < n; i++) {
        rows_h += items[i].sub ? rh_sub : rh_plain;
        any_sw |= items[i].sw != 0;
        state[i] = items[i].sw == 2;
    }
    if (y_rows + rows_h + 32 > 640) { // a long list: tighter rows, so the box still fits on the screen
        rh_plain = 58;
        rh_sub = 82;
        rows_h = 0;
        for (int i = 0; i < n; i++) rows_h += items[i].sub ? rh_sub : rh_plain;
    }
    float h = y_rows + rows_h + 32;
    float x = (SCREEN_W - w) / 2, y = (SCREEN_H - h) / 2 - 6;

    Cx c = dlg_open(x, y, w, h, -0.3f, 11);
    headline_draw(&c, 640, y + 80, &hl);
    for (int i = 0; i < nl; i++) skin_txt(&c, u->reg, 19, 0, 640, y + 126 + hx + i * 26, 1, t->text2, lines[i]);
    Scene *sc = scene_new(G.dim);
    if (!sc) return -1;
    SwRow rows[16];
    float yy = y + y_rows;
    for (int i = 0; i < n; i++) {
        float rh = items[i].sub ? rh_sub : rh_plain, rw = w - 56, ih = rh - 12;
        DItem *it = item_add(sc, x + 28, yy + 6, rw, ih, 8.0f, LIFT_ROW, paper_lift_k[LIFT_ROW].scale);
        if (!it) break;
        if (any_sw) {
            rows[i] = (SwRow){items[i].title, items[i].sub, NULL, items[i].dim, items[i].sw, state[i] ? 1.0f : 0.0f, rw, ih};
            sw_bake(it, &rows[i]);
        } else {
            IBake ib = {rw, ih, 8.0f, COL_SLIP, NULL, items[i].title, u->semi, 23, items[i].dim ? t->text2 : t->text, NULL, 0, t->text2, items[i].sub, NULL, NULL};
            item_bake(it, 0, &ib);
        }
        yy += rh;
    }
    if (sel < 0 || sel >= sc->n) sel = 0;
    scene_start_up(sc, sel);
    scene_frame(sc, 1);
    int rc = -1;
    for (;;) {
        int anim = scene_frame(sc, 0);
        uint64_t b = ui_poll(anim ? 0 : -1);
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te))
                if (te.kind == TOUCH_TAP) {
                    int i = scene_hit(sc, te.x, te.y);
                    if (i >= 0) {
                        sel = i;
                        b |= BTN_A;
                    }
                }
        }
        if (b == UI_QUIT) break;
        if ((b & BTN_A) && any_sw && items[sel].sw) { // a switch: it is set, shown as it really is, and the list stays
            DItem *it = &sc->it[sel];
            SwRow *r = &rows[sel];
            scene_press(sc, sel);
            int want = !state[sel], real = flip ? flip(user, sel, want) : state[sel];
            r->err = NULL;
            if (real < 0 || real == state[sel]) { // it could not be done (or did not take): the switch stays, and the row says so
                r->err = "Couldn\xE2\x80\x99t change it. Is the SD card full or locked?";
                sw_bake(it, r);
                scene_lean(sc, sel, 1, 0);
                continue;
            }
            ui_sound(real ? SND_STICK : SND_PEEL, 0.6f, 0);
            float from = r->k, to = real ? 1.0f : 0.0f;
            uint64_t t0 = ui_now();
            for (;;) {
                float p = (float)(ui_now() - t0) / 190.0f;
                if (p > 1.0f) p = 1.0f;
                r->k = from + (to - from) * (p * p * (3.0f - 2.0f * p));
                sw_bake(it, r);
                scene_frame(sc, 0);
                if (p >= 1.0f || ui_poll(0) == UI_QUIT) break;
            }
            r->k = to;
            sw_bake(it, r);
            state[sel] = real;
            continue;
        }
        int held = G.plat->repeated != 0, was = sel, pdx = 0, pdy = 0;
        if (b & BTN_DOWN) { if (sel < sc->n - 1) sel++; else if (sc->n > 1 && wrap_ok()) sel = 0; else pdy = 1; }
        else if (b & BTN_UP) { if (sel > 0) sel--; else if (sc->n > 1 && wrap_ok()) sel = sc->n - 1; else pdy = -1; }
        else if (b & BTN_A) {
            scene_press(sc, sel);
            rc = sel;
            break;
        }
        else if (b & BTN_B) break;
        else lean_dir(b, &pdx, &pdy); // left and right lead nowhere
        if (pdx || pdy) scene_lean(sc, sel, pdx, pdy);
        if (sel != was) scene_focus(sc, sel, held);
    }
    scene_free(sc);
    return rc;
}

// ------------------------------------------------------------------------------------------------ the six-digit pad

static const char *PAD_KEYS[4][3] = {{"1", "2", "3"}, {"4", "5", "6"}, {"7", "8", "9"}, {"Delete", "0", "Cancel"}};

enum { PAD_KW = 118, PAD_KH = 66, PAD_SX = 138, PAD_SY = 82, PAD_X0 = 443 };

typedef struct {
    float y;           // the card's top
    int len;           // digits typed
    char msg[96];
    PaperSpr *fill[6]; // how each typed digit's ring was coloured in (a new scribble each time one is typed)
    uint8_t *order[6]; // when the crayon reaches each of its pixels (0..255 through the scribbling)
    PaperSpr *part[6]; // while it is being scribbled in: as far as it has got
    uint64_t t0[6];    // when it was typed
    int busy;          // some are still being scribbled in (the clock is read only then)
} PadExtra;

#define PAD_SCRIBBLE_MS 100.0f // how long a digit's ring takes to be scribbled in (each on its own clock: fast typing scribbles several at once)
enum { PAD_RUNS = 12 };        // the flurry: the crayon's way cut into this many runs, which land in a random order

// the rings' colouring in, as far as `len` digits: a digit typed is scribbled in anew, one taken off loses its scribble
static void pad_fills(PadExtra *e, int len)
{
    static uint32_t n;
    for (int i = 0; i < 6; i++) {
        if (i < len && !e->fill[i]) {
            e->t0[i] = ui_now();
            const uint32_t seed = (uint32_t)e->t0[i] * 2654435761u ^ (++n * 40503u) ^ (uint32_t)i * 977u;
            e->fill[i] = paper_disc_scribble(10.6f, PAL(ink, PAPER_INK), 0.94f, seed, &e->order[i]);
            if (e->fill[i] && e->order[i]) { // when each pixel lands: its run's place in a shuffle of the runs (a new shuffle for every digit)
                uint8_t perm[PAD_RUNS];
                PaperRng r;
                paper_rng_seed(&r, seed ^ 0x5CA1AB1Eu);
                for (int k = 0; k < PAD_RUNS; k++) perm[k] = (uint8_t)k;
                for (int k = PAD_RUNS - 1; k > 0; k--) {
                    int j = (int)(paper_rng_f(&r) * (k + 1));
                    uint8_t t = perm[k];
                    perm[k] = perm[j];
                    perm[j] = t;
                }
                for (int k = 0, np = e->fill[i]->w * e->fill[i]->h; k < np; k++)
                    e->order[i][k] = (uint8_t)((perm[e->order[i][k] * PAD_RUNS / 256] * 2 + 1) * 255 / (2 * PAD_RUNS));
            }
            e->busy = 1;
        } else if (i >= len && e->fill[i]) {
            paper_spr_free(e->fill[i]);
            paper_spr_free(e->part[i]);
            free(e->order[i]);
            e->fill[i] = e->part[i] = NULL;
            e->order[i] = NULL;
        }
    }
    e->len = len;
}

// The rings being scribbled in now, as far as each has got (made here, on the app's thread, before the frame is painted): 1 while any still is,
// 2 when the last of them has just got all the way (it is drawn whole once more), else 0.
static int pad_scribbling(PadExtra *e)
{
    int on = 0, done = 0;
    if (!e->busy) return 0;
    const uint64_t now = ui_now();
    for (int i = 0; i < e->len; i++) {
        const PaperSpr *f = e->fill[i];
        if (!f) continue;
        float u = (float)(now - e->t0[i]) / PAD_SCRIBBLE_MS;
        if (u >= 1.0f || !e->order[i]) {
            if (e->part[i]) done = 1;
            paper_spr_free(e->part[i]);
            e->part[i] = NULL;
            continue;
        }
        on = 1;
        if (!e->part[i]) e->part[i] = paper_spr_new(f->w, f->h);
        PaperSpr *p = e->part[i];
        if (!p) continue;
        p->ox = f->ox;
        p->oy = f->oy;
        const float edge = u * 1.08f * 255.0f; // (a run is down whole the moment its turn comes: they pop on, a few a frame)
        for (int k = 0, n = f->w * f->h; k < n; k++) {
            const uint8_t *s = f->px + (size_t)k * 4;
            uint8_t *d = p->px + (size_t)k * 4;
            d[0] = s[0];
            d[1] = s[1];
            d[2] = s[2];
            d[3] = e->order[i][k] <= edge ? s[3] : 0;
        }
    }
    e->busy = on;
    return on ? 1 : done ? 2 : 0;
}

// the six crayon rings (a typed digit is one coloured in: the same ring, scribbled in under it) and a note under them
static void pad_extra(void *ud, Surf *s, int ox, int oy, const Rect *R)
{
    PadExtra *e = ud;
    Rect band = {400, (int)e->y + 148, 480, 90};
    if (!rect_hit(&band, R)) return;
    for (int i = 0; i < 6; i++) {
        const PaperSpr *fill = i < e->len ? (e->part[i] ? e->part[i] : e->fill[i]) : NULL, *ring = ring_get(10.0f, 31u + (uint32_t)i, &PAPER_CRAYON_STD);
        const int cx = 545 + i * 38, cy = (int)lroundf(e->y + 177);
        if (fill) put(s, fill, cx - fill->ox, cy - fill->oy, ox, oy);
        if (ring) put(s, ring, cx - ring->ox, cy - ring->oy, ox, oy);
    }
    if (e->msg[0]) {
        Cx c = ui_cx(s);
        skin_txtc(&c, c.skin->semi, 17, 0, 640 - ox, e->y + 211 - oy, 1, hex(PAL(bad_ink, COL_BAD_INK), 1), e->msg);
    }
}

int ui_pin_entry(const char *who, int who_color, const char *prompt, const char *problem, char out[7])
{
    if (G.quit) return 0;
    Skin *u = &G.skin;
    const Theme *t = &u->th;
    float w = 540, h = 600, x = (SCREEN_W - w) / 2, y = 50;
    Cx c = dlg_open(x, y, w, h, -0.3f, 13);
    if (who) {
        skin_avatar(&c, 640, y + 60, 30, who, who_color);
        const PaperSpr *ring = ring_get(31.0f, 22u, &PAPER_CRAYON_STD);
        if (ring) paper_spr_put(c.s, ring, 640, (int)lroundf(y + 60));
    } else {
        badge(&c, 640, y + 60, 30, UI_INFO, 1);
    }
    char fit[160];
    text_fit(u->semi, 30, -0.2f, prompt, w - 60, fit, sizeof fit);
    skin_txt(&c, u->semi, 30, -0.2f, 640, y + 130, 1, t->text, fit);

    Scene *sc = scene_new(G.dim);
    if (!sc) return 0;
    PadExtra ex;
    memset(&ex, 0, sizeof ex);
    ex.y = y;
    snprintf(ex.msg, sizeof ex.msg, "%s", problem ? problem : "");
    sc->extra = pad_extra;
    sc->extra_ud = &ex;
    for (int r = 0; r < 4; r++)
        for (int k = 0; k < 3; k++) {
            DItem *it = item_add(sc, PAD_X0 + k * PAD_SX, y + 226 + r * PAD_SY, PAD_KW, PAD_KH, 10.0f, LIFT_KEY, 0.08f);
            if (!it) continue;
            int word = strlen(PAD_KEYS[r][k]) > 1;
            IBake ib = {PAD_KW, PAD_KH, 10.0f, COL_CANCEL, NULL, PAD_KEYS[r][k], word ? u->semi : u->med, word ? 21.0f : 34.0f, word ? t->text2 : t->text,
                        NULL, 0, t->text2, NULL, NULL, NULL};
            item_bake(it, 0, &ib);
        }
    int r = 1, k = 1, len = 0, rc = 0;
    char digits[8] = {0};
    scene_start_up(sc, r * 3 + k);
    scene_frame(sc, 1);
    for (;;) {
        int scribbling = pad_scribbling(&ex);
        if (scribbling) scene_inval(sc, 400, (int)y + 148, 480, 90);
        int anim = scene_frame(sc, 0) | (scribbling == 1);
        uint64_t b = ui_poll(anim ? 0 : -1);
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te))
                if (te.kind == TOUCH_TAP) {
                    int i = scene_hit(sc, te.x, te.y);
                    if (i >= 0) {
                        r = i / 3;
                        k = i % 3;
                        b |= BTN_A;
                    }
                }
        }
        if (b == UI_QUIT) break;
        int held = G.plat->repeated != 0, pdx = 0, pdy = 0, was = r * 3 + k, changed = 0;
        if (b & BTN_UP) { if (r > 0) r--; else pdy = -1; }
        else if (b & BTN_DOWN) { if (r < 3) r++; else pdy = 1; }
        else if (b & BTN_LEFT) { if (k > 0) k--; else pdx = -1; }
        else if (b & BTN_RIGHT) { if (k < 2) k++; else pdx = 1; }
        else if (b & BTN_X) {
            if (len > 0) digits[--len] = 0;
            ex.msg[0] = 0;
            changed = 1;
        } else if (b & BTN_B) { // Back: take the last digit off, and leave only when there is none
            if (len > 0) {
                digits[--len] = 0;
                ex.msg[0] = 0;
                changed = 1;
            } else {
                break;
            }
        } else if (b & BTN_A) {
            scene_press(sc, r * 3 + k); // (the key that was chosen: a tap names one that does not have the focus yet)
            const char *key = PAD_KEYS[r][k];
            ex.msg[0] = 0;
            changed = 1;
            if (!strcmp(key, "Cancel")) break;
            if (!strcmp(key, "Delete")) {
                if (len > 0) digits[--len] = 0;
            } else if (len < 6) {
                digits[len++] = key[0];
                digits[len] = 0;
                if (len == 6) { // all six are coloured in: show it (the last ones scribbled all the way in), and done
                    pad_fills(&ex, len);
                    for (int on = 1; on && !G.quit;) {
                        on = pad_scribbling(&ex) == 1;
                        scene_inval(sc, 400, (int)y + 148, 480, 90);
                        scene_frame(sc, 0);
                    }
                    memcpy(out, digits, 7);
                    rc = 1;
                    break;
                }
            }
        }
        if (changed) {
            pad_fills(&ex, len);
            scene_inval(sc, 400, (int)y + 148, 480, 90);
        }
        if (pdx || pdy) scene_lean(sc, r * 3 + k, pdx, pdy);
        if (r * 3 + k != was) scene_focus(sc, r * 3 + k, held);
    }
    pad_fills(&ex, 0);
    scene_free(sc);
    return rc;
}

// ------------------------------------------------------------------------------------------------ the on-screen keyboard

static const char *KB_ROWS[4] = {"1234567890", "QWERTYUIOP", "ASDFGHJKL'", "ZXCVBNM-._"};
enum { KB_KW = 84, KB_KH = 56, KB_GAP = 8 };

int ui_keyboard_pos(char c, int *row, int *col)
{
    if (c >= 'a' && c <= 'z') c = (char)(c - 32);
    for (int r = 0; r < 4; r++)
        for (int k = 0; KB_ROWS[r][k]; k++)
            if (KB_ROWS[r][k] == c) {
                *row = r;
                *col = k;
                return 1;
            }
    return 0;
}

static void kb_utf8_pop(char *s)
{
    size_t n = strlen(s);
    if (!n) return;
    n--;
    while (n > 0 && ((unsigned char)s[n] & 0xC0) == 0x80) n--;
    s[n] = 0;
}

// where key k of row r is (row 4: the Shift, space and Delete keys)
static void kb_key_rect(int r, int k, float *x, float *y, float *w, float *h)
{
    static const int span[3] = {2, 6, 2};
    float x0 = 640 - (10 * KB_KW + 9 * KB_GAP) / 2, y0 = 292;
    *y = y0 + r * (KB_KH + KB_GAP);
    *h = KB_KH;
    if (r < 4) {
        *x = x0 + k * (KB_KW + KB_GAP);
        *w = KB_KW;
        return;
    }
    *x = x0;
    for (int i = 0; i < k; i++) *x += span[i] * KB_KW + span[i] * KB_GAP;
    *w = span[k] * KB_KW + (span[k] - 1) * KB_GAP;
}

static int kb_idx(int r, int k) { return r < 4 ? r * 10 + k : 40 + k; }

// the board, the corner, the question, the crayon line the name is written on, and the prompts along the bottom (the keys are items)
static void kb_base(const char *label)
{
    Cx c = ui_cx(G.base);
    Skin *u = c.skin;
    const Theme *t = &u->th;
    skin_background(&c);
    SkinStatus st;
    ui_status(&st);
    skin_top_bar(&c, NULL, &st);
    char up[80];
    size_t i = 0;
    for (; label[i] && i < sizeof up - 1; i++) up[i] = (label[i] >= 'a' && label[i] <= 'z') ? (char)(label[i] - 32) : label[i];
    up[i] = 0;
    skin_txt(&c, u->semi, 15, 2.2f, 640, 132, 1, t->text2, up);
    static PaperSpr *line;
    static unsigned serial;
    if (paper_pal_stale(&serial)) { // (a new palette: the ink's colour)
        paper_spr_free(line);
        line = NULL;
    }
    if (!line) line = paper_underline(520, &PAPER_CRAYON_BLUE, 91u);
    if (line) paper_spr_put(c.s, line, 640 - 260, 262);
    static const Prompt pr[] = {{"A", "Type"}, {"X", "Delete"}, {"Y", "Shift"}, {"+", "Done"}, {"B", "Back"}};
    skin_footer(&c, pr, 5);
}

// the key of index i, looking as it does with (shift) or without it
static void kb_key_ib(int i, int shift, IBake *ib, char *lab)
{
    Skin *u = &G.skin;
    const Theme *t = &u->th;
    float x, y, w, h;
    kb_key_rect(i / 10, i % 10, &x, &y, &w, &h);
    memset(ib, 0, sizeof *ib);
    ib->w = w;
    ib->h = h;
    ib->r = 9.0f;
    ib->card = card_get((int)w, (int)h, 9.0f, 17u, D_PAD);
    ib->gring = t->text2;
    if (i < 40) {
        lab[0] = KB_ROWS[i / 10][i % 10];
        lab[1] = 0;
        if (!shift && lab[0] >= 'A' && lab[0] <= 'Z') lab[0] = (char)(lab[0] + 32);
        ib->font = u->med;
        ib->px = 30;
        ib->ink = t->text;
    } else {
        static const char *fn[3] = {"Shift", "space", "Delete"};
        snprintf(lab, 8, "%s", fn[i - 40]);
        ib->font = u->semi;
        ib->px = 21;
        ib->ink = (i == 40 && shift) ? hex(PAL(ink, PAPER_INK), 1) : t->text2;
    }
    ib->label = lab;
}

// (re)draws the keys' two looks as needed and shows the one for `shift`
static void kb_set_shift(Scene *sc, int shift, int *baked)
{
    if (!baked[shift]) {
        for (int i = 0; i < sc->n; i++) {
            IBake ib;
            char lab[8];
            int idx = i < 40 ? i : i; // (items are added in index order)
            kb_key_ib(idx, shift, &ib, lab);
            item_bake(&sc->it[i], shift, &ib);
        }
        baked[shift] = 1;
    }
    for (int i = 0; i < sc->n; i++) {
        if (sc->it[i].var != shift) sc->it[i].force = 1;
        sc->it[i].var = shift;
    }
}

typedef struct { const char *text; } KbExtra;

// what has been typed, as large as fits, and the caret
static void kb_extra(void *ud, Surf *s, int ox, int oy, const Rect *R)
{
    const KbExtra *e = ud;
    Rect band = {200, 150, 880, 110};
    if (!rect_hit(&band, R)) return;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    float px = 84;
    while (px > 40 && text_width(u->semi, px, -1.0f, e->text) > 760) px -= 4;
    float tw = text_width(u->semi, px, -1.0f, e->text);
    skin_txt(&c, u->semi, px, -1.0f, 640 - ox, 236 - oy, 1, u->th.text, e->text);
    gfx_rrect(s, 640 + tw / 2 + 6 - ox, 236 - px * 0.78f - oy, 5, px * 0.94f, 2.5f, hex(PAL(ink, PAPER_INK), 1));
}

static int builtin_keyboard(const char *prompt, const char *initial, char *out, size_t cap)
{
    char text[64];
    snprintf(text, sizeof text, "%s", initial ? initial : "");
    size_t maxlen = cap - 1 < 31 ? cap - 1 : 31;
    int shift = text[0] == 0; // capital first letter
    int r = 1, k = 4;         // focus: row (4 = the function row) and column (function row: 0 shift, 1 space, 2 delete)
    kb_base(prompt);
    Scene *sc = scene_new(G.base);
    if (!sc) return 0;
    KbExtra ex = {text};
    sc->extra = kb_extra;
    sc->extra_ud = &ex;
    for (int i = 0; i < 43; i++) {
        float x, y, w, h;
        kb_key_rect(i / 10, i % 10, &x, &y, &w, &h);
        item_add(sc, x, y, w, h, 9.0f, LIFT_KEY, 0.08f);
    }
    int baked[2] = {0, 0};
    kb_set_shift(sc, shift, baked);
    scene_start_up(sc, kb_idx(r, k));
    ui_transition();
    scene_frame(sc, 1);
    int rc = 0;
    for (;;) {
        int anim = scene_frame(sc, 0);
        uint64_t b = ui_poll(anim ? 0 : -1);
        if (b == UI_QUIT) break;
        if (b & UI_REDRAW) {
            kb_base(prompt);
            sc->force_full = 1;
            b &= ~UI_REDRAW;
        }
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                if (te.kind != TOUCH_TAP) continue;
                int i = scene_hit(sc, te.x, te.y);
                if (i >= 0) {
                    r = i / 10;
                    k = i % 10;
                    b |= BTN_A;
                }
            }
        }
        int held = G.plat->repeated != 0, pdx = 0, pdy = 0, was = kb_idx(r, k), edit = 0; // (pdx, pdy: a press toward nothing)
        if (b & BTN_UP) {
            if (r == 4) { r = 3; k = k == 0 ? 1 : k == 1 ? 5 : 8; }
            else if (r > 0) r--;
            else pdy = -1;
        } else if (b & BTN_DOWN) {
            if (r == 3) { r = 4; k = k <= 1 ? 0 : k <= 7 ? 1 : 2; }
            else if (r < 3) r++;
            else pdy = 1;
        } else if (b & BTN_LEFT) {
            if (k > 0) k--;
            else pdx = -1;
        } else if (b & BTN_RIGHT) {
            if (k < (r == 4 ? 2 : 9)) k++;
            else pdx = 1;
        } else if (b & BTN_X) {
            kb_utf8_pop(text);
            edit = 1;
        } else if (b & BTN_Y) {
            shift = !shift;
            kb_set_shift(sc, shift, baked);
            edit = 1;
        } else if (b & BTN_PLUS) {
            if (text[0]) {
                snprintf(out, cap, "%s", text);
                rc = 1;
                break;
            }
        } else if (b & BTN_B) { // Back: delete a letter, and leave only when the text is empty
            if (text[0]) {
                kb_utf8_pop(text);
                edit = 1;
            } else {
                break;
            }
        } else if (b & BTN_A) {
            scene_press(sc, kb_idx(r, k));
            edit = 1;
            if (r == 4) {
                if (k == 0) {
                    shift = !shift;
                    kb_set_shift(sc, shift, baked);
                } else if (k == 2) {
                    kb_utf8_pop(text);
                } else if (strlen(text) < maxlen) {
                    strcat(text, " ");
                }
            } else if (strlen(text) < maxlen) {
                char ch = KB_ROWS[r][k];
                if (!shift && ch >= 'A' && ch <= 'Z') ch = (char)(ch + 32);
                size_t n = strlen(text);
                text[n] = ch;
                text[n + 1] = 0;
                if (shift && ch >= 'A' && ch <= 'Z') {
                    shift = 0; // capital first letter, then lower case
                    kb_set_shift(sc, shift, baked);
                }
            }
        }
        if (edit) scene_inval(sc, 200, 150, 880, 110);
        if (pdx || pdy) scene_lean(sc, kb_idx(r, k), pdx, pdy);
        if (kb_idx(r, k) != was) scene_focus(sc, kb_idx(r, k), held);
    }
    scene_free(sc);
    return rc;
}

int ui_text_input(const char *prompt, const char *initial, char *out, size_t cap, int builtin_only)
{
    if (G.quit) return 0;
    if (!builtin_only && !G.kb_unavailable && G.plat->text_input) {
        int rc = G.plat->text_input(G.plat, prompt, initial, out, cap);
        if (rc == 1) return out[0] != 0;
        if (rc == 0) return 0;
        G.kb_unavailable = 1; // not available here (applet mode, ...): use ours from now on
    }
    return builtin_keyboard(prompt, initial, out, cap);
}
