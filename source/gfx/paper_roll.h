// Washi's progress roll (the switching screen's bar): a roll of washi seen from above rolls along a hand-drawn dashed guide and lays its tape
// behind it. Each game gets its own strip: its own colour and pattern (never the same twice running), a little crooked, a little different in
// width, pressed down in one place (the crimp), running a little past the end of the guide, cut with a zigzag at the end; a finished strip peels
// away for the next game. Built on paper.h: the strip is one baked paper_tape(), the roll is a cylinder whose pattern is worked out per pixel
// (arcsine columns, 4x supersampled across each pixel, so it squeezes toward its edges and turns smoothly), and drawing a frame is a clipped
// blit plus a few cheap loops. Pure C, no platform code, seeded: the same seed gives the same strip on the PC and the console. Nothing here
// allocates while drawing.
#ifndef PAPER_ROLL_H
#define PAPER_ROLL_H

#include "paper.h"

#define ROLL_FRAMES 20      // turns of the roll that are baked (a turn every 18 degrees); at most ROLL_FRAMES_MAX
#define ROLL_FRAMES_MAX 32
#define ROLL_PHASE_N 64     // the table that turns progress into turns of the roll
#define ROLL_POP_MS 120     // the roll appears (scale .6 to 1, fade in)
#define ROLL_HOP_MS 170     // the used-up roll hops off
#define ROLL_PEEL_MS 260    // a finished strip peels away
#define ROLL_SPENT_MS 150   // the used-up roll shrinks away where the tape ended (the one-bar screen)
#define ROLL_DROP_MS 200    // the next roll drops in over the joint and lands with a one-frame squash
#define ROLL_V_REF 1310.0f  // px/s of roll travel at which the stick-slip taps are quickest (the mockup's v = 1)

#define ROLL_CATCH_MAX 3        // catches in one strip: the crimp (index 0, when the strip has one) and up to two snags
#define ROLL_CATCH_MS 260       // how long a catch's reaction lasts
#define ROLL_CATCH_STALL_MS 70  // how long the shown progress holds still at a catch before it catches up
#define ROLL_CATCH_CRIMP 0
#define ROLL_CATCH_SNAG 1

typedef struct { int x, y, w, h; } RollRect;
// a catch: the tape sticks to the roll or to itself where `progress` is reached, the roll hitches and shivers and the strip puckers a hair
typedef struct { float progress; int kind; float strength; } RollCatch; // strength 0.4..1; the crimp's is 1
typedef struct { int colour, kind; } RollPick; // colour: index into paper_tape_rgb; kind: PAT_*. {-1, -1} is "nothing before"

// The colour and pattern for a game, from its seed: never the colour or the pattern of `prev` (both change every time).
RollPick roll_pick(RollPick prev, uint32_t seed);
// the seed of game number `index` of a run that started from `base`
uint32_t roll_game_seed(uint32_t base, int index);

// One game's strip and roll, baked. The fields are for reading; change nothing but through roll_strip_new.
typedef struct {
    RollRect guide;      // the dashed box the strip lays itself along (the screen's own numbers)
    RollPick pick;
    uint32_t seed;
    int th;              // width of the tape, px
    float rot;           // how crooked it is, degrees clockwise
    float ox, oy;        // where the strip's left end (its middle) is, screen px
    float len;           // how far the roll travels, px: from the strip's left end to its cut end, past the guide's end a little
    float sx, sk, cx;    // the crimp: where its slanted cut starts, half its slant, its middle (px along the strip)
    float ky, kr;        // the strip beyond the crimp sits ky px to one side and is turned kr degrees
    float r0, r1;        // the roll's radius at the start and at the end (it gets used up)
    int rb;              // the roll's height across the strip (the body; the cardboard end shows below it)
    int n;               // frames baked
    PaperSpr *strip;     // the whole strip, crimped, tilted, zigzag cut at its far end, anchored at its middle
    PaperSpr *tail;      // not used (NULL): the mockup's roll has no loose end
    PaperSpr *frames[ROLL_FRAMES_MAX]; // not used (NULL): the roll is drawn from its pattern at any radius and turn, so it shrinks and turns smoothly
    float phase[ROLL_PHASE_N + 1];     // turns of the roll (in frames: n frames = one full turn) at progress k / ROLL_PHASE_N
    float sx_mid, sy_mid;              // the strip sprite's middle, screen px
    int ncatch;                        // catches[0..ncatch): sorted by progress; seeded like everything else
    RollCatch catches[ROLL_CATCH_MAX];
} RollStrip;

// Bakes one game's strip: its geometry and look come from `seed`, its colour and pattern from `pick`. `n` frames of roll (0 for ROLL_FRAMES).
RollStrip *roll_strip_new(const RollRect *guide, RollPick pick, uint32_t seed, int n);
// The numbers a seed turns into (the strip's own look and the crimp still come from the seed). roll_geo_from_seed gives what roll_strip_new
// uses; roll_strip_new_geo lets a test or a comparison sheet pin them, e.g. to the numbers read off the mockup.
typedef struct {
    int th;          // tape width, px (22..30)
    float dx, dy;    // where the strip's left end sits against the guide's left edge / middle line
    float rot;       // degrees clockwise (.35 to 1, either way)
    float ov;        // how far it runs past the guide's end
    float r0, r1;    // the roll's radius at the start and the end
    int no_crimp;    // 1: a plain strip (the one-bar screen), no pressed-down ridge and no crimp catch
    int plain_end;   // 1: the far end is the tape's own straight end (the roll ran out: roll_tail_draw), not a zigzag cut
} RollGeo;
RollGeo roll_geo_from_seed(uint32_t seed);
RollStrip *roll_strip_new_geo(const RollRect *guide, RollPick pick, uint32_t seed, int n, const RollGeo *geo);
void roll_strip_free(RollStrip *r); // NULL is fine
size_t roll_strip_bytes(const RollStrip *r); // what this strip's sprites hold (all of it is also in paper_stats())

// ---- drawing. `clip` limits where anything is painted (NULL: the whole surface), so a screen that restores only the changed rectangles can
// redraw into them; the same pixels come out as from an unclipped draw. progress is 0..1 (the caller eases it); the strip's laid length and
// the roll's place follow it. Draw the strip first, then the roll, then (once progress is 1) the hop or the peel.
void roll_strip_draw(Surf *s, const RollStrip *r, float progress, const RollRect *clip); // the tape laid so far; at 1 the zigzag cut shows
void roll_roll_draw(Surf *s, const RollStrip *r, float progress, const RollRect *clip);  // the roll with its shadow, turned as far as it has gone
void roll_roll_pop(Surf *s, const RollStrip *r, float t);                                 // t 0..1 over ROLL_POP_MS, at progress 0
void roll_roll_hop(Surf *s, const RollStrip *r, float t);                                 // t 0..1 over ROLL_HOP_MS, at progress 1
void roll_roll_spent(Surf *s, const RollStrip *r, float t);                               // t 0..1 over ROLL_SPENT_MS, at progress 1: shrinks and fades in place
void roll_roll_drop(Surf *s, const RollStrip *r, float t);                                // t 0..1 over ROLL_DROP_MS, at progress 0: falls in, one-frame squash
void roll_peel_draw(Surf *s, const RollStrip *r, float t, const RollRect *clip);          // t 0..1 over ROLL_PEEL_MS: the finished strip lifts and fades// the smallest rectangle that has to be redrawn when progress goes from p0 to p1 (roll at both, and the tape laid between); for a
// compositor. Includes the roll's shadow, tail and cardboard end.
RollRect roll_dirty(const RollStrip *r, float p0, float p1);
RollRect roll_bounds(const RollStrip *r);   // everything this strip can ever paint (strip, roll, hop and peel), for the screen's clear and restore

// ---- where things are, for the screen and for sound later
void roll_centre(const RollStrip *r, float progress, float *x, float *y); // the roll's middle on the screen
float roll_radius(const RollStrip *r, float progress);
int roll_frame_at(const RollStrip *r, float progress);                    // which baked frame is showing (turns forward as progress grows)
typedef struct {
    float crimp_progress; // the progress at which the crimp has just been laid (a rustle)
    float crimp_x;        // the screen x of the crimp's middle
    float cut_progress;   // 1: the strip is cut, with the zigzag (a tear), then the roll hops and the strip peels (a faint peel) unless it is the last
    float length;         // the roll's whole run, px
} RollEvents;
RollEvents roll_events(const RollStrip *r);
int roll_crossed_crimp(const RollStrip *r, float p0, float p1); // 1 if progress going from p0 to p1 lays the crimp (once)
float roll_angle(const RollStrip *r, float progress);         // the roll's (and the strip's there) tilt on screen, degrees clockwise: rot, plus the crimp's turn once past it

// ---- catches (for the screen, a sound and a haptic): the mockup's one catch is the crimp (a pressed-down pinch, a rustle); the strip also has
// 0..2 seeded snags, where the roll hitches. When progress crosses one (roll_crossed_catch), note the time; for the next ROLL_CATCH_MS draw with
// roll_catch_fx(index, ms since) and show roll_catch_progress() instead of the real progress, so the roll holds still, then catches up.
int roll_crossed_catch(const RollStrip *r, float p0, float p1); // 1 + index of the first catch that going from p0 to p1 crosses (forward only), else 0
typedef struct {
    float back_px;     // the roll is pulled back along the strip by this much and its pattern turns back with it
    float shiver_px;   // sideways jitter of the roll, px
    float shiver_deg;  // and its turn, degrees
    float pucker;      // 0..1: the tape bunches up in a light/dark ridge just behind the roll
} RollFx;
RollFx roll_catch_fx(const RollStrip *r, int index, float ms);   // all zero when ms is outside 0..ROLL_CATCH_MS
float roll_catch_progress(const RollStrip *r, int index, float p, float ms); // the progress to show: held at the catch, then eased up to p
int roll_catch_active(float ms); // 1 while a catch started ms ago is still moving anything
void roll_strip_draw_fx(Surf *s, const RollStrip *r, float progress, const RollRect *clip, const RollFx *fx);
void roll_roll_draw_fx(Surf *s, const RollStrip *r, float progress, const RollRect *clip, const RollFx *fx);
RollRect roll_dirty_catch(const RollStrip *r, float progress); // what a catch at (displayed) `progress` can touch: add it to roll_dirty while one runs
// the roll's speed in px/s for a change of `dprogress` over `dt_ms`
float roll_speed(const RollStrip *r, float dprogress, float dt_ms);
// ms between the stick-slip taps at that speed, as the mockup spaces them (64 slow, 10 fast), or 0 when the roll is too slow to tap
float roll_tap_gap_ms(float px_per_s);

#endif
