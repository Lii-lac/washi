// The order menu's sleeve of stickers, and the stickers' art (see ui_stickfx.h). Lengths are screen pixels (half a millimetre of board), times
// seconds.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __SWITCH__
#include <switch.h> // (armGetSystemTick)
#endif
#include "ui_internal.h"
#include "ui_stickfx.h"
#include "ui_scene.h"

#define PI_D 3.14159265358979323846

static double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
static double smooth(double t) { t = clampd(t, 0, 1); return t * t * (3 - 2 * t); }

// ---- the art

enum { K_DOT, K_CLOCK, K_ABC, K_NEW, K_STACK, K_DOWN, K_GRID, K_CARD };

int stk_kind(const char *l)
{
    if (!l) return K_DOT;
    if (strstr(l, "played")) return K_CLOCK;
    if (strstr(l, "A to Z")) return K_ABC;
    if (strstr(l, "Newest")) return K_NEW;
    if (strstr(l, "Most games")) return K_STACK;
    if (strstr(l, "downloaded")) return K_DOWN;
    if (strstr(l, "Most icons")) return K_GRID;
    if (strstr(l, "card")) return K_CARD;
    return K_DOT;
}

uint32_t stk_rgb(int idx)
{
    static const int PICK[6] = {5, 3, 6, 2, 0, 4}; // (blue, green, lilac, mint, pink, peach: the clearest under dark writing, in the menu's order)
    return paper_tape_rgb[PICK[(idx < 0 ? 0 : idx) % 6]];
}

static inline void px_over(Surf *s, int x, int y, Col k, float a)
{
    if ((unsigned)x >= (unsigned)s->w || (unsigned)y >= (unsigned)s->h || a <= 0.0f) return;
    uint8_t *p = s->px + ((size_t)y * s->w + x) * 4;
    float ia = 1.0f - a;
    p[0] = (uint8_t)(p[0] * ia + k.r * 255.0f * a + 0.5f);
    p[1] = (uint8_t)(p[1] * ia + k.g * 255.0f * a + 0.5f);
    p[2] = (uint8_t)(p[2] * ia + k.b * 255.0f * a + 0.5f);
    p[3] = 255;
}

// a polygon filled (even-odd), each pixel 4 x 4 samples
static void poly(Surf *s, const float *xy, int n, Col k)
{
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (int i = 0; i < n; i++) {
        x0 = fminf(x0, xy[2 * i]), x1 = fmaxf(x1, xy[2 * i]);
        y0 = fminf(y0, xy[2 * i + 1]), y1 = fmaxf(y1, xy[2 * i + 1]);
    }
    int X0 = (int)floorf(x0), X1 = (int)ceilf(x1), Y0 = (int)floorf(y0), Y1 = (int)ceilf(y1);
    if (X0 < 0) X0 = 0;
    if (Y0 < 0) Y0 = 0;
    if (X1 > s->w) X1 = s->w;
    if (Y1 > s->h) Y1 = s->h;
    for (int y = Y0; y < Y1; y++)
        for (int x = X0; x < X1; x++) {
            int in = 0;
            for (int q = 0; q < 16; q++) {
                float sx = x + ((q & 3) + 0.5f) / 4, sy = y + ((q >> 2) + 0.5f) / 4;
                int c = 0;
                for (int i = 0, j = n - 1; i < n; j = i++) {
                    float xi = xy[2 * i], yi = xy[2 * i + 1], xj = xy[2 * j], yj = xy[2 * j + 1];
                    if ((yi > sy) != (yj > sy) && sx < (xj - xi) * (sy - yi) / (yj - yi) + xi) c = !c;
                }
                in += c;
            }
            if (in) px_over(s, x, y, k, k.a * in / 16.0f);
        }
}

// a polygon's outline (closed)
static void poly_line(Surf *s, const float *xy, int n, float w, Col k)
{
    for (int i = 0, j = n - 1; i < n; j = i++) gfx_line(s, xy[2 * j], xy[2 * j + 1], xy[2 * i], xy[2 * i + 1], w, k);
    for (int i = 0; i < n; i++) gfx_circle(s, xy[2 * i], xy[2 * i + 1], w * 0.5f, k); // (round joins)
}

// a burst: m points, out to ro, in to ri, the first point straight up, about (cx, cy)
static int burst(float *xy, int m, float cx, float cy, float ro, float ri)
{
    for (int i = 0; i < 2 * m; i++) {
        float a = (float)(PI_D * i / m - PI_D / 2), r = i & 1 ? ri : ro;
        xy[2 * i] = cx + r * cosf(a);
        xy[2 * i + 1] = cy + r * sinf(a);
    }
    return 2 * m;
}

// an SD card's outline about (cx, cy): hw x hh half sizes, its top right corner cut off (c px), grown by g
static int sd_card(float *xy, float cx, float cy, float hw, float hh, float c, float g)
{
    hw += g, hh += g;
    float k = c + g * 0.4142f; // (the cut, moved out with the edges)
    const float p[10] = {cx - hw, cy - hh, cx + hw - k, cy - hh, cx + hw, cy - hh + k, cx + hw, cy + hh, cx - hw, cy + hh};
    memcpy(xy, p, sizeof p);
    return 5;
}

typedef struct {
    float x, y, w, h, bx, by, rb; // the box; the badge's middle and size
    int kind;
} Shape;

static Shape shape_of(float x, float y, float w, float h, const char *label)
{
    Shape o = {x, y, w, h, x + h * 0.5f, y + h * 0.5f, h * 0.5f, stk_kind(label)};
    return o;
}

// the sticker's outline, grown by g: the badge at its left end and the label part (a rounded rectangle from the badge's middle on)
static void silhouette(Surf *s, const Shape *o, float g, Col k)
{
    const float hb = o->h - 6;
    gfx_rrect(s, o->bx - g, o->y + 3 - g, o->x + o->w - o->bx + 2 * g, hb + 2 * g, 9 + g, k);
    float xy[64];
    switch (o->kind) {
    case K_ABC:
    case K_STACK:
    case K_GRID:
        gfx_rrect(s, o->bx - o->rb * 0.94f - g, o->by - o->rb * 0.94f - g, o->rb * 1.88f + 2 * g, o->rb * 1.88f + 2 * g, 7 + g, k);
        break;
    case K_NEW:
        poly(s, xy, burst(xy, 12, o->bx, o->by, o->rb + 1.6f + g, o->rb * 0.80f + g), k);
        break;
    case K_CARD:
        poly(s, xy, sd_card(xy, o->bx, o->by, o->rb * 0.80f, o->rb, o->rb * 0.42f, g), k);
        break;
    default:
        gfx_circle(s, o->bx, o->by, o->rb + g, k);
    }
}

// the sticker's colour printed as a deeper spot colour: its hue, the lightness of a printing ink, a calm saturation (yellow goes golden: a dark
// yellow is olive)
static uint32_t deepen(uint32_t rgb)
{
    float r = ((rgb >> 16) & 255) / 255.0f, g = ((rgb >> 8) & 255) / 255.0f, b = (rgb & 255) / 255.0f;
    float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), l = (mx + mn) * 0.5f, d = mx - mn, h = 0, s = 0;
    if (d > 1e-4f) {
        s = l > 0.5f ? d / (2 - mx - mn) : d / (mx + mn);
        if (mx == r) h = (g - b) / d + (g < b ? 6 : 0);
        else if (mx == g) h = (b - r) / d + 2;
        else h = (r - g) / d + 4;
        h /= 6;
    }
    if (h > 0.10f && h < 0.20f) h = 0.10f + (h - 0.10f) * 0.25f;
    float L = pal_light(PAL(board, PAPER_BOARD)) ? 0.54f : 0.46f, S = fminf(0.42f, s * 0.6f + 0.12f);
    float q = L < 0.5f ? L * (1 + S) : L + S - L * S, p = 2 * L - q, o[3];
    for (int k = 0; k < 3; k++) {
        float t = h + (1 - k) / 3.0f;
        if (t < 0) t += 1;
        if (t > 1) t -= 1;
        o[k] = t < 1 / 6.0f ? p + (q - p) * 6 * t : t < 0.5f ? q : t < 2 / 3.0f ? p + (q - p) * (2 / 3.0f - t) * 6 : p;
    }
    return ((uint32_t)(o[0] * 255 + 0.5f) << 16) | ((uint32_t)(o[1] * 255 + 0.5f) << 8) | (uint32_t)(o[2] * 255 + 0.5f);
}

// the badge's own colour, and the little picture printed on it (in the paper's cream)
static void badge(Cx *c, const Shape *o, uint32_t rgb)
{
    Surf *s = c->s;
    const float bx = o->bx, by = o->by, r = o->rb, k = fmaxf(0.55f, r / 18.0f); // (k: lines as thick for a smaller badge, a tab's)
    uint32_t deep = deepen(rgb); // (the same colour, printed deeper)
    Col ink = hex(0xFFFBF1, 1), dk = hex(deep, 1);
    float xy[64];
    switch (o->kind) {
    case K_ABC:
    case K_STACK:
    case K_GRID:
        gfx_rrect_grad(s, bx - r * 0.94f, by - r * 0.94f, r * 1.88f, r * 1.88f, 7 * k, hex(pal_mix(deep, 0xFFFFFF, 0.10f), 1), dk);
        break;
    case K_NEW:
        poly(s, xy, burst(xy, 12, bx, by, r + 1.6f * k, r * 0.80f), dk);
        break;
    case K_CARD:
        poly(s, xy, sd_card(xy, bx, by, r * 0.80f, r, r * 0.42f, 0), dk);
        break;
    default:
        gfx_circle_grad(s, bx, by, r, hex(pal_mix(deep, 0xFFFFFF, 0.10f), 1), dk);
    }
    switch (o->kind) {
    case K_CLOCK:
        gfx_ring(s, bx, by, r * 0.64f, 2.1f * k, ink);
        gfx_line(s, bx, by, bx, by - r * 0.44f, 2.2f * k, ink);
        gfx_line(s, bx, by, bx + r * 0.30f, by + r * 0.12f, 2.2f * k, ink);
        gfx_circle(s, bx, by, 1.9f * k, ink);
        break;
    case K_ABC:
        skin_txtc(c, c->skin->semi, r * 1.05f, 0, bx, by + 0.5f, 1, ink, "A");
        gfx_line(s, bx + r * 0.42f, by + r * 0.20f, bx + r * 0.42f, by + r * 0.62f, 1.6f * k, ink); // (a little arrow down beside it)
        gfx_line(s, bx + r * 0.30f, by + r * 0.48f, bx + r * 0.42f, by + r * 0.64f, 1.6f * k, ink);
        gfx_line(s, bx + r * 0.54f, by + r * 0.48f, bx + r * 0.42f, by + r * 0.64f, 1.6f * k, ink);
        break;
    case K_NEW: {
        poly(s, xy, burst(xy, 4, bx - r * 0.08f, by + r * 0.06f, r * 0.56f, r * 0.15f), ink);
        poly(s, xy, burst(xy, 4, bx + r * 0.40f, by - r * 0.40f, r * 0.24f, r * 0.07f), ink);
        break;
    }
    case K_STACK:
        for (int i = 0; i < 3; i++) { // three game cards, fanned from the bottom
            float a = (float)((i - 1) * 0.30), ca = cosf(a), sa = sinf(a), pxx = bx, pyy = by + r * 0.62f, hw = r * 0.34f, hh = r * 0.98f;
            float q[8], u[4] = {-hw, hw, hw, -hw}, v[4] = {-hh, -hh, 0, 0};
            for (int k = 0; k < 4; k++) {
                q[2 * k] = pxx + u[k] * ca - v[k] * sa;
                q[2 * k + 1] = pyy + u[k] * sa + v[k] * ca;
            }
            poly(s, q, 4, hex(i == 2 ? 0xFFFBF1 : pal_mix(deep, 0xFFFBF1, 0.55f + 0.2f * i), 1));
            poly_line(s, q, 4, 1.3f * k, dk);
        }
        break;
    case K_DOWN:
        gfx_line(s, bx, by - r * 0.52f, bx, by + r * 0.06f, 2.3f * k, ink);
        xy[0] = bx - r * 0.30f, xy[1] = by - r * 0.04f, xy[2] = bx + r * 0.30f, xy[3] = by - r * 0.04f, xy[4] = bx, xy[5] = by + r * 0.30f;
        poly(s, xy, 3, ink);
        gfx_line(s, bx - r * 0.50f, by + r * 0.14f, bx - r * 0.50f, by + r * 0.48f, 2.1f * k, ink);
        gfx_line(s, bx - r * 0.50f, by + r * 0.48f, bx + r * 0.50f, by + r * 0.48f, 2.1f * k, ink);
        gfx_line(s, bx + r * 0.50f, by + r * 0.48f, bx + r * 0.50f, by + r * 0.14f, 2.1f * k, ink);
        break;
    case K_GRID: {
        const int TC[4] = {0, 3, 5, 6};
        for (int i = 0; i < 4; i++) {
            float qx = bx + (i & 1 ? 0.08f : -0.58f) * r, qy = by + (i & 2 ? 0.08f : -0.58f) * r;
            gfx_rrect(s, qx, qy, r * 0.50f, r * 0.50f, 2.5f * k, hex(pal_mix(paper_tape_rgb[TC[i]], 0xFFFFFF, 0.15f), 1));
        }
        break;
    }
    case K_CARD:
        for (int i = 0; i < 4; i++) gfx_rect(s, bx - r * 0.48f + i * r * 0.22f, by - r * 0.70f, r * 0.10f, r * 0.30f, ink);
        gfx_rrect_stroke(s, bx - r * 0.42f, by - r * 0.10f, r * 0.84f, r * 0.62f, 2.5f * k, 1.4f * k, with_a(ink, 0.8f));
        break;
    default:
        gfx_circle(s, bx, by, r * 0.22f, ink);
    }
    // the gloss on it, from above
    gfx_blob(s, bx - r * 0.30f, by - r * 0.52f, r * 0.42f, r * 0.20f, hex(0xFFFFFF, 0.16f));
}

// a badge on its own (a pad's tab): its middle at (bx, by), r its size
void stk_badge(Cx *c, float bx, float by, float r, const char *label, int idx)
{
    Shape o = {bx - r, by - r, 2 * r, 2 * r, bx, by, r, stk_kind(label)};
    badge(c, &o, stk_rgb(idx));
}

void stk_sticker(Cx *c, float x, float y, float w, float h, const char *label, int idx)
{
    Surf *s = c->s;
    Shape o = shape_of(x, y, w, h, label);
    int light = pal_light(PAL(board, PAPER_BOARD));
    // it stands a hair off the paper
    gfx_shadow(s, o.bx, y + 3, x + w - o.bx, h - 6, 9, 0.9f, 1.6f, col(0, 0, 0, 0.15f));
    gfx_shadow(s, o.bx - o.rb, o.by - o.rb, o.rb * 2, o.rb * 2, o.rb, 0.9f, 1.6f, col(0, 0, 0, 0.15f));
    silhouette(s, &o, 3.0f, hex(light ? 0xFFFFFF : 0xDAD6CB, 1)); // the white the die cut leaves round it (dimmer by night)
    uint32_t k = stk_rgb(idx);
    const float hb = h - 6, lx = o.bx + o.rb * 0.6f;
    gfx_rrect_grad(s, lx, y + 3, x + w - lx, hb, 8, hex(pal_mix(k, 0xFFFFFF, 0.22f), 1), hex(pal_mix(k, 0x000000, 0.03f), 1)); // printed
    gfx_rrect_grad(s, lx + 1, y + 4, x + w - lx - 3, hb * 0.42f, 6, hex(0xFFFFFF, 0.32f), hex(0xFFFFFF, 0.04f));              // its gloss
    badge(c, &o, k);
    float tx = o.bx + o.rb + 7, room = x + w - 10 - tx, size = 20, tw = text_width(c->skin->semi, 20, 0, label);
    if (tw > room && tw > 0) size = fmaxf(14.5f, 20 * room / tw); // (a long name printed smaller, to fit)
    skin_txtc(c, c->skin->semi, size, 0, tx, o.by + 0.5f, 0, hex(0x2D2D2D, 1), label); // (dark ink in both themes)
}

void stk_back(Cx *c, float x, float y, float w, float h, const char *label)
{
    Shape o = shape_of(x, y, w, h, label);
    int light = pal_light(PAL(board, PAPER_BOARD));
    silhouette(c->s, &o, 3.0f, hex(light ? 0xDCDAD2 : 0xA6A297, 1));
    silhouette(c->s, &o, 2.0f, hex(light ? 0xF4F3EE : 0xBDB9AE, 1));
}

void stk_place(Cx *c, float x, float y, float w, float h, const char *label)
{
    Shape o = shape_of(x, y, w, h, label);
    int light = pal_light(PAL(board, PAPER_BOARD));
    silhouette(c->s, &o, 3.5f, col(0, 0, 0, light ? 0.20f : 0.45f)); // the cut
    silhouette(c->s, &o, 2.7f, hex(light ? 0xE6E7E5 : 0x2A2A3B, 1)); // the liner, glossy
    gfx_rect(c->s, o.bx, y + h - 4, x + w - o.bx - 10, 1.2f, hex(0xFFFFFF, light ? 0.55f : 0.10f));
    Col pr = hex(light ? 0xC6C8C3 : 0x45455A, 1); // (its maker's name printed on it, over and over, as on the back of a sheet)
    float tx = o.bx + o.rb * 0.4f, tw = text_width(c->skin->med, 12, 0.5f, "washi"), end = x + w - 12;
    for (int k = 0; tx + tw <= end && k < 8; k++, tx += tw + 14) skin_txtc(c, c->skin->med, 12, 0.5f, tx, o.by + 0.5f, 0, pr, "washi");
}

// The order stickers are printed a little soft, wherever they are (on the chips, in the sleeve, on their way to the chip): their colours a
// touch greyed and washed toward the paper's, so the chip's sticker is plain to see without shouting over the page, with the focus or not.
#define STK_TONE 0.45f // (1 is the old quiet print, too pale; 0 full colour, too loud)
// On the dark paper the same wash muddies them (the paper they are washed toward is dark): there they are only a little greyed and washed.
#define STK_GREY_DARK 0.12f
#define STK_WASH_DARK 0.05f

static void tone(PaperSpr *p) // (straight alpha)
{
    const uint32_t pp = PAL(paper, PAPER_PAPER);
    const int light = pal_light(PAL(board, PAPER_BOARD));
    const float pr = (float)((pp >> 16) & 255), pg = (float)((pp >> 8) & 255), pb = (float)(pp & 255);
    const float d = light ? 0.55f * STK_TONE : STK_GREY_DARK, m = light ? 0.40f * STK_TONE : STK_WASH_DARK;
    for (size_t i = 0; i < (size_t)p->w * p->h; i++) {
        uint8_t *x = p->px + i * 4;
        if (!x[3]) continue;
        float r = x[0], g = x[1], bl = x[2], l = 0.30f * r + 0.59f * g + 0.11f * bl;
        r += (l - r) * d, g += (l - g) * d, bl += (l - bl) * d;
        r += (pr - r) * m, g += (pg - g) * m, bl += (pb - bl) * m;
        x[0] = (uint8_t)(r + 0.5f), x[1] = (uint8_t)(g + 0.5f), x[2] = (uint8_t)(bl + 0.5f);
    }
}

typedef struct {
    const char *label;
    int idx;
    float w, h;
} TBake;

static void draw_t(Surf *s, void *ud)
{
    const TBake *b = ud;
    Cx c = ui_cx(s);
    stk_sticker(&c, STK_PAD, STK_PAD, b->w, b->h, b->label, b->idx);
}

void stk_sticker_toned(Cx *c, float x, float y, float w, float h, const char *label, int idx)
{
    TBake b = {label, idx, w, h};
    PaperSpr *p = paper_bake((int)w + 2 * STK_PAD, (int)h + 2 * STK_PAD, draw_t, &b);
    if (!p) {
        stk_sticker(c, x, y, w, h, label, idx);
        return;
    }
    tone(p);
    p->ox = p->oy = STK_PAD;
    paper_spr_put(c->s, p, (int)lroundf(x), (int)lroundf(y));
    paper_spr_free(p);
}

// ---- the sleeve

typedef struct {
    const char *label;
    int idx, part;
    float w, h;
} StBake;

static void draw_st(Surf *s, void *ud)
{
    const StBake *b = ud;
    Cx c = ui_cx(s);
    if (b->part == 0) stk_sticker(&c, STK_PAD, STK_PAD, b->w, b->h, b->label, b->idx);
    else stk_back(&c, STK_PAD, STK_PAD, b->w, b->h, b->label);
}

static PaperSpr *bake_st(const StickFx *f, int i, int part)
{
    StBake b = {f->names[i], i, part, (float)f->W, (float)f->H};
    PaperSpr *p = paper_bake((int)f->W + 2 * STK_PAD, (int)f->H + 2 * STK_PAD, draw_st, &b);
    if (!p) return NULL;
    if (part == 0) tone(p); // (as the chips show them: what is stuck on looks as it did in the sleeve)
    fx_premultiply(p);
    return p;
}

static void shadow_box(Surf *dst, double x0, double y0, double x1, double y1, double z, Rect clip)
{
    float a = (0.22f + 0.10f * (float)fmin(1.0, z / 46.0)) * PAL(shadow_k, 1.0f);
    fx_soft_box(dst, (float)x0, (float)(y0 + 2 + 0.3 * z), (float)x1, (float)(y1 + 2 + 0.3 * z), (float)(2.5 + 0.13 * z), PAL(shadow, PAPER_SHADOW), a, clip);
}

static uint32_t shc(void) { return PAL(shadow, PAPER_SHADOW); }
static float sha(void) { return 0.34f * PAL(shadow_k, 1.0f); }

// what is above the chip's lower edge put back as it was (what comes out from under the chip shows below it only)
static void restore_above(const StickFx *f, Surf *dst, Rect clip, const Surf *ground)
{
    for (int y = clip.y; y < (int)f->clip_y && y < clip.y + clip.h; y++)
        memcpy(dst->px + ((size_t)y * dst->w + clip.x) * 4, ground->px + ((size_t)y * dst->w + clip.x) * 4, (size_t)clip.w * 4);
}

// the fold that lifts a sticker by its top right corner: depth px of it off, measured from the corner's tip the way the peel goes, which is beta
// (radians) up from straight along the sticker to the right (pi/4: across the corner)
static FxFold corner_fold(const StickFx *f, double depth, double beta, double r, double wrap)
{
    const double nx = cos(beta), ny = -sin(beta), cx = f->W * 0.5 - 10, cy = -f->H * 0.5 + 10;
    FxFold o = {cx + (10 - depth) * nx, cy + (10 - depth) * ny, nx, ny, r, wrap};
    return o;
}
#define ACROSS (PI_D / 4)

// ---- 12: the sleeve (the envelope, kept tidy)

#define SLV_DY 22.0   // the stickers in it lie one below the other, this far apart (overlapping)
#define SLV_IN 12.0   // the one out still has this much of its right end in the mouth
#define SLV_FLAP 30.0 // the open flap, lying out from the mouth
#define SLV_PULL 0.08 // (A) it is pulled the rest of the way out, then peeled
#define GO_T 0.22     // closing: the sleeve is pushed back under the chip

static const double SLV_XO[SFX_MAX] = {0, 5, -3, 4, -5, 2, -2, 3};               // (each lies a little left or right in it,
static const double SLV_AN[SFX_MAX] = {-1.0, 0.8, -0.5, 1.1, -0.9, 0.6, -0.4, 0.9}; // and a little turned, degrees)

typedef struct {
    const StickFx *f;
    int part, k; // 0 its front (the glassine over what is in it), 1 its back, 2 the flap; 3 a sticker's liner (k)
} SlvBake;

static void draw_sleeve(Surf *s, void *ud)
{
    const SlvBake *b = ud;
    const StickFx *f = b->f;
    Cx c = ui_cx(s);
    const int light = pal_light(PAL(board, PAPER_BOARD));
    const float w = (float)f->ow, h = (float)f->oh, x0 = 8, y0 = 8;
    const uint32_t gl = light ? 0xF3F5F7 : 0xBCC0CE;
    if (b->part == 3) {
        stk_place(&c, STK_PAD, STK_PAD, (float)f->W, (float)f->H, f->names[f->item[b->k]]);
        return;
    }
    if (b->part == 2) { // the flap: a rounded tongue lying out from the mouth (the mouth at x = SLV_FLAP + 8)
        const float m = (float)SLV_FLAP + 8, cy = y0 + h * 0.5f;
        float xy[12] = {m, y0 + 1, m, y0 + h - 1, 16, cy + h * 0.30f, 9, cy + 6, 9, cy - 6, 16, cy - h * 0.30f};
        poly(s, xy, 6, hex(gl, light ? 0.55f : 0.45f));
        poly_line(s, xy, 6, 0.9f, hex(light ? 0xC2C9D2 : 0x80859B, 0.55f));
        gfx_line(s, m, y0 + 2, m, y0 + h - 2, 1.2f, hex(light ? 0xB9C0CA : 0x7A7F96, 0.85f)); // (the fold)
        return;
    }
    if (b->part == 1) {
        gfx_shadow(s, x0, y0, w, h, 3, 1.2f, 2.4f, col(0, 0, 0, light ? 0.16f : 0.28f));
        gfx_rrect(s, x0, y0, w, h, 3, hex(light ? 0xECEFF2 : 0xA7ABBC, light ? 0.40f : 0.34f));
        return;
    }
    gfx_rrect(s, x0, y0, w, h, 3, hex(gl, light ? 0.62f : 0.52f));
    gfx_rrect_stroke(s, x0 + 0.5f, y0 + 0.5f, w - 1, h - 1, 3, 1.0f, hex(light ? 0xB7BFC9 : 0x7A7F96, 0.80f));
    gfx_line(s, x0 + w - 6, y0 + 3, x0 + w - 6, y0 + h - 3, 1.0f, hex(light ? 0xC3CAD3 : 0x858AA0, 0.6f)); // (its folded end)
    gfx_rrect_grad(s, x0 + 2, y0 + 2, w - 4, h * 0.36f, 2, hex(0xFFFFFF, light ? 0.30f : 0.12f), hex(0xFFFFFF, 0.0f));
}

static double slv_outd(const StickFx *f, int k) { return f->W + 13 + SLV_XO[k] - SLV_IN; }

static double ease3(double g);

// the sleeve's body: its middle and how it lies, as far out from under the chip as it is
static void slv_xf(const StickFx *f, double *bx, double *by, double *ba)
{
    const double hy = f->clip_y - f->oh * 0.5 - 4; // (under the chip: all of it above the chip's lower edge)
    double g = f->slide.x;
    if (f->go_t0 >= 0) g *= 1 - ease3((f->t - f->go_t0) / GO_T); // (closing: pushed back in, gathering speed, slowing as it goes under)
    *bx = f->ox;
    *by = hy + (f->oy - hy) * g;
    *ba = f->oa * g;
}

// sticker k, `out` px out of the mouth: its middle and how it lies (straightened as it comes out)
static void slv_item(const StickFx *f, int k, double out, double bx, double by, double ba, double *x, double *y, double *a)
{
    double lx = f->hx[k] - out, ly = f->hy[k], c = cos(ba), s = sin(ba);
    *x = bx + lx * c - ly * s;
    *y = by + lx * s + ly * c;
    *a = ba + f->ha[k] * (1 - clampd(out / slv_outd(f, k), 0, 1));
}

// The one picked, as one motion, so nothing jumps from one part to the next: its corner, already lifted off the liner by the hover, peels on
// along it (the fold turning from across the corner to straight across); the last of it comes free and it straightens in the fingers;
// it is carried up to the chip, its left end lowest; it touches there first and is rolled on to its right end. One fold all the way.
#define HOV_D 24.0    // the hover: how far its top right corner is lifted off the liner (px, along the diagonal)
#define PK_PEEL 0.20  // (after the pull) the peel runs along it
#define PK_FREE 0.10  // the last of it comes free, it straightens and rises
#define PK_CARRY 0.24 // carried to the chip
#define PK_ROLL 0.17  // rolled on, left end to right
#define TOSS_T 0.62  // its liner, let go: it floats down like a sheet of paper and fades; gone after this long
#define FLAP_T 0.13  // closing: the flap folds shut over the mouth
static void toss_vary(StickFx *f); // (how this toss goes: below, with its drawing)

static double ease3(double g)
{
    g = clampd(g, 0, 1);
    return g < 0.5 ? 4 * g * g * g : 1 - pow(-2 * g + 2, 3) / 2;
}

// the hovered corner: lifted l (0..1) off its liner
static FxFold hover_fold(const StickFx *f, double l) { return corner_fold(f, 2 + (HOV_D - 2) * l, ACROSS, 3 + 2 * l, 1.25); }

// sticker o, tp seconds after the pull (x, y, a: where it lies on its liner, until it is free; then fly_x, fly_y, fly_a)
static int pick_draw(StickFx *f, Surf *dst, int o, double tp, double x, double y, double a, Rect clip)
{
    const double W = f->W, H = f->H;
    if (tp < PK_PEEL) {
        double e = smooth(tp / PK_PEEL);
        FxFold fo = corner_fold(f, HOV_D + (W - 20 - HOV_D) * e, ACROSS * (1 - smooth(e * 1.5)), 5 + 11 * e, 1.25 - 0.80 * smooth(e * 3.0));
        fx_curl(dst, f->st[o], f->bk[o], x, y, 1.0, a, &fo, shc(), sha(), clip);
        return 0;
    }
    if (tp < PK_PEEL + PK_FREE) {
        double e = smooth((tp - PK_PEEL) / PK_FREE), sc = 1 + 0.05 * e, cy = y - 6 * e;
        FxFold fo = {-W * 0.5 + 20 - 22 * e, 0, 1, 0, 16 + 2 * e, 0.45 - 0.31 * e};
        if (e > 0.02) shadow_box(dst, x - W * 0.5 * sc, cy - H * 0.5 * sc, x + W * 0.5 * sc, cy + H * 0.5 * sc, 14 * e, clip);
        fx_curl(dst, f->st[o], f->bk[o], x, cy, sc, a, &fo, shc(), sha(), clip);
        return 0;
    }
    double tf = tp - PK_PEEL - PK_FREE;
    if (tf < PK_CARRY) { // carried in an arc over the board, a little nearer the eye half way, its left end lowest at the end
        double c = tf / PK_CARRY, e = ease3(c), arc = sin(PI_D * e);
        double x0 = f->fly_x, y0 = f->fly_y - 6;
        double cx = x0 + (f->land_x - x0) * e, cy = y0 + (f->land_y - y0) * e - 22 * arc;
        double sc = 1.05 + (f->land_sc - 1.05) * e + 0.035 * arc, ang = f->fly_a + (f->land_ang - f->fly_a) * e - 0.05 * arc;
        double z = 14 + (3 - 14) * e + 8 * arc;
        FxFold fo = {-W * 0.5 - 2, 0, 1, 0, 18, 0.14 + 0.20 * smooth((c - 0.4) / 0.6)};
        shadow_box(dst, cx - W * 0.5 * sc, cy - H * 0.5 * sc, cx + W * 0.5 * sc, cy + H * 0.5 * sc, z, clip);
        fx_curl(dst, f->st[o], f->bk[o], cx, cy, sc, ang, &fo, shc(), sha(), clip);
        return 0;
    }
    double r = (tf - PK_CARRY) / PK_ROLL;
    if (r < 1) { // its left end down, rolled on to the right
        double e = 1 - pow(1 - r, 3);
        FxFold fo = {-W * 0.5 - 2 + (W + 28) * e, 0, 1, 0, 20, 0.34 * (1 - 0.6 * e)};
        fx_curl(dst, f->st[o], f->bk[o], f->land_x, f->land_y, f->land_sc, f->land_ang, &fo, shc(), sha(), clip);
        return 0;
    }
    fx_affine(dst, f->st[o], f->land_x, f->land_y, f->land_sc, f->land_ang, 0, 1.0f, clip);
    return 1;
}

static void slv_step(StickFx *f, double dt)
{
    mo_step(&f->slide, (float)dt);
    for (int k = 0; k < f->m; k++) mo_step(&f->outm[k], (float)dt);
    f->nudgev += (-f->nudge * 900.0 - f->nudgev * 2 * 0.5 * 30.0) * dt;
    f->nudge += f->nudgev * dt;
    if (!f->shown && !f->closing && f->m > 0 && f->slide.x > 0.86f) { // landed, and the one before mostly back in: the focus's one comes out
        double most = 0;
        for (int k = 0; k < f->m; k++)
            if (k != f->focus) most = fmax(most, f->outm[k].x / slv_outd(f, k));
        if (most < 0.60) {
            f->shown = 1;
            mo_spring(&f->outm[f->focus], (float)slv_outd(f, f->focus), 28.0f, 0.86f);
            ui_sound(SND_SLIP2, 0.14f, 0);
        }
    }
    for (int k = 0; k < f->m; k++) { // the hover: the one out has its corner lifted off its liner (a soft spring, a hair of overshoot)
        float want = !f->closing && f->shown && k == f->focus && f->outm[k].x > slv_outd(f, k) * 0.85 ? 1.0f : 0.0f;
        if (f->closing && f->pick >= 0 && k == f->focus) want = 1.0f;
        if (want > 0.5f && f->lift[k] < 0.02f && f->liftv[k] <= 0) ui_sound(SND_PEEL, 0.06f, 0);
        f->liftv[k] += (float)((want - f->lift[k]) * 520.0 * dt - f->liftv[k] * 2 * 0.62 * 22.8 * dt);
        f->lift[k] += f->liftv[k] * (float)dt;
    }
    if (!f->closing) return;
    if (f->pick >= 0) {
        if (f->fly_t0 < 0 && f->tc >= f->t_ex + PK_PEEL + PK_FREE) { // free of its liner: from here it is carried, and the liner is flicked away
            double bx, by, ba;
            slv_xf(f, &bx, &by, &ba);
            slv_item(f, f->focus, f->outm[f->focus].x, bx, by, ba, &f->fly_x, &f->fly_y, &f->fly_a);
            f->fly_t0 = f->t;
            f->toss_t0 = f->t;
            f->toss_x = f->fly_x, f->toss_y = f->fly_y, f->toss_a = f->fly_a;
            toss_vary(f);
            ui_sound(SND_SLIP3, 0.16f, 0);
        }
        if (f->fly_t0 >= 0 && f->flap_t0 < 0 && f->t - f->fly_t0 >= 0.05) { // (the mouth is clear: the flap shuts)
            f->flap_t0 = f->t;
            ui_sound(SND_SLIP1, 0.10f, 0);
        }
        if (f->fly_t0 >= 0 && f->t - f->fly_t0 >= PK_CARRY && f->on_chip != f->fly_o) { // it touches the chip
            f->on_chip = f->fly_o;
            f->on_t = f->t;
            ui_sound(SND_STICK, 0.5f, 0);
        }
    } else if (f->flap_t0 < 0 && (f->m <= 0 || f->outm[f->focus].x < slv_outd(f, f->focus) * 0.35)) { // (B: the one out mostly back in)
        f->flap_t0 = f->t;
        ui_sound(SND_SLIP1, 0.10f, 0);
    }
    if (f->flap_t0 >= 0 && f->go_t0 < 0 && f->t - f->flap_t0 >= FLAP_T * 0.6) { // nearly shut: the sleeve is pushed back under the chip
        f->go_t0 = f->t;
        f->going = 1;
        ui_sound(SND_SLIP0, 0.12f, 0);
    }
    int landed = f->pick < 0 || (f->fly_t0 >= 0 && f->t - f->fly_t0 >= PK_CARRY + PK_ROLL);
    int tossed = f->toss_t0 < 0 || f->t - f->toss_t0 >= TOSS_T;
    if (f->go_t0 >= 0 && f->t - f->go_t0 >= GO_T && landed && tossed) f->finished = 1;
}

// The liner, let go once the sticker is off it. It is light paper: the little flick it got drifts it off to the left and the air soon takes
// that; it falls slowly, rocking from side to side as a dropped sheet does (tipped down the way it is going, a little higher at each end of
// a rock, its trailing end lifted by the air), its shadow falling further from it as it leaves the board, and it fades as it goes.
// No two fall alike: each toss draws its own flick, fall, rock (how long, how wide, how far it tips, which way first) and drift.
static uint32_t g_toss_n; // (on the console it starts from the clock, so a start's first toss differs too; the PC tests keep the same run)

static void toss_vary(StickFx *f)
{
    PaperRng r;
#ifdef __SWITCH__
    if (!g_toss_n) {
        uint64_t tk = armGetSystemTick();
        g_toss_n = (uint32_t)tk ^ (uint32_t)(tk >> 32);
    }
#endif
    paper_rng_seed(&r, ++g_toss_n * 0x9E3779B9u ^ 0x7055u);
#define U(a, b) ((a) + ((b) - (a)) * (double)paper_rng_f(&r))
    f->toss_kx = U(120, 330);   // the flick to the left (px/s)
    f->toss_ky = U(15, 65);     // and up
    f->toss_fall = U(210, 320); // how fast it settles to falling (px/s)
    f->toss_rp = U(0.42, 0.72); // one rock, to and fro (s)
    f->toss_rx = U(8, 34);      // how far a rock carries it (px)
    f->toss_ra = U(0.10, 0.36); // how far it tips (rad)
    f->toss_ph = (paper_rng_f(&r) < 0.5f ? 0 : PI_D) + U(-0.5, 0.5); // which way it rocks first
    f->toss_spin = U(-0.30, 0.12); // a slow turn as it goes
    f->toss_fade = U(0.22, 0.30);  // when it starts to fade
#undef U
}

static void toss_draw(const StickFx *f, Surf *dst, Rect clip)
{
    double t = f->t - f->toss_t0;
    if (t >= TOSS_T) return;
    const double w = 2 * PI_D / f->toss_rp, ph = f->toss_ph, in = smooth(t / 0.14); // (the rock grows in from rest)
    const double rock = sin(w * t + ph) - sin(ph), way = cos(w * t + ph);
    double x = f->toss_x - f->toss_kx * (1 - exp(-5.5 * t)) / 5.5 + f->toss_rx * rock * in;
    double y = f->toss_y - f->toss_ky * (1 - exp(-7.0 * t)) / 7.0 + f->toss_fall * (t - (1 - exp(-4.0 * t)) / 4.0) - 4.0 * (1 - cos(2 * w * t)) * in;
    double a = f->toss_a + f->toss_spin * (1 - exp(-4.0 * t)) + f->toss_ra * way * in;
    double z = 3 + 26 * smooth(t / TOSS_T), sc = 1 + 0.03 * smooth(t / 0.2); // (up off the board, and a hair nearer the eye)
    float al = (float)(1 - smooth((t - f->toss_fade) / (TOSS_T - f->toss_fade)));
    const double W = f->W, H = f->H;
    FxFold fo = {way > 0 ? -W * 0.5 + 34 : W * 0.5 - 34, 0, way > 0 ? -1 : 1, 0, 16, 0.42 * fabs(way) * in}; // (its trailing end, lifted)
    fx_soft_box(dst, (float)(x - W * 0.5 * sc), (float)(y - H * 0.5 * sc + 2 + 0.3 * z), (float)(x + W * 0.5 * sc), (float)(y + H * 0.5 * sc + 2 + 0.3 * z),
                (float)(2.5 + 0.13 * z), shc(), 0.20f * PAL(shadow_k, 1.0f) * al * al, clip);
    fx_curl_a(dst, f->liner[f->focus], f->liner[f->focus], x, y, sc, a, &fo, shc(), sha() * 0.6f, al, clip);
}

static void slv_draw(StickFx *f, Surf *dst, Rect clip, const Surf *ground)
{
    double bx, by, ba;
    slv_xf(f, &bx, &by, &ba);
    const double c = cos(ba), s = sin(ba), fl = -f->ow * 0.5 - SLV_FLAP * 0.5;
    const int picked = f->pick >= 0 && f->fly_o >= 0;
    const double fp = f->flap_t0 < 0 ? 0 : smooth((f->t - f->flap_t0) / FLAP_T); // (closing: how far the flap has folded shut)
    if (fp < 0.01) fx_affine(dst, f->lay[2], bx + fl * c, by + fl * s, 1.0, ba, 0, 1.0f, clip); // the flap, lying out from the mouth
    fx_affine(dst, f->lay[1], bx, by, 1.0, ba, 0, 1.0f, clip);                   // the sleeve's back
    for (int k = 0; k < f->m; k++) { // what is in it, and the one out (its part still in the mouth seen through the glassine)
        double out = f->outm[k].x + (k == f->focus ? f->nudge : 0), x, y, a, l = f->lift[k] < 0 ? 0 : f->lift[k];
        slv_item(f, k, out, bx, by, ba, &x, &y, &a);
        double u = clampd(out / slv_outd(f, k), 0, 1);
        if (picked && k == f->focus) { // pulled out on its liner (still hovered), then peeled off it; the liner is then flicked away
            if (f->toss_t0 < 0) fx_affine(dst, f->liner[k], x, y, 1.0, a, 0, 1.0f, clip);
            if (f->tc < f->t_ex) {
                FxFold fo = hover_fold(f, l);
                shadow_box(dst, x - f->W * 0.5, y - f->H * 0.5, x + f->W * 0.5, y + f->H * 0.5, 3, clip);
                fx_curl(dst, f->st[f->fly_o], f->bk[f->fly_o], x, y, 1.0, a, &fo, shc(), sha(), clip);
            }
            continue;
        }
        if (u > 0.02) shadow_box(dst, x - f->W * 0.5, y - f->H * 0.5, x + f->W * 0.5, y + f->H * 0.5, 3 * u, clip);
        if (l > 0.01) { // hovered: its corner off the liner, the liner showing under it
            FxFold fo = hover_fold(f, l);
            fx_affine(dst, f->liner[k], x, y, 1.0, a, 0, 1.0f, clip);
            fx_curl(dst, f->st[f->item[k]], f->bk[f->item[k]], x, y, 1.0, a, &fo, shc(), sha(), clip);
        } else {
            fx_affine(dst, f->st[f->item[k]], x, y, 1.0, a, 0, 1.0f, clip);
        }
    }
    fx_affine(dst, f->lay[0], bx, by, 1.0, ba, 0, 1.0f, clip); // the glassine over it all
    if (fp >= 0.01) { // the flap folding over the mouth, onto the sleeve (its fold the mouth's edge, 15 px right of its middle)
        FxFold fo = {15, 0, -1, 0, 2.0, PI_D * fp};
        fx_curl(dst, f->lay[2], f->lay[2], bx + fl * c, by + fl * s, 1.0, ba, &fo, shc(), sha() * 0.5f, clip);
    }
    restore_above(f, dst, clip, ground);
    if (f->toss_t0 >= 0) toss_draw(f, dst, clip);
    if (picked && f->tc >= f->t_ex) {
        double x = f->fly_x, y = f->fly_y, a = f->fly_a;
        if (f->fly_t0 < 0) slv_item(f, f->focus, f->outm[f->focus].x, bx, by, ba, &x, &y, &a);
        pick_draw(f, dst, f->fly_o, f->tc - f->t_ex, x, y, a, clip);
    }
}

// ---- setting up, running

int sfx_init(StickFx *f, int kind, int n, const char *const *names, int cur_mode, float chip_x, float chip_y, float chip_w, float chip_h,
             int chip_lifted)
{
    (void)kind;
    memset(f, 0, sizeof *f);
    f->n = n < 1 ? 1 : n > SFX_MAX ? SFX_MAX : n;
    f->cur_mode = cur_mode >= 0 && cur_mode < f->n ? cur_mode : -1;
    f->chip_x = chip_x;
    f->chip_y = chip_y;
    f->chip_w = chip_w;
    f->chip_h = chip_h;
    f->clip_y = floorf(chip_y + chip_h);
    f->tc = -1;
    f->pick = -1;
    f->on_chip = -1;
    f->fly_o = -1;
    f->fly_t0 = -1;
    f->t_ex = -1;
    f->toss_t0 = f->flap_t0 = f->go_t0 = -1;
    f->chip_lifted = chip_lifted;
    f->W = stk_w(chip_w);
    f->H = STK_H;
    f->sx = chip_x + STK_X + f->W * 0.5;
    f->sy = chip_y + STK_Y + f->H * 0.5;
    for (int i = 0; i < f->n; i++) snprintf(f->names[i], sizeof f->names[i], "%s", names[i]);
    int ok = 1;
    for (int i = 0; i < f->n && ok; i++) {
        f->st[i] = bake_st(f, i, 0);
        f->bk[i] = bake_st(f, i, 1);
        ok = f->st[i] && f->bk[i];
    }
    // where a sticker lands: on the chip's label; the chip lifted as the focus has it (up 5 px, 4% bigger, turned a hair) if it has it
    double ccx = chip_x + chip_w * 0.5, ccy = chip_y + chip_h * 0.5, wk = fmin(1.0, 165.0 / chip_w);
    PaperXf cf = {(float)ccx, (float)ccy, 1.0f, 0.0f};
    if (chip_lifted) cf = (PaperXf){(float)ccx, (float)(ccy - 5), 1.04f, (float)(-0.4 * wk * PI_D / 180)};
    PaperXf at = paper_xf_attach(cf, (float)(f->sx - ccx), (float)(f->sy - ccy));
    f->land_x = at.x;
    f->land_y = at.y;
    f->land_sc = at.scale;
    f->land_ang = at.angle;
    // the sleeve: the orders not on the chip go in it, in the menu's order
    for (int i = 0; i < f->n; i++)
        if (i != f->cur_mode) f->item[f->m++] = i;
    f->focus = 0;
    f->ow = f->W + 26;
    f->oh = f->H + 6 + SLV_DY * (f->m > 1 ? f->m - 1 : 0) + 22;
    f->ox = chip_x + chip_w - 8 - f->ow * 0.5; // (its body's middle at rest, a hand's width under the chip)
    f->oy = f->clip_y + 16 + f->oh * 0.5;
    f->oa = 1.2 * PI_D / 180;
    for (int k = 0; k < f->m && ok; k++) {
        f->hx[k] = -f->ow * 0.5 + 13 + f->W * 0.5 + SLV_XO[k];
        f->hy[k] = -f->oh * 0.5 + 11 + (f->H + 6) * 0.5 + SLV_DY * k;
        f->ha[k] = SLV_AN[k] * PI_D / 180;
        mo_snap(&f->outm[k], 0, 0.3f);
        SlvBake b = {f, 3, k};
        f->liner[k] = paper_bake((int)f->W + 2 * STK_PAD, (int)f->H + 2 * STK_PAD, draw_sleeve, &b);
        ok = f->liner[k] != NULL;
        if (ok) fx_premultiply(f->liner[k]);
    }
    for (int p = 0; p < 3 && ok; p++) {
        SlvBake b = {f, p, 0};
        f->lay[p] = paper_bake(p == 2 ? (int)SLV_FLAP + 16 : (int)f->ow + 16, (int)f->oh + 16, draw_sleeve, &b);
        ok = f->lay[p] != NULL;
        if (ok) fx_premultiply(f->lay[p]);
    }
    mo_snap(&f->slide, 0, 0.002f);
    mo_spring(&f->slide, 1.0f, 20.0f, 0.80f);
    ui_sound(SND_SLIP0, 0.16f, 0);
    if (!ok) sfx_free(f);
    return ok;
}

void sfx_free(StickFx *f)
{
    for (int i = 0; i < SFX_MAX; i++) {
        paper_spr_free(f->st[i]);
        paper_spr_free(f->bk[i]);
        paper_spr_free(f->liner[i]);
        f->st[i] = f->bk[i] = f->liner[i] = NULL;
    }
    for (int p = 0; p < 3; p++) {
        paper_spr_free(f->lay[p]);
        f->lay[p] = NULL;
    }
}

Rect sfx_region(const StickFx *f)
{
    double x0 = fmin(f->chip_x - 30, f->ox - f->ow * 0.5 - f->W - 200), x1 = f->chip_x + f->chip_w + 30, y0 = f->chip_y - 30;
    double y1 = fmax(f->clip_y + 40, f->oy + f->oh * 0.5 + 170); // (the one out, to the left of the mouth, and its liner floating down; the sticker carried up over the chip)
    Rect r = {(int)floor(x0), (int)floor(y0), 0, 0};
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    r.w = (int)ceil(x1) - r.x;
    r.h = (int)ceil(y1) - r.y;
    if (r.x + r.w > SCREEN_W) r.w = SCREEN_W - r.x;
    if (r.y + r.h > SCREEN_H) r.h = SCREEN_H - r.y;
    return r;
}

void sfx_step(StickFx *f, double dt)
{
    if (dt <= 0) return;
    if (dt > 0.1) dt = 0.1;
    while (dt > 1e-9) { // (in small steps: a spring worked out a frame at a time can ring)
        double h = dt < 1.0 / 1200 ? dt : 1.0 / 1200;
        f->t += h;
        if (f->closing) f->tc += h;
        slv_step(f, h);
        dt -= h;
    }
}

int sfx_nav(StickFx *f, int dx, int dy)
{
    if (f->closing) return 1;
    if (f->m <= 0) return 0;
    if (dy) { // the one out goes back in, and the next comes out
        int to = f->focus + dy;
        if (to < 0 || to >= f->m) {
            f->nudgev -= 70.0; // (no more that way: it is pushed in a hair and comes back)
            return 0;
        }
        for (int k = 0; k < f->m; k++)
            if (k != to) mo_spring(&f->outm[k], 0.0f, 40.0f, 1.0f);
        f->focus = to;
        f->shown = 0; // (it comes out once the one before is mostly back in: slv_step)
        ui_sound(SND_SLIP1, 0.08f, 0);
        return 1;
    }
    f->nudgev += dx < 0 ? 150.0 : -150.0; // (Left: a little tug out; Right: a little push in; it springs back)
    ui_sound(SND_SLIP3, 0.05f, 0);
    return 1;
}

void sfx_close(StickFx *f, int pick)
{
    if (f->closing) return;
    f->closing = 1;
    f->tc = 0;
    if (pick >= 0 && f->m > 0) { // the one out (the focus)
        f->t_ex = f->shown && f->outm[f->focus].x > slv_outd(f, f->focus) * 0.8 ? SLV_PULL : 0.22; // (the rest of the way out: longer if it was not out yet)
        f->shown = 1;
        f->pick = f->item[f->focus];
        f->fly_o = f->pick;
        mo_spring(&f->outm[f->focus], (float)(slv_outd(f, f->focus) + SLV_IN + 8), 44.0f, 1.0f);
        f->nudge = f->nudgev = 0;
        ui_sound(SND_PEEL, 0.45f, (int)(f->t_ex * 1000));
    } else {
        f->pick = -1;
        if (f->m > 0) mo_spring(&f->outm[f->focus], 0.0f, 42.0f, 1.0f); // (tucked back in, firmly; the flap shuts and the sleeve goes: slv_step)
        ui_sound(SND_SLIP1, 0.10f, 0);
    }
}

int sfx_moving(const StickFx *f)
{
    if (f->finished) return 0;
    if (f->closing || !f->shown || mo_moving(&f->slide) || fabs(f->nudge) > 0.05 || fabs(f->nudgev) > 1) return 1;
    for (int k = 0; k < f->m; k++) {
        if (mo_moving(&f->outm[k])) return 1;
        float want = k == f->focus ? 1.0f : 0.0f;
        if (fabsf(f->lift[k] - want) > 0.01f || fabsf(f->liftv[k]) > 0.05f) return 1;
    }
    return 0;
}

int sfx_result(const StickFx *f) { return f->pick; }

void sfx_draw(StickFx *f, Surf *dst, Rect clip, const Surf *ground) { slv_draw(f, dst, clip, ground); }

