#include "paper_roll.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Same numbers on the PC and the console: no fused multiply-add, and the only libm calls are the exact ones (sqrtf, floorf, fabsf).
#pragma GCC optimize("fp-contract=off")

#define PI_F 3.14159265f
#define RN 24                   // facets the mockup builds its cylinder from; the roll's pattern wraps round 2 R RN tan(pi / RN)
#define ROLL_CIRC 6.31930f      // that, per px of radius
#define SH_DX 2.0f              // the roll's shadow: drop-shadow(2px 3px 2.5px rgba(74,58,26,.45))
#define SH_DY 3.0f
#define SH_SIGMA 3.0f           // measured against Chrome: its drop-shadow reads wider than a 1.25 or 2.5 sigma blur
#define SH_ALPHA 0.5f
#define SH_PAD 10               // px the shadow reaches beyond the roll
#define CORNER 3.0f
#define MAXCOL 160

// ------------------------------------------------------------------------------------------------ small maths
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float pmod(float x, float p) { return x - floorf(x / p) * p; }
static float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3.0f - 2.0f * t); }
static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

static float psin(float a)
{
    a -= 2.0f * PI_F * floorf(a / (2.0f * PI_F) + 0.5f);
    if (a > PI_F * 0.5f) a = PI_F - a;
    else if (a < -PI_F * 0.5f) a = -PI_F - a;
    float a2 = a * a;
    return a * (1.0f + a2 * (-1.0f / 6.0f + a2 * (1.0f / 120.0f + a2 * (-1.0f / 5040.0f + a2 * (1.0f / 362880.0f)))));
}
static float pcos(float a) { return psin(a + PI_F * 0.5f); }

static float patan2(float y, float x)
{
    float ax = fabsf(x), ay = fabsf(y), mx = maxf(ax, ay), mn = minf(ax, ay);
    if (mx <= 0.0f) return 0.0f;
    float a = mn / mx, s = a * a;
    float r = a * (0.9998660f + s * (-0.3302995f + s * (0.1801410f + s * (-0.0851330f + s * 0.0208351f))));
    if (ay > ax) r = PI_F * 0.5f - r;
    if (x < 0) r = PI_F - r;
    return y < 0 ? -r : r;
}
static float pasin(float x)
{
    x = clampf(x, -1.0f, 1.0f);
    return patan2(x, sqrtf(maxf(0.0f, 1.0f - x * x)));
}

// CSS cubic-bezier(x1, y1, x2, y2) at time x
static float bezier(float x1, float y1, float x2, float y2, float x)
{
    float lo = 0, hi = 1, t = x;
    x = clampf(x, 0, 1);
    for (int i = 0; i < 22; i++) {
        t = (lo + hi) * 0.5f;
        float u = 1 - t, bx = 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t;
        if (bx < x) lo = t; else hi = t;
    }
    float u = 1 - t, y = 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t;
    return y < 1e-4f ? 0.0f : y > 1.0f - 1e-4f ? 1.0f : y; // the ends are exact
}
static float ease_out(float t) { return bezier(0.0f, 0.0f, 0.58f, 1.0f, t); }
static float ease_in(float t) { return bezier(0.42f, 0.0f, 1.0f, 1.0f, t); }

static int rrect_union(RollRect *a, RollRect b)
{
    if (b.w <= 0 || b.h <= 0) return 0;
    if (a->w <= 0 || a->h <= 0) { *a = b; return 1; }
    int x0 = imin(a->x, b.x), y0 = imin(a->y, b.y), x1 = imax(a->x + a->w, b.x + b.w), y1 = imax(a->y + a->h, b.y + b.h);
    a->x = x0; a->y = y0; a->w = x1 - x0; a->h = y1 - y0;
    return 1;
}
static RollRect rect_of(float x0, float y0, float x1, float y1)
{
    RollRect r = {(int)floorf(x0), (int)floorf(y0), 0, 0};
    r.w = (int)floorf(x1) + 1 - r.x;
    r.h = (int)floorf(y1) + 1 - r.y;
    return r;
}

// the part of the surface a draw may touch: the surface, cut by the clip if there is one
static void work_area(const Surf *s, const RollRect *clip, int *x0, int *y0, int *x1, int *y1)
{
    *x0 = 0; *y0 = 0; *x1 = s->w; *y1 = s->h;
    if (clip) {
        *x0 = imax(*x0, clip->x); *y0 = imax(*y0, clip->y);
        *x1 = imin(*x1, clip->x + clip->w); *y1 = imin(*y1, clip->y + clip->h);
    }
}

// ------------------------------------------------------------------------------------------------ picks and seeds
RollPick roll_pick(RollPick prev, uint32_t seed)
{
    PaperRng r;
    paper_rng_seed(&r, seed ^ 0x70C1Cu);
    RollPick p;
    if (prev.colour >= 0 && prev.colour < PAPER_TAPE_COUNT) p.colour = (prev.colour + 1 + (int)(paper_rng_f(&r) * (PAPER_TAPE_COUNT - 1))) % PAPER_TAPE_COUNT;
    else p.colour = (int)(paper_rng_f(&r) * PAPER_TAPE_COUNT) % PAPER_TAPE_COUNT;
    if (prev.kind >= 0 && prev.kind < PAT_COUNT) p.kind = (prev.kind + 1 + (int)(paper_rng_f(&r) * (PAT_COUNT - 1))) % PAT_COUNT;
    else p.kind = (int)(paper_rng_f(&r) * PAT_COUNT) % PAT_COUNT;
    return p;
}

uint32_t roll_game_seed(uint32_t base, int index)
{
    uint32_t h = base + (uint32_t)(index + 1) * 0x9E3779B9u;
    h ^= h >> 16;
    h *= 0x85EBCA6Bu;
    h ^= h >> 13;
    h *= 0xC2B2AE35u;
    h ^= h >> 16;
    return h ? h : 1u;
}

// ------------------------------------------------------------------------------------------------ geometry
RollGeo roll_geo_from_seed(uint32_t seed)
{
    PaperRng r;
    paper_rng_seed(&r, seed ^ 0x5171Bu);
    RollGeo g;
    g.th = 22 + (int)floorf(paper_rng_f(&r) * 8.0f + 0.5f);
    g.dx = -9.0f + paper_rng_f(&r) * 13.0f;
    g.dy = (paper_rng_f(&r) * 2.0f - 1.0f) * 4.0f;
    float sgn = paper_rng_f(&r) < 0.5f ? -1.0f : 1.0f;
    g.rot = sgn * (0.35f + paper_rng_f(&r) * 0.65f);
    g.ov = 4.0f + paper_rng_f(&r) * 10.0f;
    if (g.dy * g.rot > 0) g.dy = -g.dy * 0.6f; // always visibly crooked, but it wanders across the guide rather than leaving it
    g.r0 = 24.0f + paper_rng_f(&r) * 4.0f;
    g.r1 = maxf(15.0f, g.th * 0.5f + 3.0f);
    g.no_crimp = 0;
    g.plain_end = 0;
    return g;
}

// the roll's turn (radians) once the strip is `len_laid` long: the integral of d len / R, R shrinking linearly with progress
static void make_phase(RollStrip *r)
{
    float acc = 0;
    r->phase[0] = 0;
    for (int k = 1; k <= ROLL_PHASE_N; k++) {
        for (int j = 0; j < 8; j++) {
            float e = ((k - 1) + (j + 0.5f) / 8.0f) / ROLL_PHASE_N;
            acc += r->len / ROLL_PHASE_N / 8.0f / (r->r0 + (r->r1 - r->r0) * e);
        }
        r->phase[k] = acc / (2.0f * PI_F) * r->n;
    }
}

// The catches: the crimp, and 0..2 snags (about 3 strips in 10 have none, 45 in 100 one, 25 in 100 two) that sit apart from each other and from the crimp.
static void make_catches(RollStrip *r)
{
    r->ncatch = 0;
    if (r->sx < 1e8f) {
        r->catches[0].progress = clampf((r->sx + 2.0f * r->sk + 10.0f) / r->len, 0.0f, 1.0f);
        r->catches[0].kind = ROLL_CATCH_CRIMP;
        r->catches[0].strength = 1.0f;
        r->ncatch = 1;
    }
    PaperRng g;
    paper_rng_seed(&g, r->seed ^ 0xCA7C4u);
    float f = paper_rng_f(&g);
    int want = f < 0.3f ? 0 : f < 0.75f ? 1 : 2, snags = 0;
    for (int tries = 0; tries < 24 && snags < want && r->ncatch < ROLL_CATCH_MAX; tries++) {
        float p = 0.12f + 0.78f * paper_rng_f(&g), st = 0.45f + 0.5f * paper_rng_f(&g);
        int ok = 1;
        for (int i = 0; i < r->ncatch; i++)
            if (fabsf(r->catches[i].progress - p) < 0.16f) ok = 0;
        if (!ok) continue;
        r->catches[r->ncatch].progress = p;
        r->catches[r->ncatch].kind = ROLL_CATCH_SNAG;
        r->catches[r->ncatch].strength = st;
        r->ncatch++;
        snags++;
    }
    for (int i = 1; i < r->ncatch; i++) // sorted by progress
        for (int j = i; j > 0 && r->catches[j].progress < r->catches[j - 1].progress; j--) {
            RollCatch t = r->catches[j];
            r->catches[j] = r->catches[j - 1];
            r->catches[j - 1] = t;
        }
}

RollStrip *roll_strip_new_geo(const RollRect *guide, RollPick pick, uint32_t seed, int n, const RollGeo *geo)
{
    if (n <= 0) n = ROLL_FRAMES;
    if (n > ROLL_FRAMES_MAX) n = ROLL_FRAMES_MAX;
    if (pick.colour < 0 || pick.colour >= PAPER_TAPE_COUNT) pick.colour = 0;
    if (pick.kind < 0 || pick.kind >= PAT_COUNT) pick.kind = 0;
    RollStrip *r = calloc(1, sizeof *r);
    if (!r) return NULL;
    r->guide = *guide;
    r->pick = pick;
    r->seed = seed;
    r->n = n;
    r->th = geo->th;
    r->rot = geo->rot;
    r->r0 = geo->r0;
    r->r1 = geo->r1;
    r->rb = geo->th + 4;
    int len = (int)floorf(guide->w + geo->ov - geo->dx + 0.5f);
    r->len = (float)len;
    r->ox = guide->x + geo->dx;
    r->oy = guide->y + guide->h * 0.5f + geo->dy;
    // the crimp comes from the tape's own numbers (paper.c draws them from this same seed), so the strip and the roll agree about where it is
    PaperRng cr;
    paper_rng_seed(&cr, seed ^ 0xC12117u);
    r->sx = 0;
    PaperTape t = {len, geo->th, paper_tape_rgb[pick.colour], pick.kind, PAPER_TAPE_ZIG_R | PAPER_TAPE_CRIMP | PAPER_TAPE_EDGE, geo->rot, seed};
    if (geo->plain_end) t.flags &= ~(unsigned)PAPER_TAPE_ZIG_R; // (the roll ran out: the tape's own end, nothing was cut)
    if (len >= 36 && !geo->no_crimp) {
        r->sk = 4.0f;
        // the same four draws, in the same order, as tape_geo in paper.c (its ky line leaves the order of its two draws to the compiler;
        // test_roll checks that the ridge it baked is where these numbers say)
        float f1 = paper_rng_f(&cr), f2 = paper_rng_f(&cr), f3 = paper_rng_f(&cr), f4 = paper_rng_f(&cr);
        r->cx = len * (0.3f + 0.38f * f1);
        r->sx = r->cx - r->sk;
        r->ky = (f2 < 0.5f ? -1.0f : 1.0f) * (1.2f + 1.6f * f3);
        r->kr = (f4 * 2.0f - 1.0f) * 0.3f;
    } else {
        t.flags &= ~(unsigned)PAPER_TAPE_CRIMP;
        r->sx = 1e9f;
    }
    r->strip = paper_tape(&t);
    if (!r->strip) {
        free(r);
        return NULL;
    }
    float ca = pcos(geo->rot * (PI_F / 180.0f)), sa = psin(geo->rot * (PI_F / 180.0f));
    r->sx_mid = r->ox + ca * len * 0.5f;
    r->sy_mid = r->oy + sa * len * 0.5f;
    make_phase(r);
    make_catches(r);
    return r;
}

RollStrip *roll_strip_new(const RollRect *guide, RollPick pick, uint32_t seed, int n)
{
    RollGeo g = roll_geo_from_seed(seed);
    return roll_strip_new_geo(guide, pick, seed, n, &g);
}

void roll_strip_free(RollStrip *r)
{
    if (!r) return;
    paper_spr_free(r->strip);
    paper_spr_free(r->tail);
    for (int i = 0; i < ROLL_FRAMES_MAX; i++) paper_spr_free(r->frames[i]);
    free(r);
}

size_t roll_strip_bytes(const RollStrip *r)
{
    size_t b = 0;
    if (r->strip) b += (size_t)r->strip->w * r->strip->h * 4;
    if (r->tail) b += (size_t)r->tail->w * r->tail->h * 4;
    for (int i = 0; i < ROLL_FRAMES_MAX; i++)
        if (r->frames[i]) b += (size_t)r->frames[i]->w * r->frames[i]->h * 4;
    return b;
}

// ------------------------------------------------------------------------------------------------ where things are
static float clamp01(float p) { return p != p ? 0.0f : clampf(p, 0.0f, 1.0f); }

float roll_radius(const RollStrip *r, float progress)
{
    return r->r0 + (r->r1 - r->r0) * clamp01(progress);
}

// the roll's middle in the strip's own frame (x along it, y across), then on the screen
static void roll_local(const RollStrip *r, float p, float *x, float *y)
{
    float len = r->len * clamp01(p);
    if (len > r->sx) { // past the crimp the far half runs along its own line: turned by kr about (sx - 1, ky), the roll's axis turned with it
        float d = len - r->sx + 1.0f, k = r->kr * (PI_F / 180.0f);
        *x = r->sx - 1.0f + d * pcos(k);
        *y = r->ky + d * psin(k);
    } else {
        *x = len;
        *y = 0.0f;
    }
}

float roll_angle(const RollStrip *r, float progress)
{
    return r->rot + (r->len * clamp01(progress) > r->sx ? r->kr : 0.0f);
}

void roll_centre(const RollStrip *r, float progress, float *x, float *y)
{
    float lx, ly, ca = pcos(r->rot * (PI_F / 180.0f)), sa = psin(r->rot * (PI_F / 180.0f));
    roll_local(r, progress, &lx, &ly);
    *x = r->ox + ca * lx - sa * ly;
    *y = r->oy + sa * lx + ca * ly;
}

static float turn_at(const RollStrip *r, float progress) // turns of the roll, in frames
{
    float f = clamp01(progress) * ROLL_PHASE_N;
    int i = (int)floorf(f);
    if (i >= ROLL_PHASE_N) return r->phase[ROLL_PHASE_N];
    return lerpf(r->phase[i], r->phase[i + 1], f - i);
}

int roll_frame_at(const RollStrip *r, float progress)
{
    float t = turn_at(r, progress);
    int i = (int)floorf(t);
    return ((i % r->n) + r->n) % r->n;
}

RollEvents roll_events(const RollStrip *r)
{
    RollEvents e;
    float seam = r->sx + 2.0f * r->sk + 10.0f; // the mockup lays the crimp once the strip is longer than sx + 2 sk + 10
    e.crimp_progress = r->sx < 1e8f ? clampf(seam / r->len, 0.0f, 1.0f) : 2.0f;
    e.crimp_x = r->ox + pcos(r->rot * (PI_F / 180.0f)) * r->cx;
    e.cut_progress = 1.0f;
    e.length = r->len;
    return e;
}

int roll_crossed_crimp(const RollStrip *r, float p0, float p1)
{
    float c = roll_events(r).crimp_progress;
    p0 = clamp01(p0);
    p1 = clamp01(p1);
    return c <= 1.0f && p0 < c && p1 >= c;
}

float roll_speed(const RollStrip *r, float dprogress, float dt_ms)
{
    if (dt_ms <= 0) return 0.0f;
    return fabsf(dprogress) * r->len * 1000.0f / dt_ms;
}

float roll_tap_gap_ms(float px_per_s)
{
    float v = px_per_s / ROLL_V_REF;
    if (v < 0.04f) return 0.0f;
    return 64.0f - 54.0f * minf(1.0f, v);
}

// ------------------------------------------------------------------------------------------------ the strip
static void fade_px(uint8_t *d, const uint8_t *s, int a255) // a straight-alpha source pixel over an opaque one
{
    if (a255 >= 255) {
        d[0] = s[0]; d[1] = s[1]; d[2] = s[2];
    } else {
        d[0] = (uint8_t)(d[0] + ((((int)s[0] - (int)d[0]) * a255 + 128) >> 8));
        d[1] = (uint8_t)(d[1] + ((((int)s[1] - (int)d[1]) * a255 + 128) >> 8));
        d[2] = (uint8_t)(d[2] + ((((int)s[2] - (int)d[2]) * a255 + 128) >> 8));
    }
    d[3] = 255;
}

// the strip, with the part beyond `laid` px along it left out (a straight cut across the strip, tilted with it)
void roll_strip_draw(Surf *s, const RollStrip *r, float progress, const RollRect *clip) { roll_strip_draw_fx(s, r, progress, clip, NULL); }

// the pucker: a light, dark, light ridge across the strip (the crimp's look), `a` 0..1 strong; t 0..1 across it
static void ridge_over(uint8_t *d, float t, float a)
{
    static const float T[5] = {0, 0.24f, 0.52f, 0.78f, 1.0f};
    static const float S[5][4] = {{0, 0, 0, 0}, {0.95f, 0.95f, 0.95f, 0.95f}, {0.27f * 0.46f, 0.19f * 0.46f, 0.10f * 0.46f, 0.46f}, {0.6f, 0.6f, 0.6f, 0.6f}, {0, 0, 0, 0}};
    int i = 0;
    while (i < 3 && t > T[i + 1]) i++;
    float f = (t - T[i]) / (T[i + 1] - T[i]), o[4];
    for (int c = 0; c < 4; c++) o[c] = lerpf(S[i][c], S[i + 1][c], f) * a;
    for (int c = 0; c < 3; c++) d[c] = (uint8_t)clampf(d[c] * (1.0f - o[3]) + o[c] * 255.0f + 0.5f, 0, 255);
}

// The laid length is measured along the strip's own line; past the crimp (once the mockup shows it as two pieces) along the far half's line,
// turned by kr, so the cut is square to the tape on both sides and meets the roll's edge exactly.
void roll_strip_draw_fx(Surf *s, const RollStrip *r, float progress, const RollRect *clip, const RollFx *fx)
{
    float p = clamp01(progress);
    float laid = p >= 1.0f ? r->len + 16.0f : r->len * p;
    if (laid <= 0.0f) return;
    float rr = r->rot * (PI_F / 180.0f), ca = pcos(rr), sa = psin(rr);
    float kk = r->kr * (PI_F / 180.0f), ckr = pcos(kk), skr = psin(kk), cb = pcos(rr + kk), sb = psin(rr + kk);
    int useb = laid > r->sx + 2.0f * r->sk + 10.0f;
    float dlaid = laid - r->sx + 1.0f;
    float puck = fx ? fx->pucker : 0.0f, pc = (useb ? dlaid : laid) - roll_radius(r, p) - 5.0f, cx_dir = useb ? cb : ca;
    const PaperSpr *sp = r->strip;
    int left = (int)floorf(r->sx_mid + 0.5f) - sp->ox, top = (int)floorf(r->sy_mid + 0.5f) - sp->oy;
    int ax0, ay0, ax1, ay1;
    work_area(s, clip, &ax0, &ay0, &ax1, &ay1);
    int y0 = imax(top, ay0), y1 = imin(top + sp->h, ay1);
    for (int y = y0; y < y1; y++) {
        float dy = y + 0.5f - r->oy, c0, xlim;
        if (useb) {
            c0 = dy * sb - (r->sx - 1.0f) * ckr - r->ky * skr; // distance along the far half's line is c0 + (x - ox) cb
            xlim = r->ox + (dlaid - c0) / cb;
        } else {
            c0 = dy * sa;
            xlim = r->ox + (laid - c0) / ca; // screen x at which the strip's laid length ends on this row
        }
        int xa = imax(left, ax0), xb = imin(left + sp->w, ax1);
        int xfull = (int)floorf(xlim); // pixels < xfull are whole; xfull itself is cut part way
        if (xb > xfull + 1) xb = xfull + 1;
        const uint8_t *sr = sp->px + ((size_t)(y - top) * sp->w + (xa - left)) * 4;
        uint8_t *d = s->px + ((size_t)y * s->w + xa) * 4;
        for (int x = xa; x < xb; x++, sr += 4, d += 4) {
            int a = sr[3];
            if (!a) continue;
            if (x == xfull) a = (int)(a * (xlim - xfull) + 0.5f);
            if (a > 0) {
                fade_px(d, sr, a);
                if (puck > 0.0f) {
                    float m = c0 + (x + 0.5f - r->ox) * cx_dir - pc;
                    if (m > -5.0f && m < 5.0f) ridge_over(d, m * 0.1f + 0.5f, puck * 0.7f * (a * (1.0f / 255.0f)));
                }
            }
        }
    }
}

// ------------------------------------------------------------------------------------------------ the roll
// The tape patterns, the same numbers as paper.c's paper_tape (the mockup's); `L` is the length of the texture round the roll, `H` the
// height across it. Returns the white laid over the tape's colour, 0..1.
static float pat_white(int kind, float u, float v, float L, float H)
{
    const float D = 0.70710678f;
    switch (kind) {
    case PAT_STRIPES: {
        float t = (u - L * 0.5f) * D - (v - H * 0.5f) * D + (L + H) * D * 0.5f;
        return pmod(t, 12.0f) < 5.0f ? 0.6f : 0.0f;
    }
    case PAT_DOTS: {
        float du = pmod(u, 12.0f) - 6.0f, dv = pmod(v, 12.0f) - 6.0f, d = sqrtf(du * du + dv * dv);
        return 0.85f * clampf((2.7f - d) / 0.3f, 0, 1);
    }
    case PAT_GRID: return (pmod(u, 12.0f) < 1.4f || pmod(v, 12.0f) < 1.4f) ? 0.7f : 0.0f;
    case PAT_ZIG: {
        float mu = pmod(u, 12.0f), mv = pmod(v, 12.0f);
        return (mu + mv < 6.0f || (12.0f - mu) + mv < 6.0f) ? 0.7f : 0.0f;
    }
    case PAT_CHECK: {
        float mu = pmod(u, 14.0f), mv = pmod(v, 14.0f);
        return ((mu >= 7.0f) != (mv >= 7.0f)) ? 0.7f : 0.0f;
    }
    case PAT_SCALLOP: {
        float du = pmod(u, 12.0f) - 6.0f, dv = pmod(v, 9.0f) - 9.0f, d = sqrtf(du * du + dv * dv);
        return 0.78f * clampf((4.7f - d) / 0.5f, 0, 1);
    }
    case PAT_HLINE: return pmod(H - v, 6.0f) < 2.0f ? 0.7f : 0.0f;
    case PAT_WSTRIPE: {
        float t = -(u - L * 0.5f) * D - (v - H * 0.5f) * D + (L + H) * D * 0.5f;
        return pmod(t, 20.0f) < 9.0f ? 0.62f : 0.0f;
    }
    }
    return 0.0f;
}

// the light laid over the roll across its width (premultiplied r, g, b, a for t 0..1): the mockup's linear-gradient(90deg, ...)
static void shade_at(float t, float *o)
{
    static const float T[7] = {0.0f, 0.14f, 0.30f, 0.38f, 0.52f, 0.72f, 1.0f};
    static const float S[7][4] = {
        {50 / 255.0f * 0.5f, 30 / 255.0f * 0.5f, 10 / 255.0f * 0.5f, 0.5f},
        {50 / 255.0f * 0.08f, 30 / 255.0f * 0.08f, 10 / 255.0f * 0.08f, 0.08f},
        {0, 0, 0, 0},
        {0.5f, 0.5f, 0.5f, 0.5f},
        {0, 0, 0, 0},
        {50 / 255.0f * 0.1f, 30 / 255.0f * 0.1f, 10 / 255.0f * 0.1f, 0.1f},
        {50 / 255.0f * 0.55f, 30 / 255.0f * 0.55f, 10 / 255.0f * 0.55f, 0.55f}};
    t = clampf(t, 0, 1);
    int i = 0;
    while (i < 5 && t > T[i + 1]) i++;
    float f = (t - T[i]) / (T[i + 1] - T[i]);
    for (int c = 0; c < 4; c++) o[c] = lerpf(S[i][c], S[i + 1][c], f);
}

// how much of a 1 px band [a, a + 1] of the roll's height (centre `by`) the span [lo, hi] covers
static float band(float by, float lo, float hi) { return clampf(minf(by + 0.5f, hi) - maxf(by - 0.5f, lo), 0, 1); }

// the coverage of a Gaussian-blurred span [lo, hi] at x (a smooth stand-in for the blur: sigma in px)
static float blurred_span(float x, float lo, float hi)
{
    float a = smooth01((x - lo) / (4.4f * SH_SIGMA) + 0.5f), b = smooth01((x - hi) / (4.4f * SH_SIGMA) + 0.5f);
    return a - b;
}

static float sd_rrect(float px, float py, float hw, float hh, float rad)
{
    float qx = fabsf(px) - (hw - rad), qy = fabsf(py) - (hh - rad);
    float ox = maxf(qx, 0.0f), oy = maxf(qy, 0.0f);
    return sqrtf(ox * ox + oy * oy) + minf(maxf(qx, qy), 0.0f) - rad;
}

typedef struct { float u[4]; float fx; float shade[4]; } RollCol; // everything that depends on the column only

static void roll_col(RollCol *c, float bx, float ik, float R, float phi, float circ)
{
    for (int i = 0; i < 4; i++) {
        float x = bx + (((i + 0.5f) * 0.25f) - 0.5f) * ik;
        float a = (pasin(x / R) - phi) / (2.0f * PI_F) + 0.5f / RN;
        c->u[i] = circ * (a - floorf(a));
    }
    c->fx = blurred_span(bx - SH_DX, -R, R);
    shade_at((bx + R) / (2.0f * R), c->shade);
}

// draws the roll, centred at (cx, cy), scaled by k and turned by `ang` radians about its middle, faded to `alpha`: shadow, then the body
static void roll_render(Surf *s, const RollStrip *r, float R, float phi, float cx, float cy, float k, float ang, float alpha, const RollRect *clip)
{
    if (alpha <= 0.0f || k <= 0.0f) return;
    float hw = R, hh = r->rb * 0.5f, circ = ROLL_CIRC * R, ik = 1.0f / k;
    int fast = ang == 0.0f;
    float ca = pcos(ang), sa = psin(ang);
    float ex = ((hw + SH_PAD) * fabsf(ca) + (hh + SH_PAD) * fabsf(sa)) * k, ey = ((hw + SH_PAD) * fabsf(sa) + (hh + SH_PAD) * fabsf(ca)) * k;
    int ax0, ay0, ax1, ay1;
    work_area(s, clip, &ax0, &ay0, &ax1, &ay1);
    int x0 = imax((int)floorf(cx - ex), ax0), x1 = imin((int)floorf(cx + ex) + 1, ax1);
    int y0 = imax((int)floorf(cy - ey), ay0), y1 = imin((int)floorf(cy + ey) + 1, ay1);
    if (x1 <= x0 || y1 <= y0) return;
    static RollCol cols[MAXCOL];
    if (x1 - x0 > MAXCOL) fast = 0;
    if (fast)
        for (int x = x0; x < x1; x++) roll_col(&cols[x - x0], (x + 0.5f - cx) * ik, ik, R, phi, circ);
    float tc[3] = {((paper_tape_rgb[r->pick.colour] >> 16) & 255) / 255.0f, ((paper_tape_rgb[r->pick.colour] >> 8) & 255) / 255.0f, (paper_tape_rgb[r->pick.colour] & 255) / 255.0f};
    const float shc[3] = {74.0f / 255.0f, 58.0f / 255.0f, 26.0f / 255.0f};
    for (int y = y0; y < y1; y++) {
        uint8_t *d = s->px + ((size_t)y * s->w + x0) * 4;
        for (int x = x0; x < x1; x++, d += 4) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, bx, by;
            if (fast) {
                bx = dx * ik;
                by = dy * ik;
            } else {
                bx = (dx * ca + dy * sa) * ik;
                by = (-dx * sa + dy * ca) * ik;
            }
            RollCol tmp;
            const RollCol *c;
            if (fast) c = &cols[x - x0];
            else {
                roll_col(&tmp, bx, ik, R, phi, circ);
                c = &tmp;
            }
            float base[3] = {d[0] / 255.0f, d[1] / 255.0f, d[2] / 255.0f};
            // the shadow, under the body
            float sh = SH_ALPHA * alpha * c->fx * blurred_span(by - SH_DY, -hh, hh);
            if (sh > 0.002f)
                for (int q = 0; q < 3; q++) base[q] = base[q] + (shc[q] - base[q]) * sh;
            float cov = clampf(0.5f - sd_rrect(bx, by, hw, hh, CORNER) * k, 0, 1);
            if (cov > 0.0f) {
                float v = by + hh, white = 0;
                for (int i = 0; i < 4; i++) white += pat_white(r->pick.kind, c->u[i], v, circ, (float)r->rb);
                white *= 0.25f;
                float col[3];
                for (int q = 0; q < 3; q++) col[q] = tc[q] * (1.0f - white) + white;
                for (int q = 0; q < 3; q++) col[q] = c->shade[q] + col[q] * (1.0f - c->shade[3]); // the light across the roll
                float tw = band(by, -hh, -hh + 1.0f) * 0.55f, bw = band(by, hh - 1.0f, hh) * 0.3f; // inset lines: light on top, dark below
                if (tw > 0) for (int q = 0; q < 3; q++) col[q] = col[q] + (1.0f - col[q]) * tw;
                if (bw > 0) {
                    static const float dk[3] = {60.0f / 255.0f, 40.0f / 255.0f, 20.0f / 255.0f};
                    for (int q = 0; q < 3; q++) col[q] = col[q] + (dk[q] - col[q]) * bw;
                }
                float a = cov * alpha;
                for (int q = 0; q < 3; q++) base[q] = base[q] + (col[q] - base[q]) * a;
            }
            for (int q = 0; q < 3; q++) d[q] = (uint8_t)clampf(base[q] * 255.0f + 0.5f, 0, 255);
            d[3] = 255;
        }
    }
}

static float phi_at(const RollStrip *r, float progress) { return turn_at(r, progress) / r->n * 2.0f * PI_F; }

void roll_roll_draw(Surf *s, const RollStrip *r, float progress, const RollRect *clip) { roll_roll_draw_fx(s, r, progress, clip, NULL); }

// the roll sits on the strip's line and is turned with it (its axle square to the tape), so the tape comes off its edge exactly
void roll_roll_draw_fx(Surf *s, const RollStrip *r, float progress, const RollRect *clip, const RollFx *fx)
{
    float cx, cy, R = roll_radius(r, progress), phi = phi_at(r, progress), ang = roll_angle(r, progress) * (PI_F / 180.0f);
    roll_centre(r, progress, &cx, &cy);
    if (fx) {
        float ca = pcos(ang), sa = psin(ang);
        cx += -ca * fx->back_px - sa * fx->shiver_px;
        cy += -sa * fx->back_px + ca * fx->shiver_px;
        phi -= fx->back_px / R;
        ang += fx->shiver_deg * (PI_F / 180.0f);
    }
    roll_render(s, r, R, phi, cx, cy, 1.0f, ang, 1.0f, clip);
}

void roll_roll_pop(Surf *s, const RollStrip *r, float t)
{
    float e = ease_out(clamp01(t)), cx, cy;
    roll_centre(r, 0.0f, &cx, &cy);
    roll_render(s, r, roll_radius(r, 0.0f), 0.0f, cx, cy, 0.6f + 0.4f * e, roll_angle(r, 0.0f) * (PI_F / 180.0f), e, NULL);
}

void roll_roll_hop(Surf *s, const RollStrip *r, float t)
{
    float e = ease_in(clamp01(t)), cx, cy;
    roll_centre(r, 1.0f, &cx, &cy);
    float rr = r->rot * (PI_F / 180.0f), ca = pcos(rr), sa = psin(rr);
    float mx = 8.0f * e, my = -14.0f * e; // moved in the strip's own frame
    roll_render(s, r, roll_radius(r, 1.0f), phi_at(r, 1.0f), cx + ca * mx - sa * my, cy + sa * mx + ca * my, 1.0f - 0.3f * e, (roll_angle(r, 1.0f) + 26.0f * e) * (PI_F / 180.0f), 1.0f - e, NULL);
}

// the used-up roll: smaller and fainter where it stands, no hop (the tape it laid is the piece)
void roll_roll_spent(Surf *s, const RollStrip *r, float t)
{
    float e = ease_in(clamp01(t)), cx, cy;
    roll_centre(r, 1.0f, &cx, &cy);
    roll_render(s, r, roll_radius(r, 1.0f), phi_at(r, 1.0f), cx, cy, 1.0f - 0.45f * e, roll_angle(r, 1.0f) * (PI_F / 180.0f), 1.0f - e, NULL);
}

// falls in from a little larger (shadow and all), there by 80% of the time, then one frame pressed flat before it rests
void roll_roll_drop(Surf *s, const RollStrip *r, float t)
{
    t = clamp01(t);
    float fall = clamp01(t / 0.8f), e = ease_in(fall), cx, cy;
    float k = 1.0f + 0.22f * (1.0f - e), alpha = clamp01(fall * 3.0f);
    if (t >= 0.8f && t < 0.92f) k = 0.94f;
    roll_centre(r, 0.0f, &cx, &cy);
    roll_render(s, r, roll_radius(r, 0.0f), 0.0f, cx, cy, k, roll_angle(r, 0.0f) * (PI_F / 180.0f), alpha, NULL);
}

// ------------------------------------------------------------------------------------------------ the peel
// the finished strip slides up and to the right, turns a little about its left end and fades: translate(10px, -18px) rotate(-3deg)
void roll_peel_draw(Surf *s, const RollStrip *r, float t, const RollRect *clip)
{
    float e = ease_in(clamp01(t)), alpha = 1.0f - e;
    if (alpha <= 0.0f) return;
    float turn = -3.0f * e * (PI_F / 180.0f), ct = pcos(turn), st = psin(turn);
    float mx = r->sx_mid - r->ox, my = r->sy_mid - r->oy; // the strip's middle about its left end
    float cx = r->ox + ct * mx - st * my + 10.0f * e, cy = r->oy + st * mx + ct * my - 18.0f * e;
    const PaperSpr *sp = r->strip;
    float hx = sp->w * 0.5f, hy = sp->h * 0.5f, ext_x = fabsf(ct) * hx + fabsf(st) * hy, ext_y = fabsf(st) * hx + fabsf(ct) * hy;
    int ax0, ay0, ax1, ay1;
    work_area(s, clip, &ax0, &ay0, &ax1, &ay1);
    int x0 = imax((int)floorf(cx - ext_x) - 1, ax0), x1 = imin((int)floorf(cx + ext_x) + 2, ax1);
    int y0 = imax((int)floorf(cy - ext_y) - 1, ay0), y1 = imin((int)floorf(cy + ext_y) + 2, ay1);
    // the picture's own anchor is its middle (W / 2, H / 2): the pixel centre of sprite pixel i is i + .5
    for (int y = y0; y < y1; y++) {
        uint8_t *d = s->px + ((size_t)y * s->w + x0) * 4;
        for (int x = x0; x < x1; x++, d += 4) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
            float fx = (dx * ct + dy * st) + hx - 0.5f, fy = (-dx * st + dy * ct) + hy - 0.5f;
            int ix = (int)floorf(fx), iy = (int)floorf(fy);
            if (ix < -1 || iy < -1 || ix >= sp->w || iy >= sp->h) continue;
            float tx = fx - ix, ty = fy - iy, acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    int px = ix + i, py = iy + j;
                    if (px < 0 || py < 0 || px >= sp->w || py >= sp->h) continue;
                    float w = (i ? tx : 1 - tx) * (j ? ty : 1 - ty);
                    const uint8_t *q = sp->px + ((size_t)py * sp->w + px) * 4;
                    float a = q[3] * (1.0f / 255.0f) * w;
                    acc[0] += q[0] * a; acc[1] += q[1] * a; acc[2] += q[2] * a; acc[3] += a;
                }
            float a = acc[3] * alpha;
            if (a < 0.002f) continue;
            for (int q = 0; q < 3; q++) {
                float c = acc[q] / acc[3]; // straight colour of the picture here
                d[q] = (uint8_t)clampf(d[q] + (c - d[q]) * a + 0.5f, 0, 255);
            }
            d[3] = 255;
        }
    }
}

// ------------------------------------------------------------------------------------------------ what changed, and what could
// the screen box of the roll and its shadow at progress p, turned with the strip (any_turn: whatever way it might be turned, for the hop)
static RollRect roll_rect_at(const RollStrip *r, float p, int any_turn)
{
    float cx, cy, R = roll_radius(r, p), ang = roll_angle(r, p) * (PI_F / 180.0f), hw = R + SH_PAD, hh = r->rb * 0.5f + SH_PAD;
    float ca = fabsf(pcos(ang)), sa = fabsf(psin(ang)), ex = hw * ca + hh * sa, ey = hw * sa + hh * ca;
    if (any_turn) ex = ey = sqrtf(hw * hw + hh * hh);
    roll_centre(r, p, &cx, &cy);
    return rect_of(cx - ex, cy - ey, cx + ex, cy + ey);
}

// the screen box of the strip between two lengths along it (with room for the crimp's jog, the edge shadow and the zigzag)
static RollRect strip_rect_between(const RollStrip *r, float a0, float a1)
{
    float rr = r->rot * (PI_F / 180.0f), ca = pcos(rr), sa = psin(rr);
    float half = r->th * 0.5f + 6.0f + fabsf(r->ky) + r->len * fabsf(r->kr) * 0.0175f + 1.0f;
    float xs[4] = {a0, a1, a0, a1}, ys[4] = {-half, -half, half, half};
    float lx = 1e9f, ly = 1e9f, hx = -1e9f, hy = -1e9f;
    for (int i = 0; i < 4; i++) {
        float x = r->ox + ca * xs[i] - sa * ys[i], y = r->oy + sa * xs[i] + ca * ys[i];
        lx = minf(lx, x); hx = maxf(hx, x); ly = minf(ly, y); hy = maxf(hy, y);
    }
    return rect_of(lx, ly, hx, hy);
}

RollRect roll_dirty(const RollStrip *r, float p0, float p1)
{
    p0 = clamp01(p0);
    p1 = clamp01(p1);
    RollRect d = roll_rect_at(r, p0, 0);
    rrect_union(&d, roll_rect_at(r, p1, 0));
    float lo = minf(p0, p1), hi = maxf(p0, p1);
    if (hi > lo) rrect_union(&d, strip_rect_between(r, r->len * lo - 3.0f, hi >= 1.0f ? r->len + 16.0f : r->len * hi + 3.0f));
    return d;
}

RollRect roll_bounds(const RollStrip *r)
{
    const PaperSpr *sp = r->strip;
    RollRect b = {(int)floorf(r->sx_mid + 0.5f) - sp->ox, (int)floorf(r->sy_mid + 0.5f) - sp->oy, sp->w, sp->h};
    for (int i = 0; i <= 8; i++) rrect_union(&b, roll_rect_at(r, i / 8.0f, 1));
    RollRect hop = roll_rect_at(r, 1.0f, 1); // the hop moves it up to (8, -14), turned and smaller
    hop.x -= 4; hop.y -= 18; hop.w += 20; hop.h += 22;
    rrect_union(&b, hop);
    RollRect peel = {b.x - 2, b.y - 52, b.w + 16, b.h + 54}; // the peel: up 18, right 10, and the far end up another 29 as it turns
    rrect_union(&b, peel);
    b.x -= 8; b.y -= 8; b.w += 16; b.h += 16; // a catch's pull back and shiver
    return b;
}

// ------------------------------------------------------------------------------------------------ catches
int roll_crossed_catch(const RollStrip *r, float p0, float p1)
{
    p0 = clamp01(p0);
    p1 = clamp01(p1);
    for (int i = 0; i < r->ncatch; i++)
        if (p0 < r->catches[i].progress && p1 >= r->catches[i].progress) return i + 1;
    return 0;
}

int roll_catch_active(float ms) { return ms >= 0.0f && ms < ROLL_CATCH_MS; }

// the reaction: the roll is yanked back a few px (out in 55 ms, home by 170), shivers sideways and in its turn (about 28 Hz, dying away), and
// the tape bunches up just behind it (a ridge that comes up in 40 ms and smooths out)
RollFx roll_catch_fx(const RollStrip *r, int index, float ms)
{
    RollFx f = {0, 0, 0, 0};
    if (index < 0 || index >= r->ncatch || !roll_catch_active(ms)) return f;
    float s = r->catches[index].strength, back, decay = 1.0f - ms / ROLL_CATCH_MS, pk;
    decay *= decay;
    if (ms < 55.0f) back = smooth01(ms / 55.0f);
    else if (ms < 170.0f) back = 1.0f - smooth01((ms - 55.0f) / 115.0f);
    else back = 0.0f;
    pk = ms < 40.0f ? ms / 40.0f : 1.0f - smooth01((ms - 40.0f) / (ROLL_CATCH_MS - 40.0f));
    f.back_px = 3.5f * s * back;
    f.shiver_px = 1.3f * s * psin(ms * 0.176f) * decay;
    f.shiver_deg = 2.0f * s * psin(ms * 0.176f + 1.0f) * decay;
    f.pucker = s * pk;
    return f;
}

float roll_catch_progress(const RollStrip *r, int index, float p, float ms)
{
    p = clamp01(p);
    if (index < 0 || index >= r->ncatch || ms < 0.0f) return p;
    float pc = r->catches[index].progress;
    if (p <= pc) return p;
    if (ms < ROLL_CATCH_STALL_MS) return pc;
    return pc + (p - pc) * smooth01((ms - ROLL_CATCH_STALL_MS) / 140.0f);
}

RollRect roll_dirty_catch(const RollStrip *r, float progress)
{
    RollRect d = roll_rect_at(r, progress, 0);
    d.x -= 6; d.y -= 6; d.w += 12; d.h += 12; // back, shiver
    float a = r->len * clamp01(progress);
    rrect_union(&d, strip_rect_between(r, a - roll_radius(r, progress) - 12.0f, a));
    return d;
}
