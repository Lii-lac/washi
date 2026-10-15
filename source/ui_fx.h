// Drawing paper in three dimensions, for the order menu's sleeve of stickers (ui_stickfx.c): a light in front and a little above the board,
// a sticker drawn wherever it is and however it is turned or curled off the board (a face turned from the light a little darker), soft
// shadows on the board. Sprites read here are premultiplied (fx_premultiply a copy of the app's straight-alpha ones first).
#ifndef UI_FX_H
#define UI_FX_H

#include "gfx/paper.h"
#include "ui.h"

// how lit a face whose normal (towards the eye's side) is (0, ny, nz) is, against a card lying flat on the board (1)
float fx_light(double ny, double nz);
void fx_premultiply(PaperSpr *p);

// A card curling off the board (a sticker peeled by its corner, or laid down an end first and smoothed on), seen from straight in front:
// on one side of a straight fold line it lies flat; past the line it goes up round a roll of radius r for `wrap` radians, then on straight
// (wrap more than a right angle: it leans back over itself, its back showing). The fold line is in the card's own px from its middle:
// through (lx, ly), the free side the way (nx, ny) points (a unit vector). The card (front and back sprites, premultiplied, the same size,
// the card in their middle; the back is read at the same point of the card as the front) has its middle at (cx, cy), sc as big, turned ang
// (radians, clockwise). What is lifted throws a soft shadow (shadow_rgb at shadow_a) on what is under it.
typedef struct {
    double lx, ly, nx, ny, r, wrap;
} FxFold;
void fx_curl(Surf *dst, const PaperSpr *front, const PaperSpr *back, double cx, double cy, double sc, double ang, const FxFold *fold,
             uint32_t shadow_rgb, float shadow_a, Rect clip);
// the same, alpha as strong (fading out: the card and its shadow)
void fx_curl_a(Surf *dst, const PaperSpr *front, const PaperSpr *back, double cx, double cy, double sc, double ang, const FxFold *fold,
               uint32_t shadow_rgb, float shadow_a, float alpha, Rect clip);

// a sprite with its middle at (cx, cy), sc as big, turned ang (radians, clockwise), alpha as strong; margin: clear room round its edge
void fx_affine(Surf *dst, const PaperSpr *sp, double cx, double cy, double sc, double ang, double margin, float alpha, Rect clip);
// a soft shadow on the board: the box x0..x1, y0..y1 blurred by sigma px, in rgb, a strong
void fx_soft_box(Surf *dst, float x0, float y0, float x1, float y1, float sigma, uint32_t rgb, float a, Rect clip);

#endif
