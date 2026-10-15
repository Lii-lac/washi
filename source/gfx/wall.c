// The intro's tape wall (see wall.h): tape.js, roll.js and ribbon.js of the intro mockup, in C. The numbers are the page's, line for line.
#include "wall.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define PI_F 3.14159265358979f

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline float pmod(float x, float p) { return x - floorf(x / p) * p; }

#ifdef UI_INTRO_PROF // (PC builds only: where a peel's and a roll's time goes)
#include <time.h>
double g_wall_peel_ms[4], g_roll_ms[3];
static double peel_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#define PEEL_T(i, t0) (g_wall_peel_ms[i] += peel_ms() - (t0), (t0) = peel_ms())
#else
#define PEEL_T(i, t0) ((void)0)
#endif

// The wall's sprites are made on the console's other cores while the app starts (ui_intro.c), so they are allocated here, not by paper_spr_new (which counts
// what it holds and keeps the "still working" mark moving: neither may run off the main thread). Free them with wall_spr_free.
PaperSpr *wall_spr_new(int w, int h)
{
    PaperSpr *p = malloc(sizeof *p);
    if (!p) return NULL;
    p->w = w;
    p->h = h;
    p->ox = p->oy = 0;
    p->px = calloc((size_t)w * h, 4);
    if (!p->px) {
        free(p);
        return NULL;
    }
    return p;
}

void wall_spr_free(PaperSpr *p)
{
    if (!p) return;
    free(p->px);
    free(p);
}

#ifdef WALL_PROF // (the PC timing of the strip maker: tools only)
#include <time.h>
double g_wall_prof[4];
static double wall_prof_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}
#endif

// ------------------------------------------------------------------------------------------------ numbers (tape.js)

static uint32_t rng_u32(WallRng *r)
{
    uint32_t s = r->s;
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return r->s = s;
}

void wall_rng_seed(WallRng *r, uint32_t seed)
{
    uint32_t s = seed * 2654435761u + 0x9E3779B9u;
    if (!s) s = 0x6D2B79F5u;
    r->s = s;
    for (int i = 0; i < 6; i++) rng_u32(r);
}

float wall_rng(WallRng *r) { return (float)(rng_u32(r) >> 8) * (1.0f / 16777216.0f); }

static uint32_t thash(int x, int y, uint32_t seed)
{
    uint32_t h = (seed * 0x9E3779B1u) ^ ((uint32_t)x * 0x85EBCA6Bu) ^ ((uint32_t)y * 0xC2B2AE35u);
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}

float wall_hashf(int x, int y, uint32_t seed) { return (float)(thash(x, y, seed) >> 8) * (1.0f / 16777216.0f); }

float wall_smooth(float t)
{
    t = clamp01(t);
    return t * t * (3.0f - 2.0f * t);
}

float wall_vnoise(float x, float y, uint32_t seed)
{
    float fx = floorf(x), fy = floorf(y), tx = wall_smooth(x - fx), ty = wall_smooth(y - fy);
    int ix = (int)fx, iy = (int)fy;
    float a = wall_hashf(ix, iy, seed), b = wall_hashf(ix + 1, iy, seed), c = wall_hashf(ix, iy + 1, seed), d = wall_hashf(ix + 1, iy + 1, seed);
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}

// how much of the pixel at (px, py) a line w wide from (x0, y0) to (x1, y1) covers; soft = how many pixels its edge fades over
static float seg_cov(float px, float py, float x0, float y0, float x1, float y1, float w, float soft)
{
    float vx = x1 - x0, vy = y1 - y0, wx = px - x0, wy = py - y0, l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? clamp01((wx * vx + wy * vy) / l2) : 0, dx = wx - vx * t, dy = wy - vy * t, sf = soft > 0 ? soft : 1;
    return clamp01((w * 0.5f + sf * 0.5f - sqrtf(dx * dx + dy * dy)) / sf);
}

float wall_pat_white(int kind, float u, float v, float L, float H)
{
    const float D = 0.70710678f;
    switch (kind) {
    case 0: { float t = (u - L * 0.5f) * D - (v - H * 0.5f) * D + (L + H) * D * 0.5f; return pmod(t, 12) < 5 ? 0.6f : 0; }
    case 1: { float du = pmod(u, 12) - 6, dv = pmod(v, 12) - 6, d = sqrtf(du * du + dv * dv); return 0.85f * clamp01((2.7f - d) / 0.3f); }
    case 2: return pmod(u, 12) < 1.4f || pmod(v, 12) < 1.4f ? 0.7f : 0;
    case 3: { float mu = pmod(u, 12), mv = pmod(v, 12); return mu + mv < 6 || 12 - mu + mv < 6 ? 0.7f : 0; }
    case 4: { float mu = pmod(u, 14), mv = pmod(v, 14); return (mu >= 7) != (mv >= 7) ? 0.7f : 0; }
    case 5: { float du = pmod(u, 12) - 6, dv = pmod(v, 9) - 9, d = sqrtf(du * du + dv * dv); return 0.78f * clamp01((4.7f - d) / 0.5f); }
    case 6: return pmod(H - v, 6) < 2 ? 0.7f : 0;
    case 7: { float t = -(u - L * 0.5f) * D - (v - H * 0.5f) * D + (L + H) * D * 0.5f; return pmod(t, 20) < 9 ? 0.62f : 0; }
    }
    return 0;
}

// ------------------------------------------------------------------------------------------------ the strip (tape.js tapeMake, tapeTexture)

static const float T_ALPHA = 0.70f, T_BR[3] = {0.16f, 0.11f, 0.03f};
static const float T_RIDGE_T[5] = {0, 0.24f, 0.52f, 0.78f, 1};
static const float T_RIDGE_S[5][4] = {{0, 0, 0, 0}, {0.95f, 0.95f, 0.95f, 0.95f}, {0.27f * 0.46f, 0.19f * 0.46f, 0.10f * 0.46f, 0.46f}, {0.6f, 0.6f, 0.6f, 0.6f}, {0, 0, 0, 0}};

static inline void over4(float d[4], const float s[4])
{
    float k = 1.0f - s[3];
    for (int c = 0; c < 4; c++) d[c] = s[c] + d[c] * k;
}

static float nick_reach(const WallNick *n) { return n->w * 0.5f * (1.0f + fabsf(n->sk)); }

static float nick_depth(const WallNick *n, float xa)
{
    float t = xa - n->x, half = n->w * 0.5f * (t < 0 ? 1 + n->sk : 1 - n->sk), f = 1.0f - fabsf(t) / half;
    if (f <= 0) return 0;
    float rough = 1.0f + n->j * (wall_hashf((int)floorf(xa * 7.0f), 3, n->sd ? n->sd : 1) - 0.5f);
    return n->d * powf(f, n->e > 0 ? n->e : 0.75f) * rough;
}

void wall_tape_defaults(WallTapeOpt *o)
{
    memset(o, 0, sizeof *o);
    o->k = 1;
    o->strength = 1;
    o->alpha = T_ALPHA;
    o->host[0] = 228 / 255.0f;
    o->host[1] = 221 / 255.0f;
    o->host[2] = 204 / 255.0f;
    o->patch = o->edge = o->edge_w = o->dark_tint = 1;
    o->ridge = o->wrinkles = o->jog = 1;
    o->fib_n = o->fib_l = o->fib_d = o->fib_len = o->fib_soft = 1;
}

typedef struct {
    const WallTapeOpt *o;
    float L, h, rgb[3], cr, sr, seal_w;
    int crimp;
    float sx, sk, cx, ky, kr;
    float m_y, m_x;      // how far from the long edges and the ends a pixel must be to need only one look
    float pr_near, pr_far; // the "press" noise at the pixel being made, in each half's own frame (it changes over tens of px, so once a pixel is enough)
} TapeCtx;

static int nicked(const TapeCtx *t, float xa, float yl)
{
    for (int i = 0; i < t->o->nnicks; i++) {
        const WallNick *n = &t->o->nicks[i];
        if (fabsf(xa - n->x) >= nick_reach(n)) continue;
        float dep = nick_depth(n, xa);
        if (dep > 0 && (n->top ? yl < dep : yl > t->h - dep)) return 1;
    }
    return 0;
}

static void shade(const TapeCtx *t, float u, float v, float Lp, float Hp, float fib, float press_noise, float oo[4])
{
    const WallTapeOpt *o = t->o;
    float pa = wall_pat_white(o->kind, u, v, Lp, Hp), kk = 1 + 0.02f * fib;
    float a = o->alpha * (1 - 0.02f * (fib * 0.5f + 0.5f));
    if (o->press > 0) a += (press_noise - 0.5f) * o->press; // (tape.js looks the noise up at (u .035 + 5.5, v .08 + 1.7) in the half's own frame)
    if (a > 0.93f) a = 0.93f;
    if (pa > 0) a += (0.94f - a) * clamp01(pa / 0.8f);
    for (int c = 0; c < 3; c++) oo[c] = clamp01((t->rgb[c] * (1 - pa) + pa) * kk) * a + o->host[c] * (1 - a);
    oo[3] = 1;
}

static void ridge(const TapeCtx *t, float lx, float ly, float r[4])
{
    float sk = t->sk, h = t->h, kq = (2 * sk) / h, a = 8 - sk, xs = lx - (t->cx - 8) - kq * ly, tt = (xs - (a - 5)) / 10;
    r[0] = r[1] = r[2] = r[3] = 0;
    if (xs < a - 9 || xs > a + 10) return; // (the band and its wrinkles all lie within a few px of the slanted cut)
    if (tt > 0 && tt < 1 && ly >= 0 && ly <= h) {
        int i = 0;
        while (i < 3 && tt > T_RIDGE_T[i + 1]) i++;
        float f = (tt - T_RIDGE_T[i]) / (T_RIDGE_T[i + 1] - T_RIDGE_T[i]);
        for (int c = 0; c < 4; c++) r[c] = lerpf(T_RIDGE_S[i][c], T_RIDGE_S[i + 1][c], f);
    }
    float wa0 = t->o->wrinkles;
    if (wa0 <= 0) return;
    float m = h * 0.5f, ux = xs;
    float w1 = fmaxf(fmaxf(seg_cov(ux, ly, a - 7, m - 8, a - 2, m - 5, 1.1f, 1), seg_cov(ux, ly, a + 7, m + 7, a + 2, m + 4, 1.1f, 1)), seg_cov(ux, ly, a - 6, m + 6, a - 2, m + 4, 1.1f, 1)) * 0.8f;
    float w2 = seg_cov(ux, ly, a + 6, m - 7, a + 2, m - 4, 1.0f, 1) * 0.4f;
    float wa = wa0 * w1, s1[4] = {wa, wa, wa, wa};
    over4(r, s1);
    float wb = wa0 * w2, s2[4] = {0.27f * wb, 0.19f * wb, 0.10f * wb, wb};
    over4(r, s2);
}

static void stuck(const TapeCtx *t, float lx, float ly, float fib, float oo[4])
{
    oo[0] = oo[1] = oo[2] = oo[3] = 0;
    float L = t->L, h = t->h;
    if (!t->crimp) {
        if (ly < 0 || ly > h) return;
        if (lx >= 0 && lx <= L && !nicked(t, lx, ly)) shade(t, lx, ly, L, h, fib, t->pr_near, oo);
        return;
    }
    float dx = lx - (t->sx - 1), dy = ly - h * 0.5f - t->ky, bu = dx * t->cr + dy * t->sr, bv = -dx * t->sr + dy * t->cr + h * 0.5f, wb = L - t->sx + 1;
    if (bv >= 0 && bv <= h && bu >= 1 + (2 * t->sk * bv) / h && bu <= wb) {
        if (!nicked(t, bu + t->sx - 1, bv)) shade(t, bu, bv, wb, h, fib, t->pr_far, oo);
    } else if (ly >= 0 && ly <= h && lx >= 0 && lx <= t->sx + (2 * t->sk * ly) / h + t->seal_w) {
        if (!nicked(t, lx, ly)) shade(t, lx, ly, L, h, fib, t->pr_near, oo);
    }
    if (oo[3] > 0) {
        float rr[4];
        ridge(t, lx, ly, rr);
        if (t->o->ridge != 1.0f)
            for (int c = 0; c < 4; c++) rr[c] *= t->o->ridge;
        over4(oo, rr);
    }
}

// a pixel well inside the strip, with the pattern the same at its middle and corners, needs one look instead of nine. c00..c11: the pattern at the pixel's four
// corners (the rows of corners are shared between neighbouring pixels, so the caller looks each corner up once); p0 the pattern at its middle
static int flat_at(const TapeCtx *t, float lx, float ly, float p0, float c00, float c10, float c01, float c11)
{
    float mY = t->m_y, L = t->L, h = t->h;
    if (ly < mY || ly > h - mY || lx < t->m_x || lx > L - t->m_x) return 0;
    // a pressed-down strip: only its slanted cut (and the sliver closed over it) has a hard edge across the strip; the ridge itself is a soft band
    if (t->crimp && fabsf(lx - (t->sx + (2 * t->sk * ly) / h)) < 2.0f + t->seal_w) return 0;
    for (int i = 0; i < t->o->nnicks; i++) {
        const WallNick *n = &t->o->nicks[i];
        if (fabsf(lx - n->x) < nick_reach(n) + 2 && (n->top ? ly < n->d * 1.3f + 2.5f : ly > h - n->d * 1.3f - 2.5f)) return 0;
    }
    // the pattern: a hard edge (stripes, checks: a jump of more than a tenth) needs the nine looks; the dots' and scallops' own soft rims do not
    const float tol = 0.1f;
    return fabsf(c00 - p0) < tol && fabsf(c10 - p0) < tol && fabsf(c01 - p0) < tol && fabsf(c11 - p0) < tol;
}

// tape_texture: how the strip lies on the paper. S is premultiplied, W x H x 4 floats; the strip's top left is at (pad, pad)
static int tape_texture(float *S, int W, int H, int pad, const TapeCtx *t)
{
    const WallTapeOpt *o = t->o;
    float k = o->k, invk = 1.0f / k, L = t->L, h = t->h;
    size_t N = (size_t)W * H;
    float *dist = malloc(N * sizeof(float)), *al = malloc(N * sizeof(float)), *fl = calloc(N, sizeof(float)), *fd = calloc(N, sizeof(float));
    if (!dist || !al || !fl || !fd) {
        free(dist);
        free(al);
        free(fl);
        free(fd);
        return -1;
    }
    for (size_t i = 0; i < N; i++) {
        al[i] = S[i * 4 + 3];
        dist[i] = al[i] > 0.5f ? 1e4f : 0;
    }
    for (int y = 0; y < H; y++) // how far inside the strip each pixel is (a chamfer distance from the nearest pixel that is not strip)
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)y * W + x;
            if (dist[i] == 0) continue;
            float d = dist[i];
            if (x == 0 || y == 0) d = 1;
            if (x > 0) d = fminf(d, dist[i - 1] + 1);
            if (y > 0) d = fminf(d, dist[i - W] + 1);
            if (x > 0 && y > 0) d = fminf(d, dist[i - W - 1] + 1.4142f);
            if (x < W - 1 && y > 0) d = fminf(d, dist[i - W + 1] + 1.4142f);
            dist[i] = d;
        }
    for (int y = H - 1; y >= 0; y--)
        for (int x = W - 1; x >= 0; x--) {
            size_t i = (size_t)y * W + x;
            if (dist[i] == 0) continue;
            float d = dist[i];
            if (x == W - 1 || y == H - 1) d = 1;
            if (x < W - 1) d = fminf(d, dist[i + 1] + 1);
            if (y < H - 1) d = fminf(d, dist[i + W] + 1);
            if (x < W - 1 && y < H - 1) d = fminf(d, dist[i + W + 1] + 1.4142f);
            if (x > 0 && y < H - 1) d = fminf(d, dist[i + W - 1] + 1.4142f);
            dist[i] = d;
        }
    // the fibres: short, faint flecks, mostly along the strip, a few across it; each is laid into a lighter and a darker plane over the few pixels it touches
    WallRng R;
    wall_rng_seed(&R, o->seed ^ 0xF1B7Eu);
    int nf = (int)floorf((L * h) / 70.0f);
    nf = nf < 12 ? 12 : nf > 90 ? 90 : nf;
    nf = (int)lroundf((float)nf * o->fib_n);
    for (int q = 0; q < nf; q++) {
        float cxf = wall_rng(&R) * L, cyf = wall_rng(&R) * h;
        float ang = (wall_rng(&R) - 0.5f) * 0.9f;
        if (wall_rng(&R) < 0.12f) ang += 1.5708f;
        float len = 3 + wall_rng(&R) * 5.5f, fk;
        if (wall_rng(&R) < 0.55f) fk = 0.07f + 0.06f * wall_rng(&R);
        else fk = -(0.04f + 0.035f * wall_rng(&R));
        len *= o->fib_len;
        fk *= fk > 0 ? o->fib_l : o->fib_d;
        float dx = cosf(ang) * len * 0.5f, dy = sinf(ang) * len * 0.5f;
        float p0x = (cxf - dx) * k + pad, p0y = (cyf - dy) * k + pad, p1x = (cxf + dx) * k + pad, p1y = (cyf + dy) * k + pad;
        int mg = (int)ceilf(0.35f * k + o->fib_soft * 0.5f) + 1;
        int x0 = (int)floorf(fminf(p0x, p1x)) - mg, x1 = (int)ceilf(fmaxf(p0x, p1x)) + mg, y0 = (int)floorf(fminf(p0y, p1y)) - mg, y1 = (int)ceilf(fmaxf(p0y, p1y)) + mg;
        float *pl = fk > 0 ? fl : fd, kv = fabsf(fk);
        for (int y = imax(0, y0); y <= y1 && y < H; y++)
            for (int x = imax(0, x0); x <= x1 && x < W; x++) {
                float c = seg_cov(x + 0.5f, y + 0.5f, p0x, p0y, p1x, p1y, 0.7f * k, o->fib_soft) * kv;
                size_t i = (size_t)y * W + x;
                if (c > pl[i]) pl[i] = c;
            }
    }
    for (int y = 1; y < H - 1; y++) {
        float m0 = 0, m1 = 0; // the patches' noise at every 4th pixel, straight lines between (it changes over tens of px)
        for (int x = 1; x < W - 1; x++) {
            size_t i = (size_t)y * W + x;
            if (((x - 1) & 3) == 0) {
                float lya = (y + 0.5f - pad) * invk, lxa = (x + 0.5f - pad) * invk, lxb = (x + 4.5f - pad) * invk;
                m0 = wall_vnoise(lxa * 0.085f + 3.1f, lya * 0.085f + 7.7f, o->seed ^ 0x3077u) * 2 - 1;
                m1 = wall_vnoise(lxb * 0.085f + 3.1f, lya * 0.085f + 7.7f, o->seed ^ 0x3077u) * 2 - 1;
            }
            float a = S[i * 4 + 3];
            if (a < 0.02f) continue;
            float lx = (x + 0.5f - pad) * invk, ly = (y + 0.5f - pad) * invk, e = fmaxf(dist[i] * invk - 0.5f * invk, 0);
            float lift = fl[i], dark = fd[i];
            float m = m0 + (m1 - m0) * (float)((x - 1) & 3) * 0.25f; // the patches
            lift += 0.10f * o->patch * fmaxf(m, 0);
            dark += 0.07f * o->patch * fmaxf(-m, 0);
            float gx = al[i + 1] - al[i - 1], gy = al[i + W] - al[i - W], gl = sqrtf(gx * gx + gy * gy); // the edge: which way it faces, against a light from the top left
            if (gl > 0.05f) {
                float lit = ((gx + gy) * 0.7071f) / gl, wedge = 1 - wall_smooth(e / (1.3f * o->edge_w));
                lift += 0.22f * o->edge * wedge * fmaxf(lit, 0);
                dark += 0.16f * o->edge * wedge * fmaxf(-lit, 0);
            }
            for (int n = 0; n < o->nnicks; n++) { // a nick's edge shows the pale inside of the paper where it was torn
                const WallNick *nk = &o->nicks[n];
                if (fabsf(lx - nk->x) < nick_reach(nk) + 1.5f && (nk->top ? ly < nk->d * 1.3f + 1.8f : ly > h - nk->d * 1.3f - 1.8f) && e < 1.4f) lift += 0.30f * (1 - wall_smooth(e / 1.4f));
            }
            lift = fminf(lift * o->strength, 0.6f);
            dark = fminf(dark * o->strength, 0.5f);
            for (int c = 0; c < 3; c++) {
                float v = S[i * 4 + c];
                v = v * (1 - lift) + lift * a;
                S[i * 4 + c] = v * (1 - dark) + T_BR[c] * dark * a * o->dark_tint;
            }
        }
    }
    free(dist);
    free(al);
    free(fl);
    free(fd);
    return 0;
}

int wall_tape_make(const WallTapeOpt *o, WallTape *out)
{
    memset(out, 0, sizeof *out);
    TapeCtx t;
    memset(&t, 0, sizeof t);
    t.o = o;
    t.L = o->L1;
    t.h = o->h1;
    uint32_t tc = paper_tape_rgb[o->colour & 7];
    t.rgb[0] = ((tc >> 16) & 255) / 255.0f;
    t.rgb[1] = ((tc >> 8) & 255) / 255.0f;
    t.rgb[2] = (tc & 255) / 255.0f;
    t.sk = 4;
    if (o->crimp && t.L >= 36) {
        WallRng r;
        wall_rng_seed(&r, o->seed ^ 0xC12117u);
        t.crimp = 1;
        t.cx = t.L * (0.3f + 0.38f * wall_rng(&r));
        t.sx = t.cx - t.sk;
        float ks = wall_rng(&r) < 0.5f ? -1.0f : 1.0f;
        t.ky = ks * (1.2f + 1.6f * wall_rng(&r)) * o->jog;
        t.kr = (wall_rng(&r) * 2 - 1) * 0.3f * o->jog;
    }
    float k = o->k, invk = 1.0f / k, L = t.L, h = t.h;
    t.cr = cosf(t.kr * PI_F / 180.0f);
    t.sr = sinf(t.kr * PI_F / 180.0f);
    t.seal_w = o->seal && t.crimp ? (2 * t.sk * fabsf(t.ky)) / h + 0.8f : 0;
    // one look is enough for a pixel more than half a 1x px from every edge: the long edges (the far half of a pressed-down strip lies a little off them), the ends
    // (tape.js keeps 1.8 + 3.6 px and 2.5 px, which at 4.5 times the size is many rows of nine looks each; the closer margins give the same picture)
    t.m_y = 0.25f + (t.crimp ? fabsf(t.ky) + (L - t.sx) * fabsf(t.sr) + 0.1f : 0);
    t.m_x = 0.25f;
    int pad = (int)ceilf(5.5f * k) + 2, W = (int)ceilf(L * k) + 2 * pad, H = (int)ceilf(h * k) + 2 * pad;
    float *S = calloc((size_t)W * H * 4, sizeof(float));
    if (!S) return -1;
#ifdef WALL_PROF
    double pt0 = wall_prof_now();
#endif
    float out4[4], acc[4];
    const int ss = 3;
    const float inv = 1.0f / (ss * ss);
    float *crow = malloc((size_t)(W + 1) * 2 * sizeof(float)); // the pattern at the corners of the pixels: the row above and the row below the current one
    if (!crow) {
        free(S);
        return -1;
    }
    float *top = crow, *bot = crow + W + 1;
    int have_top = 0;
    for (int y = 0; y < H; y++) {
        float lyc = (y + 0.5f - pad) * invk;
        if (lyc < -2.5f || lyc > h + 2.5f) {
            have_top = 0;
            continue;
        }
        float ly0 = (y - pad) * invk, ly1 = (y + 1 - pad) * invk;
        for (int x = 0; x <= W; x++) {
            float lx = (x - pad) * invk;
            if (!have_top) top[x] = wall_pat_white(o->kind, lx, ly0, L, h);
            bot[x] = wall_pat_white(o->kind, lx, ly1, L, h);
        }
        have_top = 1;
        float pn0 = 0, pn1 = 0, pf0 = 0, pf1 = 0;
        // a row wholly off the strip (beyond the edges by more than the margin) has nothing to make
        if (lyc < -t.m_y - invk || lyc > h + t.m_y + invk) {
            float *sw0 = top;
            top = bot;
            bot = sw0;
            continue;
        }
        for (int x = 0; x < W; x++) {
            float lxc = (x + 0.5f - pad) * invk;
            if (lxc < -1 || lxc > L + 1) continue;
            if (lxc < -t.m_x - invk || lxc > L + t.m_x + invk) continue;
            float fib = wall_hashf(x, y, o->seed ^ 0xF1B3u) * 2 - 1;
            float *d = S + ((size_t)y * W + x) * 4;
            if (o->press > 0) { // the press noise at every 4th pixel, straight lines between (it changes over a hundred px here)
                if ((x & 3) == 0 || x == 0) {
                    float lxa = lxc, lxb = lxc + 4 * invk;
                    pn0 = wall_vnoise(lxa * 0.035f + 5.5f, lyc * 0.08f + 1.7f, o->seed ^ 0x9E55u);
                    pn1 = wall_vnoise(lxb * 0.035f + 5.5f, lyc * 0.08f + 1.7f, o->seed ^ 0x9E55u);
                    if (t.crimp) {
                        float dy = lyc - h * 0.5f - t.ky, dxa = lxa - (t.sx - 1), dxb = lxb - (t.sx - 1);
                        pf0 = wall_vnoise((dxa * t.cr + dy * t.sr) * 0.035f + 5.5f, (-dxa * t.sr + dy * t.cr + h * 0.5f) * 0.08f + 1.7f, o->seed ^ 0x9E55u);
                        pf1 = wall_vnoise((dxb * t.cr + dy * t.sr) * 0.035f + 5.5f, (-dxb * t.sr + dy * t.cr + h * 0.5f) * 0.08f + 1.7f, o->seed ^ 0x9E55u);
                    }
                }
                float f = (float)(x & 3) * 0.25f;
                t.pr_near = pn0 + (pn1 - pn0) * f;
                t.pr_far = pf0 + (pf1 - pf0) * f;
            }
            float p0 = wall_pat_white(o->kind, lxc, lyc, L, h);
            if (flat_at(&t, lxc, lyc, p0, top[x], top[x + 1], bot[x], bot[x + 1])) {
                stuck(&t, lxc, lyc, fib, out4);
                memcpy(d, out4, sizeof out4);
#ifdef WALL_PROF
                g_wall_prof[2] += 1;
#endif
                continue;
            }
#ifdef WALL_PROF
            g_wall_prof[3] += 1;
#endif
            acc[0] = acc[1] = acc[2] = acc[3] = 0;
            for (int j = 0; j < ss; j++)
                for (int i = 0; i < ss; i++) {
                    stuck(&t, (x + (i + 0.5f) / ss - pad) * invk, (y + (j + 0.5f) / ss - pad) * invk, fib, out4);
                    acc[0] += out4[0];
                    acc[1] += out4[1];
                    acc[2] += out4[2];
                    acc[3] += out4[3];
                }
            for (int c = 0; c < 4; c++) d[c] = acc[c] * inv;
        }
        float *sw = top; // (this row's bottom corners are the next row's top ones)
        top = bot;
        bot = sw;
    }
    free(crow);
#ifdef WALL_PROF
    double pt1 = wall_prof_now();
#endif
    if (tape_texture(S, W, H, pad, &t) != 0) {
        free(S);
        return -1;
    }
#ifdef WALL_PROF
    double pt2 = wall_prof_now();
    g_wall_prof[0] += pt1 - pt0;
    g_wall_prof[1] += pt2 - pt1;
#endif
    PaperSpr *p = wall_spr_new(W, H);
    if (!p) {
        free(S);
        return -1;
    }
    for (size_t i = 0, N = (size_t)W * H; i < N; i++) {
        float a = S[i * 4 + 3];
        if (a < 1.0f / 512) continue;
        float q = 255.0f / a;
        uint8_t *px = p->px + i * 4;
        for (int c = 0; c < 3; c++) {
            float v = S[i * 4 + c] * q + 0.5f;
            px[c] = (uint8_t)(v > 255 ? 255 : v);
        }
        float av = a * 255 + 0.5f;
        px[3] = (uint8_t)(av > 255 ? 255 : av);
    }
    free(S);
    p->ox = W / 2;
    p->oy = H / 2;
    out->spr = p;
    out->pad = pad;
    out->crimp = t.crimp;
    out->sx = t.sx;
    out->sk = t.sk;
    out->cx = t.cx;
    out->ky = t.ky;
    out->kr = t.kr;
    return 0;
}

// ------------------------------------------------------------------------------------------------ the roll (roll.js)

WallTex *wall_roll_tex(int colour, int kind, float k, float rb, float circ0)
{
    int w = (int)ceilf(circ0 * k), h = (int)ceilf(rb * k);
    WallTex *t = calloc(1, sizeof *t);
    if (!t) return NULL;
    t->px = malloc((size_t)w * h * 4);
    if (!t->px) {
        free(t);
        return NULL;
    }
    t->w = w;
    t->h = h;
    uint32_t tc = paper_tape_rgb[colour & 7];
    int r = (tc >> 16) & 255, g = (tc >> 8) & 255, b = tc & 255;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float wh = wall_pat_white(kind, (x + 0.5f) / k, (y + 0.5f) / k, circ0, rb);
            uint8_t *p = t->px + ((size_t)y * w + x) * 4;
            p[0] = (uint8_t)lroundf(r + (255 - r) * wh);
            p[1] = (uint8_t)lroundf(g + (255 - g) * wh);
            p[2] = (uint8_t)lroundf(b + (255 - b) * wh);
            p[3] = 255;
        }
    return t;
}

void wall_roll_tex_free(WallTex *t)
{
    if (!t) return;
    free(t->px);
    free(t);
}

void wall_roll_phase(float len1, float r0, float r1, float ph[65])
{
    ph[0] = 0;
    double acc = 0;
    for (int k = 1; k <= 64; k++) {
        for (int j = 0; j < 8; j++) {
            double e = (k - 1 + (j + 0.5) / 8) / 64;
            acc += len1 / 64.0 / 8.0 / (r0 + (r1 - r0) * e);
        }
        ph[k] = (float)((acc / (2 * 3.14159265358979)) * WALL_ROLL_N);
    }
}

float wall_roll_phi(const float ph[65], float p)
{
    float f = clamp01(p) * 64;
    int i = (int)floorf(f);
    float t = i >= 64 ? ph[64] : lerpf(ph[i], ph[i + 1], f - (float)i);
    return (t / WALL_ROLL_N) * 2 * PI_F;
}

// the light across the roll (roll.js ROLL_SHADE): colour and alpha at x = 0..1 along it (a canvas gradient between straight colours, premultiplied interpolation)
typedef struct { float t, r, g, b, a; } Stop;
static const Stop ROLL_SHADE[9] = {{0, 50, 30, 10, 0.5f}, {0.14f, 50, 30, 10, 0.08f}, {0.30f, 50, 30, 10, 0}, {0.3001f, 255, 255, 255, 0}, {0.38f, 255, 255, 255, 0.5f},
                                   {0.52f, 255, 255, 255, 0}, {0.5201f, 50, 30, 10, 0}, {0.72f, 50, 30, 10, 0.1f}, {1, 50, 30, 10, 0.55f}};
static void shade_at(float t, float out[4]) // premultiplied rgb (0..255) and alpha
{
    int i = 0;
    while (i < 7 && t > ROLL_SHADE[i + 1].t) i++;
    const Stop *a = &ROLL_SHADE[i], *b = &ROLL_SHADE[i + 1];
    float f = clamp01((t - a->t) / (b->t - a->t));
    out[3] = lerpf(a->a, b->a, f);
    out[0] = lerpf(a->r * a->a, b->r * b->a, f);
    out[1] = lerpf(a->g * a->a, b->g * b->a, f);
    out[2] = lerpf(a->b * a->a, b->b * b->a, f);
}

void wall_roll_light(float x, float out[4]) { shade_at(clamp01(x), out); }

static int scratch_fit(WallScratch *sc, int n)
{
    if (sc->cap >= n) return 1;
    free(sc->a);
    free(sc->b);
    sc->a = malloc((size_t)n * 4);
    sc->b = malloc((size_t)n * 4);
    sc->cap = sc->a && sc->b ? n : 0;
    return sc->cap > 0;
}

void wall_scratch_free(WallScratch *sc)
{
    free(sc->a);
    free(sc->b);
    memset(sc, 0, sizeof *sc);
}

// a rounded rectangle's coverage at (x, y) (pixel centre), corners of radius r
static float rrect_cov(float x, float y, float x0, float y0, float x1, float y1, float r)
{
    float cx = x < x0 + r ? x0 + r : x > x1 - r ? x1 - r : x, cy = y < y0 + r ? y0 + r : y > y1 - r ? y1 - r : y;
    float dx = x - cx, dy = y - cy, d = sqrtf(dx * dx + dy * dy);
    if (x >= x0 + r && x <= x1 - r) d = 0;
    if (y >= y0 + r && y <= y1 - r) d = 0;
    if (d == 0) return clamp01(fminf(fminf(x - x0, x1 - x), fminf(y - y0, y1 - y)) + 0.5f);
    return clamp01(r - d + 0.5f);
}

// a separable box blur of an alpha plane, three passes (close to a gaussian of sigma ~ radius / 1.4)
void wall_blur_alpha(float *a, float *tmp, int w, int h, int rad)
{
    if (rad < 1) return;
    for (int pass = 0; pass < 3; pass++) {
        for (int y = 0; y < h; y++) {
            float *row = a + (size_t)y * w, *o = tmp + (size_t)y * w, s = 0;
            for (int x = -rad; x <= rad; x++) s += row[imin(imax(x, 0), w - 1)];
            for (int x = 0; x < w; x++) {
                o[x] = s / (2 * rad + 1);
                s += row[imin(x + rad + 1, w - 1)] - row[imax(x - rad, 0)];
            }
        }
        for (int x = 0; x < w; x++) {
            float s = 0;
            for (int y = -rad; y <= rad; y++) s += tmp[(size_t)imin(imax(y, 0), h - 1) * w + x];
            for (int y = 0; y < h; y++) {
                a[(size_t)y * w + x] = s / (2 * rad + 1);
                s += tmp[(size_t)imin(y + rad + 1, h - 1) * w + x] - tmp[(size_t)imax(y - rad, 0) * w + x];
            }
        }
    }
}

// The roll's shadow across one of its edges: a straight edge of the body's shape (covered where a pixel's centre is inside, a pixel soft) blurred by the three box
// passes of wall_blur_alpha with radius `rad`, as a table of whole pixels; edge_at gives it t px inside the edge (0 far outside, 1 far inside). Made once for each radius.
typedef struct { int rad, reach; float *c; } EdgeProf;
static EdgeProf g_edge[4];
static const EdgeProf *edge_prof(int rad)
{
    for (int i = 0; i < 4; i++)
        if (g_edge[i].c && g_edge[i].rad == rad) return &g_edge[i];
    EdgeProf *e = &g_edge[0];
    for (int i = 0; i < 4; i++)
        if (!g_edge[i].c) {
            e = &g_edge[i];
            break;
        }
    free(e->c);
    e->c = NULL;
    int reach = 3 * rad + 2, n = 2 * reach;
    float *a = malloc((size_t)n * sizeof(float)), *tmp = malloc((size_t)n * sizeof(float));
    if (!a || !tmp) {
        free(a);
        free(tmp);
        return NULL;
    }
    for (int i = 0; i < n; i++) a[i] = i >= reach ? 1.0f : 0.0f; // (pixel i's centre is i - reach + 0.5 px inside the edge)
    for (int pass = 0; pass < 3; pass++) {
        for (int i = 0; i < n; i++) {
            float s = 0;
            for (int j = i - rad; j <= i + rad; j++) s += a[imin(imax(j, 0), n - 1)];
            tmp[i] = s / (2 * rad + 1);
        }
        memcpy(a, tmp, (size_t)n * sizeof(float));
    }
    free(tmp);
    e->rad = rad;
    e->reach = reach;
    e->c = a;
    return e;
}
static inline float edge_at(const EdgeProf *e, float t)
{
    float f = t + e->reach - 0.5f;
    if (f <= 0) return e->c[0];
    int i = (int)f;
    if (i >= 2 * e->reach - 1) return 1;
    float w = f - (float)i;
    return e->c[i] + (e->c[i + 1] - e->c[i]) * w;
}

void wall_roll_ready(float k) { edge_prof((int)lroundf(6 * k * 0.5f)); }

// The roll's soft shadow, multiplied over the screen (it may lie on the black table): the body's shape moved (2k, 3k) in the roll's own frame, blurred (box passes of
// radius 3k), in brown at .5, turned and scaled with the roll. The page drew the body's shadow and took the body back out of it; here the blurred box is worked out
// where it is (the two edge profiles across and along multiplied), and nothing is done under the body, which covers it.
static void roll_shadow(Surf *s, const WallRoll *r, float hw, float hh)
{
    float k = r->k, scl = r->scale > 0 ? r->scale : 1, inv = 1 / scl;
    const EdgeProf *e = edge_prof((int)lroundf(6 * k * 0.5f));
    if (!e) return;
    float x0 = -hw + 2 * k, x1 = hw + 2 * k, y0 = -hh + 3 * k, y1 = hh + 3 * k, rch = (float)e->reach, ca = cosf(r->ang), sn = sinf(r->ang);
    // the box it can darken: the shadow's reach round its shape, turned onto the screen
    float ex = fmaxf(fabsf(x0), fabsf(x1)) + rch, ey = fmaxf(fabsf(y0), fabsf(y1)) + rch;
    float bx = (fabsf(ca) * ex + fabsf(sn) * ey) * scl + 1, by = (fabsf(sn) * ex + fabsf(ca) * ey) * scl + 1;
    int X0 = imax((int)floorf(r->x - bx), 0), X1 = imin((int)ceilf(r->x + bx), s->w), Y0 = imax((int)floorf(r->y - by), 0), Y1 = imin((int)ceilf(r->y + by), s->h);
    const float sr = 74 / 255.0f, sg = 58 / 255.0f, sb = 26 / 255.0f, al = 0.5f * r->alpha;
    float du = ca * inv, dv = -sn * inv, iw = hw - 1, ih = hh - 1;
    // the two profiles of the blurred box, across and down, every quarter pixel from the reach's start (the blurred box is their product)
    enum { PMAX = 4096 };
    float px[PMAX], py[PMAX], ua = x0 - rch, va = y0 - rch;
    int nx = imin(PMAX - 1, (int)ceilf((x1 + rch - ua) * 4) + 1), ny = imin(PMAX - 1, (int)ceilf((y1 + rch - va) * 4) + 1);
    for (int i = 0; i <= nx; i++) px[i] = edge_at(e, ua + i * 0.25f - x0) - edge_at(e, ua + i * 0.25f - x1);
    for (int i = 0; i <= ny; i++) py[i] = edge_at(e, va + i * 0.25f - y0) - edge_at(e, va + i * 0.25f - y1);
    // multiplying by 1 - a + a * s, in 256ths: each channel loses (1 - s) * a of itself
    const int m0 = (int)((1 - sr) * 256 + 0.5f), m1 = (int)((1 - sg) * 256 + 0.5f), m2 = (int)((1 - sb) * 256 + 0.5f);
    for (int y = Y0; y < Y1; y++) {
        float fx = X0 + 0.5f - r->x, fy = y + 0.5f - r->y, u0 = (fx * ca + fy * sn) * inv, v0 = (-fx * sn + fy * ca) * inv;
        // the row's run inside the tables, and the run under the body (jumped over): lo <= a + i * b < hi for whole i
        float lo = 0, hi = 1e9f, bl = -1e9f, bh = 1e9f;
#define RUN(a0, b0, L, H, LO, HI)                                                                                                                              \
    do {                                                                                                                                                         \
        if (fabsf(b0) < 1e-9f) {                                                                                                                                 \
            if ((a0) < (L) || (a0) >= (H)) LO = 1, HI = 0;                                                                                                       \
        } else {                                                                                                                                                 \
            float t0_ = ((L) - (a0)) / (b0), t1_ = ((H) - (a0)) / (b0);                                                                                          \
            if (t0_ > t1_) {                                                                                                                                     \
                float t_ = t0_;                                                                                                                                  \
                t0_ = t1_;                                                                                                                                       \
                t1_ = t_;                                                                                                                                        \
            }                                                                                                                                                    \
            LO = fmaxf(LO, t0_);                                                                                                                                 \
            HI = fminf(HI, t1_);                                                                                                                                 \
        }                                                                                                                                                        \
    } while (0)
        RUN((u0 - ua) * 4, du * 4, 0.0f, (float)nx - 0.001f, lo, hi);
        RUN((v0 - va) * 4, dv * 4, 0.0f, (float)ny - 0.001f, lo, hi);
        RUN(u0, du, -iw + 0.001f, iw - 0.001f, bl, bh);
        RUN(v0, dv, -ih + 0.001f, ih - 0.001f, bl, bh);
#undef RUN
        int xa = X0 + imax(0, (int)ceilf(lo)), xb = imin(X1, X0 + (int)floorf(hi) + 1), ba = X0 + (int)ceilf(bl), bb = X0 + (int)floorf(bh) + 1;
        if (bb <= ba) ba = bb = X1;
        for (int x = xa; x < xb; x++) {
            if (x == ba) { // (under the body)
                x = bb - 1;
                continue;
            }
            float i = (float)(x - X0), qu = (u0 + i * du - ua) * 4, qv = (v0 + i * dv - va) * 4;
            int iu = (int)qu, iv = (int)qv;
            if (iu < 0 || iv < 0 || iu >= nx || iv >= ny) continue;
            float fu = qu - iu, fv = qv - iv;
            float a = (px[iu] + (px[iu + 1] - px[iu]) * fu) * (py[iv] + (py[iv + 1] - py[iv]) * fv) * al;
            if (a <= 0.002f) continue;
            int a8 = (int)(a * 256 + 0.5f);
            uint8_t *d = s->px + ((size_t)y * s->w + x) * 4;
            d[0] = (uint8_t)((d[0] * (256 - ((m0 * a8 + 128) >> 8)) + 128) >> 8);
            d[1] = (uint8_t)((d[1] * (256 - ((m1 * a8 + 128) >> 8)) + 128) >> 8);
            d[2] = (uint8_t)((d[2] * (256 - ((m2 * a8 + 128) >> 8)) + 128) >> 8);
        }
    }
}

void wall_roll_draw(Surf *s, const WallTex *tex, const WallRoll *r, WallScratch *sc)
{
    if (!tex || r->alpha <= 0.003f) return;
    float k = r->k, hw = r->R * k, hh = (r->rb * k) / 2;
#ifdef UI_INTRO_PROF
    double rp0 = peel_ms();
#endif
    if (!r->keyed) roll_shadow(s, r, hw, hh);
#ifdef UI_INTRO_PROF
    double rp1 = peel_ms();
    g_roll_ms[0] += rp1 - rp0;
#endif
    // the body: a picture of just the roll (a pixel of room round it), turned and scaled onto the screen
    int W = (int)ceilf(2 * hw) + 2, H = (int)ceilf(2 * hh) + 2;
    if (!scratch_fit(sc, W * H)) return;
    uint8_t *img = sc->a; // the roll, straight alpha
    memset(img, 0, (size_t)W * H * 4);
    float ox = W / 2.0f, oy = H / 2.0f, rad = 3 * k, circ = WALL_ROLL_CIRC * r->R;
    int ncol = imin((int)ceilf(2 * hw), 1024), nrow = (int)ceilf(2 * hh), X0 = (int)floorf(ox - hw), Y0 = (int)floorf(oy - hh);
    // its columns: each takes the pattern from where the arcsine mapping puts it (u in 1x px round the roll), the light across it laid over (in 256ths)
    int txc[1024], sha[1024], shc[1024][3];
    float dxe[1024];
    for (int j = 0; j < ncol; j++) {
        float xs = r->flip ? (float)(ncol - 1 - j) : (float)j; // (a roll that runs the other way turns the other way: the pattern is mirrored)
        float x = fmaxf(-1, fminf(1, (xs + 0.5f - hw) / k / r->R));
        float a = (asinf(x) - r->phi) / (2 * PI_F) + 0.5f / WALL_ROLL_RN;
        float u = circ * (a - floorf(a));
        int tx = (int)(u * k);
        txc[j] = tx >= tex->w ? tex->w - 1 : tx < 0 ? 0 : tx;
        float sh[4];
        shade_at((j + 0.5f) / (2 * hw), sh);
        sha[j] = (int)(sh[3] * 256 + 0.5f);
        for (int q = 0; q < 3; q++) shc[j][q] = (int)(sh[q] * 256 + 0.5f);
        float cxp = X0 + j + 0.5f;
        dxe[j] = fminf(cxp - (ox - hw), (ox + hw) - cxp); // (how far in from the left or right end)
    }
    const float edge = fmaxf(1, k);
    for (int yy = 0; yy < nrow; yy++) { // row by row, in whole numbers
        int Y = Y0 + yy;
        if (Y < 0 || Y >= H) continue;
        float cyp = Y + 0.5f, dye = fminf(cyp - (oy - hh), (oy + hh) - cyp), ey = yy + 0.5f;
        int ty = (int)(ey / (2 * hh) * tex->h), line = ey < edge ? 1 : ey > 2 * hh - edge ? 2 : 0; // (a hairline of light along the top, of dark along the bottom)
        if (ty >= tex->h) ty = tex->h - 1;
        const uint8_t *trow = tex->px + (size_t)ty * tex->w * 4;
        uint8_t *d = img + ((size_t)Y * W + X0) * 4;
        int rim = dye <= rad + 1;
        for (int j = 0; j < ncol; j++, d += 4) {
            // its rounded rectangle's coverage: worked out only near the rim, whole inside
            float cov = !rim ? (dxe[j] >= 0.5f ? 1 : clamp01(dxe[j] + 0.5f))
                             : dxe[j] > rad + 1 ? clamp01(dye + 0.5f) : rrect_cov(X0 + j + 0.5f, cyp, ox - hw, oy - hh, ox + hw, oy + hh, rad);
            if (cov <= 0) continue;
            if (r->keyed) { // (its shape alone)
                d[0] = (uint8_t)(r->key >> 16);
                d[1] = (uint8_t)(r->key >> 8);
                d[2] = (uint8_t)r->key;
                d[3] = (uint8_t)(cov * 255 + 0.5f);
                continue;
            }
            const uint8_t *tp = trow + txc[j] * 4;
            int c[3];
            for (int q = 0; q < 3; q++) c[q] = (tp[q] * (256 - sha[j]) + shc[j][q] + 128) >> 8;
            if (line == 1)
                for (int q = 0; q < 3; q++) c[q] = (c[q] * 115 + 255 * 141 + 128) >> 8;
            else if (line == 2) {
                c[0] = (c[0] * 179 + 60 * 77 + 128) >> 8;
                c[1] = (c[1] * 179 + 40 * 77 + 128) >> 8;
                c[2] = (c[2] * 179 + 20 * 77 + 128) >> 8;
            }
            d[0] = (uint8_t)(c[0] > 255 ? 255 : c[0]);
            d[1] = (uint8_t)(c[1] > 255 ? 255 : c[1]);
            d[2] = (uint8_t)(c[2] > 255 ? 255 : c[2]);
            d[3] = (uint8_t)(cov * 255 + 0.5f);
        }
    }
#ifdef UI_INTRO_PROF
    double rp2 = peel_ms();
    g_roll_ms[1] += rp2 - rp1;
#endif
    gfx_blit_xform(s, img, W, H, r->x, r->y, r->scale > 0 ? r->scale : 1, r->ang, r->alpha);
#ifdef UI_INTRO_PROF
    g_roll_ms[2] += peel_ms() - rp2;
#endif
}

// ------------------------------------------------------------------------------------------------ pictures and blits

PaperSpr *wall_turned(const PaperSpr *src, float rot, int pad, float shadow_dy, float shadow_blur, uint32_t shadow_rgb, float shadow_a)
{
    float cs = fabsf(cosf(rot)), sn = fabsf(sinf(rot));
    int w = (int)ceilf(src->w * cs + src->h * sn) + 2 * pad, h = (int)ceilf(src->w * sn + src->h * cs) + 2 * pad;
    Surf tmp = {w, h, NULL};
    PaperSpr *out = wall_spr_new(w, h);
    if (!out) return NULL;
    // the picture turned, into a cleared surface (gfx_blit_xform blends straight alpha over an opaque surface: so draw it over a transparent buffer by hand)
    float ca = cosf(rot), sa = sinf(rot), hw = src->w * 0.5f, hh = src->h * 0.5f, cx = w * 0.5f, cy = h * 0.5f;
    uint8_t *pic = out->px;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float fx = x + 0.5f - cx, fy = y + 0.5f - cy, u = fx * ca + fy * sa + hw - 0.5f, v = -fx * sa + fy * ca + hh - 0.5f;
            int iu = (int)floorf(u), iv = (int)floorf(v);
            if (iu < -1 || iv < -1 || iu >= src->w || iv >= src->h) continue;
            float fu = u - iu, fv = v - iv, acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < 2; j++)
                for (int i = 0; i < 2; i++) {
                    int sx = iu + i, sy = iv + j;
                    if (sx < 0 || sy < 0 || sx >= src->w || sy >= src->h) continue;
                    float wgt = (i ? fu : 1 - fu) * (j ? fv : 1 - fv);
                    const uint8_t *p = src->px + ((size_t)sy * src->w + sx) * 4;
                    float a = p[3] * wgt;
                    acc[0] += p[0] * a;
                    acc[1] += p[1] * a;
                    acc[2] += p[2] * a;
                    acc[3] += a;
                }
            if (acc[3] < 0.5f) continue;
            uint8_t *d = pic + ((size_t)y * w + x) * 4;
            d[0] = (uint8_t)(acc[0] / acc[3] + 0.5f);
            d[1] = (uint8_t)(acc[1] / acc[3] + 0.5f);
            d[2] = (uint8_t)(acc[2] / acc[3] + 0.5f);
            d[3] = (uint8_t)fminf(255, acc[3] + 0.5f);
        }
    (void)tmp;
    // its shadow, under it: the alpha moved down and blurred, in the shadow colour; then the picture over it (straight alpha "over")
    if (shadow_a > 0) {
        float *sa2 = malloc((size_t)w * h * sizeof(float)), *t2 = malloc((size_t)w * h * sizeof(float));
        if (sa2 && t2) {
            int dy = (int)lroundf(shadow_dy);
            for (int y = 0; y < h; y++)
                for (int x = 0; x < w; x++) {
                    int sy = y - dy;
                    sa2[(size_t)y * w + x] = sy >= 0 && sy < h ? pic[((size_t)sy * w + x) * 4 + 3] / 255.0f : 0;
                }
            wall_blur_alpha(sa2, t2, w, h, (int)lroundf(shadow_blur * 0.5f));
            float sr = ((shadow_rgb >> 16) & 255), sg = ((shadow_rgb >> 8) & 255), sb = (shadow_rgb & 255);
            for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
                uint8_t *d = pic + i * 4;
                float fa = d[3] / 255.0f, ba = sa2[i] * shadow_a, oa = fa + ba * (1 - fa);
                if (oa <= 0) continue;
                for (int c = 0; c < 3; c++) {
                    float sc = c == 0 ? sr : c == 1 ? sg : sb;
                    d[c] = (uint8_t)((d[c] * fa + sc * ba * (1 - fa)) / oa + 0.5f);
                }
                d[3] = (uint8_t)(oa * 255 + 0.5f);
            }
        }
        free(sa2);
        free(t2);
    }
    out->ox = w / 2;
    out->oy = h / 2;
    return out;
}

PaperSpr *wall_shadow_only(const uint8_t *rgba, int w, int h, int pad, float dx, float dy, int rad, uint32_t rgb, float alpha)
{
    int W = w + 2 * pad, H = h + 2 * pad;
    float *a = malloc((size_t)W * H * sizeof(float)), *tmp = malloc((size_t)W * H * sizeof(float));
    PaperSpr *out = wall_spr_new(W, H);
    if (!a || !tmp || !out) {
        free(a);
        free(tmp);
        wall_spr_free(out);
        return NULL;
    }
    int ix = (int)lroundf(dx), iy = (int)lroundf(dy);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sx = x - pad - ix, sy = y - pad - iy;
            a[(size_t)y * W + x] = sx >= 0 && sy >= 0 && sx < w && sy < h ? rgba[((size_t)sy * w + sx) * 4 + 3] / 255.0f : 0;
        }
    wall_blur_alpha(a, tmp, W, H, rad);
    uint8_t r = (uint8_t)((rgb >> 16) & 255), g = (uint8_t)((rgb >> 8) & 255), b = (uint8_t)(rgb & 255);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int sx = x - pad, sy = y - pad;
            float own = sx >= 0 && sy >= 0 && sx < w && sy < h ? rgba[((size_t)sy * w + sx) * 4 + 3] / 255.0f : 0; // (the picture itself is taken back out)
            float v = a[(size_t)y * W + x] * alpha * (1 - own);
            uint8_t *d = out->px + ((size_t)y * W + x) * 4;
            d[0] = r;
            d[1] = g;
            d[2] = b;
            d[3] = (uint8_t)(v * 255 + 0.5f);
        }
    free(a);
    free(tmp);
    out->ox = W / 2;
    out->oy = H / 2;
    return out;
}

int16_t *wall_runs(const PaperSpr *p)
{
    int16_t *r = p ? malloc((size_t)p->h * 2 * sizeof(int16_t)) : NULL;
    if (!r) return NULL;
    for (int y = 0; y < p->h; y++) { // the longest run of solid pixels on the row
        const uint8_t *row = p->px + (size_t)y * p->w * 4;
        int best = 0, ba = 0;
        for (int x = 0; x < p->w;) {
            if (row[x * 4 + 3] != 255) {
                x++;
                continue;
            }
            int a = x;
            while (x < p->w && row[x * 4 + 3] == 255) x++;
            if (x - a > best) {
                best = x - a;
                ba = a;
            }
        }
        r[y * 2] = (int16_t)ba;
        r[y * 2 + 1] = (int16_t)(ba + best);
    }
    return r;
}

// pixels [a, b) of a row laid over (straight alpha)
static void blend_span(uint8_t *d, const uint8_t *src, int n)
{
    for (int i = 0; i < n; i++, src += 4, d += 4) {
        unsigned a = src[3];
        if (!a) continue;
        if (a == 255) {
            d[0] = src[0];
            d[1] = src[1];
            d[2] = src[2];
            continue;
        }
        d[0] = (uint8_t)(d[0] + (((src[0] - d[0]) * (int)a + 127) / 255));
        d[1] = (uint8_t)(d[1] + (((src[1] - d[1]) * (int)a + 127) / 255));
        d[2] = (uint8_t)(d[2] + (((src[2] - d[2]) * (int)a + 127) / 255));
    }
}

// a row's pixels [ra, rb) of the sprite onto the surface row (both at ra): the solid run (runs) copied whole, the rest blended
static void put_row(uint8_t *d, const uint8_t *src, int ra, int rb, const int16_t *run)
{
    if (rb <= ra) return;
    if (!run) {
        blend_span(d, src, rb - ra);
        return;
    }
    int oa = run[0] > ra ? run[0] : ra, ob = run[1] < rb ? run[1] : rb;
    if (ob <= oa) {
        blend_span(d, src, rb - ra);
        return;
    }
    blend_span(d, src, oa - ra);
    memcpy(d + (size_t)(oa - ra) * 4, src + (size_t)(oa - ra) * 4, (size_t)(ob - oa) * 4); // (solid over an opaque surface: the pixels themselves)
    blend_span(d + (size_t)(ob - ra) * 4, src + (size_t)(ob - ra) * 4, rb - ob);
}

void wall_put(Surf *s, const PaperSpr *p, const int16_t *runs, int x, int y)
{
    if (!p) return;
    int x0 = x - p->ox, y0 = y - p->oy;
    int ya = imax(0, -y0), yb = imin(p->h, s->h - y0), xa = imax(0, -x0), xb = imin(p->w, s->w - x0);
    for (int sy = ya; sy < yb; sy++)
        put_row(s->px + ((size_t)(y0 + sy) * s->w + x0 + xa) * 4, p->px + ((size_t)sy * p->w + xa) * 4, xa, xb, runs ? runs + sy * 2 : NULL);
}

void wall_blit_half(Surf *s, const PaperSpr *p, const int16_t *runs, int x, int y, float cx, float cy, float dx, float dy, float lim)
{
    if (!p) return;
    int x0 = x - p->ox, y0 = y - p->oy;
    int ya = imax(0, -y0), yb = imin(p->h, s->h - y0), xa = imax(0, -x0), xb = imin(p->w, s->w - x0);
    for (int sy = ya; sy < yb; sy++) {
        int Y = y0 + sy;
        float base = (Y + 0.5f - cy) * dy;
        // the row's run where the test holds: (X + .5 - cx) dx + base < lim
        int ra = xa, rb = xb;
        if (fabsf(dx) > 1e-6f) {
            float xl = (lim - base) / dx + cx - 0.5f; // the X where it changes
            if (dx > 0) rb = imin(rb, (int)ceilf(xl) - x0);
            else ra = imax(ra, (int)floorf(xl) + 1 - x0);
        } else if (base >= lim) continue;
        put_row(s->px + ((size_t)Y * s->w + x0 + ra) * 4, p->px + ((size_t)sy * p->w + ra) * 4, ra, rb, runs ? runs + sy * 2 : NULL);
    }
}

// the run of a row's pixels (from x0, the coordinates starting at U, V and stepping dU, dV) whose four texels are all inside the hole: [*a, *b), possibly empty
static void hole_run(const int hole[4], int x0, int32_t U, int32_t V, int32_t dU, int32_t dV, int *a, int *b)
{
    double lo = -1e18, hi = 1e18; // x - x0, as a real number
    const double r[2][2] = {{(double)hole[0] * 65536, (double)(hole[2] - 1) * 65536 - 1}, {(double)hole[1] * 65536, (double)(hole[3] - 1) * 65536 - 1}};
    const double st[2] = {U, V}, dd[2] = {dU, dV};
    for (int k = 0; k < 2; k++) { // texel and texel + 1 inside: the coordinate between hole[k] and hole[k + 2] - 2 (whole texels), i.e. its 16.16 value in r[k]
        if (dd[k] == 0) {
            if (st[k] < r[k][0] || st[k] > r[k][1]) lo = 1, hi = 0;
            continue;
        }
        double t0 = (r[k][0] - st[k]) / dd[k], t1 = (r[k][1] - st[k]) / dd[k];
        if (t0 > t1) {
            double t = t0;
            t0 = t1;
            t1 = t;
        }
        lo = fmax(lo, t0);
        hi = fmin(hi, t1);
    }
    *a = x0 + (int)ceil(lo + 1e-9);
    *b = x0 + (int)floor(hi - 1e-9) + 1;
    if (*b < *a) *a = *b = x0;
}

void wall_blit_shade(Surf *s, const uint8_t *a8, int w, int h, float cx, float cy, float scale, float angle, float alpha, uint32_t rgb, const int *hole)
{
    if (!a8 || w < 2 || h < 2 || scale <= 0 || alpha <= 0) return;
    int ai = (int)(fminf(alpha, 1) * 256 + 0.5f), cr = (rgb >> 16) & 255, cg = (rgb >> 8) & 255, cb = rgb & 255;
    float ca = cosf(angle), sa = sinf(angle), inv = 1 / scale, hw = w * 0.5f, hh = h * 0.5f;
    float ex = (fabsf(ca) * w + fabsf(sa) * h) * 0.5f * scale + 1, ey = (fabsf(sa) * w + fabsf(ca) * h) * 0.5f * scale + 1;
    int x0 = imax((int)floorf(cx - ex), 0), x1 = imin((int)ceilf(cx + ex), s->w), y0 = imax((int)floorf(cy - ey), 0), y1 = imin((int)ceilf(cy + ey), s->h);
    const int32_t dU = (int32_t)lroundf(ca * inv * 65536), dV = (int32_t)lroundf(-sa * inv * 65536);
    for (int y = y0; y < y1; y++) {
        float fx = x0 + 0.5f - cx, fy = y + 0.5f - cy, u = hw + (fx * ca + fy * sa) * inv - 0.5f, v = hh + (-fx * sa + fy * ca) * inv - 0.5f;
        int32_t U = (int32_t)floorf(u * 65536), V = (int32_t)floorf(v * 65536);
        uint8_t *d = s->px + ((size_t)y * s->w + x0) * 4;
        int ha = x1, hb = x1; // (the run inside the hole, jumped over)
        if (hole) hole_run(hole, x0, U, V, dU, dV, &ha, &hb);
        for (int x = x0; x < x1; x++, U += dU, V += dV, d += 4) {
            if (x == ha && hb > ha) {
                int n = hb - ha - 1;
                x += n;
                U += n * dU;
                V += n * dV;
                d += n * 4;
                continue;
            }
            int iu = U >> 16, iv = V >> 16;
            if (iu < 0 || iv < 0 || iu >= w - 1 || iv >= h - 1) continue; // (a shadow's picture has room round it: its rim is clear)
            const uint8_t *p = a8 + (size_t)iv * w + iu;
            unsigned a00 = p[0], a10 = p[1], a01 = p[w], a11 = p[w + 1];
            if (!(a00 | a10 | a01 | a11)) continue;
            unsigned fu = (unsigned)(U >> 8) & 255u, fv = (unsigned)(V >> 8) & 255u;
            unsigned top = a00 * (256 - fu) + a10 * fu, bot = a01 * (256 - fu) + a11 * fu, A = top * (256 - fv) + bot * fv; // (0..255 * 65536)
            int a = (int)(((A >> 8) * (unsigned)ai) >> 16);                                                              // 0..255
            if (!a) continue;
            d[0] = (uint8_t)(d[0] + (((cr - d[0]) * a + 127) / 255));
            d[1] = (uint8_t)(d[1] + (((cg - d[1]) * a + 127) / 255));
            d[2] = (uint8_t)(d[2] + (((cb - d[2]) * a + 127) / 255));
        }
    }
}

void wall_blit_mul(Surf *s, const uint8_t *rgba, int w, int h, int x, int y, float alpha)
{
    int ya = imax(0, -y), yb = imin(h, s->h - y), xa = imax(0, -x), xb = imin(w, s->w - x);
    int ai = (int)(alpha * 256 + 0.5f);
    for (int sy = ya; sy < yb; sy++) {
        const uint8_t *src = rgba + ((size_t)sy * w + xa) * 4;
        uint8_t *d = s->px + ((size_t)(y + sy) * s->w + x + xa) * 4;
        for (int sx = xa; sx < xb; sx++, src += 4, d += 4) {
            int a = (src[3] * ai) >> 8;
            if (!a) continue;
            for (int c = 0; c < 3; c++) d[c] = (uint8_t)((d[c] * (255 * 255 - a * (255 - src[c]))) / (255 * 255));
        }
    }
}

// ------------------------------------------------------------------------------------------------ ribbons (ribbon.js)

#define PERSP_D 520.0f
#define LIFT_UP 0.36f

void wall_lifted(float x, float y, float z, float *ox, float *oy, float *os)
{
    float f = 1 + z / PERSP_D;
    *ox = 640 + (x - 640) * f;
    *oy = 360 + (y - 360) * f - z * LIFT_UP;
    *os = f;
}

// the sprite's colour at (u, v) (pixel edges at whole numbers), bilinear, premultiplied (0..255 x alpha); b->flip = the box is read turned half round
static void sample(const WallBox *b, float u, float v, float out[4])
{
    const PaperSpr *im = b->img;
    if (b->flip) {
        u = (float)(2 * b->bx + b->bw) - u;
        v = (float)(2 * b->by + b->bh) - v;
    }
    u -= 0.5f;
    v -= 0.5f;
    int iu = (int)floorf(u), iv = (int)floorf(v);
    float fu = u - iu, fv = v - iv;
    out[0] = out[1] = out[2] = out[3] = 0;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            int x = iu + i, y = iv + j;
            if (x < 0 || y < 0 || x >= im->w || y >= im->h) continue;
            float w = (i ? fu : 1 - fu) * (j ? fv : 1 - fv);
            const uint8_t *p = im->px + ((size_t)y * im->w + x) * 4;
            float a = p[3] * w;
            out[0] += p[0] * a;
            out[1] += p[1] * a;
            out[2] += p[2] * a;
            out[3] += a;
        }
}

void wall_ribbon(Surf *s, const WallBox *box, const WallPt *pts, int n, float sc, float ang, float alpha, const float back_rgb[3], float sy)
{
    float nx = -sinf(ang), ny = cosf(ang);
    for (int i = 0; i < n - 1; i++) {
        const WallPt *a = &pts[i], *b = &pts[i + 1];
        float dx = b->x - a->x, dy = b->y - a->y, len = sqrtf(dx * dx + dy * dy), su = b->u - a->u;
        if (len < 0.2f || su < 0.01f) continue;
        float al = a->a * alpha;
        if (al <= 0.003f) continue;
        // (the page widens each slice by 0.7 px so the canvas leaves no hairline gaps; here two neighbours share their edge line exactly, the line through their
        // common point across the strip, so each pixel is one slice's or the next's and none is blended twice: no seams where the strip is see-through)
        float k = sc * (a->s + b->s) * 0.5f * (sy > 0 ? sy : 1), wd = su;
        // the slice: (lu, lv) -> a + lu * (dx, dy) / su + lv * (nx, ny) * k, lu in 0..wd, lv in -bh/2..bh/2
        float m00 = dx / su, m10 = dy / su, m01 = nx * k, m11 = ny * k, det = m00 * m11 - m01 * m10;
        if (fabsf(det) < 1e-6f) continue;
        float i00 = m11 / det, i01 = -m01 / det, i10 = -m10 / det, i11 = m00 / det, hh = box->bh * 0.5f;
        float cxs[4] = {0, wd, wd, 0}, cys[4] = {-hh, -hh, hh, hh}, x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for (int c = 0; c < 4; c++) {
            float X = a->x + cxs[c] * m00 + cys[c] * m01, Y = a->y + cxs[c] * m10 + cys[c] * m11;
            x0 = fminf(x0, X);
            y0 = fminf(y0, Y);
            x1 = fmaxf(x1, X);
            y1 = fmaxf(y1, Y);
        }
        int X0 = imax(0, (int)floorf(x0) - 1), X1 = imin(s->w, (int)ceilf(x1) + 1), Y0 = imax(0, (int)floorf(y0) - 1), Y1 = imin(s->h, (int)ceilf(y1) + 1);
        float tone0 = 1, tone1 = 1;
        if (a->back) {
            tone0 = lerpf(0.93f, 1, a->shade);
            tone1 = lerpf(0.93f, 1, b->shade);
        }
        const PaperSpr *im = box->img;
        const int ai = (int)(al * 256 + 0.5f), stride = im->w * 4, bk[3] = {(int)(back_rgb[0] + 0.5f), (int)(back_rgb[1] + 0.5f), (int)(back_rgb[2] + 0.5f)};
        const float iwd = 1 / wd;
        for (int Y = Y0; Y < Y1; Y++) {
            // the row's run inside the slice: 0 <= lu < wd and |lv| <= hh + .5, both straight in X
            float py = Y + 0.5f - a->y, lu0 = i00 * (X0 + 0.5f - a->x) + i01 * py, lv0 = i10 * (X0 + 0.5f - a->x) + i11 * py, lo = 0, hi = (float)(X1 - X0);
            const float lim[2][2] = {{0, wd}, {-hh - 0.5f, hh + 0.5f}}, st[2] = {lu0, lv0}, sl[2] = {i00, i10};
            for (int q = 0; q < 2; q++) {
                if (fabsf(sl[q]) < 1e-9f) {
                    if (st[q] < lim[q][0] || st[q] > lim[q][1]) lo = 1, hi = 0;
                    continue;
                }
                float t0 = (lim[q][0] - st[q]) / sl[q], t1 = (lim[q][1] - st[q]) / sl[q];
                if (t0 > t1) {
                    float t = t0;
                    t0 = t1;
                    t1 = t;
                }
                lo = fmaxf(lo, t0);
                hi = fminf(hi, t1);
            }
            int xa = X0 + imax(0, (int)floorf(lo)), xb = imin(X1, X0 + (int)ceilf(hi) + 1); // (a pixel of room each side: the exact tests below decide)
            for (int X = xa; X < xb; X++) {
                float px = X + 0.5f - a->x, lu = i00 * px + i01 * py, lv = i10 * px + i11 * py;
                if (lu < 0 || lu >= wd || lv < -hh - 0.5f || lv > hh + 0.5f) continue;
                uint8_t *d = s->px + ((size_t)Y * s->w + X) * 4;
                if (!a->back) { // the strip's face: bilinear in whole numbers where all four texels are in the picture (as gfx_blit_xform)
                    float u = (float)box->bx + a->u + lu, v = (float)box->by + hh + lv;
                    if (box->flip) {
                        u = (float)(2 * box->bx + box->bw) - u;
                        v = (float)(2 * box->by + box->bh) - v;
                    }
                    int32_t U = (int32_t)floorf((u - 0.5f) * 65536), V = (int32_t)floorf((v - 0.5f) * 65536);
                    int iu = U >> 16, iv = V >> 16;
                    if (iu >= 0 && iv >= 0 && iu < im->w - 1 && iv < im->h - 1) {
                        const uint8_t *p = im->px + ((size_t)iv * im->w + iu) * 4, *q = p + stride;
                        unsigned a00 = p[3], a10 = p[7], a01 = q[3], a11 = q[7];
                        if (!(a00 | a10 | a01 | a11)) continue;
                        unsigned fu = (unsigned)(U >> 8) & 255u, fv = (unsigned)(V >> 8) & 255u;
                        unsigned w11 = fu * fv, w10 = (fu << 8) - w11, w01 = (fv << 8) - w11, w00 = 65536u - w10 - w01 - w11;
                        unsigned b00 = a00 + (a00 >> 7), b10 = a10 + (a10 >> 7), b01 = a01 + (a01 >> 7), b11 = a11 + (a11 >> 7);
                        uint64_t As = (uint64_t)(b00 * w00) + b10 * w10 + b01 * w01 + b11 * w11;
                        uint32_t af = (uint32_t)((As * (unsigned)ai) >> 16);
                        if (af < 131) continue;
                        for (int c = 0; c < 3; c++) {
                            uint32_t ps = p[c] * b00 * w00 + p[4 + c] * b10 * w10 + q[c] * b01 * w01 + q[4 + c] * b11 * w11;
                            uint32_t o = (uint32_t)((d[c] * (uint64_t)(65536 - af) + (((uint64_t)ps * (unsigned)ai) >> 16) + 32768) >> 16);
                            d[c] = (uint8_t)(o > 255 ? 255 : o);
                        }
                        continue;
                    }
                }
                if (a->back) { // the pale back, a little darker towards the fold, its long edges a pixel soft (in 256ths)
                    float tone = tone0 + (tone1 - tone0) * lu * iwd, cov = hh + 0.5f - fabsf(lv);
                    int A8 = (int)((cov >= 1 ? 1 : cov) * al * 256 + 0.5f), t8 = (int)(tone * 256 + 0.5f);
                    if (A8 <= 0) continue;
                    for (int q = 0; q < 3; q++) d[q] = (uint8_t)(d[q] + ((((bk[q] * t8) >> 8) - d[q]) * A8 + 128) / 256);
                    continue;
                }
                float c[4];
                sample(box, (float)box->bx + a->u + lu * (su / wd), (float)box->by + hh + lv, c);
                float A = c[3] / 255.0f * al;
                if (A <= 0.002f) continue;
                float inv = c[3] > 0 ? 1.0f / c[3] : 0;
                for (int q = 0; q < 3; q++) d[q] = (uint8_t)(d[q] + (c[q] * inv - d[q]) * A + 0.5f);
            }
        }
    }
}

// the distance from (px, py) to the line through the points, and how far past either of its ends (0 between them: from the nearest piece of the line, so a line
// that curls back on itself is not cut by its own end)
static void line_dist(const WallGround *g, int n, float px, float py, float *dist, float *out)
{
    float best = 1e18f, o = 0;
    for (int i = 0; i < n - 1; i++) {
        float vx = g[i + 1].x - g[i].x, vy = g[i + 1].y - g[i].y, l2 = vx * vx + vy * vy, tr = l2 > 0 ? ((px - g[i].x) * vx + (py - g[i].y) * vy) / l2 : 0, t = clamp01(tr);
        float dx = px - g[i].x - vx * t, dy = py - g[i].y - vy * t, d = dx * dx + dy * dy;
        if (d < best) {
            best = d;
            o = i == 0 && tr < 0 ? -tr * sqrtf(l2) : i == n - 2 && tr > 1 ? (tr - 1) * sqrtf(l2) : 0;
        }
    }
    *dist = sqrtf(best);
    *out = o;
}

void wall_ribbon_shadow(Surf *s, const WallGround *g, int n, float half, float alpha, float blur)
{
    if (n < 2 || alpha <= 0) return;
    float zm = 0;
    for (int i = 0; i < n; i++) zm = fmaxf(zm, g[i].z);
    float bl = blur + zm * 0.16f, al = alpha * (1 - 0.65f * clamp01(zm / 90)), layer = al / 9 * 1.25f, reach = half + bl * 1.05f + 1;
    // A few layers of different widths look like a blur: a pixel's darkness is how many of the 9 widths cover it, which depends only on how far it is from the
    // footprint's middle line; a table of it, every quarter pixel.
    enum { LUTN = 4096 };
    float lut[LUTN];
    int nl = imin(LUTN - 1, (int)ceilf(reach * 4) + 1);
    for (int i = 0; i <= nl; i++) {
        float d = i * 0.25f, cover = 0;
        for (int k = 0; k < 9; k++) cover += clamp01(fmaxf(0, half + ((float)k / 8 - 0.5f) * bl * 2.1f) - d + 0.5f);
        lut[i] = fminf(1, cover * layer);
    }
    // the line, with the runs of points that lie on one straight line (the flap beyond its curl) made one piece
    WallGround m[64];
    int nm = 0;
    m[nm++] = g[0];
    for (int i = 1; i < n;) {
        int j = i;
        while (j + 1 < n && nm < 63) { // can the piece from the last point kept go on to j + 1, every point between within a hair of it?
            float vx = g[j + 1].x - m[nm - 1].x, vy = g[j + 1].y - m[nm - 1].y, l = sqrtf(vx * vx + vy * vy);
            int ok = l > 0;
            for (int q = i; q <= j && ok; q++) ok = fabsf((g[q].x - m[nm - 1].x) * vy - (g[q].y - m[nm - 1].y) * vx) / l < 0.2f;
            if (!ok) break;
            j++;
        }
        m[nm++] = g[j];
        i = j + 1;
    }
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < nm; i++) {
        x0 = fminf(x0, m[i].x - reach);
        y0 = fminf(y0, m[i].y - reach);
        x1 = fmaxf(x1, m[i].x + reach);
        y1 = fmaxf(y1, m[i].y + reach);
    }
    int X0 = imax(0, (int)floorf(x0)), X1 = imin(s->w, (int)ceilf(x1)), Y0 = imax(0, (int)floorf(y0)), Y1 = imin(s->h, (int)ceilf(y1));
    if (X0 >= X1 || Y0 >= Y1) return;
    // The distance (and how far past an end) is worked out on a grid of blocks and taken straight between their corners: the wider the blur, the bigger the blocks
    // (a whole pixel while it is sharp; what a block can be off by, near a bend or an end, is small next to how slowly the darkness changes).
    int B = bl < 8 ? 1 : bl < 16 ? 2 : bl < 32 ? 4 : 8, gw = (X1 - X0 + B - 1) / B + 1, gh = (Y1 - Y0 + B - 1) / B + 1;
    float *gd = malloc((size_t)gw * gh * 2 * sizeof(float));
    if (!gd) return;
    for (int j = 0; j < gh; j++)
        for (int i = 0; i < gw; i++) line_dist(m, nm, X0 + i * B + 0.5f, Y0 + j * B + 0.5f, &gd[((size_t)j * gw + i) * 2], &gd[((size_t)j * gw + i) * 2 + 1]);
    // Each pixel is laid towards the shadow's brown by its darkness (in 256ths). Where the blur is wide (blocks of 4 and 8) the darkness is worked out once for
    // each 2 x 2 pixels: it changes there by a tenth of a level at most. A block at an end is worked out pixel by pixel, exactly: the end is a pixel soft (full
    // strength up to the end point, nothing a pixel past it, as the page's polygon ends square there).
    const float far = reach + 0.5f * B, inv = 1.0f / B;
    for (int bj = 0; bj < gh - 1; bj++)
        for (int bi = 0; bi < gw - 1; bi++) {
            const float *c00 = &gd[((size_t)bj * gw + bi) * 2], *c10 = c00 + 2, *c01 = c00 + (size_t)gw * 2, *c11 = c01 + 2;
            if (fminf(fminf(c00[0], c10[0]), fminf(c01[0], c11[0])) >= far) continue;   // (nothing of it reaches this block)
            if (fminf(fminf(c00[1], c10[1]), fminf(c01[1], c11[1])) >= 1 + B) continue; // (past an end)
            int xa = X0 + bi * B, ya = Y0 + bj * B, xb = imin(xa + B, X1), yb = imin(ya + B, Y1);
            if (fmaxf(fmaxf(c00[1], c10[1]), fmaxf(c01[1], c11[1])) > 0) { // at an end
                for (int Y = ya; Y < yb; Y++)
                    for (int X = xa; X < xb; X++) {
                        float d, o;
                        line_dist(m, nm, X + 0.5f, Y + 0.5f, &d, &o);
                        float li = d * 4;
                        if (li >= nl || o >= 1) continue;
                        int ii = (int)li;
                        float a = (lut[ii] + (lut[ii + 1] - lut[ii]) * (li - ii)) * (1 - o);
                        if (a <= 0.002f) continue;
                        int a8 = (int)(a * 256 + 0.5f);
                        uint8_t *q = s->px + ((size_t)Y * s->w + X) * 4;
                        q[0] = (uint8_t)(q[0] + (((40 - q[0]) * a8 + 128) >> 8));
                        q[1] = (uint8_t)(q[1] + (((28 - q[1]) * a8 + 128) >> 8));
                        q[2] = (uint8_t)(q[2] + (((10 - q[2]) * a8 + 128) >> 8));
                    }
                continue;
            }
            const int G = B >= 4 ? 2 : 1;
            for (int Y = ya; Y < yb; Y += G) {
                float fy = (Y - ya + 0.5f * (G - 1)) * inv, dl = c00[0] + (c01[0] - c00[0]) * fy, dr = c10[0] + (c11[0] - c10[0]) * fy;
                int ny = imin(G, yb - Y);
                for (int X = xa; X < xb; X += G) { // (no end here: full strength)
                    float fx = (X - xa + 0.5f * (G - 1)) * inv, d = dl + (dr - dl) * fx;
                    float li = d * 4;
                    if (li >= nl) continue;
                    int ii = (int)li;
                    float a = lut[ii] + (lut[ii + 1] - lut[ii]) * (li - ii);
                    if (a <= 0.002f) continue;
                    int a8 = (int)(a * 256 + 0.5f), nx = imin(G, xb - X);
                    for (int yy = 0; yy < ny; yy++) {
                        uint8_t *q = s->px + ((size_t)(Y + yy) * s->w + X) * 4;
                        for (int xx = 0; xx < nx; xx++, q += 4) {
                            q[0] = (uint8_t)(q[0] + (((40 - q[0]) * a8 + 128) >> 8));
                            q[1] = (uint8_t)(q[1] + (((28 - q[1]) * a8 + 128) >> 8));
                            q[2] = (uint8_t)(q[2] + (((10 - q[2]) * a8 + 128) >> 8));
                        }
                    }
                }
            }
        }
    free(gd);
}

void wall_peel(Surf *s, const WallBox *box, const WallPeel *o)
{
#ifdef UI_INTRO_PROF
    double pt0 = peel_ms();
#endif
    float sc = o->sc, ang = o->ang, ca = cosf(ang), sa = sinf(ang), L = o->L, lf = o->lf < 0 ? 0 : o->lf > L ? L : o->lf, th = box->bh * sc;
    float psi = o->psi, r = o->roll > 0 ? o->roll : 22, vis = o->vis > 0 ? o->vis : 300;
    // 1 what is still stuck: the strip from the fold to the far end (its baked, turned picture, clipped where the fold is)
    if (lf < L - 0.5f && o->baked) wall_blit_half(s, o->baked, o->runs, o->bx, o->by, o->x0, o->y0, -ca, -sa, -lf);
    PEEL_T(0, pt0);
    if (lf < 2) return;
    // 2 the flap, from the fold (d = 0) out to its free end (d = lf)
    float sc_ = r * (PI_F - psi), aC = -r * sinf(psi), zC = r * (1 + cosf(psi));
    float dAt[32];
    int nd = 0, na = (int)fmaxf(4, fminf(12, ceilf(fminf(lf, sc_) / 6)));
    for (int i = 0; i <= na; i++) dAt[nd++] = fminf(lf, sc_) * ((float)i / na);
    if (lf > sc_ + 1) {
        int ns = (int)fmaxf(2, fminf(14, ceilf((lf - sc_) / 45)));
        for (int i = 1; i <= ns && nd < 32; i++) dAt[nd++] = sc_ + (lf - sc_) * ((float)i / ns);
    }
    float Fx = o->x0 + ca * lf, Fy = o->y0 + sa * lf;
    WallPt raw[32], pts[32];
    WallGround gr[32];
    float theta[32];
    for (int i = 0; i < nd; i++) {
        float d = dAt[i], a, z, th0;
        if (d <= sc_) {
            a = -r * sinf(d / r);
            z = r * (1 - cosf(d / r));
            th0 = PI_F - d / r;
        } else {
            a = aC + (d - sc_) * cosf(psi);
            z = zC + (d - sc_) * sinf(psi);
            th0 = psi;
        }
        float bx = Fx + ca * a, by = Fy + sa * a, X, Y, S;
        wall_lifted(bx, by, z, &X, &Y, &S);
        float al = d <= vis ? 1 : 1 - clamp01((d - vis) / (o->fade > 0 ? o->fade : 120));
        raw[i] = (WallPt){X, Y, S, (lf - d) / sc, al, z, clamp01(d / 260), 0};
        theta[i] = th0;
        gr[i] = (WallGround){bx + z * 0.16f, by + z * 0.3f, z * 0.55f};
    }
    for (int i = 0; i < nd; i++) pts[i] = raw[nd - 1 - i]; // free end first, so u rises
    for (int i = 0; i < nd - 1; i++) {
        float t0 = (theta[nd - 1 - i] + theta[nd - 2 - i]) * 0.5f;
        pts[i].back = t0 < PI_F / 2;
    }
    WallGround g2[32];
    for (int i = 0; i < nd; i++) g2[i] = gr[nd - 1 - i];
    PEEL_T(3, pt0);
    wall_ribbon_shadow(s, g2, nd, th / 2, 0.17f * o->alpha, 4);
    PEEL_T(1, pt0);
    wall_ribbon(s, box, pts, nd, sc, ang, o->alpha, o->back, 1);
    PEEL_T(2, pt0);
    // 3 the roll at the fold catches the light: a 3.2 px line of white at .62 a little in from the strip's middle
    for (int i = 0; i + 1 < nd; i++) {
        if (dAt[i] > sc_ * 0.75f || dAt[i + 1] > sc_ * 0.75f) break;
        float e0 = 0.82f * th * raw[i].s * 0.5f, e1 = 0.82f * th * raw[i + 1].s * 0.5f;
        gfx_line(s, raw[i].x - sa * e0, raw[i].y + ca * e0, raw[i + 1].x - sa * e1, raw[i + 1].y + ca * e1, 3.2f, col(1, 1, 1, 0.62f * o->alpha));
    }
}
