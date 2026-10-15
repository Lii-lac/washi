// Cached pictures of items (with their shadow) that a page lays over its background and slides about. An item is drawn twice, on black and
// on white, and the difference gives each pixel's coverage, so the picture can be laid over the page anywhere, whole or faint. Most of an
// item is solid: those runs are copied as they are.
#ifndef UI_SPR_H
#define UI_SPR_H

#include "ui_internal.h"

#define SPR_M 44 // room round an item for its shadow (the deep one of an item that is up)

typedef struct {
    int w, h;
    uint8_t *px;      // premultiplied RGBA
    int16_t *r0, *r1; // per row: the solid run [r0, r1)
    unsigned used;    // the frame it was last wanted in
} Spr;

// draws the item with its top left at (x, y)
typedef void (*SprDraw)(Cx *c, void *ud, float x, float y);

void spr_free_one(Spr *s);
// makes the picture of an item w x h (its corner at (SPR_M, SPR_M) in the picture)
void spr_bake(Spr *s, int item_w, int item_h, SprDraw draw, void *ud);
// lays the picture over dst with its item's corner at (x, y), k (0..256) as strong
void spr_blit(Surf *dst, const Spr *s, int x, int y, int k);
// row y of `dst` taken `keep` (0..1) of the way from `ground` (the page without the items) to what is drawn
void fade_row(Surf *dst, const Surf *ground, int y, float keep);
// the part of rows y0..y1 from column x0 to x1, put back as the page had it (fade 0) or faded to it from x0 to x1 (1: what is drawn at x0
// stays and is gone by x1; 2: the other way round, for the left edge)
void ground_cols(Surf *dst, const Surf *ground, int y0, int y1, int x0, int x1, int fade);

#endif
