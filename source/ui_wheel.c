// Wheels in a dialog, in Washi: every value is its own paper slip, fanned like a deck. The slip in the middle of the wheel that has the
// focus is the unstuck one: it rises, its tape peels and it wiggles; the other wheel's middle slip lies flat with its tape stuck. Up and
// down turn the wheel the frame is on; left and right move the focus to the next wheel. On the touch screen a wheel follows the
// finger, and a flick sets it spinning: it slows down by itself, and as it slows the nearest value ahead catches it and pulls it in
// with a little spring, so it clicks into place.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_scene.h"

#define DLG_W 700.0f
#define DLG_H 556.0f
#define SLOT 62            // a value's height on its wheel (the strip the physics runs on)
#define SLIP_H 66.0f
#define VAL_PX 42.0f
#define UNIT_PX 22.0f
#define BTN_W 190.0f
#define BTN_H 62.0f
#define MAX_WHEELS 4
#define COAST_TAU 0.34f    // how long a flicked wheel takes to slow down (seconds)
#define FLICK_MIN 220.0f   // slower than this (pixels a second) when the finger lifts, the wheel just snaps to the nearest value
#define CATCH_SPEED 260.0f // a coasting wheel slower than this is caught by the next value
#define CATCH_AHEAD 0.09f  // which value catches it: the nearest to where it would be this much later (seconds)
#define SNAP_OMEGA 32.0f   // the snap: a spring this stiff (1/s)...
#define SNAP_ZETA 0.68f    // ...and a little under critically damped, so it pulls in with a hint of overshoot

typedef struct {
    float cx, unit_dx;  // the wheel's middle, and (in a slip with its unit) where the number ends from the slip's middle
    float sw;           // the slips' width
    int pos;            // where the wheel has been turned to, counting every step (its value is pos mod n), so it keeps turning the
                        // way it was going when it goes round
    float x, v;         // the turn shown (pixels along the strip) and how fast it is turning (pixels a second)
    int coast;          // spinning on after a flick; else, when x isn't at pos * SLOT, the snap spring pulls it there
    uint64_t last;      // when the finger last moved it
    PaperLift lift;     // the unstuck slip of this wheel (the middle one)
    PaperSpr **plain, **unit; // the slips baked as they are first needed: the number alone, and the number with its unit
    PaperPeel *peel;    // this wheel's tape, stuck to peeled
    float num_right;
    int tick_v;         // the value that was last in the middle (for the taps)
} Drum;

static int wrap(int i, int n) { return ((i % n) + n) % n; }
static float rad(float deg) { return deg * 3.14159265f / 180.0f; }

// the turn shown, in values (rounded): what is in the middle right now
static int shown(const Drum *d) { return (int)lroundf(d->x / SLOT); }

// a spinning wheel stops coasting: the value nearest to where it is heading becomes the one it snaps to
static void catch_wheel(Drum *d)
{
    if (!d->coast) return;
    d->coast = 0;
    d->pos = (int)lroundf((d->x + d->v * CATCH_AHEAD) / SLOT);
}

// One step of a wheel's motion: coasting slows by friction until a value catches it; the snap is a damped spring towards
// pos * SLOT that starts at whatever speed the wheel has, so nothing ever jumps. 1 while it moves.
static int drum_step(Drum *d, float dt)
{
    if (d->coast) {
        float k = expf(-dt / COAST_TAU);
        d->x += d->v * COAST_TAU * (1.0f - k);
        d->v *= k;
        if (fabsf(d->v) < CATCH_SPEED) catch_wheel(d);
        return 1;
    }
    float tgt = d->pos * (float)SLOT;
    if (fabsf(d->x - tgt) < 0.25f && fabsf(d->v) < 12.0f) {
        d->x = tgt;
        d->v = 0;
        return 0;
    }
    for (float t = 0; t < dt; t += 0.002f) { // small steps keep the spring steady at any frame rate
        float h = dt - t < 0.002f ? dt - t : 0.002f;
        float a = -SNAP_OMEGA * SNAP_OMEGA * (d->x - tgt) - 2.0f * SNAP_ZETA * SNAP_OMEGA * d->v;
        d->v += a * h;
        d->x += d->v * h;
    }
    return 1;
}

static const char *unit_of(const UiWheel *w, int pos)
{
    const char *v = w->values[wrap(pos, w->n)];
    return w->unit_one && !strcmp(v, "1") ? w->unit_one : w->unit;
}

// ---------------------------------------------------------------------------------------------------- the slips

typedef struct {
    const PaperSpr *card;
    const char *value, *unit;
    float sw, num_right;
    Col ink;
} SlipBake;

static void draw_slip(Surf *s, void *ud)
{
    const SlipBake *b = ud;
    Cx c = ui_cx(s);
    Skin *u = c.skin;
    paper_spr_put(s, b->card, D_PAD, D_PAD);
    float cy = D_PAD + SLIP_H * 0.5f + 1.0f;
    if (b->unit) {
        skin_txtc(&c, u->semi, VAL_PX, 0, D_PAD + b->num_right, cy, 2, b->ink, b->value);
        skin_txtc(&c, u->semi, UNIT_PX, 0, D_PAD + b->num_right + 14, cy + 3, 0, b->ink, b->unit);
    } else {
        skin_txtc(&c, u->semi, VAL_PX, 0, D_PAD + b->sw * 0.5f, cy, 1, b->ink, b->value);
    }
}

// the slip of value k (0..n-1) of wheel w; with its unit when `with_unit`
static const PaperSpr *slip_get(Drum *d, const UiWheel *w, int k, int with_unit)
{
    PaperSpr **cache = with_unit ? d->unit : d->plain;
    if (cache[k]) return cache[k];
    SlipBake sb = {card_get((int)d->sw, (int)SLIP_H, 7.0f, 90u + (uint32_t)k, D_PAD), w->values[k], with_unit ? unit_of(w, k) : NULL, d->sw, d->num_right,
                   with_unit ? hex(PAL(text, PAPER_TEXT), 1) : hex(PAL(text_soft, PAPER_TEXT_SOFT), 1)};
    if (with_unit && !sb.unit) sb.unit = "";
    cache[k] = paper_bake((int)d->sw + 2 * D_PAD, (int)SLIP_H + 2 * D_PAD, draw_slip, &sb);
    return cache[k];
}

typedef struct {
    Drum *dr;
    UiWheel *wh;
    int nw, f;
    float bx, cy;
    double now;
} WPaint;

static const int TAPE_RGB[2] = {5, 1};
static const int TAPE_KIND[2] = {PAT_STRIPES, PAT_DOTS};

// the wheels as of this frame, painted over the card in the rectangle R (offset (ox, oy) from the screen)
static void paint_wheels(void *ud, Surf *s, int ox, int oy, const Rect *R)
{
    const WPaint *wp = ud;
    (void)R;
    const PaperShadows *sh = shadows_get((int)wp->dr[0].sw, (int)SLIP_H, 7.0f, 0.0f);
    const PaperSpr *ghost = ghost_get((int)wp->dr[0].sw, (int)SLIP_H, 8.0f);
    for (int i = 0; i < wp->nw; i++) {
        Drum *d = &wp->dr[i];
        const UiWheel *w = &wp->wh[i];
        float cxw = d->cx + (i == wp->f ? wp->bx : 0.0f);
        PaperPose ps;
        paper_lift_pose(&d->lift, wp->now, d->sw, &ps);
        float xs = d->x / SLOT;
        int c0 = (int)lroundf(xs);
        if (ghost && ps.ghost > 0.004f) paper_spr_xform(s, ghost, cxw - ox, wp->cy - oy, 1.0f, 0.0f, ps.ghost); // where the unstuck slip was
        int ks[7];
        float ds[7];
        int n = 0;
        for (int k = c0 - 3; k <= c0 + 3; k++) {
            float dd = k - xs;
            if (fabsf(dd) <= 2.7f) {
                ks[n] = k;
                ds[n++] = dd;
            }
        }
        for (int a = 1; a < n; a++) { // the farthest first, the middle last
            int kv = ks[a];
            float dv = ds[a];
            int b = a;
            while (b > 0 && fabsf(ds[b - 1]) < fabsf(dv)) {
                ks[b] = ks[b - 1];
                ds[b] = ds[b - 1];
                b--;
            }
            ks[b] = kv;
            ds[b] = dv;
        }
        for (int j = 0; j < n; j++) {
            float dd = ds[j], ad = fabsf(dd), side = dd < 0 ? -1.0f : 1.0f;
            int v = wrap(ks[j], w->n);
            float y = wp->cy + side * (fminf(ad, 1.0f) * 62.0f + fmaxf(ad - 1.0f, 0.0f) * 46.0f);
            float sc = 1.0f - 0.08f * fminf(ad, 2.0f);
            float al = 1.0f - (ad - 1.5f) / 1.1f;
            if (al > 1.0f) al = 1.0f;
            if (al <= 0.01f) continue;
            float mid = 1.0f - ad * 1.6f; // how much of the unstuck pose this slip has (the one in the middle has all of it)
            if (mid < 0) mid = 0;
            float cx = cxw + ps.x * mid, cy = y + ps.y * mid, sca = sc + (ps.scale - 1.0f) * mid;
            float ang = rad((float)((v * 37 + 11) % 7 - 3) * 0.55f) * (1.0f - mid * 0.5f) + ps.angle * mid;
            const PaperSpr *spr = slip_get(d, w, v, ad < 0.5f);
            if (!spr) continue;
            if (sh && al > 0.55f) {
                const PaperSpr *sps = sh->s[paper_shadow_level(sh, ps.p * mid)];
                put(s, sps, (int)lroundf(cxw - d->sw * 0.5f) - sps->ox, (int)lroundf(y - SLIP_H * 0.5f) - sps->oy, ox, oy);
            }
            paper_spr_xform(s, spr, cx - ox, cy - oy, sca, ang, al);
        }
        if (d->peel) { // the tape on the middle slip: stuck on the one lying flat, peeling on the one that is up
            PaperXf cf = {cxw + ps.x, wp->cy + ps.y, ps.scale, ps.angle};
            float dx, dy;
            paper_tape_offset(d->sw, SLIP_H, 6, -10, 56.0f, 22.0f, &dx, &dy);
            PaperXf t = paper_xf_attach(cf, dx, dy);
            const PaperSpr *ts = paper_peel_at(d->peel, ps.p);
            if (ts) paper_spr_xform(s, ts, t.x - ox, t.y - oy, t.scale, t.angle, 1.0f);
        }
    }
}

// ---------------------------------------------------------------------------------------------------- the dialog

static DItem *button(Scene *sc, float x, float y, const char *label, const char *glyph, uint32_t gfill)
{
    DItem *it = item_add(sc, x, y, BTN_W, BTN_H, 10.0f, LIFT_BTN, paper_lift_k[LIFT_BTN].scale);
    if (!it) return NULL;
    IBake ib = {.w = BTN_W, .h = BTN_H, .r = 10.0f, .fill = PAL(quiet, 0xF1EFF4), .label = label, .font = G.skin.semi, .px = 24, .ink = hex(PAL(text, PAPER_TEXT), 1),
                .glyph = glyph, .gfill = gfill, .gring = G.skin.th.text2};
    item_bake(it, 0, &ib);
    return it;
}

int ui_wheels(const char *headline, const char *detail, UiWheel *wh, int nw, const char *extra)
{
    if (G.quit || nw <= 0) return 0;
    if (nw > MAX_WHEELS) nw = MAX_WHEELS;
    Skin *u = &G.skin;
    float x = (SCREEN_W - DLG_W) / 2, y = (SCREEN_H - DLG_H) / 2 - 2, cy = y + 292;

    // where each wheel goes: its columns, and where its number ends in a slip that has its unit
    Drum dr[MAX_WHEELS];
    memset(dr, 0, sizeof dr);
    float spacing = nw > 2 ? 620.0f / nw : 300.0f, sw = spacing - 90.0f > 208.0f ? 208.0f : spacing - 90.0f;
    for (int i = 0; i < nw; i++) {
        UiWheel *w = &wh[i];
        float vw = 0, uw = 0;
        for (int k = 0; k < w->n; k++) {
            float tw = text_width(u->semi, VAL_PX, 0, w->values[k]);
            if (tw > vw) vw = tw;
        }
        if (w->unit) uw = text_width(u->semi, UNIT_PX, 0, w->unit);
        if (w->unit_one && text_width(u->semi, UNIT_PX, 0, w->unit_one) > uw) uw = text_width(u->semi, UNIT_PX, 0, w->unit_one);
        float content = vw + (uw > 0 ? 14 + uw : 0);
        Drum *d = &dr[i];
        d->sw = sw;
        d->cx = 640 + (i - (nw - 1) * 0.5f) * spacing;
        d->num_right = (sw - content) / 2 + vw;
        d->pos = w->sel < 0 || w->sel >= w->n ? 0 : w->sel;
        d->x = d->pos * (float)SLOT;
        d->tick_v = d->pos;
        d->plain = calloc((size_t)w->n, sizeof *d->plain);
        d->unit = calloc((size_t)w->n, sizeof *d->unit);
        paper_lift_init(&d->lift, LIFT_TILE);
        PaperTape t = {56, 22, paper_tape_rgb[TAPE_RGB[i & 1]], TAPE_KIND[i & 1], PAPER_TAPE_ZIG, i & 1 ? -20.0f : -38.0f, 55u + (uint32_t)i};
        paper_tape_host(&t, sw, SLIP_H, 7.0f, 6.0f, -10.0f);
        d->peel = paper_tape_peel_lazy(&t, PAPER_PEEL_STATES);
        if (!d->plain || !d->unit) {
            for (int k = 0; k <= i; k++) {
                free(dr[k].plain);
                free(dr[k].unit);
                paper_peel_free(dr[k].peel);
            }
            return 0;
        }
    }

    // what doesn't move: the card on its tape, its words and the buttons
    Cx c = dlg_open(x, y, DLG_W, DLG_H, -0.3f, 21);
    char fit[160];
    text_fit(u->semi, 34, -0.3f, headline, DLG_W - 80, fit, sizeof fit);
    skin_txt(&c, u->semi, 34, -0.3f, 640, y + 78, 1, hex(PAL(text, PAPER_TEXT), 1), fit);
    if (detail && detail[0]) {
        text_fit(u->med, 20, 0, detail, DLG_W - 80, fit, sizeof fit);
        skin_txt(&c, u->med, 20, 0, 640, y + 114, 1, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), fit);
    }
    Scene *sc = scene_new(G.dim);
    int nch = extra ? 3 : 2;
    float bgap = 19, bx0 = 640 - (nch * BTN_W + (nch - 1) * bgap) / 2, by = y + DLG_H - 37 - BTN_H;
    if (sc) {
        button(sc, bx0, by, "Cancel", "B", paper_tape_rgb[0]);
        if (extra) button(sc, bx0 + BTN_W + bgap, by, extra, "Y", paper_tape_rgb[2]);
        button(sc, bx0 + (nch - 1) * (BTN_W + bgap), by, "OK", "A", paper_tape_rgb[5]);
    }
    WPaint wp = {dr, wh, nw, 0, 0, cy, (double)ui_now()};
    if (sc) {
        sc->extra = paint_wheels;
        sc->extra_ud = &wp;
    }

    int f = 0, result = -1, held = -1; // held: the wheel a finger is turning (-2: a drag that began off the wheels)
    Bump bump = {0};
    uint64_t last = ui_now();
    int first = 1;
    while (result < 0 && sc) {
        uint64_t now = ui_now();
        float dt = now > last ? (float)(now - last) / 1000.0f : 0.0f;
        if (dt > 0.05f) dt = 0.05f; // a slow frame never leaps
        last = now;
        int moving = 0;
        for (int i = 0; i < nw; i++) {
            if (i != held) moving |= drum_step(&dr[i], dt); // (the one under a finger goes where the finger takes it)
            moving |= paper_lift_busy(&dr[i].lift, (double)now);
        }
        float bx, by2;
        moving |= bump_offset(&bump, &bx, &by2); // Left on the first wheel, Right on the last: the wheel leans and comes back
        for (int i = 0; i < nw; i++) { // a slip goes by the middle: a tap
            int v = shown(&dr[i]);
            if (v != dr[i].tick_v) {
                dr[i].tick_v = v;
                if (!first) ui_sound(SND_SLIP0 + ((v % 4) + 4) % 4, 0.5f, 0);
            }
        }
        wp.f = f;
        wp.bx = roundf(bx);
        wp.now = (double)now;
        scene_inval(sc, (int)(x + 24), (int)(cy - 160), (int)DLG_W - 48, 320);
        int anim = scene_frame(sc, first);
        if (first) {
            first = 0;
            paper_lift_grab(&dr[f].lift, (double)ui_now(), 0); // the first wheel is unstuck as the dialog arrives
            moving = 1;
        }
        uint64_t b = ui_poll(moving || anim ? 0 : -1);
        if (b == UI_QUIT) {
            result = 0;
            break;
        }
        if (b & UI_TOUCH) { // the buttons; a wheel: turn it with a finger and flick it, or tap a value to bring it to the middle
            UiTouchEvt te;
            b &= ~UI_TOUCH;
            while (ui_touch_next(&te)) {
                int col = -1;
                for (int i = 0; i < nw; i++)
                    if (in_rect(te.x, te.y, dr[i].cx - spacing / 2, cy - 140, spacing, 280)) col = i;
                uint64_t t = ui_now();
                if (te.kind == TOUCH_DRAG) {
                    if (held == -1) { // the finger starts turning a wheel (it can wander off it after that)
                        held = col >= 0 ? col : -2;
                        if (col >= 0) { // it stops where it is, under the finger
                            Drum *d = &dr[col];
                            d->coast = 0;
                            d->v = 0;
                            d->last = t;
                            if (f != col) {
                                paper_lift_release(&dr[f].lift, (double)t, 0);
                                f = col;
                                paper_lift_grab(&dr[f].lift, (double)t, 0);
                            }
                        }
                    }
                    if (held >= 0) {
                        Drum *d = &dr[held];
                        float fdt = t > d->last ? (float)(t - d->last) / 1000.0f : 0.008f, v = -te.dy / fdt;
                        d->v = d->v * 0.4f + v * 0.6f; // how fast the finger is going, smoothed
                        d->last = t;
                        d->x -= te.dy;
                    }
                } else if (te.kind == TOUCH_END) {
                    if (held >= 0) { // let go: a flick spins on until a value catches it; a slow lift snaps to the nearest
                        Drum *d = &dr[held];
                        if (t - d->last > 90) d->v = 0; // the finger had stopped before it lifted
                        if (fabsf(d->v) >= FLICK_MIN) {
                            d->coast = 1;
                        } else {
                            d->pos = (int)lroundf((d->x + d->v * CATCH_AHEAD) / SLOT);
                        }
                    }
                    held = -1;
                    last = ui_now(); // the motion carries on from the moment of letting go
                } else if (te.kind == TOUCH_TAP) {
                    int bi = scene_hit(sc, te.x, te.y);
                    if (bi >= 0) {
                        b |= bi == 0 ? BTN_B : bi == sc->n - 1 ? BTN_A : BTN_Y;
                    } else if (col >= 0) {
                        if (f != col) {
                            paper_lift_release(&dr[f].lift, (double)t, 0);
                            f = col;
                            paper_lift_grab(&dr[f].lift, (double)t, 0);
                        }
                        catch_wheel(&dr[f]);
                        dr[f].pos += (int)lroundf((te.y - cy) / SLOT);
                    }
                }
            }
        }
        if (b & (BTN_UP | BTN_DOWN)) catch_wheel(&dr[f]);
        int lean = 0, held_dpad = G.plat->repeated != 0;
        if (b & BTN_UP) dr[f].pos--;
        else if (b & BTN_DOWN) dr[f].pos++;
        else if (b & BTN_LEFT) {
            if (f > 0) {
                paper_lift_release(&dr[f].lift, (double)ui_now(), held_dpad);
                f--;
                paper_lift_grab(&dr[f].lift, (double)ui_now(), held_dpad);
            } else lean = -1;
        } else if (b & BTN_RIGHT) {
            if (f < nw - 1) {
                paper_lift_release(&dr[f].lift, (double)ui_now(), held_dpad);
                f++;
                paper_lift_grab(&dr[f].lift, (double)ui_now(), held_dpad);
            } else lean = 1;
        } else if (b & BTN_A) {
            scene_press(sc, sc->n - 1);
            result = 1;
        } else if (b & BTN_B) {
            scene_press(sc, 0);
            result = 0;
        } else if ((b & BTN_Y) && extra) {
            scene_press(sc, 1);
            result = 2;
        }
        if (lean) {
            bump_start(&bump, lean, 0);
            last = ui_now();
        }
    }
    if (result > 0)
        for (int i = 0; i < nw; i++) wh[i].sel = wrap(i == held ? shown(&dr[i]) : dr[i].pos, wh[i].n); // (a spinning one: where it stops)
    for (int i = 0; i < nw; i++) {
        for (int k = 0; k < wh[i].n; k++) {
            paper_spr_free(dr[i].plain[k]);
            paper_spr_free(dr[i].unit[k]);
        }
        free(dr[i].plain);
        free(dr[i].unit);
        paper_peel_free(dr[i].peel);
    }
    scene_free(sc);
    return result < 0 ? 0 : result;
}
