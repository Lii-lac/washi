#include "gfx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "stb_truetype.h"

static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline float smoothf(float a, float b, float x)
{
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}
static inline int imin(int a, int b) { return a < b ? a : b; }
static inline int imax(int a, int b) { return a > b ? a : b; }

// ------------------------------------------------------------------------------------------------ surfaces

void (*gfx_pump_hook)(void) = NULL;

Surf *surf_new(int w, int h)
{
    gfx_pump();
    Surf *s = malloc(sizeof *s);
    s->w = w;
    s->h = h;
    s->px = calloc((size_t)w * h, 4);
    return s;
}

Surf *surf_clone(const Surf *src)
{
    Surf *s = surf_new(src->w, src->h);
    memcpy(s->px, src->px, (size_t)src->w * src->h * 4);
    return s;
}

void surf_copy(Surf *dst, const Surf *src)
{
    memcpy(dst->px, src->px, (size_t)src->w * src->h * 4);
}

void surf_free(Surf *s)
{
    if (!s) return;
    free(s->px);
    free(s);
}

static inline void blend(Surf *s, int x, int y, float r, float g, float b, float a)
{
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h || a <= 0.0f) return;
    uint8_t *p = s->px + ((size_t)y * s->w + x) * 4;
    float ia = 1.0f - a;
    p[0] = (uint8_t)(p[0] * ia + r * 255.0f * a + 0.5f);
    p[1] = (uint8_t)(p[1] * ia + g * 255.0f * a + 0.5f);
    p[2] = (uint8_t)(p[2] * ia + b * 255.0f * a + 0.5f);
    p[3] = 255;
}

// ------------------------------------------------------------------------------------------------ blur

static void box_pass(const float *src, float *dst, int w, int h, int r, int horizontal)
{
    int n = horizontal ? w : h;
    int lines = horizontal ? h : w;
    float inv = 1.0f / (float)(2 * r + 1);
    for (int l = 0; l < lines; l++) {
        for (int c = 0; c < 3; c++) {
            const float *base = src + c;
            float *out = dst + c;
            size_t step = horizontal ? 3 : (size_t)w * 3;
            size_t off = horizontal ? (size_t)l * w * 3 : (size_t)l * 3;
            float sum = 0;
            for (int i = -r; i <= r; i++) sum += base[off + (size_t)imin(imax(i, 0), n - 1) * step];
            for (int i = 0; i < n; i++) {
                out[off + (size_t)i * step] = sum * inv;
                sum += base[off + (size_t)imin(i + r + 1, n - 1) * step];
                sum -= base[off + (size_t)imax(i - r, 0) * step];
            }
        }
    }
}

Blur *blur_make(const Surf *src, float radius_px)
{
    Blur *b = malloc(sizeof *b);
    b->scale = 4;
    b->w = (src->w + b->scale - 1) / b->scale;
    b->h = (src->h + b->scale - 1) / b->scale;
    size_t n = (size_t)b->w * b->h * 3;
    float *a = malloc(n * sizeof(float));
    float *t = malloc(n * sizeof(float));
    for (int y = 0; y < b->h; y++) {
        for (int x = 0; x < b->w; x++) {
            float acc[3] = {0, 0, 0};
            int cnt = 0;
            for (int dy = 0; dy < b->scale; dy++) {
                for (int dx = 0; dx < b->scale; dx++) {
                    int sx = x * b->scale + dx, sy = y * b->scale + dy;
                    if (sx >= src->w || sy >= src->h) continue;
                    const uint8_t *p = src->px + ((size_t)sy * src->w + sx) * 4;
                    acc[0] += p[0];
                    acc[1] += p[1];
                    acc[2] += p[2];
                    cnt++;
                }
            }
            float *o = a + ((size_t)y * b->w + x) * 3;
            for (int c = 0; c < 3; c++) o[c] = acc[c] / (cnt ? cnt : 1) / 255.0f;
        }
    }
    float sigma = radius_px / b->scale * 0.5f;
    int r = (int)((sqrtf(4.0f * sigma * sigma + 1.0f) - 1.0f) / 2.0f + 0.5f);
    if (r < 1) r = 1;
    for (int pass = 0; pass < 3; pass++) {
        box_pass(a, t, b->w, b->h, r, 1);
        box_pass(t, a, b->w, b->h, r, 0);
    }
    free(t);
    b->rgb = a;
    return b;
}

void blur_free(Blur *b)
{
    if (!b) return;
    free(b->rgb);
    free(b);
}

static inline void blur_sample(const Blur *b, float x, float y, float o[3])
{
    float u = x / b->scale - 0.5f, v = y / b->scale - 0.5f;
    int x0 = (int)floorf(u), y0 = (int)floorf(v);
    float fx = u - x0, fy = v - y0;
    int x1 = x0 + 1, y1 = y0 + 1;
    x0 = imin(imax(x0, 0), b->w - 1);
    x1 = imin(imax(x1, 0), b->w - 1);
    y0 = imin(imax(y0, 0), b->h - 1);
    y1 = imin(imax(y1, 0), b->h - 1);
    const float *p00 = b->rgb + ((size_t)y0 * b->w + x0) * 3, *p10 = b->rgb + ((size_t)y0 * b->w + x1) * 3;
    const float *p01 = b->rgb + ((size_t)y1 * b->w + x0) * 3, *p11 = b->rgb + ((size_t)y1 * b->w + x1) * 3;
    for (int c = 0; c < 3; c++) {
        float top = p00[c] + (p10[c] - p00[c]) * fx;
        float bot = p01[c] + (p11[c] - p01[c]) * fx;
        o[c] = top + (bot - top) * fy;
    }
}

// ------------------------------------------------------------------------------------------------ distance fields

typedef struct { float cx, cy, hx, hy, r; } RR;

// signed distance to a rounded rectangle (negative inside); optional outward normal
static inline float sd_rr(const RR *q, float px, float py, float *nx, float *ny)
{
    float dx = px - q->cx, dy = py - q->cy;
    float ax = fabsf(dx) - (q->hx - q->r), ay = fabsf(dy) - (q->hy - q->r);
    float mx = ax > 0 ? ax : 0, my = ay > 0 ? ay : 0;
    float len = sqrtf(mx * mx + my * my);
    float d = len + (ax > ay ? (ax < 0 ? ax : 0) : (ay < 0 ? ay : 0)) - q->r;
    if (nx) {
        float sx = dx < 0 ? -1.0f : 1.0f, sy = dy < 0 ? -1.0f : 1.0f;
        if (ax > 0 && ay > 0) {
            *nx = sx * mx / len;
            *ny = sy * my / len;
        } else if (ax > ay) {
            *nx = sx;
            *ny = 0;
        } else {
            *nx = 0;
            *ny = sy;
        }
    }
    return d;
}

static RR make_rr(float x, float y, float w, float h, float r)
{
    RR q;
    q.cx = x + w * 0.5f;
    q.cy = y + h * 0.5f;
    q.hx = w * 0.5f;
    q.hy = h * 0.5f;
    float m = (w < h ? w : h) * 0.5f;
    q.r = r > m ? m : r < 0 ? 0 : r;
    return q;
}

// The part of a row of a rounded rectangle that is surely inside it, by more than `inset` pixels from every edge (and so needs no
// distance maths): the pixels [*a, *b) of row py, or an empty span.
static inline void core_span(const RR *q, int py, float inset, int x0, int x1, int *a, int *b)
{
    float hx = q->hx - inset, hy = q->hy - inset;
    *a = *b = x0;
    if (hx <= 0 || hy <= 0 || fabsf(py + 0.5f - q->cy) >= hy) return;
    int lo = (int)floorf(q->cx - hx - 0.5f) + 1, hi = (int)ceilf(q->cx + hx - 0.5f);
    *a = imax(lo, x0);
    *b = imin(hi, x1);
    if (*b < *a) *a = *b = x0;
}

// a solid span of one colour (opaque ones are plain stores)
static inline void solid_span(Surf *s, int y, int xa, int xb, Col c)
{
    if (xb <= xa) return;
    uint8_t *p = s->px + ((size_t)y * s->w + xa) * 4;
    if (c.a >= 0.999f) {
        uint8_t px[4] = {(uint8_t)(c.r * 255.0f + 0.5f), (uint8_t)(c.g * 255.0f + 0.5f), (uint8_t)(c.b * 255.0f + 0.5f), 255};
        uint32_t v;
        memcpy(&v, px, 4);
        for (int i = 0; i < xb - xa; i++) memcpy(p + (size_t)i * 4, &v, 4);
        return;
    }
    for (int x = xa; x < xb; x++) blend(s, x, y, c.r, c.g, c.b, c.a);
}

// ------------------------------------------------------------------------------------------------ shapes

void gfx_fill(Surf *s, Col c)
{
    uint8_t r = (uint8_t)(c.r * 255.0f + 0.5f), g = (uint8_t)(c.g * 255.0f + 0.5f), b = (uint8_t)(c.b * 255.0f + 0.5f);
    for (size_t i = 0, n = (size_t)s->w * s->h; i < n; i++) {
        s->px[i * 4] = r;
        s->px[i * 4 + 1] = g;
        s->px[i * 4 + 2] = b;
        s->px[i * 4 + 3] = 255;
    }
}

void gfx_rect(Surf *s, float x, float y, float w, float h, Col c)
{
    int x0 = imax((int)floorf(x), 0), y0 = imax((int)floorf(y), 0);
    int x1 = imin((int)ceilf(x + w), s->w), y1 = imin((int)ceilf(y + h), s->h);
    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++) blend(s, px, py, c.r, c.g, c.b, c.a);
}

void gfx_rrect_grad(Surf *s, float x, float y, float w, float h, float r, Col top, Col bot)
{
    RR q = make_rr(x, y, w, h, r);
    int x0 = imax((int)floorf(x) - 1, 0), y0 = imax((int)floorf(y) - 1, 0);
    int x1 = imin((int)ceilf(x + w) + 1, s->w), y1 = imin((int)ceilf(y + h) + 1, s->h);
    for (int py = y0; py < y1; py++) {
        float t = clampf((py + 0.5f - y) / h, 0, 1);
        Col c = mixc(top, bot, t);
        int ca, cb;
        core_span(&q, py, q.r + 1.0f, x0, x1, &ca, &cb); // well inside: full cover, no distance needed
        for (int px = x0; px < x1; px++) {
            if (px == ca && cb > ca) {
                solid_span(s, py, ca, cb, c);
                px = cb - 1;
                continue;
            }
            float d = sd_rr(&q, px + 0.5f, py + 0.5f, NULL, NULL);
            float cov = clampf(0.5f - d, 0, 1);
            blend(s, px, py, c.r, c.g, c.b, c.a * cov);
        }
    }
}

void gfx_rrect(Surf *s, float x, float y, float w, float h, float r, Col c)
{
    gfx_rrect_grad(s, x, y, w, h, r, c, c);
}

void gfx_rrect_stroke(Surf *s, float x, float y, float w, float h, float r, float width, Col c)
{
    RR q = make_rr(x, y, w, h, r);
    int x0 = imax((int)floorf(x - width) - 1, 0), y0 = imax((int)floorf(y - width) - 1, 0);
    int x1 = imin((int)ceilf(x + w + width) + 1, s->w), y1 = imin((int)ceilf(y + h + width) + 1, s->h);
    float inset = fmaxf(q.r, width + 0.5f) + 1.0f;
    for (int py = y0; py < y1; py++) {
        int ca, cb;
        core_span(&q, py, inset, x0, x1, &ca, &cb); // deeper inside than the line is wide: untouched
        for (int px = x0; px < x1; px++) {
            if (px == ca && cb > ca) {
                px = cb - 1;
                continue;
            }
            float d = fabsf(sd_rr(&q, px + 0.5f, py + 0.5f, NULL, NULL) + width * 0.5f) - width * 0.5f;
            blend(s, px, py, c.r, c.g, c.b, c.a * clampf(0.5f - d, 0, 1));
        }
    }
}

void gfx_circle_grad(Surf *s, float cx, float cy, float rad, Col top, Col bot)
{
    int x0 = imax((int)floorf(cx - rad) - 1, 0), y0 = imax((int)floorf(cy - rad) - 1, 0);
    int x1 = imin((int)ceilf(cx + rad) + 1, s->w), y1 = imin((int)ceilf(cy + rad) + 1, s->h);
    for (int py = y0; py < y1; py++) {
        Col c = mixc(top, bot, clampf((py + 0.5f - (cy - rad)) / (2 * rad), 0, 1));
        for (int px = x0; px < x1; px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float d = sqrtf(dx * dx + dy * dy) - rad;
            blend(s, px, py, c.r, c.g, c.b, c.a * clampf(0.5f - d, 0, 1));
        }
    }
}

void gfx_circle(Surf *s, float cx, float cy, float rad, Col c)
{
    gfx_circle_grad(s, cx, cy, rad, c, c);
}

void gfx_ring(Surf *s, float cx, float cy, float rad, float width, Col c)
{
    int x0 = imax((int)floorf(cx - rad - width) - 1, 0), y0 = imax((int)floorf(cy - rad - width) - 1, 0);
    int x1 = imin((int)ceilf(cx + rad + width) + 1, s->w), y1 = imin((int)ceilf(cy + rad + width) + 1, s->h);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float d = fabsf(sqrtf(dx * dx + dy * dy) - rad) - width * 0.5f;
            blend(s, px, py, c.r, c.g, c.b, c.a * clampf(0.5f - d, 0, 1));
        }
    }
}

void gfx_line(Surf *s, float ax, float ay, float bx, float by, float width, Col c)
{
    float pad = width * 0.5f + 1.5f;
    int x0 = imax((int)floorf(fminf(ax, bx) - pad), 0), y0 = imax((int)floorf(fminf(ay, by) - pad), 0);
    int x1 = imin((int)ceilf(fmaxf(ax, bx) + pad), s->w), y1 = imin((int)ceilf(fmaxf(ay, by) + pad), s->h);
    float vx = bx - ax, vy = by - ay, len2 = vx * vx + vy * vy;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float wx = px + 0.5f - ax, wy = py + 0.5f - ay;
            float t = len2 > 0 ? clampf((wx * vx + wy * vy) / len2, 0, 1) : 0;
            float dx = wx - vx * t, dy = wy - vy * t;
            float d = sqrtf(dx * dx + dy * dy) - width * 0.5f;
            blend(s, px, py, c.r, c.g, c.b, c.a * clampf(0.5f - d, 0, 1));
        }
    }
}

void gfx_arc(Surf *s, float cx, float cy, float rad, float width, float a0, float a1, Col c)
{
    const float TAU = 6.2831853f;
    float span = a1 - a0;
    int x0 = imax((int)floorf(cx - rad - width) - 1, 0), y0 = imax((int)floorf(cy - rad - width) - 1, 0);
    int x1 = imin((int)ceilf(cx + rad + width) + 1, s->w), y1 = imin((int)ceilf(cy + rad + width) + 1, s->h);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float dx = px + 0.5f - cx, dy = py + 0.5f - cy;
            float dist = sqrtf(dx * dx + dy * dy);
            float d = fabsf(dist - rad) - width * 0.5f;
            if (d > 1) continue;
            float ang = atan2f(dy, dx) - a0;
            ang = fmodf(ang + 2 * TAU, TAU);
            if (ang > span) continue;
            blend(s, px, py, c.r, c.g, c.b, c.a * clampf(0.5f - d, 0, 1));
        }
    }
    gfx_circle(s, cx + rad * cosf(a0), cy + rad * sinf(a0), width * 0.5f, c);
    gfx_circle(s, cx + rad * cosf(a1), cy + rad * sinf(a1), width * 0.5f, c);
}

void gfx_blob(Surf *s, float cx, float cy, float rx, float ry, Col c)
{
    int x0 = imax((int)floorf(cx - rx), 0), y0 = imax((int)floorf(cy - ry), 0);
    int x1 = imin((int)ceilf(cx + rx), s->w), y1 = imin((int)ceilf(cy + ry), s->h);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float dx = (px + 0.5f - cx) / rx, dy = (py + 0.5f - cy) / ry;
            float q = dx * dx + dy * dy;
            if (q >= 1.0f) continue;
            float f = 1.0f - q;
            blend(s, px, py, c.r, c.g, c.b, c.a * f * f * (3.0f - 2.0f * f));
        }
    }
}

// exp(-x) for x in 0..6 from a table (the halo is the one place that needs it for tens of thousands of pixels a frame)
static float fast_exp_neg(float x)
{
    enum { N = 384 };
    static float tab[N + 2];
    static int ready;
    if (!ready) {
        for (int i = 0; i < N + 2; i++) tab[i] = expf(-6.0f * (float)i / N);
        ready = 1;
    }
    if (x >= 6.0f) return 0.0f;
    float f = x * (N / 6.0f);
    int i = (int)f;
    return tab[i] + (tab[i + 1] - tab[i]) * (f - (float)i);
}

static void halo_direct(Surf *s, float x, float y, float w, float h, float r, float spread, Col c)
{
    RR q = make_rr(x, y, w, h, r);
    float pad = spread * 3.0f;
    int x0 = imax((int)floorf(x - pad), 0), y0 = imax((int)floorf(y - pad), 0);
    int x1 = imin((int)ceilf(x + w + pad), s->w), y1 = imin((int)ceilf(y + h + pad), s->h);
    float inv = 1.0f / spread;
    for (int py = y0; py < y1; py++) {
        int ca, cb;
        core_span(&q, py, q.r + 1.0f, x0, x1, &ca, &cb);
        for (int px = x0; px < x1; px++) {
            if (px == ca && cb > ca) {
                px = cb - 1;
                continue;
            }
            float d = sd_rr(&q, px + 0.5f, py + 0.5f, NULL, NULL);
            if (d < 0) continue; // outside only: the glow never tints what it surrounds
            blend(s, px, py, c.r, c.g, c.b, c.a * fast_exp_neg(d * inv));
        }
    }
}

void gfx_blit_rrect(Surf *s, const uint8_t *rgba, int size, float x, float y, float r)
{
    RR q = make_rr(x, y, (float)size, (float)size, r);
    int x0 = imax((int)floorf(x), 0), y0 = imax((int)floorf(y), 0);
    int x1 = imin((int)ceilf(x + size), s->w), y1 = imin((int)ceilf(y + size), s->h);
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float d = sd_rr(&q, px + 0.5f, py + 0.5f, NULL, NULL);
            float cov = clampf(0.5f - d, 0, 1);
            if (cov <= 0) continue;
            int sx = imin(imax(px - (int)floorf(x), 0), size - 1), sy = imin(imax(py - (int)floorf(y), 0), size - 1);
            const uint8_t *p = rgba + ((size_t)sy * size + sx) * 4;
            blend(s, px, py, p[0] / 255.0f, p[1] / 255.0f, p[2] / 255.0f, cov * (p[3] / 255.0f));
        }
    }
}

static void shadow_direct(Surf *s, float x, float y, float w, float h, float r, float dy, float blur, Col c)
{
    RR q = make_rr(x, y, w, h, r);
    float pad = blur * 2.5f + fabsf(dy) + 2.0f;
    int x0 = imax((int)floorf(x - pad), 0), y0 = imax((int)floorf(y - pad), 0);
    int x1 = imin((int)ceilf(x + w + pad), s->w), y1 = imin((int)ceilf(y + h + pad), s->h);
    // whatever is drawn over the shape itself (a card) hides the shadow beneath it: that part is skipped
    for (int py = y0; py < y1; py++) {
        int ca, cb;
        core_span(&q, py, q.r + 1.0f, x0, x1, &ca, &cb);
        for (int px = x0; px < x1; px++) {
            if (px == ca && cb > ca) {
                px = cb - 1;
                continue;
            }
            float d = sd_rr(&q, px + 0.5f, py + 0.5f - dy, NULL, NULL);
            if (d >= blur * 2.4f) continue;
            float a = c.a * (1.0f - smoothf(-blur * 0.25f, blur * 2.4f, d));
            blend(s, px, py, c.r, c.g, c.b, a);
        }
    }
}

// ---- glows and shadows as stored shapes
// A shadow or a glow only depends on the size and softness of the thing that casts it, never on where it is, and a screen draws the
// same few over and over (a row of tiles, the focus frame moving about). So each is worked out once, as a map of how strong it is
// at every pixel, and after that painting it is a lookup. Sizes are rounded to a quarter pixel and the shape is placed on a whole
// pixel, which nobody can see on something this soft.
typedef struct {
    int kind, q[5]; // what, and w, h, r, dy, softness in quarter pixels
    int pad, mw, mh;
    uint8_t *a;
    unsigned used;
} Spr;
enum { SPR_N = 32, SPR_MAX_AREA = 700000, SPR_BUDGET = 8 << 20 }; // at most 8 MB of them are kept
static Spr g_spr[SPR_N];
static unsigned g_spr_tick;
static size_t g_spr_bytes;

static int quarter(float v) { return (int)floorf(v * 4.0f + 0.5f); }

static const Spr *spr_get(int kind, float w, float h, float r, float dy, float soft)
{
    int q[5] = {quarter(w), quarter(h), quarter(r), quarter(dy), quarter(soft)};
    int lru = 0;
    for (int i = 0; i < SPR_N; i++) {
        if (g_spr[i].a && g_spr[i].kind == kind && !memcmp(g_spr[i].q, q, sizeof q)) {
            g_spr[i].used = ++g_spr_tick;
            return &g_spr[i];
        }
        if (!g_spr[i].a) lru = i;
        else if (g_spr[lru].a && g_spr[i].used < g_spr[lru].used) lru = i;
    }
    float fw = q[0] / 4.0f, fh = q[1] / 4.0f, fr = q[2] / 4.0f, fdy = q[3] / 4.0f, fs = q[4] / 4.0f;
    int pad = kind == 0 ? (int)ceilf(fs * 2.5f + fabsf(fdy) + 2.0f) : (int)ceilf(fs * 3.0f) + 1;
    int mw = (int)ceilf(fw) + 2 * pad, mh = (int)ceilf(fh) + 2 * pad;
    if ((long)mw * mh > SPR_MAX_AREA) return NULL;
    while (g_spr_bytes + (size_t)mw * mh > SPR_BUDGET) { // make room: drop the one used longest ago
        int old = -1;
        for (int i = 0; i < SPR_N; i++)
            if (g_spr[i].a && (old < 0 || g_spr[i].used < g_spr[old].used)) old = i;
        if (old < 0) break;
        g_spr_bytes -= (size_t)g_spr[old].mw * g_spr[old].mh;
        free(g_spr[old].a);
        g_spr[old].a = NULL;
        lru = old;
    }
    uint8_t *a = calloc((size_t)mw * mh, 1);
    if (!a) return NULL;
    RR rr = make_rr((float)pad, (float)pad, fw, fh, fr);
    for (int py = 0; py < mh; py++) {
        int ca, cb;
        core_span(&rr, py, rr.r + 1.0f, 0, mw, &ca, &cb);
        for (int px = 0; px < mw; px++) {
            if (kind == 0) {
                if (px == ca && cb > ca) { // under the card itself: nothing to paint
                    px = cb - 1;
                    continue;
                }
                float d = sd_rr(&rr, px + 0.5f, py + 0.5f - fdy, NULL, NULL);
                if (d >= fs * 2.4f) continue;
                a[(size_t)py * mw + px] = (uint8_t)((1.0f - smoothf(-fs * 0.25f, fs * 2.4f, d)) * 255.0f + 0.5f);
            } else {
                if (px == ca && cb > ca) {
                    px = cb - 1;
                    continue;
                }
                float d = sd_rr(&rr, px + 0.5f, py + 0.5f, NULL, NULL);
                if (d < 0) continue;
                a[(size_t)py * mw + px] = (uint8_t)(fast_exp_neg(d / fs) * 255.0f + 0.5f);
            }
        }
    }
    if (g_spr[lru].a) g_spr_bytes -= (size_t)g_spr[lru].mw * g_spr[lru].mh;
    free(g_spr[lru].a);
    g_spr_bytes += (size_t)mw * mh;
    g_spr[lru].kind = kind;
    memcpy(g_spr[lru].q, q, sizeof q);
    g_spr[lru].pad = pad;
    g_spr[lru].mw = mw;
    g_spr[lru].mh = mh;
    g_spr[lru].a = a;
    g_spr[lru].used = ++g_spr_tick;
    return &g_spr[lru];
}

static void spr_paint(Surf *s, const Spr *sp, float x, float y, Col c)
{
    int ox = (int)floorf(x + 0.5f) - sp->pad, oy = (int)floorf(y + 0.5f) - sp->pad;
    int j0 = imax(-oy, 0), j1 = imin(sp->mh, s->h - oy), i0 = imax(-ox, 0), i1 = imin(sp->mw, s->w - ox);
    float k = c.a * (1.0f / 255.0f);
    for (int j = j0; j < j1; j++) {
        const uint8_t *row = sp->a + (size_t)j * sp->mw;
        for (int i = i0; i < i1; i++)
            if (row[i]) blend(s, ox + i, oy + j, c.r, c.g, c.b, k * row[i]);
    }
}

void gfx_halo(Surf *s, float x, float y, float w, float h, float r, float spread, Col c)
{
    const Spr *sp = spr_get(1, w, h, r, 0, spread);
    if (sp) spr_paint(s, sp, x, y, c);
    else halo_direct(s, x, y, w, h, r, spread, c);
}

void gfx_shadow(Surf *s, float x, float y, float w, float h, float r, float dy, float blur, Col c)
{
    const Spr *sp = spr_get(0, w, h, r, dy, blur);
    if (sp) spr_paint(s, sp, x, y, c);
    else shadow_direct(s, x, y, w, h, r, dy, blur, c);
}

void gfx_dither(Surf *s)
{
    uint32_t h = 2463534242u;
    for (size_t i = 0, n = (size_t)s->w * s->h; i < n; i++) {
        h ^= h << 13;
        h ^= h >> 17;
        h ^= h << 5;
        int noise = (int)(h & 3) - 1; // -1..2, mean 0.5: keeps gradients from banding
        for (int c = 0; c < 3; c++) {
            int v = s->px[i * 4 + c] + noise;
            s->px[i * 4 + c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
        }
    }
}

void gfx_lerp(Surf *dst, const Surf *a, const Surf *b, int t)
{
    size_t n = (size_t)dst->w * dst->h * 4;
    if (t <= 0) {
        if (dst != a) memcpy(dst->px, a->px, n);
        return;
    }
    if (t >= 256) {
        if (dst != b) memcpy(dst->px, b->px, n);
        return;
    }
    const uint8_t *pa = a->px, *pb = b->px;
    uint8_t *pd = dst->px;
    for (size_t i = 0; i < n; i++) pd[i] = (uint8_t)(pa[i] + ((((int)pb[i] - (int)pa[i]) * t + 128) >> 8));
}

void gfx_mix_card(Surf *dst, const Surf *src, float x, float y, float w, float h, float r, int dx, int dy, float alpha)
{
    RR q = make_rr(x, y, w, h, r);
    int x0 = imax(imax((int)floorf(x), 0), -dx), x1 = imin(imin((int)ceilf(x + w), src->w), dst->w - dx);
    int y0 = imax((int)floorf(y), 0), y1 = imin((int)ceilf(y + h), src->h);
    int ia = (int)(clampf(alpha, 0, 1) * 256.0f + 0.5f);
    for (int sy = y0; sy < y1; sy++) {
        int ty = sy + dy;
        if (ty < 0 || ty >= dst->h) continue;
        const uint8_t *s = src->px + (size_t)sy * src->w * 4;
        uint8_t *d = dst->px + ((size_t)ty * dst->w + dx) * 4;
        int corner_row = sy + 0.5f < y + q.r || sy + 0.5f > y + h - q.r;
        for (int sx = x0; sx < x1; sx++) {
            int a = ia;
            if (corner_row && (sx + 0.5f < x + q.r || sx + 0.5f > x + w - q.r)) { // only the corners need the shape
                float cov = clampf(0.5f - sd_rr(&q, sx + 0.5f, sy + 0.5f, NULL, NULL), 0, 1);
                a = (int)(ia * cov + 0.5f);
            }
            if (a <= 0) continue;
            for (int k = 0; k < 3; k++) d[sx * 4 + k] = (uint8_t)(d[sx * 4 + k] + ((((int)s[sx * 4 + k] - (int)d[sx * 4 + k]) * a + 128) >> 8));
        }
    }
}

// A texel of a straight-alpha RGBA picture as premultiplied floats; outside the picture it is clear, so edges fade out smoothly.
static inline void texel_pm(const uint8_t *rgba, int sw, int sh, int x, int y, float *pr, float *pg, float *pb, float *pa)
{
    if ((unsigned)x >= (unsigned)sw || (unsigned)y >= (unsigned)sh) {
        *pr = *pg = *pb = *pa = 0.0f;
        return;
    }
    const uint8_t *p = rgba + ((size_t)y * sw + x) * 4;
    float a = p[3] * (1.0f / 255.0f);
    *pr = p[0] * a;
    *pg = p[1] * a;
    *pb = p[2] * a;
    *pa = a;
}

void gfx_blit_over(Surf *s, const uint8_t *rgba, int sw, int sh, int x, int y)
{
    int x0 = imax(x, 0), x1 = imin(x + sw, s->w), y0 = imax(y, 0), y1 = imin(y + sh, s->h);
    for (int py = y0; py < y1; py++) {
        const uint8_t *src = rgba + ((size_t)(py - y) * sw + (x0 - x)) * 4;
        uint8_t *d = s->px + ((size_t)py * s->w + x0) * 4;
        for (int px = x0; px < x1; px++, src += 4, d += 4) {
            unsigned a = src[3];
            if (a == 0) continue;
            if (a == 255) {
                d[0] = src[0];
                d[1] = src[1];
                d[2] = src[2];
            } else {
                d[0] = (uint8_t)(d[0] + ((((int)src[0] - (int)d[0]) * (int)a + 128) >> 8));
                d[1] = (uint8_t)(d[1] + ((((int)src[1] - (int)d[1]) * (int)a + 128) >> 8));
                d[2] = (uint8_t)(d[2] + ((((int)src[2] - (int)d[2]) * (int)a + 128) >> 8));
            }
            d[3] = 255;
        }
    }
}

void gfx_restore(Surf *s, const Surf *from, const uint8_t *mask_rgba, int sw, int sh, int x, int y)
{
    int x0 = imax(x, 0), x1 = imin(x + sw, s->w), y0 = imax(y, 0), y1 = imin(y + sh, s->h);
    for (int py = y0; py < y1; py++) {
        const uint8_t *m = mask_rgba + ((size_t)(py - y) * sw + (x0 - x)) * 4;
        const uint32_t *f = (const uint32_t *)(from->px + ((size_t)py * s->w + x0) * 4);
        uint32_t *d = (uint32_t *)(s->px + ((size_t)py * s->w + x0) * 4);
        for (int px = x0; px < x1; px++, m += 4) d[px - x0] = m[3] ? f[px - x0] : d[px - x0];
    }
}

// A picture laid over the surface at a whole pixel, faded to `ai` / 256 (256 = not at all): what a turned picture comes to when it is not turned,
// not scaled and sits on whole pixels (a ghost patch, a card at rest)
static void blit_over_faded(Surf *s, const uint8_t *rgba, int sw, int sh, int x, int y, int ai)
{
    int x0 = imax(x, 0), x1 = imin(x + sw, s->w), y0 = imax(y, 0), y1 = imin(y + sh, s->h);
    for (int py = y0; py < y1; py++) {
        const uint8_t *src = rgba + ((size_t)(py - y) * sw + (x0 - x)) * 4;
        uint8_t *d = s->px + ((size_t)py * s->w + x0) * 4;
        for (int px = x0; px < x1; px++, src += 4, d += 4) {
            unsigned a = ((unsigned)src[3] * (unsigned)ai + 128u) >> 8;
            if (a == 0) continue;
            if (a >= 255) {
                d[0] = src[0];
                d[1] = src[1];
                d[2] = src[2];
            } else {
                d[0] = (uint8_t)(d[0] + ((((int)src[0] - (int)d[0]) * (int)a + 128) >> 8));
                d[1] = (uint8_t)(d[1] + ((((int)src[1] - (int)d[1]) * (int)a + 128) >> 8));
                d[2] = (uint8_t)(d[2] + ((((int)src[2] - (int)d[2]) * (int)a + 128) >> 8));
            }
            d[3] = 255;
        }
    }
}

// One pixel the general way: the four texels round (u, v) taken as straight-alpha pictures (a texel outside the picture is nothing), mixed by how
// near they are, and laid over what is there. This is where the picture's edge fades out, and where its see-through parts are.
static void xform_pixel_slow(uint8_t *d, const uint8_t *rgba, int sw, int sh, int iu, int iv, float tu, float tv, float alpha)
{
    float r00, g00, b00, a00, r10, g10, b10, a10, r01, g01, b01, a01, r11, g11, b11, a11;
    texel_pm(rgba, sw, sh, iu, iv, &r00, &g00, &b00, &a00);
    texel_pm(rgba, sw, sh, iu + 1, iv, &r10, &g10, &b10, &a10);
    texel_pm(rgba, sw, sh, iu, iv + 1, &r01, &g01, &b01, &a01);
    texel_pm(rgba, sw, sh, iu + 1, iv + 1, &r11, &g11, &b11, &a11);
    float w00 = (1 - tu) * (1 - tv), w10 = tu * (1 - tv), w01 = (1 - tu) * tv, w11 = tu * tv;
    float a = (a00 * w00 + a10 * w10 + a01 * w01 + a11 * w11) * alpha;
    if (a <= 0.002f) return;
    float k = alpha, ia = 1.0f - a;
    float r = (r00 * w00 + r10 * w10 + r01 * w01 + r11 * w11) * k;
    float g = (g00 * w00 + g10 * w10 + g01 * w01 + g11 * w11) * k;
    float b = (b00 * w00 + b10 * w10 + b01 * w01 + b11 * w11) * k;
    float o0 = d[0] * ia + r + 0.5f, o1 = d[1] * ia + g + 0.5f, o2 = d[2] * ia + b + 0.5f;
    d[0] = (uint8_t)(o0 > 255.0f ? 255.0f : o0);
    d[1] = (uint8_t)(o1 > 255.0f ? 255.0f : o1);
    d[2] = (uint8_t)(o2 > 255.0f ? 255.0f : o2);
    d[3] = 255;
}

// Fixed point: the picture's coordinates move along a row by whole steps of 1/65536 of a texel, so the middle of a card (every texel and its
// three neighbours inside the picture and opaque, which is nearly all of it) costs a few whole-number multiplications a pixel; only the rim and
// the see-through parts go the long way round.
void gfx_blit_xform(Surf *s, const uint8_t *rgba, int sw, int sh, float cx, float cy, float scale, float angle, float alpha)
{
    if (!rgba || sw <= 0 || sh <= 0 || scale <= 0.0f || alpha <= 0.0f) return;
    if (alpha > 1.0f) alpha = 1.0f;
    int ai = (int)(alpha * 256.0f + 0.5f); // alpha in 256ths (256 = fully)
    float ca = cosf(angle), sa = sinf(angle), inv = 1.0f / scale;
    float hw = sw * 0.5f, hh = sh * 0.5f;
    if (fabsf(scale - 1.0f) < 1e-5f && fabsf(sa) < 1e-6f) { // not turned, not scaled: on whole pixels it is a plain blit
        float fx0 = cx - hw, fy0 = cy - hh;
        int ix = (int)lroundf(fx0), iy = (int)lroundf(fy0);
        if (fabsf(fx0 - (float)ix) < 0.004f && fabsf(fy0 - (float)iy) < 0.004f) {
            blit_over_faded(s, rgba, sw, sh, ix, iy, ai);
            return;
        }
    }
    // the box that holds the turned, scaled picture
    float ex = (fabsf(ca) * sw + fabsf(sa) * sh) * 0.5f * scale + 1.0f;
    float ey = (fabsf(sa) * sw + fabsf(ca) * sh) * 0.5f * scale + 1.0f;
    int xs = (int)floorf(cx - ex); // where a row starts without any clipping: the picture's coordinates are worked out from here whatever the clip
    int x0 = imax(xs, 0), x1 = imin((int)ceilf(cx + ex), s->w); // (so a draw clipped to a rectangle lands on the same texels as a whole one)
    int y0 = imax((int)floorf(cy - ey), 0), y1 = imin((int)ceilf(cy + ey), s->h);
    const int32_t dU = (int32_t)lroundf(ca * inv * 65536.0f), dV = (int32_t)lroundf(-sa * inv * 65536.0f);
    const size_t stride = (size_t)sw * 4;
    for (int y = y0; y < y1; y++) {
        // the picture's coordinates (texel centres at whole numbers) at the first pixel of the row; they move in a straight line
        float fx = xs + 0.5f - cx, fy = y + 0.5f - cy;
        float u = hw + (fx * ca + fy * sa) * inv - 0.5f;
        float v = hh + (-fx * sa + fy * ca) * inv - 0.5f;
        int32_t U = (int32_t)floorf(u * 65536.0f) + (x0 - xs) * dU, V = (int32_t)floorf(v * 65536.0f) + (x0 - xs) * dV;
        uint8_t *d = s->px + ((size_t)y * s->w + x0) * 4;
        for (int x = x0; x < x1; x++, U += dU, V += dV, d += 4) {
            int iu = U >> 16, iv = V >> 16;
            if (iu < -1 || iv < -1 || iu >= sw || iv >= sh) continue;
            if (iu >= 0 && iv >= 0 && iu < sw - 1 && iv < sh - 1) { // all four texels are in the picture
                const uint8_t *p = rgba + ((size_t)iv * sw + iu) * 4, *q = p + stride;
                unsigned a00 = p[3], a10 = p[7], a01 = q[3], a11 = q[7];
                unsigned fu = (unsigned)(U >> 8) & 255u, fv = (unsigned)(V >> 8) & 255u;
                unsigned w11 = fu * fv, w10 = (fu << 8) - w11, w01 = (fv << 8) - w11, w00 = 65536u - w10 - w01 - w11;
                if ((a00 & a10 & a01 & a11) == 255u) { // opaque all round: the colour is the mix, whatever is under it only shows if it is faded
                    unsigned r = (p[0] * w00 + p[4] * w10 + q[0] * w01 + q[4] * w11 + 32768u) >> 16;
                    unsigned g = (p[1] * w00 + p[5] * w10 + q[1] * w01 + q[5] * w11 + 32768u) >> 16;
                    unsigned b = (p[2] * w00 + p[6] * w10 + q[2] * w01 + q[6] * w11 + 32768u) >> 16;
                    if (ai >= 256) {
                        d[0] = (uint8_t)r;
                        d[1] = (uint8_t)g;
                        d[2] = (uint8_t)b;
                    } else {
                        d[0] = (uint8_t)(d[0] + ((((int)r - (int)d[0]) * ai + 128) >> 8));
                        d[1] = (uint8_t)(d[1] + ((((int)g - (int)d[1]) * ai + 128) >> 8));
                        d[2] = (uint8_t)(d[2] + ((((int)b - (int)d[2]) * ai + 128) >> 8));
                    }
                    d[3] = 255;
                    continue;
                }
                if ((a00 | a10 | a01 | a11) == 0) continue; // nothing here
                // see-through here and there: the same mix as the long way, in whole numbers (texels premultiplied, alpha taken to 0..256 so every scale is a
                // power of two; the result is the long way's to within a level)
                unsigned b00 = a00 + (a00 >> 7), b10 = a10 + (a10 >> 7), b01 = a01 + (a01 >> 7), b11 = a11 + (a11 >> 7);
                uint64_t As = (uint64_t)(b00 * w00) + b10 * w10 + b01 * w01 + b11 * w11;           // up to 2^24
                uint32_t af = (uint32_t)((As * (unsigned)ai) >> 16);                                  // alpha, 0..65536
                if (af < 131) continue;                                                               // (the long way's a <= .002)
                for (int c = 0; c < 3; c++) {
                    uint32_t ps = p[c] * b00 * w00 + p[4 + c] * b10 * w10 + q[c] * b01 * w01 + q[4 + c] * b11 * w11; // premultiplied colour x 2^24
                    uint32_t o = (uint32_t)((d[c] * (uint64_t)(65536 - af) + (((uint64_t)ps * (unsigned)ai) >> 16) + 32768) >> 16);
                    d[c] = (uint8_t)(o > 255 ? 255 : o);
                }
                d[3] = 255;
                continue;
            }
            xform_pixel_slow(d, rgba, sw, sh, iu, iv, (float)(U & 0xFFFF) * (1.0f / 65536.0f), (float)(V & 0xFFFF) * (1.0f / 65536.0f), alpha);
        }
    }
}

// ------------------------------------------------------------------------------------------------ liquid glass

void gfx_glass(Surf *s, const Blur *bg, const Glass *g, float x, float y, float w, float h, float r)
{
    RR q = make_rr(x, y, w, h, r);
    float pad = g->shadow > 0 ? g->shadow_blur * 2.6f + fabsf(g->shadow_dy) + 2.0f : 2.0f;
    int x0 = imax((int)floorf(x - pad), 0), y0 = imax((int)floorf(y - pad), 0);
    int x1 = imin((int)ceilf(x + w + pad), s->w), y1 = imin((int)ceilf(y + h + pad), s->h);
    float bevel = g->bevel > 1.0f ? g->bevel : 1.0f;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float fx = px + 0.5f, fy = py + 0.5f, nx, ny;
            float d = sd_rr(&q, fx, fy, &nx, &ny);
            if (d > 0.5f) {
                if (g->shadow > 0) {
                    float ds = sd_rr(&q, fx, fy - g->shadow_dy, NULL, NULL);
                    if (ds < g->shadow_blur * 2.4f) {
                        float a = g->shadow * (1.0f - smoothf(-g->shadow_blur * 0.25f, g->shadow_blur * 2.4f, ds));
                        blend(s, px, py, g->shadow_col.r, g->shadow_col.g, g->shadow_col.b, a);
                    }
                }
                continue;
            }
            float cover = clampf(0.5f - d, 0, 1);
            if (cover < 1.0f && g->shadow > 0) {
                float ds = sd_rr(&q, fx, fy - g->shadow_dy, NULL, NULL);
                float a = g->shadow * (1.0f - smoothf(-g->shadow_blur * 0.25f, g->shadow_blur * 2.4f, ds));
                blend(s, px, py, g->shadow_col.r, g->shadow_col.g, g->shadow_col.b, a);
            }
            float depth = -d;
            float t = clampf(depth / bevel, 0, 1);
            float k = (1.0f - t) * (1.0f - t);
            float sx = fx + nx * k * g->refr, sy = fy + ny * k * g->refr;
            float c[3];
            if (g->chroma > 0.0f && k > 0.01f) {
                float a[3], b[3];
                blur_sample(bg, sx + nx * k * g->chroma, sy + ny * k * g->chroma, a);
                blur_sample(bg, sx, sy, c);
                blur_sample(bg, sx - nx * k * g->chroma, sy - ny * k * g->chroma, b);
                c[0] = a[0];
                c[2] = b[2];
            } else {
                blur_sample(bg, sx, sy, c);
            }
            float vy = clampf((fy - y) / h, 0, 1);
            float shade = 1.0f - g->bottom_shade * vy * vy;
            float lum = 0.299f * c[0] + 0.587f * c[1] + 0.114f * c[2];
            for (int i = 0; i < 3; i++) {
                float v = lum + (c[i] - lum) * g->sat;
                v *= g->bright;
                v = v * (1.0f - g->tint.a) + (i == 0 ? g->tint.r : i == 1 ? g->tint.g : g->tint.b) * g->tint.a;
                c[i] = v * shade;
            }
            float top = g->top_light * (1.0f - vy) * (1.0f - vy);
            float glow = g->glow * expf(-depth * 0.16f);
            for (int i = 0; i < 3; i++) c[i] += (1.0f - c[i]) * clampf(top + glow, 0, 1);
            if (depth < g->rim_w + 1.0f) {
                float e = 1.0f - smoothf(0.0f, g->rim_w, depth);
                float l = nx * g->lx + ny * g->ly;
                float lp = l > 0 ? l : 0, ln = l < 0 ? -l : 0;
                float spec = e * g->rim * (0.16f + 0.84f * lp * lp + 0.42f * ln * ln);
                for (int i = 0; i < 3; i++) c[i] += (1.0f - c[i]) * clampf(spec, 0, 1);
            }
            if (g->edge.a > 0.0f && depth < 1.6f) {
                float e = g->edge.a * (1.0f - smoothf(0.0f, 1.6f, depth));
                c[0] += (g->edge.r - c[0]) * e;
                c[1] += (g->edge.g - c[1]) * e;
                c[2] += (g->edge.b - c[2]) * e;
            }
            blend(s, px, py, clampf(c[0], 0, 1), clampf(c[1], 0, 1), clampf(c[2], 0, 1), cover);
        }
    }
}

// ------------------------------------------------------------------------------------------------ text

#define CACHE_N 2048

typedef struct {
    int cp, px4;
    uint8_t *bmp;
    int w, h, xoff, yoff;
    float adv;
    int used;
} GlyphEntry;

struct Font {
    stbtt_fontinfo info;
    Font *fallback;
    GlyphEntry *cache;
};

Font *font_load(const unsigned char *ttf)
{
    Font *f = calloc(1, sizeof *f);
    if (!stbtt_InitFont(&f->info, ttf, stbtt_GetFontOffsetForIndex(ttf, 0))) {
        free(f);
        return NULL;
    }
    f->cache = calloc(CACHE_N, sizeof(GlyphEntry));
    return f;
}

void font_set_fallback(Font *f, Font *fb)
{
    f->fallback = fb;
}

static const GlyphEntry *glyph(Font *f, int cp, float px)
{
    if (f->fallback && stbtt_FindGlyphIndex(&f->info, cp) == 0 && stbtt_FindGlyphIndex(&f->fallback->info, cp) != 0)
        return glyph(f->fallback, cp, px);
    int px4 = (int)(px * 4.0f + 0.5f);
    unsigned slot = ((unsigned)cp * 2654435761u ^ (unsigned)px4 * 40503u) % CACHE_N;
    GlyphEntry *e = &f->cache[slot];
    if (e->used && e->cp == cp && e->px4 == px4) return e;
    if (e->used) free(e->bmp);
    float scale = stbtt_ScaleForMappingEmToPixels(&f->info, px4 / 4.0f);
    int x0, y0, x1, y1, adv, lsb;
    stbtt_GetCodepointBitmapBox(&f->info, cp, scale, scale, &x0, &y0, &x1, &y1);
    stbtt_GetCodepointHMetrics(&f->info, cp, &adv, &lsb);
    e->used = 1;
    e->cp = cp;
    e->px4 = px4;
    e->w = x1 - x0;
    e->h = y1 - y0;
    e->xoff = x0;
    e->yoff = y0;
    e->adv = adv * scale;
    e->bmp = NULL;
    if (e->w > 0 && e->h > 0) {
        e->bmp = calloc((size_t)e->w * e->h, 1);
        stbtt_MakeCodepointBitmap(&f->info, e->bmp, e->w, e->h, e->w, scale, scale, cp);
    }
    return e;
}

static int next_cp(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    int cp;
    if (s[0] < 0x80) { cp = s[0]; *p += 1; }
    else if ((s[0] & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) { cp = ((s[0] & 0x1F) << 6) | (s[1] & 0x3F); *p += 2; }
    else if ((s[0] & 0xF0) == 0xE0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) { cp = ((s[0] & 0x0F) << 12) | ((s[1] & 0x3F) << 6) | (s[2] & 0x3F); *p += 3; }
    else if ((s[0] & 0xF8) == 0xF0 && (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80 && (s[3] & 0xC0) == 0x80) { cp = ((s[0] & 0x07) << 18) | ((s[1] & 0x3F) << 12) | ((s[2] & 0x3F) << 6) | (s[3] & 0x3F); *p += 4; }
    else { cp = 0xFFFD; *p += 1; }
    return cp;
}

float text_width(Font *f, float px, float tracking, const char *u)
{
    float w = 0;
    int n = 0;
    for (const char *p = u; *p;) {
        int cp = next_cp(&p);
        w += glyph(f, cp, px)->adv + tracking;
        n++;
    }
    return n ? w - tracking : 0;
}

#ifdef WASHI_PAL_LAB
float g_gfx_text_grain;

static float chalk_hash(int x, int y, unsigned s)
{
    unsigned h = (unsigned)x * 374761393u + (unsigned)y * 668265263u + s * 2246822519u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return ((h ^ (h >> 16)) & 0xFFFFFF) / 16777216.0f;
}

// how much of a letter's pixel the chalk leaves: fine misses and a few larger patches where it skipped over the board's grain
static float chalk_keep(int x, int y)
{
    float fine = chalk_hash(x, y, 7u), mid = 0.25f * (chalk_hash(x >> 1, y >> 1, 11u) + chalk_hash((x + 1) >> 1, y >> 1, 11u) + chalk_hash(x >> 1, (y + 1) >> 1, 11u) +
                                                     chalk_hash((x + 1) >> 1, (y + 1) >> 1, 11u));
    float miss = clampf((0.42f - (0.55f * fine + 0.45f * mid)) * 3.2f, 0, 1);
    return 1.0f - g_gfx_text_grain * miss;
}
#endif

void text_draw(Surf *s, Font *f, float px, float tracking, float x, float baseline, int align, Col c, const char *u)
{
    float w = text_width(f, px, tracking, u);
    if (align == 1) x -= w * 0.5f;
    else if (align == 2) x -= w;
    int by = (int)floorf(baseline + 0.5f);
#ifdef WASHI_PAL_LAB
    int chalk = g_gfx_text_grain > 0 && c.r + c.g + c.b > 2.1f; // (only light writing is chalk: dark letters on stickers stay printed)
#endif
    for (const char *p = u; *p;) {
        int cp = next_cp(&p);
        const GlyphEntry *g = glyph(f, cp, px);
        int gx = (int)floorf(x + 0.5f) + g->xoff, gy = by + g->yoff;
        for (int yy = 0; yy < g->h; yy++) {
            for (int xx = 0; xx < g->w; xx++) {
                uint8_t cov = g->bmp[yy * g->w + xx];
#ifdef WASHI_PAL_LAB
                if (cov && chalk) {
                    blend(s, gx + xx, gy + yy, c.r, c.g, c.b, c.a * (cov / 255.0f) * chalk_keep(gx + xx, gy + yy));
                    continue;
                }
#endif
                if (cov) blend(s, gx + xx, gy + yy, c.r, c.g, c.b, c.a * (cov / 255.0f));
            }
        }
        x += g->adv + tracking;
    }
}

void text_fit(Font *f, float px, float tracking, const char *u, float max_w, char *out, size_t cap)
{
    snprintf(out, cap, "%s", u);
    if (text_width(f, px, tracking, out) <= max_w) return;
    size_t len = strlen(out);
    while (len > 0) {
        len--;
        while (len > 0 && (out[len] & 0xC0) == 0x80) len--; // back up to the start of a UTF-8 character
        char tmp[256];
        snprintf(tmp, sizeof tmp, "%.*s...", (int)len, u);
        if (text_width(f, px, tracking, tmp) <= max_w || len == 0) {
            snprintf(out, cap, "%s", tmp);
            return;
        }
    }
}

// premultiplied bilinear sample of an RGBA picture at (u, v) in source pixels
static void sample_px(const uint8_t *src, int sw, int sh, float u, float v, float out[4])
{
    u -= 0.5f;
    v -= 0.5f;
    int x0 = (int)floorf(u), y0 = (int)floorf(v);
    float fx = u - x0, fy = v - y0;
    out[0] = out[1] = out[2] = out[3] = 0;
    for (int j = 0; j < 2; j++)
        for (int i = 0; i < 2; i++) {
            int x = x0 + i, y = y0 + j;
            if (x < 0 || y < 0 || x >= sw || y >= sh) continue; // outside the picture: clear
            float w = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
            const uint8_t *p = src + ((size_t)y * sw + x) * 4;
            float a = p[3] / 255.0f * w;
            out[0] += p[0] / 255.0f * a;
            out[1] += p[1] / 255.0f * a;
            out[2] += p[2] / 255.0f * a;
            out[3] += a;
        }
}

void gfx_blit_rrect_scaled(Surf *s, const uint8_t *rgba, int sw, int sh, float x, float y, float w, float h, float r)
{
    RR q = make_rr(x, y, w, h, r);
    float step = (float)sw / w; // source pixels per screen pixel
    int x0 = imax((int)floorf(x), 0), y0 = imax((int)floorf(y), 0);
    int x1 = imin((int)ceilf(x + w), s->w), y1 = imin((int)ceilf(y + h), s->h);
    if ((float)sw == w && (float)sh == h && floorf(x) == x && floorf(y) == y) {
        // the picture at its own size on whole pixels (a sheet's cells): each pixel is one of the picture's, and only the corners are cut, so the same
        // result as below without the sampling
        const int ox = (int)x, oy = (int)y, rc = (int)ceilf(r) + 1;
        for (int py = y0; py < y1; py++) {
            int iy = py - oy, edge_y = iy < rc || iy >= sh - rc;
            const uint8_t *row = rgba + (size_t)iy * sw * 4;
            uint8_t *dst = s->px + ((size_t)py * s->w + x0) * 4;
            for (int px = x0; px < x1; px++, dst += 4) {
                int ix = px - ox;
                const uint8_t *p = row + (size_t)ix * 4;
                if (!p[3]) continue;
                float cov = 1.0f;
                if (edge_y && (ix < rc || ix >= sw - rc)) {
                    cov = clampf(0.5f - sd_rr(&q, px + 0.5f, py + 0.5f, NULL, NULL), 0, 1);
                    if (cov <= 0) continue;
                }
                float pa = p[3] / 255.0f, a = pa * cov;
                if (a <= 0.001f) continue;
                for (int k = 0; k < 3; k++) {
                    float v = p[k] / 255.0f * pa * cov + dst[k] / 255.0f * (1 - a);
                    dst[k] = (uint8_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255 + 0.5f);
                }
            }
        }
        return;
    }
    int ss = step > 2.0f ? imin(4, (int)(step + 0.5f)) : 1; // shrinking a lot: average a grid of samples per pixel
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float d = sd_rr(&q, px + 0.5f, py + 0.5f, NULL, NULL);
            float cov = clampf(0.5f - d, 0, 1), acc[4] = {0, 0, 0, 0};
            if (cov <= 0) continue;
            for (int j = 0; j < ss; j++)
                for (int i = 0; i < ss; i++) {
                    float t[4];
                    sample_px(rgba, sw, sh, (px + (i + 0.5f) / ss - x) * step, (py + (j + 0.5f) / ss - y) * ((float)sh / h), t);
                    for (int k = 0; k < 4; k++) acc[k] += t[k] / (ss * ss);
                }
            float a = acc[3] * cov;
            if (a <= 0.001f) continue;
            uint8_t *dst = s->px + ((size_t)py * s->w + px) * 4;
            for (int k = 0; k < 3; k++) {
                float v = acc[k] * cov + dst[k] / 255.0f * (1 - a);
                dst[k] = (uint8_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255 + 0.5f);
            }
        }
    }
}

void gfx_blit_circle(Surf *s, const uint8_t *rgba, int sw, int sh, float cx, float cy, float r, float zoom)
{
    float size = 2 * r * zoom; // the picture's width on screen
    float step = (float)sw / size; // source pixels per screen pixel
    int x0 = imax((int)floorf(cx - r - 1), 0), x1 = imin((int)ceilf(cx + r + 1), s->w);
    int y0 = imax((int)floorf(cy - r - 1), 0), y1 = imin((int)ceilf(cy + r + 1), s->h);
    int ss = step > 2.0f ? imin(4, (int)(step + 0.5f)) : 1; // shrinking a lot: average a grid of samples per pixel
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float d = r - hypotf(px + 0.5f - cx, py + 0.5f - cy) + 0.5f;
            if (d <= 0) continue;
            float cov = d > 1 ? 1 : d, acc[4] = {0, 0, 0, 0};
            for (int j = 0; j < ss; j++)
                for (int i = 0; i < ss; i++) {
                    float sx = (px + (i + 0.5f) / ss - (cx - size / 2)) * step;
                    float sy = (py + (j + 0.5f) / ss - (cy - size / 2)) * step;
                    float t[4];
                    sample_px(rgba, sw, sh, sx, sy, t);
                    for (int k = 0; k < 4; k++) acc[k] += t[k] / (ss * ss);
                }
            float a = acc[3] * cov;
            if (a <= 0.001f) continue;
            uint8_t *dst = s->px + ((size_t)py * s->w + px) * 4;
            for (int k = 0; k < 3; k++) {
                float v = acc[k] * cov + dst[k] / 255.0f * (1 - a); // acc is premultiplied
                dst[k] = (uint8_t)(v < 0 ? 0 : v > 1 ? 255 : v * 255 + 0.5f);
            }
        }
    }
}

void gfx_blit_mask(Surf *s, const uint8_t *rgba, int sw, int sh, float x, float y, float w, float h, Col ink)
{
    float step = (float)sw / w;
    int x0 = imax((int)floorf(x), 0), y0 = imax((int)floorf(y), 0);
    int x1 = imin((int)ceilf(x + w), s->w), y1 = imin((int)ceilf(y + h), s->h);
    int ss = step > 1.0f ? imin(4, (int)ceilf(step)) : 1;
    for (int py = y0; py < y1; py++) {
        for (int px = x0; px < x1; px++) {
            float acc = 0;
            for (int j = 0; j < ss; j++)
                for (int i = 0; i < ss; i++) {
                    float t[4];
                    sample_px(rgba, sw, sh, (px + (i + 0.5f) / ss - x) * step, (py + (j + 0.5f) / ss - y) * ((float)sh / h), t);
                    acc += t[3];
                }
            blend(s, px, py, ink.r, ink.g, ink.b, ink.a * acc / (ss * ss));
        }
    }
}
