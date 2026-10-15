// Dirty rectangles: which parts of the frame changed, and what a platform has to copy to its framebuffer to show them.
// Pure C (no libnx), so the console platform and the PC test platform run the very same code and it is tested on the PC.
#ifndef DIRTY_H
#define DIRTY_H

#include <stddef.h>
#include <stdint.h>

#include "gfx/gfx.h"

typedef struct { int x, y, w, h; } Rect;

#define DIRTY_MAX 16 // more rectangles than this are merged into their bounding boxes (copying a bit extra beats a long list)

// The parts of a frame that changed since the one before. Rectangles are clipped to the screen, never empty, and never overlap or touch.
typedef struct {
    Rect r[DIRTY_MAX + 1]; // (one spare slot while adding)
    int n;
    int full; // the whole screen changed (r is then empty)
} Dirty;

void dirty_clear(Dirty *d);                           // nothing changed
void dirty_all(Dirty *d);                             // everything changed
void dirty_add(Dirty *d, int x, int y, int w, int h); // something changed here (clipped; empty and off-screen ones are dropped)
void dirty_add_rect(Dirty *d, Rect r);
void dirty_union(Dirty *d, const Dirty *o);           // d |= o
long dirty_area(const Dirty *d);                      // pixels covered (the whole screen when full)
int dirty_has(const Dirty *d, int x, int y);          // is this pixel covered?

// The framebuffer-history logic. A framebuffer has `images` images (3 on the console) used round-robin, so the image that shows
// present t still holds present t - images. To bring it up to date, copy everything that changed over the last `images` presents
// (this one included). Until every image has been written once, or when any of those presents was full, copy everything.
typedef struct {
    Dirty hist[3]; // what each of the last presents changed, newest at (presents - 1) % 3
    int images;    // 1..3
    long presents;
} FbSim;

void fbsim_init(FbSim *f, int images);
int fbsim_slot(const FbSim *f); // which image the next present is shown in (round-robin from 0), for a platform that has to pick one
// Records one present that changed `now` and says what to copy into the image that shows it (copy->full = copy everything).
void fbsim_present(FbSim *f, const Dirty *now, Dirty *copy);

// Copies what `d` says (all of the frame if full) from a frame to a destination buffer, `stride` bytes per row.
void dirty_copy(const Surf *frame, uint8_t *dst, size_t stride, const Dirty *d);

// The "lie detector" (for tests): how many pixels differ between two frames outside the n rectangles a caller claimed changed.
// 0 means the claim was true.
long dirty_violations(const Surf *prev, const Surf *frame, const Rect *r, int n);

#endif
