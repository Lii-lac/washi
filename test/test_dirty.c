// Dirty rectangles and the framebuffer history (source/dirty.c), checked on the PC: the list clips, drops, merges and caps; and a long
// random walk of frames changed only inside the claimed rectangles, mixed with whole-frame presents, shows on a pretend framebuffer
// of 1 to 3 round-robin images exactly the frame, after every single present. A naive copy and a lying caller must be caught.
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/dirty.h"

#define HP_STRIDE_FOR_TEST (SCREEN_W * 4 + 64) // padded rows, like a framebuffer may have

static int g_checks, g_failed;
#define CHECK(c) CHECK_MSG(c, "%s", #c)
#define CHECK_MSG(c, ...)                                                 \
    do {                                                                  \
        g_checks++;                                                       \
        if (!(c)) {                                                       \
            g_failed++;                                                   \
            printf("  FAIL [dirty] %s:%d: ", __FILE__, __LINE__);         \
            printf(__VA_ARGS__);                                          \
            printf("\n");                                                 \
        }                                                                 \
    } while (0)

static uint32_t g_rng = 12345u;
static uint32_t rnd(void)
{
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 17;
    g_rng ^= g_rng << 5;
    return g_rng;
}
static int rr(int lo, int hi) { return lo + (int)(rnd() % (uint32_t)(hi - lo + 1)); } // lo..hi

static int same_rect(Rect a, int x, int y, int w, int h) { return a.x == x && a.y == y && a.w == w && a.h == h; }

// the list's promises: every rectangle on the screen and not empty, none overlapping or touching another, no more than DIRTY_MAX
static int touching(Rect a, Rect b) // (written apart from dirty.c on purpose)
{
    int ax2 = a.x + a.w, ay2 = a.y + a.h, bx2 = b.x + b.w, by2 = b.y + b.h;
    int xgap = a.x > bx2 ? a.x - bx2 : b.x > ax2 ? b.x - ax2 : 0;
    int ygap = a.y > by2 ? a.y - by2 : b.y > ay2 ? b.y - ay2 : 0;
    int xover = a.x < bx2 && b.x < ax2, yover = a.y < by2 && b.y < ay2;
    if (xover && yover) return 1;                    // overlap
    if (xover && ygap == 0 && !yover) return 1;      // share a horizontal side
    if (yover && xgap == 0 && !xover) return 1;      // share a vertical side
    return 0;                                        // (a corner alone, or a gap)
}

static int list_ok(const Dirty *d)
{
    if (d->n < 0 || d->n > DIRTY_MAX) return 0;
    if (d->full && d->n != 0) return 0;
    for (int i = 0; i < d->n; i++) {
        Rect a = d->r[i];
        if (a.w <= 0 || a.h <= 0 || a.x < 0 || a.y < 0 || a.x + a.w > SCREEN_W || a.y + a.h > SCREEN_H) return 0;
        for (int j = i + 1; j < d->n; j++)
            if (touching(a, d->r[j])) return 0;
    }
    return 1;
}

static void test_merge(void)
{
    Dirty d;
    dirty_clear(&d);
    dirty_add(&d, 0, 0, 100, 100);
    dirty_add(&d, 50, 50, 100, 100);
    CHECK_MSG(d.n == 1 && same_rect(d.r[0], 0, 0, 150, 150), "overlapping rectangles become one");
    dirty_clear(&d);
    dirty_add(&d, 0, 0, 100, 100);
    dirty_add(&d, 100, 0, 50, 100);
    CHECK_MSG(d.n == 1 && same_rect(d.r[0], 0, 0, 150, 100), "rectangles that share a side become one");
    dirty_clear(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 10, 10, 10, 10);
    CHECK_MSG(d.n == 2, "rectangles that meet only at a corner stay apart (n = %d)", d.n);
    dirty_clear(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 11, 0, 10, 10);
    CHECK_MSG(d.n == 2, "one pixel of gap keeps them apart");
    dirty_clear(&d);
    dirty_add(&d, 0, 0, 100, 100);
    dirty_add(&d, 10, 10, 5, 5);
    dirty_add(&d, 0, 0, 100, 100);
    CHECK_MSG(d.n == 1 && same_rect(d.r[0], 0, 0, 100, 100), "a rectangle inside another, and a repeat, add nothing");
    dirty_clear(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 30, 0, 10, 10);
    dirty_add(&d, 10, 0, 20, 10);
    CHECK_MSG(d.n == 1 && same_rect(d.r[0], 0, 0, 40, 10), "one that bridges two others joins all three");
    dirty_clear(&d);
    dirty_add(&d, 0, 0, 10, 10);
    dirty_add(&d, 500, 500, 10, 10);
    dirty_add(&d, 5, 5, 10, 10);
    dirty_add(&d, 505, 505, 10, 10);
    CHECK_MSG(d.n == 2 && list_ok(&d), "two groups stay two (n = %d)", d.n);
    dirty_clear(&d);
    dirty_add(&d, 0, 0, SCREEN_W, 400);
    dirty_add(&d, 0, 400, SCREEN_W, SCREEN_H - 400);
    CHECK_MSG(d.full && d.n == 0, "two halves that make the whole screen are a full present");
    Dirty a, b;
    dirty_clear(&a);
    dirty_clear(&b);
    dirty_add(&a, 0, 0, 10, 10);
    dirty_add(&b, 20, 0, 10, 10);
    dirty_union(&a, &b);
    CHECK(a.n == 2 && dirty_has(&a, 25, 5) && dirty_has(&a, 5, 5) && !dirty_has(&a, 15, 5));
}

// all the pixels that were put in are still covered, however many rectangles it took
static void test_cap(void)
{
    Dirty d;
    dirty_clear(&d);
    Rect in[40];
    for (int i = 0; i < 40; i++) { // 40 small rectangles with gaps between: far over the cap
        Rect r = {(i % 10) * 120 + 5, (i / 10) * 170 + 7, 30 + i, 20 + i};
        in[i] = r;
        dirty_add_rect(&d, r);
        CHECK_MSG(d.n <= DIRTY_MAX, "after %d adds there are %d rectangles", i + 1, d.n);
    }
    CHECK(list_ok(&d));
    int lost = 0;
    for (int i = 0; i < 40; i++)
        for (int y = in[i].y; y < in[i].y + in[i].h; y++)
            for (int x = in[i].x; x < in[i].x + in[i].w; x++) lost += !dirty_has(&d, x, y);
    CHECK_MSG(lost == 0, "%d pixels fell out of the list when it was capped", lost);
    CHECK_MSG(d.n > 1, "it is not collapsed into one rectangle (n = %d)", d.n);

    // random lists, exact: nothing put in is ever lost and the promises hold after every add
    static uint8_t mark[SCREEN_W * SCREEN_H];
    for (int trial = 0; trial < 10; trial++) {
        memset(mark, 0, sizeof mark);
        dirty_clear(&d);
        int n = rr(1, 45), bad_list = 0;
        for (int i = 0; i < n; i++) {
            int big = rnd() % 8 == 0;
            Rect r = {rr(-100, SCREEN_W + 50), rr(-100, SCREEN_H + 50), rr(-5, big ? 700 : 120), rr(-5, big ? 500 : 90)};
            dirty_add_rect(&d, r);
            for (int y = r.y < 0 ? 0 : r.y; y < r.y + r.h && y < SCREEN_H; y++)
                for (int x = r.x < 0 ? 0 : r.x; x < r.x + r.w && x < SCREEN_W; x++) mark[y * SCREEN_W + x] = 1;
            if (!list_ok(&d)) bad_list++;
        }
        long miss = 0;
        for (int y = 0; y < SCREEN_H; y++)
            for (int x = 0; x < SCREEN_W; x++) miss += mark[y * SCREEN_W + x] && !dirty_has(&d, x, y);
        CHECK_MSG(bad_list == 0, "trial %d: the list broke its promises %d times", trial, bad_list);
        CHECK_MSG(miss == 0, "trial %d: %ld pixels lost", trial, miss);
    }
}

static void test_full_flag(void)
{
    Dirty d;
    dirty_clear(&d);
    CHECK(!d.full && d.n == 0 && dirty_area(&d) == 0);
    dirty_all(&d);
    CHECK(d.full && d.n == 0 && dirty_area(&d) == SCREEN_W * SCREEN_H);
    CHECK(dirty_has(&d, 0, 0) && dirty_has(&d, SCREEN_W - 1, SCREEN_H - 1) && !dirty_has(&d, SCREEN_W, 0));
    dirty_add(&d, 5, 5, 10, 10);
    CHECK_MSG(d.full && d.n == 0, "adding to a full list changes nothing");
    dirty_clear(&d);
    CHECK(!d.full && d.n == 0);
    dirty_add(&d, 0, 0, SCREEN_W, SCREEN_H);
    CHECK_MSG(d.full, "the whole-screen rectangle is the full flag");
    Dirty a, b;
    dirty_clear(&a);
    dirty_add(&a, 1, 1, 5, 5);
    dirty_all(&b);
    dirty_union(&a, &b);
    CHECK_MSG(a.full && a.n == 0, "a union with a full list is full");
}

static void test_copy(void)
{
    Surf f = {SCREEN_W, SCREEN_H, malloc((size_t)SCREEN_W * SCREEN_H * 4)};
    for (size_t i = 0; i < (size_t)SCREEN_W * SCREEN_H * 4; i++) f.px[i] = (uint8_t)(rnd() | 1);
    size_t stride = HP_STRIDE_FOR_TEST;
    uint8_t *dst = malloc(stride * SCREEN_H);
    memset(dst, 0x11, stride * SCREEN_H);
    Dirty d;
    dirty_clear(&d);
    dirty_add(&d, 5, 6, 7, 8);
    dirty_add(&d, 1273, 712, 7, 8);
    dirty_copy(&f, dst, stride, &d);
    int wrong = 0;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            int in = dirty_has(&d, x, y);
            const uint8_t *s = f.px + ((size_t)y * SCREEN_W + x) * 4, *q = dst + (size_t)y * stride + (size_t)x * 4;
            for (int c = 0; c < 4; c++) wrong += in ? q[c] != s[c] : q[c] != 0x11;
        }
    for (int y = 0; y < SCREEN_H; y++)
        for (size_t x = (size_t)SCREEN_W * 4; x < stride; x++) wrong += dst[(size_t)y * stride + x] != 0x11; // the row padding is not touched
    CHECK_MSG(wrong == 0, "%d wrong bytes: rectangles copy exactly their pixels, at the stride given", wrong);
    dirty_all(&d);
    dirty_copy(&f, dst, stride, &d);
    wrong = 0;
    for (int y = 0; y < SCREEN_H; y++) wrong += memcmp(dst + (size_t)y * stride, f.px + (size_t)y * SCREEN_W * 4, SCREEN_W * 4) != 0;
    CHECK_MSG(wrong == 0, "a full copy is the whole frame (%d rows wrong)", wrong);
    wrong = 0;
    memset(dst, 0x11, stride * SCREEN_H);
    dirty_copy(&f, dst, (size_t)SCREEN_W * 4, &d); // and the packed case
    wrong = memcmp(dst, f.px, (size_t)SCREEN_W * SCREEN_H * 4) != 0;
    CHECK_MSG(wrong == 0, "a full copy with no padding is the whole frame");
    free(dst);
    free(f.px);
}

// what the history says to copy, by hand
static void test_history(void)
{
    // present t changes a 10 x 10 square at x = 100 t, y = 0
    Dirty now[12], copy;
    for (int t = 0; t < 12; t++) {
        dirty_clear(&now[t]);
        dirty_add(&now[t], 100 * t, 0, 10, 10);
    }
    dirty_all(&now[4]); // present 4 is a whole-frame present
    FbSim f;
    fbsim_init(&f, 3);
    for (int t = 0; t < 12; t++) {
        CHECK_MSG(fbsim_slot(&f) == t % 3, "present %d goes to image %d (not %d)", t, t % 3, fbsim_slot(&f));
        fbsim_present(&f, &now[t], &copy);
        int full = t < 3 || (t >= 4 && t <= 6); // the first three, and the three that include present 4
        CHECK_MSG(copy.full == full, "present %d: full = %d, expected %d", t, copy.full, full);
        if (!full) {
            CHECK_MSG(copy.n == 3 && dirty_area(&copy) == 300, "present %d copies three squares (n = %d, area %ld)", t, copy.n, dirty_area(&copy));
            for (int k = 0; k < 3; k++) CHECK(dirty_has(&copy, 100 * (t - k) + 5, 5));
            CHECK(!dirty_has(&copy, 100 * (t - 3) + 5, 5));
        }
    }
    fbsim_init(&f, 1); // one image keeps what it was given: only this present's changes
    for (int t = 0; t < 4; t++) {
        fbsim_present(&f, &now[t], &copy);
        CHECK_MSG(copy.full == (t == 0), "one image, present %d: full = %d", t, copy.full);
        if (t > 0) CHECK(copy.n == 1 && same_rect(copy.r[0], 100 * t, 0, 10, 10));
    }
    fbsim_init(&f, 2);
    for (int t = 0; t < 5; t++) {
        fbsim_present(&f, &now[t], &copy);
        int full = t < 2 || t == 4 || t == 5; // present 4 is full
        CHECK_MSG(copy.full == full, "two images, present %d: full = %d", t, copy.full);
        if (t == 3) CHECK(copy.n == 2 && dirty_has(&copy, 205, 5) && dirty_has(&copy, 305, 5) && !dirty_has(&copy, 105, 5));
    }
    // a present that changed nothing still goes in the history (as an empty list)
    fbsim_init(&f, 3);
    Dirty none;
    dirty_clear(&none);
    for (int t = 0; t < 6; t++) fbsim_present(&f, &none, &copy);
    CHECK_MSG(!copy.full && copy.n == 0, "nothing changed for a while: nothing to copy");
}

// ---- the random walk

enum { MODE_RIGHT, MODE_NAIVE, MODE_LIE };

typedef struct {
    long mismatches;  // pixels where the shown image differed from the frame, over the whole run
    long bad_steps;   // presents after which the image was not the frame
    long violations;  // lie detector total
    int lies, caught; // MODE_LIE: presents that changed a pixel outside every claimed rectangle, and how many the detector flagged
    int full, rects;
    long copied;
} SimResult;

// Frames change only inside rectangles (sometimes claimed a little bigger than they changed), or wholly; each is presented to a
// pretend framebuffer of `images` images (used round-robin, rows padded) through the same code the console uses.
static SimResult run_sim(int images, int steps, int mode, uint32_t seed)
{
    SimResult res;
    memset(&res, 0, sizeof res);
    g_rng = seed;
    size_t stride = HP_STRIDE_FOR_TEST, fsize = (size_t)SCREEN_W * SCREEN_H * 4;
    Surf frame = {SCREEN_W, SCREEN_H, malloc(fsize)}, prev = {SCREEN_W, SCREEN_H, malloc(fsize)};
    uint8_t *img[3];
    for (int i = 0; i < 3; i++) {
        img[i] = malloc(stride * SCREEN_H);
        memset(img[i], 0x5A, stride * SCREEN_H);
    }
    memset(frame.px, 0, fsize);
    memset(prev.px, 0, fsize);
    FbSim fs;
    fbsim_init(&fs, images);
    for (int step = 0; step < steps; step++) {
        Rect claim[40];
        int nclaim = 0, whole = step == 0 || rnd() % 100 < 6;
        Dirty now;
        dirty_clear(&now);
        if (whole) {
            uint32_t *p = (uint32_t *)frame.px;
            for (size_t i = 0; i < fsize / 4; i++) p[i] = rnd();
            dirty_all(&now);
        } else {
            nclaim = rnd() % 10 == 0 ? rr(10, 40) : rr(0, 6);
            for (int i = 0; i < nclaim; i++) {
                int big = rnd() % 10 == 0;
                Rect r = {rr(-60, SCREEN_W + 20), rr(-60, SCREEN_H + 20), rr(-4, big ? 1400 : 260), rr(-4, big ? 800 : 160)};
                for (int y = r.y < 0 ? 0 : r.y; y < r.y + r.h && y < SCREEN_H; y++)
                    for (int x = r.x < 0 ? 0 : r.x; x < r.x + r.w && x < SCREEN_W; x++) {
                        uint32_t *px = (uint32_t *)frame.px + (size_t)y * SCREEN_W + x, v = rnd();
                        *px = v == *px ? v ^ 1u : v;
                    }
                if (rnd() % 3 == 0) { // claim more than changed: allowed
                    int m = rr(1, 30);
                    r.x -= m;
                    r.y -= m;
                    r.w += 2 * m;
                    r.h += 2 * m;
                }
                claim[i] = r;
                dirty_add_rect(&now, r);
            }
            if (mode == MODE_LIE && rnd() % 2) { // one pixel changes where nothing was claimed
                for (int tries = 0; tries < 50; tries++) {
                    int x = rr(0, SCREEN_W - 1), y = rr(0, SCREEN_H - 1), in = 0;
                    for (int i = 0; i < nclaim; i++) in |= x >= claim[i].x && y >= claim[i].y && x - claim[i].x < claim[i].w && y - claim[i].y < claim[i].h;
                    if (in) continue;
                    uint32_t *px = (uint32_t *)frame.px + (size_t)y * SCREEN_W + x;
                    *px ^= 0x00FF00FFu;
                    res.lies++;
                    break;
                }
            }
            long v = dirty_violations(&prev, &frame, claim, nclaim);
            res.violations += v;
            if (mode == MODE_LIE && v > 0) res.caught++;
            if (mode != MODE_LIE && v) printf("  step %d: the lie detector flagged %ld pixels of an honest present\n", step, v);
        }
        Dirty copy;
        int slot = fbsim_slot(&fs);
        fbsim_present(&fs, &now, &copy);
        if (!list_ok(&now) || !list_ok(&copy)) res.bad_steps += 1000000; // (broke its promises: loud)
        dirty_copy(&frame, img[slot], stride, mode == MODE_NAIVE ? &now : &copy); // naive: only what this present changed
        res.copied += dirty_area(mode == MODE_NAIVE ? &now : &copy);
        long bad = 0;
        for (int y = 0; y < SCREEN_H; y++) {
            const uint8_t *a = frame.px + (size_t)y * SCREEN_W * 4, *b = img[slot] + (size_t)y * stride;
            if (!memcmp(a, b, SCREEN_W * 4)) continue;
            for (int x = 0; x < SCREEN_W; x++) bad += memcmp(a + x * 4, b + x * 4, 4) != 0;
        }
        res.mismatches += bad;
        res.bad_steps += bad > 0;
        whole ? res.full++ : res.rects++;
        memcpy(prev.px, frame.px, fsize);
    }
    for (int i = 0; i < 3; i++) free(img[i]);
    free(frame.px);
    free(prev.px);
    return res;
}

static void test_random(void)
{
    for (int images = 1; images <= 3; images++) {
        SimResult r = run_sim(images, 800, MODE_RIGHT, 0xC0FFEEu + (uint32_t)images);
        CHECK_MSG(r.bad_steps == 0 && r.mismatches == 0, "%d image(s): %ld presents showed something other than the frame (%ld pixels)", images, r.bad_steps, r.mismatches);
        CHECK_MSG(r.violations == 0, "%d image(s): the lie detector flagged honest presents (%ld pixels)", images, r.violations);
        CHECK_MSG(r.full > 20 && r.rects > 600, "%d image(s): a mix of presents (%d whole, %d by rectangles)", images, r.full, r.rects);
        long whole = (long)SCREEN_W * SCREEN_H * (r.full + r.rects);
        CHECK_MSG(r.copied < whole, "%d image(s): rectangles save copying (%ld of %ld pixels)", images, r.copied, whole);
        printf("  %d image(s): %d whole + %d rectangle presents, every one exact; copied %.0f%% of what whole presents would\n", images, r.full, r.rects, 100.0 * r.copied / whole);
    }
    // a copy of only the present's own rectangles is right for one image and wrong for two or three: the test can see the difference
    SimResult n1 = run_sim(1, 300, MODE_NAIVE, 77), n3 = run_sim(3, 300, MODE_NAIVE, 77), n2 = run_sim(2, 300, MODE_NAIVE, 77);
    CHECK_MSG(n1.mismatches == 0, "one image needs only the present's own rectangles (%ld pixels wrong)", n1.mismatches);
    CHECK_MSG(n2.mismatches > 0 && n3.mismatches > 0, "a naive copy is caught on 2 and 3 images (%ld, %ld pixels)", n2.mismatches, n3.mismatches);
    // a caller that changes pixels it did not claim is caught every time, and the picture then really is wrong
    SimResult l = run_sim(3, 300, MODE_LIE, 99);
    CHECK_MSG(l.lies > 50 && l.caught == l.lies, "the lie detector caught %d of %d lying presents", l.caught, l.lies);
    CHECK_MSG(l.mismatches > 0, "and the unclaimed pixels did not reach the screen (%ld pixels wrong)", l.mismatches);
}

static void test_detector(void)
{
    size_t fsize = (size_t)SCREEN_W * SCREEN_H * 4;
    Surf a = {SCREEN_W, SCREEN_H, calloc(1, fsize)}, b = {SCREEN_W, SCREEN_H, calloc(1, fsize)};
    Rect r[2] = {{10, 10, 20, 20}, {100, 100, 5, 5}};
    CHECK(dirty_violations(&a, &b, NULL, 0) == 0);
    for (int y = 10; y < 30; y++) memset(b.px + ((size_t)y * SCREEN_W + 10) * 4, 7, 20 * 4);
    CHECK_MSG(dirty_violations(&a, &b, r, 2) == 0, "changes inside a claimed rectangle are fine");
    CHECK_MSG(dirty_violations(&a, &b, r + 1, 1) == 400, "the same change claimed somewhere else is 400 pixels of lie");
    b.px[((size_t)50 * SCREEN_W + 50) * 4 + 2] = 1; // one channel of one pixel
    CHECK_MSG(dirty_violations(&a, &b, r, 2) == 1, "one changed channel outside is one pixel");
    b.px[((size_t)719 * SCREEN_W + 1279) * 4] = 1;
    CHECK_MSG(dirty_violations(&a, &b, r, 2) == 2, "the last pixel of the screen is checked too");
    free(a.px);
    free(b.px);
}

int main(void)
{
    test_merge();
    test_cap();
    test_full_flag();
    test_copy();
    test_history();
    test_detector();
    test_random();
    printf("dirty: %d checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
