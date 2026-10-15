// Everything the interface needs from the machine it runs on: buttons, a place to show frames, a clock, the console's theme.
// platform_nx.c implements it with libnx; test/platform_host.c implements it with scripted button presses and PNG screenshots,
// so the whole app (screens and flows included) can be run and checked on a PC.
#ifndef PLATFORM_H
#define PLATFORM_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "dirty.h"
#include "gfx/gfx.h"

enum {
    BTN_A = 1u << 0, BTN_B = 1u << 1, BTN_X = 1u << 2, BTN_Y = 1u << 3,
    BTN_L = 1u << 4, BTN_R = 1u << 5, BTN_ZL = 1u << 6, BTN_ZR = 1u << 7,
    BTN_PLUS = 1u << 8, BTN_MINUS = 1u << 9,
    BTN_UP = 1u << 10, BTN_DOWN = 1u << 11, BTN_LEFT = 1u << 12, BTN_RIGHT = 1u << 13,
    BTN_ADMIN = 1u << 14, // not a button: the secret chord ZL + L + D-pad up + Plus, held together (a Platform reports it in their place)
    BTN_PURGE = 1u << 15, // not a button: X and ZR held together and let go (see x_on_release; "delete them all")
    BTN_R3 = 1u << 16,    // the right stick pressed in: opens every sort menu
    BTN_PIN = 1u << 17,   // not a button: A pressed while ZL and ZR are held (Home: an admin pins or unpins the profile in focus)
};
#define UI_QUIT (1ull << 63)   // the system asked the app to exit
#define UI_REDRAW (1ull << 62) // internal: the theme changed, redraw (never returned by a Platform)
#define UI_TOUCH (1ull << 61)  // a finger touched, moved or lifted (a Platform may add this; ui_poll turns it into taps and drags)

// The sounds the interface asks for (audio.c plays them on the console; a Platform without `play` is silent).
enum { SND_PEEL, SND_STICK, SND_STAMP, SND_DONE, SND_SWISH, SND_REFUSE, SND_TICK, SND_SLIP0, SND_SLIP1, SND_SLIP2, SND_SLIP3, SND_TEAR, SND_CRINKLE, SND_POP0, SND_POP1, SND_RIFFLE,
       SND_PIN_IN, SND_PIN_UP, SND_PIN_HOME, SND_PIN_OUT, // a push pin pushed in, rising as its card takes the focus, going home as it leaves, pulled out
       SND_INTRO, // the intro's music (a few seconds long)
       SND_CHORD, // the music's stamp chord on its own: an intro that is skipped ends on it
       SND_COUNT };

typedef struct Platform Platform;
struct Platform {
    void *ctx;
    // Newly pressed buttons (the d-pad auto-repeats while held). timeout_ms < 0 waits for ever, 0 only polls, > 0 waits up to
    // that long. Returns 0 when nothing happened, or UI_QUIT. Showing a frame is what paces animation, not this call.
    uint64_t (*wait_input)(Platform *, int timeout_ms);
    int repeated; // set by wait_input: the D-pad press it returned was only an auto-repeat of a held direction (menus don't wrap on those)
    int stick;    // set by wait_input: what it returned was only directions, from the left stick (no button pressed: the intro is not skipped by them)
    // Puts a finished 1280x720 frame on the screen (and waits for the display to take it).
    void (*present)(Platform *, const Surf *frame);
    // Optional (NULL = use present). Like present, but the caller promises everything outside the n rectangles is exactly as it was in
    // the frame passed to the previous present, so only those rectangles need copying to the screen.
    void (*present_rects)(Platform *, const Surf *frame, const Rect *r, int n);
    uint64_t (*now_ms)(Platform *); // a steady clock, for animation
    int (*system_dark)(Platform *); // 1 = the console is set to the dark theme, 0 = light, -1 = unknown
    int (*local_time)(Platform *, time_t t, struct tm *out); // wall-clock time in the console's time zone; 0 = ok
    // Optional (may be NULL). The console's own name (System Settings > Console Name), for the top bar; 0 = ok, anything else = none.
    int (*device_name)(Platform *, char *out, size_t cap);
    // The touch screen: 1 while a finger is down, with where it is in the 1280x720 frame. May be NULL (no touch screen).
    // wait_input should return UI_TOUCH (with any buttons) when the finger lands, moves a little or lifts.
    int (*touch)(Platform *, float *x, float *y);
    // Optional (may be NULL). With it on, X is reported when it is let go, not when it is pressed: BTN_X if ZR was not pressed while
    // it was held, otherwise BTN_PURGE (once both are let go). The ZR press is then not reported.
    void (*x_on_release)(Platform *, int on);
    // The system keyboard: 1 = text entered, 0 = cancelled, -1 = not available here (use the on-screen keyboard).
    int (*text_input)(Platform *, const char *prompt, const char *initial, char *out, size_t cap);
    // Optional (may be NULL = silent). Starts sound `snd` (SND_*) at `gain` (0..1) after delay_ms; sounds overlap.
    void (*play)(Platform *, int snd, float gain, int delay_ms);
    // Optional (may be NULL). Stops every sound playing or waiting to start (the intro, skipped, takes its music and cues with it).
    void (*hush)(Platform *);
    // Optional (may be NULL). The screen's fade-in from black at launch is not wanted: the next frames show as they are (the intro starts on black itself).
    void (*fade_off)(Platform *);
    // ---- updates (all optional: NULL = this platform can't). Called from a worker thread except restart.
    // 1 = the console is connected to the internet, 0 = it is not, -1 = can't tell.
    int (*online)(Platform *);
    // An HTTPS GET, following redirects. 0 = an answer came (*status = its HTTP status, *body = malloc'd bytes, *len of them; free(*body) whatever the
    // status), -1 = no answer (no connection, DNS, TLS, timeout, or *cancel became non-zero). progress(ud, got, total) as it comes in (total 0 = not known).
    int (*http_get)(Platform *, const char *url, uint8_t **body, size_t *len, int *status, const volatile int *cancel,
                    void (*progress)(void *ud, uint64_t got, uint64_t total), void *ud);
    // How many http_get calls the platform can have going at once, each from a thread of its own (0 or 1: one at a time). A call that follows another
    // on the same thread reuses its connection, so a page that fetches many small files runs this many workers, not one new connection per file.
    int http_parallel;
    // Puts a downloaded app in place for the next start: written next to the running one and swapped with it, the old one kept as a backup. 0 = done.
    int (*install_update)(Platform *, const uint8_t *nro, size_t len);
    // Arranges for the installed app to open when this one closes: 1 = done, the app should close now; 0 = can't (the person opens it again themselves).
    int (*restart)(Platform *);
    // The version kept from before the last update (install_update's backup): 1 with its version in ver ("" if it can't be read), 0 = none kept.
    int (*previous)(Platform *, char *ver, size_t cap);
    // Puts the kept version in place of the running one, which is kept instead (to come back to the same way). 0 = done: restart then.
    int (*roll_back)(Platform *);
    // The running version has got going (the home screen has been up a while): an update's trial is over (a new version that never gets this far twice is
    // taken back out at the next start, and the kept one opens instead).
    void (*started_ok)(Platform *);
    // 1 (once) if this start is the kept version, put back because the newer one did not get going; its version in ver ("" if unknown).
    int (*rolled_back)(Platform *, char *ver, size_t cap);
};

#endif
