// How things move (source/ui_motion.c), checked with numbers on the PC: a press arrives quickly and stays, a new press mid-way
// bends the motion instead of restarting it, a thrown row slows all the way down and lands exactly on a stop, an end gives a
// little and springs back, and nothing ever jumps.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "../source/ui_motion.h"

static int g_checks, g_failed;
#define CHECK(c) CHECK_MSG(c, "%s", #c)
#define CHECK_MSG(c, ...)                                                 \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(c)) {                                                       \
            g_failed++;                                                   \
            printf("  FAIL [motion] %s:%d: ", __FILE__, __LINE__);        \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

#define PX (1.0f / 310.0f) // a row of tiles 310 pixels apart, measured in tiles (the Backups page)
#define FRAME (1.0f / 60.0f)

static const Throw ROW = {0.30f, 1.3f, 1.0f, 0.30f, 0.07f, MO_PRESS_OM, MO_PRESS_ZETA};
static const Throw PAGE = {0.30f, 80.0f, 0.0f, 0.66f, 0.0f, MO_PRESS_OM, MO_PRESS_ZETA}; // a page that may rest anywhere, in pixels

// every way of letting go of the row
static void test_throw(void)
{
    static const float X0[] = {0.0f, 0.05f, 0.3f, 0.5f, 0.6f, 0.97f, 1.02f, 2.4f, 4.6f, 5.0f};
    static const float V[] = {0.0f, 0.4f, 1.0f, 1.4f, 2.3f, 3.2f, 4.8f, 6.5f, 9.7f, 13.0f, 20.0f, 40.0f};
    const float lo = 0, hi = 5;
    int glides = 0, helped = 0;
    for (size_t i = 0; i < sizeof X0 / sizeof X0[0]; i++)
        for (size_t j = 0; j < sizeof V / sizeof V[0]; j++)
            for (int sign = -1; sign <= 1; sign += 2) {
                float x0 = X0[i], v0 = V[j] * (float)sign;
                Mover m;
                mo_snap(&m, 0, PX);
                mo_hold(&m);
                m.x = x0;
                mo_throw(&m, v0, &ROW, lo, hi);
                int glide = m.mode == MO_GLIDE;
                float tgt = m.tgt, dir = tgt > x0 ? 1.0f : -1.0f;
                CHECK_MSG(tgt >= lo && tgt <= hi && tgt == roundf(tgt), "x %.2f v %.1f: it heads for a stop (%f)", x0, v0, tgt);
                if (fabsf(v0) < ROW.flick) {
                    CHECK_MSG(tgt == roundf(x0), "x %.2f v %.1f: let go slowly it settles on the nearest stop (%f)", x0, v0, tgt);
                } else { // thrown: never a stop behind the way it was thrown (unless it is at the end)
                    float least = v0 > 0 ? fminf(ceilf(x0 - 0.02f), hi) : fmaxf(floorf(x0 + 0.02f), lo);
                    CHECK_MSG(v0 > 0 ? tgt >= least : tgt <= least, "x %.2f v %.1f: a throw goes on to the next stop at least (%f)", x0, v0, tgt);
                }
                float x = m.x, v = m.v, t = 0, far = 0, peak = fabsf(v0), dvmax = 0, span = fabsf(tgt - x0), T = m.T;
                int wrong = 0;
                CHECK_MSG(x == x0 && v == v0, "x %.2f v %.1f: letting go changes neither place nor speed", x0, v0);
                const float dt = FRAME / 8;
                while (mo_step(&m, dt) && t < 3.0f) {
                    t += dt;
                    if (fabsf(m.v - v) > dvmax) dvmax = fabsf(m.v - v);
                    if (glide && ((m.x - x) * dir < -0.02f * PX || (m.x - tgt) * dir > 0.02f * PX)) wrong = 1;
                    if (fabsf(m.v) > peak) peak = fabsf(m.v);
                    if (fabsf(m.x - tgt) > span) span = fabsf(m.x - tgt);
                    if (m.x < lo - far) far = lo - m.x;
                    if (m.x > hi + far) far = m.x - hi;
                    x = m.x;
                    v = m.v;
                }
                // the most the speed can change in a step: what the spring, or the glide's curve, can pull with
                float pull = glide ? (4 * fabsf(v0) * T + 5.8f * fabsf(tgt - x0)) / (T * T) : ROW.om * ROW.om * span + 2 * ROW.om * peak;
                CHECK_MSG(m.mode == MO_REST && m.x == tgt && m.v == 0, "x %.2f v %.1f: at rest on its stop (%f, wanted %f)", x0, v0, m.x, tgt);
                CHECK_MSG(t < 0.9f, "x %.2f v %.1f: there in under a second (%.2f s)", x0, v0, t);
                CHECK_MSG(dvmax <= pull * dt * 1.1f + 0.01f, "x %.2f v %.1f: the speed never jumps (%.2f in a 2 ms step, %.2f allowed)", x0, v0, dvmax, pull * dt * 1.1f);
                CHECK_MSG(far < 0.75f, "x %.2f v %.1f: an end gives only a little (%.2f tiles past it)", x0, v0, far);
                if (glide) {
                    glides++;
                    CHECK_MSG(!wrong, "x %.2f v %.1f: a glide never goes back on itself or past its stop", x0, v0);
                    if (peak > fabsf(v0) * 1.02f) helped++;
                    if (fabsf(v0) * ROW.t0 >= 1.7f * fabsf(tgt - x0)) // fast enough for the distance: it only slows down
                        CHECK_MSG(peak <= fabsf(v0) * 1.02f, "x %.2f v %.1f: a throw with speed to spare only slows down (peak %.1f)", x0, v0, peak);
                }
            }
    CHECK_MSG(glides > 60, "most throws glide (%d)", glides);
    CHECK_MSG(helped > 0 && helped < glides / 2, "the slow ones are helped along, the rest keep their own speed (%d of %d helped)", helped, glides);

    // thrown at the end it is on: past it a little, and back
    Mover m;
    mo_snap(&m, 5, PX);
    mo_hold(&m);
    mo_throw(&m, 13.0f, &ROW, lo, hi);
    float most = 0;
    while (mo_step(&m, FRAME)) most = fmaxf(most, m.x - hi);
    CHECK_MSG(most > 10 * PX && most < 0.5f && m.x == hi, "thrown at the end it runs %d px past it and comes back", (int)(most / PX));
    // let go past an end
    mo_snap(&m, 0, PX);
    mo_hold(&m);
    m.x = -0.4f;
    mo_throw(&m, -3.0f, &ROW, lo, hi);
    most = 0;
    int frames = 0;
    while (mo_step(&m, FRAME)) {
        most = fmaxf(most, -m.x);
        frames++;
    }
    CHECK_MSG(m.x == lo && frames < 24 && most < 0.5f, "let go past an end it comes back to the end (%d frames, %.2f out at most)", frames, most);

    // caught mid-glide by a finger, then by a press
    mo_snap(&m, 0.3f, PX);
    mo_hold(&m);
    mo_throw(&m, 9.0f, &ROW, lo, hi);
    for (int i = 0; i < 6; i++) mo_step(&m, FRAME);
    float x = m.x;
    mo_hold(&m);
    CHECK_MSG(m.x == x && m.v == 0 && !mo_step(&m, FRAME) && m.x == x, "a finger stops it where it is");
    mo_throw(&m, 9.0f, &ROW, lo, hi);
    for (int i = 0; i < 6; i++) mo_step(&m, FRAME);
    x = m.x;
    float v = m.v;
    mo_spring(&m, 1, MO_PRESS_OM, MO_PRESS_ZETA);
    CHECK_MSG(m.x == x && m.v == v && v > 1.0f, "a press mid-glide takes over where it is, at the speed it has (%.2f)", v);
    while (mo_step(&m, FRAME)) {}
    CHECK(m.x == 1.0f);
}

// a page that may rest anywhere (pixels)
static void test_page(void)
{
    Mover m;
    mo_snap(&m, 40, 1);
    mo_hold(&m);
    mo_throw(&m, 20.0f, &PAGE, 0, 400);
    CHECK_MSG(m.mode == MO_REST && m.x == 40.0f, "let go of slowly, a page stays where it is");
    mo_hold(&m);
    mo_throw(&m, 600.0f, &PAGE, 0, 400);
    float tgt = m.tgt, prev = m.x;
    int back = 0;
    while (mo_step(&m, FRAME)) {
        if (m.x < prev - 0.01f) back = 1;
        prev = m.x;
    }
    CHECK_MSG(fabsf(tgt - 220.0f) < 1.0f && m.x == tgt && !back, "thrown, it runs on as far as its speed carries it (%.0f)", m.x);
    mo_hold(&m);
    mo_throw(&m, 3000.0f, &PAGE, 0, 400);
    float most = 0;
    while (mo_step(&m, FRAME)) most = fmaxf(most, m.x);
    CHECK_MSG(m.x == 400.0f && most < 480.0f, "thrown hard at its end it stops there (%.0f at most)", most);
}

static void test_band(void)
{
    const float lo = 0, hi = 5, room = 3;
    CHECK(mo_band(2.5f, lo, hi, room) == 2.5f && mo_band(0, lo, hi, room) == 0 && mo_band(5, lo, hi, room) == 5);
    float prev = 0, worst = 0;
    int ok = 1;
    for (float over = 0.01f; over < 40; over *= 1.3f) {
        float g = mo_band(hi + over, lo, hi, room) - hi, g2 = lo - mo_band(lo - over, lo, hi, room);
        if (g <= prev || g >= room || g > over || fabsf(g - g2) > 1e-4f) ok = 0;
        float backx = mo_unband(hi + g, lo, hi, room) - hi;
        if (over < 10 && fabsf(backx - over) / over > worst) worst = fabsf(backx - over) / over;
        prev = g;
    }
    CHECK_MSG(ok, "past an end it follows less and less, the same at both ends, and never further than its room");
    CHECK_MSG(worst < 0.01f, "and it is picked up from where it shows (%.3f)", worst);
    CHECK_MSG(mo_band(hi + 0.1f, lo, hi, room) - hi > 0.05f, "a small pull is followed by about half (%.3f)", mo_band(hi + 0.1f, lo, hi, room) - hi);
    // how much of the finger's speed it has: all of it between the ends, and past one what the curve's slope there says
    float slope = (mo_band(hi + 1.01f, lo, hi, room) - mo_band(hi + 0.99f, lo, hi, room)) / 0.02f;
    CHECK_MSG(mo_band_rate(2.5f, lo, hi, room) == 1.0f && fabsf(mo_band_rate(hi + 1.0f, lo, hi, room) - slope) < 0.01f, "past an end it has part of the finger's speed (%.3f, slope %.3f)", mo_band_rate(hi + 1.0f, lo, hi, room), slope);
    CHECK_MSG(fabsf(mo_band_rate(lo - 1.0f, lo, hi, room) - slope) < 0.01f, "the same at the other end");
}

static void test_track(void)
{
    Track k;
    track_start(&k, 1000, 500);
    CHECK_MSG(track_speed(&k, 1010) == 0, "one place is no speed");
    for (int i = 1; i <= 8; i++) track_add(&k, 1000 + (uint64_t)i * 16, 500 - (float)i * 32); // 2000 px a second to the left
    CHECK_MSG(fabsf(track_speed(&k, 1000 + 8 * 16 + 16) + 2000) < 1, "a steady finger's speed (%.0f)", track_speed(&k, 1000 + 8 * 16 + 16));
    CHECK_MSG(track_speed(&k, 1000 + 8 * 16 + 120) == 0, "a finger that stopped before lifting throws nothing");
    track_start(&k, 2000, 100);
    track_add(&k, 2016, 100); // it rested ...
    track_add(&k, 2032, 100);
    track_add(&k, 2200, 110); // ... then, much later, went
    track_add(&k, 2216, 140);
    track_add(&k, 2232, 170);
    CHECK_MSG(fabsf(track_speed(&k, 2240) - 1875) < 1, "only its last moments count (%.0f)", track_speed(&k, 2240));
    track_start(&k, 4000, 0);
    for (int i = 1; i <= 5; i++) track_add(&k, 4000 + (uint64_t)i * 16, (float)i * 40); // fast ...
    for (int i = 6; i <= 10; i++) track_add(&k, 4000 + (uint64_t)i * 16, 200 + (float)(i - 5) * 4); // ... then slowing right down
    CHECK_MSG(track_speed(&k, 4170) < 300, "a finger that slowed down before lifting throws gently (%.0f)", track_speed(&k, 4170));
    track_start(&k, 3000, 0);
    track_add(&k, 3030, 90); // a quick flick: down, one place, up
    CHECK_MSG(fabsf(track_speed(&k, 3060) - 3000) < 1, "a flick of two places still has its speed (%.0f)", track_speed(&k, 3060));
}

int main(void)
{
    test_throw();
    test_page();
    test_band();
    test_track();
    printf("%d motion checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
