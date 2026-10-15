// Small copies of profile pictures (THUMB_PX square, straight-alpha RGBA) for pages that show a great many at once: the picture strip's sheet and its
// cards. They are made on a thread of their own (on a core the drawing doesn't use), so a page never waits for a PNG to be read and decoded: thumbs_get
// gives the copy when it is there and otherwise asks for it, and the page draws a stand-in until thumbs_serial() says copies have come.
#ifndef UI_THUMBS_H
#define UI_THUMBS_H

#include <stdint.h>

#define THUMB_PX 76

enum { THUMB_WAIT = 0, THUMB_READY = 1, THUMB_NONE = 2 }; // NONE: there is no picture to be had (draw the letter)

// The copy of picture `id` (a console picture or an Extra icon; -1 = none), or NULL while it is being made (*state says which). The pointer is good
// until the next thumbs_ call: draw with it at once. What was asked for last is made first.
const uint8_t *thumbs_get(int id, int *state);
void thumbs_want(int id);     // asks for it ahead of time (after anything thumbs_get asked for)
void thumbs_drop_wants(void); // forgets what was asked for and not yet started (the page went on to other pictures)
int thumbs_ready(int id);     // 1 when the copy is there (or there is none to be had): drawing it now would be final
unsigned thumbs_serial(void); // goes up whenever a copy lands
void thumbs_flush(void);      // forgets every copy (the pictures behind the numbers changed)
void thumbs_shutdown(void);   // stops the thread and frees everything

extern int g_thumbs_sync; // tests: make each copy at once on the calling thread, so what a frame shows doesn't depend on timing

#endif
