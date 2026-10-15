// Things that move the way a hand expects. A Mover is one value on a line (how far a row of tiles has slid, how far a page has
// scrolled). A press pulls it to a new place with a spring: off at once, firmly there, no creeping to a stop. A finger holds it and
// drags it, a little past its ends if pulled. Letting go throws it: it runs on at the speed it had, slowing down, and comes to rest
// exactly on a stop. Every change starts from where it is now at the speed it has now, so it can be caught and sent somewhere else
// at any moment and nothing ever jumps. No screen, no clock: the caller says how much time has passed (and test/test_motion.c
// checks all of the above with numbers).
#ifndef UI_MOTION_H
#define UI_MOTION_H

#include <stdint.h>

enum { MO_REST = 0, MO_SPRING, MO_GLIDE, MO_HELD };

// The spring in use: most of the way in three frames, there in about a seventh of a second, and it stays (half a pixel of
// overshoot on a tile's width).
#define MO_PRESS_OM 36.0f
#define MO_PRESS_ZETA 0.90f
// The shoulder buttons' spring (L, R, ZL, ZR): it runs about 8% past and swings back once (26 px on a tile's width), settled in a
// third of a second.
#define MO_BUMP_OM 30.0f
#define MO_BUMP_ZETA 0.62f

typedef struct {
    float x, v;         // where it is, and how fast it is going (units, units a second)
    float tgt;          // where it comes to rest
    int mode;           // MO_REST; MO_SPRING (pulled to tgt); MO_GLIDE (thrown, on its way to tgt); MO_HELD (a finger has it)
    float px;           // one pixel in its units: closer to rest than that, it is at rest
    float om, zeta;     // MO_SPRING: how stiff (1/s) and how damped (a little under 1: it arrives and stays, with no visible bounce)
    float lead, lead_v, lead_t; // mo_run: where the spring pulls it to right now, moving on to tgt at lead_v for another lead_t seconds
    float x0, v0, T, t; // MO_GLIDE: from x0 at speed v0, T seconds long, t seconds in
} Mover;

// What letting go does (mo_throw).
typedef struct {
    float carry;    // a throw is good for this many seconds of the speed it was let go at
    float flick;    // let go slower than this, it only settles on the nearest stop
    float step;     // how far apart the stops are (0: it may rest anywhere)
    float t0, t1;   // the glide to a stop d steps away takes at most t0 + t1 * d seconds (slower throws are helped along)
    float om, zeta; // the spring that settles it, and that brings it back from past an end
} Throw;

void mo_snap(Mover *m, float x, float px);                 // it is there, at rest
void mo_spring(Mover *m, float tgt, float om, float zeta); // from now on a spring pulls it to tgt
// The same for one step of a held button: the place the spring pulls it to moves over to tgt steadily, taking T seconds (the time
// between two steps), so the steps run together into one even motion instead of a string of little pulls.
void mo_run(Mover *m, float tgt, float om, float zeta, float T);
void mo_hold(Mover *m);                                    // a finger has it: it stays where it is put (set x) until it is thrown
// Let go at speed v, between the ends lo and hi: past an end it springs back; slow, it settles on the nearest stop; fast, it glides
// to the stop its speed carries it to (the next one that way at least), or, with no room left for its speed, runs into the end and back.
void mo_throw(Mover *m, float v, const Throw *w, float lo, float hi);
int mo_step(Mover *m, float dt); // dt seconds on; 1 while it is still moving by itself
static inline int mo_moving(const Mover *m) { return m->mode == MO_SPRING || m->mode == MO_GLIDE; }
// Past an end things follow the finger less and less (never further than `room`): where something dragged to `raw` shows, the
// other way round (to pick it up from where it shows), and how much of the finger's speed it has there (1 between the ends).
float mo_band(float raw, float lo, float hi, float room);
float mo_unband(float x, float lo, float hi, float room);
float mo_band_rate(float raw, float lo, float hi, float room);

// How fast a finger is going when it lifts: its last few places, with when it was there.
typedef struct {
    uint64_t t[6];
    float p[6];
    int n;
} Track;
void track_start(Track *k, uint64_t t_ms, float pos);
void track_add(Track *k, uint64_t t_ms, float pos);
float track_speed(const Track *k, uint64_t now_ms); // units a second over its last 70 ms; 0 if it had stopped before lifting

#endif
