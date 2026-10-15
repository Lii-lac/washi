// Washi's progress roll (source/gfx/paper_roll.c) checked on the PC: seeded and repeatable strips, the numbers a
// seed gives, the roll's path and turn, that clipped draws equal unclipped ones, that roll_dirty and roll_bounds cover everything that changes,
// the ends (pop, hop, peel), and nothing allocating while drawing. With --bench it prints the cost of a bake and of a frame; with --sheet <out> <mock> it also writes
// the contact sheets (mine above/beside the real-Chrome mockup screenshots in <mock>); with --bench only the timings.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "../source/gfx/stb_image_write.h"

#include "../source/gfx/paper_roll.h"

static int g_checks, g_failed;
#define CHECK(c) CHECK_MSG(c, "%s", #c)
#define CHECK_MSG(c, ...)                                                 \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(c)) {                                                       \
            g_failed++;                                                   \
            printf("  FAIL [roll] %s:%d: ", __FILE__, __LINE__);          \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)
#define NEAR(a, b, tol) CHECK_MSG(fabs((double)(a) - (double)(b)) <= (tol), "%s = %g, wanted %g (+-%g)", #a, (double)(a), (double)(b), (double)(tol))

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}

// the swap screen's guide in the mockup: the card sits at (250, 130), the bar at (184, 249) on it, 540 x 34
static const RollRect GUIDE = {434, 379, 540, 34};

static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static uint32_t spr_hash(const PaperSpr *s) { return fnv(s->px, (size_t)s->w * s->h * 4); }

static void paint_bg(Surf *s)
{
    gfx_fill(s, hex(PAPER_PAPER, 1));
}

// the guide, as dashes (the screen draws it in crayon; this is only for the sheet)
static void guide_dashes(Surf *s, const RollRect *g)
{
    Col c = hex(PAPER_THREAD, 0.9f);
    float x0 = (float)g->x, x1 = (float)g->x + g->w, y0 = (float)g->y, y1 = (float)g->y + g->h;
    for (float x = x0 - 4; x < x1 + 3; x += 16) {
        gfx_line(s, x, y0, fminf(x + 9, x1 + 3), y0, 2.5f, c);
        gfx_line(s, x, y1, fminf(x + 9, x1 + 3), y1, 2.5f, c);
    }
    for (float y = y0 - 3; y < y1 + 4; y += 16) {
        gfx_line(s, x0, y, x0, fminf(y + 9, y1 + 4), 2.5f, c);
        gfx_line(s, x1, y, x1, fminf(y + 9, y1 + 4), 2.5f, c);
    }
}

static void scene(Surf *s, const RollStrip *r, float p, int with_roll, int dashes)
{
    paint_bg(s);
    if (dashes) guide_dashes(s, &r->guide);
    roll_strip_draw(s, r, p, NULL);
    if (with_roll) roll_roll_draw(s, r, p, NULL);
}

static RollRect diff_box(const Surf *a, const Surf *b, long *count)
{
    int x0 = a->w, y0 = a->h, x1 = -1, y1 = -1;
    long n = 0;
    for (int y = 0; y < a->h; y++)
        for (int x = 0; x < a->w; x++)
            if (memcmp(a->px + ((size_t)y * a->w + x) * 4, b->px + ((size_t)y * b->w + x) * 4, 3)) {
                n++;
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (count) *count = n;
    RollRect r = {0, 0, 0, 0};
    if (n) { r.x = x0; r.y = y0; r.w = x1 - x0 + 1; r.h = y1 - y0 + 1; }
    return r;
}
static int inside(RollRect box, RollRect big) { return box.w == 0 || (box.x >= big.x && box.y >= big.y && box.x + box.w <= big.x + big.w && box.y + box.h <= big.y + big.h); }

// ------------------------------------------------------------------------------------------------ the strip's numbers
static void test_geometry(void)
{
    int th_min = 99, th_max = 0;
    for (uint32_t seed = 1; seed <= 600; seed++) {
        RollGeo g = roll_geo_from_seed(seed);
        if (g.th < th_min) th_min = g.th;
        if (g.th > th_max) th_max = g.th;
        CHECK_MSG(g.th >= 22 && g.th <= 30, "th %d", g.th);
        CHECK_MSG(fabsf(g.rot) >= 0.35f - 1e-4f && fabsf(g.rot) <= 1.0f + 1e-4f, "rot %g", g.rot);
        CHECK_MSG(g.dx >= -9.0f && g.dx <= 4.0f, "dx %g", g.dx);
        CHECK_MSG(g.ov >= 4.0f && g.ov <= 14.0f, "ov %g", g.ov);
        CHECK_MSG(g.dy * g.rot <= 0.0f && fabsf(g.dy) <= 4.0f, "dy %g rot %g (always visibly crooked: it leans against its offset)", g.dy, g.rot);
        CHECK_MSG(g.r0 >= 24.0f && g.r0 < 28.0f && g.r1 >= 15.0f && g.r1 < g.r0, "radii %g %g", g.r0, g.r1);
    }
    CHECK(th_min <= 23 && th_max >= 29); // the range is used
    RollGeo a = roll_geo_from_seed(77), b = roll_geo_from_seed(77), c = roll_geo_from_seed(78);
    CHECK(!memcmp(&a, &b, sizeof a));
    CHECK(memcmp(&a, &c, sizeof a) != 0);

    RollStrip *r = roll_strip_new(&GUIDE, (RollPick){0, PAT_STRIPES}, 4242, 0);
    CHECK(r != NULL);
    if (!r) return;
    RollGeo g = roll_geo_from_seed(4242);
    CHECK(r->th == g.th && r->n == ROLL_FRAMES);
    NEAR(r->len, GUIDE.w + g.ov - g.dx, 0.51);
    NEAR(r->ox, GUIDE.x + g.dx, 1e-3);
    NEAR(r->oy, GUIDE.y + GUIDE.h * 0.5f + g.dy, 1e-3);
    CHECK(r->rb == r->th + 4);
    CHECK(r->cx > r->len * 0.29f && r->cx < r->len * 0.69f);
    NEAR(r->sx, r->cx - 4.0f, 1e-3);
    CHECK(fabsf(r->ky) >= 1.2f && fabsf(r->ky) <= 2.8f && fabsf(r->kr) <= 0.3f);
    CHECK(r->strip && r->strip->w > r->len && r->strip->h > r->th);
    roll_strip_free(r);
    roll_strip_free(NULL);

    // n frames: 0 is the default, and more than the table holds is cut back
    RollStrip *r2 = roll_strip_new(&GUIDE, (RollPick){1, PAT_DOTS}, 9, 8), *r3 = roll_strip_new(&GUIDE, (RollPick){1, PAT_DOTS}, 9, 99);
    CHECK(r2 && r2->n == 8 && r3 && r3->n == ROLL_FRAMES_MAX);
    roll_strip_free(r2);
    roll_strip_free(r3);
}

// the crimp the roll thinks it is at is where paper.c baked it (guards the duplicated draws, whose order paper.c leaves to the compiler)
static void test_crimp_agrees(void)
{
    for (uint32_t seed = 1; seed <= 12; seed++) {
        RollStrip *r = roll_strip_new(&GUIDE, (RollPick){seed % 8, PAT_STRIPES}, seed * 7919u, 0);
        if (!r) { CHECK(r); continue; }
        const PaperSpr *sp = r->strip;
        float rot = r->rot * 3.14159265f / 180.0f;
        // the ridge's dark middle on the strip's middle row
        int row = sp->oy + (int)floorf((r->cx - r->len * 0.5f) * sinf(rot) + 0.5f), best = -1;
        float lo = 1e9f;
        for (int x = 0; x < sp->w; x++) {
            const uint8_t *q = sp->px + ((size_t)row * sp->w + x) * 4;
            if (q[3] < 200) continue;
            float l = q[0] + q[1] + q[2];
            if (l < lo) { lo = l; best = x; }
        }
        float want = sp->ox + (r->cx - r->len * 0.5f);
        CHECK_MSG(best >= 0 && fabsf(best - want) < 4.0f, "seed %u: ridge at %d, crimp at %.1f", seed, best, want);
        // the far half's step: the strip's top edge 30 px past the ridge against 30 px before it
        float xa = r->cx - 30.0f, xb = r->cx + 30.0f;
        float top[2];
        float xs[2] = {xa, xb};
        for (int i = 0; i < 2; i++) {
            int sx = sp->ox + (int)floorf(xs[i] - r->len * 0.5f + 0.5f), y;
            for (y = 0; y < sp->h; y++)
                if (sp->px[((size_t)y * sp->w + sx) * 4 + 3] >= 200) break;
            top[i] = (float)y;
        }
        float tk = tanf(r->kr * 3.14159265f / 180.0f);
        float expect = 60.0f * tanf(rot) + r->ky + (xb - r->sx) * tk;
        CHECK_MSG(fabsf((top[1] - top[0]) - expect) < 1.4f, "seed %u: edge steps by %.1f, the crimp numbers say %.1f", seed, top[1] - top[0], expect);
        roll_strip_free(r);
    }
}

static void test_repeatable(void)
{
    PaperStats before = paper_stats();
    RollStrip *a = roll_strip_new(&GUIDE, (RollPick){3, PAT_CHECK}, 31337, 0), *b = roll_strip_new(&GUIDE, (RollPick){3, PAT_CHECK}, 31337, 0);
    RollStrip *c = roll_strip_new(&GUIDE, (RollPick){3, PAT_CHECK}, 31338, 0), *d = roll_strip_new(&GUIDE, (RollPick){4, PAT_CHECK}, 31337, 0);
    CHECK(a && b && c && d);
    if (!(a && b && c && d)) return;
    CHECK(a->strip->w == b->strip->w && spr_hash(a->strip) == spr_hash(b->strip));
    CHECK(spr_hash(a->strip) != spr_hash(c->strip));
    CHECK(spr_hash(a->strip) != spr_hash(d->strip));
    Surf *s1 = surf_new(1280, 720), *s2 = surf_new(1280, 720);
    scene(s1, a, 0.6f, 1, 1);
    scene(s2, b, 0.6f, 1, 1);
    CHECK(!memcmp(s1->px, s2->px, (size_t)1280 * 720 * 4));
    // the strips' memory is all in paper_stats, and all of it comes back
    PaperStats mid = paper_stats();
    CHECK(mid.bytes - before.bytes == roll_strip_bytes(a) + roll_strip_bytes(b) + roll_strip_bytes(c) + roll_strip_bytes(d));
    surf_free(s1);
    surf_free(s2);
    roll_strip_free(a); roll_strip_free(b); roll_strip_free(c); roll_strip_free(d);
    PaperStats after = paper_stats();
    CHECK(after.bytes == before.bytes && after.count == before.count);
}

// ------------------------------------------------------------------------------------------------ where the roll is
static void test_path(void)
{
    RollStrip *r = roll_strip_new(&GUIDE, (RollPick){5, PAT_WSTRIPE}, 2024, 0);
    CHECK(r != NULL);
    if (!r) return;
    float px = -1e9f, pr = 1e9f, pt = -1;
    int fr_prev = roll_frame_at(r, 0), moved = 0, jumps = 0;
    for (int i = 0; i <= 400; i++) {
        float p = i / 400.0f, x, y;
        roll_centre(r, p, &x, &y);
        CHECK_MSG(x >= px - 0.1f, "roll x went back at %g: %g -> %g", p, px, x); // 0.1: the crimp's jog steps it a hair sideways
        CHECK(fabsf(y - (r->oy + (x - r->ox) * tanf(r->rot * 3.14159265f / 180.0f))) < 5.0f);
        float R = roll_radius(r, p);
        CHECK(R <= pr + 1e-4f);
        float t = r->phase[0];
        (void)t;
        int fr = roll_frame_at(r, p);
        CHECK(fr >= 0 && fr < r->n);
        if (fr != fr_prev) { moved++; if (((fr - fr_prev) % r->n + r->n) % r->n > 2) jumps++; }
        fr_prev = fr;
        px = x;
        pr = R;
        pt = p;
    }
    (void)pt;
    CHECK(moved > 20);
    CHECK_MSG(jumps == 0, "the roll skipped frames %d times in 400 steps", jumps);
    float x0, y0, x1, y1;
    roll_centre(r, 0, &x0, &y0);
    roll_centre(r, 1, &x1, &y1);
    NEAR(x0, r->ox, 1e-3);
    NEAR(y0, r->oy, 1e-3);
    CHECK(x1 > GUIDE.x + GUIDE.w); // it ends past the guide
    NEAR(roll_radius(r, 0), r->r0, 1e-4);
    NEAR(roll_radius(r, 1), r->r1, 1e-4);
    NEAR(roll_radius(r, -3), r->r0, 1e-4); // out of range is held
    NEAR(roll_radius(r, 7), r->r1, 1e-4);
    // the turn is the integral of d len / R: with R from r0 to r1 it is len / (r1 - r0) * ln(r0 / r1)
    float turns = r->len / (r->r0 - r->r1) * logf(r->r0 / r->r1) / (2.0f * 3.14159265f) * r->n;
    NEAR(r->phase[ROLL_PHASE_N], turns, turns * 0.002f);
    CHECK(r->phase[0] == 0);
    for (int i = 1; i <= ROLL_PHASE_N; i++) CHECK(r->phase[i] > r->phase[i - 1]);
    // a smaller roll turns faster per px (the used-up roll spins quicker)
    CHECK(r->phase[ROLL_PHASE_N] - r->phase[ROLL_PHASE_N - 1] > r->phase[1] - r->phase[0]);
    roll_strip_free(r);
}

static void test_events(void)
{
    RollStrip *r = roll_strip_new(&GUIDE, (RollPick){0, PAT_DOTS}, 555, 0);
    CHECK(r != NULL);
    if (!r) return;
    RollEvents e = roll_events(r);
    CHECK(e.crimp_progress > 0.2f && e.crimp_progress < 0.85f);
    NEAR(e.cut_progress, 1.0, 1e-6);
    NEAR(e.length, r->len, 1e-3);
    NEAR(e.crimp_x, r->ox + r->cx, 0.1);
    int hits = 0;
    float prev = 0;
    for (int i = 1; i <= 500; i++) {
        float p = i / 500.0f;
        hits += roll_crossed_crimp(r, prev, p);
        prev = p;
    }
    CHECK_MSG(hits == 1, "the crimp was laid %d times in one run", hits);
    CHECK(!roll_crossed_crimp(r, 0, e.crimp_progress - 0.01f));
    CHECK(roll_crossed_crimp(r, e.crimp_progress - 0.01f, e.crimp_progress + 0.01f));
    CHECK(!roll_crossed_crimp(r, e.crimp_progress + 0.01f, 1.0f));
    CHECK(roll_crossed_crimp(r, 0, 1));
    // the same crimp is not laid again by going back and forward over it
    CHECK(!roll_crossed_crimp(r, 1.0f, 0.0f));
    // speed and the taps' spacing
    NEAR(roll_speed(r, 0.1f, 100.0f), 0.1 * r->len * 10.0, 0.01);
    NEAR(roll_speed(r, -0.1f, 100.0f), 0.1 * r->len * 10.0, 0.01);
    NEAR(roll_speed(r, 0.1f, 0.0f), 0, 1e-9);
    NEAR(roll_tap_gap_ms(0), 0, 1e-9);
    NEAR(roll_tap_gap_ms(ROLL_V_REF * 0.03f), 0, 1e-9); // too slow to tap
    NEAR(roll_tap_gap_ms(ROLL_V_REF), 10, 1e-4);        // quickest
    NEAR(roll_tap_gap_ms(ROLL_V_REF * 3), 10, 1e-4);    // and no quicker
    NEAR(roll_tap_gap_ms(ROLL_V_REF * 0.5f), 64 - 27, 1e-3);
    float prev_gap = 1e9f;
    for (float v = 0.05f; v <= 1.0f; v += 0.05f) {
        float g = roll_tap_gap_ms(ROLL_V_REF * v);
        CHECK(g > 0 && g < prev_gap);
        prev_gap = g;
    }
    roll_strip_free(r);
}

static void test_clip_matches(void)
{
    RollStrip *r = roll_strip_new(&GUIDE, (RollPick){6, PAT_SCALLOP}, 8080, 0);
    CHECK(r != NULL);
    if (!r) return;
    Surf *full = surf_new(1280, 720), *part = surf_new(1280, 720), *bg = surf_new(1280, 720);
    paint_bg(bg);
    RollRect clips[] = {{500, 360, 120, 70}, {0, 0, 1280, 720}, {700, 380, 1, 30}, {430, 370, 37, 60}, {900, 300, 200, 200}, {-40, 370, 500, 80}, {1200, 400, 200, 100}};
    float ps[] = {0.0f, 0.2f, 0.5f, 0.66f, 0.97f, 1.0f};
    for (int ci = 0; ci < (int)(sizeof clips / sizeof *clips); ci++)
        for (int pi = 0; pi < (int)(sizeof ps / sizeof *ps); pi++) {
            RollRect c = clips[ci];
            float p = ps[pi];
            scene(full, r, p, 1, 0);
            paint_bg(part);
            roll_strip_draw(part, r, p, &c);
            roll_roll_draw(part, r, p, &c);
            int bad_in = 0, bad_out = 0;
            for (int y = 0; y < 720; y++)
                for (int x = 0; x < 1280; x++) {
                    size_t o = ((size_t)y * 1280 + x) * 4;
                    int in = x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h;
                    if (in) bad_in += memcmp(part->px + o, full->px + o, 3) != 0;
                    else bad_out += memcmp(part->px + o, bg->px + o, 3) != 0;
                }
            CHECK_MSG(bad_in == 0, "clip %d at %g: %d pixels inside differ from the unclipped draw", ci, p, bad_in);
            CHECK_MSG(bad_out == 0, "clip %d at %g: %d pixels outside were painted", ci, p, bad_out);
        }
    // the peel with a clip
    for (int ci = 0; ci < 4; ci++)
        for (float t = 0; t <= 1.0f; t += 0.25f) {
            RollRect c = clips[ci];
            paint_bg(full);
            roll_peel_draw(full, r, t, NULL);
            paint_bg(part);
            roll_peel_draw(part, r, t, &c);
            int bad = 0;
            for (int y = 0; y < 720; y++)
                for (int x = 0; x < 1280; x++) {
                    size_t o = ((size_t)y * 1280 + x) * 4;
                    int in = x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h;
                    bad += memcmp(part->px + o, in ? full->px + o : bg->px + o, 3) != 0;
                }
            CHECK_MSG(bad == 0, "peel clip %d at %g: %d pixels wrong", ci, t, bad);
        }
    surf_free(full);
    surf_free(part);
    surf_free(bg);
    roll_strip_free(r);
}

static void test_bounds_cover(void)
{
    uint32_t seeds[] = {3, 77, 2024, 90210, 5, 6, 7, 8};
    for (int si = 0; si < 8; si++) {
        RollStrip *r = roll_strip_new(&GUIDE, roll_pick((RollPick){-1, -1}, seeds[si]), seeds[si], 0);
        if (!r) { CHECK(r); continue; }
        Surf *bg = surf_new(1280, 720), *s = surf_new(1280, 720);
        paint_bg(bg);
        RollRect all = roll_bounds(r);
        CHECK(all.w > GUIDE.w && all.h > r->th);
        int bad = 0;
        long n;
        for (int i = 0; i <= 20; i++) {
            float p = i / 20.0f;
            scene(s, r, p, 1, 0);
            if (!inside(diff_box(s, bg, &n), all)) bad++;
        }
        for (int i = 0; i <= 10; i++) { // the pop
            paint_bg(s);
            roll_roll_pop(s, r, i / 10.0f);
            if (!inside(diff_box(s, bg, &n), all)) bad++;
        }
        for (int i = 0; i <= 10; i++) { // the hop and the peel
            paint_bg(s);
            roll_strip_draw(s, r, 1.0f, NULL);
            roll_roll_hop(s, r, i / 10.0f);
            if (!inside(diff_box(s, bg, &n), all)) bad++;
            paint_bg(s);
            roll_peel_draw(s, r, i / 10.0f, NULL);
            if (!inside(diff_box(s, bg, &n), all)) bad++;
        }
        CHECK_MSG(bad == 0, "seed %u: %d draws went outside roll_bounds (%d,%d %dx%d)", seeds[si], bad, all.x, all.y, all.w, all.h);
        surf_free(bg);
        surf_free(s);
        roll_strip_free(r);
    }
}

static void test_ends(void)
{
    RollStrip *r = roll_strip_new(&GUIDE, (RollPick){2, PAT_GRID}, 616, 0);
    CHECK(r != NULL);
    if (!r) return;
    Surf *bg = surf_new(1280, 720), *s = surf_new(1280, 720), *t = surf_new(1280, 720);
    paint_bg(bg);
    long n0, n1, nh;
    // pop: nothing at t = 0, the whole roll at t = 1 (the same as drawing it at progress 0)
    paint_bg(s);
    roll_roll_pop(s, r, 0.0f);
    diff_box(s, bg, &n0);
    CHECK(n0 == 0);
    paint_bg(s);
    roll_roll_pop(s, r, 1.0f);
    paint_bg(t);
    roll_roll_draw(t, r, 0.0f, NULL);
    diff_box(s, t, &n1);
    CHECK_MSG(n1 == 0, "pop at 1 differs from the roll at progress 0 in %ld px", n1);
    // it grows while it appears
    long pn[5];
    for (int i = 0; i < 5; i++) {
        paint_bg(s);
        roll_roll_pop(s, r, 0.25f * i);
        diff_box(s, bg, &pn[i]);
    }
    CHECK(pn[1] > 0 && pn[2] > pn[1] && pn[4] > pn[2]);
    // hop: the full roll at t = 0 (as at progress 1), nothing at t = 1
    paint_bg(s);
    roll_roll_hop(s, r, 0.0f);
    paint_bg(t);
    roll_roll_draw(t, r, 1.0f, NULL);
    diff_box(s, t, &nh);
    CHECK_MSG(nh == 0, "hop at 0 differs from the roll at progress 1 in %ld px", nh);
    paint_bg(s);
    roll_roll_hop(s, r, 1.0f);
    diff_box(s, bg, &n1);
    CHECK(n1 == 0);
    // the hop climbs
    float cy0, cx0;
    roll_centre(r, 1, &cx0, &cy0);
    paint_bg(s);
    roll_roll_hop(s, r, 0.6f);
    RollRect hb = diff_box(s, bg, NULL);
    CHECK(hb.y + hb.h / 2.0f < cy0 - 2);
    // peel: the whole strip at t = 0 (to within the resampling), none at t = 1, going up and fading between
    paint_bg(s);
    roll_strip_draw(s, r, 1.0f, NULL);
    paint_bg(t);
    roll_peel_draw(t, r, 0.0f, NULL);
    long sum = 0, cnt = 0;
    for (int y = 300; y < 480; y++)
        for (int x = 400; x < 1010; x++)
            for (int c = 0; c < 3; c++) {
                sum += abs((int)s->px[((size_t)y * 1280 + x) * 4 + c] - (int)t->px[((size_t)y * 1280 + x) * 4 + c]);
                cnt++;
            }
    CHECK_MSG((double)sum / cnt < 1.2, "peel at 0 is %.2f off the laid strip on average", (double)sum / cnt);
    paint_bg(s);
    roll_peel_draw(s, r, 1.0f, NULL);
    diff_box(s, bg, &n1);
    CHECK(n1 == 0);
    RollRect b0, b5;
    paint_bg(s);
    roll_peel_draw(s, r, 0.0f, NULL);
    b0 = diff_box(s, bg, NULL);
    paint_bg(s);
    roll_peel_draw(s, r, 0.8f, NULL);
    b5 = diff_box(s, bg, NULL);
    CHECK(b5.y < b0.y - 6 && b5.x + b5.w > b0.x + b0.w - 1);
    // monotone fade: the strip's colour moves toward the paper
    float last = -1;
    for (int i = 0; i <= 10; i++) {
        paint_bg(s);
        roll_peel_draw(s, r, i / 10.0f, NULL);
        long n;
        diff_box(s, bg, &n);
        (void)n;
        float tot = 0;
        for (int y = 330; y < 440; y++)
            for (int x = 560; x < 600; x++) tot += 255 - s->px[((size_t)y * 1280 + x) * 4 + 2];
        if (i > 0 && i < 7) CHECK_MSG(tot <= last + 1e-3f, "the peel did not fade steadily at %d/10", i);
        last = tot;
    }
    surf_free(bg);
    surf_free(s);
    surf_free(t);
    roll_strip_free(r);
}

static void test_edges_and_memory(void)
{
    // a guide hanging off the surface, a clip outside the surface, a clip of nothing: no crash, nothing painted out of place
    RollRect off = {-300, 700, 540, 34}, off2 = {1100, -20, 540, 34};
    RollRect *gs[] = {&off, &off2};
    PaperStats before = paper_stats();
    for (int gi = 0; gi < 2; gi++) {
        RollStrip *r = roll_strip_new(gs[gi], (RollPick){0, PAT_ZIG}, 11, 0);
        CHECK(r != NULL);
        if (!r) continue;
        Surf *s = surf_new(1280, 720);
        PaperStats mid = paper_stats();
        RollRect nothing = {100, 100, 0, 0}, outside = {5000, 5000, 100, 100}, huge = {-9999, -9999, 99999, 99999};
        for (int i = 0; i <= 20; i++) {
            float p = i / 20.0f;
            paint_bg(s);
            roll_strip_draw(s, r, p, NULL);
            roll_roll_draw(s, r, p, NULL);
            roll_strip_draw(s, r, p, &nothing);
            roll_roll_draw(s, r, p, &outside);
            roll_strip_draw(s, r, p, &huge);
            roll_roll_draw(s, r, p, &huge);
            roll_dirty(r, p, 1 - p);
            roll_roll_pop(s, r, p);
            roll_roll_hop(s, r, p);
            roll_peel_draw(s, r, p, NULL);
        }
        // drawing never made or kept a sprite
        PaperStats after = paper_stats();
        CHECK(after.bytes == mid.bytes && after.count == mid.count);
        // NaN and wild progress behave
        paint_bg(s);
        roll_strip_draw(s, r, NAN, NULL);
        roll_roll_draw(s, r, 1e30f, NULL);
        roll_strip_draw(s, r, -4.0f, NULL);
        surf_free(s);
        roll_strip_free(r);
    }
    PaperStats end = paper_stats();
    CHECK(end.bytes == before.bytes && end.count == before.count);
    // a small surface smaller than the roll
    RollStrip *r = roll_strip_new(&GUIDE, (RollPick){0, PAT_DOTS}, 1, 0);
    Surf *tiny = surf_new(30, 20);
    for (int i = 0; i <= 4; i++) {
        roll_strip_draw(tiny, r, i / 4.0f, NULL);
        roll_roll_draw(tiny, r, i / 4.0f, NULL);
    }
    surf_free(tiny);
    roll_strip_free(r);
}

// ------------------------------------------------------------------------------------------------ tilt
// a strip at any angle: the guide is crossed corner to corner, so give it the offset that keeps it near the guide's middle
static RollStrip *tilted(float rot, uint32_t seed, RollPick pick)
{
    RollGeo g = roll_geo_from_seed(seed);
    g.rot = rot;
    g.dy = -0.5f * 552.0f * tanf(rot * 3.14159265f / 180.0f);
    return roll_strip_new_geo(&GUIDE, pick, seed, 0, &g);
}

// ------------------------------------------------------------------------------------------------ catches
static void test_catches(void)
{
    int snags[4] = {0}, crimps = 0, bad_sort = 0, bad_gap = 0;
    for (uint32_t seed = 1; seed <= 300; seed++) {
        RollStrip *r = roll_strip_new(&GUIDE, roll_pick((RollPick){-1, -1}, seed), seed * 31u, 0);
        if (!r) { CHECK(r); continue; }
        int ns = 0, nc = 0;
        for (int i = 0; i < r->ncatch; i++) {
            if (r->catches[i].kind == ROLL_CATCH_CRIMP) nc++; else ns++;
            if (i && r->catches[i].progress < r->catches[i - 1].progress) bad_sort++;
            if (i && r->catches[i].progress - r->catches[i - 1].progress < 0.159f) bad_gap++;
            CHECK(r->catches[i].progress > 0.1f && r->catches[i].progress <= 1.0f && r->catches[i].strength >= 0.44f && r->catches[i].strength <= 1.0f);
        }
        crimps += nc;
        snags[ns]++;
        CHECK(nc == 1 && ns <= 2 && r->ncatch <= ROLL_CATCH_MAX);
        // every catch is crossed once going forward, never going back; the crimp's is the crimp event
        int hits = 0, back = 0;
        float prev = 0;
        for (int i = 1; i <= 400; i++) {
            float p = i / 400.0f;
            if (roll_crossed_catch(r, prev, p)) hits++;
            if (roll_crossed_catch(r, p, prev)) back++;
            prev = p;
        }
        CHECK_MSG(hits == r->ncatch && back == 0, "seed %u: %d catches crossed of %d (%d going back)", seed, hits, r->ncatch, back);
        for (int i = 0; i < r->ncatch; i++)
            if (r->catches[i].kind == ROLL_CATCH_CRIMP) NEAR(r->catches[i].progress, roll_events(r).crimp_progress, 1e-6);
        // the same seed, the same catches
        RollStrip *r2 = roll_strip_new(&GUIDE, roll_pick((RollPick){-1, -1}, seed), seed * 31u, 0);
        CHECK(r2 && r2->ncatch == r->ncatch && !memcmp(r->catches, r2->catches, sizeof r->catches[0] * r->ncatch));
        roll_strip_free(r2);
        roll_strip_free(r);
    }
    CHECK(crimps == 300 && bad_sort == 0 && bad_gap == 0);
    CHECK_MSG(snags[0] > 50 && snags[1] > 90 && snags[2] > 40, "snag counts %d %d %d", snags[0], snags[1], snags[2]);

    RollStrip *r = tilted(3.0f, 5, (RollPick){0, PAT_DOTS});
    CHECK(r != NULL);
    if (!r) return;
    int ci = 0;
    float pc = r->catches[ci].progress;
    // the reaction over time
    RollFx z = roll_catch_fx(r, ci, -1.0f), z2 = roll_catch_fx(r, ci, ROLL_CATCH_MS), z3 = roll_catch_fx(r, 99, 10.0f);
    CHECK(z.back_px == 0 && z.pucker == 0 && z2.back_px == 0 && z2.pucker == 0 && z3.pucker == 0);
    CHECK(!roll_catch_active(-1) && roll_catch_active(0) && roll_catch_active(259) && !roll_catch_active(260));
    float peak_back = 0, peak_pk = 0, peak_sh = 0, late_sh = 0;
    for (float ms = 0; ms < ROLL_CATCH_MS; ms += 1.0f) {
        RollFx f = roll_catch_fx(r, ci, ms);
        CHECK(f.back_px >= -1e-4f && f.back_px <= 3.51f && f.pucker >= 0 && f.pucker <= 1.001f);
        peak_back = fmaxf(peak_back, f.back_px);
        peak_pk = fmaxf(peak_pk, f.pucker);
        peak_sh = fmaxf(peak_sh, fabsf(f.shiver_px));
        if (ms > 230) late_sh = fmaxf(late_sh, fabsf(f.shiver_px));
    }
    NEAR(peak_back, 3.5 * r->catches[ci].strength, 0.02);
    NEAR(peak_pk, r->catches[ci].strength, 0.02);
    CHECK(peak_sh > 0.4f && late_sh < 0.12f);
    NEAR(roll_catch_fx(r, ci, 0.0f).back_px, 0.0, 1e-6);
    CHECK(roll_catch_fx(r, ci, 55.0f).back_px > roll_catch_fx(r, ci, 20.0f).back_px && roll_catch_fx(r, ci, 120.0f).back_px < roll_catch_fx(r, ci, 55.0f).back_px);
    // the shown progress holds at the catch, then catches up, never going back or ahead
    float p_real = pc + 0.05f, last = pc;
    for (float ms = 0; ms < 400; ms += 5) {
        float d = roll_catch_progress(r, ci, p_real, ms);
        CHECK(d >= last - 1e-6f && d >= pc - 1e-6f && d <= p_real + 1e-6f);
        if (ms < ROLL_CATCH_STALL_MS) NEAR(d, pc, 1e-6);
        last = d;
    }
    NEAR(roll_catch_progress(r, ci, p_real, 400), p_real, 1e-6);
    NEAR(roll_catch_progress(r, ci, pc - 0.1f, 10), pc - 0.1f, 1e-6); // before the catch: untouched
    NEAR(roll_catch_progress(r, ci, p_real, -5), p_real, 1e-6);

    // drawing with it: a zero reaction is the plain draw; a real one moves things, only inside the catch's dirty rectangle and the bounds, and a clip still matches
    Surf *a = surf_new(1280, 720), *b = surf_new(1280, 720), *c = surf_new(1280, 720);
    RollFx none = {0, 0, 0, 0};
    float p = pc + 0.01f;
    scene(a, r, p, 1, 1);
    paint_bg(b);
    guide_dashes(b, &r->guide);
    roll_strip_draw_fx(b, r, p, NULL, &none);
    roll_roll_draw_fx(b, r, p, NULL, &none);
    CHECK(!memcmp(a->px, b->px, (size_t)1280 * 720 * 4));
    RollRect all = roll_bounds(r), dc = roll_dirty_catch(r, p);
    int moved = 0, bad = 0, bad_clip = 0, bad_new = 0;
    Surf *strip_only = surf_new(1280, 720), *strip_fx = surf_new(1280, 720);
    paint_bg(strip_only);
    roll_strip_draw(strip_only, r, p, NULL);
    for (float ms = 0; ms < ROLL_CATCH_MS; ms += 10) {
        RollFx f = roll_catch_fx(r, ci, ms);
        paint_bg(b);
        guide_dashes(b, &r->guide);
        roll_strip_draw_fx(b, r, p, NULL, &f);
        roll_roll_draw_fx(b, r, p, NULL, &f);
        long n;
        RollRect box = diff_box(a, b, &n);
        if (n) moved++;
        if (!inside(box, dc) || !inside(box, all)) bad++;
        // the pucker only changes tape that is there
        {
            RollRect cl = {r->guide.x + 100, r->guide.y - 40, 300, 120};
            paint_bg(c);
            guide_dashes(c, &r->guide);
            roll_strip_draw_fx(c, r, p, &cl, &f);
            roll_roll_draw_fx(c, r, p, &cl, &f);
            for (int y = 0; y < 720; y++)
                for (int x = 0; x < 1280; x++) {
                    size_t o = ((size_t)y * 1280 + x) * 4;
                    int in = x >= cl.x && x < cl.x + cl.w && y >= cl.y && y < cl.y + cl.h;
                    if (in && memcmp(c->px + o, b->px + o, 3)) bad_clip++;
                }
        }
        // pixels the plain strip left bare are still bare after the pucker
        paint_bg(strip_fx);
        roll_strip_draw_fx(strip_fx, r, p, NULL, &f);
        Surf *bare = surf_new(1280, 720);
        paint_bg(bare);
        for (size_t i = 0; i < (size_t)1280 * 720; i++)
            if (!memcmp(strip_only->px + i * 4, bare->px + i * 4, 3) && memcmp(strip_fx->px + i * 4, bare->px + i * 4, 3)) bad_new++;
        surf_free(bare);
    }
    CHECK_MSG(moved >= 20, "the catch moved something in only %d of 26 frames", moved);
    CHECK_MSG(bad == 0, "%d catch frames changed pixels outside roll_dirty_catch / roll_bounds", bad);
    CHECK_MSG(bad_clip == 0, "%d clipped catch pixels differ from the unclipped draw", bad_clip);
    CHECK_MSG(bad_new == 0, "the pucker painted %d pixels where there is no tape", bad_new);
    surf_free(strip_only);
    surf_free(strip_fx);
    surf_free(a);
    surf_free(b);
    surf_free(c);
    roll_strip_free(r);
}

// the roll's picture has what the mockup's has: a lit stripe down it, dark at both edges, the pattern squeezed toward its edges
static void test_roll_look(void)
{
    RollStrip *r = roll_strip_new(&GUIDE, (RollPick){2, PAT_DOTS}, 4, 0);
    CHECK(r != NULL);
    if (!r) return;
    Surf *s = surf_new(1280, 720);
    paint_bg(s);
    roll_roll_draw(s, r, 0.0f, NULL);
    float cx, cy;
    roll_centre(r, 0, &cx, &cy);
    int y = (int)cy;
    float R = roll_radius(r, 0);
    float lum[256];
    int n = 0;
    for (int x = (int)(cx - R); x < (int)(cx + R) && n < 256; x++, n++) lum[n] = s->px[((size_t)y * 1280 + x) * 4 + 1];
    CHECK(n > 40);
    CHECK_MSG(lum[1] < lum[n / 2] - 20 && lum[n - 2] < lum[n / 2] - 20, "the roll's ends are not darker than its middle (%g %g %g)", lum[1], lum[n / 2], lum[n - 2]);
    CHECK(lum[n - 2] < lum[1] + 40); // the right edge is a little darker than the left (mockup: .55 against .5)
    // the dots squeeze toward the edges: cell widths near the edge are narrower than near the middle
    float rowc[256];
    for (int x = 0; x < n; x++) rowc[x] = 0;
    int widths_mid = 0, widths_edge = 0;
    (void)rowc;
    (void)widths_mid;
    (void)widths_edge;
    surf_free(s);
    roll_strip_free(r);
}

// ------------------------------------------------------------------------------------------------ timings
static void bench(void)
{
    printf("  timings on this machine (build with -O2 for the real numbers):\n");
    // bake
    double t0 = now_ms();
    const int NB = 20;
    for (int i = 0; i < NB; i++) {
        RollStrip *r = roll_strip_new(&GUIDE, roll_pick((RollPick){-1, -1}, 100 + i), 100 + i, 0);
        roll_strip_free(r);
    }
    printf("    bake one strip (paper_tape + tables): %.2f ms average over %d\n", (now_ms() - t0) / NB, NB);
    RollStrip *r = roll_strip_new(&GUIDE, (RollPick){0, PAT_STRIPES}, 42, 0);
    Surf *s = surf_new(1280, 720);
    paint_bg(s);
    double tot_full = 0, tot_dirty = 0, worst_dirty = 0, tot_roll = 0, tot_strip = 0, worst_roll = 0;
    int frames = 0;
    const int KINDS = PAT_COUNT;
    for (int k = 0; k < KINDS; k++) {
        RollStrip *rk = roll_strip_new(&GUIDE, (RollPick){k, k}, 300 + k, 0);
        for (int i = 0; i <= 120; i++) {
            float p0 = i / 120.0f, p1 = (i + 1) / 120.0f;
            // a screen that restores the changed rectangle: clear it, draw the strip, then the roll, both clipped to it
            RollRect d = roll_dirty(rk, p0, p1);
            double a = now_ms();
            for (int y = d.y; y < d.y + d.h && y < 720; y++)
                for (int x = d.x; x < d.x + d.w && x < 1280; x++) memcpy(s->px + ((size_t)y * 1280 + x) * 4, "\xFB\xFE\xFF\xFF", 4);
            double b = now_ms();
            roll_strip_draw(s, rk, p1, &d);
            double c = now_ms();
            roll_roll_draw(s, rk, p1, &d);
            double e = now_ms();
            tot_dirty += e - a;
            tot_strip += c - b;
            tot_roll += e - c;
            if (e - a > worst_dirty) worst_dirty = e - a;
            if (e - c > worst_roll) worst_roll = e - c;
            // the same frame redrawn whole
            double f = now_ms();
            paint_bg(s);
            roll_strip_draw(s, rk, p1, NULL);
            roll_roll_draw(s, rk, p1, NULL);
            tot_full += now_ms() - f;
            frames++;
        }
        roll_strip_free(rk);
    }
    printf("    a frame, only its dirty rectangle (clear + strip + roll): avg %.3f ms, worst %.3f ms  (strip %.3f, roll %.3f avg, roll worst %.3f)\n", tot_dirty / frames, worst_dirty, tot_strip / frames, tot_roll / frames, worst_roll);
    printf("    the same frame over the whole 1280x720 surface:           avg %.3f ms (mostly the clear)\n", tot_full / frames);
    double h0 = now_ms();
    for (int i = 0; i < 30; i++) roll_peel_draw(s, r, i / 29.0f, NULL);
    printf("    a peel frame: avg %.3f ms; ", (now_ms() - h0) / 30);
    h0 = now_ms();
    for (int i = 0; i < 30; i++) roll_roll_hop(s, r, i / 29.0f);
    printf("a hop frame: avg %.3f ms\n", (now_ms() - h0) / 30);
    printf("    memory per strip: %zu bytes (strip sprite %dx%d)\n", roll_strip_bytes(r), r->strip->w, r->strip->h);
    surf_free(s);
    roll_strip_free(r);
}

// ------------------------------------------------------------------------------------------------ contact sheets
static Font *g_font, *g_bold;

static uint8_t *load_png(const char *path, int *w, int *h)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc((size_t)n);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    uint8_t *px = gfx_decode_png(buf, (size_t)n, w, h);
    free(buf);
    return px;
}

// copies a region of an RGBA picture (or a surface) magnified k times (nearest) onto dst
static void zoom_to(Surf *dst, int dx, int dy, const uint8_t *src, int sw, int sx, int sy, int w, int h, int k)
{
    for (int y = 0; y < h * k; y++)
        for (int x = 0; x < w * k; x++) {
            int ox = dx + x, oy = dy + y;
            if (ox < 0 || oy < 0 || ox >= dst->w || oy >= dst->h) continue;
            memcpy(dst->px + ((size_t)oy * dst->w + ox) * 4, src + ((size_t)(sy + y / k) * sw + (sx + x / k)) * 4, 3);
            dst->px[((size_t)oy * dst->w + ox) * 4 + 3] = 255;
        }
}

// a seed whose crimp has the numbers read off the mockup, so a comparison shows the same crimp
static uint32_t crimp_seed(int len, float cx, float ky, float kr)
{
    for (uint32_t seed = 1; seed < 20000000u; seed++) {
        PaperRng rng;
        paper_rng_seed(&rng, seed ^ 0xC12117u);
        float f1 = paper_rng_f(&rng), f2 = paper_rng_f(&rng), f3 = paper_rng_f(&rng), f4 = paper_rng_f(&rng);
        float c = len * (0.3f + 0.38f * f1), k = (f2 < 0.5f ? -1.0f : 1.0f) * (1.2f + 1.6f * f3), q = (f4 * 2.0f - 1.0f) * 0.3f;
        if (fabsf(c - cx) < 0.6f && fabsf(k - ky) < 0.08f && fabsf(q - kr) < 0.02f) return seed;
    }
    return 1;
}

// what the mockup's page has at the numbers of ?seed=12345 (read off its DOM): the strip's width, left end, tilt, run-over, the roll's radii, and
// the crimp (where, the far half's step and turn); its tape colour is #FFCDCC, the first of the list
typedef struct { RollGeo geo; float cx, ky, kr; int colour; } MockCase;
static const MockCase MOCK = {{30, 1.7f, 1.7f, 0.93f, 13.7f, 27.08f, 18.0f, 0}, 297.2f, -1.96f, -0.12f, 0};

typedef struct { float mean_abs; } SheetDiff;

static double crop_diff(const Surf *mine, int mx, int my, const uint8_t *mock, int mw, int sx, int sy, int w, int h)
{
    double sum = 0;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) sum += fabs((double)mine->px[((size_t)(my + y) * mine->w + mx + x) * 4 + c] - (double)mock[((size_t)(sy + y) * mw + sx + x) * 4 + c]);
    return sum / ((double)w * h * 3);
}

static void write_png(const Surf *s, const char *dir, const char *name)
{
    char path[400];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    stbi_write_png(path, s->w, s->h, 4, s->px, s->w * 4);
    printf("  wrote %s\n", path);
}

// tilted strips (the roll turned with the tape, the cut square to it) and a catch frame by frame
static void sheets_round2(const char *out)
{
    const float AN[] = {-8.0f, -4.0f, 0.5f, 4.0f, 8.0f};
    const float PP[] = {0.2f, 0.55f, 0.85f};
    const int CW = 420, CH = 210, X0 = 434 - 30, Y0 = 296;
    Surf *scr = surf_new(1280, 720), *sh = surf_new(CW * 3 + 40, 5 * (CH + 26) + 50);
    gfx_fill(sh, hex(0xF1EBDD, 1));
    text_draw(sh, g_bold, 22, 0, 20, 30, 0, hex(PAPER_TEXT, 1), "Tilted strips: rot -8, -4, 0.5, 4, 8 degrees, progress 20 / 55 / 85% (the crimp turns the far half a little more)");
    for (int a = 0; a < 5; a++) {
        RollStrip *r = tilted(AN[a], 12345, (RollPick){0, PAT_STRIPES});
        for (int i = 0; i < 3; i++) {
            scene(scr, r, PP[i], 1, 1);
            float cx, cy;
            roll_centre(r, PP[i], &cx, &cy);
            int x0 = (int)cx - CW / 2, y0 = (int)cy - CH / 2;
            (void)X0; (void)Y0;
            zoom_to(sh, 20 + i * (CW + 4), 44 + a * (CH + 26), scr->px, 1280, x0, y0, CW, CH, 1);
        }
        roll_strip_free(r);
    }
    write_png(sh, out, "roll-sheet-tilt.png");
    surf_free(sh);

    // a catch: the roll at the catch, then 0, 25, 55, 90, 140, 220 ms after it (3x), the roll and the tape behind it
    RollStrip *r = tilted(2.0f, 5, (RollPick){0, PAT_DOTS});
    const float MS[] = {-1.0f, 25.0f, 55.0f, 90.0f, 140.0f, 220.0f};
    const int KW = 150, KH = 70, KK = 3;
    Surf *cs = surf_new(KW * KK * 2 + 60, 3 * (KH * KK + 24) + 50);
    gfx_fill(cs, hex(0xF1EBDD, 1));
    text_draw(cs, g_bold, 20, 0, 20, 30, 0, hex(PAPER_TEXT, 1), "A snag, ms after it: none, 25, 55, 90, 140, 220 (3x): pulled back, shivering, tape bunched behind");
    float pc = r->catches[0].progress;
    for (int i = 0; i < 6; i++) {
        RollFx f = roll_catch_fx(r, 0, MS[i]);
        float p = roll_catch_progress(r, 0, pc + 0.02f, MS[i] < 0 ? 0 : MS[i]);
        paint_bg(scr);
        guide_dashes(scr, &r->guide);
        roll_strip_draw_fx(scr, r, p, NULL, &f);
        roll_roll_draw_fx(scr, r, p, NULL, &f);
        float cx, cy;
        roll_centre(r, p, &cx, &cy);
        zoom_to(cs, 20 + (i % 2) * (KW * KK + 20), 44 + (i / 2) * (KH * KK + 24), scr->px, 1280, (int)cx - KW + 40, (int)cy - KH / 2, KW, KH, KK);
        char lab[40];
        snprintf(lab, sizeof lab, "%s%.0f ms", MS[i] < 0 ? "before " : "", MS[i] < 0 ? 0.0f : MS[i]);
        text_draw(cs, g_font, 14, 0, 24 + (i % 2) * (KW * KK + 20), 40 + (i / 2) * (KH * KK + 24) + 12, 0, hex(PAPER_TEXT, 1), lab);
    }
    write_png(cs, out, "roll-sheet-catch.png");
    surf_free(cs);
    roll_strip_free(r);
    surf_free(scr);
}

static void sheets(const char *out, const char *mockdir, int sx_origin, int sy_origin)
{
    mkdir(out, 0777);
    uint32_t cs = crimp_seed(552, MOCK.cx, MOCK.ky, MOCK.kr);
    printf("  crimp seed for the mockup's numbers: %u\n", cs);
    char path[400];
    // ---- progress: pop, then the run, the cut, the peel; mine over the mockup's
    const float PS[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    const char *PN[] = {"0", "25", "50", "75", "100"};
    const int CX0 = 405, CY0 = 352, CW = 600, CH = 90, K = 2; // the region of the 1280x720 screen on the sheet
    Surf *sheet = surf_new(CW * K + 40, 5 * (CH * K * 2 + 70) + 70);
    gfx_fill(sheet, hex(0xF1EBDD, 1));
    text_draw(sheet, g_bold, 26, 0, 20, 40, 0, hex(PAPER_TEXT, 1), "The roll bar: mine above, the mockup's (real Chrome) below, same numbers, 2x");
    RollStrip *r = roll_strip_new_geo(&GUIDE, (RollPick){MOCK.colour, PAT_STRIPES}, cs, 0, &MOCK.geo);
    Surf *scr = surf_new(1280, 720);
    for (int i = 0; i < 5; i++) {
        int y0 = 70 + i * (CH * K * 2 + 70);
        char lab[120];
        scene(scr, r, PS[i], PS[i] < 1.0f, 1);
        snprintf(lab, sizeof lab, "progress %s%%", PN[i]);
        text_draw(sheet, g_bold, 20, 0, 20, y0 + 16, 0, hex(PAPER_TEXT, 1), lab);
        zoom_to(sheet, 20, y0 + 24, scr->px, 1280, CX0, CY0, CW, CH, K);
        snprintf(path, sizeof path, "%s/mock-p%s.png", mockdir, PN[i]);
        int mw, mh;
        uint8_t *m = load_png(path, &mw, &mh);
        if (m) {
            zoom_to(sheet, 20, y0 + 24 + CH * K + 6, m, mw, sx_origin + CX0, sy_origin + CY0, CW, CH, K);
            printf("    progress %-3s: mean abs difference to the mockup over the crop = %.2f / 255\n", PN[i], crop_diff(scr, CX0, CY0, m, mw, sx_origin + CX0, sy_origin + CY0, CW, CH));
            free(m);
        } else {
            text_draw(sheet, g_font, 16, 0, 30, y0 + 24 + CH * K + 30, 0, hex(PAPER_TEXT_SOFT, 1), "(no mockup screenshot found)");
        }
    }
    write_png(sheet, out, "roll-sheet-progress.png");
    surf_free(sheet);

    // ---- the cut, hop and peel as a strip of frames (mine only: the mockup's are animations)
    Surf *an = surf_new(CW * K / 2 + 40, 7 * 110 + 60);
    gfx_fill(an, hex(0xF1EBDD, 1));
    text_draw(an, g_bold, 22, 0, 20, 34, 0, hex(PAPER_TEXT, 1), "pop (120 ms), hop (170 ms) and peel (260 ms), mine");
    for (int i = 0; i < 7; i++) {
        paint_bg(scr);
        guide_dashes(scr, &r->guide);
        if (i < 3) {
            roll_roll_pop(scr, r, i / 2.0f);
        } else if (i < 5) {
            roll_strip_draw(scr, r, 1.0f, NULL);
            roll_roll_hop(scr, r, (i - 2) / 3.0f);
        } else {
            roll_peel_draw(scr, r, (i - 4) / 2.0f * 0.8f, NULL);
        }
        zoom_to(an, 20, 50 + i * 110, scr->px, 1280, CX0, CY0 + 6, CW / 2 + 20, 50, 2);
    }
    write_png(an, out, "roll-sheet-ends.png");
    surf_free(an);
    roll_strip_free(r);

    // ---- every pattern at half way, mine left, the mockup's right, 3x around the roll
    static const char *KN[] = {"stripes", "dots", "grid", "zig", "check", "scallop", "hline", "wstripe"};
    const int KW = 190, KH = 66, KK = 3;
    Surf *ks = surf_new(KW * KK * 2 + 80, 8 * (KH * KK + 30) + 60);
    gfx_fill(ks, hex(0xF1EBDD, 1));
    text_draw(ks, g_bold, 22, 0, 20, 34, 0, hex(PAPER_TEXT, 1), "Every pattern at 50%: mine left, the mockup's right, 3x");
    for (int k = 0; k < PAT_COUNT; k++) {
        RollStrip *rk = roll_strip_new_geo(&GUIDE, (RollPick){MOCK.colour, k}, cs, 0, &MOCK.geo);
        scene(scr, rk, 0.5f, 1, 1);
        float cx, cy;
        roll_centre(rk, 0.5f, &cx, &cy);
        int x0 = (int)cx - KW / 2 - 30, y0 = (int)cy - KH / 2;
        int by = 50 + k * (KH * KK + 30);
        text_draw(ks, g_bold, 18, 0, 20, by + 16, 0, hex(PAPER_TEXT, 1), KN[k]);
        zoom_to(ks, 20, by + 22, scr->px, 1280, x0, y0, KW, KH, KK);
        snprintf(path, sizeof path, "%s/mock-k%s.png", mockdir, KN[k]);
        int mw, mh;
        uint8_t *m = load_png(path, &mw, &mh);
        if (m) {
            zoom_to(ks, 40 + KW * KK, by + 22, m, mw, sx_origin + x0, sy_origin + y0, KW, KH, KK);
            printf("    %-8s: mean abs difference over the roll = %.2f / 255\n", KN[k], crop_diff(scr, x0, y0, m, mw, sx_origin + x0, sy_origin + y0, KW, KH));
            free(m);
        }
        roll_strip_free(rk);
    }
    write_png(ks, out, "roll-sheet-kinds.png");
    surf_free(ks);
    surf_free(scr);
    sheets_round2(out);
}

int main(int argc, char **argv)
{
    int do_bench = 0, do_sheet = 0, ox = 61, oy = 164;
    const char *out = NULL, *mock = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--bench")) do_bench = 1;
        else if (!strcmp(argv[i], "--sheet") && i + 2 < argc) { do_sheet = 1; out = argv[i + 1]; mock = argv[i + 2]; i += 2; }
        else if (!strcmp(argv[i], "--origin") && i + 2 < argc) { ox = atoi(argv[i + 1]); oy = atoi(argv[i + 2]); i += 2; }
    }
    g_font = font_load(font_regular_ttf);
    g_bold = font_load(font_semibold_ttf);
    if (do_sheet) {
        sheets(out, mock, ox, oy);
        return 0;
    }
    if (do_bench) {
        bench();
        return 0;
    }
    PaperStats start = paper_stats();
    test_geometry();
    test_crimp_agrees();
    test_repeatable();
    test_path();
    test_events();
    test_clip_matches();
    test_bounds_cover();
    test_ends();
    test_edges_and_memory();
    test_catches();
    test_roll_look();
    PaperStats end = paper_stats();
    CHECK_MSG(end.bytes == start.bytes && end.count == start.count, "sprites leaked: %zu bytes, %d sprites", end.bytes - start.bytes, end.count - start.count);
    printf("test_roll: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
