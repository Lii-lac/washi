#include "ui_spr.h"

#include <stdlib.h>
#include <string.h>

void spr_free_one(Spr *s)
{
    free(s->px);
    free(s->r0);
    free(s->r1);
    memset(s, 0, sizeof *s);
}


void spr_bake(Spr *s, int item_w, int item_h, SprDraw draw, void *ud)
{
    int w = item_w + 2 * SPR_M, h = item_h + 2 * SPR_M;
    Surf *k = surf_new(w, h), *wh = surf_new(w, h);
    gfx_fill(k, col(0, 0, 0, 1));
    gfx_fill(wh, col(1, 1, 1, 1));
    for (int pass = 0; pass < 2; pass++) {
        Cx c = ui_cx(pass ? wh : k);
        draw(&c, ud, SPR_M, SPR_M);
    }
    s->w = w;
    s->h = h;
    s->px = malloc((size_t)w * h * 4);
    s->r0 = malloc((size_t)h * sizeof *s->r0);
    s->r1 = malloc((size_t)h * sizeof *s->r1);
    for (int y = 0; y < h; y++) {
        const uint8_t *pb = k->px + (size_t)y * w * 4, *pw = wh->px + (size_t)y * w * 4;
        uint8_t *o = s->px + (size_t)y * w * 4;
        int first = -1, run_end = w;
        for (int x = 0; x < w; x++, pb += 4, pw += 4, o += 4) {
            int d = ((int)pw[0] - pb[0] + (int)pw[1] - pb[1] + (int)pw[2] - pb[2]) / 3, a = 255 - d;
            if (a < 0) a = 0;
            if (a > 255) a = 255;
            o[0] = pb[0] > a ? (uint8_t)a : pb[0];
            o[1] = pb[1] > a ? (uint8_t)a : pb[1];
            o[2] = pb[2] > a ? (uint8_t)a : pb[2];
            o[3] = (uint8_t)a;
            if (a >= 254) {
                if (first < 0) first = x;
            } else if (first >= 0 && run_end == w) {
                run_end = x;
            }
        }
        s->r0[y] = (int16_t)(first < 0 ? w : first);
        s->r1[y] = (int16_t)(first < 0 ? w : run_end);
    }
    surf_free(k);
    surf_free(wh);
}


// lays the picture over dst with its item's corner at (x, y), k (0..256) as strong
void spr_blit(Surf *dst, const Spr *s, int x, int y, int k)
{
    int ox = x - SPR_M, oy = y - SPR_M;
    for (int j = 0; j < s->h; j++) {
        int dy = oy + j;
        if (dy < 0 || dy >= dst->h) continue;
        int x0 = 0, x1 = s->w;
        if (ox + x0 < 0) x0 = -ox;
        if (ox + x1 > dst->w) x1 = dst->w - ox;
        if (x0 >= x1) continue;
        const uint8_t *p = s->px + ((size_t)j * s->w + x0) * 4;
        uint8_t *d = dst->px + ((size_t)dy * dst->w + ox + x0) * 4;
        if (k < 256) { // (faint: every pixel is mixed)
            for (int i = x0; i < x1; i++, p += 4, d += 4) {
                if (!p[3]) continue;
                int ia = 255 - (p[3] * k >> 8);
                d[0] = (uint8_t)((p[0] * k >> 8) + (d[0] * ia + 127) / 255);
                d[1] = (uint8_t)((p[1] * k >> 8) + (d[1] * ia + 127) / 255);
                d[2] = (uint8_t)((p[2] * k >> 8) + (d[2] * ia + 127) / 255);
            }
            continue;
        }
        int a0 = s->r0[j], a1 = s->r1[j];
        for (int i = x0; i < x1; i++, p += 4, d += 4) {
            if (i >= a0 && i < a1) {
                int n = (a1 < x1 ? a1 : x1) - i;
                memcpy(d, p, (size_t)n * 4);
                p += (size_t)(n - 1) * 4;
                d += (size_t)(n - 1) * 4;
                i += n - 1;
                continue;
            }
            if (!p[3]) continue;
            int ia = 255 - p[3];
            d[0] = (uint8_t)(p[0] + (d[0] * ia + 127) / 255);
            d[1] = (uint8_t)(p[1] + (d[1] * ia + 127) / 255);
            d[2] = (uint8_t)(p[2] + (d[2] * ia + 127) / 255);
        }
    }
}


// row y of `dst` taken `keep` (0..1) of the way from `ground` (the page without the items) to what is drawn
void fade_row(Surf *dst, const Surf *ground, int y, float keep)
{
    uint8_t *p = dst->px + (size_t)y * dst->w * 4;
    const uint8_t *g = ground->px + (size_t)y * ground->w * 4;
    int k = (int)(keep * 256);
    for (int x = 0; x < dst->w * 4; x++) p[x] = (uint8_t)(g[x] + (((int)p[x] - (int)g[x]) * k >> 8));
}


// the part of rows y0..y1 from column x0 to x1, put back as the page had it (fade 0) or faded to it from x0 to x1 (1: what is drawn
// at x0 stays and is gone by x1; 2: the other way round, for the left edge)
void ground_cols(Surf *dst, const Surf *ground, int y0, int y1, int x0, int x1, int fade)
{
    if (y0 < 0) y0 = 0;
    if (y1 > dst->h) y1 = dst->h;
    for (int y = y0; y < y1; y++) {
        uint8_t *p = dst->px + ((size_t)y * dst->w + x0) * 4;
        const uint8_t *g = ground->px + ((size_t)y * ground->w + x0) * 4;
        for (int x = x0; x < x1; x++, p += 4, g += 4) {
            int k = 0;
            if (fade) {
                float a = (float)(x - x0) / (float)(x1 - x0);
                if (fade == 1) a = 1.0f - a;
                k = (int)(a * 256);
            }
            for (int ch = 0; ch < 3; ch++) p[ch] = (uint8_t)(g[ch] + (((int)p[ch] - (int)g[ch]) * k >> 8));
        }
    }
}

