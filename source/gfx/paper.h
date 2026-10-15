// Washi's drawing library: the colours, the seeded random numbers, and everything the look is made of, baked once into straight-alpha
// RGBA sprites (paper cards, washi tape and its peel, crayon marks, the ghost patch, card shadows), plus the lift and wiggle maths.
// Pure C on top of gfx.h, no platform code; the same seed gives the same sprite on the PC and the console. Sprites are drawn later with
// gfx_blit_over (still) or gfx_blit_xform (lifted and turned); nothing here draws per frame.
#ifndef PAPER_H
#define PAPER_H

#include <stddef.h>
#include <stdint.h>

#include "gfx.h"

// ---- tokens: every colour of the look, as 0xRRGGBB; nothing is hard-coded anywhere else
#define PAPER_BOARD 0xF1EBDD      // the pinboard
#define PAPER_PAPER 0xFFFEFB      // a card
#define PAPER_TEXT 0x2D2D2D
#define PAPER_TEXT_SOFT 0x5A5A5A
#define PAPER_THREAD 0x7A7590     // dashed guides, quiet lines
#define PAPER_INK 0x2F3A8F        // the one state colour (stamp, step marks)
#define PAPER_GHOST_FILL 0xFAF6EC // the patch left on the board where a card was
#define PAPER_GHOST_EDGE 0xDCD1B6
#define PAPER_CRAYON_1 0x3F3A5A   // first crayon pass
#define PAPER_CRAYON_2 0x5E5778   // second pass
#define PAPER_SHADOW 0x4A3A1A     // shadow tint: warm brown
#define PAPER_TITLE_STRIP 0xD6FACA       // the printed strip the name is on, and its folded end
#define PAPER_TITLE_FOLD 0xECFDE6
#define PAPER_NIGHT_TITLE_STRIP 0xB4BCF0 // the same in Paper night (the stamp's pale blue ink)
#define PAPER_NIGHT_TITLE_FOLD 0xDDE1F8
#define PAPER_TAPE_COUNT 8
#define PAPER_TAPE_LIST 0xFFCDCC, 0xFFE3CD, 0xFFFF9A, 0xD4FAC7, 0xBFEFE1, 0xAED6F9, 0xE4CCFE, 0xFFCCFA
extern uint32_t paper_tape_rgb[PAPER_TAPE_COUNT]; // (the palette's: paper_pal_set fills it, the light theme's are PAPER_TAPE_LIST)

// ---- the palette: every colour of the look that the screens read as they draw, PAL(field, value) at each place (the value is the light theme's, there to
// read the code by). paper_pal_set picks the light theme (cream) or Paper night (the dark theme); a cached picture made from it checks paper_pal_serial.
// (Built with -DWASHI_PAL_LAB, the mockup tools' other palettes and materials come in too: WASHI_PAL=a..o, see paper.c.)
typedef struct {
    uint32_t board, paper, text, text_soft, thread, ink, ghost_fill, ghost_edge, crayon_1, crayon_2, shadow;
    float fleck[3], fleck_k, vignette[3], vignette_k; // the board: its flecks (0..255) and how strongly they show, the colour its corners go towards and how far
    float grain[3], grain_k;                          // a card's grain: multiplied in (grain_mode 0: the light paper's warm brown) or laid on as a colour (1: 0..1)
    int grain_mode;
    uint32_t rim;                                     // the faint line just outside a card's cut edge
    float rim_a, shadow_k;                            // how strong it is; how strong the cards' shadows are (times the app's)
    float tape_board[3], tape_paper[3], tape_alpha;   // what a frosted strip on a card lets through (the board's mean colour, the paper's; 0..1), how much it hides
    uint32_t tape_shadow;                             // the strips' contact shadows
    uint32_t panel_alt, line, panel_rim;              // the theme's second paper and its lines; the line round a flat panel (footer, status tag)
    float panel_rim_a, scrim[4];
    uint32_t mark_ink, board_thread, ink_on;          // the seal in the corner (on the board), marks on the board, a letter on an ink disc
    int glyph_flat;                                   // 1: the buttons' marks are outlined on the paper (as the console draws them), not on pastel discs
    uint32_t glyph_ink, glyph_ring;                   // a button mark's letter and ring
    uint32_t sign_fill, sign_disc, sign_ink, x_fill;  // the Sign out sticker, its X's disc, its writing; the X button's disc in the footer
    uint32_t new_disc, new_ink;                       // the New profile tile's disc and plus
    uint32_t rail_arrow, rail_on, rail_off, title_ink; // the A-Z rail's arrows and letters (with people, without), the name on the title strip
    float hi_mix;                                     // the crayon highlight under the loaded name, taken this far towards the paper (light writing over it)
    // the other pages': a slip that is not a coloured one (the quiet button, the pad's and the keyboard's keys; a row of a choice), the writing of
    // something that cannot be chosen, a toggle that is off (and its dashed edge), the soft patch pictures sit on, the log's rules
    uint32_t quiet, slip, dim, pill_off, pill_edge, patch, rule;
    // a switch's paper knob, the red and the green of a note written on paper (a wrong password; ON), the dashed guide a switch's tape goes in, and
    // the soft shadow under a paper disc (its alpha: how much stronger than the light theme's; see pal_disc_shadow)
    uint32_t knob, bad_ink, ok_ink, toggle_on; // (toggle_on: a list's switch that is on)
    uint32_t flap, flap_line;                  // a picture sticker's curled corner: its back, and the fold
    uint32_t step_dim;                         // the number of a step still to come (the progress screen)
    float doodle_lift;                         // the "still working" crayons, taken this far towards white (they draw on the footer's paper)
    Col sw_guide, disc_shadow;
    uint32_t go;                                     // the button that says yes (the light theme's mint; Paper night's lit window)
    uint32_t title_strip, title_fold;                // the printed strip the name is on, and its folded end
    uint32_t tab_fill[4], tab_strip[4];              // the hub's tabs (0: the light theme's way, tape pastels): a closed tab's paper, and the open one's strip
    uint32_t tape[PAPER_TAPE_COUNT];                 // the washi tape's colours (paper_tape_rgb is made of them)
    int panels_paper;                                // the footer's strip and the status tag are made of the cards' paper
    int board_scene;                                  // 2: the board is Paper night's scene cut from paper (1: the mockup's first version of it)
#ifdef WASHI_PAL_LAB
    // mockups only: the other rounds' materials
    float fib_n, fib_k;                               // fibres laid in the paper (per 10 000 px of card) and how strongly they show
    uint32_t fib_rgb;
    float cloud_k;                                    // the paper's thickness wandering (lit from behind, thick is darker)
    float lantern_k;                                  // lit from behind: how far the colour goes towards lantern_edge at the card's edges
    uint32_t lantern_edge;
    float leaf_n;                                     // gold leaf flecks (per 100 000 px of card), their bright and dark gold
    uint32_t leaf_rgb, leaf_dark;
    float dust_k;                                     // chalk dust over a card (a chalkboard label)
    int tape_mul;                                     // the light comes through the tape (multiplied over the paper), as on a lantern
    int board_chalk;                                  // the board is a chalkboard: dust, eraser swipes, old writing rubbed out
    float text_grain;                                 // light writing is chalk: how much of each letter the chalk misses
    float fib_len, fib_w, fib_bend;                   // fibres: the longest (px), and width and bend times the second round's (0: as they were)
    int leaf_corner;                                  // the leaf gathers towards a card's corners, clear of its writing
    float leaf_dust, leaf_sq, leaf_noge;              // gold dust (sunago), cut squares (kirihaku) and slivers (noge), each per 100 000 px of card
    float crayon_wk;                                  // the rings' stroke width (0: the app's)
    float board_sheen;                                // a lacquered board's soft highlights
    float board_ribs;                                 // a cardboard board: its flutes showing faintly through the liner
    int board_box;                                    // printing on the cardboard, rubbed and faded
    int board_felt;                                   // a game table's felt: its fuzz, a lamp's pool of light, gold arcs, dice, a d20, a meeple, tokens
    int board_peg;                                    // a painted pegboard: rows of holes, the app's tape on brass hooks, torn pieces stuck on for later
    int card_index;                                   // each card is a playing card: a border line, the person's initial and a suit in its corner
    int board_passport;                               // a passport page: gold guilloche (a rosette, wave bands); its visa stamps are skin.c's
    int board_blueprint;                              // a blueprint: cyanotype paper and its grid; the club room's plan on it is skin.c's
    float guil_k;                                     // fine security-print waves over each card, this strong
    uint32_t guil_rgb;
#endif
} PaperPal;
extern PaperPal g_pal;
extern unsigned paper_pal_serial; // changes whenever the palette does: what a cached picture was made from
void paper_pal_set(int night);    // 0 the light theme (cream), 1 Paper night
// for a cache of pictures made from the palette, keeping the serial it was made at in *serial: 1 (once) when the palette changed since, and the cache
// is to be emptied
static inline int paper_pal_stale(unsigned *serial)
{
    if (*serial == paper_pal_serial) return 0;
    *serial = paper_pal_serial;
    return 1;
}
#define PAL(f, v) (g_pal.f)
#define PAL3(f, i, v) (g_pal.f[i])
// the soft shadow under a paper disc (a switch's knob, a round button), as strong as a is in the light theme
static inline Col pal_disc_shadow(float a)
{
    Col k = g_pal.disc_shadow;
    k.a *= a;
    return k;
}
// a light colour (dark writing goes on it), by its brightness
static inline int pal_light(uint32_t rgb) { return ((rgb >> 16) & 255) * 299 + ((rgb >> 8) & 255) * 587 + (rgb & 255) * 114 > 128000; }
static inline int col_light(Col c) { return c.r * 0.299f + c.g * 0.587f + c.b * 0.114f > 0.502f; }
// a colour taken t (0..1) of the way from a to b
static inline uint32_t pal_mix(uint32_t a, uint32_t b, float t)
{
    uint32_t o = 0;
    for (int s = 16; s >= 0; s -= 8) {
        float x = ((a >> s) & 255) + (((b >> s) & 255) - (float)((a >> s) & 255)) * t;
        o |= (uint32_t)(x + 0.5f) << s;
    }
    return o;
}
#ifdef WASHI_PAL_LAB
void pal_suit(Surf *s, float cx, float cy, float size, int suit, Col c); // (mockups) a card suit about size px tall: 0 spade, 1 heart, 2 diamond, 3 club
#endif

// WCAG contrast ratio of two colours (1..21)
float paper_contrast(uint32_t rgb_a, uint32_t rgb_b);

// ---- seeded random numbers (xorshift32): the same seed gives the same numbers everywhere
typedef struct { uint32_t s; } PaperRng;
void paper_rng_seed(PaperRng *r, uint32_t seed);
uint32_t paper_rng_u32(PaperRng *r);
float paper_rng_f(PaperRng *r);                      // 0 <= x < 1
float paper_rng_range(PaperRng *r, float a, float b); // a <= x < b

// ---- sprites. px is straight-alpha RGBA, w x h. (ox, oy) is the figure's own anchor inside the sprite: for cards, ghosts and shadows
// the top left corner of the card, for tape and marks the middle (and for an underline its left end). To turn or scale a sprite about
// its middle, give gfx_blit_xform / paper_spr_xform the point where that middle should land.
typedef struct { int w, h, ox, oy; uint8_t *px; } PaperSpr;
PaperSpr *paper_spr_new(int w, int h); // clear, counted by paper_stats()
void paper_spr_free(PaperSpr *p);      // NULL is fine
// puts sprite `p` over `dst` (both straight alpha) with p's anchor at (ax, ay) in dst; dst may be a plain sprite you made with paper_spr_new
void paper_spr_over(PaperSpr *dst, const PaperSpr *p, int ax, int ay);
// draws on a surface with the anchor at (ax, ay), whole pixels (gfx_blit_over)
void paper_spr_put(Surf *s, const PaperSpr *p, int ax, int ay);
// draws turned (radians, clockwise) and scaled about the sprite's middle, which lands on (cx, cy) (gfx_blit_xform)
void paper_spr_xform(Surf *s, const PaperSpr *p, float cx, float cy, float scale, float angle, float alpha);

// What the sprites hold, so memory can be budgeted. Counts only sprites made here and not yet freed.
typedef struct { size_t bytes; int count; } PaperStats;
PaperStats paper_stats(void);

// ---- the bake helper: draws twice, once on black and once on white, and solves colour and transparency from the two, so ordinary
// drawing code (blends onto an opaque surface) makes a straight-alpha sprite. The surface starts filled; draw() paints on it.
typedef void (*PaperDraw)(Surf *s, void *ud);
PaperSpr *paper_bake(int w, int h, PaperDraw draw, void *ud);

// ---- the pinboard: fills a surface with the board colour and its fine warm speckle (any size; seeded)
void paper_board_fill(Surf *s, uint32_t seed);
extern int g_paper_board_flat; // tests: 1 = a plain board without the flecks (the real one is not the same under a shift by whole pixels)
// (2 = a black board, 3 = a white one: for the intro mockups, which draw a screen on each to get the alpha of what is on it)
extern float g_paper_pat_scale; // mockups only: 1 = the tape patterns as the app prints them; bigger makes the pattern bigger with the strip

// ---- paper cards. A card sprite is the card (w x h, corner radius r) with `pad` px of clear room all round (for the cut edge, the
// faint edge line and tape that overhangs when you compose them together); the card's top left is at (pad, pad).
#define PAPER_CARD_PAD 14
PaperSpr *paper_card(int w, int h, float radius, uint32_t seed);
PaperSpr *paper_card_ex(int w, int h, float radius, uint32_t seed, int pad);

// ---- washi tape
enum { PAT_STRIPES, PAT_DOTS, PAT_GRID, PAT_ZIG, PAT_CHECK, PAT_SCALLOP, PAT_HLINE, PAT_WSTRIPE, PAT_COUNT };
#define PAPER_TAPE_ZIG_L 1  // zigzag cut at the left end (else straight)
#define PAPER_TAPE_ZIG_R 2
#define PAPER_TAPE_ZIG (PAPER_TAPE_ZIG_L | PAPER_TAPE_ZIG_R)
#define PAPER_TAPE_CRIMP 4  // pressed down in the middle: a ridge, and the far half sits a little out of line
#define PAPER_TAPE_EDGE 8   // a hairline and a soft shadow round the strip, for strips laid on the board
#define PAPER_TAPE_SLANT_L 16 // an end cut with scissors at a slant (12 to 30% of the width; which way and how much come from the seed);
#define PAPER_TAPE_SLANT_R 32 // a zigzag on the same end wins
#define PAPER_TAPE_OPAQUE 64  // hides what is under it (the title's strip and its fold, the logo's tape), not the translucent tape of a card
#define PAPER_TAPE_GRAINY 128 // grained the way the mockup's print filter does it (a hair darker), as the title's strip and the logo's tape
#define PAPER_TAPE_COARSE 256 // five teeth to a zigzag end whatever the width (the title's strip), not one to every 8 px
#define PAPER_TAPE_PLAIN 512  // no pattern, only the colour (the title's fold)
#define PAPER_TAPE_PAT(n) ((unsigned)(n) << 10) // the pattern at n/16 of its strength, n = 1..15 (no PAT: full strength)
#define PAPER_TAPE_UPRIGHT 16384 // baked without the tilt (the strip lies along the sprite's x axis); the tilt is still how it lies on its card, for the marks
// Tape is only lightly pressed on: it is see-through (82%), it throws a faint contact shadow, and where it goes over the edge of the card it is stuck to
// (host_w > 0) it does not hug it: a pale haze in patches where it bridges the edge, a loose tent across a rounded corner, small pleats where a finger
// pushed it down. Nothing follows the card's outline. The marks fade as the strip peels.
typedef struct {
    int len, wid;      // length along the strip, width across it, px
    uint32_t rgb;      // PAPER_TAPE_LIST colour or any other
    int kind;          // PAT_*
    unsigned flags;    // PAPER_TAPE_*
    float tilt;        // degrees, clockwise, baked into the sprite
    uint32_t seed;     // the crimp's jog and the paper's fibres
    float host_w, host_h, host_r; // the card it is stuck to: its size and corner radius (0: not on a card, no marks)
    float host_x, host_y;         // where the strip's top left corner is from the card's top left, before the tilt (what paper_tape_offset takes)
} PaperTape;
// says which card the strip is stuck to (see PaperTape); x, y as for paper_tape_offset
static inline void paper_tape_host(PaperTape *t, float card_w, float card_h, float radius, float x, float y)
{
    t->host_w = card_w;
    t->host_h = card_h;
    t->host_r = radius;
    t->host_x = x;
    t->host_y = y;
}
// one strip, flat on the paper: its middle is the sprite's middle (so it is drawn about the point where the strip's middle should be)
PaperSpr *paper_tape(const PaperTape *t);
// The soft shadow the shape of `src` (its alpha) casts, as a sprite with the same anchor: moved (dx, dy), blurred like an SVG drop shadow of
// that standard deviation, in `rgb` at `alpha`. Draw it first and the shape over it. NULL when out of memory.
PaperSpr *paper_shadow_of(const PaperSpr *src, float dx, float dy, float sigma, uint32_t rgb, float alpha);
// the peel: a strip that is stuck, then peels up from its right end into a foreshortened, lighter flap that is folded over and casts a
// shadow. n states from stuck (0) to fully peeled (n-1) in one common sprite size and anchor, so they can be stepped through.
#define PAPER_PEEL_STATES 6
#define PAPER_PEEL_MAX 12
typedef struct { int n; PaperSpr *s[PAPER_PEEL_MAX]; int w, h; void *geo; } PaperPeel;
PaperPeel *paper_tape_peel(const PaperTape *t, int n); // every state made now
// The same, but only state 0 (the stuck strip: its size is every state's) is made now; each other state is made the first time paper_peel_at asks for it, so a screen
// with many taped items only pays for the peels that are really seen. (s[i] is NULL until then.)
PaperPeel *paper_tape_peel_lazy(const PaperTape *t, int n);
void paper_peel_free(PaperPeel *p);
const PaperSpr *paper_peel_at(const PaperPeel *p, float lift); // the state for a lift 0..1
// where something taped to a card's corner is centred relative to the card's centre, from the strip's top left in card coordinates
static inline void paper_tape_offset(float card_w, float card_h, float x, float y, float len, float wid, float *dx, float *dy)
{
    *dx = x + len * 0.5f - card_w * 0.5f;
    *dy = y + wid * 0.5f - card_h * 0.5f;
}
// A strip put on by hand. *t and the strip's top left (*x, *y, from the card's top left) are what the page asked for; h (the item's own number, the same
// from frame to frame) makes it a little different from its neighbours: moved off the corner, turned, longer or shorter, its ends cut this way or that,
// sometimes pressed down with a ridge. spread 1 = as much as a Backups tile; 0 leaves it as asked. Returns the lift (for paper_peel_at) at which to
// draw it, 0 when it is stuck flat all along, for the few whose free end did not take. Call it before paper_tape_offset and paper_tape_host.
float paper_tape_hand(PaperTape *t, float *x, float *y, uint32_t h, float spread);

// ---- crayon marks. Hand-drawn paths with seeded jitter, two passes, edges broken up by wax grain. (ox, oy) as described above.
typedef struct {
    uint32_t c1, c2; // the two passes
    float a1, a2;    // their opacities (0..1)
    float wk;        // stroke width multiplier
} PaperCrayon;
extern PaperCrayon PAPER_CRAYON_STD; // CRAYON_1 and CRAYON_2 (the palette's crayon_1 and crayon_2): the rings round pictures
extern PaperCrayon PAPER_CRAYON_BLUE; // ink blue (the palette's ink) both passes: the step marks and the heading underline
PaperSpr *paper_ring(float radius, const PaperCrayon *st, uint32_t seed);          // middle at the circle's centre
PaperSpr *paper_tick(float size, const PaperCrayon *st, uint32_t seed);            // a hand-drawn check mark, middle at the box's centre
PaperSpr *paper_underline(int len, const PaperCrayon *st, uint32_t seed);          // anchor at the left end of the stroke
PaperSpr *paper_wave(int len, const PaperCrayon *c, uint32_t seed); // two waves of crayon, one under the other (the About title)
PaperSpr *paper_stamp_frame(int w, int h, uint32_t seed, uint32_t ink);                   // a rubber stamp's clean double ink frame, middle at the box's centre
void paper_stamp_weather(PaperSpr *p, uint32_t seed, float strength);                     // ages a stamp's impression: lighter patches and fine pinholes, shapes stay clean
PaperSpr *paper_scribble(int size, uint32_t fill_rgb, const PaperCrayon *st, uint32_t seed); // a square coloured in, with a drawn frame
// a circle of radius r coloured in by hand (no outline: it goes under a ring): back-and-forth crayon strokes close together at a slant of their own,
// a few crossing them, so it is nearly all covered; cut to the circle with a ragged edge. Each seed colours it in another way. Middle at its centre.
// order (may be NULL): set to a map, one byte a pixel of the sprite, of how far through the scribbling (0..255) each pixel is reached, for drawing
// it in as it is scribbled (free() it)
PaperSpr *paper_disc_scribble(float r, uint32_t rgb, float opacity, uint32_t seed, uint8_t **order);
PaperSpr *paper_star(int size, uint32_t rgb, uint32_t seed); // a five-pointed star `size` px across, coloured in with pastel wax and outlined by hand; middle at its centre
// the same for the banner's other doodles (PAPER_SHAPE_STAR is paper_star itself)
enum { PAPER_SHAPE_STAR, PAPER_SHAPE_HEART, PAPER_SHAPE_FLOWER, PAPER_SHAPE_CLOUD, PAPER_SHAPE_MOON, PAPER_SHAPE_SPARKLE, PAPER_SHAPE_BURST, PAPER_SHAPE_BADGE, PAPER_SHAPE_COUNT };
PaperSpr *paper_shape(int kind, int size, uint32_t rgb, uint32_t seed);
PaperSpr *paper_seal(float R, uint32_t rgb, uint32_t seed); // the logo: a hand-pressed ink seal of radius R with a ring, dots and the person cut out; anchored at its middle
// a loose patch of back-and-forth strokes w x h px (like a highlighter laid on in crayon); anchored at the left end, half way down
PaperSpr *paper_hatch(int w, int h, uint32_t rgb, float opacity, uint32_t seed);
// the dashed patch left where a card was lifted (fill GHOST_FILL, 2 px dashed GHOST_EDGE round the card's outline): a card of w x h
// sits inside it; the sprite is 2 px bigger on every side and the anchor is the card's top left
PaperSpr *paper_ghost(int w, int h, float radius);

// ---- shadows: the card's shadow at n lift levels (default 8). Level i has p = i / (n - 1): `0 (2+14p)px (5+12p)px`, alpha .22 + .22p,
// the card grown by 1 + grow * p. Every level is the same size with the card's top left (unscaled) at the same anchor, so a card at lift
// p puts level paper_shadow_level() with its anchor at (card x, card y - rise * p), and the shadow falls where the card's own would.
#define PAPER_SHADOW_LEVELS 8
#define PAPER_SHADOW_MAX 16
typedef struct { int n; PaperSpr *s[PAPER_SHADOW_MAX]; } PaperShadows;
PaperShadows *paper_shadows(int w, int h, float radius, float grow, int n);
void paper_shadows_free(PaperShadows *s);
int paper_shadow_level(const PaperShadows *s, float lift);
void paper_shadow_params(float p, float *dy, float *blur, float *alpha); // the CSS numbers above

// ---- lift maths. One Lift per thing that can have the focus. All times are milliseconds on any clock the caller likes.
typedef struct { float rise, scale, dx; } PaperLiftK; // px it rises, how much bigger it gets, px it slides right
enum { LIFT_TILE, LIFT_ROW, LIFT_BTN, LIFT_TAB, LIFT_KEY, LIFT_KINDS }; // LIFT_KEY: a key of the keypad or keyboard (a button's lift, a softer sound)
extern const PaperLiftK paper_lift_k[LIFT_KINDS];
#define PAPER_LIFT_UP_MS 133    // ease-out cubic
#define PAPER_LIFT_DOWN_MS 100  // smoothstep
#define PAPER_LIFT_UP_HELD_MS 80
#define PAPER_LIFT_DOWN_HELD_MS 60
#define PAPER_WIGGLE_MS 170
#define PAPER_BUMP_MS 130       // the refusal bump at the end of a row
#define PAPER_PRESS_MS 70       // the little squash when a card lands
typedef struct {
    int kind;
    float from;                  // the lift when the last grab or release began
    int target;                  // 1 rising, 0 falling
    double t0, dur;              // when it began, how long it takes
    double wig_t0;               // when the wiggle began, -1 for none
    float wig_rest;              // degrees (times the lift) it rests at when no wiggle is running
    double bump_t0;              // -1 for none
    int tapeless;                // nothing is taped on it (1: the search bar, a button; 2: a push pin holds it): it sounds like it (set after paper_lift_init)
} PaperLift;
#define LIFT_TAPELESS 0x100      // with the kind in the hook: what lifts has no tape on it
#define LIFT_PINNED 0x200        // with the kind in the hook: what lifts is held by a push pin (PaperLift.tapeless 2)
// Called when a lift is grabbed (grab 1) or let go (grab 0), held = on a held D-pad's repeat, kind = the lift's kind (| LIFT_TAPELESS): the interface hangs
// its sounds here.
extern void (*paper_lift_hook)(int grab, int held, int kind);
void paper_lift_init(PaperLift *l, int kind);
void paper_lift_grab(PaperLift *l, double now, int held);    // held: a held D-pad repeat, quicker and without the wiggle
void paper_lift_release(PaperLift *l, double now, int held);
void paper_lift_bump(PaperLift *l, double now);
void paper_lift_wiggle_again(PaperLift *l, double now);      // a press on an item that is up: it wiggles once more, as when it landed
float paper_lift_value(const PaperLift *l, double now);      // 0 down .. 1 up; grab and release start from this, so they can interrupt
float paper_wiggle_at(double ms);                            // the wiggle table, degrees; ms since it began
float paper_wiggle_k(float width);                           // wide things wiggle less: min(1, 165 / width)
float paper_lift_wiggle(const PaperLift *l, double now, float width); // degrees: the table, then rests, then fades out with the lift
int paper_lift_busy(const PaperLift *l, double now);         // anything still moving: lift, wiggle, bump or the landing squash
typedef struct {
    float p;         // lift 0..1
    float x, y;      // px offset of the card's middle (slide, bump, rise: y is negative)
    float scale;     // 1 + grow
    float angle;     // radians, clockwise
    float ghost;     // opacity of the patch left behind, 0..1
} PaperPose;
void paper_lift_pose(const PaperLift *l, double now, float width, PaperPose *o);

// Where a thing fixed to a card (tape, a stamp) goes when the card is moved: the card's pose and its own middle in screen px, and the
// thing's middle relative to the card's middle at rest. Draw the thing with paper_spr_xform at (x, y), scale, angle.
typedef struct { float x, y, scale, angle; } PaperXf;
PaperXf paper_xf_attach(PaperXf card, float dx, float dy);

#endif
