// "Still working": when the app has been busy between two screens for a while (reading the slow SD card, baking a page's pictures), a crayon starts drawing little
// symbols straight onto the paper at the left of the footer strip, one after the other. The moment the work is over a rubber scribbles them out (missing bits,
// going back for them) and the next screen shows. That is all: no card, no tape. Made of the same pieces as the rest of the look (paper.h): wax crayon.
//
// How it is driven. The app has one thread, so the mark can only move when the work gives it a moment. ui_screen_done() starts the clock (ui_wait_begin);
// the long loops (a sprite or surface being made, a folder walked, a filter at work) call gfx_pump / fsx_pump, which land in pump() here. Once the work has
// gone on for g_ui_wait_after_ms the first symbol begins, and every later pump draws the next frame of the drawing on a private copy of the screen that is
// showing, and presents that. The app's own frame (G.work) is never drawn on, so a screen that is shown while the drawing is there (a whole frame takes it
// off the display) can never carry a piece of it along. The next screen's first frame (ui_transition), a dialog (ui_dialog_begin), or waiting for input (ui_poll) start
// the rubbing out at once, and it does not hold anything up: the ink is laid over each frame that is shown while it lasts (the change to the next screen, the next
// screen's own frames, the frames of a slow job's pumps, the waits for a button), so the next screen can be used while the last of the ink is still being scribbled away.
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "fsx.h"
#include "gfx/paper.h"
#include "ui_internal.h"

#define PIF 3.14159265f

int g_ui_wait_after_ms = 350; // how long the app is busy before the drawing starts (tests set it very high, or low)
int g_ui_wait_count = 5;      // how many symbols the bar may hold (1..5): symbols are added until it is full
long g_ui_wait_shown;         // tests: how many times it has started
long g_ui_wait_frames;        // tests: frames drawn

// where the drawing goes: the left of the footer strip, a bar with room for five symbols in a row (a symbol has a place CARD_W x CARD_H, the drawing code's old name for it);
// the whole bar is one ink plane of PLANE_W x CARD_H
enum { CARD_W = 96, CARD_H = 44, NSLOT = 5, SLOT_STEP = 88, SLOT_X0 = 106, SLOT_Y = 662, PLANE_W = (NSLOT - 1) * SLOT_STEP + CARD_W };
static const Rect AREA = {SLOT_X0 - 2, SLOT_Y - 2, PLANE_W + 4, CARD_H + 4}; // everything the mark touches: saved, redrawn and shown each frame
#define SYM_K 0.62f // a symbol's 120 x 56 box, in pixels

// the timeline, in ms
#define T_DRAW_SPEED 360.0f  // (units a second of the symbol's own 120 x 56 box)
#define GAP_MIN 40.0f        // from one symbol being drawn to the next beginning: a short, uneven pause, GAP_MIN to GAP_MIN + GAP_VAR
#define GAP_VAR 80.0f
#define ERASE_K 2.4f         // how much quicker than the slow rubbing-out the erasing at the end goes
#define ERASE_MAX_MS 600.0f  // and it never takes longer than this, however much is drawn

static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float ease(float x) { x = clampf(x, 0, 1); return x * x * (3 - 2 * x); }

// ------------------------------------------------------------------------------------------------ the symbols
// Strokes in a 120 x 56 box, in a small subset of SVG path syntax (M L H V Q C Z, upper or lower case) plus O (circle) and E (ellipse).
typedef struct { uint32_t rgb; int n; const char *s[6]; } Sym;
#define NSYM 10
static const Sym SYMS[NSYM] = {
    {0x2F3A8F, 3, {"O14 20.2 2.2", "O14 42.2 2.2", "M40 10Q62 28 40 48"}},
    {0xE0527A, 1, {"M60 50C14 26 36 -2 60 18C84 -2 106 26 60 50"}},
    {0x2FA88E, 5, {"M18 8Q4 28 18 50", "M34 30l8 -10l8 10", "M54 42h14", "M72 30l8 -10l8 10", "M104 8Q118 28 104 50"}},
    {0xE0A800, 1, {"M60 4L70 24L93 27L76 42L81 54L60 46L39 54L44 42L27 27L50 24Z"}},
    {0x7A5CC4, 3, {"O14 20.2 2.2", "O14 42.2 2.2", "M44 50L44 8Q74 4 72 20Q70 32 44 30"}}, // :P (a tail on the bowl made it an R)
    {0x2F3A8F, 4, {"M44 6L44 34", "O44 46 1.3", "M74 6L74 34", "O74 46 1.3"}},
    {0xE0527A, 5, {"M12 8Q-2 28 12 50", "M30 18l14 9l-14 9", "M54 44h14", "M92 18l-14 9l14 9", "M108 8Q122 28 108 50"}},
    {0x2FA88E, 2, {"E61 46 7 5.5", "M61 44L61 8Q78 12 74 28"}},
    {0x7A5CC4, 3, {"M10 18C10 46 34 46 34 18", "M45 20C45 46 59 46 59 25C59 46 73 46 73 20", "M86 18C86 46 110 46 110 18"}}, // uwu (wide round bowls: narrow ones read as v w v in crayon)
    {0xE0A800, 5, {"M60 4Q64 26 86 28Q64 30 60 52Q56 30 34 28Q56 26 60 4", "M96 8l0 10", "M91 13l10 0", "M22 38l0 8", "M18 42l8 0"}},
};

typedef struct { float *x, *y, *s; int n, cap; float len; } Poly;

static void poly_add(Poly *p, float x, float y)
{
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 64;
        p->x = realloc(p->x, (size_t)p->cap * sizeof(float));
        p->y = realloc(p->y, (size_t)p->cap * sizeof(float));
        p->s = realloc(p->s, (size_t)p->cap * sizeof(float));
    }
    p->s[p->n] = p->n ? p->s[p->n - 1] + hypotf(x - p->x[p->n - 1], y - p->y[p->n - 1]) : 0.0f;
    p->x[p->n] = x;
    p->y[p->n] = y;
    p->len = p->s[p->n];
    p->n++;
}

static void poly_free(Poly *p)
{
    free(p->x);
    free(p->y);
    free(p->s);
    memset(p, 0, sizeof *p);
}

static void poly_at(const Poly *p, float s, float *x, float *y)
{
    if (p->n < 2 || s <= 0) {
        *x = p->x[0];
        *y = p->y[0];
        return;
    }
    if (s >= p->len) {
        *x = p->x[p->n - 1];
        *y = p->y[p->n - 1];
        return;
    }
    int lo = 0, hi = p->n - 1;
    while (hi - lo > 1) {
        int m = (lo + hi) / 2;
        if (p->s[m] <= s) lo = m;
        else hi = m;
    }
    float seg = p->s[hi] - p->s[lo], t = seg > 1e-6f ? (s - p->s[lo]) / seg : 0.0f;
    *x = p->x[lo] + (p->x[hi] - p->x[lo]) * t;
    *y = p->y[lo] + (p->y[hi] - p->y[lo]) * t;
}

static float num(const char **pp)
{
    const char *c = *pp;
    while (*c == ' ' || *c == ',') c++;
    char *end;
    float v = strtof(c, &end);
    *pp = end;
    return v;
}

static void flatten(const char *d, Poly *p)
{
    memset(p, 0, sizeof *p);
    float cx = 0, cy = 0, sx = 0, sy = 0;
    const char *c = d;
    while (*c) {
        char cmd = *c++;
        if (cmd == ' ' || cmd == ',') continue;
        if (cmd == 'O' || cmd == 'E') {
            float ox = num(&c), oy = num(&c), rx = num(&c), ry = cmd == 'E' ? num(&c) : rx;
            for (int i = 0; i <= 16; i++) {
                float a = i * 2.0f * PIF / 16.0f;
                poly_add(p, ox + cosf(a) * rx, oy + sinf(a) * ry);
            }
            cx = ox + rx;
            cy = oy;
            continue;
        }
        int rel = cmd >= 'a';
        switch (cmd | 0x20) {
        case 'm': {
            float x = num(&c), y = num(&c);
            if (rel) { x += cx; y += cy; }
            cx = sx = x;
            cy = sy = y;
            poly_add(p, cx, cy);
            break;
        }
        case 'l': {
            float x = num(&c), y = num(&c);
            if (rel) { x += cx; y += cy; }
            poly_add(p, x, y);
            cx = x;
            cy = y;
            break;
        }
        case 'h': {
            float x = num(&c);
            if (rel) x += cx;
            poly_add(p, x, cy);
            cx = x;
            break;
        }
        case 'v': {
            float y = num(&c);
            if (rel) y += cy;
            poly_add(p, cx, y);
            cy = y;
            break;
        }
        case 'q': {
            float qx = num(&c), qy = num(&c), x = num(&c), y = num(&c);
            if (rel) { qx += cx; qy += cy; x += cx; y += cy; }
            for (int i = 1; i <= 20; i++) {
                float t = i / 20.0f, m = 1 - t;
                poly_add(p, m * m * cx + 2 * m * t * qx + t * t * x, m * m * cy + 2 * m * t * qy + t * t * y);
            }
            cx = x;
            cy = y;
            break;
        }
        case 'c': {
            float q1x = num(&c), q1y = num(&c), q2x = num(&c), q2y = num(&c), x = num(&c), y = num(&c);
            if (rel) { q1x += cx; q1y += cy; q2x += cx; q2y += cy; x += cx; y += cy; }
            for (int i = 1; i <= 28; i++) {
                float t = i / 28.0f, m = 1 - t;
                poly_add(p, m * m * m * cx + 3 * m * m * t * q1x + 3 * m * t * t * q2x + t * t * t * x, m * m * m * cy + 3 * m * m * t * q1y + 3 * m * t * t * q2y + t * t * t * y);
            }
            cx = x;
            cy = y;
            break;
        }
        case 'z':
            poly_add(p, sx, sy);
            cx = sx;
            cy = sy;
            break;
        default: break;
        }
    }
}

// ------------------------------------------------------------------------------------------------ a doodle, drawn by "hand"
typedef struct { float x, y, s, t, v; } DPt; // a point of a stroke: where, how far along, the share of the stroke's time at which the pen is there, how fast the pen is
typedef struct {
    DPt *p;
    int n;
    float len, gain, lw;
    float t0, dur;  // when it is drawn, ms into the doodle
    float last_f;   // how much of it is on the paper already
} DStroke;
typedef struct { float x, y, t, w; } EPt; // a point of the rubber's track: where, when (ms into the erasing), how wide it is pressed
typedef struct { float x, y; } V2;
typedef struct {
    int sym, nst;
    DStroke st[6];
    float tx, ty, rot, scale;
    float draw, hold, erase_dur; // ms: the drawing, the look at it, the rubbing out
    EPt *esc;
    int nesc;
    double t0;                   // when it begins, ms since the scrap came down
    float erase_last;            // how far the erasing has gone on the paper
    float er_from, er_k;         // from here on the rubbing is er_k times quicker (the work finished)
    int er_set;
    float draw_cap;              // the pen stops here (the work finished while it was drawing)
    float xoff;                  // where its place is along the bar
} Doodle;

static void doodle_free(Doodle *d)
{
    for (int i = 0; i < d->nst; i++) free(d->st[i].p);
    free(d->esc);
    memset(d, 0, sizeof *d);
}

static float frand(PaperRng *r) { return paper_rng_f(r); }

// ---- the rubber. Nobody wipes a doodle out in even rows: a first scribble goes over most of it in quick back-and-forth zig-zags whose direction and reach keep
// changing, and leaves bits; the hand stops for a beat, sees what is left and goes back to scrub each bit out in its own small scribble, until nothing is
// left. Every point of the drawing is tested against the rubber's real track, so the gaps are real and so is the cleaning.
typedef struct { float x, y, v, pause; } SP; // a corner of the rubber's path: where, how fast it gets there (units a second), a stop before it (ms)
typedef struct { SP *a; int n, cap; } SPath;

static void sp_push(SPath *p, float x, float y, float v, float pause)
{
    if (p->n == p->cap) {
        p->cap = p->cap ? p->cap * 2 : 256;
        p->a = realloc(p->a, (size_t)p->cap * sizeof(SP));
    }
    p->a[p->n].x = x;
    p->a[p->n].y = y;
    p->a[p->n].v = v;
    p->a[p->n].pause = pause;
    p->n++;
}

static void sp_add(SPath *p, float x, float y, float v, float pause)
{
    if (pause > 0 && p->n) { // the hand stops where it is
        SP l = p->a[p->n - 1];
        sp_push(p, l.x, l.y, 1e9f, pause);
    }
    sp_push(p, x, y, v, 0);
}

static float seg_dist(float px, float py, float ax, float ay, float bx, float by)
{
    float dx = bx - ax, dy = by - ay, l2 = dx * dx + dy * dy, t = l2 > 0 ? clampf(((px - ax) * dx + (py - ay) * dy) / l2, 0, 1) : 0.0f;
    return hypotf(px - (ax + dx * t), py - (ay + dy * t));
}

static int uncovered(const V2 *D, int nD, const SPath *p, float thr, V2 *out)
{
    int n = 0;
    for (int i = 0; i < nD; i++) {
        float best = 1e9f;
        for (int j = 1; j < p->n; j++) {
            float d = seg_dist(D[i].x, D[i].y, p->a[j - 1].x, p->a[j - 1].y, p->a[j].x, p->a[j].y);
            if (d < best) best = d;
        }
        if (best > thr) {
            if (out) out[n] = D[i];
            n++;
        }
    }
    return n;
}

typedef struct { float cx, cy; V2 *pts; int n; } Cluster;

static void scribble_make(Doodle *d, const V2 *D, int nD, PaperRng *R)
{
    float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < nD; i++) {
        if (D[i].x < minx) minx = D[i].x;
        if (D[i].x > maxx) maxx = D[i].x;
        if (D[i].y < miny) miny = D[i].y;
        if (D[i].y > maxy) maxy = D[i].y;
    }
    float bw = fmaxf(10, maxx - minx), bh = fmaxf(10, maxy - miny), cxm = (minx + maxx) / 2, cym = (miny + maxy) / 2, THR = 2.3f;
    SPath P = {0};
    // the first scribble: a few cells of the drawing in a random order, each reached by a zig-zag, the whole thing shifted a little off the drawing
    int cols = bw > 60 ? (int)ceilf(bw / 60.0f) : 2, rows = bh > 24 ? 2 : 1, nc;
    if (cols > 15) cols = 15;
    nc = cols * rows;
    V2 cells[32];
    int sweep = nc > 4; // (a wide drawing is gone over from one end to the other, down one column and up the next)
    int k = 0;
    for (int c = 0; c < cols; c++)
        for (int r = 0; r < rows; r++) {
            cells[k].x = minx + (c + 0.5f) * bw / cols;
            cells[k].y = miny + ((sweep && (c & 1) ? rows - 1 - r : r) + 0.5f) * bh / rows;
            k++;
        }
    for (int c = sweep ? 0 : nc - 1; c > 0; c--) {
        int j = (int)(frand(R) * (c + 1));
        V2 t = cells[c];
        cells[c] = cells[j];
        cells[j] = t;
    }
    float sx = (frand(R) - 0.5f) * bw * 0.3f, sy = (frand(R) - 0.5f) * bh * 0.4f;
    int nT = sweep ? nc : nc < 3 + (int)(frand(R) * 2) ? nc : 3 + (int)(frand(R) * 2);
    V2 cur = {cxm + (frand(R) - 0.5f) * bw * 0.8f, cym + (frand(R) - 0.5f) * bh * 0.9f};
    sp_add(&P, cur.x, cur.y, 1300, 0);
    for (int t = 0; t < nT; t++) {
        V2 to = {cells[t].x + sx + (frand(R) - 0.5f) * bw / cols * 0.5f, cells[t].y + sy + (frand(R) - 0.5f) * bh / rows * 0.5f};
        float v = 1050 + frand(R) * 450;
        float dx = to.x - cur.x, dy = to.y - cur.y, Ld = hypotf(dx, dy);
        if (Ld < 1e-3f) Ld = 1;
        float ux = dx / Ld, uy = dy / Ld, nx = -uy, ny = ux;
        int m = (int)fmaxf(2, floorf(Ld / (7 + frand(R) * 5) + 0.5f));
        float amp0 = 3.5f + frand(R) * 5.5f;
        for (int i = 1; i <= 2 * m; i++) { // quick back-and-forth across the line of travel
            float tt = (float)i / (2 * m), along = Ld * tt + (frand(R) - 0.5f) * Ld / (2 * m) * 0.6f, side = (i % 2 ? 1 : -1) * amp0 * (0.55f + frand(R) * 0.9f);
            sp_add(&P, cur.x + ux * along + nx * side, cur.y + uy * along + ny * side, v, 0);
        }
        sp_add(&P, to.x, to.y, v, 0);
        cur = to;
    }
    V2 *U = malloc((size_t)(nD ? nD : 1) * sizeof(V2));
    int nU = U ? uncovered(D, nD, &P, THR, U) : 0;
    // then it goes back for what is left, a bit at a time, nearest first
    for (int rd = 0; U && rd < 3 && nU > 0; rd++) {
        Cluster cl[16];
        int ncl = 0;
        for (int i = 0; i < nU; i++) { // group the bits that are near each other
            int best = -1;
            float bd = 1e9f;
            for (int c = 0; c < ncl; c++) {
                float dd = hypotf(U[i].x - cl[c].cx, U[i].y - cl[c].cy);
                if (dd < bd) { bd = dd; best = c; }
            }
            if (best >= 0 && bd < 11) {
                cl[best].n++;
                cl[best].cx += (U[i].x - cl[best].cx) / cl[best].n;
                cl[best].cy += (U[i].y - cl[best].cy) / cl[best].n;
            } else if (ncl < 16) {
                cl[ncl].cx = U[i].x;
                cl[ncl].cy = U[i].y;
                cl[ncl].n = 1;
                cl[ncl].pts = NULL;
                ncl++;
            } else {
                // (more bits than clusters: they join the nearest; the dabs below catch anything that is still showing)
                cl[best < 0 ? 0 : best].n++;
            }
        }
        int first = 1, left = ncl;
        char taken[16] = {0};
        while (left > 0) {
            SP pen = P.a[P.n - 1];
            int bi = -1;
            float bdist = 1e9f;
            for (int c = 0; c < ncl; c++) {
                if (taken[c]) continue;
                float dd = hypotf(cl[c].cx - pen.x, cl[c].cy - pen.y);
                if (dd < bdist) { bdist = dd; bi = c; }
            }
            taken[bi] = 1;
            left--;
            // the extent of what is left near this cluster
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f, mx = 0, my = 0;
            int cnt = 0;
            for (int i = 0; i < nU; i++) {
                if (hypotf(U[i].x - cl[bi].cx, U[i].y - cl[bi].cy) > 14) continue;
                if (U[i].x < x0) x0 = U[i].x;
                if (U[i].x > x1) x1 = U[i].x;
                if (U[i].y < y0) y0 = U[i].y;
                if (U[i].y > y1) y1 = U[i].y;
                mx += U[i].x;
                my += U[i].y;
                cnt++;
            }
            if (!cnt) continue;
            mx /= cnt;
            my /= cnt;
            float w = x1 - x0, h = y1 - y0, len = w >= h ? w : h, perp = w >= h ? h : w;
            V2 A = w >= h ? (V2){x0 - 3, my} : (V2){mx, y0 - 3}, B = w >= h ? (V2){x1 + 3, my} : (V2){mx, y1 + 3};
            if (hypotf(pen.x - A.x, pen.y - A.y) > hypotf(pen.x - B.x, pen.y - B.y)) { V2 t = A; A = B; B = t; }
            int m = (int)fmaxf(2, ceilf((len + 6) / 7));
            float amp = perp / 2 + 4.2f + frand(R) * 2.2f, ddx = B.x - A.x, ddy = B.y - A.y, Lx = hypotf(ddx, ddy);
            if (Lx < 1e-3f) Lx = 1;
            float ux = ddx / Lx, uy = ddy / Lx, v = 1100 + frand(R) * 400;
            sp_add(&P, A.x, A.y, v, first ? (rd == 0 ? 130.0f : 70.0f) : 35.0f);
            for (int i = 1; i <= 2 * m; i++) {
                float tt = (float)i / (2 * m), sd = (i % 2 ? 1 : -1) * amp * (0.85f + frand(R) * 0.3f);
                sp_add(&P, A.x + ddx * tt - uy * sd, A.y + ddy * tt + ux * sd, v, 0);
            }
            sp_add(&P, B.x, B.y, v, 0);
            first = 0;
        }
        nU = uncovered(D, nD, &P, THR, U);
    }
    if (U && nU > 0) // anything still showing gets a quick dab
        for (int i = 0; i < nU; i++)
            for (int a = 0; a <= 6; a++) sp_add(&P, U[i].x + cosf(a * 1.05f) * 1.6f, U[i].y + sinf(a * 1.05f) * 1.6f, 900, a == 0 && i == 0 ? 60.0f : 0.0f);
    free(U);
    // the track in time
    d->esc = malloc((size_t)(P.n ? P.n : 1) * sizeof(EPt));
    d->nesc = d->esc ? P.n : 0;
    float t = 0, ph = frand(R) * 6.28f;
    for (int i = 0; i < d->nesc; i++) {
        if (i) t += hypotf(P.a[i].x - P.a[i - 1].x, P.a[i].y - P.a[i - 1].y) / P.a[i].v * 1000.0f;
        t += P.a[i].pause;
        d->esc[i].x = P.a[i].x;
        d->esc[i].y = P.a[i].y;
        d->esc[i].t = t;
        d->esc[i].w = 12.5f + 4.0f * (0.5f + 0.5f * sinf(i * 1.3f + ph)); // the rubber's width changes with how hard it is pressed
    }
    if (t > 1150) { // a long scribble is a quicker one
        float f = fmaxf(0.6f, 1150.0f / t);
        for (int i = 0; i < d->nesc; i++) d->esc[i].t *= f;
        t *= f;
    }
    d->erase_dur = t + 40;
    free(P.a);
}

// ---- the crayon. Every symbol is redrawn by hand each time, the way a quick, sloppy person does it: each stroke bows a little as one smooth arc (no tremor),
// starts and stops a bit off, closed shapes overshoot their start, fast strokes flick off at the end, the pen goes slower in tight turns and faster on straights,
// presses harder where it is slow and at the start and lifts off at the end, each stroke has its own pressure, and the whole symbol sits a little crooked.
static void doodle_make(Doodle *d, int sym, uint32_t seed, double t0)
{
    memset(d, 0, sizeof *d);
    d->sym = sym;
    d->t0 = t0;
    PaperRng R;
    paper_rng_seed(&R, seed);
    d->tx = (frand(&R) - 0.5f) * 14;
    d->ty = (frand(&R) - 0.5f) * 6;
    d->rot = (frand(&R) - 0.5f) * 0.16f;
    d->scale = 0.92f + frand(&R) * 0.16f;
    const Sym *S = &SYMS[sym];
    V2 *D = malloc(512 * sizeof(V2));
    int nD = 0, capD = 512;
    for (int si = 0; si < S->n && D; si++) {
        Poly pl;
        flatten(S->s[si], &pl);
        if (pl.n < 2) {
            poly_free(&pl);
            continue;
        }
        float L = fmaxf(2, pl.len);
        int closed = L > 30 && hypotf(pl.x[0] - pl.x[pl.n - 1], pl.y[0] - pl.y[pl.n - 1]) < 3.5f;
        int n = (int)fmaxf(4, floorf(L / 2 + 0.5f)), tiny = L < 8;
        float bow = tiny ? 0 : (frand(&R) - 0.5f) * 0.11f * L, bow2 = tiny ? 0 : (frand(&R) - 0.5f) * 0.045f * L, ph = frand(&R) * 6.28f;
        float wx0 = (frand(&R) - 0.5f) * 2.4f, wy0 = (frand(&R) - 0.5f) * 2.4f, wx1 = (frand(&R) - 0.5f) * 3.2f, wy1 = (frand(&R) - 0.5f) * 3.2f;
        float over;
        if (closed) over = 0.05f + frand(&R) * 0.11f; // overshoot (closed shapes always), or stopping short
        else if (frand(&R) < 0.35f) over = frand(&R) * 0.08f;
        else over = -(frand(&R) < 0.4f ? frand(&R) * 0.09f : 0.0f);
        float span = L * (1 + over), uc = 1.0f / (1 + over);
        float rs = (frand(&R) - 0.5f) * 0.1f, ss = 0.95f + frand(&R) * 0.1f, gain = 0.82f + frand(&R) * 0.18f, lw = 0.92f + frand(&R) * 0.18f;
        DPt *pts = malloc((size_t)(n + 5) * sizeof *pts);
        if (!pts) {
            poly_free(&pl);
            break;
        }
        int np = 0;
        float cx = 0, cy = 0;
        for (int i = 0; i <= n; i++) {
            float u = (float)i / n, s = span * u;
            float sp = closed ? fmodf(s, L) : clampf(s, 0, L), sq = closed ? fmodf(s + 0.8f, L) : clampf(s + 0.8f, 0, L);
            float ax, ay, bx, by;
            poly_at(&pl, sp, &ax, &ay);
            poly_at(&pl, sq, &bx, &by);
            float dx = bx - ax, dy = by - ay, dl = hypotf(dx, dy), nx = 0, ny = 0;
            if (dl >= 0.01f) {
                nx = -dy / dl;
                ny = dx / dl;
            }
            float disp = bow * sinf(PIF * u) + bow2 * sinf(2 * PIF * u + ph);
            if (closed && u > uc) disp += 3.2f * (u - uc) / (1 - uc); // the overshoot drifts off the line it is retracing
            float x = ax + nx * disp + wx0 + (wx1 - wx0) * u, y = ay + ny * disp + wy0 + (wy1 - wy0) * u;
            pts[np].x = x;
            pts[np].y = y;
            np++;
            cx += x;
            cy += y;
        }
        if (!closed && !tiny && L > 14 && frand(&R) < 0.8f) { // the flick as the pen leaves the paper
            float ax = pts[np - 2].x, ay = pts[np - 2].y, bx = pts[np - 1].x, by = pts[np - 1].y;
            float dir = atan2f(by - ay, bx - ax), curl = (frand(&R) < 0.5f ? -1.0f : 1.0f) * (0.25f + frand(&R) * 0.35f), tl = 2.5f + frand(&R) * 3, px = bx, py = by;
            for (int kk = 1; kk <= 3; kk++) {
                dir += curl * 0.5f;
                px += cosf(dir) * tl / 3;
                py += sinf(dir) * tl / 3;
                pts[np].x = px;
                pts[np].y = py;
                np++;
                cx += px;
                cy += py;
            }
        }
        cx /= np; // this stroke is turned and sized a touch about its own middle
        cy /= np;
        float cr = cosf(rs), sr = sinf(rs);
        for (int i = 0; i < np; i++) {
            float ax = (pts[i].x - cx) * ss, ay = (pts[i].y - cy) * ss;
            pts[i].x = cx + ax * cr - ay * sr;
            pts[i].y = cy + ax * sr + ay * cr;
        }
        float acc = 0;
        pts[0].s = 0;
        for (int i = 1; i < np; i++) {
            acc += hypotf(pts[i].x - pts[i - 1].x, pts[i].y - pts[i - 1].y);
            pts[i].s = acc;
        }
        float tt = 0;
        pts[0].t = 0;
        pts[0].v = 1;
        for (int i = 1; i < np; i++) { // how sharply the path turns here (smoothed): slower in tight turns
            int ia = i - 2 < 0 ? 0 : i - 2, ic = i + 2 > np - 1 ? np - 1 : i + 2;
            float a1 = atan2f(pts[i].y - pts[ia].y, pts[i].x - pts[ia].x), a2 = atan2f(pts[ic].y - pts[i].y, pts[ic].x - pts[i].x), da = fabsf(a2 - a1);
            if (da > PIF) da = 2 * PIF - da;
            float v = 1.0f / (1 + 5 * da);
            tt += (pts[i].s - pts[i - 1].s) / v;
            pts[i].t = tt;
            pts[i].v = v;
        }
        if (np > 1) pts[0].v = pts[1].v;
        for (int i = 0; i < np; i++) {
            pts[i].t = tt > 0 ? pts[i].t / tt : 0.0f;
            if (nD == capD) {
                capD *= 2;
                V2 *nd = realloc(D, (size_t)capD * sizeof(V2));
                if (!nd) break;
                D = nd;
            }
            D[nD].x = pts[i].x;
            D[nD].y = pts[i].y;
            nD++;
        }
        DStroke *st = &d->st[d->nst++];
        st->p = pts;
        st->n = np;
        st->len = fmaxf(acc, 1.5f);
        st->gain = gain;
        st->lw = lw;
        poly_free(&pl);
    }
    // when each stroke is drawn: a beat before the first, strokes quicker or slower, uneven gaps between them, a moment to look at it, then the rubber
    float d0 = 60 + frand(&R) * 150;
    for (int i = 0; i < d->nst; i++) {
        float dur = (d->st[i].len / T_DRAW_SPEED * 1000.0f + 45) * (0.8f + frand(&R) * 0.45f);
        d->st[i].t0 = d0;
        d->st[i].dur = dur;
        d->draw = d0 + dur;
        d0 += dur + 35 + frand(&R) * 135;
    }
    d->hold = 340 + frand(&R) * 320;
    if (D && nD > 0) { // a hand that ran out of room would have drawn it a little over: keep the whole drawing, and the width of the crayon, on the card
        float k = SYM_K * d->scale, cr = cosf(d->rot), sr = sinf(d->rot), x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
        for (int i = 0; i < nD; i++) {
            float X = k * ((D[i].x - 60) * cr - (D[i].y - 28) * sr), Y = k * ((D[i].x - 60) * sr + (D[i].y - 28) * cr);
            x0 = fminf(x0, X);
            x1 = fmaxf(x1, X);
            y0 = fminf(y0, Y);
            y1 = fmaxf(y1, Y);
        }
        const float MX = 4.0f, MY = 3.5f;
        float lo = MX - x0 - CARD_W * 0.5f, hi = CARD_W - MX - x1 - CARD_W * 0.5f;
        d->tx = lo > hi ? (lo + hi) * 0.5f : clampf(d->tx, lo, hi);
        lo = MY - y0 - CARD_H * 0.5f - 1;
        hi = CARD_H - MY - y1 - CARD_H * 0.5f - 1;
        d->ty = lo > hi ? (lo + hi) * 0.5f : clampf(d->ty, lo, hi);
    }
    free(D);
}

// ------------------------------------------------------------------------------------------------ drawing a doodle into an ink plane
// The plane is one byte of ink per pixel of the card (the colour is the symbol's); the wax texture is laid over it when it is turned into a sprite.
static void ink_seg(uint8_t *A, float x0, float y0, float x1, float y1, float w, float alpha)
{
    int bx0 = (int)floorf(fminf(x0, x1) - w * 0.5f - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + w * 0.5f + 1);
    int by0 = (int)floorf(fminf(y0, y1) - w * 0.5f - 1), by1 = (int)ceilf(fmaxf(y0, y1) + w * 0.5f + 1);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > PLANE_W) bx1 = PLANE_W;
    if (by1 > CARD_H) by1 = CARD_H;
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) {
            float cov = clampf(w * 0.5f + 0.5f - seg_dist(x + 0.5f, y + 0.5f, x0, y0, x1, y1), 0, 1) * alpha;
            if (cov <= 0.002f) continue;
            uint8_t *a = &A[y * PLANE_W + x];
            int add = (int)(cov * 255.0f + 0.5f);
            *a = (uint8_t)(*a + add * (255 - *a) / 255);
        }
}

static void cut_seg(uint8_t *A, float x0, float y0, float x1, float y1, float w)
{
    int bx0 = (int)floorf(fminf(x0, x1) - w * 0.5f - 1), bx1 = (int)ceilf(fmaxf(x0, x1) + w * 0.5f + 1);
    int by0 = (int)floorf(fminf(y0, y1) - w * 0.5f - 1), by1 = (int)ceilf(fmaxf(y0, y1) + w * 0.5f + 1);
    if (bx0 < 0) bx0 = 0;
    if (by0 < 0) by0 = 0;
    if (bx1 > PLANE_W) bx1 = PLANE_W;
    if (by1 > CARD_H) by1 = CARD_H;
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) {
            float cov = clampf(w * 0.5f + 0.5f - seg_dist(x + 0.5f, y + 0.5f, x0, y0, x1, y1), 0, 1);
            if (cov <= 0.002f) continue;
            uint8_t *a = &A[y * PLANE_W + x];
            *a = (uint8_t)(*a * (255 - (int)(cov * 255.0f + 0.5f)) / 255);
        }
}

// advances the ink plane to `lt` ms into the doodle: draws what was drawn since the last time, rubs out what was rubbed since
static void doodle_advance(Doodle *d, float lt, uint8_t *A)
{
    float lts = fminf(lt, d->draw_cap > 0 ? d->draw_cap : 1e9f); // (what is drawn stops where the pen was when the work finished)
    float k = SYM_K * d->scale, ox = d->xoff + CARD_W * 0.5f + d->tx, oy = CARD_H * 0.5f + d->ty + 1, cr = cosf(d->rot), sr = sinf(d->rot);
#define TX(px, py) (ox + k * (((px)-60) * cr - ((py)-28) * sr)) // (the symbol's 120 x 56 box, turned about its middle)
#define TY(px, py) (oy + k * (((px)-60) * sr + ((py)-28) * cr))
    for (int si = 0; si < d->nst; si++) {
        DStroke *st = &d->st[si];
        float f = (lts - st->t0) / st->dur;
        if (f <= 0) continue;
        if (f > 1) f = 1;
        float f2 = 0.55f * f + 0.45f * ease(f); // slow into the stroke, quick through it, slow out of it
        if (f2 <= st->last_f) continue;
        for (int j = 1; j < st->n; j++) {
            const DPt *a = &st->p[j - 1], *b = &st->p[j];
            if (b->t <= st->last_f) continue;
            if (a->t >= f2) break;
            float span = b->t - a->t, ta = fmaxf(a->t, st->last_f), tb = fminf(b->t, f2);
            float ua = span > 1e-6f ? (ta - a->t) / span : 0.0f, ub = span > 1e-6f ? (tb - a->t) / span : 1.0f;
            float ax = a->x + (b->x - a->x) * ua, ay = a->y + (b->y - a->y) * ua, bx = a->x + (b->x - a->x) * ub, by = a->y + (b->y - a->y) * ub;
            float u = a->s / st->len, slow = 1.0f - fminf(1.0f, ((a->v > 0 ? a->v : 1.0f) - 0.35f) / 0.65f); // slower = more wax laid down
            float press = (1.12f - 0.5f * u * u) * (0.8f + 0.35f * slow) * (u > 0.86f ? fmaxf(0.3f, 1 - (u - 0.86f) / 0.16f) : 1.0f) * (a->s < 2.5f ? 1.15f : 1.0f);
            ink_seg(A, TX(ax, ay), TY(ax, ay), TX(bx, by), TY(bx, by), 3.9f * press * st->lw * k, fminf(1.0f, (0.5f + 0.4f * press) * st->gain)); // the wax
            ink_seg(A, TX(ax + 0.8f, ay - 0.7f), TY(ax + 0.8f, ay - 0.7f), TX(bx + 0.8f, by - 0.7f), TY(bx + 0.8f, by - 0.7f), 1.5f * k, (0.28f + 0.25f * press) * st->gain); // and a lighter pass beside it
        }
        st->last_f = f2;
    }
#undef TX
#undef TY
}

// ------------------------------------------------------------------------------------------------ the pieces, made once
static struct {
    int ok;
    PaperSpr *dood;   // the wax of the whole bar, as a picture
    uint8_t *plane;   // the ink of the whole bar: PLANE_W x CARD_H
    float *dx, *dy;   // how the paper makes a stroke wander
    uint8_t *grain;   // the paper's tooth: how much wax lands
} A;

static uint32_t hash2(int x, int y, uint32_t s)
{
    uint32_t h = s * 0x9E3779B1u ^ (uint32_t)x * 0x85EBCA6Bu ^ (uint32_t)y * 0xC2B2AE35u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
static float hashf(int x, int y, uint32_t s) { return (hash2(x, y, s) >> 8) * (1.0f / 16777216.0f); }
static float vnoise(float x, float y, uint32_t s)
{
    float fx = floorf(x), fy = floorf(y), tx = x - fx, ty = y - fy;
    tx = tx * tx * (3 - 2 * tx);
    ty = ty * ty * (3 - 2 * ty);
    int ix = (int)fx, iy = (int)fy;
    float a = hashf(ix, iy, s), b = hashf(ix + 1, iy, s), c = hashf(ix, iy + 1, s), d = hashf(ix + 1, iy + 1, s);
    return (a + (b - a) * tx) + ((c + (d - c) * tx) - (a + (b - a) * tx)) * ty;
}

static void assets_free(void)
{
    paper_spr_free(A.dood);
    free(A.plane);
    free(A.dx);
    free(A.dy);
    free(A.grain);
    memset(&A, 0, sizeof A);
}

static int assets_make(void)
{
    if (A.ok) return 1;
    size_t n = (size_t)PLANE_W * CARD_H;
    A.dood = paper_spr_new(PLANE_W, CARD_H);
    A.plane = calloc(n, 1);
    A.dx = malloc(n * sizeof(float));
    A.dy = malloc(n * sizeof(float));
    A.grain = malloc(n);
    if (!A.dood || !A.plane || !A.dx || !A.dy || !A.grain) {
        assets_free();
        return 0;
    }
    A.dood->ox = A.dood->oy = 0;
    for (int y = 0; y < CARD_H; y++)
        for (int x = 0; x < PLANE_W; x++) {
            size_t i = (size_t)y * PLANE_W + x;
            A.dx[i] = (vnoise(x * 0.07f, y * 0.07f, 21u) - 0.5f) * 2.4f; // the wander of a stroke's edge
            A.dy[i] = (vnoise(x * 0.07f, y * 0.07f, 22u) - 0.5f) * 2.4f;
            float h = (hashf(x, y, 31u) + hashf(x >> 1, y >> 1, 32u) + hashf(x, y, 33u)) / 3.0f; // the tooth of the paper: where the wax does not catch
            A.grain[i] = (uint8_t)(clampf(4.8f * h - 1.15f, 0, 1) * 255.0f + 0.5f);
        }
    A.ok = 1;
    return 1;
}

// ------------------------------------------------------------------------------------------------ the mark itself: state, and a frame
static struct {
    int on, shown, busy;
    unsigned n;
    uint64_t t0, t_show, t_last;
    uint8_t *patch; // AREA of G.work as it was when the drawing began
    Surf *area;     // AREA-sized scratch the frame is drawn in
    Surf *frame;    // the screen as it was when the drawing began, with the drawing in AREA: what is presented while it is there (G.work itself is never drawn on)
    Doodle d[NSLOT];
    int nd;         // symbols begun
    unsigned seq;   // how many there have been
    PaperRng rng;   // what to draw next, how each is drawn and how long the pauses are: new every time the drawing starts
    unsigned used;  // the symbols on the bar now (one bit each): none is drawn twice in a row of them
    float gap;      // the pause before the next symbol
    int finishing;  // the work is over: the rubbing out is on, nothing new begins
    EPt *esc;       // the rubber's track over the whole drawing, in the bar's own pixels
    int nesc;
    float edur, elast, t_fin; // how long it takes, how far it has gone, when it began (ms since the drawing began)
    uint64_t t_fin_abs;       // when the rubbing out began (ui_now)
    uint8_t *save;  // AREA of the frame the ink is being laid over, while it is (see ui_wait_overlay_on)
    int saved, over; // that is held now; the rubbing out has reached its end
} W;

static void pump(void);

static void unhook(void)
{
    if (gfx_pump_hook == pump) gfx_pump_hook = NULL;
    if (fsx_pump_hook == pump) fsx_pump_hook = NULL;
}

static void reset(void)
{
    unhook();
    for (int k = 0; k < NSLOT; k++) doodle_free(&W.d[k]);
    free(W.esc);
    free(W.save);
    free(W.patch);
    surf_free(W.area);
    surf_free(W.frame);
    memset(&W, 0, sizeof W);
}

Rect ui_wait_area(void) { return AREA; }


static void start_symbol(float T)
{
    int k = W.nd;
    int free_n = 0, pick = 0;
    for (int s = 0; s < NSYM; s++) free_n += !(W.used >> s & 1u);
    int r = (int)(paper_rng_f(&W.rng) * free_n);
    for (int s = 0; s < NSYM; s++) // (a random one of those not on the bar yet)
        if (!(W.used >> s & 1u) && r-- == 0) {
            pick = s;
            break;
        }
    W.used |= 1u << pick;
    doodle_make(&W.d[k], pick, 1000u + (uint32_t)(paper_rng_f(&W.rng) * 1000000.0f), T);
    W.gap = GAP_MIN + GAP_VAR * paper_rng_f(&W.rng);
    W.d[k].xoff = (float)(k * SLOT_STEP);
    W.seq++;
    W.nd++;
}

// the ink of the bar as the wax lays it: the strokes' edges wander, the paper's tooth eats some of it, and each symbol has its own colour
static void dood_update(void)
{
    for (int y = 0; y < CARD_H; y++)
        for (int x = 0; x < PLANE_W; x++) {
            size_t i = (size_t)y * PLANE_W + x;
            float sx = x + 0.5f + A.dx[i] - 0.5f, sy = y + 0.5f + A.dy[i] - 0.5f;
            int ix = (int)floorf(sx), iy = (int)floorf(sy);
            float fx = sx - ix, fy = sy - iy, v[4];
            for (int k = 0; k < 4; k++) {
                int px = ix + (k & 1), py = iy + (k >> 1);
                v[k] = px < 0 || py < 0 || px >= PLANE_W || py >= CARD_H ? 0.0f : A.plane[(size_t)py * PLANE_W + px];
            }
            float a = ((v[0] + (v[1] - v[0]) * fx) + ((v[2] + (v[3] - v[2]) * fx) - (v[0] + (v[1] - v[0]) * fx)) * fy) * (A.grain[i] / 255.0f);
            int slot = x / SLOT_STEP;
            slot = slot < 0 ? 0 : slot > W.nd - 1 ? W.nd - 1 : slot;
            uint32_t rgb = pal_mix(SYMS[W.d[slot].sym].rgb, 0xFFFFFF, PAL(doodle_lift, 0.0f)); // (Paper night's crayons are paler: dark paper)
            uint8_t *o = A.dood->px + i * 4;
            o[0] = (uint8_t)(rgb >> 16);
            o[1] = (uint8_t)(rgb >> 8);
            o[2] = (uint8_t)rgb;
            o[3] = (uint8_t)clampf(a + 0.5f, 0, 255);
        }
}

// rubs the bar out as far as `es` ms into the rubbing
static void erase_to(float es)
{
    if (es <= 0 || !W.esc) return;
    for (int j = 1; j < W.nesc; j++) {
        const EPt *a = &W.esc[j - 1], *b = &W.esc[j];
        if (b->t <= W.elast) continue;
        if (a->t >= es) break;
        if (a->x == b->x && a->y == b->y) continue;
        float span = b->t - a->t, ta = fmaxf(a->t, W.elast), tb = fminf(b->t, es);
        float ua = span > 1e-6f ? (ta - a->t) / span : 0.0f, ub = span > 1e-6f ? (tb - a->t) / span : 1.0f;
        cut_seg(A.plane, a->x + (b->x - a->x) * ua, a->y + (b->y - a->y) * ua, a->x + (b->x - a->x) * ub, a->y + (b->y - a->y) * ub, a->w * SYM_K);
    }
    W.elast = es;
    if (es >= W.edur) memset(A.plane, 0, (size_t)PLANE_W * CARD_H); // gone, completely
}

// brings the drawing to T and makes the frame: the footer as it was, the symbols on it, copied into the private copy of the screen (W.frame) and shown
static void frame_at(float T)
{
    int most = g_ui_wait_count < 1 ? 1 : g_ui_wait_count > NSLOT ? NSLOT : g_ui_wait_count;
    if (!W.finishing) { // the next symbol begins once the last is drawn, and a pause, until the bar is full
        if (W.nd == 0) start_symbol(T);
        else if (W.nd < most && T >= (float)W.d[W.nd - 1].t0 + W.d[W.nd - 1].draw + W.gap) start_symbol(T);
    }
    for (int k = 0; k < W.nd; k++) {
        float lt = T - (float)W.d[k].t0;
        if (lt > 0) doodle_advance(&W.d[k], lt, A.plane);
    }
    if (W.finishing) erase_to(T - W.t_fin);
    dood_update();
    for (int y = 0; y < AREA.h; y++) memcpy(W.area->px + (size_t)y * AREA.w * 4, W.patch + (size_t)y * AREA.w * 4, (size_t)AREA.w * 4);
    gfx_blit_over(W.area, A.dood->px, PLANE_W, CARD_H, SLOT_X0 - AREA.x, SLOT_Y - AREA.y);
    for (int y = 0; y < AREA.h; y++)
        memcpy(W.frame->px + ((size_t)(AREA.y + y) * SCREEN_W + AREA.x) * 4, W.area->px + (size_t)y * AREA.w * 4, (size_t)AREA.w * 4);
    ui_present_rects(W.frame, &AREA, 1);
    g_ui_wait_frames++;
}

static void show(uint64_t now)
{
    W.busy = 1;
    int ok = assets_make();
    if (ok) {
        W.patch = malloc((size_t)AREA.w * AREA.h * 4);
        W.area = surf_new(AREA.w, AREA.h);
        W.frame = surf_clone(G.work);
        if (!W.patch || !W.area || !W.frame) ok = 0;
    }
    if (!ok) {
        free(W.patch);
        W.patch = NULL;
        surf_free(W.area);
        W.area = NULL;
        surf_free(W.frame);
        W.frame = NULL;
        W.on = 0;
        unhook();
        W.busy = 0;
        return;
    }
    memset(A.plane, 0, (size_t)PLANE_W * CARD_H);
    for (int y = 0; y < AREA.h; y++) memcpy(W.patch + (size_t)y * AREA.w * 4, G.work->px + ((size_t)(AREA.y + y) * SCREEN_W + AREA.x) * 4, (size_t)AREA.w * 4);
    paper_rng_seed(&W.rng, (uint32_t)now * 2654435761u ^ (uint32_t)(W.t0 >> 2) ^ (uint32_t)g_ui_wait_shown * 40503u);
    W.shown = 1;
    W.t_show = now;
    W.t_last = now;
    g_ui_wait_shown++;
    frame_at(0);
    W.busy = 0;
}

static void pump(void)
{
    if (!W.busy) ui_busy_tick(); // (a waiting page's dots move with the work too; not from inside a frame of the drawing, which is shown from its own copy)
    if (W.finishing) { // (being rubbed out over the next screen's work: it goes on moving)
        ui_wait_tick();
        return;
    }
    if (!W.on || W.busy || !G.plat || g_ui_wait_after_ms >= 1000000) return;
    if (!W.shown && (++W.n & 7u)) return; // (the clock is read now and then, not at every sprite)
    uint64_t now = ui_now();
    if (!W.shown) {
        if (now - W.t0 >= (uint64_t)g_ui_wait_after_ms) show(now);
        return;
    }
    if (now - W.t_last < 30) return;
    W.busy = 1;
    W.t_last = now;
    frame_at((float)(now - W.t_show));
    W.busy = 0;
}

void ui_wait_begin(void)
{
    if (!G.plat || G.quit || g_ui_wait_after_ms >= 1000000) return;
    if (W.shown) ui_wait_abort(); // (one still being rubbed out is cut short: the display is given back as it should be)
    reset();
    W.on = 1;
    W.t0 = ui_now();
    gfx_pump_hook = pump;
    fsx_pump_hook = pump;
}

// forgets it. If the display may still carry some of it (the rubbing out was under way), the place is shown again as the app's own frame has it.
void ui_wait_abort(void)
{
    if (!W.on && !W.shown) return;
    int carries = W.finishing;
    reset();
    if (carries && G.plat && !G.quit && G.work) ui_present_rects(G.work, &AREA, 1);
}

// the drawing is on the display now, made by this module's own frames (not yet being rubbed out, and not this module drawing a frame)
int ui_wait_shown(void) { return W.shown && !W.finishing && !W.busy; }

// while it is shown, its private copy of the screen is what the display has: a part of the same screen that changes meanwhile (a waiting page's dots) is put on that
// copy and shown from it, so the drawing stays and the console's buffers are only ever given the one picture
Surf *ui_wait_frame(void) { return ui_wait_shown() ? W.frame : NULL; }

// the rubbing out is going on: the ink is laid over whatever the display is given, until it has gone
int ui_wait_erasing(void) { return W.on && W.finishing; }

// where a point of a symbol's own 120 x 56 box lands in the bar's pixels
static void to_bar(const Doodle *d, float bx, float by, float *X, float *Y)
{
    float k = SYM_K * d->scale, cr = cosf(d->rot), sr = sinf(d->rot);
    *X = d->xoff + CARD_W * 0.5f + d->tx + k * ((bx - 60) * cr - (by - 28) * sr);
    *Y = CARD_H * 0.5f + d->ty + 1 + k * ((bx - 60) * sr + (by - 28) * cr);
}

// The work is over: the pen stops where it is, and at once one rubbing-out is begun over the whole of what is drawn, all the symbols together (a scribble over the rectangle
// that holds them, then a go at what it missed). It does not hold anything up: the next screen's frames (and the change to it) are shown with what is left of the ink laid
// over them, ui_wait_tick keeps it going while the app waits or works, and input is taken as ever, so the rubbing out goes on while the next screen is being used.
void ui_wait_finish(void)
{
    if (!W.on || W.finishing) return;
    if (!W.shown || G.quit) {
        reset();
        return;
    }
    W.busy = 1;
    W.finishing = 1;
    float T = (float)(ui_now() - W.t_show);
    for (int k = 0; k < W.nd; k++) { // (up to date as of now, and no further)
        Doodle *d = &W.d[k];
        float lt = T - (float)d->t0;
        if (lt <= 0) continue;
        d->draw_cap = lt < 1 ? 1 : lt;
        doodle_advance(d, lt, A.plane);
    }
    V2 *D = malloc(4096 * sizeof(V2));
    int nD = 0;
    for (int k = 0; k < W.nd && D; k++) // every few points of every stroke drawn so far, in the bar's pixels
        for (int si = 0; si < W.d[k].nst; si++) {
            const DStroke *st = &W.d[k].st[si];
            for (int j = 0; j < st->n && nD < 4096; j += 3)
                if (st->p[j].t <= st->last_f && st->last_f > 0) to_bar(&W.d[k], st->p[j].x, st->p[j].y, &D[nD].x, &D[nD].y), nD++;
        }
    W.edur = 0;
    if (D && nD > 0) {
        Doodle tmp;
        memset(&tmp, 0, sizeof tmp);
        PaperRng R;
        paper_rng_seed(&R, 4242u + W.seq);
        scribble_make(&tmp, D, nD, &R);
        W.esc = tmp.esc;
        W.nesc = tmp.nesc;
        float f = fminf(1.0f / ERASE_K, ERASE_MAX_MS / (tmp.erase_dur > 1 ? tmp.erase_dur : 1)); // (as quick as it is made, but never longer than ERASE_MAX_MS)
        for (int i = 0; i < W.nesc; i++) W.esc[i].t *= f;
        W.edur = tmp.erase_dur * f;
    }
    free(D);
    W.t_fin = T;
    W.elast = 0;
    W.t_fin_abs = ui_now();
    W.t_last = 0;
    // what the display holds of the mark now is the old screen with the ink on it (the private frame): from here on the ink is laid over the frames that are shown, so the
    // private copies of the old screen are not needed
    free(W.patch);
    W.patch = NULL;
    surf_free(W.area);
    W.area = NULL;
    surf_free(W.frame);
    W.frame = NULL;
    W.save = malloc((size_t)AREA.w * AREA.h * 4);
    if (!W.save) { // (no room to lay it over anything: it is simply taken off)
        W.busy = 0;
        ui_wait_abort();
        return;
    }
    W.busy = 0;
}

// Lays the ink, as far into the rubbing out as it is by the clock, over the frame `s` that is about to be shown (the picture of the place is kept to put back with
// ui_wait_overlay_off). Returns 0 when nothing is being rubbed out; 1 when the ink is on `s`; 2 when the rubbing out has just ended: nothing is laid, but the place must
// still be shown once more (ui_wait_area) so that the last of the ink leaves the display; ui_wait_overlay_off then ends it.
int ui_wait_overlay_on(Surf *s)
{
    if (!W.on || !W.finishing || !s || !W.save) return 0;
    float es = (float)(ui_now() - W.t_fin_abs);
    if (es >= W.edur) {
        W.over = 1;
        return 2;
    }
    erase_to(es);
    dood_update();
    for (int y = 0; y < AREA.h; y++) memcpy(W.save + (size_t)y * AREA.w * 4, s->px + ((size_t)(AREA.y + y) * SCREEN_W + AREA.x) * 4, (size_t)AREA.w * 4);
    W.saved = 1;
    gfx_blit_over(s, A.dood->px, PLANE_W, CARD_H, SLOT_X0, SLOT_Y);
    return 1;
}

// Takes the ink off `s` again (the app's frames are never kept with ink on them), and ends it when the rubbing out is over.
void ui_wait_overlay_off(Surf *s, int how)
{
    if (how == 1 && W.saved && W.save && s)
        for (int y = 0; y < AREA.h; y++) memcpy(s->px + ((size_t)(AREA.y + y) * SCREEN_W + AREA.x) * 4, W.save + (size_t)y * AREA.w * 4, (size_t)AREA.w * 4);
    W.saved = 0;
    if (how == 2 || W.over) reset();
}

// While the app waits for a button or works, the rubbing out must go on without a frame of the next screen to ride on: now and then the place is shown again with the ink as
// it is by now, laid over what the display shows (the screen that is going away, if the change to the next has not begun to play yet).
void ui_wait_tick(void)
{
    if (!W.on || !W.finishing || W.busy || G.quit || !G.plat) return;
    uint64_t now = ui_now();
    if (W.t_last && now - W.t_last < 12) return; // (about eighty a second at most)
    W.t_last = now;
    W.busy = 1;
    Surf *base = ui_display_base();
    int ov = ui_wait_overlay_on(base);
    if (ov) {
        if (G.plat->present_rects && G.prev_ok) G.plat->present_rects(G.plat, base, &AREA, 1);
        else G.plat->present(G.plat, base);
        g_ui_wait_frames++;
        ui_wait_overlay_off(base, ov);
    }
    W.busy = 0;
}

// A still of the drawing `t_ms` in, made the way the bar makes it (symbols one after another, each drawn by hand, none rubbed out yet), as a picture of the
// whole bar (PLANE_W x CARD_H, anchor at its top left): for a page that shows the drawing itself while it waits (the update page, checking). NULL while the bar
// itself is out (they share the ink plane) or if it cannot be made. Free with paper_spr_free.
PaperSpr *ui_wait_still(float t_ms, uint32_t seed)
{
    if (W.on || !assets_make()) return NULL;
    PaperRng r;
    paper_rng_seed(&r, seed);
    Doodle d[NSLOT];
    memset(d, 0, sizeof d);
    int nd = 0;
    unsigned used = 0;
    float T = 0;
    memset(A.plane, 0, (size_t)PLANE_W * CARD_H);
    while (nd < NSLOT && T <= t_ms) {
        int free_n = 0, pick = 0;
        for (int s = 0; s < NSYM; s++) free_n += !(used >> s & 1u);
        int k = (int)(paper_rng_f(&r) * free_n);
        for (int s = 0; s < NSYM; s++)
            if (!(used >> s & 1u) && k-- == 0) {
                pick = s;
                break;
            }
        used |= 1u << pick;
        doodle_make(&d[nd], pick, 1000u + (uint32_t)(paper_rng_f(&r) * 1000000.0f), T);
        d[nd].xoff = (float)(nd * SLOT_STEP);
        T = (float)d[nd].t0 + d[nd].draw + GAP_MIN + GAP_VAR * paper_rng_f(&r);
        nd++;
    }
    for (int k = 0; k < nd; k++)
        if (t_ms > d[k].t0) doodle_advance(&d[k], t_ms - (float)d[k].t0, A.plane);
    PaperSpr *out = paper_spr_new(PLANE_W, CARD_H);
    if (out) {
        out->ox = out->oy = 0;
        for (int y = 0; y < CARD_H; y++)
            for (int x = 0; x < PLANE_W; x++) { // (as dood_update lays the wax)
                size_t i = (size_t)y * PLANE_W + x;
                float sx = x + A.dx[i], sy = y + A.dy[i];
                int ix = (int)floorf(sx), iy = (int)floorf(sy);
                float fx = sx - ix, fy = sy - iy, v[4];
                for (int q = 0; q < 4; q++) {
                    int px = ix + (q & 1), py = iy + (q >> 1);
                    v[q] = px < 0 || py < 0 || px >= PLANE_W || py >= CARD_H ? 0.0f : A.plane[(size_t)py * PLANE_W + px];
                }
                float a = ((v[0] + (v[1] - v[0]) * fx) + ((v[2] + (v[3] - v[2]) * fx) - (v[0] + (v[1] - v[0]) * fx)) * fy) * (A.grain[i] / 255.0f);
                int slot = x / SLOT_STEP;
                slot = slot < 0 ? 0 : slot > nd - 1 ? nd - 1 : slot;
                uint32_t rgb = nd ? pal_mix(SYMS[d[slot].sym].rgb, 0xFFFFFF, PAL(doodle_lift, 0.0f)) : 0;
                uint8_t *o = out->px + i * 4;
                o[0] = (uint8_t)(rgb >> 16);
                o[1] = (uint8_t)(rgb >> 8);
                o[2] = (uint8_t)rgb;
                o[3] = (uint8_t)clampf(a + 0.5f, 0, 255);
            }
    }
    for (int k = 0; k < nd; k++) doodle_free(&d[k]);
    memset(A.plane, 0, (size_t)PLANE_W * CARD_H);
    return out;
}
