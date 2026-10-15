// The intro's tape wall: the app's pressed washi made at any size, the roll that lays it, and a strip being torn off. A C copy of the intro mockup's
// tape.js, roll.js and ribbon.js (the intro's design mock-up, not kept here), which were themselves copies of paper.c and paper_roll.c taken up to the
// wall's 4.5 times size, with the wall's own options (nicks, a closed crimp, softer fibres, the tape's colour against a light host). Pure C on
// gfx.h and paper.h, no platform code; seeded, so the same seed gives the same strip on the PC and the console.
#ifndef WALL_H
#define WALL_H

#include <stdint.h>

#include "gfx.h"
#include "paper.h"

// ---- the page's numbers (tape.js, roll.js): random numbers, hashes and value noise exactly as the page has them
typedef struct { uint32_t s; } WallRng;
void wall_rng_seed(WallRng *r, uint32_t seed); // tRng: xorshift32, seeded as paper_rng_seed
float wall_rng(WallRng *r);                    // 0 <= x < 1
float wall_hashf(int x, int y, uint32_t seed); // tHashF
float wall_vnoise(float x, float y, uint32_t seed);
float wall_smooth(float t);                    // smoothstep of t clamped to 0..1
float wall_pat_white(int kind, float u, float v, float L, float H); // patWhite: how much white the tape's pattern lays over its colour at (u, v), 1x px

// ---- a strip of pressed tape
typedef struct {
    float x;      // where along the strip (1x px)
    int top;      // 1: in the top edge, 0: the bottom one
    float w, d;   // how wide and how deep (1x px)
    float sk;     // -0.5..0.5: which side the deepest point leans to
    float e;      // the profile's exponent (below 1 a round bite, above 1 a slit)
    float j;      // how ragged the torn edge is
    uint32_t sd;
} WallNick;
#define WALL_NICK_MAX 4
typedef struct {
    float L1, h1, k;           // length and width in the app's 1x px; k makes it k times bigger
    int kind, colour;          // PAT_*, index into paper_tape_rgb
    uint32_t seed;
    int crimp;                 // pressed down in one place (a ridge; the far half a little out of line)
    WallNick nicks[WALL_NICK_MAX];
    int nnicks;
    float strength;            // how strongly the unevenness shows (1 = the app's)
    float alpha;               // how much of the tape's own colour against the host mixed in (the app's .70)
    float host[3];             // the colour mixed in, 0..1
    float press;               // how much that changes along the strip
    float patch, edge, edge_w, dark_tint; // the lift and dark patches, the light and shade on the edges and their width, how much brown in the dark
    float ridge, wrinkles, jog;           // the pressed-down ridge's strength, the wrinkles round it, how far the far half is out of line
    int seal;                  // close the hair of a gap between the two halves of a pressed-down strip
    float fib_n, fib_l, fib_d, fib_len, fib_soft; // fibres: how many, the light and dark ones' strength, their length, how many px their edges fade over
} WallTapeOpt;
void wall_tape_defaults(WallTapeOpt *o); // the app's own numbers (every option as paper.c has it)
typedef struct {
    PaperSpr *spr;      // straight alpha; the strip's top left is at (pad, pad); ox, oy = its middle
    int pad;
    int crimp;
    float sx, sk, cx, ky, kr; // the crimp (1x px): where its slanted cut starts, half its slant, its middle; the far half's offset and turn (degrees)
} WallTape;
int wall_tape_make(const WallTapeOpt *o, WallTape *out); // 0 = made; free out->spr with wall_spr_free
// The wall's own sprites: allocated without paper_spr_new's bookkeeping (it is not for other threads), so the wall can be made on other cores. Free with wall_spr_free.
PaperSpr *wall_spr_new(int w, int h);
void wall_spr_free(PaperSpr *p);

// ---- the roll that lays it (roll.js drawRoll): a cylinder 2R long along the strip and rb across, the pattern wrapped round it by the arcsine mapping
#define WALL_ROLL_RN 24
#define WALL_ROLL_CIRC 6.3193f
#define WALL_ROLL_N 20
typedef struct { uint8_t *px; int w, h; } WallTex; // the pattern laid flat, opaque RGBA
WallTex *wall_roll_tex(int colour, int kind, float k, float rb, float circ0);
void wall_roll_tex_free(WallTex *t);
void wall_roll_phase(float len1, float r0, float r1, float ph[65]); // turns (in frames of 1/WALL_ROLL_N turn) once p of the strip is laid, at p = i / 64
float wall_roll_phi(const float ph[65], float p);                   // radians the roll has turned at p
typedef struct {
    float x, y, ang;   // its middle on the screen, the strip's direction (radians)
    float k, R, rb;    // size, radius now and height across (1x px)
    float phi, circ0;  // its turn; the circumference the texture is made for
    float alpha, scale;
    int flip;          // a roll running right to left: the pattern mirrored, the light across it not
    int keyed;         // drawn as its shape alone, all in one colour (key, 0xRRGGBB), without its shadow: to find where it shows once all is drawn
    uint32_t key;
} WallRoll;
void wall_roll_light(float x, float out[4]); // the light laid over a roll's pattern at x = 0..1 along it: premultiplied rgb (0..255) and alpha
// draws it (and its soft shadow, multiplied) over s; scratch is reused between calls (freed with wall_scratch_free). Rolls drawn on several threads at once each
// need their own scratch, and wall_roll_ready(k) called first (it makes what the shadows of rolls k times bigger share).
typedef struct { uint8_t *a, *b; int cap; } WallScratch;
void wall_roll_ready(float k);
void wall_roll_draw(Surf *s, const WallTex *tex, const WallRoll *r, WallScratch *sc);
void wall_scratch_free(WallScratch *sc);

// ---- pictures and blits
// three box blurs of a plane of alpha (about a gaussian of sigma = rad); tmp is w x h scratch
void wall_blur_alpha(float *a, float *tmp, int w, int h, int rad);
// only the soft shadow a picture casts (as a canvas shadow drawn and the picture taken back out of it): moved (dx, dy), blurred, rgb at alpha; the picture's middle is
// the result's middle (ox, oy) and it has `pad` px of room all round. Free with wall_spr_free.
PaperSpr *wall_shadow_only(const uint8_t *rgba, int w, int h, int pad, float dx, float dy, int rad, uint32_t rgb, float alpha);
// a straight-alpha picture turned by `rot` radians about its middle into a new sprite (bilinear), with a soft shadow baked under it (dy down, blur, colour, alpha);
// the result's middle is where the picture's middle was (ox, oy)
PaperSpr *wall_turned(const PaperSpr *src, float rot, int pad, float shadow_dy, float shadow_blur, uint32_t shadow_rgb, float shadow_a);
// For each row of a sprite its longest run of solid pixels (start, end; malloc'd, free() it): drawing it copies those whole and blends only the rest.
int16_t *wall_runs(const PaperSpr *p);
// draws a sprite (straight alpha) with its anchor (ox, oy) at the whole pixel (x, y) over the opaque surface; runs from wall_runs, or NULL
void wall_put(Surf *s, const PaperSpr *p, const int16_t *runs, int x, int y);
// the same only where (px - cx) * dx + (py - cy) * dy < lim (a half plane: the part of a strip laid so far)
void wall_blit_half(Surf *s, const PaperSpr *p, const int16_t *runs, int x, int y, float cx, float cy, float dx, float dy, float lim);
// the same, faded and multiplied: for shadows that must darken whatever is under them (the black table included) instead of laying brown on it
void wall_blit_mul(Surf *s, const uint8_t *rgba, int w, int h, int x, int y, float alpha);
// A plane of alpha (w x h, a byte a pixel) laid in one colour (0xRRGGBB), scaled by `scale` and turned by `angle` radians about its middle, which goes at (cx, cy),
// faded to `alpha`; bilinear. For shadows and glows: gfx_blit_xform takes the slow way round for every pixel that is not opaque, which is all of a shadow.
// `hole` (x0, y0, x1, y1 in texels, or NULL): a rectangle the alpha is 0 all over (the middle of a card's shadow, under the card), jumped over.
void wall_blit_shade(Surf *s, const uint8_t *a8, int w, int h, float cx, float cy, float scale, float angle, float alpha, uint32_t rgb, const int *hole);

// ---- a strip cut into slices that follow a line, so it can lift and curl (ribbon.js): each point has its place, how much bigger it is drawn (nearer the eye),
// u (sprite px along the strip's box) and an alpha; a slice that starts at a point with back = 1 shows the strip's pale back instead of its face
typedef struct { float x, y, s, u, a, z, shade; int back; } WallPt;
typedef struct { const PaperSpr *img; int bx, by, bw, bh, flip; } WallBox; // the part of the sprite the strip is (its box); flip: read it turned half round (a strip torn from its right end)
void wall_ribbon(Surf *s, const WallBox *box, const WallPt *pts, int n, float sc, float ang, float alpha, const float back_rgb[3], float sy);
// its shadow: the footprint (ground points), as thick as the strip, softer the higher it is (a few layers, no blur)
typedef struct { float x, y, z; } WallGround;
void wall_ribbon_shadow(Surf *s, const WallGround *g, int n, float half, float alpha, float blur);
// the perspective the page lifts things with (lifted()): a point z px above the table at (x, y)
void wall_lifted(float x, float y, float z, float *ox, float *oy, float *os);
// a strip torn off from one end (peelStrip): x0, y0 its free end on the table, ang the way it runs, L its length on the screen, lf how far the fold has run, psi the angle
// the flap rises at, roll the radius at the fold, vis the length of flap drawn at full strength (then it fades over `fade`), back the pale back's colour (0..255).
// `baked` is the stuck strip with its shadow, turned so that it runs along +x from (bx, by) (its free end), for the part still stuck.
typedef struct { float x0, y0, ang, L, sc, lf, psi, roll, vis, fade, alpha; float back[3]; const PaperSpr *baked; const int16_t *runs; int bx, by; } WallPeel;
void wall_peel(Surf *s, const WallBox *box, const WallPeel *o);

#endif
