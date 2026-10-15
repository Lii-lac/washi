// The crayon filter (crayon.c): a picture painted again with heavy wax strokes on rough paper. Pure C, no platform code.
#ifndef CRAYON_H
#define CRAYON_H

#include <stdint.h>

// `rgba` is a size x size picture (straight alpha). Returns a new picture of the same size (malloc'd; free() it) with the colours painted over;
// the alpha stays as it was. The same seed gives the same strokes. If memory runs out the picture comes back unchanged.
uint8_t *crayon_filter(const uint8_t *rgba, int size, uint32_t seed);

// A grid of n x n blocks coloured in with wax crayon, as a w x h picture (straight alpha, malloc'd; free() it; NULL if memory runs out). A
// nonzero byte of `cells` (row by row) is a block; `cell` is the px of one cell and (gx, gy) where the grid's top left lies in the picture.
// `dark` and `light` are the two waxes (0xRRGGBB). The same seed gives the same strokes.
uint8_t *crayon_grid(const uint8_t *cells, int n, float cell, int w, int h, float gx, float gy, uint32_t dark, uint32_t light, uint32_t seed);

#endif
