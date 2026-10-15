// Washi's drawing library (source/gfx/paper.c) checked on the PC: the lift and wiggle numbers, interruptible motion, seeded sprites that come
// out the same every time, sizes and memory accounting, contrast of the type on paper and every tape pattern. `./test_paper --sheets` also writes a
// contact sheet of every primitive (out/paper/*.png) to look at: cards, tape, the peel, crayon marks, the ghost, shadows, and a lifted card in place.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_ASSERT(x) ((void)0)
#include "../source/gfx/stb_image_write.h"

#include "../source/gfx/crayon.h"
#include "../source/gfx/paper.h"

static int g_checks, g_failed;
#define CHECK(c) CHECK_MSG(c, "%s", #c)
#define CHECK_MSG(c, ...)                                                 \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(c)) {                                                       \
            g_failed++;                                                   \
            printf("  FAIL [paper] %s:%d: ", __FILE__, __LINE__);         \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)
#define NEAR(a, b, tol) CHECK_MSG(fabs((double)(a) - (double)(b)) <= (tol), "%s = %g, wanted %g (+-%g)", #a, (double)(a), (double)(b), (double)(tol))

static uint32_t fnv(const uint8_t *p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 16777619u;
    return h;
}
static uint32_t spr_hash(const PaperSpr *s) { return fnv(s->px, (size_t)s->w * s->h * 4); }
static int same(const PaperSpr *a, const PaperSpr *b) { return a->w == b->w && a->h == b->h && !memcmp(a->px, b->px, (size_t)a->w * a->h * 4); }

// ------------------------------------------------------------------------------------------------ lift maths
static void test_lift_table(void)
{
    NEAR(paper_lift_k[LIFT_TILE].rise, 9, 1e-6);
    NEAR(paper_lift_k[LIFT_TILE].scale, 0.06, 1e-6);
    NEAR(paper_lift_k[LIFT_ROW].rise, 6, 1e-6);
    NEAR(paper_lift_k[LIFT_ROW].scale, 0.012, 1e-6);
    NEAR(paper_lift_k[LIFT_BTN].rise, 5, 1e-6);
    NEAR(paper_lift_k[LIFT_BTN].scale, 0.04, 1e-6);
    NEAR(paper_lift_k[LIFT_TAB].rise, 0, 1e-6);
    NEAR(paper_lift_k[LIFT_TAB].scale, 0, 1e-6);
    NEAR(paper_lift_k[LIFT_TAB].dx, 12, 1e-6);

    PaperLift l;
    paper_lift_init(&l, LIFT_TILE);
    NEAR(paper_lift_value(&l, 5000), 0, 1e-9);
    paper_lift_grab(&l, 1000, 0);
    NEAR(paper_lift_value(&l, 1000), 0, 1e-6);
    NEAR(paper_lift_value(&l, 1066.5), 0.875, 1e-5); // ease-out cubic: 1 - (1 - .5)^3
    NEAR(paper_lift_value(&l, 1133), 1, 1e-6);
    NEAR(paper_lift_value(&l, 9000), 1, 1e-6);
    paper_lift_release(&l, 2000, 0);
    NEAR(paper_lift_value(&l, 2000), 1, 1e-6);
    NEAR(paper_lift_value(&l, 2050), 0.5, 1e-5); // smoothstep
    NEAR(paper_lift_value(&l, 2100), 0, 1e-6);
    NEAR(paper_lift_value(&l, 2025), 1 - (0.25 * 0.25 * (3 - 0.5)), 1e-5);

    paper_lift_init(&l, LIFT_ROW);
    paper_lift_grab(&l, 0, 1); // a held D-pad: 80 ms up, 60 ms down
    NEAR(paper_lift_value(&l, 79), 1 - pow(1 - 79.0 / 80, 3), 1e-5);
    NEAR(paper_lift_value(&l, 80), 1, 1e-6);
    paper_lift_release(&l, 500, 1);
    NEAR(paper_lift_value(&l, 530), 0.5, 1e-5);
    NEAR(paper_lift_value(&l, 560), 0, 1e-6);

    // the pose of each kind at full lift
    PaperPose po;
    paper_lift_init(&l, LIFT_TILE);
    paper_lift_grab(&l, 0, 0);
    paper_lift_pose(&l, 1000, 150, &po);
    NEAR(po.p, 1, 1e-6);
    NEAR(po.y, -9, 1e-5);
    NEAR(po.scale, 1.06, 1e-5);
    NEAR(po.x, 0, 1e-6);
    NEAR(po.ghost, 1, 1e-6);
    paper_lift_pose(&l, 41.5, 150, &po); // ghost opacity min(1, 1.6 p)
    NEAR(po.ghost, fminf(1, 1.6f * po.p), 1e-5);
    paper_lift_init(&l, LIFT_TAB);
    paper_lift_grab(&l, 0, 0);
    paper_lift_pose(&l, 1000, 200, &po);
    NEAR(po.x, 12, 1e-5);
    NEAR(po.y, 0, 1e-6);
    NEAR(po.scale, 1, 1e-6);
    NEAR(po.angle, 0, 1e-6); // a tab does not wiggle
    paper_lift_init(&l, LIFT_BTN);
    paper_lift_grab(&l, 0, 0);
    paper_lift_pose(&l, 1000, 200, &po);
    NEAR(po.y, -5, 1e-5);
    NEAR(po.scale, 1.04, 1e-5);
}

static void test_interrupt(void)
{
    // grab, let go part way, grab again: the value never jumps, and each new motion starts where the last one was
    PaperLift l;
    paper_lift_init(&l, LIFT_TILE);
    paper_lift_grab(&l, 0, 0);
    double v40 = paper_lift_value(&l, 40);
    NEAR(v40, 1 - pow(1 - 40.0 / 133, 3), 1e-5);
    paper_lift_release(&l, 40, 0);
    NEAR(paper_lift_value(&l, 40), v40, 1e-6);
    NEAR(paper_lift_value(&l, 39.999), v40, 1e-3);
    NEAR(paper_lift_value(&l, 90), v40 * 0.5, 1e-5); // half way through a 100 ms smoothstep down from v40
    double v90 = paper_lift_value(&l, 90);
    paper_lift_grab(&l, 90, 0);
    NEAR(paper_lift_value(&l, 90), v90, 1e-6);
    CHECK(paper_lift_value(&l, 100) > v90);
    NEAR(paper_lift_value(&l, 90 + 133), 1, 1e-6);
    // a release in the middle of the way down starts from there too
    paper_lift_release(&l, 500, 0);
    paper_lift_release(&l, 560, 0); // twice in a row
    double v = paper_lift_value(&l, 560);
    paper_lift_grab(&l, 560, 1);
    NEAR(paper_lift_value(&l, 560), v, 1e-6);
    // a long random walk of grabs and releases: no frame-to-frame step bigger than the fastest motion allows
    paper_lift_init(&l, LIFT_TILE);
    PaperRng r;
    paper_rng_seed(&r, 99);
    double now = 0, prev = 0, worst = 0;
    for (int i = 0; i < 4000; i++) {
        now += 1.0; // 1 ms steps
        if (paper_rng_f(&r) < 0.02) {
            if (paper_rng_f(&r) < 0.5) paper_lift_grab(&l, now, paper_rng_f(&r) < 0.3);
            else paper_lift_release(&l, now, paper_rng_f(&r) < 0.3);
        }
        double v2 = paper_lift_value(&l, now);
        CHECK(v2 >= -1e-6 && v2 <= 1 + 1e-6);
        if (fabs(v2 - prev) > worst) worst = fabs(v2 - prev);
        prev = v2;
    }
    CHECK_MSG(worst < 0.04, "largest 1 ms step was %g", worst); // the steepest part (ease-out start, 80 ms) moves 3/80 per ms
}

static void test_wiggle(void)
{
    NEAR(paper_wiggle_at(0), -3.4, 1e-5);
    NEAR(paper_wiggle_at(42), 3.0, 1e-5);
    NEAR(paper_wiggle_at(85), -1.8, 1e-5);
    NEAR(paper_wiggle_at(128), 1.0, 1e-5);
    NEAR(paper_wiggle_at(170), -0.4, 1e-5);
    NEAR(paper_wiggle_at(21), -0.2, 1e-5); // half way between -3.4 and 3.0
    NEAR(paper_wiggle_at(63.5), 0.6, 1e-5);
    NEAR(paper_wiggle_at(500), -0.4, 1e-5);
    NEAR(paper_wiggle_k(150), 1, 1e-6);
    NEAR(paper_wiggle_k(330), 0.5, 1e-6);

    PaperLift l;
    paper_lift_init(&l, LIFT_TILE);
    NEAR(paper_lift_wiggle(&l, 0, 150), 0, 1e-6); // at rest
    paper_lift_grab(&l, 1000, 0);
    NEAR(paper_lift_wiggle(&l, 1000, 150), -3.4, 1e-5);
    NEAR(paper_lift_wiggle(&l, 1042, 150), 3.0, 1e-5);
    NEAR(paper_lift_wiggle(&l, 1085, 150), -1.8, 1e-5);
    NEAR(paper_lift_wiggle(&l, 1042, 660), 3.0 * 0.25, 1e-5); // a wide row wiggles a quarter as much
    NEAR(paper_lift_wiggle(&l, 1400, 150), -0.4, 1e-5);       // rests at -0.4 while lifted
    paper_lift_release(&l, 1400, 0);
    NEAR(paper_lift_wiggle(&l, 1450, 150), -0.4 * 0.5, 1e-5); // and fades with the lift on the way down
    NEAR(paper_lift_wiggle(&l, 1500, 150), 0, 1e-5);
    // a held repeat has no wiggle and rests level
    paper_lift_init(&l, LIFT_TILE);
    paper_lift_grab(&l, 0, 1);
    NEAR(paper_lift_wiggle(&l, 20, 150), 0, 1e-6);
    NEAR(paper_lift_wiggle(&l, 500, 150), 0, 1e-6);
    // busy: moving, wiggling, bump, the landing squash; then still
    paper_lift_init(&l, LIFT_TILE);
    CHECK(!paper_lift_busy(&l, 0));
    paper_lift_grab(&l, 0, 0);
    CHECK(paper_lift_busy(&l, 100));
    CHECK(paper_lift_busy(&l, 150)); // lift done at 133, wiggle until 170
    CHECK(!paper_lift_busy(&l, 171));
    paper_lift_bump(&l, 300);
    CHECK(paper_lift_busy(&l, 400));
    CHECK(!paper_lift_busy(&l, 431));
    paper_lift_release(&l, 500, 0);
    CHECK(paper_lift_busy(&l, 640)); // landed at 600, squashing until 670
    CHECK(!paper_lift_busy(&l, 671));
    PaperPose po;
    paper_lift_pose(&l, 640, 150, &po);
    NEAR(po.scale, 1 - 0.008, 1e-6);
    paper_lift_bump(&l, 1000);
    paper_lift_pose(&l, 1000 + 130 / 6.0, 150, &po); // sin(pi/2 ...) of the bump: 5 px at its strongest near a sixth
    CHECK(fabs(po.x) > 2.0 && fabs(po.x) <= 5.0);
}

// ------------------------------------------------------------------------------------------------ tokens, contrast, random numbers
static void test_tokens(void)
{
    CHECK(paper_contrast(PAPER_TEXT, PAPER_PAPER) >= 4.5f);
    CHECK(paper_contrast(PAPER_TEXT_SOFT, PAPER_PAPER) >= 4.5f);
    CHECK(paper_contrast(PAPER_INK, PAPER_PAPER) >= 4.5f);
    CHECK(paper_contrast(PAPER_CRAYON_1, PAPER_PAPER) >= 4.5f);
    CHECK(paper_contrast(PAPER_TEXT, PAPER_BOARD) >= 4.5f);
    CHECK(paper_contrast(PAPER_TEXT_SOFT, PAPER_BOARD) >= 4.5f);
    CHECK(paper_contrast(PAPER_INK, PAPER_BOARD) >= 4.5f);
    CHECK(paper_contrast(PAPER_THREAD, PAPER_PAPER) >= 3.0f); // lines and guides, not type
    for (int i = 0; i < PAPER_TAPE_COUNT; i++) CHECK_MSG(paper_contrast(PAPER_TEXT, paper_tape_rgb[i]) >= 4.5f, "text on tape %d", i);
    NEAR(paper_contrast(0x000000, 0xFFFFFF), 21, 1e-3);
    NEAR(paper_contrast(0x777777, 0x777777), 1, 1e-6);
    printf("  contrast on paper: text %.1f, soft text %.1f, ink %.1f, crayon %.1f, thread %.1f\n", paper_contrast(PAPER_TEXT, PAPER_PAPER),
           paper_contrast(PAPER_TEXT_SOFT, PAPER_PAPER), paper_contrast(PAPER_INK, PAPER_PAPER), paper_contrast(PAPER_CRAYON_1, PAPER_PAPER),
           paper_contrast(PAPER_THREAD, PAPER_PAPER));
    // the tape colours are the ones in the plan
    static const uint32_t want[8] = {0xFFCDCC, 0xFFE3CD, 0xFFFF9A, 0xD4FAC7, 0xBFEFE1, 0xAED6F9, 0xE4CCFE, 0xFFCCFA};
    for (int i = 0; i < 8; i++) CHECK(paper_tape_rgb[i] == want[i]);
}

// ------------------------------------------------------------------------------------------------ sprites
static double mean_lum(const PaperSpr *s, int x0, int y0, int x1, int y1)
{
    double sum = 0, n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            const uint8_t *p = s->px + ((size_t)y * s->w + x) * 4;
            if (!p[3]) continue;
            sum += (p[0] + p[1] + p[2]) / 3.0;
            n++;
        }
    return n ? sum / n : 0;
}

// the same over the body of a strip only (not the faint contact shadow round it)
static double body_lum(const PaperSpr *s)
{
    double sum = 0, n = 0;
    for (size_t i = 0, cnt = (size_t)s->w * s->h; i < cnt; i++) {
        const uint8_t *p = s->px + i * 4;
        if (p[3] < 128) continue;
        sum += (p[0] + p[1] + p[2]) / 3.0;
        n++;
    }
    return n ? sum / n : 0;
}

static void test_card(void)
{
    PaperStats s0 = paper_stats();
    PaperSpr *c = paper_card(150, 164, 6, 1);
    CHECK(c->w == 150 + 2 * PAPER_CARD_PAD && c->h == 164 + 2 * PAPER_CARD_PAD);
    CHECK(c->ox == PAPER_CARD_PAD && c->oy == PAPER_CARD_PAD);
    PaperStats s1 = paper_stats();
    CHECK(s1.count == s0.count + 1);
    CHECK(s1.bytes == s0.bytes + (size_t)c->w * c->h * 4);
    CHECK(c->px[3] == 0); // room around the card is clear
    int mid = (c->h / 2) * c->w + c->w / 2;
    CHECK(c->px[mid * 4 + 3] == 255);
    // light grain: the inside is a little darker than pure paper, by a few per cent on average and 8% at the most
    double lum = mean_lum(c, 30, 30, c->w - 30, c->h - 30), pure = ((PAPER_PAPER >> 16) + ((PAPER_PAPER >> 8) & 255) + (PAPER_PAPER & 255)) / 3.0;
    double dark = 1.0 - lum / pure;
    CHECK_MSG(dark > 0.01 && dark < 0.06, "mean grain darkening %.3f", dark);
    int worst = 0;
    for (int y = 30; y < c->h - 30; y++)
        for (int x = 30; x < c->w - 30; x++) {
            const uint8_t *p = c->px + ((size_t)y * c->w + x) * 4;
            int m = (p[0] + p[1] + p[2]) / 3;
            if ((int)pure - m > worst) worst = (int)pure - m;
        }
    CHECK_MSG(worst <= 0.09 * pure, "darkest grain %d of %.0f", worst, pure);
    CHECK(worst >= 0.03 * pure); // and it is visible somewhere
    // the cut edge is soft (partial alpha) and not perfectly straight: the top edge's first opaque row varies along it
    int partial = 0;
    double minrow = 1e9, maxrow = 0;
    for (int x = 40; x < c->w - 40; x++) {
        double edge = 0; // where the top edge is, to a fraction of a pixel: the coverage in the column above the middle
        for (int y = 0; y < c->h / 2; y++) edge += c->px[((size_t)y * c->w + x) * 4 + 3] / 255.0;
        edge = c->h / 2 - edge;
        if (edge < minrow) minrow = edge;
        if (edge > maxrow) maxrow = edge;
        for (int y = 0; y < 30; y++) {
            int a = c->px[((size_t)y * c->w + x) * 4 + 3];
            if (a > 0 && a < 255) partial++;
        }
    }
    CHECK(partial > 100);
    CHECK_MSG(maxrow - minrow <= 1.2, "edge wanders by %.2f px", maxrow - minrow);
    CHECK_MSG(maxrow - minrow >= 0.15, "edge is dead straight (%.2f px)", maxrow - minrow); // ... but not ruler straight
    // determinism: same seed, same bytes; another seed, other grain
    PaperSpr *c2 = paper_card(150, 164, 6, 1), *c3 = paper_card(150, 164, 6, 2), *c4 = paper_card(150, 164, 10, 1);
    CHECK(same(c, c2));
    CHECK(!same(c, c3));
    CHECK(!same(c, c4)); // the radius shows
    CHECK(c3->w == c->w);
    PaperSpr *w = paper_card_ex(600, 62, 6, 5, 6);
    CHECK(w->w == 612 && w->h == 74 && w->ox == 6);
    paper_spr_free(c);
    paper_spr_free(c2);
    paper_spr_free(c3);
    paper_spr_free(c4);
    paper_spr_free(w);
    PaperStats s2 = paper_stats();
    CHECK(s2.count == s0.count && s2.bytes == s0.bytes);
}

static PaperTape strip(int kind, int ci, unsigned flags, int len, int wid)
{
    PaperTape t = {len, wid, paper_tape_rgb[ci], kind, flags, 0, 1};
    return t;
}

static void test_tape(void)
{
    PaperStats s0 = paper_stats();
    // all eight patterns look different on the same colour, and all differ from a bare strip
    PaperSpr *p[PAT_COUNT];
    for (int k = 0; k < PAT_COUNT; k++) {
        PaperTape t = strip(k, 5, PAPER_TAPE_ZIG, 76, 24);
        p[k] = paper_tape(&t);
        CHECK(p[k]->w == p[0]->w && p[k]->h == p[0]->h);
    }
    for (int a = 0; a < PAT_COUNT; a++)
        for (int b = a + 1; b < PAT_COUNT; b++) CHECK_MSG(!same(p[a], p[b]), "patterns %d and %d are the same", a, b);
    // each pattern puts white on the colour: lighter than the colour alone (stripes etc. at 25% to 85%)
    uint32_t bare = paper_tape_rgb[5];
    double bare_l = (((bare >> 16) & 255) + ((bare >> 8) & 255) + (bare & 255)) / 3.0;
    for (int k = 0; k < PAT_COUNT; k++) {
        double l = body_lum(p[k]);
        CHECK_MSG(l > bare_l + 1.5 && l < bare_l + 60, "pattern %d mean %.1f vs colour %.1f", k, l, bare_l);
    }
    // every pattern has real structure: more than a few distinct values along a row through its middle
    for (int k = 0; k < PAT_COUNT; k++) {
        int lo = 255, hi = 0;
        for (int x = 10; x < p[k]->w - 10; x++)
            for (int y = p[k]->h / 2 - 4; y < p[k]->h / 2 + 4; y++) {
                int v = p[k]->px[((size_t)y * p[k]->w + x) * 4];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
        CHECK_MSG(hi - lo >= 20, "pattern %d is flat (%d..%d)", k, lo, hi);
    }
    // all eight colours: each strip differs, and its middle is its colour, lightened by the pattern and by 82% opacity (over nothing, so
    // the colour itself in straight alpha)
    PaperSpr *cs[PAPER_TAPE_COUNT];
    for (int i = 0; i < PAPER_TAPE_COUNT; i++) {
        PaperTape t = strip(PAT_STRIPES, i, PAPER_TAPE_ZIG, 76, 24);
        cs[i] = paper_tape(&t);
    }
    for (int a = 0; a < PAPER_TAPE_COUNT; a++)
        for (int b = a + 1; b < PAPER_TAPE_COUNT; b++) CHECK(!same(cs[a], cs[b]));
    for (int i = 0; i < PAPER_TAPE_COUNT; i++) paper_spr_free(cs[i]);
    // zigzag ends cut in, flat ends do not
    PaperTape tz = strip(PAT_GRID, 2, PAPER_TAPE_ZIG, 76, 24), tf = strip(PAT_GRID, 2, 0, 76, 24);
    PaperSpr *z = paper_tape(&tz), *f = paper_tape(&tf);
    int cx = z->w / 2, cy = z->h / 2, left = cx - 38; // the strip's left end column
    int opaque_z = 0, opaque_f = 0;
    for (int y = cy - 12; y < cy + 12; y++) {
        opaque_z += z->px[((size_t)y * z->w + left + 1) * 4 + 3] > 128;
        opaque_f += f->px[((size_t)y * f->w + left + 1) * 4 + 3] > 128;
    }
    CHECK_MSG(opaque_f == 24 && opaque_z < 20, "end column: flat %d of 24, zigzag %d", opaque_f, opaque_z);
    CHECK_MSG(f->px[(cy * f->w + cx) * 4 + 3] >= 225 && f->px[(cy * f->w + cx) * 4 + 3] <= 240, "alpha %d", f->px[(cy * f->w + cx) * 4 + 3]); // 93%, a little less where the paper's fibres are thin
    CHECK(z->ox == z->w / 2 && z->oy == z->h / 2);
    // a tilt is baked in: the sprite is taller than the strip
    PaperTape tt = tz;
    tt.tilt = 30;
    PaperSpr *ti = paper_tape(&tt);
    CHECK(ti->h > z->h + 15);
    // the crimp: a ridge in the middle, so it differs from a plain strip, and a longer strip's crimp sits elsewhere
    PaperTape tc = strip(PAT_DOTS, 3, PAPER_TAPE_ZIG | PAPER_TAPE_CRIMP, 200, 26);
    PaperTape tp = tc;
    tp.flags = PAPER_TAPE_ZIG;
    PaperSpr *c1 = paper_tape(&tc), *c0 = paper_tape(&tp);
    CHECK(c1->w == c0->w && c1->h >= c0->h && !same(c1, c0));
    tc.seed = 2;
    PaperSpr *c2 = paper_tape(&tc);
    CHECK(!same(c1, c2));
    tc.seed = 1;
    PaperSpr *c3 = paper_tape(&tc);
    CHECK(same(c1, c3));
    // the edge flag adds a hairline and soft shadow outside the strip
    PaperTape te = tf;
    te.flags = PAPER_TAPE_EDGE;
    PaperSpr *e = paper_tape(&te);
    CHECK(e->w > f->w - 1);
    paper_spr_free(e);
    paper_spr_free(c1);
    paper_spr_free(c0);
    paper_spr_free(c2);
    paper_spr_free(c3);
    paper_spr_free(ti);
    paper_spr_free(z);
    paper_spr_free(f);
    for (int k = 0; k < PAT_COUNT; k++) paper_spr_free(p[k]);
    PaperStats s2 = paper_stats();
    CHECK(s2.count == s0.count && s2.bytes == s0.bytes);
}

static void test_peel(void)
{
    PaperStats s0 = paper_stats();
    PaperTape t = strip(PAT_STRIPES, 0, PAPER_TAPE_ZIG, 76, 24);
    t.tilt = 4;
    PaperPeel *pl = paper_tape_peel(&t, PAPER_PEEL_STATES);
    CHECK(pl->n == PAPER_PEEL_STATES);
    for (int i = 1; i < pl->n; i++) {
        CHECK(pl->s[i]->w == pl->s[0]->w && pl->s[i]->h == pl->s[0]->h);
        CHECK_MSG(!same(pl->s[i], pl->s[i - 1]), "peel states %d and %d are the same", i - 1, i);
    }
    // state 0 is the stuck strip; the flap rises above the strip's top edge more and more
    PaperSpr *plain = paper_tape(&t);
    CHECK(plain->w <= pl->s[0]->w && plain->h <= pl->s[0]->h);
    int cy = pl->s[0]->h / 2;
    int prev = -1;
    for (int i = 0; i < pl->n; i++) {
        const PaperSpr *s = pl->s[i];
        int top = 0;
        for (int y = 0; y < cy - 14; y++) // rows above the strip (+ tilt slack)
            for (int x = 0; x < s->w; x++) top += s->px[((size_t)y * s->w + x) * 4 + 3] > 40;
        CHECK_MSG(top >= prev, "state %d reaches up less than the one before", i);
        prev = top;
    }
    CHECK(prev > 100);
    // the flap is lighter than the strip: the pale fold
    const PaperSpr *last = pl->s[pl->n - 1];
    double flap = 0, strip_l = mean_lum(pl->s[0], pl->s[0]->w / 2 - 38, cy - 8, pl->s[0]->w / 2 - 10, cy + 8);
    int n = 0;
    for (int y = 0; y < cy - 14; y++)
        for (int x = 0; x < last->w; x++) {
            const uint8_t *p = last->px + ((size_t)y * last->w + x) * 4;
            if (p[3] > 200) {
                flap += (p[0] + p[1] + p[2]) / 3.0;
                n++;
            }
        }
    CHECK(n > 50);
    CHECK_MSG(n && flap / n > strip_l, "flap %.1f vs strip %.1f", n ? flap / n : 0, strip_l);
    // a lift picks a state
    CHECK(paper_peel_at(pl, 0) == pl->s[0]);
    CHECK(paper_peel_at(pl, 1) == pl->s[pl->n - 1]);
    CHECK(paper_peel_at(pl, 0.5f) == pl->s[3] || paper_peel_at(pl, 0.5f) == pl->s[2]);
    CHECK(paper_stats().count == s0.count + pl->n + 1);
    paper_peel_free(pl);
    paper_spr_free(plain);
    PaperStats s2 = paper_stats();
    CHECK(s2.count == s0.count && s2.bytes == s0.bytes);
}

static void test_shadows(void)
{
    PaperStats s0 = paper_stats();
    PaperShadows *sh = paper_shadows(150, 164, 6, 0.06f, PAPER_SHADOW_LEVELS);
    CHECK(sh->n == 8);
    size_t bytes = 0;
    double prev_sum = 0;
    for (int i = 0; i < sh->n; i++) {
        const PaperSpr *s = sh->s[i];
        CHECK(s->w == sh->s[0]->w && s->h == sh->s[0]->h && s->ox == sh->s[0]->ox);
        CHECK(s->ox > 30 && s->w == 150 + 2 * s->ox);
        bytes += (size_t)s->w * s->h * 4;
        double sum = 0, cy = 0;
        int peak = 0;
        for (int y = 0; y < s->h; y++)
            for (int x = 0; x < s->w; x++) {
                int a = s->px[((size_t)y * s->w + x) * 4 + 3];
                sum += a;
                cy += (double)a * y;
                if (a > peak) peak = a;
                // the warm tint, wherever there is any
                if (a > 10) CHECK(s->px[((size_t)y * s->w + x) * 4] == ((PAPER_SHADOW >> 16) & 255));
            }
        float p = i / 7.0f, dy, bl, al;
        paper_shadow_params(p, &dy, &bl, &al);
        NEAR(dy, 2 + 14 * p, 1e-5);
        NEAR(bl, 5 + 12 * p, 1e-5);
        NEAR(al, 0.22 + 0.22 * p, 1e-5);
        CHECK_MSG(peak <= al * 255 + 1 && peak >= al * 255 * 0.8, "level %d peak alpha %d, wanted about %.0f", i, peak, al * 255);
        CHECK_MSG(sum > prev_sum, "shadow %d is not stronger than the one before", i);
        prev_sum = sum;
        // its weight sits below the card's middle by about the drop
        double mid = s->oy + 164 * 0.5;
        CHECK_MSG(cy / sum - mid > dy * 0.5f && cy / sum - mid < dy * 1.3f + 1, "level %d: centre of mass %.1f below the middle, drop %.1f", i, cy / sum - mid, dy);
        // nothing at the sprite's border: the room is enough for the blur
        for (int x = 0; x < s->w; x++) CHECK(s->px[x * 4 + 3] <= 1 && s->px[((size_t)(s->h - 1) * s->w + x) * 4 + 3] <= 1);
    }
    PaperStats s1 = paper_stats();
    CHECK(s1.count == s0.count + 8 && s1.bytes == s0.bytes + bytes);
    CHECK(paper_shadow_level(sh, 0) == 0 && paper_shadow_level(sh, 1) == 7 && paper_shadow_level(sh, 0.5f) == 4 && paper_shadow_level(sh, -3) == 0 && paper_shadow_level(sh, 9) == 7);
    PaperShadows *sh2 = paper_shadows(150, 164, 6, 0.06f, 4);
    CHECK(sh2->n == 4);
    for (int i = 0; i < 4; i++) CHECK(sh2->s[i]->w == sh->s[0]->w);
    NEAR(sh2->s[3]->px[(sh2->s[3]->h / 2 * sh2->s[3]->w + sh2->s[3]->w / 2) * 4 + 3], 0.44 * 255, 2); // top level: alpha .44
    paper_shadows_free(sh);
    paper_shadows_free(sh2);
    PaperStats s2 = paper_stats();
    CHECK(s2.count == s0.count && s2.bytes == s0.bytes);
}

// The crayon filter: a picture painted again with heavy wax strokes. Same size, same alpha, the same seed the same picture, another seed another
// one, and not a copy of what it was given; the colours stay in the picture's family.
static void test_crayon(void)
{
    enum { N = 80 };
    uint8_t *src = malloc((size_t)N * N * 4);
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) { // an orange ground with a white ring round a darker disc, as a game icon might be
            uint8_t *p = src + ((size_t)y * N + x) * 4;
            float dx = x - 40.0f, dy = y - 40.0f, d = sqrtf(dx * dx + dy * dy);
            uint32_t rgb = d < 14 ? 0xE87A24 : d < 28 ? 0xFFFFFF : 0xF4864A;
            p[0] = (uint8_t)(rgb >> 16);
            p[1] = (uint8_t)(rgb >> 8);
            p[2] = (uint8_t)rgb;
            p[3] = x < 4 ? 128 : 255;
        }
    clock_t t0 = clock();
    uint8_t *a = crayon_filter(src, N, 7), *b = crayon_filter(src, N, 7), *c = crayon_filter(src, N, 8);
    double ms = (clock() - t0) * 1000.0 / CLOCKS_PER_SEC / 3.0;
    CHECK(a && b && c);
    if (a && b && c) {
        CHECK_MSG(!memcmp(a, b, (size_t)N * N * 4), "the same seed paints the same picture");
        CHECK_MSG(memcmp(a, c, (size_t)N * N * 4), "another seed paints another one");
        int same = 0, alpha_ok = 1;
        double ms_src[3] = {0, 0, 0}, ms_out[3] = {0, 0, 0};
        for (int i = 0; i < N * N; i++) {
            same += !memcmp(src + 4 * i, a + 4 * i, 3);
            alpha_ok &= src[4 * i + 3] == a[4 * i + 3];
            for (int k = 0; k < 3; k++) {
                ms_src[k] += src[4 * i + k];
                ms_out[k] += a[4 * i + k];
            }
        }
        CHECK_MSG(same < N * N / 4, "it is painted over, not copied (%d of %d pixels as they were)", same, N * N);
        CHECK_MSG(alpha_ok, "the alpha is as it was");
        for (int k = 0; k < 3; k++) NEAR(ms_out[k] / (N * N), ms_src[k] / (N * N), 40.0); // (the strokes' colours average out near the picture's)
        printf("  crayon filter: %.1f ms for an %d px icon on this machine (the console is several times slower)\n", ms, N);
    }
    free(a);
    free(b);
    free(c);
    free(src);
}

// The banner's doodles: a star coloured in with pastel wax (its colouring cut to a five-pointed star, its outline drawn on it, both in the same
// place) and a loose highlighter patch. A disc of the same size would fail the area check, so the check can fail.
static void test_doodles(void)
{
    const int size = 100;
    PaperSpr *a = paper_star(size, 0xC7A4F6, 31u), *b = paper_star(size, 0xC7A4F6, 31u), *c = paper_star(size, 0xC7A4F6, 32u);
    CHECK(a && b && c);
    if (a && b && c) {
        CHECK_MSG(a->w == a->h && a->ox == a->w / 2 && a->oy == a->h / 2, "the star is square and anchored at its middle (%dx%d, %d,%d)", a->w, a->h, a->ox, a->oy);
        CHECK_MSG(!memcmp(a->px, b->px, (size_t)a->w * a->h * 4), "the same seed draws the same star");
        CHECK_MSG(memcmp(a->px, c->px, (size_t)a->w * a->h * 4), "another seed draws another");
        long solid = 0, violet = 0, outside = 0;
        const float cx = (float)a->ox, cy = (float)a->oy, R = size * 0.5f;
        for (int y = 0; y < a->h; y++)
            for (int x = 0; x < a->w; x++) {
                const uint8_t *p = a->px + ((size_t)y * a->w + x) * 4;
                if (p[3] > 128) {
                    solid++;
                    violet += p[2] > p[1] && p[0] > p[1];
                    float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                    outside += sqrtf(dx * dx + dy * dy) > R + 4.0f; // (nothing reaches past the star's points)
                }
            }
        double area = 1.381 * R * R; // a regular star with an inner radius of 0.47 of the outer
        CHECK_MSG(solid > area * 0.85 && solid < area * 1.2, "it is a star in size: %ld solid pixels, a star of that radius is %.0f (a disc would be %.0f)", solid, area, 3.1416 * R * R);
        CHECK_MSG(violet > solid * 0.97, "and in pastel violet (%ld of %ld)", violet, solid);
        CHECK_MSG(outside == 0, "and nothing is drawn beyond its points (%ld pixels)", outside);
        long mid = 0; // (the wax is speckled where the paper's tooth shows: the middle is judged over a patch)
        for (int y = -4; y <= 4; y++)
            for (int x = -4; x <= 4; x++) mid += a->px[((size_t)(a->oy + y) * a->w + a->ox + x) * 4 + 3];
        CHECK_MSG(mid / 81 > 150, "the middle is coloured in (mean alpha %ld)", mid / 81);
        int corner = a->px[((size_t)2 * a->w + 2) * 4 + 3];
        CHECK_MSG(corner == 0, "a corner of its box is bare (alpha %d)", corner);
    }
    paper_spr_free(a);
    paper_spr_free(b);
    paper_spr_free(c);
    PaperSpr *h = paper_hatch(120, 16, 0xFFCDB0, 0.85f, 32u);
    CHECK(h != NULL);
    if (h) {
        long band = 0, off = 0;
        for (int y = 0; y < h->h; y++)
            for (int x = 0; x < h->w; x++) {
                int al = h->px[((size_t)y * h->w + x) * 4 + 3], dy = abs(y - h->oy);
                if (dy <= 5 && x >= h->ox + 6 && x < h->ox + 114) band += al > 100;
                if (dy > 14) off += al > 0;
            }
        CHECK_MSG(band > 11 * 108 / 3, "the highlighter patch is coloured in along its middle (%ld solid pixels)", band);
        CHECK_MSG(off == 0, "and stays in its band (%ld pixels beyond it)", off);
    }
    paper_spr_free(h);
}

static void test_marks(void)
{
    PaperStats s0 = paper_stats();
    PaperSpr *r1 = paper_ring(15.4f, NULL, 3), *r2 = paper_ring(15.4f, NULL, 3), *r3 = paper_ring(15.4f, NULL, 4), *big = paper_ring(60, NULL, 3);
    CHECK(same(r1, r2));
    CHECK(!same(r1, r3));
    CHECK(big->w > r1->w + 80);
    CHECK(r1->ox == r1->w / 2 && r1->oy == r1->h / 2);
    // a ring is hollow; its ink is the crayon's; it is broken up by grain (many partial pixels) and sits near its radius
    CHECK(r1->px[(r1->oy * r1->w + r1->ox) * 4 + 3] == 0);
    int partial = 0, solid = 0;
    double rmin = 1e9, rmax = 0, cr = 0, cg = 0, cb = 0, cw = 0;
    for (int y = 0; y < r1->h; y++)
        for (int x = 0; x < r1->w; x++) {
            const uint8_t *p = r1->px + ((size_t)y * r1->w + x) * 4;
            if (!p[3]) continue;
            double d = sqrt((x + 0.5 - r1->ox) * (x + 0.5 - r1->ox) + (y + 0.5 - r1->oy) * (y + 0.5 - r1->oy));
            if (d < rmin) rmin = d;
            if (d > rmax) rmax = d;
            if (p[3] < 250) partial++;
            else solid++;
            cr += p[0] * p[3];
            cg += p[1] * p[3];
            cb += p[2] * p[3];
            cw += p[3];
        }
    CHECK_MSG(partial > solid / 2, "ring grain: %d partial, %d solid", partial, solid);
    CHECK_MSG(rmin > 10 && rmax < 23, "ring spans %.1f..%.1f from the middle", rmin, rmax);
    CHECK_MSG(fabs(cr / cw - 0x41) < 14 && fabs(cg / cw - 0x3C) < 14 && fabs(cb / cw - 0x5C) < 14, "ring colour %.0f %.0f %.0f", cr / cw, cg / cw, cb / cw);
    // blue style
    PaperSpr *rb = paper_ring(15.4f, &PAPER_CRAYON_BLUE, 3);
    CHECK(!same(rb, r1));
    PaperSpr *t1 = paper_tick(20, &PAPER_CRAYON_BLUE, 5), *t2 = paper_tick(20, &PAPER_CRAYON_BLUE, 5), *t3 = paper_tick(20, &PAPER_CRAYON_BLUE, 6);
    CHECK(same(t1, t2) && !same(t1, t3));
    PaperSpr *u1 = paper_underline(120, NULL, 1), *u2 = paper_underline(300, NULL, 1);
    CHECK(u2->w - u1->w == 180);
    CHECK(u1->ox < 12 && u1->oy > 4);
    PaperSpr *sc = paper_scribble(88, paper_tape_rgb[1], NULL, 2);
    CHECK(sc->w > 88);
    // the scribble fills its square: the middle is mostly covered
    int cover = 0, total = 0;
    for (int y = sc->h / 2 - 20; y < sc->h / 2 + 20; y++)
        for (int x = sc->w / 2 - 20; x < sc->w / 2 + 20; x++) {
            total++;
            cover += sc->px[((size_t)y * sc->w + x) * 4 + 3] > 100;
        }
    CHECK_MSG(cover > total * 0.5, "scribble covers %d of %d", cover, total);
    PaperSpr *g = paper_ghost(150, 164, 6), *g2 = paper_ghost(150, 164, 6), *g3 = paper_ghost(600, 62, 6);
    CHECK(g->w == 154 && g->h == 168 && g->ox == 2 && g->oy == 2 && same(g, g2) && g3->w == 604 && g3->h == 66);
    // the patch: fill in the middle (opaque), dashes at the edge (some pixels the edge colour, gaps the fill colour)
    const uint8_t *m = g->px + ((g->h / 2) * g->w + g->w / 2) * 4;
    CHECK(m[3] == 255 && m[0] == ((PAPER_GHOST_FILL >> 16) & 255) && m[2] == (PAPER_GHOST_FILL & 255));
    int dash = 0, gap = 0;
    for (int x = 20; x < g->w - 20; x++) {
        const uint8_t *e = g->px + ((size_t)0 * g->w + x) * 4;
        const uint8_t *e2 = g->px + ((size_t)1 * g->w + x) * 4;
        if (e[0] < 0xE0 && e2[0] < 0xE0) dash++;
        else gap++;
    }
    CHECK_MSG(dash > 20 && gap > 20, "dashes %d gaps %d on the top edge", dash, gap);
    paper_spr_free(r1);
    paper_spr_free(r2);
    paper_spr_free(r3);
    paper_spr_free(big);
    paper_spr_free(rb);
    paper_spr_free(t1);
    paper_spr_free(t2);
    paper_spr_free(t3);
    paper_spr_free(u1);
    paper_spr_free(u2);
    paper_spr_free(sc);
    paper_spr_free(g);
    paper_spr_free(g2);
    paper_spr_free(g3);
    PaperStats s2 = paper_stats();
    CHECK(s2.count == s0.count && s2.bytes == s0.bytes);
}

static double region_mean(const Surf *s, int x0, int y0, int w, int h, int ch)
{
    double sum = 0;
    for (int y = y0; y < y0 + h; y++)
        for (int x = x0; x < x0 + w; x++) sum += s->px[((size_t)y * s->w + x) * 4 + ch];
    return sum / ((double)w * h);
}

// how rough the board is: the picture's brightness against the average of the 5 x 5 pixels round each (rms), over a region
static double region_grain(const Surf *s, int x0, int y0, int w, int h)
{
    double sum = 0;
    long n = 0;
    for (int y = y0 + 2; y < y0 + h - 2; y++)
        for (int x = x0 + 2; x < x0 + w - 2; x++) {
            double m = 0;
            for (int dy = -2; dy <= 2; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    const uint8_t *q = s->px + ((size_t)(y + dy) * s->w + x + dx) * 4;
                    m += 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2];
                }
            const uint8_t *q = s->px + ((size_t)y * s->w + x) * 4;
            double d = 0.299 * q[0] + 0.587 * q[1] + 0.114 * q[2] - m / 25.0;
            sum += d * d;
            n++;
        }
    return sqrt(sum / (double)n);
}

static int cmp_int(const void *x, const void *y)
{
    int a = *(const int *)x, b = *(const int *)y;
    return a < b ? -1 : a > b;
}

// the stamp: a clean machined frame (the same on all four sides, the mockup's weights) and wear that only lightens it and never reshapes it
static void test_stamp(void)
{
    PaperStats s0 = paper_stats();
    PaperSpr *f = paper_stamp_frame(190, 58, 5, PAPER_INK);
    CHECK(f->w == 190 && f->h == 58 && f->ox == 95 && f->oy == 29);
    int asym = 0, ink = 0;
    for (int y = 0; y < f->h; y++)
        for (int x = 0; x < f->w; x++) {
            const uint8_t *a = f->px + ((size_t)y * f->w + x) * 4, *b = f->px + ((size_t)y * f->w + (f->w - 1 - x)) * 4,
                          *c = f->px + ((size_t)(f->h - 1 - y) * f->w + x) * 4;
            if (memcmp(a, b, 4) || memcmp(a, c, 4)) asym++;
            if (a[3]) ink++;
        }
    CHECK_MSG(asym == 0, "%d pixels of the clean frame are not mirrored left to right and top to bottom", asym);
    CHECK_MSG(ink > 1800 && ink < 3400, "the frame has %d inked pixels", ink);
    // the mockup's weights, read down the middle of the top edge: a 4 px heavy frame, a gap, then a 1.3 px thin frame (its path 6.5 px in)
    int col[10];
    for (int y = 0; y < 10; y++) col[y] = f->px[((size_t)y * f->w + 95) * 4 + 3];
    CHECK_MSG(col[0] >= 232 && col[1] >= 232 && col[2] >= 232 && col[3] >= 232, "heavy frame rows %d %d %d %d", col[0], col[1], col[2], col[3]);
    CHECK_MSG(col[4] == 0 && col[5] <= 40 && col[6] >= 232 && col[7] <= 40 && col[8] == 0, "thin frame rows %d %d %d %d %d", col[4], col[5], col[6], col[7], col[8]);
    CHECK_MSG(f->px[((size_t)30 * f->w + 95) * 4 + 3] == 0, "the middle of the stamp is empty (it is for the letters)");
    CHECK(f->px[0 * 4 + 3] < 60); // the corner is rounded
    // a small stamp has finer frames, so its letters have room
    PaperSpr *sm = paper_stamp_frame(104, 24, 9, PAPER_INK);
    int hv = 0;
    while (hv < 12 && sm->px[((size_t)hv * sm->w + 52) * 4 + 3] >= 200) hv++;
    CHECK_MSG(hv >= 2 && hv <= 3, "the small stamp's heavy frame is %d px", hv);
    // wear: lighter patches and fine pinholes, on the ink only
    PaperSpr *w = paper_stamp_frame(190, 58, 5, PAPER_INK), *w0 = paper_stamp_frame(190, 58, 5, PAPER_INK), *w2 = paper_stamp_frame(190, 58, 5, PAPER_INK), *w3 = paper_stamp_frame(190, 58, 5, PAPER_INK);
    paper_stamp_weather(w, 5, 1.0f);
    paper_stamp_weather(w0, 5, 0.0f);
    paper_stamp_weather(w2, 5, 1.0f);
    paper_stamp_weather(w3, 6, 1.0f);
    CHECK(same(w0, f));  // no wear, no change
    CHECK(same(w, w2));  // the same seed wears it the same way
    CHECK(!same(w, w3)); // another seed, other wear
    long solid = 0, holes = 0, lighter = 0;
    double ratio = 0;
    int grew = 0;
    for (int i = 0; i < f->w * f->h; i++) {
        int a0 = f->px[i * 4 + 3], a1 = w->px[i * 4 + 3];
        if (a1 > a0 || memcmp(f->px + i * 4, w->px + i * 4, 3)) grew++;
        if (a0 < 232) continue;
        solid++;
        ratio += a1 / (double)a0;
        if (a1 < a0 * 0.5) holes++;
        else if (a1 < a0 * 0.9) lighter++;
    }
    CHECK_MSG(grew == 0, "wear changed colour or added ink on %d pixels", grew);
    printf("  stamp wear on the big frame: ink kept %.1f%%, %.1f%% pinholes, %.1f%% lighter patches\n", 100.0 * ratio / solid, 100.0 * holes / solid, 100.0 * lighter / solid);
    CHECK_MSG(ratio / solid > 0.80 && ratio / solid < 0.97, "mean ink kept %.3f", ratio / solid);
    CHECK_MSG(holes > solid * 0.01 && holes < solid * 0.10, "%ld pinholes in %ld solid pixels", holes, solid);
    CHECK_MSG(lighter > solid * 0.05 && lighter < solid * 0.50, "%ld lighter pixels in %ld solid pixels", lighter, solid);
    paper_spr_free(f);
    paper_spr_free(sm);
    paper_spr_free(w);
    paper_spr_free(w0);
    paper_spr_free(w2);
    paper_spr_free(w3);
    PaperStats s1 = paper_stats();
    CHECK(s1.count == s0.count && s1.bytes == s0.bytes);
}

static void test_board(void)
{
    // the whole screen, as the app bakes it (with the mockup's own noise seed, 8)
    Surf *a = surf_new(SCREEN_W, SCREEN_H), *b = surf_new(SCREEN_W, SCREEN_H), *c = surf_new(SCREEN_W, SCREEN_H);
    paper_board_fill(a, 8);
    paper_board_fill(b, 8);
    paper_board_fill(c, 2);
    CHECK(!memcmp(a->px, b->px, (size_t)SCREEN_W * SCREEN_H * 4));
    CHECK(memcmp(a->px, c->px, (size_t)SCREEN_W * SCREEN_H * 4));
    // It is the mockup's board: these numbers were measured on Chrome's picture of the mockup with nothing on it (1280 x 720): the middle of the
    // board, its roughness, and the bottom right corner (the mockup darkens the corners a little). The seed-8 fleck pattern itself matched
    // that picture to within a level or two at every pixel when this was written.
    const double mid[3] = {235.35, 228.24, 211.74}, corner[3] = {224.41, 216.44, 197.93};
    for (int k = 0; k < 3; k++) NEAR(region_mean(a, 400, 300, 400, 200, k), mid[k], 0.6);
    for (int k = 0; k < 3; k++) NEAR(region_mean(a, 1160, 630, 120, 90, k), corner[k], 1.6);
    NEAR(region_grain(a, 400, 300, 400, 200), 9.02, 0.5);
    // the fleck pattern repeats every 320 px, as the mockup's tile does (in the middle, where the corner darkening is none)
    int same = 1;
    for (int y = 300; y < 340; y++) same &= !memcmp(a->px + ((size_t)y * SCREEN_W + 400) * 4, a->px + ((size_t)y * SCREEN_W + 400 + 320) * 4, 4 * 60);
    CHECK_MSG(same, "the flecks repeat every 320 px");
    // type reads on the board: against its average colour and a typical dark fleck (the 5th percentile) and, for the strongest ink, the darkest
    // pixel; flecks are single pixels, the type is many, so what counts is the colour the eye averages to
    int n = SCREEN_W * SCREEN_H;
    int *sums = malloc(sizeof(int) * (size_t)n);
    double mean[3] = {0, 0, 0};
    for (int i = 0; i < n; i++) {
        const uint8_t *p = a->px + (size_t)i * 4;
        sums[i] = p[0] + p[1] + p[2];
        for (int k = 0; k < 3; k++) mean[k] += p[k];
    }
    qsort(sums, (size_t)n, sizeof(int), cmp_int);
    double msum = mean[0] + mean[1] + mean[2];
    msum /= n;
    uint32_t avg = (uint32_t)(mean[0] / n) << 16 | (uint32_t)(mean[1] / n) << 8 | (uint32_t)(mean[2] / n);
    double scale5 = sums[n / 20] / msum, scale0 = sums[0] / msum;
    uint32_t p5 = (uint32_t)(mean[0] / n * scale5) << 16 | (uint32_t)(mean[1] / n * scale5) << 8 | (uint32_t)(mean[2] / n * scale5);
    uint32_t dark = (uint32_t)(mean[0] / n * scale0) << 16 | (uint32_t)(mean[1] / n * scale0) << 8 | (uint32_t)(mean[2] / n * scale0);
    CHECK(paper_contrast(PAPER_TEXT, avg) >= 4.5f && paper_contrast(PAPER_TEXT_SOFT, avg) >= 4.5f && paper_contrast(PAPER_INK, avg) >= 4.5f);
    CHECK(paper_contrast(PAPER_TEXT, p5) >= 4.5f && paper_contrast(PAPER_TEXT_SOFT, p5) >= 4.0f && paper_contrast(PAPER_INK, p5) >= 4.5f);
    CHECK(paper_contrast(PAPER_TEXT, dark) >= 4.5f && paper_contrast(PAPER_INK, dark) >= 4.5f);
    printf("  board mean %.2f %.2f %.2f (token %d %d %d, flecked darker); on the average: text %.1f, soft %.1f, ink %.1f; on the darkest: text %.1f, ink %.1f\n",
           mean[0] / n, mean[1] / n, mean[2] / n, (PAPER_BOARD >> 16) & 255, (PAPER_BOARD >> 8) & 255, PAPER_BOARD & 255, paper_contrast(PAPER_TEXT, avg),
           paper_contrast(PAPER_TEXT_SOFT, avg), paper_contrast(PAPER_INK, avg), paper_contrast(PAPER_TEXT, dark), paper_contrast(PAPER_INK, dark));
    free(sums);
    surf_free(a);
    surf_free(b);
    surf_free(c);
}

static void test_determinism_print(void)
{
    // a few hashes to compare with the console's, should that ever be wanted (the bakers avoid libm's inexact functions)
    PaperSpr *c = paper_card(150, 164, 6, 1);
    PaperTape t = strip(PAT_SCALLOP, 4, PAPER_TAPE_ZIG | PAPER_TAPE_CRIMP, 120, 26);
    PaperSpr *tp = paper_tape(&t), *r = paper_ring(15.4f, NULL, 3), *g = paper_ghost(150, 164, 6);
    PaperShadows *sh = paper_shadows(150, 164, 6, 0.06f, 8);
    printf("  hashes: card %08x tape %08x ring %08x ghost %08x shadow7 %08x\n", spr_hash(c), spr_hash(tp), spr_hash(r), spr_hash(g), spr_hash(sh->s[7]));
    PaperStats st = paper_stats();
    printf("  held by these: %d sprites, %.2f MB\n", st.count, st.bytes / 1048576.0);
    paper_spr_free(c);
    paper_spr_free(tp);
    paper_spr_free(r);
    paper_spr_free(g);
    paper_shadows_free(sh);
}

// ------------------------------------------------------------------------------------------------ the contact sheets
static Font *g_font, *g_bold;

static void label(Surf *s, float x, float y, const char *t)
{
    text_draw(s, g_font, 15, 0, x, y, 0, hex(PAPER_TEXT_SOFT, 1), t);
}
static void title(Surf *s, float x, float y, const char *t)
{
    text_draw(s, g_bold, 22, 0, x, y, 0, hex(PAPER_TEXT, 1), t);
}

// a region of one surface, magnified (nearest) onto another
static void zoom_copy(Surf *dst, int dx, int dy, const Surf *src, int sx, int sy, int sw, int sh, int k)
{
    for (int y = 0; y < sh * k; y++)
        for (int x = 0; x < sw * k; x++) {
            int ox = dx + x, oy = dy + y;
            if (ox < 0 || oy < 0 || ox >= dst->w || oy >= dst->h) continue;
            memcpy(dst->px + ((size_t)oy * dst->w + ox) * 4, src->px + ((size_t)(sy + y / k) * src->w + (sx + x / k)) * 4, 4);
        }
}

static void save(const Surf *s, const char *name)
{
    char path[200];
    mkdir("out", 0777);
    mkdir("out/paper", 0777);
    snprintf(path, sizeof path, "out/paper/%s", name);
    stbi_write_png(path, s->w, s->h, 4, s->px, s->w * 4);
    printf("  wrote test/%s\n", path);
}

static Surf *board(int w, int h)
{
    Surf *s = surf_new(w, h);
    paper_board_fill(s, 7);
    return s;
}

// a Home-style tile as one picture: the card, an avatar swatch with a person on it and its crayon ring, a name and a line
typedef struct { uint32_t swatch; const char *name, *sub; int pad; } TileArt;

static void draw_tile_art(Surf *s, void *ud)
{
    const TileArt *a = ud;
    float cx = a->pad + 75.0f, cy = a->pad + 60.0f;
    gfx_circle(s, cx, cy, 34, hex(a->swatch, 1));
    gfx_circle(s, cx, cy - 8, 11, hex(PAPER_PAPER, 1));
    gfx_blob(s, cx, cy + 32, 24, 17, hex(PAPER_PAPER, 1));
    gfx_ring(s, cx, cy, 44, 20, hex(PAPER_PAPER, 1)); // the person is cut off at the edge of the swatch
    text_draw(s, g_bold, 26, 0, cx, a->pad + 130, 1, hex(PAPER_TEXT, 1), a->name);
    text_draw(s, g_font, 16, 0, cx, a->pad + 153, 1, hex(PAPER_TEXT_SOFT, 1), a->sub);
}

static PaperSpr *make_tile(uint32_t seed, uint32_t swatch, const char *name, const char *sub)
{
    PaperSpr *card = paper_card(150, 164, 6, seed);
    TileArt a = {swatch, name, sub, card->ox};
    PaperSpr *art = paper_bake(card->w, card->h, draw_tile_art, &a);
    paper_spr_over(card, art, 0, 0);
    paper_spr_free(art);
    PaperSpr *ring = paper_ring(37.0f, NULL, seed * 3 + 1);
    paper_spr_over(card, ring, card->ox + 75, card->oy + 60);
    paper_spr_free(ring);
    return card;
}

typedef struct { PaperShadows *sh; PaperSpr *ghost; } Kit;

// one tile in the pose its lift gives it: the patch it left, its shadow, the card with its picture turned and raised, the tape riding on it
static void draw_tile(Surf *s, const Kit *k, const PaperSpr *card, const PaperPeel *peel, int x, int y, float tilt, double now, const PaperLift *lift)
{
    float w = 150, h = 164, cx = x + w / 2, cy = y + h / 2;
    PaperPose po;
    paper_lift_pose(lift, now, w, &po);
    if (po.ghost > 0.001f) paper_spr_xform(s, k->ghost, cx, cy, 1.0f, tilt * 3.14159265f / 180.0f, po.ghost);
    paper_spr_put(s, k->sh->s[paper_shadow_level(k->sh, po.p)], x, y + (int)floorf(po.y + 0.5f));
    PaperXf c = {cx + po.x, cy + po.y, po.scale, po.angle + tilt * 3.14159265f / 180.0f};
    paper_spr_xform(s, card, c.x, c.y, c.scale, c.angle, 1.0f);
    float dx, dy;
    paper_tape_offset(w, h, 37, -4, 76, 24, &dx, &dy);
    PaperXf t = paper_xf_attach(c, dx, dy);
    paper_spr_xform(s, paper_peel_at(peel, po.p), t.x, t.y, t.scale, t.angle, 1.0f);
}

// tape lightly pressed on: over a card's top edge, across a rounded corner, on the end of a long slip, two on a dialog's top edge; real size above, 4x below
static void pressed_case(Surf *sh, int X, int Y, float w, float h, float r, uint32_t seed, PaperTape *t, float tx, float ty, int zx, int zy, int zw, int zh, int zdx, int zdy)
{
    PaperSpr *card = paper_card((int)w, (int)h, r, seed);
    PaperShadows *shd = paper_shadows((int)w, (int)h, r, 0.06f, 8);
    paper_spr_put(sh, shd->s[0], X, Y);
    paper_spr_put(sh, card, X, Y);
    paper_tape_host(t, w, h, r, tx, ty);
    PaperSpr *tp = paper_tape(t);
    float dx, dy;
    paper_tape_offset(w, h, tx, ty, (float)t->len, (float)t->wid, &dx, &dy);
    paper_spr_put(sh, tp, (int)lroundf(X + w * 0.5f + dx), (int)lroundf(Y + h * 0.5f + dy));
    zoom_copy(sh, zdx, zdy, sh, X + zx, Y + zy, zw, zh, 4);
    paper_spr_free(tp);
    paper_spr_free(card);
    paper_shadows_free(shd);
}

static void sheet_pressed(void)
{
    Surf *sh = board(1280, 920);
    title(sh, 30, 36, "Tape lightly pressed on: a tile's top edge, a scrap's corner, a slip's end, a dialog's two corners (real size, then 4x)");
    PaperTape t1 = {76, 24, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG, 4, 7};
    pressed_case(sh, 60, 80, 150, 164, 6, 1, &t1, 37, -4, 20, -14, 110, 50, 40, 270);
    PaperTape t2 = {54, 22, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG, -30, 8};
    pressed_case(sh, 290, 100, 212, 92, 6, 2, &t2, -15.6f, -13.5f, -20, -22, 70, 50, 330, 270);
    PaperTape t3 = {64, 24, paper_tape_rgb[3], PAT_STRIPES, PAPER_TAPE_ZIG, -24, 9};
    pressed_case(sh, 560, 100, 420, 66, 7, 3, &t3, -15, -9, -24, -22, 90, 50, 640, 270);
    PaperTape t4a = {100, 28, paper_tape_rgb[0], PAT_STRIPES, PAPER_TAPE_ZIG, -30, 41}, t4b = {100, 28, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG, 30, 43};
    pressed_case(sh, 60, 470, 380, 180, 6, 4, &t4a, 60, -12, 20, -26, 130, 56, 40, 676);
    pressed_case(sh, 700, 470, 380, 180, 6, 5, &t4b, 220, -12, 220, -26, 130, 56, 760, 676);
    label(sh, 30, 268, "4x of the first four: edge of a tile, the scrap's corner, a slip's left end");
    save(sh, "paper-5-pressed-tape.png");
    surf_free(sh);
}

static void sheet_cards_tape(void)
{
    Surf *sh = board(1600, 1320);
    title(sh, 30, 40, "Paper cards, real size (left); a corner and the grain at 6x (right)");
    PaperSpr *c1 = paper_card(150, 164, 6, 1), *c2 = paper_card(150, 148, 10, 2), *c3 = paper_card(560, 62, 6, 3);
    PaperShadows *s1 = paper_shadows(150, 164, 6, 0.06f, 8), *s2 = paper_shadows(150, 148, 10, 0.06f, 8), *s3 = paper_shadows(560, 62, 6, 0.012f, 8);
    paper_spr_put(sh, s1->s[0], 50, 70);
    paper_spr_put(sh, c1, 50, 70);
    paper_spr_put(sh, s2->s[0], 230, 70);
    paper_spr_put(sh, c2, 230, 70);
    paper_spr_put(sh, s3->s[0], 50, 260);
    paper_spr_put(sh, c3, 50, 260);
    label(sh, 50, 258, "150x164 r6   150x148 r10   560x62 r6 (each with its resting shadow)");
    // magnify one corner of the first card, and a patch of its inside
    zoom_copy(sh, 640, 60, sh, 44, 64, 50, 36, 6);
    label(sh, 640, 55, "top left corner, 6x");
    zoom_copy(sh, 1000, 60, sh, 70, 140, 40, 30, 6);
    label(sh, 1000, 55, "grain, 6x");

    int y0 = 380;
    title(sh, 30, y0, "Tape: 8 patterns x 8 colours (zigzag ends, 76 x 24), real size, on the board and on a card");
    Surf *cardbg = board(1600, 600);
    for (int pat = 0; pat < PAT_COUNT; pat++)
        for (int ci = 0; ci < 8; ci++) {
            PaperTape t = {76, 24, paper_tape_rgb[ci], pat, PAPER_TAPE_ZIG, 0, (uint32_t)(pat * 8 + ci)};
            PaperSpr *p = paper_tape(&t);
            paper_spr_put(sh, p, 70 + ci * 100, y0 + 40 + pat * 38);
            paper_spr_free(p);
        }
    // the same on a white card
    gfx_rect(sh, 880, y0 + 20, 700, 330, hex(PAPER_PAPER, 1));
    for (int pat = 0; pat < PAT_COUNT; pat++)
        for (int ci = 0; ci < 4; ci++) {
            PaperTape t = {76, 24, paper_tape_rgb[(ci * 2 + pat) % 8], pat, PAPER_TAPE_ZIG, (float)(ci - 2) * 3, (uint32_t)(pat * 8 + ci)};
            PaperSpr *p = paper_tape(&t);
            paper_spr_put(sh, p, 940 + ci * 160, y0 + 50 + pat * 38);
            paper_spr_free(p);
        }
    // 5x magnification of the patterns on one colour, to see the pattern drawing
    Surf *z = board(480, 400);
    for (int pat = 0; pat < PAT_COUNT; pat++) {
        PaperTape t = {76, 24, paper_tape_rgb[pat % 8], pat, PAPER_TAPE_ZIG, 0, 1};
        PaperSpr *p = paper_tape(&t);
        paper_spr_put(z, p, 40 + (pat % 2) * 100, 20 + (pat / 2) * 34);
        paper_spr_free(p);
    }
    int y1 = y0 + 390;
    title(sh, 30, y1, "the patterns at 4x (stripes, dots, grid, zig, check, scallop, hline, wstripe)");
    for (int pat = 0; pat < PAT_COUNT; pat++) {
        PaperTape t = {76, 24, paper_tape_rgb[pat], pat, PAPER_TAPE_ZIG, 0, 1};
        PaperSpr *p = paper_tape(&t);
        Surf *one = board(p->w, p->h);
        paper_spr_put(one, p, p->ox, p->oy);
        zoom_copy(sh, 30 + (pat % 4) * 390, y1 + 20 + (pat / 4) * 120, one, 0, 0, one->w, one->h, 4);
        surf_free(one);
        paper_spr_free(p);
    }
    surf_free(z);
    surf_free(cardbg);

    int y2 = y1 + 290;
    title(sh, 30, y2, "Flat ends (left), zigzag (middle), crimped (right), tilted; long strips with the edge hairline and shadow");
    int pats[3] = {PAT_STRIPES, PAT_CHECK, PAT_ZIG};
    for (int i = 0; i < 3; i++) {
        PaperTape f = {160, 26, paper_tape_rgb[i * 2 + 1], pats[i], 0, 0, 1};
        PaperTape zz = {160, 26, paper_tape_rgb[i * 2 + 1], pats[i], PAPER_TAPE_ZIG, 0, 1};
        PaperTape cr = {160, 26, paper_tape_rgb[i * 2 + 1], pats[i], PAPER_TAPE_ZIG | PAPER_TAPE_CRIMP | PAPER_TAPE_EDGE, -2, (uint32_t)(3 + i)};
        PaperSpr *a = paper_tape(&f), *b = paper_tape(&zz), *c = paper_tape(&cr);
        paper_spr_put(sh, a, 110, y2 + 50 + i * 50);
        paper_spr_put(sh, b, 380, y2 + 50 + i * 50);
        paper_spr_put(sh, c, 650, y2 + 50 + i * 50);
        paper_spr_free(a);
        paper_spr_free(b);
        paper_spr_free(c);
    }
    PaperTape bar[3] = {{540, 30, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG | PAPER_TAPE_CRIMP | PAPER_TAPE_EDGE, 0.6f, 11},
                        {420, 26, paper_tape_rgb[3], PAT_SCALLOP, PAPER_TAPE_ZIG_R | PAPER_TAPE_CRIMP | PAPER_TAPE_EDGE, -0.5f, 12},
                        {300, 24, paper_tape_rgb[1], PAT_WSTRIPE, PAPER_TAPE_ZIG | PAPER_TAPE_EDGE, 0.4f, 13}};
    gfx_rect(sh, 880, y2 + 10, 700, 200, hex(PAPER_PAPER, 1));
    for (int i = 0; i < 3; i++) {
        PaperSpr *p = paper_tape(&bar[i]);
        paper_spr_put(sh, p, 1230, y2 + 50 + i * 60);
        paper_spr_free(p);
    }
    label(sh, 30, y2 + 215, "crimp: the far half sits a little out of line, with a ridge and wrinkles; EDGE adds the hairline and a soft shadow");
    save(sh, "paper-1-cards-tape.png");
    surf_free(sh);
    paper_spr_free(c1);
    paper_spr_free(c2);
    paper_spr_free(c3);
    paper_shadows_free(s1);
    paper_shadows_free(s2);
    paper_shadows_free(s3);
}

static void sheet_peel_marks(void)
{
    Surf *sh = board(1600, 1460);
    title(sh, 30, 40, "The peel: 6 states of a corner strip, stuck to fully peeled (real size, then 3x), on a card");
    int pats[4] = {PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG};
    for (int r = 0; r < 4; r++) {
        PaperTape t = {76, 24, paper_tape_rgb[(r * 2) % 8], pats[r], PAPER_TAPE_ZIG, (float)(r % 2 ? 4 : -4), (uint32_t)r + 1};
        PaperPeel *pl = paper_tape_peel(&t, 6);
        gfx_rect(sh, 30, 60 + r * 130, 1540, 120, hex(PAPER_PAPER, 1));
        for (int i = 0; i < 6; i++) {
            paper_spr_put(sh, pl->s[i], 80 + i * 90, 120 + r * 130);
            Surf *one = surf_new(pl->s[i]->w, pl->s[i]->h);
            gfx_fill(one, hex(PAPER_PAPER, 1));
            paper_spr_put(one, pl->s[i], pl->s[i]->ox, pl->s[i]->oy);
            zoom_copy(sh, 640 + i * 150, 62 + r * 130, one, 0, 0, one->w, one->h, 1);
            surf_free(one);
        }
        paper_peel_free(pl);
    }
    {
        PaperTape t = {76, 24, paper_tape_rgb[2], PAT_GRID, PAPER_TAPE_ZIG, 0, 3};
        PaperPeel *pl = paper_tape_peel(&t, 6);
        for (int i = 0; i < 6; i++) {
            Surf *one = surf_new(pl->s[i]->w, pl->s[i]->h);
            gfx_fill(one, hex(PAPER_PAPER, 1));
            paper_spr_put(one, pl->s[i], pl->s[i]->ox, pl->s[i]->oy);
            zoom_copy(sh, 30 + i * 262, 600, one, 0, 0, one->w, one->h, 2);
            surf_free(one);
        }
        label(sh, 30, 585, "2x: states 0..5 of a grid strip");
        paper_peel_free(pl);
    }

    title(sh, 30, 840, "Crayon marks (real size and 4x): rings r 11, 15.4, 19.6, 30, 60; ticks; underlines; scribbled squares");
    const PaperCrayon *st[2] = {&PAPER_CRAYON_STD, &PAPER_CRAYON_BLUE};
    float radii[5] = {11, 15.4f, 19.6f, 30, 60};
    gfx_rect(sh, 30, 860, 1540, 310, hex(PAPER_PAPER, 1));
    int x = 60;
    for (int i = 0; i < 5; i++) {
        gfx_circle(sh, x + radii[i] + 8, 930, radii[i] * 0.9f, hex(paper_tape_rgb[(i + 3) % 8], 1));
        PaperSpr *r = paper_ring(radii[i], st[0], (uint32_t)i + 1);
        paper_spr_put(sh, r, x + (int)radii[i] + 8, 930);
        paper_spr_free(r);
        x += (int)(radii[i] * 2) + 30;
    }
    {
        // 4x of the 15.4 ring, standard and blue
        for (int s = 0; s < 2; s++) {
            Surf *one = surf_new(60, 60);
            gfx_fill(one, hex(PAPER_PAPER, 1));
            gfx_circle(one, 30, 30, 14, hex(paper_tape_rgb[5], 1));
            PaperSpr *r = paper_ring(15.4f, st[s], 5);
            paper_spr_put(one, r, 30, 30);
            paper_spr_free(r);
            zoom_copy(sh, 700 + s * 250, 870, one, 0, 0, 60, 60, 4);
            surf_free(one);
        }
        for (int s = 0; s < 2; s++) {
            Surf *one = surf_new(60, 60);
            gfx_fill(one, hex(PAPER_PAPER, 1));
            PaperSpr *t = paper_tick(20, st[s], 7);
            paper_spr_put(one, t, 30, 30);
            paper_spr_free(t);
            zoom_copy(sh, 1200 + s * 190, 870, one, 0, 0, 60, 60, 3);
            surf_free(one);
        }
    }
    int ux = 60;
    for (int i = 0; i < 4; i++) {
        PaperSpr *t = paper_tick(14.0f + i * 4, st[i % 2], (uint32_t)i + 20);
        paper_spr_put(sh, t, ux + 20, 1060);
        paper_spr_free(t);
        ux += 60;
    }
    PaperSpr *u1 = paper_underline(180, &PAPER_CRAYON_BLUE, 1), *u2 = paper_underline(320, &PAPER_CRAYON_BLUE, 2), *u3 = paper_underline(120, &PAPER_CRAYON_STD, 3);
    text_draw(sh, g_bold, 40, 0, 330, 1076, 0, hex(PAPER_TEXT, 1), "Splatoon 3");
    paper_spr_put(sh, u1, 334, 1088);
    paper_spr_put(sh, u2, 500, 1112);
    paper_spr_put(sh, u3, 880, 1112);
    paper_spr_free(u1);
    paper_spr_free(u2);
    paper_spr_free(u3);
    {
        Surf *one = surf_new(200, 80);
        gfx_fill(one, hex(PAPER_PAPER, 1));
        PaperSpr *a = paper_underline(180, &PAPER_CRAYON_BLUE, 1);
        paper_spr_put(one, a, 12, 30);
        paper_spr_free(a);
        zoom_copy(sh, 1160, 1070, one, 0, 14, 200, 50, 2);
    }

    title(sh, 30, 1210, "Scribbled-in squares (88 px), the dashed ghost patch, with the card on it");
    for (int i = 0; i < 4; i++) {
        PaperSpr *s = paper_scribble(88, paper_tape_rgb[(i * 2 + 1) % 8], NULL, (uint32_t)i + 1);
        paper_spr_put(sh, s, 100 + i * 160, 1340);
        paper_spr_free(s);
    }
    PaperSpr *g = paper_ghost(150, 164, 6), *c = paper_card(150, 164, 6, 9);
    PaperShadows *shd = paper_shadows(150, 164, 6, 0.06f, 8);
    gfx_rect(sh, 800, 1230, 770, 200, hex(PAPER_BOARD, 1));
    paper_spr_put(sh, g, 830, 1240);
    label(sh, 830, 1426, "ghost 150x164");
    PaperSpr *g3 = paper_ghost(60, 80, 6);
    paper_spr_put(sh, g3, 1010, 1240);
    paper_spr_put(sh, g, 1100, 1240);
    paper_spr_put(sh, shd->s[7], 1100 + 12, 1240 - 8);
    paper_spr_put(sh, c, 1100 + 12, 1240 - 8);
    paper_spr_free(g);
    paper_spr_free(g3);
    paper_spr_free(c);
    paper_shadows_free(shd);
    save(sh, "paper-2-peel-marks.png");
    surf_free(sh);
}

static const char *NAMES10[10] = {"Ava", "Eli", "Jun", "Kai", "Leo", "Mira", "Nia", "Sam", "Theo", "Uma"};
static const uint32_t SWATCH[10] = {0x90DCF3, 0xFFCDCC, 0xFFE3CD, 0xFFFF9A, 0xBFEFE1, 0xD4FAC7, 0xAED6F9, 0xFFC49A, 0xFFCCFA, 0xD3A9FF};

// the Home screen in the mockup's layout (10 tiles, 150 x 164 at 164 px apart, tape on each); `focus` is lifted `p_now` ms into its grab
static void draw_board(Surf *s, int ox, int oy, int focus, int prev, double ms, int w_cols)
{
    Kit k = {paper_shadows(150, 164, 6, 0.06f, 8), paper_ghost(150, 164, 6)};
    PaperSpr *cards[10];
    PaperPeel *peels[10];
    PaperLift L[10];
    for (int i = 0; i < 10; i++) {
        cards[i] = make_tile((uint32_t)i + 1, SWATCH[i], NAMES10[i], "3 games - 1d ago");
        PaperTape t = {76, 24, paper_tape_rgb[(i * 3 + 3) % 8], i % 4, PAPER_TAPE_ZIG, (float)(i % 2 ? 4 : -4), (uint32_t)i + 1};
        peels[i] = paper_tape_peel(&t, 6);
        paper_lift_init(&L[i], LIFT_TILE);
    }
    double now = 1000 + ms;
    if (prev >= 0) {
        paper_lift_grab(&L[prev], 1000, 0);
        paper_lift_release(&L[prev], 1000 + 160, 0);
    }
    if (focus >= 0) paper_lift_grab(&L[focus], prev >= 0 ? 1000 + 160 : 1000, 0);
    for (int pass = 0; pass < 2; pass++)
        for (int i = 0; i < 10; i++) {
            int lifted = paper_lift_value(&L[i], now) > 0.001f;
            if (lifted != pass) continue;
            int x = ox + (i % w_cols) * 164, y = oy + (i / w_cols) * 176;
            draw_tile(s, &k, cards[i], peels[i], x, y, (float)((i * 5) % 7 - 3) * 0.3f, now, &L[i]);
        }
    for (int i = 0; i < 10; i++) {
        paper_spr_free(cards[i]);
        paper_peel_free(peels[i]);
    }
    paper_spr_free(k.ghost);
    paper_shadows_free(k.sh);
}

static void sheet_shadow_lift(void)
{
    Surf *sh = board(1600, 1080);
    title(sh, 30, 40, "Shadow levels 0..7 (8 baked) with a card on each: p = 0, .14 ... 1");
    PaperShadows *shd = paper_shadows(150, 164, 6, 0.06f, 8);
    PaperSpr *card = paper_card(150, 164, 6, 4);
    for (int i = 0; i < 8; i++) {
        float p = i / 7.0f;
        int x = 40 + i * 190, y = 90;
        paper_spr_put(sh, shd->s[i], x, y - (int)(9 * p + 0.5f));
        paper_spr_xform(sh, card, x + 75.0f, y + 82.0f - 9 * p, 1.0f + 0.06f * p, 0, 1.0f);
        char t[40];
        snprintf(t, sizeof t, "level %d  p=%.2f", i, p);
        label(sh, x, y + 200, t);
    }
    paper_spr_free(card);
    paper_shadows_free(shd);
    title(sh, 30, 340, "Home board, the focus moving (real size): at rest; grabbed, 45, 90, 133 ms; then moved on (tile 1 let go, tile 2 grabbed)");
    double ms[5] = {0, 45, 90, 133, 120};
    for (int m = 0; m < 5; m++) {
        int ox = 20 + (m % 2) * 780, oy = 370 + (m / 2) * 220;
        Surf *scene = board(700, 200);
        // two columns of the board: tiles 0..3 on the left, 2 rows would not fit, so one row of four tiles
        draw_board(scene, 10, 30, 1, m == 4 ? 0 : -1, m == 4 ? 160 + ms[m] : ms[m], 4);
        zoom_copy(sh, ox, oy, scene, 0, 0, 700, 200, 1);
        char t[80];
        snprintf(t, sizeof t, m == 4 ? "tile 0 let go at 0 ms, tile 1 grabbed at 160 ms, now 280 ms" : "tile 1 grabbed, %.0f ms ago", ms[m]);
        label(sh, ox + 8, oy + 20, t);
        surf_free(scene);
    }
    save(sh, "paper-3-shadow-lift.png");
    surf_free(sh);
}

// the whole Home screen, as the mockup lays it out, with the focus resting on one tile
static void sheet_home(void)
{
    Surf *sh = board(1280, 720);
    title(sh, 40, 50, "Home: ten tiles (a tile is 150 x 164, tape 76 x 24), focus on the sixth, resting");
    draw_board(sh, 40, 90, 5, -1, 600, 7);
    save(sh, "paper-4-home.png");
    surf_free(sh);
}

static void test_timing(void)
{
    // how long the bakers take here (the console is about 8 to 10 times slower than a PC)
    clock_t t = clock();
    PaperSpr *c = paper_card(150, 164, 6, 1);
    double ms_card = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
    t = clock();
    PaperTape tp = {76, 24, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 4, 1};
    PaperPeel *pl = paper_tape_peel(&tp, PAPER_PEEL_STATES);
    double ms_peel = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
    t = clock();
    PaperShadows *sh = paper_shadows(150, 164, 6, 0.06f, 8);
    double ms_sh = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
    t = clock();
    PaperSpr *r = paper_ring(37, NULL, 1), *g = paper_ghost(150, 164, 6);
    double ms_mark = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
    t = clock();
    PaperTape bar = {540, 30, paper_tape_rgb[5], PAT_DOTS, PAPER_TAPE_ZIG | PAPER_TAPE_CRIMP | PAPER_TAPE_EDGE, 0.6f, 11};
    PaperSpr *b = paper_tape(&bar);
    double ms_bar = (clock() - t) * 1000.0 / CLOCKS_PER_SEC;
    printf("  bake times on this PC (ASAN build): card %.1f ms, 6 peel states %.1f ms, 8 shadow levels %.1f ms, ring+ghost %.1f ms, 540 px bar %.1f ms\n", ms_card,
           ms_peel, ms_sh, ms_mark, ms_bar);
    printf("  sizes: card %dx%d, peel state %dx%d, shadow level %dx%d, bar %dx%d\n", c->w, c->h, pl->s[0]->w, pl->s[0]->h, sh->s[0]->w, sh->s[0]->h, b->w, b->h);
    paper_spr_free(c);
    paper_peel_free(pl);
    paper_shadows_free(sh);
    paper_spr_free(r);
    paper_spr_free(g);
    paper_spr_free(b);
}

// Tape put on by hand (paper_tape_hand): the same item gives the same strip every time, no two items in a row of tiles are alike, the numbers stay
// inside what the callers' padding allows, spread 0 leaves a strip as asked, and about as many have a ridge or an end that stands up as the numbers say.
static void test_tape_hand(void)
{
    enum { N = 400 };
    int ridge = 0, stood = 0, same_as_prev = 0, ends_seen[4] = {0, 0, 0, 0};
    PaperTape prev = {0};
    for (int i = 0; i < N; i++) {
        PaperTape a = {54, 20, 0xAED6F9, PAT_DOTS, PAPER_TAPE_ZIG, -40.0f, 300u}, b = a;
        float ax = -12, ay = 1, bx = -12, by = 1;
        uint32_t h = (uint32_t)i * 7919u + 11u;
        float la = paper_tape_hand(&a, &ax, &ay, h, 1.0f), lb = paper_tape_hand(&b, &bx, &by, h, 1.0f);
        CHECK(la == lb && a.len == b.len && a.wid == b.wid && a.tilt == b.tilt && a.flags == b.flags && a.seed == b.seed && ax == bx && ay == by);
        CHECK(a.len >= 40 && a.len <= 68);                  // 54 +- 22%
        CHECK(a.wid >= 18 && a.wid <= 22);
        CHECK(fabsf(a.tilt + 40.0f) <= 12.01f);             // 7 + 5 degrees at most
        CHECK(fabsf(ax + 12.0f) <= 9.01f && fabsf(ay - 1.0f) <= 4.51f);
        CHECK(la == 0.0f || la == 0.2f);                    // a state of the six the peel makes
        if ((a.flags & PAPER_TAPE_CRIMP) && a.len >= 40) ridge++;
        if (la > 0.0f) stood++;
        ends_seen[((a.flags & PAPER_TAPE_ZIG_L) ? 1 : 0) + ((a.flags & PAPER_TAPE_ZIG_R) ? 2 : 0)]++;
        if (i > 0 && a.len == prev.len && a.tilt == prev.tilt && a.flags == prev.flags && ax == bx && a.wid == prev.wid) same_as_prev++;
        prev = a;
    }
    CHECK(same_as_prev <= 2);                               // neighbours are not copies of each other
    CHECK(ridge > N * 20 / 100 && ridge < N * 40 / 100);    // 30 in 100
    CHECK(stood > N * 14 / 100 && stood < N * 30 / 100);    // 22 in 100
    for (int k = 0; k < 4; k++) CHECK(ends_seen[k] > N / 12); // every mix of zigzag and plain/slanted ends turns up
    PaperTape t = {56, 22, 0xAED6F9, PAT_DOTS, PAPER_TAPE_ZIG, -20.0f, 300u};
    float x = 4, y = -8;
    paper_tape_hand(&t, &x, &y, 77u, 0.0f);                 // spread 0: where and how it was asked for (the ends and ridge still vary)
    CHECK(t.len == 56 && t.wid == 22 && t.tilt == -20.0f && x == 4 && y == -8);
}

// With no argument it runs the checks. `--sheets` writes the contact sheets to out/paper/ to look at, and `--info` prints the bake times and a few hashes;
// neither asserts anything, so they stay out of the default run.
int main(int argc, char **argv)
{
    g_font = font_load(font_medium_ttf);
    g_bold = font_load(font_semibold_ttf);
    if (argc > 1 && !strcmp(argv[1], "--sheets")) {
        sheet_cards_tape();
        sheet_pressed();
        sheet_peel_marks();
        sheet_shadow_lift();
        sheet_home();
        return 0;
    }
    if (argc > 1 && !strcmp(argv[1], "--info")) {
        test_determinism_print();
        test_timing();
        return 0;
    }
    test_tokens();
    test_lift_table();
    test_interrupt();
    test_wiggle();
    test_board();
    test_stamp();
    test_card();
    test_tape();
    test_tape_hand();
    test_peel();
    test_shadows();
    test_marks();
    test_crayon();
    test_doodles();
    PaperStats st = paper_stats();
    CHECK_MSG(st.count == 0 && st.bytes == 0, "%d sprites (%zu bytes) were not freed", st.count, st.bytes);
    printf("paper: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
