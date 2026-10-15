// Push pins: a pinned profile's card is held by a push pin instead of tape. The pin is a modelled, lit object (tools/pins: rendered on a PC with
// its shadow traced through it), so its pictures are made beforehand, data/pins.bin, and only laid down here: the pin at a few heights off its
// card (h px up, drawn K*h px higher), its shadow on the card at many heights (crossfaded between the two nearest), and the dent in the paper round
// a pin that is home. A light-theme set and a Paper night set; the pin comes in the tape's eight colours (the slots of paper_tape_rgb).
//
// data/pins.bin: a PinsHead, then each theme's block, raw DEFLATE (gfx_inflate_raw), holding its sprites one after another, each
// {u16 w, u16 h, i16 ox, i16 oy} and w*h straight-alpha RGBA: the pins (colour by colour, each at every pin height), the shadows (every shadow
// height), the dent. Little-endian. Made by tools/pins/make_pins.c.
#ifndef PINS_H
#define PINS_H
#include <stddef.h>
#include <stdint.h>
#include "gfx.h"

#define PINS_MAGIC 0x314E4950u // "PIN1"
#define PINS_VERSION 1u
#define PINS_THEMES 2          // 0 the light theme, 1 Paper night
#define PINS_COLOURS 8         // the tape's colour slots
#define PINS_PIN_HS 4
#define PINS_SHADOW_HS 18

typedef struct {
    uint32_t magic, version;
    float px_mm, k;                    // the pin's scale (px per mm); the view: h px up is drawn k*h px higher
    float pin_h[PINS_PIN_HS];          // the heights the pin was made at (px off the card, rising)
    float shadow_h[PINS_SHADOW_HS];    // and its shadow
    uint32_t raw[PINS_THEMES];         // each theme's block: its size inflated
    uint32_t packed[PINS_THEMES];      // and in the blob (the blocks follow the head, theme 0 first)
} PinsHead;

// the blob, made beforehand (main.c hands over the one built into the app; the PC tests read data/pins.bin). Kept, not copied.
void pins_set_blob(const void *blob, size_t n);
// the pictures for the palette in use are at hand (made ready on the first call after the palette changed); 0: no blob or a broken one, and
// nothing is drawn
int pins_ready(void);
// the pin with its point at (x, y), h px up off its card, sc times its size, in tape colour slot `slot`
void pins_draw(Surf *s, float x, float y, float h, float sc, int slot, float alpha);
// its shadow on the card (the pin h px up), the point at (x, y)
void pins_draw_shadow(Surf *s, float x, float y, float h, float sc, float alpha);
// the paper pressed in round its flange (a pin that is home)
void pins_draw_dent(Surf *s, float x, float y, float sc, float alpha);
// the box (from the point) that the pin, its shadow and the dent can cover at any height up to h_max, at scale sc
void pins_reach(float h_max, float sc, float *x0, float *y0, float *x1, float *y1);
#endif
