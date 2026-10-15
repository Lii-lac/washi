// The order menu (R3 on an order chip): a glassine sleeve of stickers, one sticker for each order, and the stickers' art (also the chips'
// label). The sleeve slides out from under the chip; the stickers stay in it, seen milky through it, and the focus slides one out of its
// mouth, its corner lifted off its liner (Up and Down: that one slides back in and the next comes out). A pulls it out and peels it off its
// liner in one motion: the peel runs on from the lifted corner, it comes free, it is carried to the chip left end low and rolled on.
#ifndef UI_STICKFX_H
#define UI_STICKFX_H

#include "gfx/paper.h"
#include "gfx/skin.h"
#include "ui.h"
#include "ui_fx.h"
#include "ui_motion.h"

// (the menu's way: g_ui_sort_anim; the old values name the ways tried before, all gone now but the sleeve)
enum { SFX_ROLL = 9, SFX_PAD = 10, SFX_ENVELOPE = 11, SFX_SLEEVE = 12, SFX_CARDFILE = 13, SFX_NOTEBOOK = 14, SFX_LAST = 14, SFX_MAX = 8 };

// ---- the art. A sticker is the box x, y, w x h (its die-cut white border stands 3 px outside it): a badge at its left end (its shape and the
// little picture printed on it say which order: a clock, an alphabet block, a burst, a stack of game cards, an arrow into a tray, a grid of
// pictures, an SD card) and the order's name. idx: the order's place in its menu (the sticker's colour).
void stk_sticker(Cx *c, float x, float y, float w, float h, const char *label, int idx);
// the same sticker as the chips show it, focused or not: printed a little soft (its colours a touch greyed and washed toward the paper's)
void stk_sticker_toned(Cx *c, float x, float y, float w, float h, const char *label, int idx);
void stk_back(Cx *c, float x, float y, float w, float h, const char *label);  // its back: the white label stock, where the glue is
void stk_place(Cx *c, float x, float y, float w, float h, const char *label); // its liner: glossy, in its shape, cut round
uint32_t stk_rgb(int idx);
void stk_badge(Cx *c, float bx, float by, float r, const char *label, int idx); // the badge alone, its middle at (bx, by)
int stk_kind(const char *label); // (which picture the badge has)
// how big a chip's sticker is, and where it goes on the chip (its box's left and top, from the chip's top left)
// (the card is as close round them on the left as on the right: the R3 mark's edge 10 px in, the sticker's end 10 px in)
#define STK_H 36.0f
#define STK_X 42.0f
#define STK_Y 6.0f
#define STK_PAD 7 // (room round a baked sticker's box: its border, a burst's points)
#define CHIP_R3_CX 25.0f // the R3 mark's middle, from the chip's left
static inline float stk_w(float chip_w) { return chip_w - 52.0f; }

// ---- the sleeve
typedef struct {
    int n, cur_mode, focus;           // how many orders; the one on the chip; the sticker out (k)
    float chip_x, chip_y, chip_w, chip_h, clip_y; // the chip, and its lower edge (what comes out from under it shows below it only)
    double W, H;                      // a sticker's box
    double t, tc;                     // seconds since it opened; since closing began (-1: not closing)
    int closing, pick, finished;
    char names[SFX_MAX][40];
    PaperSpr *st[SFX_MAX], *bk[SFX_MAX]; // each order's sticker and its back (premultiplied, STK_PAD of room round the box)
    PaperSpr *liner[SFX_MAX];         // each sticker's liner (k)
    PaperSpr *lay[3];                 // the sleeve's front (the glassine over what is in it), its back, its open flap
    double sx, sy;                    // the sticker's middle on the chip, the chip lying flat
    double land_x, land_y, land_sc, land_ang; // where a sticker goes on the chip (the chip as it lies: lifted if the focus is on it)
    int on_chip;                      // the order stuck on the chip (-1: the chip as it was)
    double on_t;
    int fly_o;                        // the sticker picked (-1: none)
    double fly_t0, fly_x, fly_y, fly_a; // when it came free of its liner (-1: not yet), and where it lay then
    double t_ex;                      // how long the pull out of the mouth takes before the peel
    int m, item[SFX_MAX];             // how many stickers are in it (the orders not on the chip), and which order each is
    double ow, oh, ox, oy, oa;        // the sleeve's body: its size, its middle at rest, how it lies (radians)
    double hx[SFX_MAX], hy[SFX_MAX], ha[SFX_MAX]; // each sticker's place in it (from the body's middle, along it), how it lies
    Mover slide;                      // how far the sleeve is out from under the chip (0 .. 1)
    Mover outm[SFX_MAX];              // how far each sticker is out of its mouth (px)
    float lift[SFX_MAX], liftv[SFX_MAX]; // the hover: how far its corner is lifted off its liner (0..1), how fast
    int shown, going;                 // the focus's one has been sent out; the way back under the chip has begun
    double nudge, nudgev;             // Left and Right: a little tug on the one out
    double toss_t0, toss_x, toss_y, toss_a; // the picked one's liner, let go once the sticker is off it: since when (-1: not), from where
    double toss_kx, toss_ky, toss_fall, toss_rp, toss_rx, toss_ra, toss_ph, toss_spin, toss_fade; // and how this one goes (drawn afresh at each toss)
    double flap_t0;                   // closing: when the flap began to fold shut over the mouth (-1: not yet)
    double go_t0;                     // closing: when the sleeve began to go back under the chip (-1: not yet)
    int chip_lifted;                  // the chip has the focus (it is lifted: where the new sticker lands)
} StickFx;

// kind: g_ui_sort_anim (only the sleeve is left; it is not read)
int sfx_init(StickFx *f, int kind, int n, const char *const *names, int cur_mode, float chip_x, float chip_y, float chip_w, float chip_h,
             int chip_lifted);
void sfx_free(StickFx *f);
Rect sfx_region(const StickFx *f);              // the part of the screen it draws in
void sfx_step(StickFx *f, double dt);           // runs it on by dt seconds (and makes its sounds)
int sfx_nav(StickFx *f, int dx, int dy);        // a D-pad press: 1 if it moved the focus (0: a lean; the caller bumps)

void sfx_close(StickFx *f, int pick);           // closing begins: pick is the order chosen (the focus), or -1
int sfx_moving(const StickFx *f);
int sfx_result(const StickFx *f);               // the order on the chip at the end (-1: as before)
// Draws it over dst (which has what is under it already), inside clip; ground: the screen as it was, for what must not show over the chip.
void sfx_draw(StickFx *f, Surf *dst, Rect clip, const Surf *ground);

#endif
