#include "paper.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

// Same bytes on the PC and the console: no fused multiply-add (the console's compiler would otherwise use it), and the only libm calls are
// the exact ones (sqrtf, floorf, fabsf); sine, cosine and arctangent are written out below.
#pragma GCC optimize("fp-contract=off")

#define PI_F 3.14159265f

uint32_t paper_tape_rgb[PAPER_TAPE_COUNT] = {PAPER_TAPE_LIST};

// ------------------------------------------------------------------------------------------------ small maths
static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static float minf(float a, float b) { return a < b ? a : b; }
static float maxf(float a, float b) { return a > b ? a : b; }
static float pmod(float x, float p) { return x - floorf(x / p) * p; }
static float smooth01(float t) { t = clampf(t, 0, 1); return t * t * (3.0f - 2.0f * t); }

static float psin(float a)
{
    a -= 2.0f * PI_F * floorf(a / (2.0f * PI_F) + 0.5f); // -pi..pi
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

float paper_contrast(uint32_t a, uint32_t b)
{
    float lum[2];
    uint32_t v[2] = {a, b};
    for (int i = 0; i < 2; i++) {
        float c[3] = {((v[i] >> 16) & 255) / 255.0f, ((v[i] >> 8) & 255) / 255.0f, (v[i] & 255) / 255.0f};
        for (int k = 0; k < 3; k++) c[k] = c[k] <= 0.04045f ? c[k] / 12.92f : powf((c[k] + 0.055f) / 1.055f, 2.4f);
        lum[i] = 0.2126f * c[0] + 0.7152f * c[1] + 0.0722f * c[2];
    }
    float hi = maxf(lum[0], lum[1]), lo = minf(lum[0], lum[1]);
    return (hi + 0.05f) / (lo + 0.05f);
}

// ------------------------------------------------------------------------------------------------ random numbers and noise
void paper_rng_seed(PaperRng *r, uint32_t seed)
{
    r->s = seed * 2654435761u + 0x9E3779B9u;
    if (!r->s) r->s = 0x6D2B79F5u;
    for (int i = 0; i < 6; i++) paper_rng_u32(r);
}
uint32_t paper_rng_u32(PaperRng *r)
{
    uint32_t x = r->s;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return r->s = x;
}
float paper_rng_f(PaperRng *r) { return (paper_rng_u32(r) >> 8) * (1.0f / 16777216.0f); }
float paper_rng_range(PaperRng *r, float a, float b) { return a + (b - a) * paper_rng_f(r); }

static uint32_t hash3(int x, int y, uint32_t seed)
{
    uint32_t h = seed * 0x9E3779B1u ^ (uint32_t)x * 0x85EBCA6Bu ^ (uint32_t)y * 0xC2B2AE35u;
    h ^= h >> 15;
    h *= 0x2C1B3C6Du;
    h ^= h >> 12;
    h *= 0x297A2D39u;
    h ^= h >> 15;
    return h;
}
static float hashf(int x, int y, uint32_t seed) { return (hash3(x, y, seed) >> 8) * (1.0f / 16777216.0f); }
// smooth value noise, 0..1, one lattice cell per unit
static float vnoise(float x, float y, uint32_t seed)
{
    float fx = floorf(x), fy = floorf(y);
    int ix = (int)fx, iy = (int)fy;
    float tx = smooth01(x - fx), ty = smooth01(y - fy);
    float a = hashf(ix, iy, seed), b = hashf(ix + 1, iy, seed), c = hashf(ix, iy + 1, seed), d = hashf(ix + 1, iy + 1, seed);
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}

// ------------------------------------------------------------------------------------------------ sprites
static size_t g_bytes;
static int g_count;

PaperSpr *paper_spr_new(int w, int h)
{
    gfx_pump();
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
    g_bytes += (size_t)w * h * 4;
    g_count++;
    return p;
}

void paper_spr_free(PaperSpr *p)
{
    if (!p) return;
    g_bytes -= (size_t)p->w * p->h * 4;
    g_count--;
    free(p->px);
    free(p);
}

PaperStats paper_stats(void)
{
    PaperStats s = {g_bytes, g_count};
    return s;
}

void paper_spr_over(PaperSpr *dst, const PaperSpr *p, int ax, int ay)
{
    int x = ax - p->ox, y = ay - p->oy;
    int x0 = x < 0 ? 0 : x, x1 = x + p->w > dst->w ? dst->w : x + p->w;
    int y0 = y < 0 ? 0 : y, y1 = y + p->h > dst->h ? dst->h : y + p->h;
    for (int py = y0; py < y1; py++) {
        const uint8_t *s = p->px + ((size_t)(py - y) * p->w + (x0 - x)) * 4;
        uint8_t *d = dst->px + ((size_t)py * dst->w + x0) * 4;
        for (int px = x0; px < x1; px++, s += 4, d += 4) {
            if (!s[3]) continue;
            if (s[3] == 255 || !d[3]) {
                memcpy(d, s, 4);
                continue;
            }
            float sa = s[3] * (1.0f / 255.0f), da = d[3] * (1.0f / 255.0f), oa = sa + da * (1.0f - sa);
            for (int c = 0; c < 3; c++) {
                float v = (s[c] * sa + d[c] * da * (1.0f - sa)) / oa;
                d[c] = (uint8_t)clampf(v + 0.5f, 0, 255);
            }
            d[3] = (uint8_t)clampf(oa * 255.0f + 0.5f, 0, 255);
        }
    }
}

void paper_spr_put(Surf *s, const PaperSpr *p, int ax, int ay) { gfx_blit_over(s, p->px, p->w, p->h, ax - p->ox, ay - p->oy); }

void paper_spr_xform(Surf *s, const PaperSpr *p, float cx, float cy, float scale, float angle, float alpha)
{
    gfx_blit_xform(s, p->px, p->w, p->h, cx, cy, scale, angle, alpha);
}

// the premultiplied float picture of the tape and shadow passes to a sprite
static void spr_from_premul(PaperSpr *p, const float *pm)
{
    for (size_t i = 0, n = (size_t)p->w * p->h; i < n; i++) {
        const float *q = pm + i * 4;
        uint8_t *o = p->px + i * 4;
        if (q[3] < 1.0f / 512.0f) continue;
        float inv = 255.0f / q[3];
        for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf(q[c] * inv + 0.5f, 0, 255);
        o[3] = (uint8_t)clampf(q[3] * 255.0f + 0.5f, 0, 255);
    }
}

// ------------------------------------------------------------------------------------------------ the bake helper
PaperSpr *paper_bake(int w, int h, PaperDraw draw, void *ud)
{
    PaperSpr *out = paper_spr_new(w, h);
    if (!out) return NULL;
    Surf *a = surf_new(w, h), *b = surf_new(w, h);
    gfx_fill(a, col(0, 0, 0, 1));
    draw(a, ud);
    gfx_fill(b, col(1, 1, 1, 1));
    draw(b, ud);
    for (size_t i = 0, n = (size_t)w * h; i < n; i++) {
        const uint8_t *pa = a->px + i * 4, *pb = b->px + i * 4;
        if (pa[0] == pb[0] && pa[1] == pb[1] && pa[2] == pb[2]) { // the same on black and on white: opaque (most pixels of a card)
            uint8_t *o = out->px + i * 4;
            o[0] = pa[0];
            o[1] = pa[1];
            o[2] = pa[2];
            o[3] = 255;
            continue;
        }
        float diff = ((pb[0] - pa[0]) + (pb[1] - pa[1]) + (pb[2] - pa[2])) / 3.0f; // 255 * (1 - alpha)
        float alpha = clampf(1.0f - diff / 255.0f, 0, 1);
        uint8_t *o = out->px + i * 4;
        if (alpha < 0.004f) continue;
        for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf(pa[c] / alpha + 0.5f, 0, 255);
        o[3] = (uint8_t)(alpha * 255.0f + 0.5f);
    }
    surf_free(a);
    surf_free(b);
    return out;
}

// ------------------------------------------------------------------------------------------------ shapes
// signed distance to a rounded rectangle centred at (cx, cy), half sizes hw x hh, corner radius r (negative inside)
static float sd_rrect(float px, float py, float cx, float cy, float hw, float hh, float r)
{
    float qx = fabsf(px - cx) - (hw - r), qy = fabsf(py - cy) - (hh - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0, m = qx > qy ? qx : qy;
    if (m > 0) m = 0;
    return sqrtf(ox * ox + oy * oy) + m - r;
}

// how far along the outline (from the middle of the bottom edge, going right and round) the point of a rounded rectangle nearest (px, py)
// is; `total` receives the outline's length. Used to lay dashes round a rounded box so they run on through the corners.
static float rr_param(float px, float py, float cx, float cy, float hw, float hh, float r, float *total)
{
    float a = hw - r, b = hh - r, quarter = a + PI_F * 0.5f * r + b;
    *total = 4.0f * quarter;
    float qx = fabsf(px - cx), qy = fabsf(py - cy), s;
    if (qx > a && qy > b) s = a + r * patan2(qx - a, qy - b);          // round the corner: angle from straight down
    else if (qy - hh > qx - hw) s = minf(qx, a);                       // along the bottom
    else s = a + PI_F * 0.5f * r + (b - minf(qy, b));                  // up the side
    int right = px >= cx, below = py >= cy;
    if (below) return right ? s : 4.0f * quarter - s;
    return right ? 2.0f * quarter - s : 2.0f * quarter + s;
}

// ------------------------------------------------------------------------------------------------ paper cards
// The paper's grain: fine speckle with a softer blotch under it. A card used to work it out for every pixel (four noise lookups each, the most expensive
// thing about a big sheet); now it is worked out once, as one tile that repeats (every part of it wraps round, so there is no seam), and each card
// reads it from where its seed says to start. Only the edge, where the cut wanders, still takes noise per pixel.
enum { GRAIN_N = 250 }; // (0.5 and 0.12 both divide it into whole noise cells: 125 and 30)
static uint8_t *g_grain;

static float vnoise_wrap(float x, float y, uint32_t seed, int period)
{
    float fx = floorf(x), fy = floorf(y);
    int ix = (int)fx, iy = (int)fy, jx = (ix + 1) % period, jy = (iy + 1) % period;
    ix %= period;
    iy %= period;
    float tx = smooth01(x - fx), ty = smooth01(y - fy);
    float a = hashf(ix, iy, seed), b = hashf(jx, iy, seed), c = hashf(ix, jy, seed), d = hashf(jx, jy, seed);
    return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}

static const uint8_t *grain_tile(void)
{
    if (g_grain) return g_grain;
    g_grain = malloc((size_t)GRAIN_N * GRAIN_N);
    if (!g_grain) return NULL;
    for (int y = 0; y < GRAIN_N; y++)
        for (int x = 0; x < GRAIN_N; x++) {
            float n = 0.35f * hashf(x, y, 0x6A09u) + 0.40f * vnoise_wrap(x * 0.5f, y * 0.5f, 0x6A0Au, GRAIN_N / 2) + 0.25f * vnoise_wrap(x * 0.12f, y * 0.12f, 0x6A0Bu, GRAIN_N * 12 / 100);
            g_grain[y * GRAIN_N + x] = (uint8_t)(clampf((n - 0.38f) * 1.8f, 0, 1) * 255.0f + 0.5f);
        }
    return g_grain;
}

// 0xRRGGBB as three 0..1 values
static void rgb01(uint32_t c, float o[3])
{
    for (int k = 0; k < 3; k++) o[k] = ((c >> (16 - 8 * k)) & 255) / 255.0f;
}

// whether (x, y) is inside the polygon (even-odd)
static int in_poly(const float *vx, const float *vy, int n, float x, float y)
{
    int in = 0;
    for (int i = 0, j = n - 1; i < n; j = i++)
        if ((vy[i] > y) != (vy[j] > y) && x < (vx[j] - vx[i]) * (y - vy[i]) / (vy[j] - vy[i]) + vx[i]) in = !in;
    return in;
}

#ifdef WASHI_PAL_LAB
// mockups only: the second round's materials worked into a finished card (straight alpha, its top left at (pad, pad)): fibres laid in the paper, its
// thickness wandering, the light of a lantern behind it, gold leaf scattered on it, chalk dust over it
static void card_materials(PaperSpr *p, int w, int h, float radius, uint32_t seed, int pad)
{
    const PaperPal *q = &g_pal;
    if (q->fib_n <= 0 && q->cloud_k <= 0 && q->lantern_k <= 0 && q->leaf_n <= 0 && q->dust_k <= 0 && q->guil_k <= 0) return;
    int W = p->w, H = p->h;
    float *fib = calloc((size_t)W * H, sizeof(float)), *leaf = calloc((size_t)W * H * 4, sizeof(float));
    if (!fib || !leaf) {
        free(fib);
        free(leaf);
        return;
    }
    PaperRng r;
    paper_rng_seed(&r, seed ^ 0xF1B5u);
    // fibres: thin strands, mostly short, a few long, each bending its own way (unryu paper: long, fine and swirling)
    int nf = (int)(q->fib_n * w * h / 10000.0f);
    for (int i = 0; i < nf; i++) {
        float x = pad + paper_rng_f(&r) * w, y = pad + paper_rng_f(&r) * h, a = paper_rng_f(&r) * 6.2831853f;
        float u = paper_rng_f(&r), len = 14.0f + (q->fib_len > 0 ? q->fib_len - 14.0f : 70.0f) * u * u;
        float bend = (paper_rng_f(&r) - 0.5f) * 1.4f * (q->fib_bend > 0 ? q->fib_bend : 1.0f), wd = (0.5f + 0.7f * paper_rng_f(&r)) * (q->fib_w > 0 ? q->fib_w : 1.0f);
        float st = 0.35f + 0.65f * paper_rng_f(&r);
        for (float t = 0; t < len; t += 0.5f) {
            x += cosf(a) * 0.5f;
            y += sinf(a) * 0.5f;
            a += bend / len * 0.5f;
            for (int yy = (int)floorf(y) - 2; yy <= (int)floorf(y) + 2; yy++)
                for (int xx = (int)floorf(x) - 2; xx <= (int)floorf(x) + 2; xx++) {
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    float dx = xx + 0.5f - x, dy = yy + 0.5f - y, c = clampf(wd * 0.5f + 0.5f - sqrtf(dx * dx + dy * dy), 0, 1) * st;
                    float *f = &fib[(size_t)yy * W + xx];
                    if (c > *f) *f = c;
                }
        }
    }
    // gold leaf, scattered more thickly in one or two places (chirashi): torn flakes, cut squares (kirihaku), slivers (noge) and dust (sunago), each piece
    // catching the light its own way. With leaf_corner the places are a card's corners, where its writing is not.
    int ncl = paper_rng_f(&r) < 0.5f ? 1 : 2;
    float ccx[2], ccy[2], g1[3], g0[3];
    for (int k = 0; k < 2; k++) {
        if (q->leaf_corner) {
            int cxr = paper_rng_f(&r) < 0.5f, cyb = paper_rng_f(&r) < 0.5f;
            ccx[k] = pad + w * (cxr ? 0.93f : 0.07f);
            ccy[k] = pad + h * (cyb ? 0.9f : 0.1f);
        } else {
            ccx[k] = pad + w * (0.12f + 0.76f * paper_rng_f(&r));
            ccy[k] = pad + h * (0.15f + 0.7f * paper_rng_f(&r));
        }
    }
    rgb01(q->leaf_rgb, g1);
    rgb01(q->leaf_dark, g0);
    float sg = q->leaf_corner ? 0.09f * (w < h ? w : h) + 0.035f * (w > h ? w : h) : 0.16f * (w < h ? w : h) + 0.07f * (w > h ? w : h);
    float nkind[4] = {q->leaf_n, q->leaf_sq, q->leaf_noge, q->leaf_dust};
    for (int kind = 0; kind < 4; kind++) {
        int nl = (int)(nkind[kind] * w * h / 100000.0f + 0.5f);
        for (int i = 0; i < nl; i++) {
            float fx, fy;
            if (paper_rng_f(&r) < (q->leaf_corner ? 0.85f : 0.65f)) {
                int k = (int)(paper_rng_f(&r) * ncl) % ncl;
                float gx = paper_rng_f(&r) + paper_rng_f(&r) + paper_rng_f(&r) - 1.5f, gy = paper_rng_f(&r) + paper_rng_f(&r) + paper_rng_f(&r) - 1.5f;
                fx = ccx[k] + sg * gx * (kind == 3 ? 1.8f : 1.2f);
                fy = ccy[k] + sg * 0.6f * gy * (kind == 3 ? 1.8f : 1.2f);
            } else {
                fx = pad + paper_rng_f(&r) * w;
                fy = pad + paper_rng_f(&r) * h;
            }
            float u = paper_rng_f(&r), rot = paper_rng_f(&r) * 6.2831853f, sheen = paper_rng_f(&r), gang = paper_rng_f(&r) * 6.2831853f, R;
            int nv;
            float vx[8], vy[8], alpha = 0.96f;
            if (kind == 0) { // a torn flake
                R = 1.5f + 5.5f * u * u * u;
                nv = 5 + (int)(paper_rng_f(&r) * 3);
                for (int v = 0; v < nv; v++) {
                    float an = rot + v * 6.2831853f / nv + (paper_rng_f(&r) - 0.5f) * 0.6f, rr = R * (0.45f + 0.55f * paper_rng_f(&r));
                    vx[v] = fx + cosf(an) * rr;
                    vy[v] = fy + sinf(an) * rr;
                }
            } else { // a square, a sliver or a grain of dust: a turned rectangle
                float hl = kind == 1 ? 1.0f + 1.8f * u : kind == 2 ? 3.0f + 5.0f * u : 0.35f + 0.4f * u, hw = kind == 1 ? hl * (0.8f + 0.3f * paper_rng_f(&r)) : kind == 2 ? 0.3f + 0.25f * paper_rng_f(&r) : hl;
                if (kind == 3) alpha = 0.45f + 0.5f * paper_rng_f(&r);
                float c = cosf(rot), s = sinf(rot);
                nv = 4;
                static const float SX[4] = {-1, 1, 1, -1}, SY[4] = {-1, -1, 1, 1};
                for (int v = 0; v < 4; v++) {
                    vx[v] = fx + SX[v] * hl * c - SY[v] * hw * s;
                    vy[v] = fy + SX[v] * hl * s + SY[v] * hw * c;
                }
                R = hl + hw;
            }
            for (int yy = (int)floorf(fy - R) - 1; yy <= (int)ceilf(fy + R) + 1; yy++)
                for (int xx = (int)floorf(fx - R) - 1; xx <= (int)ceilf(fx + R) + 1; xx++) {
                    if (xx < 0 || yy < 0 || xx >= W || yy >= H) continue;
                    if (sd_rrect(xx + 0.5f, yy + 0.5f, pad + w * 0.5f, pad + h * 0.5f, w * 0.5f, h * 0.5f, radius) > -1.5f) continue; // (on the paper only)
                    int hit = 0;
                    for (int sy = 0; sy < 3; sy++)
                        for (int sx = 0; sx < 3; sx++) hit += in_poly(vx, vy, nv, xx + (sx + 0.5f) / 3.0f, yy + (sy + 0.5f) / 3.0f);
                    if (!hit) continue;
                    float crinkle = 0.3f * (vnoise(xx / 1.6f, yy / 1.6f, (uint32_t)(i * 4 + kind) * 7u + seed) - 0.5f); // (leaf is crinkled: its light comes in patches)
                    float cov = hit / 9.0f * alpha, t = clampf(sheen * 0.7f + 0.3f + 0.45f * ((xx - fx) * cosf(gang) + (yy - fy) * sinf(gang)) / (R + 0.5f) + crinkle, 0, 1);
                    float *o = &leaf[((size_t)yy * W + xx) * 4];
                    for (int c = 0; c < 3; c++) o[c] = lerpf(g0[c], g1[c], t) * cov + o[c] * (1.0f - cov);
                    o[3] = cov + o[3] * (1.0f - cov);
                }
        }
    }
    float fc[3], ec[3], cx = pad + w * 0.5f, cy = pad + h * 0.5f;
    rgb01(q->fib_rgb, fc);
    rgb01(q->lantern_edge, ec);
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *o = p->px + ((size_t)y * W + x) * 4;
            if (!o[3]) continue;
            float c3[3] = {o[0] / 255.0f, o[1] / 255.0f, o[2] / 255.0f};
            if (q->lantern_k > 0) { // brightest in the middle, where the light is, darker and warmer towards the edges
                float dx = (x + 0.5f - cx) / (w * 0.5f), dy = (y + 0.5f - cy) / (h * 0.5f), f = smooth01(clampf((sqrtf(dx * dx + dy * dy) - 0.25f) / 0.9f, 0, 1));
                for (int c = 0; c < 3; c++) c3[c] = lerpf(c3[c], ec[c], f * q->lantern_k);
            }
            if (q->cloud_k > 0) {
                float n = (vnoise(x / 34.0f, y / 34.0f, seed ^ 0xC10Du) + 0.5f * vnoise(x / 11.0f, y / 11.0f, seed ^ 0xC10Eu)) / 1.5f;
                for (int c = 0; c < 3; c++) c3[c] *= 1.0f + q->cloud_k * 2.0f * (n - 0.5f);
            }
            float fv = fib[(size_t)y * W + x] * q->fib_k;
            for (int c = 0; c < 3; c++) c3[c] = lerpf(c3[c], fc[c], fv);
            if (q->dust_k > 0) {
                float n = 0.7f * vnoise(x / 26.0f, y / 26.0f, seed ^ 0xD057u) + 0.3f * hashf(x, y, seed ^ 0xD058u), d = q->dust_k * smooth01(clampf((n - 0.35f) * 1.6f, 0, 1));
                static const float DUST[3] = {0.86f, 0.87f, 0.84f};
                for (int c = 0; c < 3; c++) c3[c] = lerpf(c3[c], DUST[c], d);
            }
            if (q->guil_k > 0) { // security print: fine waves across the paper, every 7 px
                float v = fmodf(y + 5.0f * sinf(x / 23.0f + y / 50.0f) + 2.0f * sinf(x / 7.3f) + 700.0f, 7.0f), d = minf(v, 7.0f - v), cov = clampf(0.8f - d, 0, 1);
                float gc[3];
                rgb01(q->guil_rgb, gc);
                for (int c = 0; c < 3; c++) c3[c] = lerpf(c3[c], gc[c], cov * q->guil_k);
            }
            const float *l = &leaf[((size_t)y * W + x) * 4];
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf((l[c] + c3[c] * (1.0f - l[3])) * 255.0f + 0.5f, 0, 255);
        }
    free(fib);
    free(leaf);
}
#endif

PaperSpr *paper_card_ex(int w, int h, float radius, uint32_t seed, int pad)
{
    int W = w + 2 * pad, H = h + 2 * pad;
    PaperSpr *p = paper_spr_new(W, H);
    if (!p) return NULL;
    p->ox = p->oy = pad;
    float cx = pad + w * 0.5f, cy = pad + h * 0.5f, hw = w * 0.5f, hh = h * 0.5f;
    const float tint[3] = {PAL3(grain, 0, 0.55f), PAL3(grain, 1, 0.47f), PAL3(grain, 2, 0.33f)}; // the warm brown the grain multiplies in
    const uint32_t pp = PAL(paper, PAPER_PAPER);
    float base[3] = {((pp >> 16) & 255) / 255.0f, ((pp >> 8) & 255) / 255.0f, (pp & 255) / 255.0f};
    const uint8_t *grain = grain_tile();
    int gx0 = (int)(hash3(1, 2, seed) % GRAIN_N), gy0 = (int)(hash3(3, 4, seed) % GRAIN_N);
    for (int y = 0; y < H; y++) {
        const uint8_t *grow = grain ? grain + (size_t)((y + gy0) % GRAIN_N) * GRAIN_N : NULL;
        for (int x = 0; x < W; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float sd = sd_rrect(px, py, cx, cy, hw, hh, radius);
            if (sd > 2.95f) continue;
            // the cut edge is not perfectly straight: a slow wander of a fraction of a pixel (only where it can show: within a pixel or so of the edge)
            if (sd > -1.4f) sd += (vnoise(px * 0.06f, py * 0.06f, seed ^ 0x51u) - 0.5f) * 0.8f;
            if (sd > 2.5f) continue;
            float cov = clampf(0.5f - sd, 0, 1), outer = clampf(1.5f - sd, 0, 1);
            // light grain: fine speckle with a softer blotch under it, multiplied in as warm brown, a few per cent at most
            float g = PAL(grain_k, 0.085f) * (grow ? grow[(x + gx0) % GRAIN_N] * (1.0f / 255.0f) : 0.0f);
            float pm[3];
            if (g_pal.grain_mode) // (dark paper: the fibres catch the light, so the grain is lighter flecks)
                for (int c = 0; c < 3; c++) pm[c] = base[c] + (tint[c] - base[c]) * g;
            else
                for (int c = 0; c < 3; c++) pm[c] = base[c] * (1.0f - g * (1.0f - tint[c]));
            float ring = PAL(rim_a, 0.06f) * outer * (1.0f - cov); // the faint edge line: black at 6% just outside the cut (Paper night: white)
            float a = cov + ring;
            if (a < 0.0005f) continue;
            uint8_t *o = p->px + ((size_t)y * W + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf((pm[c] * cov + ((g_pal.rim >> (16 - 8 * c)) & 255) / 255.0f * ring) / a * 255.0f + 0.5f, 0, 255);
            o[3] = (uint8_t)clampf(a * 255.0f + 0.5f, 0, 255);
        }
    }
#ifdef WASHI_PAL_LAB
    card_materials(p, w, h, radius, seed, pad);
#endif
    return p;
}

PaperSpr *paper_card(int w, int h, float radius, uint32_t seed) { return paper_card_ex(w, h, radius, seed, PAPER_CARD_PAD); }

// ------------------------------------------------------------------------------------------------ the board
// The pinboard, as the mockup makes it: the board colour, flecked with a light brown, and a little darker towards the corners. The flecks are
// the mockup's own: an SVG feTurbulence (fractal noise, base frequency 0.8, two octaves) whose alpha is stretched (a' = 3a - 1.45, so only the
// high values show) and laid in light brown over the board in a 320 px tile that repeats. Chrome's picture of the mockup is matched to the pixel
// (a correlation of 0.9997 with it): the noise below is the SVG specification's own reference code (Perlin's gradient noise), run in the same way.
enum { TB_SIZE = 0x100, TB_MASK = 0xff, TB_N = 0x1000, TILE = 320 };
static int tb_lat[TB_SIZE + TB_SIZE + 2];
static double tb_grad[4][TB_SIZE + TB_SIZE + 2][2];

static int64_t tb_random(int64_t seed)
{
    int64_t r = 16807 * (seed % 127773) - 2836 * (seed / 127773);
    return r <= 0 ? r + 2147483647 : r;
}

static void tb_init(int64_t seed)
{
    if (seed <= 0) seed = -(seed % (2147483647 - 1)) + 1;
    if (seed > 2147483647 - 1) seed = 2147483647 - 1;
    int i, j, k;
    for (k = 0; k < 4; k++)
        for (i = 0; i < TB_SIZE; i++) {
            tb_lat[i] = i;
            for (j = 0; j < 2; j++) tb_grad[k][i][j] = (double)(((seed = tb_random(seed)) % (TB_SIZE + TB_SIZE)) - TB_SIZE) / TB_SIZE;
            double s = sqrt(tb_grad[k][i][0] * tb_grad[k][i][0] + tb_grad[k][i][1] * tb_grad[k][i][1]);
            tb_grad[k][i][0] /= s;
            tb_grad[k][i][1] /= s;
        }
    while (--i) {
        k = tb_lat[i];
        tb_lat[i] = tb_lat[j = (int)((seed = tb_random(seed)) % TB_SIZE)];
        tb_lat[j] = k;
    }
    for (i = 0; i < TB_SIZE + 2; i++) {
        tb_lat[TB_SIZE + i] = tb_lat[i];
        for (k = 0; k < 4; k++)
            for (j = 0; j < 2; j++) tb_grad[k][TB_SIZE + i][j] = tb_grad[k][i][j];
    }
}

static double tb_noise2(int ch, double vx, double vy)
{
    double t = vx + TB_N;
    int bx0 = ((int)t) & TB_MASK, bx1 = (bx0 + 1) & TB_MASK;
    double rx0 = t - (int)t, rx1 = rx0 - 1.0;
    t = vy + TB_N;
    int by0 = ((int)t) & TB_MASK, by1 = (by0 + 1) & TB_MASK;
    double ry0 = t - (int)t, ry1 = ry0 - 1.0;
    int i = tb_lat[bx0], j = tb_lat[bx1];
    int b00 = tb_lat[i + by0], b10 = tb_lat[j + by0], b01 = tb_lat[i + by1], b11 = tb_lat[j + by1];
    double sx = rx0 * rx0 * (3.0 - 2.0 * rx0), sy = ry0 * ry0 * (3.0 - 2.0 * ry0);
    const double *q = tb_grad[ch][b00];
    double u = rx0 * q[0] + ry0 * q[1];
    q = tb_grad[ch][b10];
    double v = rx1 * q[0] + ry0 * q[1], a = u + sx * (v - u);
    q = tb_grad[ch][b01];
    u = rx0 * q[0] + ry1 * q[1];
    q = tb_grad[ch][b11];
    v = rx1 * q[0] + ry1 * q[1];
    double b = u + sx * (v - u);
    return a + sy * (b - a);
}

// the fleck tile: how strongly the light brown shows at each point (0..255), from the noise's alpha channel
static void board_tile(uint8_t *out, uint32_t seed)
{
    tb_init((int64_t)seed);
    for (int y = 0; y < TILE; y++)
        for (int x = 0; x < TILE; x++) {
            double vx = (x + 1) * 0.8, vy = (y + 1) * 0.8, sum = tb_noise2(3, vx, vy) + tb_noise2(3, vx * 2, vy * 2) / 2.0;
            double a = 3.0 * ((sum + 1.0) / 2.0) - 1.45;
            out[y * TILE + x] = (uint8_t)(a <= 0 ? 0 : a >= 1 ? 255 : a * 255.0 + 0.5);
        }
}

int g_paper_board_flat; // tests: 1 = a plain board, no flecks and no corner shade (a texture is not the same under a shift by whole pixels)

#ifdef WASHI_PAL_LAB
// mockups only: the board as a chalkboard: a haze of chalk dust, and broad eraser swipes with the dust dragged along them in streaks
static void board_chalk(Surf *s, uint32_t seed)
{
    static const float CH[3] = {226.0f, 230.0f, 222.0f};
    PaperRng r;
    paper_rng_seed(&r, seed ^ 0xC4A1u);
    float sy[4], amp[4], lam[4], ph[4], bw[4];
    for (int k = 0; k < 4; k++) {
        sy[k] = s->h * (0.12f + 0.78f * paper_rng_f(&r));
        amp[k] = 20.0f + 50.0f * paper_rng_f(&r);
        lam[k] = 160.0f + 260.0f * paper_rng_f(&r);
        ph[k] = paper_rng_f(&r) * 6.2831853f;
        bw[k] = 26.0f + 40.0f * paper_rng_f(&r);
    }
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            float a = 0.035f * vnoise(x / 170.0f, y / 110.0f, seed ^ 0xA1u) + 0.025f * vnoise(x / 60.0f, y / 40.0f, seed ^ 0xA2u);
            for (int k = 0; k < 4; k++) {
                float d = fabsf(y - (sy[k] + amp[k] * sinf(x / lam[k] * 6.2831853f + ph[k]))) / bw[k];
                if (d < 1.0f) a += (1.0f - d * d) * (0.35f + 0.65f * vnoise(x / 26.0f, y / 2.5f, seed ^ (0xB0u + k))) * 0.05f;
            }
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)(o[c] + (CH[c] - o[c]) * a + 0.5f);
        }
}

// mockups only: the board as a night scene in cut paper, as a shadow box is made: a sky of deep indigo going to plum at the horizon, stars punched out
// of it, a paper moon, and four layers of hills cut with wavy tops, each darker and nearer, each throwing a soft shadow onto the one behind and catching
// a little light along its cut edge
static void board_scene(Surf *s, uint32_t seed)
{
    static const float TOP[3] = {27, 29, 58}, HOR[3] = {66, 44, 88};
    static const struct { uint32_t rgb; float base, amp, lam, ph, amp2, lam2; } L[4] = {
        {0x4A3A6E, 0.60f, 26, 520, 0.4f, 10, 170},
        {0x33406A, 0.69f, 30, 430, 2.1f, 9, 140},
        {0x283A4E, 0.78f, 22, 360, 4.0f, 8, 120},
        {0x1C2A36, 0.88f, 16, 300, 1.2f, 6, 95},
    };
    const int W = s->w, H = s->h;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float c[3], t = clampf(y / (H * 0.72f), 0, 1);
            for (int k = 0; k < 3; k++) c[k] = TOP[k] + (HOR[k] - TOP[k]) * t;
            float edge[4];
            for (int k = 0; k < 4; k++)
                edge[k] = L[k].base * H - L[k].amp * sinf(x / L[k].lam * 6.2831853f + L[k].ph) - L[k].amp2 * sinf(x / L[k].lam2 * 6.2831853f + L[k].ph * 1.7f);
            int front = -1;
            for (int k = 3; k >= 0; k--)
                if (y >= edge[k]) {
                    front = k;
                    break;
                }
            if (front >= 0) {
                float lc[3];
                rgb01(L[front].rgb, lc);
                for (int k = 0; k < 3; k++) c[k] = lc[k] * 255.0f;
                float d = y - edge[front]; // (its cut edge catches a little light)
                if (d < 1.5f) for (int k = 0; k < 3; k++) c[k] += (255.0f - c[k]) * 0.10f * (1.5f - d) / 1.5f;
            }
            if (front < 3) { // the layer in front of this one throws a soft shadow onto it, just above its own edge
                float d = edge[front + 1] - y;
                if (d > 0 && d < 12.0f) {
                    float sh = 0.38f * (1.0f - d / 12.0f) * (1.0f - d / 12.0f);
                    for (int k = 0; k < 3; k++) c[k] *= 1.0f - sh;
                }
            }
            float g = 1.0f + 0.05f * (vnoise(x / 3.0f, y / 3.0f, seed ^ 0x9A1u) - 0.5f) + 0.04f * (vnoise(x / 40.0f, y / 40.0f, seed ^ 0x9A2u) - 0.5f); // (paper)
            uint8_t *o = s->px + ((size_t)y * W + x) * 4;
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)clampf(c[k] * g + 0.5f, 0, 255);
        }
    // stars punched out of the sky (the paper behind shows pale through each hole), and the moon, a disc of cream paper with its own soft shadow
    PaperRng r;
    paper_rng_seed(&r, seed ^ 0x57A2u);
    for (int i = 0; i < 70; i++) {
        float sx = paper_rng_f(&r) * W, sy = paper_rng_f(&r) * H * 0.55f, rad = 0.9f + 1.4f * paper_rng_f(&r) * paper_rng_f(&r);
        for (int y = (int)(sy - rad) - 1; y <= (int)(sy + rad) + 1; y++)
            for (int x = (int)(sx - rad) - 1; x <= (int)(sx + rad) + 1; x++) {
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                float a = clampf(rad + 0.5f - hypotf(x + 0.5f - sx, y + 0.5f - sy), 0, 1) * 0.85f;
                uint8_t *o = s->px + ((size_t)y * W + x) * 4;
                static const float ST[3] = {236, 226, 196};
                for (int k = 0; k < 3; k++) o[k] = (uint8_t)(o[k] + (ST[k] - o[k]) * a + 0.5f);
            }
    }
    const float mx = 556, my = 46, mr = 30;
    for (int y = (int)(my - mr) - 8; y <= (int)(my + mr) + 10; y++)
        for (int x = (int)(mx - mr) - 8; x <= (int)(mx + mr) + 10; x++) {
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            uint8_t *o = s->px + ((size_t)y * W + x) * 4;
            float ds = hypotf(x + 0.5f - mx - 2.0f, y + 0.5f - my - 3.0f), sh = clampf((mr + 5.0f - ds) / 6.0f, 0, 1) * 0.45f;
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)(o[k] * (1.0f - sh) + 0.5f);
            float d = hypotf(x + 0.5f - mx, y + 0.5f - my), a = clampf(mr + 0.5f - d, 0, 1);
            if (a <= 0) continue;
            float lit = 0.9f + 0.1f * (vnoise(x / 6.0f, y / 6.0f, seed ^ 0x300Du)) - 0.08f * clampf((x - mx + 8.0f) / mr, 0, 1); // (paper, a shade darker on one side)
            static const float MO[3] = {236, 226, 196};
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)clampf(o[k] + (MO[k] * lit - o[k]) * a + 0.5f, 0, 255);
        }
}

#endif

// Paper night, the dark theme's board (board_scene 2): a night scene cut from paper, made like a shadow box. The sky washed onto its paper, stars punched
// round and a few with a star punch, a moon of layered paper with its craters, two paper clouds crossing it, four layers of hills cut by hand (the scissors'
// wobble), each layer with its own paper grain and a deeper shadow where it stands off the one behind; and in the corner no card covers, a little house on
// the third hill with its window lit, and pines along the front one.
static void scene2_star(Surf *s, float cx, float cy, float R, float rot, const float col3[3], float a);

static void board_scene2(Surf *s, uint32_t seed)
{
    static const float TOP[3] = {25, 27, 56}, HOR[3] = {68, 44, 90};
    static const struct { uint32_t rgb; float base, amp, lam, ph, amp2, lam2; } L[4] = {
        {0x4A3A6E, 0.60f, 26, 520, 0.4f, 10, 170},
        {0x33406A, 0.69f, 30, 430, 2.1f, 9, 140},
        {0x283A4E, 0.78f, 22, 360, 4.0f, 8, 120},
        {0x1C2A36, 0.88f, 16, 300, 1.2f, 6, 95},
    };
    const int W = s->w, H = s->h;
    float *edge = malloc((size_t)W * 4 * sizeof(float));
    if (!edge) return;
    for (int k = 0; k < 4; k++)
        for (int x = 0; x < W; x++) {
            float e = L[k].base * H - L[k].amp * sinf(x / L[k].lam * 6.2831853f + L[k].ph) - L[k].amp2 * sinf(x / L[k].lam2 * 6.2831853f + L[k].ph * 1.7f);
            e += 1.6f * (vnoise(x / 9.0f, k * 13.7f, seed ^ 0xC07u) - 0.5f) + 0.7f * (vnoise(x / 2.5f, k * 5.1f, seed ^ 0xC08u) - 0.5f); // (cut by hand)
            edge[k * W + x] = e;
        }
    // the house on the third hill, its roof and chimney part of the same cut, and pines along the front hill (only on a screen-sized board: the palette's
    // own measuring of the board uses a small one)
    const float hx = 1000.0f, body = 27.0f, half = 19.0f, roof = 17.0f;
    const int house = hx + half + 1 < W && H > 600;
    const float hbase = house ? edge[2 * W + (int)hx] : 0.0f;
    for (int x = (int)(hx - half); house && x <= (int)(hx + half); x++) {
        float top = hbase - body - roof * (1.0f - fabsf(x - hx) / half);
        if (x >= (int)hx + 5 && x <= (int)hx + 9) top = minf(top, hbase - body - roof * (1.0f - 7.0f / half) - 8.0f);
        edge[2 * W + x] = minf(edge[2 * W + x], top);
    }
    static const float TREES[][3] = {{708, 22, 9}, {726, 30, 11}, {744, 18, 8}, {766, 34, 12}, {788, 24, 10}, {812, 28, 11}, {1128, 26, 10}, {1150, 34, 12}, {1171, 22, 9}};
    for (size_t i = 0; i < sizeof TREES / sizeof TREES[0]; i++) {
        if (TREES[i][0] + TREES[i][2] + 1 >= W) continue;
        float tx = TREES[i][0], th = TREES[i][1], tw = TREES[i][2], g = edge[3 * W + (int)tx];
        for (int x = (int)(tx - tw); x <= (int)(tx + tw); x++) {
            if (x < 0 || x >= W) continue;
            float f = 1.0f - fabsf(x - tx) / tw, top = g - th * f;
            if (fabsf(x - tx) < 2.0f) top = minf(top, g - th - 3.0f); // (its tip)
            edge[3 * W + x] = minf(edge[3 * W + x], top);
        }
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float c[3], t = clampf(y / (H * 0.72f), 0, 1), wash = 1.0f + 0.05f * (vnoise(x / 140.0f, y / 90.0f, seed ^ 0xA5u) - 0.5f) + 0.03f * (vnoise(x / 35.0f, y / 35.0f, seed ^ 0xA6u) - 0.5f);
            for (int k = 0; k < 3; k++) c[k] = (TOP[k] + (HOR[k] - TOP[k]) * t) * wash;
            int front = -1;
            for (int k = 3; k >= 0; k--)
                if (y >= edge[k * W + x]) {
                    front = k;
                    break;
                }
            if (front >= 0) {
                float lc[3], gr = 1.0f + 0.045f * (vnoise(x / 2.2f, y / 2.2f, seed + 31u * front) - 0.5f) + 0.035f * (vnoise(x / 30.0f, y / 30.0f, seed + 77u * front) - 0.5f);
                rgb01(L[front].rgb, lc);
                for (int k = 0; k < 3; k++) c[k] = lc[k] * 255.0f * gr;
                float d = y - edge[front * W + x];
                if (d < 1.5f) for (int k = 0; k < 3; k++) c[k] += (255.0f - c[k]) * 0.12f * (1.5f - d) / 1.5f;
            }
            if (front < 3) { // the layer in front stands off this one: a deeper shadow just above its edge, fading over 18 px
                float d = edge[(front + 1) * W + x] - y;
                if (d > 0 && d < 18.0f) {
                    float sh = 0.5f * expf(-d / 6.0f);
                    for (int k = 0; k < 3; k++) c[k] *= 1.0f - sh;
                }
            }
            float g = 1.0f + 0.04f * (vnoise(x / 3.0f, y / 3.0f, seed ^ 0x9A1u) - 0.5f);
            uint8_t *o = s->px + ((size_t)y * W + x) * 4;
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)clampf(c[k] * g + 0.5f, 0, 255);
        }
    // the house's window, lit: a square cut out with warm paper behind it, two bars across
    for (int y = (int)(hbase - 21); house && y < (int)(hbase - 9); y++)
        for (int x = (int)(hx - 7); x < (int)(hx + 7); x++) {
            uint8_t *o = s->px + ((size_t)y * W + x) * 4;
            int bar = x == (int)hx || y == (int)(hbase - 15);
            o[0] = bar ? 0x2A : 0xF2, o[1] = bar ? 0x2C : 0xC8, o[2] = bar ? 0x3A : 0x6E;
        }
    free(edge);
    // stars: most punched round, a few with a star punch
    static const float ST[3] = {236, 226, 196};
    PaperRng r;
    paper_rng_seed(&r, seed ^ 0x57A2u);
    for (int i = 0; i < 76; i++) {
        float sx = paper_rng_f(&r) * W, sy = paper_rng_f(&r) * H * 0.55f, rad = 0.9f + 1.4f * paper_rng_f(&r) * paper_rng_f(&r);
        if (i % 11 == 5) {
            scene2_star(s, sx, sy, 3.6f + 2.0f * paper_rng_f(&r), paper_rng_f(&r) * 1.2566f, ST, 0.92f);
            continue;
        }
        for (int y = (int)(sy - rad) - 1; y <= (int)(sy + rad) + 1; y++)
            for (int x = (int)(sx - rad) - 1; x <= (int)(sx + rad) + 1; x++) {
                if (x < 0 || y < 0 || x >= W || y >= H) continue;
                float a = clampf(rad + 0.5f - hypotf(x + 0.5f - sx, y + 0.5f - sy), 0, 1) * 0.85f;
                uint8_t *o = s->px + ((size_t)y * W + x) * 4;
                for (int k = 0; k < 3; k++) o[k] = (uint8_t)(o[k] + (ST[k] - o[k]) * a + 0.5f);
            }
    }
    scene2_star(s, 408, 30, 5.5f, 0.2f, ST, 0.95f);
    scene2_star(s, 742, 22, 4.5f, 0.9f, ST, 0.95f);
    // the moon: a disc of cream paper standing off the sky (its shadow), three craters of a slightly darker paper laid on it
    const float mx = 556, my = 46, mr = 30;
    for (int y = (int)(my - mr) - 8; y <= (int)(my + mr) + 12; y++)
        for (int x = (int)(mx - mr) - 8; x <= (int)(mx + mr) + 12; x++) {
            if (x < 0 || y < 0 || x >= W || y >= H) continue;
            uint8_t *o = s->px + ((size_t)y * W + x) * 4;
            float ds = hypotf(x + 0.5f - mx - 3.0f, y + 0.5f - my - 4.0f), sh = clampf((mr + 6.0f - ds) / 7.0f, 0, 1) * 0.5f;
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)(o[k] * (1.0f - sh) + 0.5f);
            float d = hypotf(x + 0.5f - mx, y + 0.5f - my), a = clampf(mr + 0.5f - d, 0, 1);
            if (a <= 0) continue;
            float lit = 0.9f + 0.1f * vnoise(x / 6.0f, y / 6.0f, seed ^ 0x300Du) - 0.08f * clampf((x - mx + 8.0f) / mr, 0, 1);
            static const float CR[3][3] = {{-9, -8, 7}, {10, 6, 5}, {-4, 13, 4}};
            for (int c = 0; c < 3; c++) {
                float dc = hypotf(x + 0.5f - mx - CR[c][0], y + 0.5f - my - CR[c][1]);
                if (dc < CR[c][2]) lit -= 0.07f * clampf(CR[c][2] + 0.5f - dc, 0, 1);
                else if (dc < CR[c][2] + 1.2f && y > my + CR[c][1]) lit += 0.04f; // (the crater's paper edge catching the light)
            }
            static const float MO[3] = {236, 226, 196};
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)clampf(o[k] + (MO[k] * lit - o[k]) * a + 0.5f, 0, 255);
        }
    // two small clouds of lavender paper drifting over the top, one just over the moon's lower edge, each standing off the sky, their tops catching the
    // moonlight along the cut
    static const struct { float x, y, w; } CL[2] = {{604, 80, 92}, {372, 42, 70}};
    for (int i = 0; i < 2; i++) {
        float cx = CL[i].x, cy = CL[i].y, w = CL[i].w;
        const float bumps[4][3] = {{-0.30f, 0.05f, 0.20f}, {-0.05f, -0.10f, 0.27f}, {0.22f, -0.02f, 0.22f}, {0.38f, 0.08f, 0.15f}};
        for (int pass = 0; pass < 2; pass++)
            for (int y = (int)(cy - w * 0.5f); y <= (int)(cy + w * 0.3f) + 6; y++)
                for (int x = (int)(cx - w * 0.6f); x <= (int)(cx + w * 0.6f) + 6; x++) {
                    if (x < 0 || y < 0 || x >= W || y >= H) continue;
                    float px = x + 0.5f - (pass ? cx : cx + 3.0f), py = y + 0.5f - (pass ? cy : cy + 4.0f), dmin = 1e9f;
                    for (int b = 0; b < 4; b++) {
                        float dx = px - bumps[b][0] * w, dy = py - bumps[b][1] * w;
                        dmin = minf(dmin, sqrtf(dx * dx + dy * dy) - bumps[b][2] * w);
                    }
                    dmin = maxf(dmin, py - w * 0.14f); // (cut flat underneath)
                    float in = clampf(0.5f - dmin, 0, 1);
                    if (in <= 0) continue;
                    uint8_t *o = s->px + ((size_t)y * W + x) * 4;
                    if (!pass) {
                        for (int k = 0; k < 3; k++) o[k] = (uint8_t)(o[k] * (1.0f - 0.32f * in) + 0.5f);
                        continue;
                    }
                    static const float CC[3] = {116, 106, 168};
                    float gl = 1.0f + 0.04f * (vnoise(x / 2.4f, y / 2.4f, seed ^ (0xC1u + i)) - 0.5f);
                    float rim = (py < w * 0.05f) ? clampf(1.0f + dmin / 2.0f, 0, 1) * 0.22f : 0.0f; // (lit along its top)
                    for (int k = 0; k < 3; k++) {
                        float v = CC[k] * gl;
                        v += (240.0f - v) * rim;
                        o[k] = (uint8_t)clampf(o[k] + (v - o[k]) * in + 0.5f, 0, 255);
                    }
                }
    }
}

static void scene2_star(Surf *s, float cx, float cy, float R, float rot, const float col3[3], float a)
{
    float vx[10], vy[10];
    for (int k = 0; k < 10; k++) {
        float rr = (k & 1) ? R * 0.42f : R, an = rot + k * (PI_F / 5.0f) - PI_F / 2.0f;
        vx[k] = cx + rr * cosf(an);
        vy[k] = cy + rr * sinf(an);
    }
    for (int y = (int)(cy - R) - 1; y <= (int)(cy + R) + 1; y++)
        for (int x = (int)(cx - R) - 1; x <= (int)(cx + R) + 1; x++) {
            if (x < 0 || y < 0 || x >= s->w || y >= s->h) continue;
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) hit += in_poly(vx, vy, 10, x + (sx + 0.5f) / 4.0f, y + (sy + 0.5f) / 4.0f);
            if (!hit) continue;
            float k2 = a * hit / 16.0f;
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            for (int k = 0; k < 3; k++) o[k] = (uint8_t)(o[k] + (col3[k] - o[k]) * k2 + 0.5f);
        }
}

#ifdef WASHI_PAL_LAB
// mockups only: a passport page's security printing in gold: a rosette in the corner no card covers, and bands of fine waves across the page
static void board_passport(Surf *s, uint32_t seed)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) { // the page's paper
            float n = 0.03f * (vnoise(x / 2.5f, y / 2.5f, seed ^ 0x9A55u) - 0.5f) + 0.035f * (vnoise(x / 90.0f, y / 70.0f, seed ^ 0x9A56u) - 0.5f);
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf(o[c] * (1.0f + n) + 0.5f, 0, 255);
        }
    Col gold = col(0.80f, 0.66f, 0.36f, 0.16f), gold2 = col(0.80f, 0.66f, 0.36f, 0.13f);
    for (int j = 0; j < 20; j++) { // the rosette: a flower of fine curves, each a circle waved nine times round, turned a little from the last
        float ph = j * 6.2831853f / 20.0f, px = 0, py = 0;
        for (int i = 0; i <= 900; i++) {
            float th = i * 6.2831853f / 900.0f, r = 112.0f + 30.0f * sinf(9.0f * th + ph), x = 950.0f + r * cosf(th), y = 566.0f + r * 0.74f * sinf(th);
            if (i) gfx_line(s, px, py, x, y, 0.6f, gold);
            px = x, py = y;
        }
    }
    static const float BANDS[] = {58.0f, 474.0f, 652.0f};
    for (size_t b = 0; b < sizeof BANDS / sizeof BANDS[0]; b++) // bands of fine waves, each line a little out of step with the one above
        for (int i = 0; i < 12; i++) {
            float px = 0, py = 0;
            for (int x = 0; x <= s->w; x += 3) {
                float y = BANDS[b] + i * 2.4f + 7.0f * sinf(x / 58.0f + i * 0.28f) + 3.0f * sinf(x / 23.0f - i * 0.4f);
                if (x) gfx_line(s, (float)px, py, (float)x, y, 0.7f, gold2);
                px = x, py = y;
            }
        }
}

// mockups only: a blueprint's cyanotype paper (the wash a little uneven) and its grid
static void board_blueprint(Surf *s, uint32_t seed)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            float n = 0.05f * (vnoise(x / 120.0f, y / 90.0f, seed ^ 0xB1Eu) - 0.5f) + 0.03f * (vnoise(x / 2.6f, y / 2.6f, seed ^ 0xB1Fu) - 0.5f);
            int minor = (x % 24 == 0) || (y % 24 == 0), major = (x % 120 == 0) || (y % 120 == 0);
            float gl = major ? 0.13f : minor ? 0.06f : 0.0f;
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            for (int c = 0; c < 3; c++) {
                float v = o[c] * (1.0f + n);
                o[c] = (uint8_t)clampf(v + (235.0f - v) * gl + 0.5f, 0, 255);
            }
        }
}

// mockups only: filled shapes for the props (4 x 4 samples a pixel)
static void lab_poly(Surf *s, const float *vx, const float *vy, int n, Col c)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        x0 = minf(x0, vx[i]), y0 = minf(y0, vy[i]), x1 = maxf(x1, vx[i]), y1 = maxf(y1, vy[i]);
    }
    for (int y = (int)floorf(y0); y <= (int)ceilf(y1); y++)
        for (int x = (int)floorf(x0); x <= (int)ceilf(x1); x++) {
            if (x < 0 || y < 0 || x >= s->w || y >= s->h) continue;
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) hit += in_poly(vx, vy, n, x + (sx + 0.5f) / 4.0f, y + (sy + 0.5f) / 4.0f);
            if (!hit) continue;
            float a = c.a * hit / 16.0f;
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            o[0] = (uint8_t)(o[0] + (c.r * 255.0f - o[0]) * a + 0.5f);
            o[1] = (uint8_t)(o[1] + (c.g * 255.0f - o[1]) * a + 0.5f);
            o[2] = (uint8_t)(o[2] + (c.b * 255.0f - o[2]) * a + 0.5f);
        }
}

static void lab_quad(Surf *s, float ax, float ay, float bx, float by, float cx, float cy, float dx, float dy, Col c)
{
    const float vx[4] = {ax, bx, cx, dx}, vy[4] = {ay, by, cy, dy};
    lab_poly(s, vx, vy, 4, c);
}

static void lab_tri(Surf *s, float ax, float ay, float bx, float by, float cx, float cy, Col c)
{
    const float vx[3] = {ax, bx, cx}, vy[3] = {ay, by, cy};
    lab_poly(s, vx, vy, 3, c);
}

void pal_suit(Surf *s, float cx, float cy, float z, int suit, Col c)
{
    switch (suit & 3) {
    case 0: // spade: a heart upside down, and a foot
        gfx_circle(s, cx - 0.22f * z, cy + 0.06f * z, 0.26f * z, c);
        gfx_circle(s, cx + 0.22f * z, cy + 0.06f * z, 0.26f * z, c);
        lab_tri(s, cx - 0.47f * z, cy + 0.12f * z, cx + 0.47f * z, cy + 0.12f * z, cx, cy - 0.5f * z, c);
        lab_tri(s, cx, cy + 0.05f * z, cx - 0.17f * z, cy + 0.5f * z, cx + 0.17f * z, cy + 0.5f * z, c);
        break;
    case 1: // heart
        gfx_circle(s, cx - 0.22f * z, cy - 0.12f * z, 0.27f * z, c);
        gfx_circle(s, cx + 0.22f * z, cy - 0.12f * z, 0.27f * z, c);
        lab_tri(s, cx - 0.48f * z, cy - 0.06f * z, cx + 0.48f * z, cy - 0.06f * z, cx, cy + 0.48f * z, c);
        break;
    case 2: // diamond
        lab_quad(s, cx, cy - 0.5f * z, cx + 0.36f * z, cy, cx, cy + 0.5f * z, cx - 0.36f * z, cy, c);
        break;
    default: // club
        gfx_circle(s, cx, cy - 0.24f * z, 0.21f * z, c);
        gfx_circle(s, cx - 0.23f * z, cy + 0.07f * z, 0.21f * z, c);
        gfx_circle(s, cx + 0.23f * z, cy + 0.07f * z, 0.21f * z, c);
        lab_tri(s, cx, cy - 0.05f * z, cx - 0.16f * z, cy + 0.5f * z, cx + 0.16f * z, cy + 0.5f * z, c);
        break;
    }
}

// a soft shadow under a prop (a disc of shade, blurred by layering)
static void lab_shade(Surf *s, float cx, float cy, float rx, float ry, float k)
{
    for (int y = (int)(cy - ry * 1.6f); y <= (int)(cy + ry * 1.6f); y++)
        for (int x = (int)(cx - rx * 1.6f); x <= (int)(cx + rx * 1.6f); x++) {
            if (x < 0 || y < 0 || x >= s->w || y >= s->h) continue;
            float dx = (x + 0.5f - cx) / rx, dy = (y + 0.5f - cy) / ry, d = sqrtf(dx * dx + dy * dy), a = k * (1.0f - smooth01(clampf((d - 0.55f) / 0.95f, 0, 1)));
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)(o[c] * (1.0f - a) + 0.5f);
        }
}

// a six-sided die seen from above, turned by ang, `z` px across: its top face, the pips, a lit edge
static void lab_d6(Surf *s, float cx, float cy, float z, float ang, int pips, Col body, Col pip)
{
    lab_shade(s, cx + 4, cy + 6, z * 0.62f, z * 0.62f, 0.55f);
    float c = cosf(ang), sn = sinf(ang), h = z * 0.5f, r = z * 0.16f;
    // the face as a rounded square: a polygon round its corners
    float vx[32], vy[32];
    int n = 0;
    for (int k = 0; k < 4; k++) {
        float qx = (k == 0 || k == 3) ? h - r : -(h - r), qy = (k < 2) ? h - r : -(h - r);
        for (int j = 0; j < 8; j++) {
            float a0 = (k * 90.0f + j * (90.0f / 7.0f)) * (PI_F / 180.0f), px = qx + r * cosf(a0), py = qy + r * sinf(a0);
            vx[n] = cx + px * c - py * sn;
            vy[n] = cy + px * sn + py * c;
            n++;
        }
    }
    lab_poly(s, vx, vy, n, body);
    static const float PX[7][7][2] = {{{0}}, {{0, 0}}, {{-1, -1}, {1, 1}}, {{-1, -1}, {0, 0}, {1, 1}}, {{-1, -1}, {1, -1}, {-1, 1}, {1, 1}},
                                      {{-1, -1}, {1, -1}, {0, 0}, {-1, 1}, {1, 1}}, {{-1, -1}, {1, -1}, {-1, 0}, {1, 0}, {-1, 1}, {1, 1}}};
    for (int i = 0; i < pips && pips <= 6; i++) {
        float px = PX[pips][i][0] * z * 0.24f, py = PX[pips][i][1] * z * 0.24f;
        gfx_circle(s, cx + px * c - py * sn, cy + px * sn + py * c, z * 0.075f, pip);
    }
}

// the game table's things in its empty places: dice, a d20, a meeple, two tokens
static void board_felt(Surf *s, uint32_t seed)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) { // the felt's fuzz, and the lamp over the table
            float fz = 0.06f * (hashf(x, y, seed ^ 0xFE17u) - 0.5f) + 0.05f * (vnoise(x / 5.0f, y / 2.0f, seed ^ 0xFE18u) - 0.5f) +
                       0.04f * (vnoise(x / 60.0f, y / 60.0f, seed ^ 0xFE19u) - 0.5f);
            float dx = (x - 640.0f) / 820.0f, dy = (y - 330.0f) / 560.0f, lamp = 0.14f * (1.0f - smooth01(clampf(sqrtf(dx * dx + dy * dy), 0, 1)));
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf(o[c] * (1.0f + fz + lamp) + 0.5f, 0, 255);
        }
    lab_d6(s, 1028, 588, 46, 0.21f, 3, col(0.95f, 0.93f, 0.88f, 1), col(0.10f, 0.10f, 0.12f, 1));
    lab_d6(s, 1110, 622, 36, -0.35f, 5, col(0.80f, 0.20f, 0.18f, 1), col(0.98f, 0.96f, 0.92f, 1));
    // a d20: a hexagon of purple facets, lighter ones on top
    {
        float cx = 878, cy = 596, R = 30;
        lab_shade(s, cx + 4, cy + 7, R * 0.95f, R * 0.95f, 0.5f);
        float vx[6], vy[6];
        for (int k = 0; k < 6; k++) {
            float a = (k * 60.0f - 90.0f + 8.0f) * (PI_F / 180.0f);
            vx[k] = cx + R * cosf(a);
            vy[k] = cy + R * sinf(a);
        }
        lab_poly(s, vx, vy, 6, col(0.42f, 0.27f, 0.62f, 1));
        float tx[3], ty[3];
        for (int k = 0; k < 3; k++) {
            float a = (k * 120.0f - 90.0f + 8.0f) * (PI_F / 180.0f);
            tx[k] = cx + R * 0.56f * cosf(a);
            ty[k] = cy + R * 0.56f * sinf(a);
        }
        lab_poly(s, tx, ty, 3, col(0.60f, 0.45f, 0.80f, 1));
        for (int k = 0; k < 6; k++) gfx_line(s, tx[(k + 1) / 2 % 3], ty[(k + 1) / 2 % 3], vx[k], vy[k], 1.2f, col(0.30f, 0.18f, 0.45f, 0.9f));
        for (int k = 0; k < 3; k++) gfx_line(s, tx[k], ty[k], tx[(k + 1) % 3], ty[(k + 1) % 3], 1.2f, col(0.30f, 0.18f, 0.45f, 0.9f));
    }
    // a meeple, standing, seen from above-front
    {
        float cx = 960, cy = 610, z = 46;
        lab_shade(s, cx + 5, cy + z * 0.42f, z * 0.5f, z * 0.2f, 0.55f);
        Col m = col(0.97f, 0.78f, 0.25f, 1);
        gfx_circle(s, cx, cy - z * 0.30f, z * 0.15f, m);
        const float vx[10] = {cx - z * 0.10f, cx + z * 0.10f, cx + z * 0.42f, cx + z * 0.40f, cx + z * 0.16f, cx + z * 0.30f, cx - z * 0.30f, cx - z * 0.16f,
                              cx - z * 0.40f, cx - z * 0.42f};
        const float vy[10] = {cy - z * 0.20f, cy - z * 0.20f, cy - z * 0.10f, cy + z * 0.04f, cy + z * 0.04f, cy + z * 0.44f, cy + z * 0.44f, cy + z * 0.04f,
                              cy + z * 0.04f, cy - z * 0.10f};
        lab_poly(s, vx, vy, 10, m);
    }
    // two tokens
    lab_shade(s, 780 + 3, 620 + 5, 16, 16, 0.5f);
    gfx_circle(s, 780, 620, 15, col(0.30f, 0.46f, 0.82f, 1));
    gfx_ring(s, 780, 620, 10.5f, 2.0f, col(0.85f, 0.90f, 1.0f, 0.8f));
    lab_shade(s, 1160, 548, 13, 13, 0.5f);
    gfx_circle(s, 1157, 544, 12, col(0.93f, 0.90f, 0.82f, 1));
    gfx_ring(s, 1157, 544, 8.0f, 1.8f, col(0.80f, 0.20f, 0.18f, 0.8f));
}

// the craft wall: a painted pegboard's holes, and rolls of the app's own washi tape hanging on brass hooks, each with its loose end hanging
static void tape_roll_front(Surf *s, float cx, float cy, float R, uint32_t rgb, int kind, uint32_t seed)
{
    lab_shade(s, cx + 5, cy + 7, R * 1.05f, R * 1.05f, 0.6f);
    // the loose end, hanging from the bottom of the roll
    PaperTape t = {(int)(R * 0.9f), (int)(R * 0.62f), rgb, kind, PAPER_TAPE_ZIG_R | PAPER_TAPE_UPRIGHT, 0.0f, seed};
    PaperSpr *tail = paper_tape(&t);
    if (tail) {
        paper_spr_xform(s, tail, cx + R * 0.25f, cy + R + R * 0.30f, 1.0f, 92.0f * (PI_F / 180.0f), 1.0f);
        paper_spr_free(tail);
    }
    float tc[3];
    for (int c = 0; c < 3; c++) tc[c] = ((rgb >> (16 - 8 * c)) & 255) / 255.0f;
    float rc = R * 0.56f, rh = R * 0.44f; // the cardboard core's outside and its hole
    for (int y = (int)(cy - R) - 1; y <= (int)(cy + R) + 1; y++)
        for (int x = (int)(cx - R) - 1; x <= (int)(cx + R) + 1; x++) {
            if (x < 0 || y < 0 || x >= s->w || y >= s->h) continue;
            float d = hypotf(x + 0.5f - cx, y + 0.5f - cy);
            float a = clampf(R + 0.5f - d, 0, 1) * clampf(d - rh + 0.5f, 0, 1);
            if (a <= 0) continue;
            float col3[3];
            if (d > rc) { // the tape, wound: faint rings, darker where it is wound tighter at the core, a darker outer edge
                float ring = 0.03f * sinf(d * 2.3f) + 0.02f * (hashf((int)(d * 3), 0, seed) - 0.5f), dk = 0.82f + 0.18f * (d - rc) / (R - rc) - (d > R - 1.2f ? 0.12f : 0);
                for (int c = 0; c < 3; c++) col3[c] = clampf(tc[c] * (dk + ring), 0, 1);
            } else { // the core
                static const float CORE[3] = {0.72f, 0.56f, 0.37f};
                float dk = d < rh + 1.2f ? 0.7f : 1.0f;
                for (int c = 0; c < 3; c++) col3[c] = CORE[c] * dk;
            }
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)(o[c] + (col3[c] * 255.0f - o[c]) * a + 0.5f);
        }
    // the hook: out of the hole above, down through the core, its tip bent up inside
    Col brass = col(0.78f, 0.62f, 0.32f, 1);
    gfx_line(s, cx, cy - R - 14, cx, cy - rh + 3, 3.2f, brass);
    gfx_circle(s, cx, cy - R - 14, 3.6f, col(0.62f, 0.48f, 0.22f, 1));
    gfx_line(s, cx, cy - rh + 3, cx + 5, cy - rh - 4, 3.0f, brass);
}

static void board_peg(Surf *s, uint32_t seed)
{
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) { // the paint over hardboard
            float n = 0.035f * (vnoise(x / 3.0f, y / 3.0f, seed ^ 0x9E61u) - 0.5f) + 0.03f * (vnoise(x / 70.0f, y / 50.0f, seed ^ 0x9E62u) - 0.5f);
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf(o[c] * (1.0f + n) + 0.5f, 0, 255);
        }
    const float P = 26.0f, hr = 3.3f;
    for (float hy = 13; hy < s->h; hy += P)
        for (float hx = 13; hx < s->w; hx += P) { // each hole: dark inside, its lower rim catching the light from above
            for (int y = (int)(hy - hr) - 2; y <= (int)(hy + hr) + 2; y++)
                for (int x = (int)(hx - hr) - 2; x <= (int)(hx + hr) + 2; x++) {
                    if (x < 0 || y < 0 || x >= s->w || y >= s->h) continue;
                    float dx = x + 0.5f - hx, dy = y + 0.5f - hy, d = sqrtf(dx * dx + dy * dy);
                    uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
                    float in = clampf(hr + 0.5f - d, 0, 1), rim = clampf(1.2f - fabsf(d - hr - 0.6f), 0, 1) * clampf(dy / hr, 0, 1);
                    for (int c = 0; c < 3; c++) {
                        float v = o[c] * (1.0f - 0.78f * in);
                        v += (255.0f - v) * 0.10f * rim;
                        o[c] = (uint8_t)(v + 0.5f);
                    }
                }
        }
    // the tape on hooks (in the places no card covers), and torn pieces stuck on for later
    tape_roll_front(s, 793, 556, 38, paper_tape_rgb[0], PAT_DOTS, 401);
    tape_roll_front(s, 923, 570, 33, paper_tape_rgb[3], PAT_GRID, 402);
    tape_roll_front(s, 1053, 554, 42, paper_tape_rgb[6], PAT_STRIPES, 403);
    static const struct { float x, y, tilt; int c, kind; } BITS[] = {{372, 40, -6, 2, PAT_ZIG}, {446, 58, 4, 5, PAT_DOTS}, {520, 36, -3, 1, PAT_STRIPES}};
    for (size_t i = 0; i < sizeof BITS / sizeof BITS[0]; i++) {
        PaperTape t = {58, 22, paper_tape_rgb[BITS[i].c], BITS[i].kind, PAPER_TAPE_ZIG_L | PAPER_TAPE_SLANT_R | PAPER_TAPE_EDGE, BITS[i].tilt, 500u + (uint32_t)i};
        PaperSpr *p = paper_tape(&t);
        if (p) paper_spr_put(s, p, (int)BITS[i].x, (int)BITS[i].y);
        paper_spr_free(p);
    }
}

// mockups only: a lacquered board's soft highlights (a broad one and a narrow one, slanting, as a window would lie in it), and a cardboard board's flutes
// showing through its liner (a faint ribbing across it, uneven where the liner is thicker)
static void board_extras(Surf *s, uint32_t seed)
{
    const float sheen = g_pal.board_sheen, ribs = g_pal.board_ribs;
    if (sheen <= 0 && ribs <= 0) return;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            uint8_t *o = s->px + ((size_t)y * s->w + x) * 4;
            if (sheen > 0) {
                float t = (x * 0.55f + y) / (s->w * 0.55f + s->h), b1 = (t - 0.34f) / 0.16f, b2 = (t - 0.47f) / 0.035f;
                float a = sheen * (expf(-b1 * b1) + 0.55f * expf(-b2 * b2)) * (0.85f + 0.3f * vnoise(x / 90.0f, y / 90.0f, seed ^ 0x5EEAu));
                for (int c = 0; c < 3; c++) o[c] = (uint8_t)(o[c] + (205.0f - o[c]) * a + 0.5f);
            }
            if (ribs > 0) {
                float k = 1.0f + ribs * sinf(x * (6.2831853f / 8.0f)) * (0.6f + 0.4f * vnoise(x / 40.0f, y / 160.0f, seed ^ 0x2B5u));
                for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf(o[c] * k + 0.5f, 0, 255);
            }
        }
}
#endif

void paper_board_fill(Surf *s, uint32_t seed)
{
    static uint8_t tile[TILE * TILE];
    static uint32_t tile_seed;
    static int have;
    if (g_paper_board_flat >= 2) { // mockups: 2 = black, 3 = white (a screen drawn on each gives the alpha of everything on it)
        gfx_fill(s, g_paper_board_flat == 2 ? col(0, 0, 0, 1) : col(1, 1, 1, 1));
        return;
    }
    const uint32_t bd = PAL(board, PAPER_BOARD);
    if (g_paper_board_flat) {
        gfx_fill(s, col(((bd >> 16) & 255) / 255.0f, ((bd >> 8) & 255) / 255.0f, (bd & 255) / 255.0f, 1.0f));
        return;
    }
    if (!have || tile_seed != seed) {
        board_tile(tile, seed);
        tile_seed = seed;
        have = 1;
    }
    const float base[3] = {((bd >> 16) & 255) * 1.0f, ((bd >> 8) & 255) * 1.0f, (bd & 255) * 1.0f};
    const float fleck[3] = {PAL3(fleck, 0, 206.4f), PAL3(fleck, 1, 194.1f), PAL3(fleck, 2, 165.7f)}; // (0.62, 0.54, 0.38) in linear light, as the mockup's colour matrix states it, in sRGB
    const float edge[3] = {PAL3(vignette, 0, 138.0f), PAL3(vignette, 1, 122.0f), PAL3(vignette, 2, 85.0f)}; // the mockup's radial gradient towards the corners
    float rx = s->w * 0.5f * 1.41421356f, ry = s->h * 0.5f * 1.41421356f;
    for (int y = 0; y < s->h; y++) {
        uint8_t *o = s->px + (size_t)y * s->w * 4;
        float dy = ((float)y + 0.5f - s->h * 0.5f) / ry;
        for (int x = 0; x < s->w; x++, o += 4) {
            float a = tile[(y % TILE) * TILE + (x % TILE)] * (1.0f / 255.0f) * PAL(fleck_k, 1.0f);
            float dx = ((float)x + 0.5f - s->w * 0.5f) / rx, d2 = dx * dx + dy * dy, va = 0.0f;
            if (d2 > 0.3844f) { // (beyond 62% of the way to the corner)
                float t = (sqrtf(d2) - 0.62f) / 0.38f;
                va = PAL(vignette_k, 0.16f) * (t > 1.0f ? 1.0f : t);
            }
            for (int c = 0; c < 3; c++) {
                float v = base[c] * (1.0f - a) + fleck[c] * a;
                v = v * (1.0f - va) + edge[c] * va;
                o[c] = (uint8_t)(v + 0.5f);
            }
            o[3] = 255;
        }
    }
    if (g_pal.board_scene == 2) board_scene2(s, seed); // (Paper night)
#ifdef WASHI_PAL_LAB
    if (g_pal.board_scene == 1) board_scene(s, seed);
    if (g_pal.board_passport) board_passport(s, seed);
    if (g_pal.board_blueprint) board_blueprint(s, seed);
    if (g_pal.board_chalk) board_chalk(s, seed);
    if (g_pal.board_felt) board_felt(s, seed);
    if (g_pal.board_peg) board_peg(s, seed);
    board_extras(s, seed);
#endif
}

// ------------------------------------------------------------------------------------------------ the ghost patch
PaperSpr *paper_ghost(int w, int h, float radius)
{
    int W = w + 4, H = h + 4;
    PaperSpr *p = paper_spr_new(W, H);
    if (!p) return NULL;
    p->ox = p->oy = 2;
    float R = radius + 1.0f, cx = W * 0.5f, cy = H * 0.5f, total;
    rr_param(cx, cy, cx, cy, W * 0.5f - 1, H * 0.5f - 1, maxf(R - 1, 1), &total);
    int n = (int)floorf(total / 15.0f + 0.5f);
    if (n < 4) n = 4;
    float period = total / n, dash = period * 0.6f;
    const uint32_t gf = PAL(ghost_fill, PAPER_GHOST_FILL), ge = PAL(ghost_edge, PAPER_GHOST_EDGE);
    float fill[3] = {((gf >> 16) & 255) / 255.0f, ((gf >> 8) & 255) / 255.0f, (gf & 255) / 255.0f};
    float edge[3] = {((ge >> 16) & 255) / 255.0f, ((ge >> 8) & 255) / 255.0f, (ge & 255) / 255.0f};
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            float px = x + 0.5f, py = y + 0.5f, sd = sd_rrect(px, py, cx, cy, W * 0.5f, H * 0.5f, R);
            if (sd > 0.5f) continue;
            float cov = clampf(0.5f - sd, 0, 1);
            float band = cov * clampf(0.5f + (sd + 2.0f), 0, 1); // the 2 px just inside the edge
            float s = rr_param(px, py, cx, cy, W * 0.5f - 1, H * 0.5f - 1, maxf(R - 1, 1), &total);
            float m = pmod(s, period);
            float on = clampf(m + 0.5f, 0, 1) * clampf(dash - m + 0.5f, 0, 1); // 1 px soft ends
            float e = band * on;
            uint8_t *o = p->px + ((size_t)y * W + x) * 4;
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)clampf((lerpf(fill[c], edge[c], e)) * 255.0f + 0.5f, 0, 255);
            o[3] = (uint8_t)clampf(cov * 255.0f + 0.5f, 0, 255);
        }
    }
    return p;
}

// ------------------------------------------------------------------------------------------------ shadows
void paper_shadow_params(float p, float *dy, float *blur, float *alpha)
{
    *dy = 2.0f + 14.0f * p;
    *blur = 5.0f + 12.0f * p;
    *alpha = 0.22f + 0.22f * p;
}

// box blur of a w x h float plane, radius r, `passes` times (three of them are close to a gaussian)
static void blur_plane(float *a, int w, int h, int r, int passes)
{
    if (r < 1) return;
    float *tmp = malloc((size_t)(w > h ? w : h) * sizeof *tmp);
    if (!tmp) return;
    float inv = 1.0f / (2 * r + 1);
    for (int pass = 0; pass < passes; pass++) {
        for (int y = 0; y < h; y++) { // along x
            float *row = a + (size_t)y * w, sum = 0;
            for (int i = -r; i <= r; i++) sum += (i >= 0 && i < w) ? row[i] : 0;
            for (int x = 0; x < w; x++) {
                tmp[x] = sum * inv;
                int add = x + r + 1, sub = x - r;
                sum += (add < w ? row[add] : 0) - (sub >= 0 ? row[sub] : 0);
            }
            memcpy(row, tmp, (size_t)w * sizeof *tmp);
        }
        for (int x = 0; x < w; x++) { // along y
            float sum = 0;
            for (int i = -r; i <= r; i++) sum += (i >= 0 && i < h) ? a[(size_t)i * w + x] : 0;
            for (int y = 0; y < h; y++) {
                tmp[y] = sum * inv;
                int add = y + r + 1, sub = y - r;
                sum += (add < h ? a[(size_t)add * w + x] : 0) - (sub >= 0 ? a[(size_t)sub * w + x] : 0);
            }
            for (int y = 0; y < h; y++) a[(size_t)y * w + x] = tmp[y];
        }
    }
    free(tmp);
}

static int blur_radius_for(float sigma)
{
    float b = sqrtf(4.0f * sigma * sigma + 1.0f); // width of each of three boxes
    int r = (int)floorf((b - 1.0f) * 0.5f + 0.5f);
    return r < 1 ? 1 : r;
}

PaperShadows *paper_shadows(int w, int h, float radius, float grow, int n)
{
    if (n < 2) n = 2;
    if (n > PAPER_SHADOW_MAX) n = PAPER_SHADOW_MAX;
    PaperShadows *sh = calloc(1, sizeof *sh);
    if (!sh) return NULL;
    int M = (int)ceilf(17.0f + 16.0f + maxf(w, h) * grow * 0.5f) + 3; // blur, drop and growth at the top level, and a little over
    int W = w + 2 * M, H = h + 2 * M;
    float *mask = malloc((size_t)W * H * sizeof *mask);
    if (!mask) {
        free(sh);
        return NULL;
    }
    sh->n = n;
    for (int i = 0; i < n; i++) {
        float p = i / (float)(n - 1), k = 1.0f + grow * p, dy, blur, alpha;
        paper_shadow_params(p, &dy, &blur, &alpha);
        alpha *= PAL(shadow_k, 1.0f);
        float cx = W * 0.5f, cy = H * 0.5f + dy;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) mask[(size_t)y * W + x] = clampf(0.5f - sd_rrect(x + 0.5f, y + 0.5f, cx, cy, w * k * 0.5f, h * k * 0.5f, radius * k), 0, 1);
        blur_plane(mask, W, H, blur_radius_for(blur * 0.5f), 3);
        PaperSpr *s = paper_spr_new(W, H);
        if (!s) {
            sh->n = i;
            paper_shadows_free(sh);
            free(mask);
            return NULL;
        }
        s->ox = s->oy = M;
        const uint32_t shc = PAL(shadow, PAPER_SHADOW);
        uint8_t r8 = (shc >> 16) & 255, g8 = (shc >> 8) & 255, b8 = shc & 255;
        for (size_t j = 0, cnt = (size_t)W * H; j < cnt; j++) {
            int a = (int)(mask[j] * alpha * 255.0f + 0.5f);
            if (!a) continue;
            s->px[j * 4] = r8;
            s->px[j * 4 + 1] = g8;
            s->px[j * 4 + 2] = b8;
            s->px[j * 4 + 3] = (uint8_t)a;
        }
        sh->s[i] = s;
    }
    free(mask);
    return sh;
}

void paper_shadows_free(PaperShadows *s)
{
    if (!s) return;
    for (int i = 0; i < s->n; i++) paper_spr_free(s->s[i]);
    free(s);
}

int paper_shadow_level(const PaperShadows *s, float lift)
{
    int i = (int)(clampf(lift, 0, 1) * (s->n - 1) + 0.5f);
    return i < 0 ? 0 : i > s->n - 1 ? s->n - 1 : i;
}

// ------------------------------------------------------------------------------------------------ washi tape
typedef struct {
    int len, wid, kind;
    unsigned flags;
    float tilt, rgb[3];
    int nz;                          // zigzag rows
    int crimp;
    float sx, sk, cx, ky, kr;        // the crimp: where the cut starts, half slant, middle, the far half's jog (px) and turn (degrees)
    float slant_l, slant_r;          // slanted ends: how far (px, signed) the cut runs along the strip across its width
    float pat_str;                   // how much of its white the pattern lays over the colour (1: all of it)
    uint32_t seed;
    int host;                        // stuck to a card: the loose-tape marks apply
    float hw, hh, hr, hx, hy, mcos, msin; // the card (size, corner radius), where the strip's top left is on it, and the strip's tilt on it (cos, sin)
} TapeGeo;

#define TOOTH 4.0f
#define TAPE_SH(c) (((g_pal.tape_shadow >> (16 - 8 * (c))) & 255) / 255.0f) // the contact shadows' colour (the light theme's warm brown, Paper night's black)
#define TAPE_ALPHA PAL(tape_alpha, 0.70f) // how much of the board and card a strip of washi hides: the mockup's tape is a milky 60%, the card's edge and the board reaching through it
#define TAPE_ALPHA_EDGE 0.92f // the roll's pieces lie in a box and are nearly solid
static float tape_alpha(const TapeGeo *g);

static void tape_geo(TapeGeo *g, const PaperTape *t)
{
    memset(g, 0, sizeof *g);
    g->len = t->len < 8 ? 8 : t->len;
    g->wid = t->wid < 6 ? 6 : t->wid;
    g->kind = t->kind < 0 || t->kind >= PAT_COUNT ? 0 : t->kind;
    g->flags = t->flags;
    g->tilt = (t->flags & PAPER_TAPE_UPRIGHT) ? 0.0f : t->tilt;
    g->seed = t->seed;
    if (t->host_w > 0 && t->host_h > 0) { // the card it is stuck to
        g->host = 1;
        g->hw = t->host_w;
        g->hh = t->host_h;
        g->hr = t->host_r;
        g->hx = t->host_x;
        g->hy = t->host_y;
        g->mcos = pcos(t->tilt * (PI_F / 180.0f));
        g->msin = psin(t->tilt * (PI_F / 180.0f));
    }
    for (int c = 0; c < 3; c++) g->rgb[c] = ((t->rgb >> (16 - 8 * c)) & 255) / 255.0f;
    g->nz = (int)floorf(g->wid / ((t->flags & PAPER_TAPE_COARSE) ? 10.76f : 8.0f) + 0.5f);
    if (g->nz < 2) g->nz = 2;
    unsigned ps = (t->flags >> 10) & 15u;
    g->pat_str = ps ? ps / 16.0f : 1.0f;
    if (t->flags & (PAPER_TAPE_SLANT_L | PAPER_TAPE_SLANT_R)) {
        PaperRng r;
        paper_rng_seed(&r, t->seed ^ 0x51A47u);
        float v[4];
        for (int i = 0; i < 4; i++) v[i] = paper_rng_f(&r); // (drawn in a fixed order, then used)
        g->slant_l = (v[0] < 0.5f ? -1.0f : 1.0f) * (0.12f + 0.18f * v[1]) * g->wid;
        g->slant_r = (v[2] < 0.5f ? -1.0f : 1.0f) * (0.12f + 0.18f * v[3]) * g->wid;
    }
    if ((t->flags & PAPER_TAPE_CRIMP) && g->len >= 36) {
        PaperRng r;
        paper_rng_seed(&r, t->seed ^ 0xC12117u);
        g->crimp = 1;
        g->sk = 4.0f;
        g->cx = g->len * (0.3f + 0.38f * paper_rng_f(&r));
        g->sx = g->cx - g->sk;
        float ksign = paper_rng_f(&r) < 0.5f ? -1.0f : 1.0f;    // two draws in a fixed order (C leaves it open inside one expression)
        g->ky = ksign * (1.2f + 1.6f * paper_rng_f(&r));
        g->kr = (paper_rng_f(&r) * 2.0f - 1.0f) * 0.3f;
    }
}

// how far a zigzag cut end is cut in at height v (0 at the points, TOOTH at the notches)
static float zig_off(int n, float h, float v)
{
    float k = v / h * n;
    int i = (int)floorf(k);
    if (i < 0) i = 0;
    if (i > n - 1) i = n - 1;
    float f = k - i, a = (i & 1) ? TOOTH : 0.0f, b = ((i + 1) & 1) ? TOOTH : 0.0f;
    return a + (b - a) * f;
}

// how far an end of the strip is cut in at height v across its width: the pinking shears' teeth, a slanted cut, or nothing (square)
static float cut_in(const TapeGeo *g, int right, float h, float v)
{
    if (g->flags & (right ? PAPER_TAPE_ZIG_R : PAPER_TAPE_ZIG_L)) return zig_off(g->nz, h, v);
    if (g->flags & (right ? PAPER_TAPE_SLANT_R : PAPER_TAPE_SLANT_L)) {
        float s = right ? g->slant_r : g->slant_l, f = clampf(v / h, 0, 1);
        if (right) f = 1.0f - f;
        return s >= 0 ? s * f : -s * (1.0f - f);
    }
    return 0.0f;
}

// the white laid over the tape colour by each pattern, 0..1, at (u, v) in the strip's own pixels from its top left; L, H the piece's size
// (the stripes' phase follows it, as in CSS gradients). Numbers are the mockup's.
static float pat_white1(int kind, float u, float v, float L, float H)
{
    const float D = 0.70710678f;
    switch (kind) {
    case PAT_STRIPES: { // 45deg, white .6 for 5 of every 12
        float t = (u - L * 0.5f) * D - (v - H * 0.5f) * D + (L + H) * D * 0.5f;
        return pmod(t, 12.0f) < 5.0f ? 0.6f : 0.0f;
    }
    case PAT_DOTS: { // a dot at the middle of each 12 px cell, radius 2.4 fading out by 2.7
        float du = pmod(u, 12.0f) - 6.0f, dv = pmod(v, 12.0f) - 6.0f, d = sqrtf(du * du + dv * dv);
        return 0.85f * clampf((2.7f - d) / 0.3f, 0, 1);
    }
    case PAT_GRID: return (pmod(u, 12.0f) < 1.4f || pmod(v, 12.0f) < 1.4f) ? 0.7f : 0.0f;
    case PAT_ZIG: { // two right triangles at the top corners of each 12 px cell
        float mu = pmod(u, 12.0f), mv = pmod(v, 12.0f);
        return (mu + mv < 6.0f || (12.0f - mu) + mv < 6.0f) ? 0.7f : 0.0f;
    }
    case PAT_CHECK: { // 7 px squares
        float mu = pmod(u, 14.0f), mv = pmod(v, 14.0f);
        int right = mu >= 7.0f, below = mv >= 7.0f;
        return (right != below) ? 0.7f : 0.0f;
    }
    case PAT_SCALLOP: { // half circles standing on the bottom of each 12 x 9 cell
        float du = pmod(u, 12.0f) - 6.0f, dv = pmod(v, 9.0f) - 9.0f, d = sqrtf(du * du + dv * dv);
        return 0.78f * clampf((4.7f - d) / 0.5f, 0, 1);
    }
    case PAT_HLINE: return pmod(H - v, 6.0f) < 2.0f ? 0.7f : 0.0f;
    case PAT_WSTRIPE: { // -45deg, white .62 for 9 of every 20
        float t = -(u - L * 0.5f) * D - (v - H * 0.5f) * D + (L + H) * D * 0.5f;
        return pmod(t, 20.0f) < 9.0f ? 0.62f : 0.0f;
    }
    }
    return 0.0f;
}

float g_paper_pat_scale = 1.0f; // mockups only (never set by the app): bigger tape draws its pattern bigger, as a bigger roll of tape would print it
static float pat_white(int kind, float u, float v, float L, float H)
{
    float k = g_paper_pat_scale;
    return k == 1.0f ? pat_white1(kind, u, v, L, H) : pat_white1(kind, u / k, v / k, L / k, H / k);
}

// a tape pixel: colour, pattern and the faint fibres of the paper, premultiplied into o
static float tape_alpha(const TapeGeo *g) { return (g->flags & PAPER_TAPE_EDGE) ? TAPE_ALPHA_EDGE : TAPE_ALPHA; }

static void tape_shade(const TapeGeo *g, float u, float v, float L, float H, float alpha, float fib, float *o)
{
    float pa = (g->flags & PAPER_TAPE_PLAIN) ? 0.0f : pat_white(g->kind, u, v, L, H) * g->pat_str, k = 1.0f + 0.02f * fib;
    float a = alpha * (1.0f - 0.02f * (fib * 0.5f + 0.5f));
    if (pa > 0.0f) a += (0.94f - a) * clampf(pa / 0.8f, 0.0f, 1.0f); // the white of a pattern is printed on, not seen through: dots and stripes stay crisp
    if (g->flags & PAPER_TAPE_GRAINY) k *= 0.974f;
    if (g->flags & PAPER_TAPE_OPAQUE) a = 1.0f;
    if (g->host && alpha < 0.99f && !(g->flags & (PAPER_TAPE_OPAQUE | PAPER_TAPE_EDGE))) {
        // A strip stuck to a card is frosted, like the mockup's (its tape diffuses what is under it): the board's grain does not show through, only the colour of what is
        // there, the board's or the card's, softly changing across the card's edge. So it is made opaque, with that colour already mixed in.
        float px = u - g->len * 0.5f, py = v - g->wid * 0.5f;
        float qx = g->hx + g->len * 0.5f + (px * g->mcos - py * g->msin), qy = g->hy + g->wid * 0.5f + (px * g->msin + py * g->mcos);
        float cx = g->hw * 0.5f, cy = g->hh * 0.5f, d = sd_rrect(qx, qy, cx, cy, cx, cy, g->hr);
        float inside = 1.0f - smooth01(clampf(0.5f + d / 5.5f, 0.0f, 1.0f)); // (the card's edge fades out through the tape over about 5 px)
        const float *UB = g_pal.tape_board, *UC = g_pal.tape_paper; // the board's mean colour, the paper's
        for (int c = 0; c < 3; c++) {
            float under = UB[c] + (UC[c] - UB[c]) * inside, tape = clampf((g->rgb[c] * (1.0f - pa) + pa) * k, 0, 1);
#ifdef WASHI_PAL_LAB
            if (g_pal.tape_mul) { // (lit from behind: the light comes through the paper and then the tape)
                o[c] = under * (1.0f - a + a * tape);
                continue;
            }
#endif
            o[c] = tape * a + under * (1.0f - a);
        }
        o[3] = 1.0f;
        return;
    }
    for (int c = 0; c < 3; c++) o[c] = clampf((g->rgb[c] * (1.0f - pa) + pa) * k, 0, 1) * a;
    o[3] = a;
}

static float seg_cov(float px, float py, float x0, float y0, float x1, float y1, float w)
{
    float vx = x1 - x0, vy = y1 - y0, wx = px - x0, wy = py - y0, l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? clampf((wx * vx + wy * vy) / l2, 0, 1) : 0, dx = wx - vx * t, dy = wy - vy * t;
    return clampf(w * 0.5f + 0.5f - sqrtf(dx * dx + dy * dy), 0, 1);
}

static void over_premul(float *dst, const float *src) // dst = src over dst
{
    float k = 1.0f - src[3];
    for (int c = 0; c < 4; c++) dst[c] = src[c] + dst[c] * k;
}

// the pressed-down ridge: a slanted band, light then dark then light, and a few small wrinkles
static void crimp_ridge(const TapeGeo *g, float lx, float ly, float *o)
{
    float h = (float)g->wid, k = 2.0f * g->sk / h, a = 8.0f - g->sk;
    float xs = (lx - (g->cx - 8.0f)) - k * ly, t = (xs - (a - 5.0f)) / 10.0f;
    o[0] = o[1] = o[2] = o[3] = 0;
    if (t > 0 && t < 1 && ly >= 0 && ly <= h) {
        // stops at 0, .24, .52, .78, 1: transparent white, white .95, brown .46, white .6, transparent white (premultiplied, lerped)
        static const float T[5] = {0, 0.24f, 0.52f, 0.78f, 1.0f};
        static const float S[5][4] = {{0, 0, 0, 0}, {0.95f, 0.95f, 0.95f, 0.95f}, {0.27f * 0.46f, 0.19f * 0.46f, 0.10f * 0.46f, 0.46f}, {0.6f, 0.6f, 0.6f, 0.6f}, {0, 0, 0, 0}};
        int i = 0;
        while (i < 3 && t > T[i + 1]) i++;
        float f = (t - T[i]) / (T[i + 1] - T[i]);
        for (int c = 0; c < 4; c++) o[c] = lerpf(S[i][c], S[i + 1][c], f);
    }
    float m = h * 0.5f, ux = xs; // wrinkles, in the unslanted frame
    float w1 = maxf(maxf(seg_cov(ux, ly, a - 7, m - 8, a - 2, m - 5, 1.1f), seg_cov(ux, ly, a + 7, m + 7, a + 2, m + 4, 1.1f)), seg_cov(ux, ly, a - 6, m + 6, a - 2, m + 4, 1.1f)) * 0.8f;
    float w2 = seg_cov(ux, ly, a + 6, m - 7, a + 2, m - 4, 1.0f) * 0.4f;
    float lw[4] = {w1, w1, w1, w1}, dk[4] = {0.27f * w2, 0.19f * w2, 0.10f * w2, w2};
    over_premul(o, lw);
    over_premul(o, dk);
}

// A stroke of width w seen through a blur of sigma sg, at distance d from its centre line: 0..1, soft edged (a stroke thinner than its blur is dimmer, as it is
// in the mockup, where every mark is blurred by half a pixel or two).
static float soft_stroke(float d, float w, float sg)
{
    float peak = clampf(w / (2.5f * sg), 0.0f, 1.0f);
    return peak * smooth01(clampf((w * 0.5f + 1.7f * sg - fabsf(d)) / (3.4f * sg), 0.0f, 1.0f));
}

static float soft_seg(float px, float py, float x0, float y0, float x1, float y1, float w, float sg)
{
    float vx = x1 - x0, vy = y1 - y0, wx = px - x0, wy = py - y0, l2 = vx * vx + vy * vy;
    float t = l2 > 0 ? clampf((wx * vx + wy * vy) / l2, 0, 1) : 0, dx = wx - vx * t, dy = wy - vy * t;
    return soft_stroke(sqrtf(dx * dx + dy * dy), w, sg);
}

#define MARK_LIGHT 2.2f // the pressed look is laid on a little stronger than the mockup draws it (1.3 times its 1.7; the user's pick from the tape lab, round 2)
#define MARK_DARK 2.5f

// How a strip lies when it was only lightly pressed down over the edge of its card, at (lx, ly) of the strip (from its top left): `light` and `dark` are
// how strongly white and brown are laid over the tape there (0..1). In the card's own frame (origin at its corner, the card on the +x +y side), and
// nothing follows the outline: a pale haze where the tape bridges the edge, in broad soft patches; a loose, pale tent just outside each rounded corner;
// two faint puckers fanning out from the corner; and the tape a shade darker where it lies over the board than over the card, the edge showing through
// it softly. (The mockup also had a pleat every six or seven pixels along the edge; on the console they looked like fingernail marks, so they are gone.)
// The numbers are the mockup's (doodle.html wrapLayers), every stroke blurred as it is blurred there.
static void tape_marks(const TapeGeo *g, float lx, float ly, float *light, float *dark)
{
    float px = lx - g->len * 0.5f, py = ly - g->wid * 0.5f;
    float qx = g->hx + g->len * 0.5f + (px * g->mcos - py * g->msin), qy = g->hy + g->wid * 0.5f + (px * g->msin + py * g->mcos);
    float cx = g->hw * 0.5f, cy = g->hh * 0.5f, d = sd_rrect(qx, qy, cx, cy, cx, cy, g->hr); // d > 0: outside the card
    float lt = 0, dk = 0;
    static const float OX[4] = {-1, 1, -1, 1}, OY[4] = {-1, -1, 1, 1};
    for (int k = 0; k < 4; k++) {
        float kx = OX[k] > 0 ? g->hw : 0.0f, ky = OY[k] > 0 ? g->hh : 0.0f;
        float rx = qx - (kx + OX[k] * 3.6f), ry = qy - (ky + OY[k] * 3.6f); // the tent: an ellipse across the corner's diagonal
        float al = (rx * -OY[k] + ry * OX[k]) * 0.70710678f, ac = (rx * OX[k] + ry * OY[k]) * 0.70710678f;
        float nr = (al / 8.5f) * (al / 8.5f) + (ac / 5.4f) * (ac / 5.4f);
        if (nr < 1.0f) {
            float t = (1.0f - nr) * (1.0f - nr);
            lt = 1.0f - (1.0f - lt) * (1.0f - 0.22f * t);
        }
        float ax = (qx - kx) * OX[k], ay = (qy - ky) * OY[k]; // the puckers, in the corner's frame (outward is +, +)
        if (ax > -4.0f && ay > -4.0f && ax < 11.0f && ay < 11.0f) {
            float c1 = soft_seg(ax, ay, 1.4f, 1.4f, 6.6f, 3.2f, 0.7f, 0.5f), c2 = soft_seg(ax, ay, 1.4f, 1.4f, 3.2f, 6.6f, 0.7f, 0.5f);
            float l1 = soft_seg(ax - 0.8f, ay, 1.4f, 1.4f, 6.6f, 3.2f, 0.8f, 0.5f), l2 = soft_seg(ax - 0.8f, ay, 1.4f, 1.4f, 3.2f, 6.6f, 0.8f, 0.5f);
            dk = 1.0f - (1.0f - dk) * (1.0f - 0.10f * maxf(c1, c2));
            lt = 1.0f - (1.0f - lt) * (1.0f - 0.32f * maxf(l1, l2));
        }
    }
    float qxr = fabsf(qx - cx) - (cx - g->hr), qyr = fabsf(qy - cy) - (cy - g->hr);
    if (!(qxr > 0 && qyr > 0) && fabsf(d) < 9.0f) { // along a straight edge
        int id = qxr > qyr ? (qx < cx ? 2 : 3) : (qy < cy ? 0 : 1);
        float along = qxr > qyr ? qy : qx;
        // the haze: broad soft patches along the edge itself, some stronger than others (the mockup's three are .2, .11 and .17 over 28 px)
        float strength = 0.07f + 0.15f * vnoise(along * 0.16f + id * 3.7f, id * 1.7f, g->seed ^ 0x77u);
        lt = 1.0f - (1.0f - lt) * (1.0f - strength * soft_stroke(d, 5.0f, 1.9f));
    }
    // over the board the tape is a shade darker than over the card (it is laid over what is under it, not painted on): the card's edge shows through, softly
    *light = minf(lt * MARK_LIGHT, 0.88f); // (scaled at the end, so many strokes together can not go past solid)
    *dark = minf(dk * MARK_DARK, 0.75f);
}

// the part of the strip that is stuck: premultiplied into o, o[3] = 0 outside. `u_peel` is the length that has come up.
static void tape_stuck(const TapeGeo *g, float lx, float ly, float u_peel, float fib, float *o)
{
    float h = (float)g->wid, L = (float)g->len;
    o[0] = o[1] = o[2] = o[3] = 0;
    if (ly < 0 || ly > h) {
        // the far half of a crimped strip sits a little out of line, so it can be outside this band: tested below
        if (!g->crimp || u_peel > 2) return;
    }
    if (u_peel > 2) { // peeling: the stuck part is the left of the strip, cut straight on the right where the flap lifts
        float wd = L - u_peel + 6.0f;
        if (lx >= cut_in(g, 0, h, ly) && lx <= wd) tape_shade(g, lx, ly, L, h, tape_alpha(g), fib, o);
        return;
    }
    if (!g->crimp) {
        if (lx >= cut_in(g, 0, h, ly) && lx <= L - cut_in(g, 1, h, ly)) tape_shade(g, lx, ly, L, h, tape_alpha(g), fib, o);
        return;
    }
    // crimped: the near half to the left of the slanted cut, the far half laid over it, moved down or up by ky and turned a hair
    float ca = pcos(g->kr * (PI_F / 180.0f)), sa = psin(g->kr * (PI_F / 180.0f));
    float dx = lx - (g->sx - 1.0f), dy = ly - h * 0.5f - g->ky;
    float bu = dx * ca + dy * sa, bv = -dx * sa + dy * ca + h * 0.5f, wb = L - g->sx + 1.0f;
    int inb = bv >= 0 && bv <= h && bu >= 1.0f + 2.0f * g->sk * bv / h && bu <= wb - cut_in(g, 1, h, bv);
    if (inb) {
        tape_shade(g, bu, bv, wb, h, tape_alpha(g), fib, o);
    } else if (ly >= 0 && ly <= h && lx >= cut_in(g, 0, h, ly) && lx <= g->sx + 2.0f * g->sk * ly / h) {
        tape_shade(g, lx, ly, L, h, tape_alpha(g), fib, o);
    }
    if (o[3] > 0) {
        float r[4];
        crimp_ridge(g, lx, ly, r);
        over_premul(o, r);
    }
}

// the flap: premultiplied into o, o[3] = 0 outside. u is its length, p the peel 0..1
static void tape_flap(const TapeGeo *g, float lx, float ly, float u, float p, float fib, float *o)
{
    float h = (float)g->wid, a = -34.0f * p * (PI_F / 180.0f), ca = pcos(a), sa = psin(a);
    float dx = lx - (g->len - u), dy = ly + 3.0f * p - h * 0.5f;
    float fx = dx * ca + dy * sa, fy = -dx * sa + dy * ca + h * 0.5f;
    o[0] = o[1] = o[2] = o[3] = 0;
    if (fy < 0 || fy > h || fx < 0 || fx > u - cut_in(g, 1, h, fy)) return;
    tape_shade(g, fx, fy, u, h, 1.0f, fib, o);
    // the fold: dark where it turns over, then lit; linear-gradient(90deg, rgba(60,45,20,.3), rgba(255,255,255,.4) 40%, rgba(255,255,255,.25))
    float t = fx / u, gp[4];
    if (t < 0.4f) {
        float f = t / 0.4f, a0 = lerpf(0.3f, 0.4f, f);
        gp[0] = lerpf(60.0f / 255 * 0.3f, 0.4f, f);
        gp[1] = lerpf(45.0f / 255 * 0.3f, 0.4f, f);
        gp[2] = lerpf(20.0f / 255 * 0.3f, 0.4f, f);
        gp[3] = a0;
    } else {
        float a0 = lerpf(0.4f, 0.25f, (t - 0.4f) / 0.6f);
        gp[0] = gp[1] = gp[2] = gp[3] = a0;
    }
    float res[4] = {o[0], o[1], o[2], o[3]};
    for (int c = 0; c < 4; c++) o[c] = gp[c] + res[c] * (1.0f - gp[3]);
}

// the sprite size that holds the strip at any peel up to `peel_max`, centred on the strip's middle (so the sprite's middle is the strip's)
static void tape_box(const TapeGeo *g, float peel_max, int *W, int *H)
{
    float h = (float)g->wid, L = (float)g->len, ex = 0, ey = 0;
    float pts[16][2];
    int n = 0;
    float jog = g->crimp ? 3.5f : 0.0f;
    pts[n][0] = -L * 0.5f; pts[n++][1] = -h * 0.5f - jog;
    pts[n][0] = L * 0.5f;  pts[n++][1] = -h * 0.5f - jog;
    pts[n][0] = L * 0.5f;  pts[n++][1] = h * 0.5f + jog;
    pts[n][0] = -L * 0.5f; pts[n++][1] = h * 0.5f + jog;
    if (peel_max > 0) {
        float u = L * 0.55f * peel_max, a = -34.0f * peel_max * (PI_F / 180.0f), ca = pcos(a), sa = psin(a);
        float fxs[2] = {0, u}, fys[2] = {0, h};
        for (int i = 0; i < 2; i++)
            for (int j = 0; j < 2; j++) {
                float dx = fxs[i], dy = fys[j] - h * 0.5f;
                pts[n][0] = (L - u) + dx * ca - dy * sa - L * 0.5f;
                pts[n++][1] = h * 0.5f + dx * sa + dy * ca - 3.0f * peel_max - h * 0.5f;
            }
    }
    float ct = pcos(g->tilt * (PI_F / 180.0f)), st = psin(g->tilt * (PI_F / 180.0f));
    for (int i = 0; i < n; i++) {
        float x = pts[i][0] * ct - pts[i][1] * st, y = pts[i][0] * st + pts[i][1] * ct;
        ex = maxf(ex, fabsf(x));
        ey = maxf(ey, fabsf(y));
    }
    float m = 2.0f + ((g->flags & PAPER_TAPE_EDGE) ? 3.0f : 0.0f) + (peel_max > 0 ? 9.0f : 0.0f);
    *W = 2 * (int)ceilf(ex + m);
    *H = 2 * (int)ceilf(ey + m);
}

// Is the pixel (x, y) of the sprite well inside a plain strip, with the pattern the same at its middle and its four corners? Then every sample in it would
// come out the same and only one needs working out. (Stripes, dots and grid lines are much bigger than a pixel: only the pixels on their edges take the full count.)
static int pixel_flat(const TapeGeo *g, int x, int y, int W, int H, float ct, float st, float L, float h)
{
    if (g->flags & PAPER_TAPE_PLAIN) return 0;
    float rx = x + 0.5f - W * 0.5f, ry = y + 0.5f - H * 0.5f;
    float lx = rx * ct + ry * st + L * 0.5f, ly = -rx * st + ry * ct + h * 0.5f;
    float mx = (g->flags & (PAPER_TAPE_SLANT_L | PAPER_TAPE_SLANT_R)) ? 0.3f * h + 2.5f : (g->flags & (PAPER_TAPE_ZIG_L | PAPER_TAPE_ZIG_R)) ? TOOTH + 2.5f : 2.5f; // (the cut ends take room: the teeth, or a slant of up to a third of the width)
    if (ly < 1.8f || ly > h - 1.8f || lx < mx || lx > L - mx) return 0;
    float p0 = pat_white(g->kind, lx, ly, L, h);
    static const float OX[4] = {-0.5f, 0.5f, -0.5f, 0.5f}, OY[4] = {-0.5f, -0.5f, 0.5f, 0.5f};
    for (int k = 0; k < 4; k++) {
        float qx = rx + OX[k], qy = ry + OY[k];
        if (pat_white(g->kind, qx * ct + qy * st + L * 0.5f, -qx * st + qy * ct + h * 0.5f, L, h) != p0) return 0;
    }
    return 1;
}

// How the strip lies on the paper, laid over it after its marks (S premultiplied, W x H, the strip's middle at the sprite's middle, ct and st its turn): it is stuck
// down harder in some places than in others (broad milky patches where air is under it), it has a thickness that catches the light along its top left edges and
// is in shade along the bottom right ones, and the long fibres of washi show faintly through it. (The user's pick from the tape lab, round 2.)
static void tape_texture(float *S, int W, int H, const TapeGeo *g, float ct, float st, float L, float h)
{
    size_t N = (size_t)W * H;
    float *dist = malloc(N * sizeof(float)), *al = malloc(N * sizeof(float)), *fl = calloc(N, sizeof(float)), *fd = calloc(N, sizeof(float));
    if (!dist || !al || !fl || !fd) goto done;
    for (size_t i = 0; i < N; i++) {
        al[i] = S[i * 4 + 3];
        dist[i] = al[i] > 0.5f ? 1e4f : 0.0f;
    }
    for (int y = 0; y < H; y++) // how far inside the strip each pixel is (a chamfer distance from the nearest pixel that is not strip)
        for (int x = 0; x < W; x++) {
            size_t i = (size_t)y * W + x;
            if (dist[i] == 0.0f) continue;
            float d = dist[i];
            if (x == 0 || y == 0) d = 1.0f;
            if (x > 0) d = minf(d, dist[i - 1] + 1.0f);
            if (y > 0) d = minf(d, dist[i - W] + 1.0f);
            if (x > 0 && y > 0) d = minf(d, dist[i - W - 1] + 1.4142f);
            if (x < W - 1 && y > 0) d = minf(d, dist[i - W + 1] + 1.4142f);
            dist[i] = d;
        }
    for (int y = H - 1; y >= 0; y--)
        for (int x = W - 1; x >= 0; x--) {
            size_t i = (size_t)y * W + x;
            if (dist[i] == 0.0f) continue;
            float d = dist[i];
            if (x == W - 1 || y == H - 1) d = 1.0f;
            if (x < W - 1) d = minf(d, dist[i + 1] + 1.0f);
            if (y < H - 1) d = minf(d, dist[i + W] + 1.0f);
            if (x < W - 1 && y < H - 1) d = minf(d, dist[i + W + 1] + 1.4142f);
            if (x > 0 && y < H - 1) d = minf(d, dist[i + W - 1] + 1.4142f);
            dist[i] = d;
        }
    // the fibres: short, faint flecks, mostly along the strip, a few across it; each is laid into a lighter and a darker plane over the few pixels it touches
    PaperRng R;
    paper_rng_seed(&R, g->seed ^ 0xF1B7Eu);
    int nf = (int)(L * h / 70.0f);
    nf = nf < 12 ? 12 : nf > 90 ? 90 : nf;
    for (int i = 0; i < nf; i++) {
        float cx = paper_rng_f(&R) * L, cy = paper_rng_f(&R) * h; // (each draw is its own statement: C leaves the order open inside one expression)
        float ang = (paper_rng_f(&R) - 0.5f) * 0.9f;
        if (paper_rng_f(&R) < 0.12f) ang += 1.5708f;
        float len = 3.0f + paper_rng_f(&R) * 5.5f;
        float k = paper_rng_f(&R) < 0.55f ? 0.07f + 0.06f * paper_rng_f(&R) : -(0.04f + 0.035f * paper_rng_f(&R));
        float dx = pcos(ang) * len * 0.5f, dy = psin(ang) * len * 0.5f, e[4] = {cx - dx - L * 0.5f, cy - dy - h * 0.5f, cx + dx - L * 0.5f, cy + dy - h * 0.5f}, p[4];
        for (int j = 0; j < 2; j++) { // (into the sprite's own pixels: the strip's frame turned by its tilt)
            p[j * 2] = e[j * 2] * ct - e[j * 2 + 1] * st + W * 0.5f;
            p[j * 2 + 1] = e[j * 2] * st + e[j * 2 + 1] * ct + H * 0.5f;
        }
        int x0 = (int)floorf(minf(p[0], p[2])) - 1, x1 = (int)ceilf(maxf(p[0], p[2])) + 1, y0 = (int)floorf(minf(p[1], p[3])) - 1, y1 = (int)ceilf(maxf(p[1], p[3])) + 1;
        for (int y = y0 < 0 ? 0 : y0; y <= y1 && y < H; y++)
            for (int x = x0 < 0 ? 0 : x0; x <= x1 && x < W; x++) {
                float c = seg_cov(x + 0.5f, y + 0.5f, p[0], p[1], p[2], p[3], 0.7f) * (k > 0 ? k : -k);
                float *pl = (k > 0 ? fl : fd) + (size_t)y * W + x;
                if (c > *pl) *pl = c;
            }
    }
    static const float BR[3] = {0.16f, 0.11f, 0.03f}; // the brown of the shadows
    for (int y = 1; y < H - 1; y++)
        for (int x = 1; x < W - 1; x++) {
            size_t i = (size_t)y * W + x;
            float *s = S + i * 4, a = s[3];
            if (a < 0.02f) continue;
            float rx = x + 0.5f - W * 0.5f, ry = y + 0.5f - H * 0.5f, lx = rx * ct + ry * st + L * 0.5f, ly = -rx * st + ry * ct + h * 0.5f;
            float e = maxf(dist[i] - 0.5f, 0.0f), lift = fl[i], dark = fd[i];
            float m = vnoise(lx * 0.085f + 3.1f, ly * 0.085f + 7.7f, g->seed ^ 0x3077u) * 2.0f - 1.0f; // the patches
            lift += 0.10f * maxf(m, 0.0f);
            dark += 0.07f * maxf(-m, 0.0f);
            float gx = al[i + 1] - al[i - 1], gy = al[i + W] - al[i - W], gl = sqrtf(gx * gx + gy * gy); // the edge: which way it faces, against a light from the top left
            if (gl > 0.05f) {
                float lit = (gx + gy) * 0.7071f / gl, wedge = 1.0f - smooth01(e / 1.3f);
                lift += 0.22f * wedge * maxf(lit, 0.0f);
                dark += 0.16f * wedge * maxf(-lit, 0.0f);
            }
            lift = minf(lift, 0.6f);
            dark = minf(dark, 0.5f);
            for (int c = 0; c < 3; c++) {
                s[c] = s[c] * (1.0f - lift) + lift * a;
                s[c] = s[c] * (1.0f - dark) + BR[c] * dark * a;
            }
        }
done:
    free(dist);
    free(al);
    free(fl);
    free(fd);
}

// one picture of the strip at peel p (0 stuck .. 1 fully peeled), into a W x H sprite whose middle is the strip's middle
static void tape_render(PaperSpr *out, const TapeGeo *g, float peel)
{
    int W = out->w, H = out->h;
    size_t N = (size_t)W * H;
    float *S = calloc(N * 4, sizeof(float)), *F = calloc(N * 4, sizeof(float));
    float *shS1 = NULL, *shS2 = NULL, *shF = NULL;
    if (!S || !F) {
        free(S);
        free(F);
        return;
    }
    float L = (float)g->len, h = (float)g->wid, u_peel = L * 0.55f * peel;
    float ct = pcos(g->tilt * (PI_F / 180.0f)), st = psin(g->tilt * (PI_F / 180.0f));
    int ss = N > 12000 ? 3 : 4, nsub = ss * ss;
    float inv = 1.0f / nsub;
    int plain_stuck = !g->crimp && u_peel <= 2;
    // most of the sprite is empty: look at the middle of each pixel first, and only work on the pixels that touch the tape or sit next to one
    unsigned char *near_tape = calloc(N, 1);
    if (!near_tape) {
        free(S);
        free(F);
        return;
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            float rx = x + 0.5f - W * 0.5f, ry = y + 0.5f - H * 0.5f, o[4];
            float lx = rx * ct + ry * st + L * 0.5f, ly = -rx * st + ry * ct + h * 0.5f;
            tape_stuck(g, lx, ly, u_peel, 0.0f, o);
            int hit = o[3] > 0;
            if (!hit && u_peel > 2) {
                tape_flap(g, lx, ly, u_peel, peel, 0.0f, o);
                hit = o[3] > 0;
            }
            if (hit)
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++)
                        if (x + dx >= 0 && x + dx < W && y + dy >= 0 && y + dy < H) near_tape[(size_t)(y + dy) * W + x + dx] = 1;
        }
    for (int y = 0; y < H; y++) {
        for (int x = 0; x < W; x++) {
            if (!near_tape[(size_t)y * W + x]) continue;
            float fib = hashf(x, y, g->seed ^ 0xF1B3u) * 2.0f - 1.0f;
            float *s = S + ((size_t)y * W + x) * 4, *f = F + ((size_t)y * W + x) * 4;
            if (plain_stuck && pixel_flat(g, x, y, W, H, ct, st, L, h)) { // well inside the strip, the pattern the same all over this pixel: one look is as good as sixteen
                float rx = x + 0.5f - W * 0.5f, ry = y + 0.5f - H * 0.5f, o[4];
                tape_stuck(g, rx * ct + ry * st + L * 0.5f, -rx * st + ry * ct + h * 0.5f, u_peel, fib, o);
                for (int c = 0; c < 4; c++) s[c] = o[c];
                continue;
            }
            for (int j = 0; j < ss; j++)
                for (int i = 0; i < ss; i++) {
                    float rx = x + (i + 0.5f) / ss - W * 0.5f, ry = y + (j + 0.5f) / ss - H * 0.5f;
                    float lx = rx * ct + ry * st + L * 0.5f, ly = -rx * st + ry * ct + h * 0.5f, o[4];
                    tape_stuck(g, lx, ly, u_peel, fib, o);
                    for (int c = 0; c < 4; c++) s[c] += o[c] * inv;
                    if (u_peel > 2) {
                        tape_flap(g, lx, ly, u_peel, peel, fib, o);
                        for (int c = 0; c < 4; c++) f[c] += o[c] * inv;
                    }
                }
        }
    }
    free(near_tape);
    if (g->host && !(g->flags & PAPER_TAPE_OPAQUE)) { // how it lies over the edge of its card (only the stuck part; they fade as it peels)
        float mk = 1.0f - smooth01(peel * 3.0f);
        if (mk > 0.004f)
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    float *s = S + ((size_t)y * W + x) * 4;
                    if (s[3] < 0.02f) continue;
                    float rx = x + 0.5f - W * 0.5f, ry = y + 0.5f - H * 0.5f, lt, dk;
                    tape_marks(g, rx * ct + ry * st + L * 0.5f, -rx * st + ry * ct + h * 0.5f, &lt, &dk);
                    lt *= mk;
                    dk *= mk;
                    static const float BR[3] = {0.16f, 0.11f, 0.03f}; // the brown of the shadows
                    for (int c = 0; c < 3; c++) {
                        s[c] = s[c] * (1.0f - lt) + lt * s[3];
                        s[c] = s[c] * (1.0f - dk) + BR[c] * dk * s[3];
                    }
                }
    }
    if (g->host && !(g->flags & PAPER_TAPE_OPAQUE)) tape_texture(S, W, H, g, ct, st, L, h);
    float *pm = calloc(N * 4, sizeof(float));
    shF = u_peel > 2 ? malloc(N * sizeof(float)) : NULL;
    int faint = !(g->flags & (PAPER_TAPE_EDGE | PAPER_TAPE_OPAQUE)); // a strip on a card: only a faint contact shadow
    if (g->flags & PAPER_TAPE_EDGE) {
        shS1 = malloc(N * sizeof(float));
        shS2 = malloc(N * sizeof(float));
    } else if (faint) {
        shS2 = malloc(N * sizeof(float));
    }
    if (!pm || (u_peel > 2 && !shF) || ((g->flags & PAPER_TAPE_EDGE) && (!shS1 || !shS2)) || (faint && !shS2)) goto done;
    if (faint) {
        for (size_t i = 0; i < N; i++) shS2[i] = S[i * 4 + 3];
        blur_plane(shS2, W, H, 1, 2);
        for (int y = 1; y < H; y++)
            for (int x = 0; x < W; x++) {
                size_t i = (size_t)y * W + x;
                float sa = shS2[i - W] * 0.17f * (1.0f - S[i * 4 + 3]), r2[4] = {TAPE_SH(0) * sa, TAPE_SH(1) * sa, TAPE_SH(2) * sa, sa};
                over_premul(pm + i * 4, r2);
            }
    }
    // shadows from the alpha planes: a hairline round the strip and a soft one below it, and the flap's, 4 px down
    if (shS1) {
        for (size_t i = 0; i < N; i++) shS1[i] = shS2[i] = S[i * 4 + 3];
        blur_plane(shS1, W, H, 1, 1);
        blur_plane(shS2, W, H, 1, 2);
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                size_t i = (size_t)y * W + x;
                float out_of = 1.0f - S[i * 4 + 3]; // (only outside the strip: it is see-through, so a shadow under it would dirty its colour)
                float edge = clampf(shS1[i] * 1.4f - 0.2f, 0, 1) * 0.5f * out_of;
                float soft = (y > 0 ? shS2[i - W] : 0.0f) * 0.28f * out_of;
                float r1[4] = {70.0f / 255 * edge, 50.0f / 255 * edge, 25.0f / 255 * edge, edge};
                float r2[4] = {74.0f / 255 * soft, 58.0f / 255 * soft, 26.0f / 255 * soft, soft};
                over_premul(pm + i * 4, r1);
                over_premul(pm + i * 4, r2);
            }
    }
    for (size_t i = 0; i < N; i++) { // the strip over its shadows
        float over[4] = {S[i * 4], S[i * 4 + 1], S[i * 4 + 2], S[i * 4 + 3]};
        float *d = pm + i * 4, k = 1.0f - over[3];
        for (int c = 0; c < 4; c++) d[c] = over[c] + d[c] * k;
    }
    if (shF) {
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) shF[(size_t)y * W + x] = (y >= 4) ? F[((size_t)(y - 4) * W + x) * 4 + 3] : 0.0f;
        blur_plane(shF, W, H, 1, 2);
        float k = 0.35f * minf(1.0f, peel * 4.0f);
        for (size_t i = 0; i < N; i++) {
            float sa = shF[i] * k, r[4] = {TAPE_SH(0) * sa, TAPE_SH(1) * sa, TAPE_SH(2) * sa, sa};
            float *d = pm + i * 4;
            float kk = 1.0f - r[3];
            for (int c = 0; c < 4; c++) d[c] = r[c] + d[c] * kk;
            over_premul(d, F + i * 4);
        }
    }
    spr_from_premul(out, pm);
done:
    free(S);
    free(F);
    free(pm);
    free(shS1);
    free(shS2);
    free(shF);
}

// A strip that has been drawn is kept, so drawing the same one again is a copy: the same tiles, rows and strips come up on every visit to a screen, and a
// strip costs several milliseconds on the console. The key is every number that goes into the strip (the whole of TapeGeo, the peel and the sprite size).
enum { TCACHE_N = 192 };
#define TCACHE_BYTES (8u << 20)
static struct {
    TapeGeo g;
    float peel;
    int w, h;
    uint8_t *px;
} g_tc[TCACHE_N];
static int g_tc_next;
static size_t g_tc_bytes;

// every kept strip goes (the palette changed: what a strip lets through is part of its colour)
static void tape_cache_flush(void)
{
    for (int i = 0; i < TCACHE_N; i++) free(g_tc[i].px);
    memset(g_tc, 0, sizeof g_tc);
    g_tc_next = 0;
    g_tc_bytes = 0;
}

static void tape_render_cached(PaperSpr *out, const TapeGeo *g, float peel)
{
    size_t bytes = (size_t)out->w * out->h * 4;
    for (int i = 0; i < TCACHE_N; i++)
        if (g_tc[i].px && g_tc[i].peel == peel && g_tc[i].w == out->w && g_tc[i].h == out->h && !memcmp(&g_tc[i].g, g, sizeof *g)) {
            memcpy(out->px, g_tc[i].px, bytes);
            return;
        }
    tape_render(out, g, peel);
    uint8_t *copy = malloc(bytes);
    if (!copy) return;
    int i = g_tc_next;
    g_tc_next = (g_tc_next + 1) % TCACHE_N;
    if (g_tc[i].px) {
        g_tc_bytes -= (size_t)g_tc[i].w * g_tc[i].h * 4;
        free(g_tc[i].px);
        g_tc[i].px = NULL;
    }
    if (g_tc_bytes + bytes > TCACHE_BYTES) {
        free(copy);
        return;
    }
    memcpy(copy, out->px, bytes);
    memset(&g_tc[i], 0, sizeof g_tc[i]);
    memcpy(&g_tc[i].g, g, sizeof *g);
    g_tc[i].peel = peel;
    g_tc[i].w = out->w;
    g_tc[i].h = out->h;
    g_tc[i].px = copy;
    g_tc_bytes += bytes;
}

PaperSpr *paper_tape(const PaperTape *t)
{
    TapeGeo g;
    tape_geo(&g, t);
    int W, H;
    tape_box(&g, 0, &W, &H);
    PaperSpr *p = paper_spr_new(W, H);
    if (!p) return NULL;
    p->ox = W / 2;
    p->oy = H / 2;
    tape_render_cached(p, &g, 0);
    return p;
}

static float spr_alpha(const PaperSpr *p, int x, int y)
{
    if (x < 0 || y < 0 || x >= p->w || y >= p->h) return 0.0f;
    return p->px[((size_t)y * p->w + x) * 4 + 3] * (1.0f / 255.0f);
}

PaperSpr *paper_shadow_of(const PaperSpr *src, float dx, float dy, float sigma, uint32_t rgb, float alpha)
{
    if (!src) return NULL;
    int M = (int)ceilf(3.0f * sigma + fmaxf(fabsf(dx), fabsf(dy))) + 2;
    int W = src->w + 2 * M, H = src->h + 2 * M;
    float *mask = malloc((size_t)W * H * sizeof *mask);
    if (!mask) return NULL;
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) { // the shape's alpha moved by (dx, dy), between whole pixels
            float fx = x - M - dx, fy = y - M - dy;
            int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
            float tx = fx - x0, ty = fy - y0;
            float top = spr_alpha(src, x0, y0) * (1 - tx) + spr_alpha(src, x0 + 1, y0) * tx;
            float bot = spr_alpha(src, x0, y0 + 1) * (1 - tx) + spr_alpha(src, x0 + 1, y0 + 1) * tx;
            mask[(size_t)y * W + x] = top * (1 - ty) + bot * ty;
        }
    blur_plane(mask, W, H, blur_radius_for(sigma), 3);
    PaperSpr *s = paper_spr_new(W, H);
    if (!s) {
        free(mask);
        return NULL;
    }
    s->ox = src->ox + M;
    s->oy = src->oy + M;
    uint8_t r8 = (rgb >> 16) & 255, g8 = (rgb >> 8) & 255, b8 = rgb & 255;
    for (size_t j = 0, cnt = (size_t)W * H; j < cnt; j++) {
        int a = (int)(mask[j] * alpha * 255.0f + 0.5f);
        if (!a) continue;
        s->px[j * 4] = r8;
        s->px[j * 4 + 1] = g8;
        s->px[j * 4 + 2] = b8;
        s->px[j * 4 + 3] = (uint8_t)a;
    }
    free(mask);
    return s;
}

static PaperPeel *peel_make(const PaperTape *t, int n, int lazy)
{
    if (n < 2) n = 2;
    if (n > PAPER_PEEL_MAX) n = PAPER_PEEL_MAX;
    TapeGeo g;
    tape_geo(&g, t);
    g.crimp = 0; // a corner strip is not crimped
    int W, H;
    tape_box(&g, 1.0f, &W, &H);
    PaperPeel *pl = calloc(1, sizeof *pl);
    if (!pl) return NULL;
    pl->w = W;
    pl->h = H;
    if (lazy) {
        TapeGeo *kept = malloc(sizeof *kept);
        if (!kept) {
            free(pl);
            return NULL;
        }
        memcpy(kept, &g, sizeof g);
        pl->geo = kept;
        pl->n = n;
        PaperSpr *p = paper_spr_new(W, H);
        if (!p) {
            paper_peel_free(pl);
            return NULL;
        }
        p->ox = W / 2;
        p->oy = H / 2;
        tape_render_cached(p, kept, 0.0f);
        pl->s[0] = p;
        return pl;
    }
    for (int i = 0; i < n; i++) {
        PaperSpr *p = paper_spr_new(W, H);
        if (!p) {
            paper_peel_free(pl);
            return NULL;
        }
        p->ox = W / 2;
        p->oy = H / 2;
        tape_render_cached(p, &g, i / (float)(n - 1));
        pl->s[pl->n++] = p;
    }
    return pl;
}

PaperPeel *paper_tape_peel(const PaperTape *t, int n) { return peel_make(t, n, 0); }
PaperPeel *paper_tape_peel_lazy(const PaperTape *t, int n) { return peel_make(t, n, 1); }

void paper_peel_free(PaperPeel *p)
{
    if (!p) return;
    for (int i = 0; i < p->n; i++) paper_spr_free(p->s[i]);
    free(p->geo);
    free(p);
}

const PaperSpr *paper_peel_at(const PaperPeel *p, float lift)
{
    int i = (int)(clampf(lift, 0, 1) * (p->n - 1) + 0.5f);
    i = i < 0 ? 0 : i > p->n - 1 ? p->n - 1 : i;
    if (!p->s[i] && p->geo) { // a lazy peel: this state is made now
        PaperPeel *m = (PaperPeel *)p;
        PaperSpr *s = paper_spr_new(m->w, m->h);
        if (s) {
            s->ox = m->w / 2;
            s->oy = m->h / 2;
            tape_render_cached(s, m->geo, i / (float)(m->n - 1));
            m->s[i] = s;
        }
    }
    return p->s[i] ? p->s[i] : p->s[0];
}

// ------------------------------------------------------------------------------------------------ crayon marks
PaperCrayon PAPER_CRAYON_STD = {PAPER_CRAYON_1, PAPER_CRAYON_2, 0.8f, 0.4f, 0.75f}; // (its two colours follow the palette: paper_pal_set)
PaperCrayon PAPER_CRAYON_BLUE = {PAPER_INK, PAPER_INK, 0.88f, 0.45f, 1.0f}; // (its colour follows the palette's ink)

// ------------------------------------------------------------------------------------------------ the palette
// The light theme's numbers are the ones written at each PAL() in the code; Paper night's are pal_night's.
#define PAL_LIGHT_INIT                                                                                                                                     \
    {                                                                                                                                                      \
        .board = PAPER_BOARD, .paper = PAPER_PAPER, .text = PAPER_TEXT, .text_soft = PAPER_TEXT_SOFT, .thread = PAPER_THREAD, .ink = PAPER_INK,           \
        .ghost_fill = PAPER_GHOST_FILL, .ghost_edge = PAPER_GHOST_EDGE, .crayon_1 = PAPER_CRAYON_1, .crayon_2 = PAPER_CRAYON_2, .shadow = PAPER_SHADOW,  \
        .fleck = {206.4f, 194.1f, 165.7f}, .fleck_k = 1.0f, .vignette = {138.0f, 122.0f, 85.0f}, .vignette_k = 0.16f, .grain = {0.55f, 0.47f, 0.33f},   \
        .grain_k = 0.085f, .grain_mode = 0, .rim = 0x000000, .rim_a = 0.06f, .shadow_k = 1.0f, .tape_board = {228.0f / 255, 221.0f / 255, 204.0f / 255}, \
        .tape_paper = {1.0f, 0.996f, 0.984f}, .tape_alpha = 0.70f, .tape_shadow = 0x4A3A1A, .panel_alt = 0xF3EEE0, .line = 0xDDD5C3,                     \
        .panel_rim = 0x000000, .panel_rim_a = 0.06f, .scrim = {0.945f, 0.922f, 0.867f, 0.62f}, .mark_ink = PAPER_INK, .board_thread = PAPER_THREAD,      \
        .ink_on = 0xFFFFFF, .glyph_flat = 0, .glyph_ink = PAPER_TEXT, .glyph_ring = PAPER_TEXT_SOFT, .sign_fill = 0xE8F8E0, .sign_disc = PAPER_PAPER,     \
        .sign_ink = PAPER_TEXT, .x_fill = 0xE8F8E0, .new_disc = 0xFBE8F6, .new_ink = PAPER_TEXT_SOFT, .rail_arrow = 0x4A4658, .rail_on = PAPER_TEXT,      \
        .rail_off = 0xAEA9BE, .title_ink = PAPER_TEXT, .hi_mix = 0.0f, .quiet = 0xF1EFF4, .slip = 0xF7F3E8, .dim = 0x8A857A, .pill_off = 0xE7E1D1,        \
        .pill_edge = 0xB9AE92, .patch = 0xECE9F1, .rule = 0xE3DFEA, .knob = PAPER_PAPER, .bad_ink = 0xB3261E, .ok_ink = 0x1E6B34, .toggle_on = PAPER_INK,\
        .sw_guide = {0.45f, 0.40f, 0.30f, 0.62f}, .disc_shadow = {0.29f, 0.227f, 0.1f, 1.0f}, .flap = 0xF2ECDA, .flap_line = 0xB8AE92, .step_dim = 0x6E6A80,\
        .go = 0xD4FAC7, .title_strip = PAPER_TITLE_STRIP, .title_fold = PAPER_TITLE_FOLD, .tape = {PAPER_TAPE_LIST},                                   \
    }
static const PaperPal PAL_LIGHT = PAL_LIGHT_INIT;
PaperPal g_pal = PAL_LIGHT_INIT;
unsigned paper_pal_serial = 1;
static int g_pal_locked; // (mockups: a WASHI_PAL palette stays, whatever theme the app asks for)

static void pal_rgb(float o[3], uint32_t rgb, float k)
{
    for (int c = 0; c < 3; c++) o[c] = ((rgb >> (16 - 8 * c)) & 255) * k;
}

// Paper night, the dark theme: the board a night scene cut from paper (board_scene 2), the cards night-blue paper whose grain is lighter flecks with a
// white rim, pale writing, pale wax rings, a pale blue stamp, the corner seal in the moon's cream, black shadows (a warm brown would lighten the night),
// the footer's strip and the status tag of the cards' paper. The tape, the pictures, the button discs and the name's mint strip stay as they are.
static void pal_night(PaperPal *p)
{
    p->board_scene = 2;
    p->board = 0x2A2546; // (the sky's middle: what a page that fills with the board's flat colour shows)
    p->vignette_k = 0.0f;
    p->paper = 0x29283B;
    pal_rgb(p->grain, 0x3A384E, 1.0f / 255);
    p->grain_k = 0.08f, p->grain_mode = 1;
    p->rim = 0xFFFFFF, p->rim_a = 0.06f;
    p->text = 0xEFEAF2, p->text_soft = 0xBDB6C8, p->thread = 0x8E86A0;
    p->ink = 0xB4BCF0, p->mark_ink = 0xECE2C4, p->ink_on = 0x1E1F3A, p->board_thread = 0xCFC8DC;
    p->crayon_1 = 0xE2DCEE, p->crayon_2 = 0xA9A1C2;
    p->shadow = 0x050403, p->shadow_k = 1.6f, p->tape_shadow = 0x000000;
    p->ghost_fill = 0x24233A, p->ghost_edge = 0x4A4766;
    p->tape_alpha = 0.80f;
    p->panels_paper = 1;
    p->panel_alt = 0x333248, p->line = 0x45435C, p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.06f;
    p->scrim[0] = 0.08f, p->scrim[1] = 0.08f, p->scrim[2] = 0.14f;
    p->glyph_ring = 0x4A4658;
    p->sign_fill = 0x3A3858, p->sign_disc = 0xE8F8E0, p->sign_ink = 0xEFEAF2;
    p->hi_mix = 0.65f;
    p->new_disc = 0x3A3852, p->new_ink = 0xDCD6E8;
    p->rail_arrow = 0xD6D0E4, p->rail_on = 0xEFEAF2, p->rail_off = 0x615C7A;
    // the other pages: the quiet slips as the Sign out sticker (a shade lighter than the card they lie on), with pale writing; the coloured ones (the
    // button that says yes, the badges, the hub's section tabs) stay pastel with dark writing, as the button discs do
    p->quiet = 0x3A3858, p->slip = 0x353449, p->dim = 0x6E6A80, p->pill_off = 0x3A3858, p->pill_edge = 0x6E6A86, p->patch = 0x33324A, p->rule = 0x3A394F;
    p->knob = 0xDCD6E8, p->bad_ink = 0xF2A39C, p->ok_ink = 0x93D9A6, p->toggle_on = 0x6C76C4; // (a pale knob shows on a mid blue, not on the pale ink)
    p->sw_guide = col(0.86f, 0.84f, 0.93f, 0.45f), p->disc_shadow = col(0.0f, 0.0f, 0.0f, 1.5f);
    p->flap = 0x423F5E, p->flap_line = 0x6E6A86, p->step_dim = 0x8E86A0, p->doodle_lift = 0.45f;
    // colours from the scene itself (the user: "cohesive with the background", as the rainbow is with the cream board): the button that says yes is the lit
    // window's gold, the name's strip the stamp's pale blue ink (its fold lighter, as the mint's is), the hub's tabs things in the picture (the blue hill, the
    // teal hill, the cloud's lavender, the dusk sky's mauve; pale writing) with the open one's strip a light tint of its colour
    p->go = 0xF2C86E;
    p->title_strip = PAPER_NIGHT_TITLE_STRIP, p->title_fold = PAPER_NIGHT_TITLE_FOLD;
    static const uint32_t TAB_FILL[4] = {0x48588B, 0x33636E, 0x635A90, 0x704A78}, TAB_STRIP[4] = {0xACBCEB, 0x9EC6D0, 0xBCB7E7, 0xD2B0D9};
    memcpy(p->tab_fill, TAB_FILL, sizeof TAB_FILL);
    memcpy(p->tab_strip, TAB_STRIP, sizeof TAB_STRIP);
    // the washi tape in colours from the scene, its slots keeping their hues (pink, peach, yellow, mint, teal, blue, lilac, magenta): the dusk's rose, the window's
    // amber, the moon's cream, the pines, the hills' teal and blue, the cloud's lavender, the sky's mauve (the user picked W3 of the review page's five)
    static const uint32_t TAPE[PAPER_TAPE_COUNT] = {0xC38BA5, 0xE1B581, 0xE2D7B7, 0x84A894, 0x719FA4, 0x8394C1, 0x9F96CD, 0xAD85B3};
    memcpy(p->tape, TAPE, sizeof TAPE);
}

// what follows from a dark palette (g_pal must already be it): what a frosted strip of tape lets through, the paper as it is and the board's own mean
// colour, measured from the board itself
static void pal_derive(PaperPal *p)
{
    pal_rgb(p->tape_paper, p->paper, 1.0f / 255);
    Surf *b = surf_new(TILE, TILE);
    if (!b) return;
    paper_board_fill(b, 8u);
    double sum[3] = {0, 0, 0};
    for (size_t i = 0; i < (size_t)TILE * TILE; i++)
        for (int c = 0; c < 3; c++) sum[c] += b->px[i * 4 + c];
    for (int c = 0; c < 3; c++) p->tape_board[c] = (float)(sum[c] / ((double)TILE * TILE) / 255.0);
    surf_free(b);
}

void paper_pal_set(int night)
{
    static int current = 0; // (g_pal starts as the light theme)
    if (g_pal_locked || (night != 0) == current) return;
    current = night != 0;
    g_pal = PAL_LIGHT;
    if (night) {
        pal_night(&g_pal);
        pal_derive(&g_pal);
    }
    PAPER_CRAYON_STD.c1 = g_pal.crayon_1;
    PAPER_CRAYON_STD.c2 = g_pal.crayon_2;
    PAPER_CRAYON_BLUE.c1 = PAPER_CRAYON_BLUE.c2 = g_pal.ink;
    memcpy(paper_tape_rgb, g_pal.tape, sizeof paper_tape_rgb);
    tape_cache_flush(); // (a strip's colour depends on what it lets through)
    paper_pal_serial++;
}

#ifdef WASHI_PAL_LAB
// ---- mockups only: the dark mode rounds' other palettes (WASHI_PAL=a..o; p is Paper night as the app has it, j as it first was)

// the dyed washi family (f, h, i): a lacquered board; dyed paper with long fine fibres; leaf at the cards' corners; fine metal rings; the seals in seal ink
static void pal_dyed(PaperPal *p, uint32_t board, uint32_t paper, uint32_t fib, uint32_t leaf, uint32_t leaf_dark, uint32_t seal, uint32_t sticker)
{
    p->board = board;
    pal_rgb(p->fleck, pal_mix(board, 0xFFFFFF, 0.04f), 1.0f);
    p->fleck_k = 0.25f;
    pal_rgb(p->vignette, pal_mix(board, 0x000000, 0.6f), 1.0f);
    p->vignette_k = 0.35f;
    p->board_sheen = 0.10f;
    p->paper = paper;
    pal_rgb(p->grain, pal_mix(paper, 0xFFFFFF, 0.08f), 1.0f / 255);
    p->grain_k = 0.09f;
    p->fib_n = 4.0f, p->fib_len = 150.0f, p->fib_w = 0.75f, p->fib_bend = 1.3f, p->fib_k = 0.26f, p->fib_rgb = fib;
    p->leaf_corner = 1, p->leaf_n = 8.0f, p->leaf_sq = 10.0f, p->leaf_noge = 10.0f, p->leaf_dust = 120.0f, p->leaf_rgb = leaf, p->leaf_dark = leaf_dark;
    p->rim = 0xFFFFFF, p->rim_a = 0.06f;
    p->ink = seal, p->mark_ink = seal;
    p->crayon_1 = pal_mix(leaf, leaf_dark, 0.25f), p->crayon_2 = leaf_dark, p->crayon_wk = 0.5f;
    p->shadow_k = 1.8f;
    p->ghost_fill = pal_mix(board, 0xFFFFFF, 0.05f), p->ghost_edge = pal_mix(board, 0xFFFFFF, 0.22f);
    p->tape_alpha = 0.80f;
    p->panels_paper = 1;
    p->panel_alt = pal_mix(paper, 0xFFFFFF, 0.06f), p->line = pal_mix(paper, 0xFFFFFF, 0.15f), p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.06f;
    p->scrim[0] = p->scrim[1] = p->scrim[2] = 0.06f;
    p->glyph_ring = 0x4A4658;
    p->sign_fill = sticker, p->sign_disc = 0xE8F8E0;
    p->hi_mix = 0.65f;
    p->new_disc = pal_mix(paper, 0xFFFFFF, 0.1f);
}

__attribute__((constructor)) static void pal_lab_init(void)
{
    const char *v = getenv("WASHI_PAL");
    PaperPal *p = &g_pal;
    if (!v || !v[0]) return;
    // what every dark one shares: the paper's grain is lighter flecks, shadows are black and stronger (a warm brown would lighten a dark board), the
    // buttons' discs keep their pastel with dark letters, the Sign out sticker stays pale mint, the name's strip stays mint with a dark name
    p->grain_mode = 1;
    p->shadow = 0x050403;
    p->tape_shadow = 0x000000;
    p->ink_on = 0x1E2140;
    switch (v[0]) {
    case 'a': // lights down: the board is dark charcoal cork; every paper on it stays paper (a little dimmed), so writing, ink and crayon are as they are
        p->board = 0x25221E;
        pal_rgb(p->fleck, 0x3B362F, 1.0f);
        p->fleck_k = 0.9f;
        pal_rgb(p->vignette, 0x0E0C0A, 1.0f);
        p->vignette_k = 0.30f;
        p->paper = 0xF3EEE4;
        p->grain_mode = 0; // (light paper: the warm brown grain as it is)
        p->ink_on = 0xFFFFFF;
        p->shadow_k = 1.6f;
        p->ghost_fill = 0x2E2A25;
        p->ghost_edge = 0x57504A;
        p->mark_ink = 0xB8BEF2;
        p->board_thread = 0xA6A0B8;
        p->sign_disc = 0xF3EEE4;
        p->new_disc = 0xF6E2EF;
        p->panel_alt = 0xE9E3D6;
        p->scrim[0] = 0.10f, p->scrim[1] = 0.09f, p->scrim[2] = 0.08f;
        break;
    case 'b': // black paper: black card stock on a darker board, white gel pen, pale wax crayon, pale pigment ink; the tape stays pastel washi
        p->board = 0x1B1A19;
        pal_rgb(p->fleck, 0x2C2A28, 1.0f);
        p->fleck_k = 0.7f;
        pal_rgb(p->vignette, 0x080807, 1.0f);
        p->vignette_k = 0.35f;
        p->paper = 0x302E2C;
        pal_rgb(p->grain, 0x4C4843, 1.0f / 255);
        p->grain_k = 0.12f;
        p->rim = 0xFFFFFF, p->rim_a = 0.08f; // (the cut edge catches the light)
        p->text = 0xF2EDE4, p->text_soft = 0xBDB7AC, p->thread = 0x8F899E;
        p->ink = 0xA9B3F5, p->mark_ink = 0xA9B3F5, p->board_thread = 0x9590A4;
        p->crayon_1 = 0xDAD3EE, p->crayon_2 = 0xA9A1C8;
        p->shadow_k = 1.8f;
        p->ghost_fill = 0x252423, p->ghost_edge = 0x4E4A46;
        p->tape_alpha = 0.80f;
        p->panel_alt = 0x3B3936, p->line = 0x4A4743, p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.07f;
        p->scrim[0] = 0.06f, p->scrim[1] = 0.06f, p->scrim[2] = 0.06f;
        p->glyph_ring = 0x4A4658;
        p->sign_fill = 0x34473B, p->sign_disc = 0xE8F8E0, p->sign_ink = 0xF2EDE4; // (a deep mint sticker; its X keeps the footer's pale disc)
        p->hi_mix = 0.65f;
        p->new_disc = 0x4A3C48, p->new_ink = 0xE8D9E6;
        p->rail_arrow = 0xD2CCDD, p->rail_on = 0xF2EDE4, p->rail_off = 0x6B6676;
        break;
    case 'c': // the console's Basic Black: its flat greys and 1 px lines, white writing, the buttons outlined in white as the console draws them;
              // only the tape and the pictures bring colour
        p->board = 0x2D2D2D;
        pal_rgb(p->fleck, 0x333333, 1.0f);
        p->fleck_k = 0.35f;
        p->vignette_k = 0.0f;
        p->paper = 0x3A3A3A;
        pal_rgb(p->grain, 0x444444, 1.0f / 255);
        p->grain_k = 0.04f;
        p->rim = 0x4D4D4D, p->rim_a = 1.0f;
        p->text = 0xFFFFFF, p->text_soft = 0xC8C8C8, p->thread = 0x8C8C8C;
        p->ink = 0xA3ACF2, p->mark_ink = 0xEDEDED, p->board_thread = 0x9A9A9A;
        p->crayon_1 = 0xE0E0E0, p->crayon_2 = 0xA0A0A0;
        p->shadow_k = 1.3f;
        p->ghost_fill = 0x323232, p->ghost_edge = 0x555555;
        p->tape_alpha = 0.78f;
        p->panel_alt = 0x444444, p->line = 0x4D4D4D, p->panel_rim = 0x4D4D4D, p->panel_rim_a = 1.0f;
        p->scrim[0] = 0.10f, p->scrim[1] = 0.10f, p->scrim[2] = 0.10f;
        p->glyph_flat = 1, p->glyph_ink = 0xFFFFFF, p->glyph_ring = 0xEAEAEA;
        p->sign_fill = 0x44504A, p->sign_disc = 0x44504A, p->sign_ink = 0xFFFFFF;
        p->hi_mix = 0.65f;
        p->new_disc = 0x474747, p->new_ink = 0xD0D0D0;
        p->rail_arrow = 0xD8D8D8, p->rail_on = 0xFFFFFF, p->rail_off = 0x6E6E6E;
        break;
    case 'd': // indigo night: the ink's own colour becomes the room: an indigo board, navy paper, cream writing, pale ink; the tape glows on it
        p->board = 0x1C1E36;
        pal_rgb(p->fleck, 0x2C2F50, 1.0f);
        p->fleck_k = 0.7f;
        pal_rgb(p->vignette, 0x0A0B18, 1.0f);
        p->vignette_k = 0.35f;
        p->paper = 0x2A2E4C;
        pal_rgb(p->grain, 0x3E4268, 1.0f / 255);
        p->grain_k = 0.10f;
        p->rim = 0xFFFFFF, p->rim_a = 0.07f;
        p->text = 0xF3EFE4, p->text_soft = 0xBEBAD6, p->thread = 0x9290B5;
        p->ink = 0xC3C9FF, p->mark_ink = 0xC3C9FF, p->board_thread = 0x9A97BC, p->ink_on = 0x1C1E36;
        p->crayon_1 = 0xE6E1F7, p->crayon_2 = 0xB0AAD6;
        p->shadow = 0x05060F, p->shadow_k = 1.8f;
        p->ghost_fill = 0x23264A, p->ghost_edge = 0x4B4F7A;
        p->tape_alpha = 0.80f;
        p->panel_alt = 0x363A5C, p->line = 0x444870, p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.07f;
        p->scrim[0] = 0.07f, p->scrim[1] = 0.07f, p->scrim[2] = 0.13f;
        p->glyph_ring = 0x4A4658;
        p->sign_fill = 0x34503F, p->sign_disc = 0xE8F8E0, p->sign_ink = 0xF3EFE4;
        p->hi_mix = 0.6f;
        p->new_disc = 0x3F3B62, p->new_ink = 0xDCD8F0;
        p->rail_arrow = 0xD6D2EC, p->rail_on = 0xF3EFE4, p->rail_off = 0x615E86;
        break;
    case 'e': // a lantern: washi is lantern paper, so at night the cards glow with a light behind them; the fibres show in it, the writing is sumi ink,
              // the tape is seen against the light; the room around them is dark
        p->board = 0x15110D;
        pal_rgb(p->fleck, 0x1E1915, 1.0f);
        p->fleck_k = 0.5f;
        pal_rgb(p->vignette, 0x060403, 1.0f);
        p->vignette_k = 0.45f;
        p->paper = 0xFFF0CF;
        p->grain_mode = 0, p->grain_k = 0.06f;
        p->fib_n = 9.0f, p->fib_k = 0.35f, p->fib_rgb = 0xC08A48;
        p->cloud_k = 0.10f;
        p->lantern_k = 0.62f, p->lantern_edge = 0xD99A4E;
        p->rim_a = 0.0f;
        p->text = 0x2A2018, p->text_soft = 0x5C4A36, p->thread = 0x7A6650;
        p->ink_on = 0xFFFFFF;
        p->mark_ink = 0xEBD3A0, p->board_thread = 0xB49A78;
        p->shadow = 0xC07A2E, p->shadow_k = 0.6f; // (no shadow on the dark: a little of the light spills round each card instead)
        p->ghost_fill = 0x1E1915, p->ghost_edge = 0x4A3C2E;
        p->tape_alpha = 0.78f;
        p->panels_paper = 1;
        p->panel_alt = 0xE8CD98, p->line = 0xC9A874;
        p->scrim[0] = 0.08f, p->scrim[1] = 0.06f, p->scrim[2] = 0.05f;
        p->glyph_ring = 0x5A4A3A;
        p->sign_fill = 0xDDEFD0, p->sign_disc = 0xF8EBCB;
        p->new_disc = 0xF0CFC0, p->new_ink = 0x5C4A36;
        p->rail_arrow = 0x5C4A36, p->rail_on = 0x2A2018, p->rail_off = 0xB39A7A;
        break;
    case 'f': // black washi and gold leaf (refined): kurozome paper with long unryu fibres, gold leaf as it is laid (dust, cut squares, slivers, a few torn
              // flakes) gathered at the cards' corners, on a black lacquer board; shell-white writing, fine gold rings, the seals in vermilion
        pal_dyed(p, 0x120F0E, 0x221F1D, 0x655A50, 0xE9CB7C, 0x9A7834, 0xD23C2A, 0x3A4630);
        p->cloud_k = 0.03f;
        p->text = 0xF1EADB, p->text_soft = 0xBCB2A2, p->thread = 0x8A8070;
        p->ink_on = 0xFFFFFF, p->board_thread = 0x8F8A84, p->sign_ink = 0xF1EADB, p->new_ink = 0xE5D9C5;
        p->rail_arrow = 0xD8CDBA, p->rail_on = 0xF1EADB, p->rail_off = 0x6A625A;
        break;
    case 'h': // aizome: indigo-dyed washi, its dye uneven as indigo is, silver leaf, on an indigo lacquer board; white writing, silver rings, vermilion seals
        pal_dyed(p, 0x0E1120, 0x1B2547, 0x3E4C76, 0xE3E6EA, 0x868C96, 0xE24B30, 0x2A3760);
        p->cloud_k = 0.08f;
        p->text = 0xF2F1EC, p->text_soft = 0xB9BFD2, p->thread = 0x8790AE;
        p->ink_on = 0xFFFFFF, p->board_thread = 0x8E95AE, p->sign_ink = 0xF2F1EC, p->new_ink = 0xD8DCEA;
        p->rail_arrow = 0xD3D7E4, p->rail_on = 0xF2F1EC, p->rail_off = 0x55608A;
        break;
    case 'i': // murasaki: purple-dyed washi with gold leaf, on a plum lacquer board; cream writing, gold rings, the seals a lighter vermilion to show on purple
        pal_dyed(p, 0x150E19, 0x2B1D3A, 0x4E3D62, 0xE9CB7C, 0x9A7834, 0xEE6B4D, 0x3E2D52);
        p->cloud_k = 0.07f;
        p->text = 0xF3ECE0, p->text_soft = 0xC4B8CC, p->thread = 0x9284A3;
        p->ink_on = 0x2B1D3A, p->board_thread = 0x9A8CAA, p->sign_ink = 0xF3ECE0, p->new_ink = 0xE0D6E8;
        p->rail_arrow = 0xDCD2E4, p->rail_on = 0xF3ECE0, p->rail_off = 0x6A5A7E;
        break;
    case 'j': // paper night: the app's own (pal_night); 'j' is it as it first was (the first scene, the light board colour)
    case 'p':
        pal_night(p);
        if (v[0] == 'j') p->board_scene = 1, p->board = PAPER_BOARD;
        break;
    case 'l': // game night: the board is the club's game table, green felt under a lamp with gold arcs and GAME NIGHT printed in it, dice, a d20, a meeple
              // and tokens in its empty places; the cards are black playing cards, each with the person's initial and a suit; the stamp is gold
        p->board = 0x17402B;
        p->fleck_k = 0.0f;
        pal_rgb(p->vignette, 0x061810, 1.0f);
        p->vignette_k = 0.45f;
        p->board_felt = 1, p->card_index = 1;
        p->paper = 0x1D1D21;
        pal_rgb(p->grain, 0x2E2E33, 1.0f / 255);
        p->grain_k = 0.05f;
        p->rim = 0xFFFFFF, p->rim_a = 0.07f;
        p->text = 0xF4F2EC, p->text_soft = 0xB9B6B0, p->thread = 0x8C8984;
        p->ink = 0xD9B45A, p->mark_ink = 0xD9B45A, p->ink_on = 0x1D1D21, p->board_thread = 0xCFE3D6;
        p->crayon_1 = 0xE8E4DC, p->crayon_2 = 0xA8A39A, p->crayon_wk = 0.6f;
        p->shadow_k = 1.6f;
        p->ghost_fill = 0x153A28, p->ghost_edge = 0x2F5A44;
        p->tape_alpha = 0.80f;
        p->panels_paper = 1;
        p->panel_alt = 0x29292E, p->line = 0x3A3A40, p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.07f;
        p->scrim[0] = 0.05f, p->scrim[1] = 0.12f, p->scrim[2] = 0.08f;
        p->glyph_ring = 0x4A4658;
        p->sign_fill = 0x5A2028, p->sign_disc = 0xE8F8E0, p->sign_ink = 0xF4F2EC; // (a deep card red)
        p->hi_mix = 0.65f;
        p->new_disc = 0x303036, p->new_ink = 0xE8E4DC;
        p->rail_arrow = 0xE2DED6, p->rail_on = 0xF4F2EC, p->rail_off = 0x66646A;
        break;
    case 'm': // the craft wall: a burgundy pegboard with the app's own tape rolls on brass hooks and torn pieces stuck on for later; the cards are dark
              // cardstock tags in white pencil, the stamp pale blue
        p->board = 0x4A1D24;
        p->fleck_k = 0.0f;
        pal_rgb(p->vignette, 0x180709, 1.0f);
        p->vignette_k = 0.40f;
        p->board_peg = 1;
        p->paper = 0x2B2627;
        pal_rgb(p->grain, 0x3C3537, 1.0f / 255);
        p->grain_k = 0.08f;
        p->rim = 0xFFFFFF, p->rim_a = 0.06f;
        p->text = 0xF2EEE8, p->text_soft = 0xC3BBB5, p->thread = 0x948A86;
        p->text_grain = 0.22f; // (pencil)
        p->ink = 0xB9C2F0, p->mark_ink = 0xB9C2F0, p->ink_on = 0x1E2140, p->board_thread = 0xE9D9DB;
        p->crayon_1 = 0xEEE9E2, p->crayon_2 = 0xB8AFA8, p->crayon_wk = 0.6f;
        p->shadow_k = 1.6f;
        p->ghost_fill = 0x3E1820, p->ghost_edge = 0x6E3A42;
        p->tape_alpha = 0.80f;
        p->panels_paper = 1;
        p->panel_alt = 0x363031, p->line = 0x4A4244, p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.06f;
        p->scrim[0] = 0.12f, p->scrim[1] = 0.05f, p->scrim[2] = 0.06f;
        p->glyph_ring = 0x4A4658;
        p->sign_fill = 0x34473B, p->sign_disc = 0xE8F8E0, p->sign_ink = 0xF2EEE8;
        p->hi_mix = 0.65f;
        p->new_disc = 0x3C3436, p->new_ink = 0xE8E2DC;
        p->rail_arrow = 0xDED8D2, p->rail_on = 0xF2EEE8, p->rail_off = 0x6E6466;
        break;
    case 'n': // the passport: the board is a plum passport page with gold security printing (a rosette, bands of waves) and visa stamps in the empty places;
              // the cards are visa paper with fine gold waves, the corner seal a gold crest, the LOADED stamp an entry stamp in violet-blue
        p->board = 0x2E1A2C;
        p->fleck_k = 0.0f;
        pal_rgb(p->vignette, 0x12080F, 1.0f);
        p->vignette_k = 0.35f;
        p->board_passport = 1;
        p->paper = 0x3A2436;
        pal_rgb(p->grain, 0x48304A, 1.0f / 255);
        p->grain_k = 0.06f;
        p->guil_k = 0.10f, p->guil_rgb = 0xD8B867;
        p->rim = 0xFFFFFF, p->rim_a = 0.06f;
        p->text = 0xF4ECE4, p->text_soft = 0xC9B9C4, p->thread = 0x9A8696;
        p->ink = 0x9FAEF0, p->ink_on = 0x1E1A3A, p->mark_ink = 0xD8B867, p->board_thread = 0xD8C3CF;
        p->crayon_1 = 0xD8B867, p->crayon_2 = 0x9C7A35, p->crayon_wk = 0.6f;
        p->shadow_k = 1.6f;
        p->ghost_fill = 0x351E33, p->ghost_edge = 0x5E3C58;
        p->tape_alpha = 0.80f;
        p->panels_paper = 1;
        p->panel_alt = 0x452C40, p->line = 0x5A3E55, p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.06f;
        p->scrim[0] = 0.12f, p->scrim[1] = 0.06f, p->scrim[2] = 0.11f;
        p->glyph_ring = 0x4A4658;
        p->sign_fill = 0x4A2440, p->sign_disc = 0xE8F8E0, p->sign_ink = 0xF4ECE4;
        p->hi_mix = 0.65f;
        p->new_disc = 0x4A3046, p->new_ink = 0xEADDE6;
        p->rail_arrow = 0xE3D5DE, p->rail_on = 0xF4ECE4, p->rail_off = 0x75606F;
        break;
    case 'o': // the blueprint: the club room's plan on cyanotype paper (the TV, the Switch's dock, the couch, the snacks, a door, the title block); the
              // cards are blueprint paper with white lettering, the LOADED stamp a pale yellow mark
        p->board = 0x163C73;
        p->fleck_k = 0.0f;
        pal_rgb(p->vignette, 0x081A36, 1.0f);
        p->vignette_k = 0.30f;
        p->board_blueprint = 1;
        p->paper = 0x12305E;
        pal_rgb(p->grain, 0x1C3C6E, 1.0f / 255);
        p->grain_k = 0.06f;
        p->rim = 0xFFFFFF, p->rim_a = 0.10f;
        p->text = 0xF2F5FA, p->text_soft = 0xB8C6E0, p->thread = 0x8EA2C8;
        p->ink = 0xF0D878, p->ink_on = 0x12305E, p->mark_ink = 0xF2F5FA, p->board_thread = 0xDDE6F5;
        p->crayon_1 = 0xEEF2FA, p->crayon_2 = 0xB5C2DC, p->crayon_wk = 0.6f;
        p->shadow = 0x020814, p->shadow_k = 1.5f;
        p->ghost_fill = 0x18406F, p->ghost_edge = 0x5B7CB0;
        p->tape_alpha = 0.80f;
        p->panels_paper = 1;
        p->panel_alt = 0x1A3B6E, p->line = 0x2B4C82, p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.10f;
        p->scrim[0] = 0.05f, p->scrim[1] = 0.10f, p->scrim[2] = 0.22f;
        p->glyph_ring = 0x24365A;
        p->sign_fill = 0x24487E, p->sign_disc = 0xE8F8E0, p->sign_ink = 0xF2F5FA;
        p->hi_mix = 0.65f;
        p->new_disc = 0x1E3F75, p->new_ink = 0xDDE6F5;
        p->rail_arrow = 0xDDE6F5, p->rail_on = 0xF2F5FA, p->rail_off = 0x5872A3;
        break;
    case 'k': // the mailroom: a dark cardboard board (its flutes faint under the liner, old box printing rubbed off), kraft labels written in white paint
              // marker, a white rubber stamp; the washi tape as it is
        p->board = 0x3E2C1E;
        pal_rgb(p->fleck, 0x2A1D13, 1.0f);
        p->fleck_k = 0.7f;
        pal_rgb(p->vignette, 0x140D08, 1.0f);
        p->vignette_k = 0.40f;
        p->board_ribs = 0.035f, p->board_box = 1;
        p->paper = 0x7A5C3E;
        p->grain_mode = 0, p->grain_k = 0.12f;
        p->fib_n = 12.0f, p->fib_len = 30.0f, p->fib_w = 0.7f, p->fib_k = 0.30f, p->fib_rgb = 0x5E4630;
        p->rim = 0x000000, p->rim_a = 0.12f;
        p->text = 0xFFFDF7, p->text_soft = 0xEFE5D5, p->thread = 0xD9CBB5;
        p->ink = 0xF4EEE2, p->mark_ink = 0xF4EEE2, p->ink_on = 0x2D2620, p->board_thread = 0xE8DDC8;
        p->crayon_1 = 0xF4EEE2, p->crayon_2 = 0xCDBFA8, p->crayon_wk = 0.8f;
        p->shadow_k = 1.5f;
        p->ghost_fill = 0x43301F, p->ghost_edge = 0x6E5640;
        p->tape_alpha = 0.80f;
        p->panels_paper = 1;
        p->panel_alt = 0x6E5236, p->line = 0x5E4630, p->panel_rim = 0x000000, p->panel_rim_a = 0.15f;
        p->scrim[0] = 0.12f, p->scrim[1] = 0.09f, p->scrim[2] = 0.06f;
        p->glyph_ring = 0x4A3A2A;
        p->sign_fill = 0xF3ECDD, p->sign_disc = 0xE8F8E0, p->sign_ink = 0x2D2620; // (a white address label)
        p->hi_mix = 0.5f;
        p->new_disc = 0x6B5034, p->new_ink = 0xF4EEE2;
        p->rail_arrow = 0xF4EEE2, p->rail_on = 0xFFFDF7, p->rail_off = 0xB09878;
        break;
    case 'g': // the club room's chalkboard: slate with chalk dust, eraser swipes and old writing rubbed out; the cards are chalkboard labels written
              // in chalk, the stamp is pale blue chalk paint; the washi tape is as it is
        p->board = 0x263029;
        pal_rgb(p->fleck, 0x2E3832, 1.0f);
        p->fleck_k = 0.5f;
        pal_rgb(p->vignette, 0x141A16, 1.0f);
        p->vignette_k = 0.30f;
        p->board_chalk = 1;
        p->paper = 0x1F2321;
        pal_rgb(p->grain, 0x333A36, 1.0f / 255);
        p->grain_k = 0.08f;
        p->dust_k = 0.10f;
        p->rim = 0xFFFFFF, p->rim_a = 0.06f;
        p->text = 0xF3F1EA, p->text_soft = 0xC9CCC5, p->thread = 0x8E968F;
        p->text_grain = 0.55f;
        p->ink = 0xB4C6F2, p->mark_ink = 0xB4C6F2, p->ink_on = 0x1D2331, p->board_thread = 0xC9CEC6;
        p->crayon_1 = 0xECEAE3, p->crayon_2 = 0xB9BDB5;
        p->shadow_k = 1.6f;
        p->ghost_fill = 0x2A342D, p->ghost_edge = 0x56615A;
        p->tape_alpha = 0.82f;
        p->panels_paper = 1;
        p->panel_alt = 0x2A2F2C, p->line = 0x3E4541, p->panel_rim = 0xFFFFFF, p->panel_rim_a = 0.06f;
        p->scrim[0] = 0.08f, p->scrim[1] = 0.10f, p->scrim[2] = 0.09f;
        p->glyph_ring = 0x3F4A44;
        p->sign_fill = 0x34473B, p->sign_disc = 0xE8F8E0, p->sign_ink = 0xF3F1EA;
        p->hi_mix = 0.65f;
        p->new_disc = 0x33403A, p->new_ink = 0xDDE3DC;
        p->rail_arrow = 0xDDE3DC, p->rail_on = 0xF3F1EA, p->rail_off = 0x6B746E;
        break;
    default: return;
    }
    g_pal_locked = 1;
    pal_derive(p);
    if (p->lantern_k > 0) { // (tape sits at a card's edge, where the lantern's light is dimmer and warmer)
        float e[3];
        pal_rgb(e, p->lantern_edge, 1.0f / 255);
        for (int c = 0; c < 3; c++) p->tape_paper[c] += (e[c] - p->tape_paper[c]) * 0.85f * p->lantern_k;
    }
    g_gfx_text_grain = p->text_grain;
    memcpy(paper_tape_rgb, p->tape, sizeof paper_tape_rgb);
    PAPER_CRAYON_STD.c1 = p->crayon_1;
    PAPER_CRAYON_STD.c2 = p->crayon_2;
    if (p->crayon_wk > 0) PAPER_CRAYON_STD.wk = p->crayon_wk;
}
#endif

typedef struct { float x, y; } Pt;
typedef struct { Pt *p; int n, cap; } Path;

static void path_add(Path *a, float x, float y)
{
    if (a->n == a->cap) {
        a->cap = a->cap ? a->cap * 2 : 64;
        a->p = realloc(a->p, (size_t)a->cap * sizeof(Pt));
    }
    a->p[a->n].x = x;
    a->p[a->n].y = y;
    a->n++;
}
static void path_line(Path *a, float x, float y) // to (x, y), with points about every 2.5 px so the stroke can be bent and broken up
{
    if (!a->n) {
        path_add(a, x, y);
        return;
    }
    Pt c = a->p[a->n - 1];
    float dx = x - c.x, dy = y - c.y, d = sqrtf(dx * dx + dy * dy);
    int k = (int)ceilf(d / 2.5f);
    for (int i = 1; i <= k; i++) path_add(a, c.x + dx * i / k, c.y + dy * i / k);
}
static void path_quad(Path *a, float qx, float qy, float x, float y)
{
    Pt c = a->p[a->n - 1];
    float d = sqrtf((qx - c.x) * (qx - c.x) + (qy - c.y) * (qy - c.y)) + sqrtf((x - qx) * (x - qx) + (y - qy) * (y - qy));
    int k = (int)ceilf(d / 2.5f);
    if (k < 2) k = 2;
    for (int i = 1; i <= k; i++) {
        float t = (float)i / k, m = 1.0f - t;
        path_add(a, m * m * c.x + 2 * m * t * qx + t * t * x, m * m * c.y + 2 * m * t * qy + t * t * y);
    }
}

// the mockup's hand-drawn forms, ported
static void hand_ring(Path *a, float cx, float cy, float r, PaperRng *rnd, float turns)
{
    float circ = 2.0f * PI_F * r;
    int n = (int)ceilf(circ * turns / 2.6f);
    if (n < 36) n = 36;
    float a0 = paper_rng_f(rnd) * 2.0f * PI_F;
    for (int i = 0; i <= n; i++) {
        float f = (float)i / n, ang = a0 + turns * 2.0f * PI_F * f;
        float rr = r + 1.1f * psin(f * 16.2f + a0) + 0.9f * f + (paper_rng_f(rnd) - 0.5f) * 0.5f;
        path_add(a, cx + rr * pcos(ang), cy + rr * psin(ang));
    }
}
static void hand_line(Path *a, float x0, float y0, float x1, float y1, float wave, PaperRng *rnd)
{
    path_add(a, x0, y0);
    path_quad(a, (x0 + x1) * 0.5f, (y0 + y1) * 0.5f + (paper_rng_f(rnd) - 0.5f) * wave * 2.0f, x1, y1);
}
static void hand_check(Path *a, float cx, float cy, float s, PaperRng *rnd)
{
#define JIT ((paper_rng_f(rnd) - 0.5f) * 1.2f)
    path_add(a, cx - 0.42f * s + JIT, cy + 0.02f * s + JIT);
    path_line(a, cx - 0.1f * s + JIT, cy + 0.34f * s + JIT);
    path_line(a, cx + 0.46f * s + JIT, cy - 0.36f * s + JIT);
#undef JIT
}
static void scribble_path(Path *a, float x, float y, float w, float h, int passes, PaperRng *rnd)
{
    float step = h / passes;
    for (int i = 0; i < passes; i++) {
        float yy = y + step * (i + 0.5f) + (paper_rng_f(rnd) - 0.5f) * step * 0.35f;
        float l = x - 2.0f - paper_rng_f(rnd) * 3.0f, r = x + w + 2.0f + paper_rng_f(rnd) * 3.0f, tilt = (paper_rng_f(rnd) - 0.5f) * 6.0f;
        float xa = (i & 1) ? r : l, xb = (i & 1) ? l : r;
        if (i == 0) path_add(a, xa, yy - tilt * 0.5f);
        else path_line(a, xa, yy - tilt * 0.5f);
        path_line(a, xb, yy + tilt * 0.5f);
    }
}
static void hand_frame(Path *a, float x, float y, float w, float h, float r, PaperRng *rnd)
{
#define J ((paper_rng_f(rnd) - 0.5f) * 1.6f)
    float X = x + w, Y = y + h;
    path_add(a, x + r + 5 + J, y + J);
    path_line(a, X - r + J, y + J);
    path_quad(a, X + J, y + J, X + J, y + r + J);
    path_line(a, X + J, Y - r + J);
    path_quad(a, X + J, Y + J, X - r + J, Y + J);
    path_line(a, x + r + J, Y + J);
    path_quad(a, x + J, Y + J, x + J, Y - r + J);
    path_line(a, x + J, y + r + J);
    path_quad(a, x + J, y + J, x + r + 12 + J, y + 1.5f + J);
#undef J
}

typedef struct { const Path *path; float width; uint32_t rgb; } StrokeDraw;
static void draw_polyline(Surf *s, void *ud)
{
    const StrokeDraw *d = ud;
    Col c = hex(d->rgb, 1.0f);
    for (int i = 1; i < d->path->n; i++) gfx_line(s, d->path->p[i - 1].x, d->path->p[i - 1].y, d->path->p[i].x, d->path->p[i].y, d->width, c);
    if (d->path->n == 1) gfx_circle(s, d->path->p[0].x, d->path->p[0].y, d->width * 0.5f, c);
}

typedef struct {
    Path path;
    float width, opacity, gain, bias;
    uint32_t rgb, seed;
} Stroke;

// stroke the paths one after another into a W x H sprite: each is drawn solid, bent a little by slow noise (the wax does not go where
// the hand meant), then eaten away by fine grain, then laid over the last
static PaperSpr *render_strokes(int W, int H, int ox, int oy, Stroke *st, int n)
{
    PaperSpr *res = paper_spr_new(W, H);
    if (!res) return NULL;
    res->ox = ox;
    res->oy = oy;
    for (int k = 0; k < n; k++) {
        Stroke *s = &st[k];
        for (int i = 0; i < s->path.n; i++) { // bend the path
            Pt *q = &s->path.p[i];
            float dx = (vnoise(q->x * 0.045f + 3.1f, q->y * 0.045f, s->seed) - 0.5f) * 2.2f * 2.0f;
            float dy = (vnoise(q->x * 0.045f, q->y * 0.045f + 7.7f, s->seed + 5) - 0.5f) * 2.2f * 2.0f;
            q->x += dx;
            q->y += dy;
        }
        StrokeDraw d = {&s->path, s->width, s->rgb};
        PaperSpr *layer = paper_bake(W, H, draw_polyline, &d);
        if (!layer) continue;
        for (int y = 0; y < H; y++)
            for (int x = 0; x < W; x++) {
                uint8_t *px = layer->px + ((size_t)y * W + x) * 4;
                if (!px[3]) continue;
                float R = 0.5f + 0.45f * ((hashf(x, y, s->seed + 11) - 0.5f) * 0.55f + (vnoise(x * 0.7f, y * 0.7f, s->seed + 13) - 0.5f) * 0.9f);
                float a = clampf(s->gain * R - s->bias, 0, 1) * s->opacity;
                px[3] = (uint8_t)clampf(px[3] * a + 0.5f, 0, 255);
            }
        paper_spr_over(res, layer, 0, 0);
        paper_spr_free(layer);
    }
    return res;
}

static void stroke_init(Stroke *s, float width, uint32_t rgb, float opacity, float gain, float bias, uint32_t seed)
{
    memset(s, 0, sizeof *s);
    s->width = width;
    s->rgb = rgb;
    s->opacity = opacity;
    s->gain = gain;
    s->bias = bias;
    s->seed = seed;
}

static PaperSpr *finish(Stroke *st, int n, int W, int H, int ox, int oy)
{
    PaperSpr *p = render_strokes(W, H, ox, oy, st, n);
    for (int i = 0; i < n; i++) free(st[i].path.p);
    return p;
}

PaperSpr *paper_ring(float radius, const PaperCrayon *c, uint32_t seed)
{
    if (!c) c = &PAPER_CRAYON_STD;
    int half = (int)ceilf(radius + 9.0f * (c->wk > 1 ? c->wk : 1)), W = 2 * half;
    Stroke st[2];
    PaperRng r;
    paper_rng_seed(&r, seed);
    stroke_init(&st[0], 3.4f * c->wk, c->c1, c->a1, 3.4f, 0.85f, seed * 7 + 1);
    hand_ring(&st[0].path, (float)half, (float)half, radius, &r, 1.07f);
    stroke_init(&st[1], 2.0f * c->wk, c->c2, c->a2, 3.8f, 1.45f, seed * 7 + 2);
    hand_ring(&st[1].path, half - 1.2f, half - 1.0f, radius + 1.3f, &r, 1.04f);
    return finish(st, 2, W, W, half, half);
}

PaperSpr *paper_tick(float size, const PaperCrayon *c, uint32_t seed)
{
    if (!c) c = &PAPER_CRAYON_STD;
    int half = (int)ceilf(size * 0.6f + 7.0f), W = 2 * half;
    Stroke st[2];
    PaperRng r;
    paper_rng_seed(&r, seed);
    stroke_init(&st[0], 3.8f * c->wk, c->c1, c->a1, 3.4f, 0.85f, seed * 7 + 3);
    hand_check(&st[0].path, (float)half, (float)half, size, &r);
    stroke_init(&st[1], 2.0f * c->wk, c->c2, c->a2, 3.8f, 1.45f, seed * 7 + 4);
    hand_check(&st[1].path, half + 0.8f, half + 0.6f, size, &r);
    return finish(st, 2, W, W, half, half);
}

PaperSpr *paper_underline(int len, const PaperCrayon *c, uint32_t seed)
{
    if (!c) c = &PAPER_CRAYON_STD;
    const int m = 9;
    int W = len + 2 * m, H = 36;
    Stroke st[2];
    PaperRng r;
    paper_rng_seed(&r, seed);
    stroke_init(&st[0], 4.2f * c->wk, c->c1, c->a1, 3.4f, 0.85f, seed * 7 + 5);
    hand_line(&st[0].path, (float)m, 14.0f, (float)(m + len), 12.0f, 6.0f, &r);
    float cut = minf(26.0f, len * 0.2f);
    stroke_init(&st[1], 2.4f * c->wk, c->c2, c->a2, 3.8f, 1.45f, seed * 7 + 6);
    hand_line(&st[1].path, m + minf(16.0f, len * 0.1f), 21.0f, m + len - cut, 20.0f, 5.0f, &r);
    return finish(st, 2, W, H, m, 14);
}

// two waves of crayon one under the other, the second thinner and paler and half a wave along: an underline that looks like water. len px long; the
// anchor is the first wave's left end
PaperSpr *paper_wave(int len, const PaperCrayon *c, uint32_t seed)
{
    if (!c) c = &PAPER_CRAYON_STD;
    const int m = 9;
    int W = len + 2 * m, H = 40;
    Stroke st[2];
    PaperRng r;
    paper_rng_seed(&r, seed);
    stroke_init(&st[0], 4.2f * c->wk, c->c1, c->a1, 3.4f, 0.85f, seed * 7 + 5);
    path_add(&st[0].path, (float)m, 14.0f);
    float x = (float)m;
    int up = 1;
    while (x < m + len - 1) {
        float xe = minf(x + 24.0f * (0.9f + 0.2f * paper_rng_f(&r)), (float)(m + len));
        path_quad(&st[0].path, (x + xe) * 0.5f, 14.0f + up * 8.0f * (0.85f + 0.3f * paper_rng_f(&r)), xe, 14.0f);
        x = xe;
        up = -up;
    }
    stroke_init(&st[1], 2.6f * c->wk, c->c2, c->a2, 3.8f, 1.45f, seed * 7 + 6);
    path_add(&st[1].path, (float)(m + 8), 26.0f);
    x = (float)(m + 8);
    up = -1;
    while (x < m + len - 9) {
        float xe = minf(x + 24.0f * (0.9f + 0.2f * paper_rng_f(&r)), (float)(m + len - 8));
        path_quad(&st[1].path, (x + xe) * 0.5f, 26.0f + up * 8.0f * (0.85f + 0.3f * paper_rng_f(&r)), xe, 26.0f);
        x = xe;
        up = -up;
    }
    return finish(st, 2, W, H, m, 14);
}

PaperSpr *paper_scribble(int size, uint32_t fill_rgb, const PaperCrayon *c, uint32_t seed)
{
    if (!c) c = &PAPER_CRAYON_STD;
    const int m = 16;
    int W = size + 2 * m;
    float k = size / 88.0f;
    Stroke st[2];
    PaperRng r;
    paper_rng_seed(&r, seed);
    int passes = (int)floorf(size / 9.8f + 0.5f);
    if (passes < 3) passes = 3;
    stroke_init(&st[0], 11.0f * k, fill_rgb, 1.0f, 4.6f, 1.1f, seed * 7 + 7);
    scribble_path(&st[0].path, (float)m, (float)m, (float)size, (float)size, passes, &r);
    stroke_init(&st[1], 3.0f * c->wk, c->c1, c->a1, 3.4f, 0.85f, seed * 7 + 8);
    hand_frame(&st[1].path, (float)m, (float)m, (float)size, (float)size, 12.0f * k, &r);
    return finish(st, 2, W, W, W / 2, W / 2);
}

PaperSpr *paper_disc_scribble(float r, uint32_t rgb, float opacity, uint32_t seed, uint8_t **order)
{
    if (order) *order = NULL;
    const int m = 6;
    int half = (int)ceilf(r) + m, W = 2 * half;
    PaperRng rnd;
    paper_rng_seed(&rnd, seed);
    Stroke st[2];
    // the first go: strokes back and forth across it, as wide as a crayon's tip on something this small, at the hand's slant
    float ang = paper_rng_f(&rnd) * PI_F, w = 2.6f;
    stroke_init(&st[0], w, rgb, opacity, 5.0f, 0.9f, seed * 7 + 31);
    int passes = (int)ceilf(2.0f * r / (w * 1.18f)); // (a hair apart: the strokes show)
    scribble_path(&st[0].path, -r, -r, 2.0f * r, 2.0f * r, passes, &rnd);
    // the second: fewer, thinner, across the first, to fill in some of what it missed
    float ang2 = ang + PI_F * (0.3f + 0.4f * paper_rng_f(&rnd));
    stroke_init(&st[1], w * 0.8f, rgb, opacity * 0.8f, 4.0f, 1.0f, seed * 7 + 32);
    scribble_path(&st[1].path, -r * 0.85f, -r * 0.85f, 1.7f * r, 1.7f * r, passes / 2 > 2 ? passes / 2 : 2, &rnd);
    for (int k = 0; k < 2; k++) { // (turned to their slant, about the middle)
        float a = k ? ang2 : ang, c = pcos(a), s = psin(a);
        for (int i = 0; i < st[k].path.n; i++) {
            Pt *q = &st[k].path.p[i];
            float x = q->x, y = q->y;
            q->x = half + x * c - y * s;
            q->y = half + x * s + y * c;
        }
    }
    PaperSpr *res = render_strokes(W, W, half, half, st, 2);
    if (res && order && (*order = malloc((size_t)W * W))) { // when the crayon gets to each pixel: the first go along its way, then the crossing one
        int n0 = st[0].path.n, nall = n0 + st[1].path.n;
        for (int y = 0; y < W; y++)
            for (int x = 0; x < W; x++) {
                int best = nall;
                for (int k = 0; k < 2 && best == nall; k++) {
                    float reach = st[k].width * 0.5f + 2.6f;
                    for (int i = 0; i < st[k].path.n; i++) {
                        float dx = st[k].path.p[i].x - (x + 0.5f), dy = st[k].path.p[i].y - (y + 0.5f);
                        if (dx * dx + dy * dy <= reach * reach) {
                            best = (k ? n0 : 0) + i;
                            break;
                        }
                    }
                }
                (*order)[(size_t)y * W + x] = (uint8_t)(nall > 1 ? best * 255 / nall : 0);
            }
    }
    for (int k = 0; k < 2; k++) free(st[k].path.p);
    if (!res) return NULL;
    for (int y = 0; y < W; y++) // cut to the circle: a ragged edge, here a little short of the line, there a little over it
        for (int x = 0; x < W; x++) {
            uint8_t *q = res->px + ((size_t)y * W + x) * 4;
            if (!q[3]) continue;
            float dx = x + 0.5f - half, dy = y + 0.5f - half, d = sqrtf(dx * dx + dy * dy) - r + (vnoise(x * 0.4f, y * 0.4f, seed + 9u) - 0.5f) * 2.2f;
            q[3] = (uint8_t)(q[3] * clampf(0.5f - d, 0.0f, 1.0f) + 0.5f);
        }
    return res;
}

static uint32_t deepen(uint32_t rgb, float k)
{
    uint32_t r = (uint32_t)(((rgb >> 16) & 255) * k), g = (uint32_t)(((rgb >> 8) & 255) * k), b = (uint32_t)((rgb & 255) * k);
    return r << 16 | g << 8 | b;
}

// the corners of a five-pointed star (outer radius R, inner r), a point up, turned by rot (radians)
static void star_pts(Pt v[10], float cx, float cy, float R, float r, float rot)
{
    for (int i = 0; i < 10; i++) {
        float a = rot - PI_F * 0.5f + (float)i * PI_F / 5.0f, rr = (i & 1) ? r : R;
        v[i].x = cx + rr * pcos(a);
        v[i].y = cy + rr * psin(a);
    }
}

// signed distance from (x, y) to the edge of the closed polygon v[0..n-1] (negative inside)
static float poly_sd(const Pt *v, int n, float x, float y)
{
    float best = 1e9f;
    int in = 0;
    for (int i = 0; i < n; i++) {
        Pt a = v[i], b = v[(i + 1) % n];
        if ((a.y > y) != (b.y > y) && x < (b.x - a.x) * (y - a.y) / (b.y - a.y) + a.x) in = !in;
        float dx = b.x - a.x, dy = b.y - a.y, l2 = dx * dx + dy * dy, t = l2 > 1e-9f ? clampf(((x - a.x) * dx + (y - a.y) * dy) / l2, 0.0f, 1.0f) : 0.0f;
        float ex = x - (a.x + t * dx), ey = y - (a.y + t * dy), d = sqrtf(ex * ex + ey * ey);
        if (d < best) best = d;
    }
    return in ? -best : best;
}

// signed distance from (x, y) to the star's edge (negative inside)
static float star_sd(const Pt v[10], float x, float y) { return poly_sd(v, 10, x, y); }

PaperSpr *paper_star(int size, uint32_t rgb, uint32_t seed)
{
    const int m = 10;
    int W = size + 2 * m;
    float R = size * 0.5f, r = R * 0.47f, cx = W * 0.5f, cy = W * 0.5f + R * 0.05f;
    PaperRng rnd;
    paper_rng_seed(&rnd, seed);
    float rot = paper_rng_range(&rnd, -0.35f, 0.35f);
    Pt v[10];
    star_pts(v, cx, cy, R, r, rot);
    // the colouring: back-and-forth strokes over the star's box, cut to the star with a ragged edge
    Stroke fill;
    stroke_init(&fill, size / 8.5f, rgb, 0.8f, 4.2f, 1.0f, seed * 7 + 21);
    scribble_path(&fill.path, cx - R, cy - R, 2 * R, 2 * R, (int)floorf(size / 7.5f + 0.5f), &rnd);
    PaperSpr *res = finish(&fill, 1, W, W, W / 2, W / 2);
    if (!res) return NULL;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *q = res->px + ((size_t)y * W + x) * 4;
            if (!q[3]) continue;
            float sd = star_sd(v, x + 0.5f, y + 0.5f) + (vnoise(x * 0.35f, y * 0.35f, seed + 3u) - 0.5f) * 1.8f;
            q[3] = (uint8_t)(q[3] * clampf(0.5f - sd, 0.0f, 1.0f) + 0.5f);
        }
    // the outline, drawn by hand: two passes, the second thinner and a little off
    Stroke line[2];
    stroke_init(&line[0], 3.0f, deepen(rgb, 0.80f), 0.75f, 3.4f, 0.85f, seed * 7 + 22);
    stroke_init(&line[1], 1.8f, deepen(rgb, 0.70f), 0.45f, 3.8f, 1.45f, seed * 7 + 23);
    for (int p = 0; p < 2; p++) {
        Pt w[10];
        star_pts(w, cx + p * 1.0f, cy + p * 0.8f, R + p * 0.8f, r + p * 0.4f, rot + p * 0.02f);
        for (int i = 0; i <= 10; i++) {
            float jx = (paper_rng_f(&rnd) - 0.5f) * 2.0f, jy = (paper_rng_f(&rnd) - 0.5f) * 2.0f;
            if (i == 0) path_add(&line[p].path, w[0].x + jx, w[0].y + jy);
            else path_line(&line[p].path, w[i % 10].x + jx, w[i % 10].y + jy);
        }
    }
    PaperSpr *edge = finish(line, 2, W, W, W / 2, W / 2);
    if (edge) paper_spr_over(res, edge, res->ox, res->oy); // (laid on by its anchor: both are anchored at the middle)
    paper_spr_free(edge);
    return res;
}

// ---- the banner's other doodles: a heart, a flower, a cloud, a moon, a sparkle, a starburst, a scalloped badge. The same crayon colouring and hand-drawn
// outline as the star; only the outline differs. Each builds its closed outline in a box of 2R round (cx, cy), turned by rot.
enum { SHAPE_MAXPTS = 128 };

static int shape_star(Pt *v, int tips, float cx, float cy, float R, float r, float rot)
{
    for (int i = 0; i < tips * 2; i++) {
        float a = rot - PI_F * 0.5f + (float)i * PI_F / (float)tips, rr = (i & 1) ? r : R;
        v[i].x = cx + rr * pcos(a);
        v[i].y = cy + rr * psin(a);
    }
    return tips * 2;
}

// puts the n points into the box of 2R about (cx, cy) (their own bounding box's middle goes to the centre; the wider side fills the box), turned by rot
static void shape_fit(Pt *v, int n, float cx, float cy, float R, float rot)
{
    float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        if (v[i].x < x0) x0 = v[i].x;
        if (v[i].x > x1) x1 = v[i].x;
        if (v[i].y < y0) y0 = v[i].y;
        if (v[i].y > y1) y1 = v[i].y;
    }
    float k = 2.0f * R / maxf(x1 - x0, y1 - y0), mx = (x0 + x1) * 0.5f, my = (y0 + y1) * 0.5f, c = pcos(rot), s = psin(rot);
    for (int i = 0; i < n; i++) {
        float x = (v[i].x - mx) * k, y = (v[i].y - my) * k;
        v[i].x = cx + x * c - y * s;
        v[i].y = cy + x * s + y * c;
    }
}

static int shape_poly(Pt *v, int kind, float cx, float cy, float R, float rot)
{
    const int N = 96;
    int n = 0;
    switch (kind) {
    case PAPER_SHAPE_HEART:
        n = 72;
        for (int i = 0; i < n; i++) {
            float t = 2.0f * PI_F * (float)i / (float)n, s = psin(t);
            v[i].x = 16.0f * s * s * s;
            v[i].y = -(13.0f * pcos(t) - 5.0f * pcos(2.0f * t) - 2.0f * pcos(3.0f * t) - pcos(4.0f * t));
        }
        shape_fit(v, n, cx, cy, R, rot);
        return n;
    case PAPER_SHAPE_FLOWER: // five round petals
        n = N;
        for (int i = 0; i < n; i++) {
            float a = 2.0f * PI_F * (float)i / (float)n, rr = 0.52f + 0.48f * sqrtf(fabsf(pcos(2.5f * a)));
            v[i].x = rr * pcos(a);
            v[i].y = rr * psin(a);
        }
        shape_fit(v, n, cx, cy, R, rot);
        return n;
    case PAPER_SHAPE_CLOUD: { // the outline of four overlapping circles, flat underneath
        static const float C[4][3] = {{-0.46f, 0.12f, 0.40f}, {-0.12f, -0.20f, 0.50f}, {0.36f, -0.06f, 0.44f}, {0.05f, 0.18f, 0.46f}};
        n = N;
        for (int i = 0; i < n; i++) {
            float a = 2.0f * PI_F * (float)i / (float)n, dx = pcos(a), dy = psin(a), best = 0.0f;
            for (int k = 0; k < 4; k++) {
                float b = C[k][0] * dx + C[k][1] * dy, c = C[k][0] * C[k][0] + C[k][1] * C[k][1] - C[k][2] * C[k][2], disc = b * b - c;
                if (disc >= 0.0f && b + sqrtf(disc) > best) best = b + sqrtf(disc);
            }
            v[i].x = dx * best;
            v[i].y = dy * best > 0.36f ? 0.36f : dy * best;
        }
        shape_fit(v, n, cx, cy, R, rot);
        return n;
    }
    case PAPER_SHAPE_MOON: { // a crescent opening to the right
        const float rc = 0.80f, ox = 0.46f, oy = -0.12f;
        float d = sqrtf(ox * ox + oy * oy), base = patan2(oy, ox);
        float ca = (1.0f + d * d - rc * rc) / (2.0f * d), a0 = patan2(sqrtf(maxf(0.0f, 1.0f - ca * ca)), ca);
        float p1a = base + a0, p2a = base + 2.0f * PI_F - a0;
        int n1 = 56, n2 = 40;
        for (int i = 0; i < n1; i++) {
            float a = p1a + (p2a - p1a) * (float)i / (float)(n1 - 1);
            v[n++] = (Pt){pcos(a), psin(a)};
        }
        // back along the cutting circle, through the side that faces the middle
        float psi0 = patan2(-oy, -ox), q1 = patan2(psin(p1a) - oy, pcos(p1a) - ox), q2 = patan2(psin(p2a) - oy, pcos(p2a) - ox);
        float d2 = q2 - psi0, d1 = q1 - psi0;
        while (d2 > PI_F) d2 -= 2.0f * PI_F;
        while (d2 < -PI_F) d2 += 2.0f * PI_F;
        while (d1 > PI_F) d1 -= 2.0f * PI_F;
        while (d1 < -PI_F) d1 += 2.0f * PI_F;
        for (int i = 1; i < n2 - 1; i++) {
            float a = psi0 + d2 + (d1 - d2) * (float)i / (float)(n2 - 1);
            v[n++] = (Pt){ox + rc * pcos(a), oy + rc * psin(a)};
        }
        shape_fit(v, n, cx, cy, R, rot);
        return n;
    }
    case PAPER_SHAPE_SPARKLE: // four long points
        return shape_star(v, 4, cx, cy, R, R * 0.26f, rot);
    case PAPER_SHAPE_BURST: // ten short ones
        return shape_star(v, 10, cx, cy, R, R * 0.70f, rot);
    case PAPER_SHAPE_BADGE: // a scalloped circle
        n = N;
        for (int i = 0; i < n; i++) {
            float a = 2.0f * PI_F * (float)i / (float)n, rr = 0.88f + 0.12f * pcos(8.0f * a);
            v[i].x = cx + R * rr * pcos(a + rot);
            v[i].y = cy + R * rr * psin(a + rot);
        }
        return n;
    default: break;
    }
    return shape_star(v, 5, cx, cy, R, R * 0.47f, rot);
}

PaperSpr *paper_shape(int kind, int size, uint32_t rgb, uint32_t seed)
{
    if (kind == PAPER_SHAPE_STAR) return paper_star(size, rgb, seed);
    const int m = 10;
    int W = size + 2 * m;
    float R = size * 0.5f, cx = W * 0.5f, cy = W * 0.5f;
    PaperRng rnd;
    paper_rng_seed(&rnd, seed);
    float rot = paper_rng_range(&rnd, -0.3f, 0.3f);
    Pt v[SHAPE_MAXPTS];
    int n = shape_poly(v, kind, cx, cy, R, rot);
    Stroke fill;
    stroke_init(&fill, size / 8.5f, rgb, 0.8f, 4.2f, 1.0f, seed * 7 + 21);
    scribble_path(&fill.path, cx - R, cy - R, 2 * R, 2 * R, (int)floorf(size / 7.5f + 0.5f), &rnd);
    PaperSpr *res = finish(&fill, 1, W, W, W / 2, W / 2);
    if (!res) return NULL;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            uint8_t *q = res->px + ((size_t)y * W + x) * 4;
            if (!q[3]) continue;
            float sd = poly_sd(v, n, x + 0.5f, y + 0.5f) + (vnoise(x * 0.35f, y * 0.35f, seed + 3u) - 0.5f) * 1.8f;
            q[3] = (uint8_t)(q[3] * clampf(0.5f - sd, 0.0f, 1.0f) + 0.5f);
        }
    float jit = n > 24 ? 0.6f : 2.0f; // (a smooth outline of many points is only wobbled a hair; a star's few corners wander)
    Stroke line[2];
    stroke_init(&line[0], 3.0f, deepen(rgb, 0.80f), 0.75f, 3.4f, 0.85f, seed * 7 + 22);
    stroke_init(&line[1], 1.8f, deepen(rgb, 0.70f), 0.45f, 3.8f, 1.45f, seed * 7 + 23);
    for (int p = 0; p < 2; p++) {
        float k = 1.0f + (float)p * 0.8f / R;
        for (int i = 0; i <= n; i++) {
            Pt q = v[i % n];
            float jx = (paper_rng_f(&rnd) - 0.5f) * jit, jy = (paper_rng_f(&rnd) - 0.5f) * jit;
            float px = cx + (q.x - cx) * k + p * 1.0f + jx, py = cy + (q.y - cy) * k + p * 0.8f + jy;
            if (i == 0) path_add(&line[p].path, px, py);
            else path_line(&line[p].path, px, py);
        }
    }
    PaperSpr *edge = finish(line, 2, W, W, W / 2, W / 2);
    if (edge) paper_spr_over(res, edge, res->ox, res->oy);
    paper_spr_free(edge);
    return res;
}

// The logo's seal: a disc of ink pressed by hand, the paper showing through where the impression has a thin ring, a ring of dots and the person cut out
// of it; the rim a little darker where the ink pooled, the edge wavering, pinholes where the paper's tooth refused it. R px, anchored at its middle.
PaperSpr *paper_seal(float R, uint32_t rgb, uint32_t seed)
{
    int W = (int)ceilf(2.0f * R) + 6;
    PaperSpr *p = paper_spr_new(W, W);
    if (!p) return NULL;
    p->ox = p->oy = W / 2;
    float cx = W * 0.5f, cy = W * 0.5f;
    float ringw = maxf(1.1f, R * 0.034f), dotr = maxf(0.75f, R * 0.02f), dotR = R * 0.925f;
    int ndots = (int)(R * 0.62f);
    if (ndots < 16) ndots = 16;
    const float r0 = ((rgb >> 16) & 255) / 255.0f, g0 = ((rgb >> 8) & 255) / 255.0f, b0 = (rgb & 255) / 255.0f;
    for (int y = 0; y < W; y++)
        for (int x = 0; x < W; x++) {
            float cov = 0.0f;
            for (int sy = 0; sy < 3; sy++)
                for (int sx = 0; sx < 3; sx++) {
                    float fx = x + (sx + 0.5f) / 3.0f, fy = y + (sy + 0.5f) / 3.0f;
                    fx += (vnoise(fx * 0.07f, fy * 0.07f, seed) - 0.5f) * 2.6f; // the edge wavers
                    fy += (vnoise(fx * 0.07f + 5.0f, fy * 0.07f, seed + 1u) - 0.5f) * 2.6f;
                    float dx = fx - cx, dy = fy - cy, d = sqrtf(dx * dx + dy * dy);
                    if (d > R) continue;
                    if (fabsf(d - R * 0.86f) < ringw * 0.5f) continue; // the thin ring
                    int cut = 0;
                    // the dots: the nearest of ndots round the rim
                    float ang = patan2(dy, dx), step = 2.0f * PI_F / (float)ndots, k = floorf((ang + PI_F) / step + 0.5f), da = (ang + PI_F) - k * step;
                    float tx = d * pcos(da) - dotR, ty = d * psin(da);
                    if (tx * tx + ty * ty < dotr * dotr) cut = 1;
                    // the person: a head and shoulders, only inside the ring
                    if (d < R * 0.9f) {
                        float hx = dx, hy = dy + R * 0.14f;
                        if (hx * hx + hy * hy < R * 0.27f * R * 0.27f) cut = 1;
                        float bx = dx / (R * 0.52f), by = (dy - R * 0.6f) / (R * 0.4f);
                        if (bx * bx + by * by < 1.0f) cut = 1;
                    }
                    if (!cut) cov += 1.0f / 9.0f;
                }
            if (cov <= 0.0f) continue;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            float fine = 0.55f * hashf(x, y, seed + 41u) + 0.45f * vnoise(x / 1.7f, y / 1.7f, seed + 43u);
            float hole = 1.0f - smooth01(clampf((fine - 0.10f) / 0.14f, 0.0f, 1.0f)); // a pinhole
            float slow = vnoise(x / 9.0f, y / 9.0f, seed + 47u);
            float a = cov * 0.95f * (1.0f - 0.7f * hole) * (1.0f - 0.18f * smooth01(clampf((slow - 0.5f) / 0.3f, 0.0f, 1.0f)));
            float pool = smooth01(clampf((d - R * 0.88f) / (R * 0.12f), 0.0f, 1.0f)) * 0.35f; // darker where the ink pooled at the rim
            uint8_t *o = p->px + ((size_t)y * W + x) * 4;
            o[0] = (uint8_t)(r0 * (1.0f - pool * 0.45f) * 255.0f + 0.5f);
            o[1] = (uint8_t)(g0 * (1.0f - pool * 0.45f) * 255.0f + 0.5f);
            o[2] = (uint8_t)(b0 * (1.0f - pool * 0.30f) * 255.0f + 0.5f);
            o[3] = (uint8_t)(clampf(a, 0.0f, 1.0f) * 255.0f + 0.5f);
        }
    return p;
}

PaperSpr *paper_hatch(int w, int h, uint32_t rgb, float opacity, uint32_t seed)
{
    const int m = 8;
    PaperRng rnd;
    paper_rng_seed(&rnd, seed);
    Stroke st;
    stroke_init(&st, h / 2.6f, rgb, opacity, 4.4f, 1.05f, seed * 7 + 31);
    scribble_path(&st.path, (float)m, (float)m, (float)w, (float)h, (int)floorf(h / 6.0f + 0.5f), &rnd);
    return finish(&st, 1, w + 2 * m, h + 2 * m, m, m + h / 2);
}

// The ink frame of a rubber stamp: a heavy rounded frame with a thin one inside it (the mockup's stamp). Clean: straight, even, with the crisp edge
// that inked rubber has, because a stamp is machined and a hand-drawn one reads as a mess. paper_stamp_weather ages it. w x h px, anchored at its middle,
// in the ink ik.
PaperSpr *paper_stamp_frame(int w, int h, uint32_t seed, uint32_t ik)
{
    (void)seed;
    PaperSpr *p = paper_spr_new(w, h);
    if (!p) return NULL;
    p->ox = w / 2;
    p->oy = h / 2;
    const float cx = w * 0.5f, cy = h * 0.5f;
    const uint8_t r = (ik >> 16) & 255, g = (ik >> 8) & 255, b = ik & 255;
    // the big stamp (58 px high) has the mockup's 4 px and 1.3 px frames; a smaller one has finer frames, so its letters still have room
    const float k = clampf(h / 58.0f, 0.55f, 1.0f), heavy = 4.0f * k, thin = 1.3f * k < 1.0f ? 1.0f : 1.3f * k, gap = 6.5f * k;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float outer = fabsf(sd_rrect(px, py, cx, cy, w * 0.5f - heavy * 0.5f, h * 0.5f - heavy * 0.5f, 4.0f * k)) - heavy * 0.5f; // the heavy frame
            float inner = fabsf(sd_rrect(px, py, cx, cy, w * 0.5f - gap, h * 0.5f - gap, 2.0f * k)) - thin * 0.5f;                    // the thin one inside it
            float a = clampf(0.5f - (outer < inner ? outer : inner), 0, 1) * 0.92f;
            if (a <= 0.0f) continue;
            uint8_t *o = p->px + ((size_t)y * w + x) * 4;
            o[0] = r;
            o[1] = g;
            o[2] = b;
            o[3] = (uint8_t)(a * 255.0f + 0.5f);
        }
    return p;
}

// An old stamp's impression: the ink took a little less in patches (uneven pressure, a worn pad) and the hardened rubber leaves fine pinholes. The
// shapes themselves stay clean. strength 0..1 (a small stamp takes less: each pinhole is a bigger part of it). Same seed, same wear.
void paper_stamp_weather(PaperSpr *p, uint32_t seed, float strength)
{
    if (!p) return;
    for (int y = 0; y < p->h; y++)
        for (int x = 0; x < p->w; x++) {
            uint8_t *q = p->px + ((size_t)y * p->w + x) * 4;
            if (!q[3]) continue;
            float slow = 0.62f * vnoise(x / 17.0f, y / 17.0f, seed + 31u) + 0.38f * vnoise(x / 6.5f + 9.1f, y / 6.5f, seed + 37u);
            float patch = smooth01(clampf((slow - 0.46f) / 0.34f, 0.0f, 1.0f)); // 1 where the ink took least
            float fine = 0.55f * hashf(x, y, seed + 41u) + 0.45f * vnoise(x / 1.7f, y / 1.7f, seed + 43u);
            float hole = 1.0f - smooth01(clampf((fine - 0.12f) / 0.14f, 0.0f, 1.0f)); // 1 in a pinhole
            float a = (1.0f - 0.34f * strength * patch) * (1.0f - strength * hole);
            q[3] = (uint8_t)(q[3] * a + 0.5f);
        }
}

// ------------------------------------------------------------------------------------------------ lift maths
const PaperLiftK paper_lift_k[LIFT_KINDS] = {{9.0f, 0.06f, 0.0f}, {6.0f, 0.012f, 0.0f}, {5.0f, 0.04f, 0.0f}, {0.0f, 0.0f, 12.0f}, {5.0f, 0.04f, 0.0f}};

void paper_lift_init(PaperLift *l, int kind)
{
    memset(l, 0, sizeof *l);
    l->kind = kind;
    l->t0 = -1e12;
    l->wig_t0 = -1;
    l->bump_t0 = -1;
}

float paper_lift_value(const PaperLift *l, double now)
{
    float to = l->target ? 1.0f : 0.0f;
    if (l->dur <= 0) return to;
    double t = (now - l->t0) / l->dur;
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    double e = l->target ? 1.0 - (1.0 - t) * (1.0 - t) * (1.0 - t) : t * t * (3.0 - 2.0 * t); // ease-out cubic up, smoothstep down
    return l->from + (to - l->from) * (float)e;
}

// a wiggle that has run its course has left the card resting at -0.4 degrees
static void lift_settle(PaperLift *l, double now)
{
    if (l->wig_t0 >= 0 && now - l->wig_t0 >= PAPER_WIGGLE_MS) {
        l->wig_rest = -0.4f;
        l->wig_t0 = -1;
    }
}

void (*paper_lift_hook)(int grab, int held, int kind);

void paper_lift_grab(PaperLift *l, double now, int held)
{
    if (paper_lift_hook) paper_lift_hook(1, held, l->kind | (l->tapeless == 2 ? LIFT_PINNED : l->tapeless ? LIFT_TAPELESS : 0));
    lift_settle(l, now);
    l->from = paper_lift_value(l, now);
    l->target = 1;
    l->t0 = now;
    l->dur = held ? PAPER_LIFT_UP_HELD_MS : PAPER_LIFT_UP_MS;
    if (!held && l->kind != LIFT_TAB) {
        l->wig_t0 = now;
    } else {
        l->wig_t0 = -1;
        if (held) l->wig_rest = 0.0f; // a held D-pad lifts quietly and level
    }
}

void paper_lift_release(PaperLift *l, double now, int held)
{
    if (paper_lift_hook) paper_lift_hook(0, held, l->kind | (l->tapeless == 2 ? LIFT_PINNED : l->tapeless ? LIFT_TAPELESS : 0));
    lift_settle(l, now);
    l->from = paper_lift_value(l, now);
    l->target = 0;
    l->t0 = now;
    l->dur = held ? PAPER_LIFT_DOWN_HELD_MS : PAPER_LIFT_DOWN_MS;
    l->wig_t0 = -1;
}

void paper_lift_bump(PaperLift *l, double now) { l->bump_t0 = now; }

void paper_lift_wiggle_again(PaperLift *l, double now)
{
    if (l->target && l->kind != LIFT_TAB) l->wig_t0 = now; // (an item that is not up has nothing to wiggle)
}

float paper_wiggle_at(double ms)
{
    static const float T[5] = {0, 42, 85, 128, 170}, V[5] = {-3.4f, 3.0f, -1.8f, 1.0f, -0.4f};
    if (ms >= 170) return -0.4f;
    if (ms < 0) ms = 0;
    for (int i = 0; i < 4; i++)
        if (ms < T[i + 1]) return V[i] + (V[i + 1] - V[i]) * (float)((ms - T[i]) / (T[i + 1] - T[i]));
    return -0.4f;
}

float paper_wiggle_k(float width) { return width <= 165.0f ? 1.0f : 165.0f / width; }

float paper_lift_wiggle(const PaperLift *l, double now, float width)
{
    float wk = paper_wiggle_k(width), p = paper_lift_value(l, now);
    if (l->wig_t0 >= 0) {
        double t = now - l->wig_t0;
        return t < PAPER_WIGGLE_MS ? paper_wiggle_at(t) * wk : -0.4f * p * wk;
    }
    return l->wig_rest * p * wk;
}

int paper_lift_busy(const PaperLift *l, double now)
{
    if (now < l->t0 + l->dur) return 1;
    if (l->wig_t0 >= 0 && now - l->wig_t0 < PAPER_WIGGLE_MS) return 1;
    if (l->bump_t0 >= 0 && now - l->bump_t0 < PAPER_BUMP_MS) return 1;
    if (!l->target && l->kind != LIFT_TAB && now >= l->t0 + l->dur && now < l->t0 + l->dur + PAPER_PRESS_MS) return 1;
    return 0;
}

void paper_lift_pose(const PaperLift *l, double now, float width, PaperPose *o)
{
    const PaperLiftK *k = &paper_lift_k[l->kind];
    float p = paper_lift_value(l, now), bx = 0, press = 0;
    if (l->bump_t0 >= 0) {
        double bt = now - l->bump_t0;
        if (bt >= 0 && bt < PAPER_BUMP_MS) bx = psin((float)(bt / PAPER_BUMP_MS) * PI_F * 3.0f) * 5.0f * (1.0f - (float)(bt / PAPER_BUMP_MS));
    }
    if (!l->target && l->kind != LIFT_TAB && now >= l->t0 + l->dur && now < l->t0 + l->dur + PAPER_PRESS_MS) press = -0.008f;
    o->p = p;
    o->x = bx + k->dx * p;
    o->y = -k->rise * p;
    o->scale = 1.0f + k->scale * p + press;
    o->angle = paper_lift_wiggle(l, now, width) * (PI_F / 180.0f);
    o->ghost = minf(1.0f, p * 1.6f);
}

PaperXf paper_xf_attach(PaperXf c, float dx, float dy)
{
    float ca = pcos(c.angle), sa = psin(c.angle);
    PaperXf o = {c.x + c.scale * (dx * ca - dy * sa), c.y + c.scale * (dx * sa + dy * ca), c.scale, c.angle};
    return o;
}

float paper_tape_hand(PaperTape *t, float *x, float *y, uint32_t h, float spread)
{
    PaperRng r;
    paper_rng_seed(&r, h ^ 0x7A9E5u);
    float v[12];
    for (int i = 0; i < 12; i++) v[i] = paper_rng_f(&r); // (drawn in a fixed order, then used)
    t->len = (int)(t->len * (1.0f + (v[0] * 2.0f - 1.0f) * 0.22f * spread) + 0.5f);
    t->wid += (int)floorf((v[1] * 2.0f - 1.0f) * 2.0f * spread + 0.5f);
    float turn = (v[2] * 2.0f - 1.0f) * 7.0f * spread;
    if (v[3] < 0.2f) turn += (v[4] < 0.5f ? -1.0f : 1.0f) * 5.0f * spread; // now and then one that is plainly crooked
    t->tilt += turn;
    *x += (v[5] * 2.0f - 1.0f) * 9.0f * spread;
    *y += (v[6] * 2.0f - 1.0f) * 4.5f * spread;
    // the ends: each cut with scissors, torn along the teeth of the cutter, or left as it came off the roll
    t->flags &= ~(unsigned)(PAPER_TAPE_ZIG | PAPER_TAPE_SLANT_L | PAPER_TAPE_SLANT_R | PAPER_TAPE_CRIMP);
    t->flags |= v[7] < 0.4f ? PAPER_TAPE_ZIG_L : v[7] < 0.75f ? PAPER_TAPE_SLANT_L : 0;
    t->flags |= v[8] < 0.4f ? PAPER_TAPE_ZIG_R : v[8] < 0.75f ? PAPER_TAPE_SLANT_R : 0;
    if (v[9] < 0.3f && t->len >= 40) t->flags |= PAPER_TAPE_CRIMP; // pressed down in the middle, the far half a little out of line
    t->seed = t->seed * 31u + (h & 0xFFFFu);
    return v[10] < 0.22f ? 0.2f : 0.0f; // the end that did not take stands up a little: the peel's first step (the lift 0.2 of six states)
}
