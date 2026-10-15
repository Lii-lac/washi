// A profile's own colour for bars and charts: its background swatch, nudged a little by its name and NetID so two people with the
// same background still get two colours, both plainly of that background's family (light red stays a light red, salmon or pink).
#ifndef TINT_H
#define TINT_H

#include <stdint.h>

// rgb: the profile's background (0xRRGGBB). name, netid: may be NULL or "". The same input always gives the same colour.
uint32_t profile_tint(uint32_t rgb, const char *name, const char *netid);
// c moved in lightness only as far as it needs to stand apart from each of `against` (up to 4 colours)
uint32_t tint_apart(uint32_t c, const uint32_t *against, int n);
// the same against what a bar sits on in the light or the dark theme: the card, the bar's empty track, and a focused row's fill
uint32_t tint_for_theme(uint32_t c, int dark);

// OKLab, for the tests: L 0..1, a and b roughly -0.4..0.4
void tint_oklab(uint32_t rgb, float lab[3]);

#endif
