// The crayon filter: a picture painted again with wax crayon strokes, from broad to fine, on rough paper (the "wax strokes" attempt of
// the design mock-up's crayon filter, made heavier on purpose: wide strokes that hide most of the picture's detail).
//
// The paper is a map of how deep each point lies (0 a peak, 1 a valley). Wax only lands where the pressure is higher than the depth, so a light
// touch leaves speckle and a hard one fills nearly everything. A stroke is dragged one way, so it uses a map whose speckle is drawn out along that
// way: streaks, not snow. A stroke is carried along the lines of the picture (never across them) and stops when the picture changes colour under it.
// Pure C on arrays, no platform code; the same seed gives the same picture everywhere.
#include "crayon.h"
#include "gfx.h" // (gfx_pump: a long filter keeps the interface's "still working" mark moving)

#include <math.h>
#include <stdlib.h>
#include <string.h>

static float clampf(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
static int clampi(int x, int a, int b) { return x < a ? a : x > b ? b : x; }

static float smoothf(float a, float b, float x)
{
    float t = clampf((x - a) / (b - a), 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

static float hash2(int x, int y, uint32_t s)
{
    uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 1442695041u;
    h = (h ^ (h >> 13)) * 1274126177u;
    h ^= h >> 16;
    return (float)h / 4294967296.0f;
}

static float vnoise(float x, float y, uint32_t s)
{
    float fx0 = floorf(x), fy0 = floorf(y), fx = x - fx0, fy = y - fy0;
    int ix = (int)fx0, iy = (int)fy0;
    float a = hash2(ix, iy, s), b = hash2(ix + 1, iy, s), c = hash2(ix, iy + 1, s), d = hash2(ix + 1, iy + 1, s);
    float ux = fx * fx * (3 - 2 * fx), uy = fy * fy * (3 - 2 * fy);
    return a + (b - a) * ux + (c - a) * uy + (a - b - c + d) * ux * uy;
}

// the random numbers of one picture (mulberry32): the same seed gives the same strokes
typedef struct { uint32_t a; } Rng;

static void rng_seed(Rng *r, uint32_t seed)
{
    r->a = seed;
}

static float rng_f(Rng *r)
{
    r->a += 0x6D2B79F5u;
    uint32_t t = r->a;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return (float)(t ^ (t >> 14)) / 4294967296.0f;
}

// ---- images of three floats a pixel (sRGB, 0..1)
typedef struct { int w, h; float *d; } Img;

static Img img_new(int w, int h)
{
    Img i = {w, h, calloc((size_t)w * h * 3, sizeof(float))};
    return i;
}

static void gauss1(float *a, int w, int h, float sigma)
{
    int r = (int)ceilf(sigma * 2.5f);
    if (r < 1) r = 1;
    float k[64], s = 0;
    if (r > 31) r = 31;
    for (int i = -r; i <= r; i++) {
        k[i + r] = expf(-(float)(i * i) / (2 * sigma * sigma));
        s += k[i + r];
    }
    for (int i = 0; i < 2 * r + 1; i++) k[i] /= s;
    float *t = malloc((size_t)w * h * sizeof(float));
    if (!t) return;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float acc = 0;
            for (int i = -r; i <= r; i++) acc += a[y * w + clampi(x + i, 0, w - 1)] * k[i + r];
            t[y * w + x] = acc;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float acc = 0;
            for (int i = -r; i <= r; i++) acc += t[clampi(y + i, 0, h - 1) * w + x] * k[i + r];
            a[y * w + x] = acc;
        }
    free(t);
}

static Img gauss_img(const Img *im, float sigma)
{
    Img o = img_new(im->w, im->h);
    int n = im->w * im->h;
    float *ch = malloc((size_t)n * sizeof(float));
    if (!ch || !o.d) {
        free(ch);
        return o;
    }
    for (int c = 0; c < 3; c++) {
        for (int i = 0; i < n; i++) ch[i] = im->d[3 * i + c];
        gauss1(ch, im->w, im->h, sigma);
        for (int i = 0; i < n; i++) o.d[3 * i + c] = ch[i];
    }
    free(ch);
    return o;
}

// ---- the paper
typedef struct {
    int w, h;
    float u;
    float *fine, *rest;
    float *maps[8]; // one per direction a stroke can be dragged, made when first needed
} Paper;

// each value replaced by its rank (0..1): the map's depths are spread evenly, so how hard a crayon presses decides how much it covers
static void equalize(const float *a, int n, float *out)
{
    enum { BINS = 2048 };
    uint32_t hist[BINS];
    float cdf[BINS], mn = 1e9f, mx = -1e9f;
    memset(hist, 0, sizeof hist);
    for (int i = 0; i < n; i++) {
        if (a[i] < mn) mn = a[i];
        if (a[i] > mx) mx = a[i];
    }
    float k = (BINS - 1) / (mx - mn > 1e-9f ? mx - mn : 1.0f);
    for (int i = 0; i < n; i++) hist[(int)((a[i] - mn) * k)]++;
    uint32_t acc = 0;
    for (int i = 0; i < BINS; i++) {
        acc += hist[i];
        cdf[i] = (float)acc / (float)n;
    }
    for (int i = 0; i < n; i++) out[i] = cdf[(int)((a[i] - mn) * k)];
}

static int paper_init(Paper *p, int w, int h, uint32_t seed, float u)
{
    memset(p, 0, sizeof *p);
    int n = w * h;
    p->w = w;
    p->h = h;
    p->u = u;
    p->fine = malloc((size_t)n * sizeof(float));
    p->rest = malloc((size_t)n * sizeof(float));
    float *raw = calloc((size_t)n, sizeof(float));
    if (!p->fine || !p->rest || !raw) {
        free(raw);
        return 0;
    }
    float f = 0.85f / (u > 0.9f ? u : 0.9f);
    for (int y = 0, i = 0; y < h; y++)
        for (int x = 0; x < w; x++, i++) {
            p->fine[i] = 0.45f * hash2(x, y, seed) + 0.55f * vnoise(x * f * 0.8f, y * f * 0.8f, seed + 4);
            raw[i] = 0.55f * vnoise(x * f * 0.3f, y * f * 0.3f, seed + 1) + 0.45f * vnoise(x * f * 0.11f, y * f * 0.11f, seed + 2);
        }
    equalize(raw, n, p->rest);
    free(raw);
    return 1;
}

static void paper_free(Paper *p)
{
    free(p->fine);
    free(p->rest);
    for (int i = 0; i < 8; i++) free(p->maps[i]);
}

static const float *paper_dir(Paper *p, int bin)
{
    if (p->maps[bin]) return p->maps[bin];
    int w = p->w, h = p->h, n = w * h;
    float *g = calloc((size_t)n, sizeof(float)), *eg = calloc((size_t)n, sizeof(float)), *out = calloc((size_t)n, sizeof(float));
    if (!g || !eg || !out) {
        free(g);
        free(eg);
        free(out);
        return p->rest;
    }
    float th = bin * 3.14159265f / 8.0f, cx = cosf(th), cy = sinf(th);
    int L = (int)lroundf(7.0f * p->u);
    if (L < 3) L = 3;
    for (int y = 0, i = 0; y < h; y++)
        for (int x = 0; x < w; x++, i++) {
            float acc = 0;
            for (int t = -L; t <= L; t++) acc += p->fine[clampi((int)lroundf(y + cy * t), 0, h - 1) * w + clampi((int)lroundf(x + cx * t), 0, w - 1)];
            g[i] = acc / (float)(2 * L + 1);
        }
    equalize(g, n, eg);
    for (int i = 0; i < n; i++) g[i] = 0.5f * eg[i] + 0.5f * p->rest[i];
    equalize(g, n, out);
    free(g);
    free(eg);
    p->maps[bin] = out;
    return out;
}

static int bin_of(float ex, float ey)
{
    float a = atan2f(ey, ex);
    a = fmodf(fmodf(a, 3.14159265f) + 3.14159265f, 3.14159265f);
    return (int)lroundf(a / (3.14159265f / 8.0f)) % 8;
}

// ---- crayon and pressure for a target colour
static const float PAPERC[3] = {1.0f, 0.996f, 0.984f};

typedef struct {
    float col[3];
    float c;
} Wax;

static Wax waxify(const float t[3])
{
    Wax w;
    float mx = fmaxf(t[0], fmaxf(t[1], t[2])), mn = fminf(t[0], fminf(t[1], t[2]));
    float ink = fmaxf(1.0f - (0.299f * t[0] + 0.587f * t[1] + 0.114f * t[2]), 0.85f * (mx - mn));
    if (ink < 0.07f) {
        memcpy(w.col, PAPERC, sizeof w.col);
        w.c = 0;
        return w;
    }
    float c = 0.5f + 0.46f * smoothf(0.07f, 0.5f, ink);
    w.c = c;
    for (int k = 0; k < 3; k++) w.col[k] = clampf(PAPERC[k] + (t[k] - PAPERC[k]) / c, 0.0f, 1.0f);
    return w;
}

static void boost(const float t[3], float k, float out[3])
{
    float y = 0.299f * t[0] + 0.587f * t[1] + 0.114f * t[2];
    for (int c = 0; c < 3; c++) out[c] = clampf(y + (t[c] - y) * k, 0.0f, 1.0f);
}

// ---- the sheet being painted
typedef struct {
    int w, h;
    Paper *paper;
    float *d;     // the colour so far
    float *scr;   // one stroke's coverage so far
    int *touched; // the pixels it has touched
} Sheet;

typedef struct {
    Sheet *sh;
    float r, pressure, sx, sl, total, ex, ey;
    uint32_t seed;
    int nt;
} Stroker;

static void stamp(Stroker *st, float cx, float cy, float dd)
{
    Sheet *sh = st->sh;
    float r = st->r, end = fminf(smoothf(0.0f, 2.5f, dd), 0.55f + 0.45f * smoothf(0.0f, 6.0f, st->total - dd));
    float along = (0.86f + 0.28f * vnoise(dd * 0.05f + st->sl, 3.3f, st->seed)) * (0.62f + 0.38f * end);
    const float *gr = paper_dir(sh->paper, bin_of(st->ex, st->ey));
    int x0 = (int)fmaxf(0, floorf(cx - r - 1)), x1 = (int)fminf((float)(sh->w - 1), ceilf(cx + r + 1));
    int y0 = (int)fmaxf(0, floorf(cy - r - 1)), y1 = (int)fminf((float)(sh->h - 1), ceilf(cy + r + 1));
    for (int y = y0; y <= y1; y++)
        for (int x = x0; x <= x1; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = sqrtf(dx * dx + dy * dy);
            if (d > r) continue;
            float prof = d < 0.55f * r ? 1.0f : 1.0f - (d - 0.55f * r) / (0.45f * r);
            float v = dx * -st->ey + dy * st->ex;
            float p = st->pressure * prof * along * (0.78f + 0.44f * vnoise(v * 0.8f + st->sx, 1.7f, st->seed + 1));
            int idx = y * sh->w + x;
            float g = gr[idx], cov = smoothf(g - 0.1f, g + 0.1f, p);
            if (cov > sh->scr[idx]) {
                if (sh->scr[idx] == 0) sh->touched[st->nt++] = idx;
                sh->scr[idx] = cov;
            }
        }
}

// lays one stroke down: a polyline, a brush radius, a colour and a pressure. Coverage is worked out for the whole stroke first (the strongest
// touch at each pixel) and then laid on once, so a stroke does not darken where its own stamps overlap.
static void stroke(Sheet *sh, const float (*pts)[2], int n, float r, const float col[3], float pressure, uint32_t seed, float opacity)
{
    Stroker st = {sh, r, pressure, hash2((int)seed, 7, 3) * 60.0f, hash2((int)seed, 9, 5) * 60.0f, 0, 1, 0, seed, 0};
    float total = 0;
    for (int i = 1; i < n; i++) total += hypotf(pts[i][0] - pts[i - 1][0], pts[i][1] - pts[i - 1][1]);
    if (total < 1e-6f) return;
    st.total = total;
    float step = fmaxf(0.7f, r * 0.3f), walked = 0, next_at = 0;
    for (int i = 1; i < n; i++) {
        float ax = pts[i - 1][0], ay = pts[i - 1][1], bx = pts[i][0], by = pts[i][1], d = hypotf(bx - ax, by - ay);
        if (d < 1e-6f) continue;
        st.ex = (bx - ax) / d;
        st.ey = (by - ay) / d;
        while (next_at <= walked + d) {
            float t = next_at - walked;
            stamp(&st, ax + st.ex * t, ay + st.ey * t, next_at);
            next_at += step;
        }
        walked += d;
    }
    stamp(&st, pts[n - 1][0], pts[n - 1][1], total);
    for (int k = 0; k < st.nt; k++) {
        int idx = sh->touched[k];
        float a = sh->scr[idx] * opacity;
        sh->scr[idx] = 0;
        for (int c = 0; c < 3; c++) sh->d[3 * idx + c] += (col[c] - sh->d[3 * idx + c]) * a;
    }
}

// ---- the lines of the picture: the way a stroke should run at each pixel (along the edges there, the base angle where there are none)
typedef struct { float *dx, *dy; } Field;

static void dir_field(const Img *src, float u, float base, Field *f)
{
    int w = src->w, h = src->h, n = w * h;
    float *y = malloc((size_t)n * sizeof(float)), *jxx = malloc((size_t)n * sizeof(float)), *jxy = malloc((size_t)n * sizeof(float)),
          *jyy = malloc((size_t)n * sizeof(float));
    f->dx = malloc((size_t)n * sizeof(float));
    f->dy = malloc((size_t)n * sizeof(float));
    if (!y || !jxx || !jxy || !jyy || !f->dx || !f->dy) {
        free(y);
        free(jxx);
        free(jxy);
        free(jyy);
        return;
    }
    for (int i = 0; i < n; i++) y[i] = 0.299f * src->d[3 * i] + 0.587f * src->d[3 * i + 1] + 0.114f * src->d[3 * i + 2];
    gauss1(y, w, h, 1.1f * u + 0.3f);
    for (int yy = 0; yy < h; yy++)
        for (int x = 0; x < w; x++) {
            int xm = x > 0 ? x - 1 : 0, xp = x < w - 1 ? x + 1 : w - 1, ym = yy > 0 ? yy - 1 : 0, yp = yy < h - 1 ? yy + 1 : h - 1;
            float a = y[ym * w + xm], b = y[ym * w + x], c = y[ym * w + xp], d = y[yy * w + xm], e = y[yy * w + xp], ff = y[yp * w + xm],
                  g = y[yp * w + x], hh = y[yp * w + xp];
            float gx = (c + 2 * e + hh - a - 2 * d - ff) / 4, gy = (ff + 2 * g + hh - a - 2 * b - c) / 4;
            int i = yy * w + x;
            jxx[i] = gx * gx;
            jxy[i] = gx * gy;
            jyy[i] = gy * gy;
        }
    float sg = 3.2f * u + 1;
    gauss1(jxx, w, h, sg);
    gauss1(jxy, w, h, sg);
    gauss1(jyy, w, h, sg);
    float cb = cosf(2 * base), sb = sinf(2 * base);
    for (int i = 0; i < n; i++) {
        float a = jxx[i] - jyy[i], b = 2 * jxy[i], e = jxx[i] + jyy[i], mg = sqrtf(a * a + b * b), coh = mg / (e + 1e-9f);
        float wgt = smoothf(0.25f, 0.75f, coh) * smoothf(0.0006f, 0.006f, e);
        float vx = wgt * (-a / (mg + 1e-12f)) + (1 - wgt) * cb, vy = wgt * (-b / (mg + 1e-12f)) + (1 - wgt) * sb;
        float ang = 0.5f * atan2f(vy, vx);
        f->dx[i] = cosf(ang);
        f->dy[i] = sinf(ang);
    }
    free(y);
    free(jxx);
    free(jxy);
    free(jyy);
}

typedef struct {
    float r, T, tol, max_len, min_len;
} Layer;

static void col_at(const Img *im, float px, float py, float out[3])
{
    int xi = clampi((int)px, 0, im->w - 1), yi = clampi((int)py, 0, im->h - 1);
    memcpy(out, im->d + 3 * (yi * im->w + xi), 3 * sizeof(float));
}

enum { MAXPTS = 160 };

// one half of a stroke: from the seed along the field (`sign` 1: one way, -1: the other), stopping when the picture changes under it
static int walk(const Img *ref, const Img *cur, const Field *fld, const Layer *L, float sx, float sy, int sign, const float c0[3], float step_len,
                int max_steps, int min_steps, float (*out)[2])
{
    int w = ref->w, h = ref->h, n = 0;
    float px = sx + 0.5f, py = sy + 0.5f, lx = 0, ly = 0;
    for (int st = 0; st < max_steps && n < MAXPTS / 2; st++) {
        int k = clampi((int)py, 0, h - 1) * w + clampi((int)px, 0, w - 1);
        float fx = fld->dx[k] * sign, fy = fld->dy[k] * sign;
        if (st > 0) {
            if (fx * lx + fy * ly < 0) {
                fx = -fx;
                fy = -fy;
            }
            fx = 0.55f * fx + 0.45f * lx;
            fy = 0.55f * fy + 0.45f * ly;
            float l = hypotf(fx, fy);
            if (l == 0) l = 1;
            fx /= l;
            fy /= l;
        }
        px += fx * step_len;
        py += fy * step_len;
        if (px < 0 || py < 0 || px >= w || py >= h) break;
        lx = fx;
        ly = fy;
        out[n][0] = px;
        out[n][1] = py;
        n++;
        if (st >= min_steps) {
            float rr[3], cc[3];
            col_at(ref, px, py, rr);
            col_at(cur, px, py, cc);
            float d1 = sqrtf((rr[0] - c0[0]) * (rr[0] - c0[0]) + (rr[1] - c0[1]) * (rr[1] - c0[1]) + (rr[2] - c0[2]) * (rr[2] - c0[2]));
            float d2 = sqrtf((rr[0] - cc[0]) * (rr[0] - cc[0]) + (rr[1] - cc[1]) * (rr[1] - cc[1]) + (rr[2] - cc[2]) * (rr[2] - cc[2]));
            if (d1 > L->tol || d2 < 0.45f * d1) break;
        }
    }
    return n;
}

uint8_t *crayon_filter(const uint8_t *rgba, int size, uint32_t seed)
{
    int w = size, h = size, n = w * h;
    uint8_t *out = malloc((size_t)n * 4);
    if (!out) return NULL;
    memcpy(out, rgba, (size_t)n * 4);
    Img src = img_new(w, h);
    Paper paper;
    memset(&paper, 0, sizeof paper);
    Sheet sheet = {w, h, &paper, malloc((size_t)n * 3 * sizeof(float)), calloc((size_t)n, sizeof(float)), malloc((size_t)n * sizeof(int))};
    Field fld = {NULL, NULL};
    int ok = src.d && sheet.d && sheet.scr && sheet.touched && paper_init(&paper, w, h, seed + 21, 1.0f);
    if (ok) {
        for (int i = 0; i < n; i++) {
            const uint8_t *p = rgba + 4 * i;
            // (a see-through pixel is paper)
            float a = p[3] / 255.0f;
            for (int c = 0; c < 3; c++) {
                src.d[3 * i + c] = (p[c] / 255.0f) * a + PAPERC[c] * (1 - a);
                sheet.d[3 * i + c] = PAPERC[c];
            }
        }
        dir_field(&src, 1.0f, -0.62f, &fld);
        ok = fld.dx && fld.dy;
    }
    if (ok) {
        Rng rng;
        rng_seed(&rng, seed * 13u + 5u);
        for (int i = 0; i < 6; i++) rng_f(&rng);
        // heavy: broad strokes first, then finer ones where the picture still shows through (as if drawn at 256 px and then shrunk, only
        // a lot less)
        const Layer layers[3] = {{4.2f, 0.0f, 0.15f, 52.0f, 12.0f}, {2.5f, 0.09f, 0.12f, 31.0f, 7.5f}, {1.3f, 0.17f, 0.10f, 17.0f, 4.0f}};
        for (int li = 0; li < 3; li++) {
            const Layer *L = &layers[li];
            float r = L->r;
            Img ref = gauss_img(&src, 0.5f * r + 0.2f);
            Img sheet_img = {w, h, sheet.d};
            Img cur = gauss_img(&sheet_img, 0.7f * r + 0.3f);
            int step = (int)lroundf(r);
            if (step < 1) step = 1;
            int cap = ((w + step - 1) / step) * ((h + step - 1) / step), ns = 0;
            float (*seeds)[2] = malloc((size_t)cap * sizeof *seeds);
            if (ref.d && cur.d && seeds) {
                for (int cy = 0; cy < h; cy += step) {
                    gfx_pump();
                    for (int cx = 0; cx < w; cx += step) {
                        float tot = 0, be = -1;
                        int bx = cx, by = cy, cnt = 0;
                        for (int y = cy; y < h && y < cy + step; y++)
                            for (int x = cx; x < w && x < cx + step; x++) {
                                int i = 3 * (y * w + x);
                                float dr = ref.d[i] - cur.d[i], dg = ref.d[i + 1] - cur.d[i + 1], db = ref.d[i + 2] - cur.d[i + 2];
                                float e = sqrtf(dr * dr + dg * dg + db * db) / 1.732f;
                                tot += e;
                                cnt++;
                                if (e > be) {
                                    be = e;
                                    bx = x;
                                    by = y;
                                }
                            }
                        if (tot / (float)cnt > L->T) {
                            seeds[ns][0] = (float)bx;
                            seeds[ns][1] = (float)by;
                            ns++;
                        }
                    }
                }
                for (int i = ns - 1; i > 0; i--) { // the strokes go down in a shuffled order
                    int j = (int)(rng_f(&rng) * (float)(i + 1));
                    float t0 = seeds[i][0], t1 = seeds[i][1];
                    seeds[i][0] = seeds[j][0];
                    seeds[i][1] = seeds[j][1];
                    seeds[j][0] = t0;
                    seeds[j][1] = t1;
                }
                float step_len = fmaxf(0.8f, r * 0.6f);
                int max_steps = (int)lroundf(L->max_len / step_len / 2), min_steps = (int)lroundf(L->min_len / step_len / 2);
                static float fw[MAXPTS / 2][2], bw[MAXPTS / 2][2], pts[MAXPTS][2];
                for (int si = 0; si < ns; si++) {
                    if ((si & 15) == 0) gfx_pump();
                    float c0[3], t[3];
                    col_at(&ref, seeds[si][0] + 0.5f, seeds[si][1] + 0.5f, c0);
                    boost(c0, 1.15f, t);
                    Wax wx = waxify(t);
                    if (wx.c < 0.1f) continue;
                    float col[3];
                    for (int c = 0; c < 3; c++) col[c] = clampf(wx.col[c] * (1 + (rng_f(&rng) - 0.5f) * 0.08f), 0.0f, 1.0f);
                    int nf = walk(&ref, &cur, &fld, L, seeds[si][0], seeds[si][1], 1, c0, step_len, max_steps, min_steps, fw);
                    int nb = walk(&ref, &cur, &fld, L, seeds[si][0], seeds[si][1], -1, c0, step_len, max_steps, min_steps, bw);
                    int np = 0;
                    for (int i = nb - 1; i >= 0; i--) {
                        pts[np][0] = bw[i][0];
                        pts[np][1] = bw[i][1];
                        np++;
                    }
                    pts[np][0] = seeds[si][0] + 0.5f;
                    pts[np][1] = seeds[si][1] + 0.5f;
                    np++;
                    for (int i = 0; i < nf; i++) {
                        pts[np][0] = fw[i][0];
                        pts[np][1] = fw[i][1];
                        np++;
                    }
                    if (np < 2) continue;
                    float pr = wx.c * (0.92f + 0.16f * rng_f(&rng));
                    stroke(&sheet, (const float(*)[2])pts, np, r, col, pr, (uint32_t)(rng_f(&rng) * 1e6f), 0.96f);
                }
            }
            free(seeds);
            free(ref.d);
            free(cur.d);
        }
        for (int i = 0; i < n; i++)
            for (int c = 0; c < 3; c++) out[4 * i + c] = (uint8_t)(clampf(sheet.d[3 * i + c], 0.0f, 1.0f) * 255.0f + 0.5f);
    }
    free(src.d);
    free(sheet.d);
    free(sheet.scr);
    free(sheet.touched);
    free(fld.dx);
    free(fld.dy);
    paper_free(&paper);
    return out; // (if anything could not be allocated, the picture comes back as it was)
}

// ---- a grid of blocks coloured in with wax (the About page's QR code)
//
// Each dark cell is coloured in by hand: the block's edges wander a little off the grid, two passes of hatch strokes cross each other (one lighter,
// one darker, each stroke with its own pressure that rises and falls along it), the paper's tooth shows through as pinholes where the pressure is
// light, and the edge of every stroke ends a little short or a little over. Evaluated a pixel at a time (four samples to a pixel, so the pinholes
// come out as soft grey rather than snow), so the picture can be any size and never needs a buffer the size of the card.

typedef struct {
    float pitch, width, gain, opacity;
    float cs, sn, t0;
    int n;
    float *v; // 10 arrays of n: pressure, three (frequency, phase, amplitude) wobbles of the stroke's line and its pressure, and the edge spread
} Hatch;

enum { H_PRESS, H_F1, H_PH1, H_A1, H_F2, H_PH2, H_A2, H_F3, H_PH3, H_SPREAD, H_ARRAYS };

static int hatch_new(Hatch *h, Rng *r, float angle_deg, float pitch, float width_k, float plo, float gain, float opacity, float spread_lo,
                     float spread_hi, int w, int hgt)
{
    float th = angle_deg * 3.14159265f / 180.0f;
    h->pitch = pitch;
    h->width = pitch * width_k * 0.5f;
    h->gain = gain;
    h->opacity = opacity;
    h->cs = cosf(th);
    h->sn = sinf(th);
    float t[4] = {0, -(float)w * h->sn, (float)hgt * h->cs, -(float)w * h->sn + (float)hgt * h->cs};
    h->t0 = fminf(fminf(t[0], t[1]), fminf(t[2], t[3]));
    h->n = (int)((float)(w > hgt ? w : hgt) * 1.5f / pitch) + 4;
    h->v = malloc((size_t)h->n * H_ARRAYS * sizeof(float));
    if (!h->v) return 0;
    for (int k = 0; k < h->n; k++) {
        float *p = h->v + k;
        const int n = h->n;
        p[H_PRESS * n] = plo + (1.0f - plo) * rng_f(r);
        p[H_F1 * n] = 0.04f + 0.08f * rng_f(r);
        p[H_PH1 * n] = 6.28f * rng_f(r);
        p[H_A1 * n] = 0.15f + 0.40f * rng_f(r);
        p[H_F2 * n] = 0.2f + 0.3f * rng_f(r);
        p[H_PH2 * n] = 6.28f * rng_f(r);
        p[H_A2 * n] = 0.05f + 0.15f * rng_f(r);
        p[H_F3 * n] = 0.03f + 0.06f * rng_f(r);
        p[H_PH3 * n] = 6.28f * rng_f(r);
        p[H_SPREAD * n] = spread_lo + (spread_hi - spread_lo) * rng_f(r);
    }
    return 1;
}

// the pressure of the strokes at (x, y), 0..1; *edge is the spread of the stroke that is under the point (how far its end runs past the cell's edge)
static float hatch_at(const Hatch *h, float x, float y, float *edge)
{
    float s = x * h->cs + y * h->sn, t = -x * h->sn + y * h->cs - h->t0;
    int k0 = (int)floorf(t / h->pitch), n = h->n;
    float out = 0.0f;
    *edge = h->v[H_SPREAD * n + clampi(k0, 0, n - 1)];
    for (int dk = -1; dk <= 1; dk++) {
        int k = clampi(k0 + dk, 0, n - 1);
        const float *p = h->v + k;
        float tt = t - ((float)k + 0.5f) * h->pitch + p[H_A1 * n] * sinf(s * p[H_F1 * n] + p[H_PH1 * n]) + p[H_A2 * n] * sinf(s * p[H_F2 * n] + p[H_PH2 * n]);
        float prof = clampf(1.0f - fabsf(tt) / h->width, 0.0f, 1.0f);
        prof = prof * prof * (3.0f - 2.0f * prof);
        float pr = p[H_PRESS * n] * (0.82f + 0.18f * sinf(s * p[H_F3 * n] + p[H_PH3 * n]));
        out = 1.0f - (1.0f - out) * (1.0f - prof * pr);
    }
    return out;
}

typedef struct {
    const uint8_t *cells;
    int n;
    float cell, gx, gy;
    float dark[3], light[3];
    uint32_t seed;
    Hatch h1, h2;
} Grid;

enum { G_SS = 2 }; // samples to a side of a pixel

// the grid's cell (i, j) holds wax
static int grid_on(const Grid *g, int i, int j) { return i >= 0 && j >= 0 && i < g->n && j < g->n && g->cells[j * g->n + i]; }

// the distance in px from (a, b) to the segment (x0, y0)-(x1, y1)
static float seg_dist(float a, float b, float x0, float y0, float x1, float y1)
{
    float dx = x1 - x0, dy = y1 - y0, t = clampf(((a - x0) * dx + (b - y0) * dy) / (dx * dx + dy * dy), 0.0f, 1.0f);
    float ex = a - (x0 + t * dx), ey = b - (y0 + t * dy);
    return sqrtf(ex * ex + ey * ey);
}

// Signed distance in px from (x, y) to the edge of the wax blocks (each a touch bigger than its cell), positive inside; -1e9 when no block is near,
// 8 when inside with no edge near. Only the sides of a block with no wax next to them are edges, so touching blocks are one shape.
static float grid_dist(const Grid *g, float x, float y)
{
    const float inf = 0.30f, c = g->cell;
    int ci = (int)floorf((x - g->gx) / c), cj = (int)floorf((y - g->gy) / c), any = 0, inside = 0;
    float best = 8.0f;
    for (int dj = -1; dj <= 1; dj++)
        for (int di = -1; di <= 1; di++) {
            int i = ci + di, j = cj + dj;
            if (!grid_on(g, i, j)) continue;
            any = 1;
            float x0 = g->gx + (float)i * c - inf, y0 = g->gy + (float)j * c - inf, x1 = x0 + c + 2 * inf, y1 = y0 + c + 2 * inf;
            if (x >= x0 && x <= x1 && y >= y0 && y <= y1) inside = 1;
            int l = !grid_on(g, i - 1, j), r = !grid_on(g, i + 1, j), u = !grid_on(g, i, j - 1), d = !grid_on(g, i, j + 1);
            float ya = u ? y0 : y0 + inf, yb = d ? y1 : y1 - inf, xa = l ? x0 : x0 + inf, xb = r ? x1 : x1 - inf; // a side runs on past its end only at a corner
            if (u) best = fminf(best, seg_dist(x, y, xa, y0, xb, y0));
            if (d) best = fminf(best, seg_dist(x, y, xa, y1, xb, y1));
            if (l) best = fminf(best, seg_dist(x, y, x0, ya, x0, yb));
            if (r) best = fminf(best, seg_dist(x, y, x1, ya, x1, yb));
        }
    if (!any) return -1e9f;
    return inside ? best : -best;
}

// the paper's tooth at a sample, 0..1: grain, a little clumped
static float tooth(float x, float y, int sx, int sy, uint32_t seed)
{
    float t = 0.50f * hash2(sx, sy, seed) + 0.32f * vnoise(x / 1.2f, y / 1.2f, seed + 1u) + 0.18f * vnoise(x / 3.5f, y / 3.5f, seed + 2u);
    return clampf((t - 0.04f) / 0.92f, 0.0f, 1.0f);
}

// one sample: premultiplied colour (0..255) and coverage in out[0..3]
static void grid_sample(const Grid *g, float x, float y, int sx, int sy, float out[4])
{
    out[0] = out[1] = out[2] = out[3] = 0.0f;
    if (grid_dist(g, x, y) < -4.0f) return; // far from every block (before any warp)
    // the edges wander: low, slow drift and a finer wobble
    float wx = x + (vnoise(x / 7.0f, y / 7.0f, g->seed + 11u) - 0.5f) * 1.0f + (vnoise(x / 1.6f, y / 1.6f, g->seed + 12u) - 0.5f) * 0.45f;
    float wy = y + (vnoise(x / 7.0f, y / 7.0f, g->seed + 13u) - 0.5f) * 1.0f + (vnoise(x / 1.6f, y / 1.6f, g->seed + 14u) - 0.5f) * 0.45f;
    float d = grid_dist(g, wx, wy);
    if (d < -3.0f) return;
    float rim = 1.0f - smoothf(0.5f, 1.5f, d > 0 ? d : 0.0f);
    float blot = (vnoise(x / 7.0f, y / 7.0f, g->seed + 21u) - 0.5f) * 2.0f * 0.09f;
    float edge1, edge2;
    float p1 = hatch_at(&g->h1, x, y, &edge1), p2 = hatch_at(&g->h2, x, y, &edge2);
    float a1 = smoothf(-0.05f, 0.14f, p1 * g->h1.gain - tooth(x, y, sx, sy, g->seed + 31u)) * smoothf(-0.30f, 0.30f, d + edge1);
    float a2 = smoothf(-0.05f, 0.14f, p2 * g->h2.gain - tooth(x, y, sx, sy, g->seed + 41u)) * smoothf(-0.30f, 0.30f, d + edge2);
    float m1 = smoothf(0.45f, 0.95f, p1), m2 = smoothf(0.45f, 0.95f, p2);
    float o1 = a1 * g->h1.opacity * (0.80f + 0.20f * m1), o2 = a2 * g->h2.opacity * (0.82f + 0.18f * m2);
    float k = (1.0f + blot) * (1.0f - 0.22f * rim);
    for (int c = 0; c < 3; c++) {
        float l = g->light[c], dk = g->dark[c];
        float c1 = (l * (1.0f - m1) + (l * 0.55f + dk * 0.45f) * m1) * k;
        float c2 = ((l * 0.35f + dk * 0.65f) * (1.0f - m2) + dk * m2) * k;
        out[c] = c1 * o1 * (1.0f - o2) + c2 * o2;
    }
    out[3] = o1 + o2 - o1 * o2;
}

uint8_t *crayon_grid(const uint8_t *cells, int n, float cell, int w, int h, float gx, float gy, uint32_t dark, uint32_t light, uint32_t seed)
{
    uint8_t *out = calloc((size_t)w * h * 4, 1);
    if (!out) return NULL;
    Grid g;
    memset(&g, 0, sizeof g);
    g.cells = cells;
    g.n = n;
    g.cell = cell;
    g.gx = gx;
    g.gy = gy;
    g.seed = seed;
    for (int c = 0; c < 3; c++) {
        g.dark[c] = (float)((dark >> (16 - 8 * c)) & 255u);
        g.light[c] = (float)((light >> (16 - 8 * c)) & 255u);
    }
    Rng r;
    rng_seed(&r, seed);
    if (!hatch_new(&g.h1, &r, 58.0f, 3.0f, 3.2f, 0.58f, 1.30f, 0.98f, -0.75f, 1.15f, w, h) ||
        !hatch_new(&g.h2, &r, -32.0f, 3.4f, 2.9f, 0.62f, 1.24f, 0.95f, -0.75f, 1.15f, w, h)) {
        free(g.h1.v);
        free(g.h2.v);
        free(out);
        return NULL;
    }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            if (x == 0 && (y & 7) == 0) gfx_pump();
            float acc[4] = {0, 0, 0, 0};
            for (int sy = 0; sy < G_SS; sy++)
                for (int sx = 0; sx < G_SS; sx++) {
                    float o[4];
                    grid_sample(&g, (float)x + ((float)sx + 0.5f) / G_SS, (float)y + ((float)sy + 0.5f) / G_SS, x * G_SS + sx, y * G_SS + sy, o);
                    for (int c = 0; c < 4; c++) acc[c] += o[c];
                }
            float a = acc[3] / (G_SS * G_SS);
            uint8_t *p = out + ((size_t)y * w + x) * 4;
            if (a < 0.002f) continue;
            for (int c = 0; c < 3; c++) p[c] = (uint8_t)clampi((int)(acc[c] / (G_SS * G_SS) / a + 0.5f), 0, 255);
            p[3] = (uint8_t)clampi((int)(a * 255.0f + 0.5f), 0, 255);
        }
    free(g.h1.v);
    free(g.h2.v);
    return out;
}
