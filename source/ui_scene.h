// The scene toolkit (ui_scene.c): paper items that lift on a still base, painted by dirty rectangles. Not for the app.
#ifndef UI_SCENE_H
#define UI_SCENE_H

#include "gfx/paper.h"
#include "ui_internal.h"

// What an item looks like: a card (coloured, or white paper), a label, and a button's round letter.
typedef struct {
    float w, h, r;
    uint32_t fill; // 0: the paper card sprite
    const PaperSpr *card;
    const char *label;
    Font *font;
    float px;
    Col ink;
    const char *glyph;
    uint32_t gfill;
    Col gring;
    const char *sub; // a second, quieter line under the label
    void (*paint)(Cx *c, void *ud, float ox, float oy); // draws the whole inside instead of the label (ox, oy: the card's top left)
    void *pud;
} IBake;

enum { D_PAD = 14, D_RPAD = 22 };

typedef struct {
    PaperSpr *body; // the card and what is printed on it, D_PAD of room all round: what is lifted and turned
    PaperSpr *rest; // the card as it sits: its quiet shadow, the tape stuck on; its card's top left is at (rox, roy)
    int rox, roy;
} Sprs;

typedef struct {
    int active;
    float x, y, w, h, r;      // the card at rest
    int lk;                   // lift kind
    float grow;               // how much bigger it gets when up
    PaperLift lift;
    Sprs sp[2];               // two looks (the keyboard's two cases)
    int var;
    PaperSpr *blend;          // a second body drawn over the first, `mix` strong (the sealed button going solid)
    float mix;
    const PaperShadows *sh;
    const PaperSpr *ghost;
    PaperSpr *tape;           // a strip stuck across it (and the peel states of it)
    PaperPeel *peel;
    float tilt;               // radians: how it lies at rest (kept while it is up)
    float tdx, tdy;
    // this frame
    int moving;
    PaperPose ps;
    float lx, ly, cx, cy, sc, ang;
    float pk[12], pk_prev[12];
    Rect bb, bb_prev;
    int drawn, force;
} DItem;

enum { SC_MAX = 48 };

typedef struct {
    Surf *base; // everything that never moves
    DItem it[SC_MAX];
    int n, cur;
    Bump lean;
    int lean_item;
    void (*extra)(void *ud, Surf *s, int ox, int oy, const Rect *R); // drawn over the base, under the items (typed text, the dots)
    void *extra_ud;
    Rect inval[8];
    int ninval, force_full;
} Scene;


// ---- baked pieces, cached
const PaperSpr *card_get(int w, int h, float r, uint32_t seed, int pad);
const PaperSpr *ring_get(float radius, uint32_t seed, const PaperCrayon *st);
const PaperSpr *ghost_get(int w, int h, float r);
const PaperShadows *shadows_get(int w, int h, float r, float grow);

// ---- items and the scene
Scene *scene_new(Surf *base);
void scene_free(Scene *sc);
DItem *item_add(Scene *sc, float x, float y, float w, float h, float r, int lk, float grow);
PaperSpr *body_bake(const DItem *it, const IBake *ib);
void item_bake(DItem *it, int var, const IBake *ib);
void item_tape(DItem *it, const PaperTape *t, float tx, float ty);
void item_free(DItem *it);
// Makes this frame: poses, what changed, the changed rectangles painted into G.work and shown. first: the whole screen, as a full
// present. Returns 1 while anything is moving.
int scene_frame(Scene *sc, int first);
void scene_inval(Scene *sc, int x, int y, int w, int h);
void scene_focus(Scene *sc, int want, int held); // the old one sticks back down, the new one peels up
void scene_start_up(Scene *sc, int i);           // starts with item i already up and its wiggle done
void scene_press(Scene *sc, int i);              // item i pushes flat for a moment (an A press or tap, just before what it does)
int scene_hit(const Scene *sc, float x, float y);
int lean_dir(uint64_t b, int *dx, int *dy);      // a D-pad press as a direction to lean (0, 0 if there was none)
void scene_lean(Scene *sc, int item, int dx, int dy);
int rect_hit(const Rect *a, const Rect *b);
void put(Surf *s, const PaperSpr *p, int x, int y, int ox, int oy);

// ---- page pieces: the title slip (its width is returned), a page's board + top bar + title + footer, a taped card laid on the board
#define SCENE_TAG_H 46
#define SCENE_PAD 40
float scene_title_tag(Surf *dst, const char *title, float x, float y);
void scene_chrome(Surf *dst, const char *title, const char *sub, const Prompt *prompts, int nprompts);
typedef struct {
    PaperTape t;
    float x, y; // the strip's top left corner from the card's top left
} SceneTape;
// draws what is printed on the card; (ox, oy) is where the card's top left is on `s`
typedef void (*SceneContent)(Surf *s, float ox, float oy, void *ud);
// A card (w x h, radius r) turned tilt_deg, with its shadow (shadow 1: the deeper one), the strips, and `content` on it
void scene_card_on(Surf *dst, float x, float y, float w, float h, float r, uint32_t seed, float tilt_deg, int shadow, const SceneTape *tp, int ntp,
                   SceneContent content, void *ud);
void scene_dashed_rrect(Surf *s, float x, float y, float w, float h, float r, float width, float dash, float gap, Col k);// A dialog opened over the screen (ui_dialogs.c): G.dim = the cream-dimmed screen with the taped paper card (x, y, w, h) on it; its first frame
// rises over it. Returns a context to print on G.dim.
Cx dlg_open(float x, float y, float w, float h, float tilt_deg, uint32_t seed);

#endif
