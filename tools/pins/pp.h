// The push pin (Washi's pinned profiles): modelled in millimetres (a flange, a waisted grip with fine ribs, a near-flat top, a steel needle) and
// rendered with light the way a photograph would show it: the plastic reflects the room (a soft box up toward the top of the screen, a window
// to the right, the lit card below). Its shadow on the card is traced too (a photon map through the pin, and the room's light it hides), so the
// paper right round the flange is darkest. Same oblique view as the board: h px up is drawn K*h px higher. PC only (OpenMP): make_pins.c runs
// it to make data/pins.bin, the pictures the app draws. (PP_CLEAR, see-through plastic, was the other finish on the review page.)
#ifndef PP_H
#define PP_H
#include "paper.h"

enum { PP_CLEAR, PP_GLOSS };

typedef struct {
    int finish;            // PP_CLEAR: dyed see-through plastic; PP_GLOSS: solid moulded plastic
    float s;               // px per mm
    uint32_t dye;          // PP_CLEAR: what 1 mm of the plastic lets through, per colour; PP_GLOSS: the plastic's colour
    float haze;            // PP_CLEAR: how milky (light scattered per mm)
    float tilt_x, tilt_y;  // how it leans (radians about x and y)
    int flutes;            // ribs round the grip (0 = smooth)
    uint32_t card;         // the card's paper colour (behind and under it)
    int photons;           // light paths per px for the shadow (0 = 144)
    int ss;                // samples per px along each side (0 = 4)
    float shadow_k;        // how much darker its shadow and the dent's shaded side are than the light theme's (0 = 1; Paper night's cards are dark)
    uint32_t dent_dark;    // the dent's shaded side (0 = the light theme's warm brown)
} PPLook;

// the pin standing h px off its card (0 = pushed home): the pin itself, and its shadow and light on the card (straight alpha, to lay over
// the card). Both are anchored where the point goes into the card.
int pp_render(const PPLook *L, float h, PaperSpr **pin, PaperSpr **shadow);
// the paper pressed in round the flange of a pin that is home
PaperSpr *pp_dent(const PPLook *L);
#endif
