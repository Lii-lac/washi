// Drawing paper in three dimensions (see ui_fx.h).
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ui_fx.h"

#define LIGHT_UP 0.35 // the light comes from in front and a little above: (0, -LIGHT_UP, 1)
#define AMBIENT 0.68  // what a face turned right away from the light still gets, of a flat card's light (a room's light comes from all round)

static int imin(int a, int b) { return a < b ? a : b; }
static int imax(int a, int b) { return a > b ? a : b; }

float fx_light(double ny, double nz)
{
    double l = sqrt(1 + LIGHT_UP * LIGHT_UP), d = (-LIGHT_UP * ny + nz) / l;
    double b = (AMBIENT + (1 - AMBIENT) * (d > 0 ? d : 0)) / (AMBIENT + (1 - AMBIENT) / l);
    return (float)(b > 1.08 ? 1.08 : b);
}

void fx_premultiply(PaperSpr *p)
{
    for (size_t i = 0, n = (size_t)p->w * p->h; i < n; i++) {
        uint8_t *q = p->px + i * 4;
        for (int c = 0; c < 3; c++) q[c] = (uint8_t)((q[c] * q[3] + 127) / 255);
    }
}

// what of a premultiplied sprite is at (sx, sy) (between pixels, as its four neighbours mix), added to acc (0..255, premultiplied)
static inline void sample(const PaperSpr *sp, float sx, float sy, float acc[4])
{
    int x0 = (int)(sx + 4096.0f) - 4096, y0 = (int)(sy + 4096.0f) - 4096; // (rounded down, below 0 too)
    float fx = sx - (float)x0, fy = sy - (float)y0, w11 = fx * fy, w10 = fx - w11, w01 = fy - w11, w00 = 1.0f - fx - fy + w11;
    if ((unsigned)x0 < (unsigned)(sp->w - 1) && (unsigned)y0 < (unsigned)(sp->h - 1)) { // (all four inside: nearly always)
        const uint8_t *p = sp->px + ((size_t)y0 * sp->w + x0) * 4, *q = p + (size_t)sp->w * 4;
        for (int c = 0; c < 4; c++) acc[c] += p[c] * w00 + p[4 + c] * w10 + q[c] * w01 + q[4 + c] * w11;
        return;
    }
    const float wt[4] = {w00, w10, w01, w11};
    for (int t = 0; t < 4; t++) {
        int x = x0 + (t & 1), y = y0 + (t >> 1);
        if (x < 0 || y < 0 || x >= sp->w || y >= sp->h) continue;
        const uint8_t *p = sp->px + ((size_t)y * sp->w + x) * 4;
        for (int c = 0; c < 4; c++) acc[c] += p[c] * wt[t];
    }
}

// lays what was gathered (acc: n samples' worth, premultiplied 0..255) over a pixel, its colour times light, alpha as strong
static inline void lay(uint8_t *d, const float acc[4], float n, float light, float alpha)
{
    float a = acc[3] / (255.0f * n) * alpha;
    if (a <= 0.002f) return;
    if (a > 1) a = 1;
    float k = light * alpha / n;
    for (int c = 0; c < 3; c++) {
        float v = acc[c] * k;
        if (v > 255 * a) v = 255 * a; // (more light than the paper can give back)
        float o = d[c] * (1 - a) + v;
        d[c] = (uint8_t)(o < 0 ? 0 : o > 255 ? 255 : o + 0.5f);
    }
}

// ---- curling paper

// the paper s px past the fold line (measured along it, flat): how far past the line it shows (d), how high it is (z), its slope (th)
static void curl_at(const FxFold *k, double s, double *d, double *z, double *th)
{
    double arc = k->r * k->wrap;
    if (s <= 0) {
        *d = s, *z = 0, *th = 0;
    } else if (s < arc) {
        *th = s / k->r;
        *d = k->r * sin(*th);
        *z = k->r * (1 - cos(*th));
    } else {
        *th = k->wrap;
        *d = k->r * sin(k->wrap) + (s - arc) * cos(k->wrap);
        *z = k->r * (1 - cos(k->wrap)) + (s - arc) * sin(k->wrap);
    }
}

void fx_curl(Surf *dst, const PaperSpr *front, const PaperSpr *back, double cx, double cy, double sc, double ang, const FxFold *fold,
             uint32_t shadow_rgb, float shadow_a, Rect clip)
{
    fx_curl_a(dst, front, back, cx, cy, sc, ang, fold, shadow_rgb, shadow_a, 1.0f, clip);
}

void fx_curl_a(Surf *dst, const PaperSpr *front, const PaperSpr *back, double cx, double cy, double sc, double ang, const FxFold *fold,
               uint32_t shadow_rgb, float shadow_a, float alpha, Rect clip)
{
    if (!front || sc <= 0 || alpha <= 0.002f) return;
    FxFold k = *fold;
    if (k.r < 0.5) k.r = 0.5;
    if (k.wrap < 0) k.wrap = 0;
    if (k.wrap > 3.1) k.wrap = 3.1;
    if (fabs(k.wrap - 1.5707963) < 2e-3) k.wrap = 1.5707963 - 2e-3; // (stood straight up it would show as a line)
    const double ca = cos(ang), sa = sin(ang), hw = front->w * 0.5, hh = front->h * 0.5;
    const double sw_ = sin(k.wrap), cw_ = cos(k.wrap), w1 = k.wrap < 1.5707963 ? k.wrap : 1.5707963, d1 = k.r * sin(w1);
    // how far it reaches: the card's edge, each point where the curl puts it, on the screen
    double X0 = 1e9, Y0 = 1e9, X1 = -1e9, Y1 = -1e9, zmax = 0;
    for (int e = 0; e < 4; e++)
        for (int i = 0; i <= 64; i++) {
            double t = i / 64.0, u = e < 2 ? -hw + 2 * hw * t : (e == 2 ? -hw : hw), v = e < 2 ? (e == 0 ? -hh : hh) : -hh + 2 * hh * t;
            double s = (u - k.lx) * k.nx + (v - k.ly) * k.ny, d, z, th;
            curl_at(&k, s, &d, &z, &th);
            u += (d - s) * k.nx;
            v += (d - s) * k.ny;
            double X = cx + sc * (u * ca - v * sa), Y = cy + sc * (u * sa + v * ca);
            X0 = fmin(X0, X), Y0 = fmin(Y0, Y), X1 = fmax(X1, X), Y1 = fmax(Y1, Y), zmax = fmax(zmax, z);
        }
    const double grow = k.r * sc + 2, drop = 1.5 + 0.3 * zmax * sc + 4; // (the roll bulges past the edge points; the shadow falls below)
    int bx0 = imax(imax((int)floor(X0 - grow), clip.x), 0), bx1 = imin(imin((int)ceil(X1 + grow), clip.x + clip.w), dst->w);
    int by0 = imax(imax((int)floor(Y0 - grow), clip.y), 0), by1 = imin(imin((int)ceil(Y1 + grow + drop), clip.y + clip.h), dst->h);
    int BW = bx1 - bx0, BH = by1 - by0;
    if (BW <= 0 || BH <= 0) return;
    float *up = calloc((size_t)BW * BH, 4 * sizeof(float)); // what is lifted: its colour, premultiplied
    float *shd = calloc((size_t)BW * BH, sizeof(float));
    float *tmp = calloc((size_t)BW * BH, sizeof(float));
    if (!up || !shd || !tmp) {
        free(up), free(shd), free(tmp);
        return;
    }
    const double nsy = k.nx * sa + k.ny * ca; // (the free side's way, down the screen: how the light falls on the roll)
    int any = 0;
    for (int Y = by0; Y < by1; Y++) {
        uint8_t *drow = dst->px + (size_t)Y * dst->w * 4;
        for (int X = bx0; X < bx1; X++) {
            double dx = (X + 0.5 - cx) / sc, dy = (Y + 0.5 - cy) / sc, u = dx * ca + dy * sa, v = -dx * sa + dy * ca;
            double d = (u - k.lx) * k.nx + (v - k.ly) * k.ny;
            if (d < 0) { // lying flat
                float acc[4] = {0, 0, 0, 0};
                sample(front, (float)(u + hw - 0.5), (float)(v + hh - 0.5), acc);
                lay(drow + (size_t)X * 4, acc, 1.0f, 1.0f, alpha);
            }
            if (d > k.r && k.wrap > 1.5707963) continue; // (past the top of the roll nothing is left, unless it goes on up and out)
            float *o = up + ((size_t)(Y - by0) * BW + (X - bx0)) * 4;
            for (int layer = 0; layer < 2; layer++) { // the roll's near half, its front up; then what comes over past the top, its back up
                double s;
                if (layer == 0) {
                    if (d < 0) continue;
                    if (d <= d1) s = k.r * asin(d / k.r);
                    else if (k.wrap < 1.5707963) s = k.r * k.wrap + (d - k.r * sw_) / cw_;
                    else continue;
                } else {
                    if (k.wrap <= 1.5707963 || !back) continue;
                    if (d >= k.r * sw_) s = k.r * (3.14159265358979 - asin(d / k.r));
                    else s = k.r * k.wrap + (d - k.r * sw_) / cw_;
                }
                double dd, z, th;
                curl_at(&k, s, &dd, &z, &th);
                double mu = u + (s - d) * k.nx, mv = v + (s - d) * k.ny;
                float acc[4] = {0, 0, 0, 0};
                sample(layer ? back : front, (float)(mu + hw - 0.5), (float)(mv + hh - 0.5), acc);
                if (alpha < 1.0f)
                    for (int c = 0; c < 4; c++) acc[c] *= alpha; // (fading: it and its shadow both)
                if (acc[3] < 0.5f) continue;
                float l = layer ? fx_light(sin(th) * nsy, -cos(th)) : fx_light(-sin(th) * nsy, cos(th));
                float a = acc[3] / 255.0f;
                if (a > 1) a = 1;
                for (int c = 0; c < 3; c++) { // (over what the near half put here)
                    float v2 = acc[c] * l;
                    if (v2 > 255 * a) v2 = 255 * a;
                    o[c] = v2 + o[c] * (1 - a);
                }
                o[3] = a + o[3] * (1 - a);
                any = 1;
                // its shadow, on what is under it: fainter where it has hardly left the paper
                float fy = (float)((Y - by0) + 1.0 + 0.3 * z * sc), w = a * (float)fmin(1.0, z / 4.0);
                int iy = (int)floorf(fy);
                float fr = fy - iy;
                if (iy >= 0 && iy < BH) shd[(size_t)iy * BW + (X - bx0)] += w * (1 - fr);
                if (iy + 1 >= 0 && iy + 1 < BH) shd[(size_t)(iy + 1) * BW + (X - bx0)] += w * fr;
            }
        }
    }
    if (any) {
        // the shadow softened (a box blur of 2 px, twice) and laid down, then what is lifted over it
        for (int pass = 0; pass < 2; pass++) {
            for (int y = 0; y < BH; y++)
                for (int x = 0; x < BW; x++) {
                    float sum = 0;
                    for (int j = -2; j <= 2; j++) sum += x + j >= 0 && x + j < BW ? shd[(size_t)y * BW + x + j] : 0;
                    tmp[(size_t)y * BW + x] = sum / 5;
                }
            for (int y = 0; y < BH; y++)
                for (int x = 0; x < BW; x++) {
                    float sum = 0;
                    for (int j = -2; j <= 2; j++) sum += y + j >= 0 && y + j < BH ? tmp[(size_t)(y + j) * BW + x] : 0;
                    shd[(size_t)y * BW + x] = sum / 5;
                }
        }
        const float sr = (float)((shadow_rgb >> 16) & 255), sg = (float)((shadow_rgb >> 8) & 255), sb = (float)(shadow_rgb & 255);
        for (int y = 0; y < BH; y++) {
            uint8_t *d = dst->px + ((size_t)(y + by0) * dst->w + bx0) * 4;
            for (int x = 0; x < BW; x++, d += 4) {
                float a = shd[(size_t)y * BW + x];
                if (a > 1) a = 1;
                a *= shadow_a;
                if (a > 0.002f) {
                    d[0] = (uint8_t)(d[0] + (sr - d[0]) * a + 0.5f);
                    d[1] = (uint8_t)(d[1] + (sg - d[1]) * a + 0.5f);
                    d[2] = (uint8_t)(d[2] + (sb - d[2]) * a + 0.5f);
                }
                const float *o = up + ((size_t)y * BW + x) * 4;
                if (o[3] <= 0.002f) continue;
                for (int c = 0; c < 3; c++) {
                    float v2 = d[c] * (1 - o[3]) + o[c];
                    d[c] = (uint8_t)(v2 < 0 ? 0 : v2 > 255 ? 255 : v2 + 0.5f);
                }
            }
        }
    }
    free(up), free(shd), free(tmp);
}

void fx_affine(Surf *dst, const PaperSpr *sp, double cx, double cy, double sc, double ang, double margin, float alpha, Rect clip)
{
    if (!sp || sc <= 0 || alpha <= 0.002f) return;
    double c = cos(ang), s = sin(ang), hw = sp->w * 0.5, hh = sp->h * 0.5, X0 = 1e9, Y0 = 1e9, X1 = -1e9, Y1 = -1e9;
    for (int k = 0; k < 4; k++) {
        double u = (k & 1 ? 1 : -1) * (hw - margin), v = (k & 2 ? 1 : -1) * (hh - margin), X = cx + sc * (u * c - v * s), Y = cy + sc * (u * s + v * c);
        X0 = fmin(X0, X);
        Y0 = fmin(Y0, Y);
        X1 = fmax(X1, X);
        Y1 = fmax(Y1, Y);
    }
    int x0 = imax(imax((int)floor(X0) - 1, clip.x), 0), x1 = imin(imin((int)ceil(X1) + 1, clip.x + clip.w), dst->w);
    int y0 = imax(imax((int)floor(Y0) - 1, clip.y), 0), y1 = imin(imin((int)ceil(Y1) + 1, clip.y + clip.h), dst->h);
    float du = (float)(c / sc), dv = (float)(-s / sc);
    for (int y = y0; y < y1; y++) {
        double dx = (x0 + 0.5 - cx) / sc, dy = (y + 0.5 - cy) / sc;
        float u = (float)(hw - 0.5 + dx * c + dy * s), v = (float)(hh - 0.5 - dx * s + dy * c);
        uint8_t *d = dst->px + ((size_t)y * dst->w + x0) * 4;
        for (int x = x0; x < x1; x++, d += 4, u += du, v += dv) {
            float acc[4] = {0, 0, 0, 0};
            sample(sp, u, v, acc);
            lay(d, acc, 1.0f, 1.0f, alpha);
        }
    }
}

static float phi(float x) { return 1.0f / (1.0f + expf(-1.702f * x)); } // (the normal curve's area up to x, near enough)

void fx_soft_box(Surf *dst, float x0, float y0, float x1, float y1, float sigma, uint32_t rgb, float a, Rect clip)
{
    static float col[1280];
    if (a <= 0.002f || x1 <= x0 || y1 <= y0) return;
    float mg = sigma * 3 + 1;
    int X0 = imax(imax((int)floorf(x0 - mg), clip.x), 0), X1 = imin(imin((int)ceilf(x1 + mg), clip.x + clip.w), dst->w);
    int Y0 = imax(imax((int)floorf(y0 - mg), clip.y), 0), Y1 = imin(imin((int)ceilf(y1 + mg), clip.y + clip.h), dst->h);
    if (X1 <= X0 || Y1 <= Y0 || X1 - X0 > 1280) return;
    for (int x = X0; x < X1; x++) col[x - X0] = phi((x + 0.5f - x0) / sigma) - phi((x + 0.5f - x1) / sigma);
    float cr = (float)((rgb >> 16) & 255), cg = (float)((rgb >> 8) & 255), cb = (float)(rgb & 255);
    for (int y = Y0; y < Y1; y++) {
        float ay = a * (phi((y + 0.5f - y0) / sigma) - phi((y + 0.5f - y1) / sigma));
        if (ay < 0.002f) continue;
        uint8_t *d = dst->px + ((size_t)y * dst->w + X0) * 4;
        for (int x = X0; x < X1; x++, d += 4) {
            float k = ay * col[x - X0];
            d[0] = (uint8_t)(d[0] + (cr - d[0]) * k + 0.5f);
            d[1] = (uint8_t)(d[1] + (cg - d[1]) * k + 0.5f);
            d[2] = (uint8_t)(d[2] + (cb - d[2]) * k + 0.5f);
        }
    }
}

