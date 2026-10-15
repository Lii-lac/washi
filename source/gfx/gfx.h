// A small software renderer for the Washi interface: anti-aliased shapes, text, and a "liquid glass" material
// (blurred, refracting backdrop with specular rims). Pure C, no platform code, so the exact same drawing runs on the
// Switch (libnx framebuffer) and on a PC (PNG screenshots for review).
#ifndef GFX_H
#define GFX_H

#include <stddef.h>
#include <stdint.h>

#define SCREEN_W 1280
#define SCREEN_H 720

typedef struct { float r, g, b, a; } Col; // straight (non-premultiplied), 0..1

static inline Col col(float r, float g, float b, float a) { Col c = {r, g, b, a}; return c; }
static inline Col hex(uint32_t rgb, float a)
{
    Col c = {((rgb >> 16) & 255) / 255.0f, ((rgb >> 8) & 255) / 255.0f, (rgb & 255) / 255.0f, a};
    return c;
}
static inline Col with_a(Col c, float a) { c.a = a; return c; }
static inline Col mixc(Col a, Col b, float t)
{
    Col c = {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t};
    return c;
}

#ifdef WASHI_PAL_LAB
extern float g_gfx_text_grain; // mockups only: light writing drawn as chalk (how much of each letter it misses), see paper.h's palettes
#endif

// ---- surfaces: RGBA8, always opaque
typedef struct { int w, h; uint8_t *px; } Surf;
Surf *surf_new(int w, int h);
Surf *surf_clone(const Surf *s);
void surf_copy(Surf *dst, const Surf *src);
void surf_free(Surf *s);
// A hook for long jobs: called now and then from loops that can take a while (a surface or sprite being made, a filter at work, a folder being walked). The
// interface sets it while the app is busy between two screens, to keep a mark of "still working" moving. NULL: nothing.
extern void (*gfx_pump_hook)(void);
static inline void gfx_pump(void) { if (gfx_pump_hook) gfx_pump_hook(); }

// ---- blurred copy of a surface, sampled by the glass material
typedef struct { int w, h, scale; float *rgb; } Blur;
Blur *blur_make(const Surf *src, float radius_px);
void blur_free(Blur *b);

// ---- shapes (AA, alpha-blended onto an opaque surface)
void gfx_fill(Surf *s, Col c);
void gfx_rect(Surf *s, float x, float y, float w, float h, Col c);
void gfx_rrect(Surf *s, float x, float y, float w, float h, float r, Col c);
void gfx_rrect_grad(Surf *s, float x, float y, float w, float h, float r, Col top, Col bottom);
void gfx_rrect_stroke(Surf *s, float x, float y, float w, float h, float r, float width, Col c);
void gfx_circle(Surf *s, float cx, float cy, float rad, Col c);
void gfx_circle_grad(Surf *s, float cx, float cy, float rad, Col top, Col bottom);
void gfx_ring(Surf *s, float cx, float cy, float rad, float width, Col c);
void gfx_line(Surf *s, float x0, float y0, float x1, float y1, float width, Col c);
void gfx_arc(Surf *s, float cx, float cy, float rad, float width, float a0, float a1, Col c); // radians, y down
void gfx_blob(Surf *s, float cx, float cy, float rx, float ry, Col c);                         // soft ellipse
void gfx_halo(Surf *s, float x, float y, float w, float h, float r, float spread, Col c);      // soft glow outside a rrect
// draws a size x size RGBA image (e.g. a game icon) at (x, y) with rounded corners
// RGBA picture (straight alpha, sw x sh) scaled to `zoom` x the circle's diameter and clipped to the circle, over what is there
void gfx_blit_circle(Surf *s, const uint8_t *rgba, int sw, int sh, float cx, float cy, float r, float zoom);
// RGBA picture (straight alpha, sw x sh) scaled to fill the w x h rounded rectangle at (x, y), over what is there
void gfx_blit_rrect_scaled(Surf *s, const uint8_t *rgba, int sw, int sh, float x, float y, float w, float h, float r);
void gfx_blit_rrect(Surf *s, const uint8_t *rgba, int size, float x, float y, float r);
// The transparency of an RGBA picture (sw x sh) as a stencil, scaled to w x h at (x, y) and painted in `ink`.
void gfx_blit_mask(Surf *s, const uint8_t *rgba, int sw, int sh, float x, float y, float w, float h, Col ink);
// decodes a JPEG and area-scales it to out_size x out_size RGBA (malloc'd; free() it), or NULL if it can't be decoded
uint8_t *gfx_decode_icon(const uint8_t *jpeg, size_t n, int out_size);
// raw DEFLATE (no zlib header) to bytes (malloc'd; free() it), or NULL if the data is bad
uint8_t *gfx_inflate_raw(const uint8_t *data, size_t n, size_t *out_size);
// the surface as a JPEG (malloc'd; free() it), or NULL
uint8_t *gfx_encode_jpeg(const Surf *s, int quality, size_t *size);
uint8_t *gfx_decode_png(const uint8_t *png, size_t n, int *w, int *h); // RGBA, free() it; NULL if it is not a PNG
void gfx_shadow(Surf *s, float x, float y, float w, float h, float r, float dy, float blur, Col c); // soft drop shadow
void gfx_dither(Surf *s);
// dst = a + (b - a) * t / 256, for whole frames (cross-fades)
void gfx_lerp(Surf *dst, const Surf *a, const Surf *b, int t);
// Lays the rounded rectangle (x, y, w, h, r) of `src` over `dst`, moved by (dx, dy) pixels and faded to `alpha` (0..1).
void gfx_mix_card(Surf *dst, const Surf *src, float x, float y, float w, float h, float r, int dx, int dy, float alpha);

// A straight-alpha RGBA picture (sw x sh) with its top left at the whole pixel (x, y), over what is there; no turning or scaling, so
// it is quick (whole-pixel integer maths). For things that sit still: resting cards, shadows baked at their size.
void gfx_blit_over(Surf *s, const uint8_t *rgba, int sw, int sh, int x, int y);
// Where a straight-alpha RGBA picture (sw x sh) at the whole pixel (x, y) has any ink at all, puts back what `from` (the same size as
// s, e.g. the bare ground) has: takes a card and its shadow off a composed screen.
void gfx_restore(Surf *s, const Surf *from, const uint8_t *mask_rgba, int sw, int sh, int x, int y);
// A straight-alpha RGBA picture (sw x sh) scaled by `scale` and turned by `angle` radians (clockwise on screen) about its own centre,
// which goes at (cx, cy); faded to `alpha` (0..1); bilinear, and its edges fade out smoothly. For a card that lifts and wiggles.
void gfx_blit_xform(Surf *s, const uint8_t *rgba, int sw, int sh, float cx, float cy, float scale, float angle, float alpha);

// ---- liquid glass
typedef struct {
    float sat, bright;      // backdrop adjustments
    Col tint;               // colour and strength laid over the backdrop
    float top_light;        // extra light along the top
    float bottom_shade;     // darkening toward the bottom, for thickness
    float refr, bevel;      // refraction displacement and the width of the lens edge, px
    float chroma;           // colour fringe at the lens edge, px
    float rim, rim_w;       // specular rim strength and width
    float glow;             // soft inner glow along the edge
    Col edge;               // hairline at the very edge
    float shadow, shadow_dy, shadow_blur;
    Col shadow_col;
    float lx, ly;           // unit vector pointing toward the light (screen space, y down)
} Glass;
void gfx_glass(Surf *s, const Blur *bg, const Glass *g, float x, float y, float w, float h, float r);

// ---- text (UTF-8)
typedef struct Font Font;
Font *font_load(const unsigned char *ttf);
void font_set_fallback(Font *f, Font *fallback);
float text_width(Font *f, float px, float tracking, const char *utf8);
// baseline-anchored; align: 0 left, 1 centre, 2 right (x is the anchor)
void text_draw(Surf *s, Font *f, float px, float tracking, float x, float baseline, int align, Col c, const char *utf8);
// copies utf8 into out, shortened with "..." so it is at most max_w wide
void text_fit(Font *f, float px, float tracking, const char *utf8, float max_w, char *out, size_t cap);

extern const unsigned char font_regular_ttf[], font_medium_ttf[], font_semibold_ttf[];
extern const unsigned int font_regular_ttf_len, font_medium_ttf_len, font_semibold_ttf_len;

#endif
