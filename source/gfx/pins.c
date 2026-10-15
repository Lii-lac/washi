// Push pins: see pins.h. The blob is the app's own (built in), so this only checks its sizes, not its pictures.
#include "pins.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "paper.h"

typedef struct { int w, h, ox, oy; const uint8_t *px; } PSpr;

static struct {
    const uint8_t *blob;
    size_t n;
    PinsHead hd;
    int ok;           // the head is sound
    int theme;        // the set at hand (-1: none)
    uint8_t *raw;     // its block, inflated
    PSpr pin[PINS_COLOURS][PINS_PIN_HS], shadow[PINS_SHADOW_HS], dent;
    float reach[PINS_SHADOW_HS][4]; // from the point, all that can be covered at a height up to shadow_h[i] (at scale 1)
} P = {.theme = -1};

static void drop(void)
{
    free(P.raw);
    P.raw = NULL;
    P.theme = -1;
}

void pins_set_blob(const void *blob, size_t n)
{
    drop();
    P.ok = 0;
    P.blob = blob;
    P.n = n;
    if (!blob || n < sizeof(PinsHead)) return;
    memcpy(&P.hd, blob, sizeof(PinsHead));
    if (P.hd.magic != PINS_MAGIC || P.hd.version != PINS_VERSION) return;
    size_t need = sizeof(PinsHead);
    for (int t = 0; t < PINS_THEMES; t++) need += P.hd.packed[t];
    P.ok = need <= n;
}

// the next sprite in the inflated block, or 0 if it runs past the end
static int take(const uint8_t **q, const uint8_t *end, PSpr *o)
{
    if (end - *q < 8) return 0;
    uint16_t w, h;
    int16_t ox, oy;
    memcpy(&w, *q, 2);
    memcpy(&h, *q + 2, 2);
    memcpy(&ox, *q + 4, 2);
    memcpy(&oy, *q + 6, 2);
    *q += 8;
    size_t bytes = (size_t)w * h * 4;
    if ((size_t)(end - *q) < bytes) return 0;
    o->w = w, o->h = h, o->ox = ox, o->oy = oy, o->px = *q;
    *q += bytes;
    return 1;
}

static void box_of(const PSpr *s, float dy, float b[4])
{
    float x0 = (float)-s->ox, y0 = (float)-s->oy + dy, x1 = x0 + s->w, y1 = y0 + s->h;
    if (x0 < b[0]) b[0] = x0;
    if (y0 < b[1]) b[1] = y0;
    if (x1 > b[2]) b[2] = x1;
    if (y1 > b[3]) b[3] = y1;
}

int pins_ready(void)
{
    if (!P.ok) return 0;
    const int theme = g_pal.board_scene == 2; // (Paper night)
    if (P.raw && P.theme == theme) return 1;
    drop();
    size_t off = sizeof(PinsHead);
    for (int t = 0; t < theme; t++) off += P.hd.packed[t];
    size_t got = 0;
    uint8_t *raw = gfx_inflate_raw(P.blob + off, P.hd.packed[theme], &got);
    if (!raw || got != P.hd.raw[theme]) {
        free(raw);
        P.ok = 0; // (broken: never tried again)
        return 0;
    }
    const uint8_t *q = raw, *end = raw + got;
    int good = 1;
    for (int c = 0; c < PINS_COLOURS && good; c++)
        for (int i = 0; i < PINS_PIN_HS && good; i++) good = take(&q, end, &P.pin[c][i]);
    for (int i = 0; i < PINS_SHADOW_HS && good; i++) good = take(&q, end, &P.shadow[i]);
    if (good) good = take(&q, end, &P.dent);
    if (!good) {
        free(raw);
        P.ok = 0;
        return 0;
    }
    P.raw = raw;
    P.theme = theme;
    // what can be covered up to each shadow height: the dent, every shadow so far, and the pin at every height so far (drawn k*h higher)
    float b[4] = {1e9f, 1e9f, -1e9f, -1e9f};
    box_of(&P.dent, 0, b);
    for (int i = 0; i < PINS_SHADOW_HS; i++) {
        box_of(&P.shadow[i], 0, b);
        const float h = P.hd.shadow_h[i];
        for (int j = 0; j < PINS_PIN_HS; j++) { // (whichever picture is drawn for this height, moved to it and grown a little: see pins_draw)
            const PSpr *s = &P.pin[0][j];
            float g = 1.0f + 0.004f * h, d = -P.hd.k * (h - P.hd.pin_h[j]);
            float e[4] = {-s->ox * g, -s->oy * g + d, (s->w - s->ox) * g, (s->h - s->oy) * g + d};
            if (e[0] < b[0]) b[0] = e[0];
            if (e[1] < b[1]) b[1] = e[1];
            if (e[2] > b[2]) b[2] = e[2];
            if (e[3] > b[3]) b[3] = e[3];
        }
        memcpy(P.reach[i], b, sizeof b);
    }
    return 1;
}

// a sprite with its anchor at (x, y), scaled about it
static void blit(Surf *s, const PSpr *p, float x, float y, float sc, float alpha)
{
    if (!p->px || alpha <= 0.004f) return;
    if (sc == 1.0f && alpha >= 0.999f && x == floorf(x) && y == floorf(y)) {
        gfx_blit_over(s, p->px, p->w, p->h, (int)x - p->ox, (int)y - p->oy);
        return;
    }
    gfx_blit_xform(s, p->px, p->w, p->h, x + (p->w * 0.5f - p->ox) * sc, y + (p->h * 0.5f - p->oy) * sc, sc, 0.0f, alpha);
}

void pins_draw(Surf *s, float x, float y, float h, float sc, int slot, float alpha)
{
    if (!P.raw) return;
    if (slot < 0 || slot >= PINS_COLOURS) slot = 0;
    int j = 0; // (the picture made at the nearest height: the pin itself hardly changes as it rises, only where it is drawn)
    for (int i = 1; i < PINS_PIN_HS; i++)
        if (fabsf(P.hd.pin_h[i] - h) < fabsf(P.hd.pin_h[j] - h)) j = i;
    blit(s, &P.pin[slot][j], x, y - P.hd.k * (h - P.hd.pin_h[j]) * sc, sc * (1.0f + 0.004f * h), alpha);
}

// How strong the shadow is laid: the Paper night set was rendered a lot darker than the light one (its alpha reaches 255 and it covers 1.8 times as much of
// the card under the pin), which on the console was a black smudge over the avatar
static const float SHADOW_GAIN[PINS_THEMES] = {0.85f, 0.35f};

// The shadow between two heights, mixed pixel by pixel (the shadows' colour is the same, so this is a lerp of their alpha). Two shadows laid one over the other
// with alphas (1 - f) and f are NOT that: where the shadow is dense they leave a lighter dip between two heights, so a pin whose height drifts across one
// (the rise's overshoot, a swing) made its shadow flicker. A bit of memory for the mix, got and let go of at each call: the bands are painted on several cores.
static void blit_mix(Surf *s, const PSpr *a, const PSpr *b, float f, float x, float y, float sc, float alpha)
{
    const int x0 = -(a->ox > b->ox ? a->ox : b->ox), y0 = -(a->oy > b->oy ? a->oy : b->oy);
    const int x1 = (a->w - a->ox > b->w - b->ox ? a->w - a->ox : b->w - b->ox), y1 = (a->h - a->oy > b->h - b->oy ? a->h - a->oy : b->h - b->oy);
    const int w = x1 - x0, h = y1 - y0;
    uint8_t *buf = malloc((size_t)w * h * 4);
    if (!buf) {
        blit(s, f < 0.5f ? a : b, x, y, sc, alpha);
        return;
    }
    const float wa = 1.0f - f, wb = f;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) {
            const int ax = i + x0 + a->ox, ay = j + y0 + a->oy, bx = i + x0 + b->ox, by = j + y0 + b->oy;
            const uint8_t *pa = ax >= 0 && ay >= 0 && ax < a->w && ay < a->h ? a->px + ((size_t)ay * a->w + ax) * 4 : NULL;
            const uint8_t *pb = bx >= 0 && by >= 0 && bx < b->w && by < b->h ? b->px + ((size_t)by * b->w + bx) * 4 : NULL;
            const float aa = pa ? pa[3] * wa : 0.0f, ab = pb ? pb[3] * wb : 0.0f, at = aa + ab;
            uint8_t *o = buf + ((size_t)j * w + i) * 4;
            if (at < 0.5f) {
                o[0] = o[1] = o[2] = o[3] = 0;
                continue;
            }
            for (int c = 0; c < 3; c++) o[c] = (uint8_t)((((pa ? pa[c] : 0) * aa + (pb ? pb[c] : 0) * ab) / at) + 0.5f);
            o[3] = (uint8_t)(at + 0.5f);
        }
    PSpr m = {w, h, -x0, -y0, buf};
    blit(s, &m, x, y, sc, alpha);
    free(buf);
}

void pins_draw_shadow(Surf *s, float x, float y, float h, float sc, float alpha)
{
    if (!P.raw) return;
    alpha *= SHADOW_GAIN[P.theme];
    const float *H = P.hd.shadow_h;
    if (h <= H[0]) {
        blit(s, &P.shadow[0], x, y, sc, alpha);
        return;
    }
    int i = 0;
    while (i < PINS_SHADOW_HS - 2 && h > H[i + 1]) i++;
    float f = (h - H[i]) / (H[i + 1] - H[i]);
    if (f > 1) f = 1;
    if (f < 0.02f) blit(s, &P.shadow[i], x, y, sc, alpha);
    else if (f > 0.98f) blit(s, &P.shadow[i + 1], x, y, sc, alpha);
    else blit_mix(s, &P.shadow[i], &P.shadow[i + 1], f, x, y, sc, alpha);
}

void pins_draw_dent(Surf *s, float x, float y, float sc, float alpha)
{
    if (P.raw) blit(s, &P.dent, x, y, sc, alpha);
}

void pins_reach(float h_max, float sc, float *x0, float *y0, float *x1, float *y1)
{
    *x0 = *y0 = *x1 = *y1 = 0;
    if (!P.raw) return;
    int i = 0;
    while (i < PINS_SHADOW_HS - 1 && P.hd.shadow_h[i] < h_max) i++;
    *x0 = P.reach[i][0] * sc - 2;
    *y0 = P.reach[i][1] * sc - 2;
    *x1 = P.reach[i][2] * sc + 2;
    *y1 = P.reach[i][3] * sc + 2;
}
