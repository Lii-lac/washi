// A pretend console for running the whole app on a PC: scripted button presses in, PNG screenshots out.
#ifndef PLATFORM_HOST_H
#define PLATFORM_HOST_H

#include "../source/platform.h"

#define HP_MAX_STEPS 4096

typedef enum { ST_BTN, ST_WAIT, ST_SNAP, ST_TEXT, ST_SNAP_AFTER, ST_CALL, ST_TOUCH } HpStepKind;

typedef struct HostPlat HostPlat;
typedef struct {
    HpStepKind kind;
    uint64_t buttons;
    const char *str;
    int n;
    int stick; // ST_BTN: the directions come from the left stick (hp_stick)
    int after; // hp_in: this press or touch arrives that many frames after the input before it, even if the screen is still moving
    void (*fn)(HostPlat *);
    float tx[24], ty[24]; // ST_TOUCH: the finger lands on the first point, slides through the rest and lifts
    int ntouch;
} HpStep;

struct HostPlat {
    Platform p; // must stay first
    HpStep steps[HP_MAX_STEPS];
    int nsteps, pos;
    Surf *last;          // the last frame presented
    uint64_t t_ms;       // virtual clock: 16 ms per presented frame, 1 s per idle tick
    int dark_sys;        // what the "console" says its theme is
    int tz_offset_s;     // seconds east of UTC for local_time
    int keyboard_available;
    char outdir[300];
    char scenario[64];
    int snaps;
    const char *texts_q[8]; // text for the next prompts, in order (an empty string cancels one); no more = cancel
    int ntexts_q;
    int snap_after[6];   // each > 0: save a screenshot when that many more frames have been shown
    char snap_after_name[6][64];
    int now_step_ms;     // virtual time that passes with every clock reading (so progress updates are not all throttled away)
    int presents;
    int input_at;        // `presents` when the last press was handed over, or the last finger lifted (for hp_in)
    int touch_active, touch_idx, touch_is_down; // a scripted touch in progress
    HpStep *touch_step;
    float touch_x, touch_y;
    // motion trace: while on, every presented frame records when it appeared and where the focus frame's left edge was
    int trace_on, trace_png, ntrace;
    uint64_t trace_t[256];
    int trace_x[256];
    Surf *trace_ref; // when set, trace_x is the right edge of what changed against this picture (a screen with no focus frame)
    void (*frame_hook)(HostPlat *, const Surf *); // while tracing, also called with every presented frame (may be NULL)
    int ticks;
    char texts_seen[8][64]; // text prompts the app asked for (to check the right question was shown)
    int ntexts_seen;
    // The pretend framebuffer: every present (whole or by rectangles) goes through the console's copy code into one of fb_images
    // images, used round-robin, and the image is checked against the frame. Set fb_images (1..3) before the first present.
    int fb_images;           // 3, like the console
    FbSim fbsim;
    uint8_t *fbimg[3];       // each HP_FB_STRIDE bytes per row; starts as a pattern no frame has, so an image that missed a copy shows
    long dirty_violations;   // lie detector: pixels outside the rectangles a present_rects call claimed that differed from the frame before
    long fb_mismatches;      // pixels of the image just shown that differed from the frame presented (0 when the copy logic is right)
    int full_presents, rect_presents;
    long copied_pixels;      // pixels copied to the images by all presents (a whole frame is 921,600)
    int sounds[SND_COUNT];   // how many times each sound was asked for
    int sounds_total;
    struct { uint64_t t; int snd, delay; float gain; } plog[512]; // the first 512 sounds asked for: when (the host clock, ms), which, after how many ms, how loud (a film's soundtrack)
    int nplog;
    float last_gain[SND_COUNT];
    float max_gain[SND_COUNT]; // the loudest each sound was asked for
    int hushes, fade_offs;     // the intro's asks: every sound stopped (a skip), the launch fade-in turned off
    // updates: a pretend GitHub (HP_NET_*: what it answers), and what the app asked of it
    int net;
    int net_gets, installs, restarts;
    // a test's own pretend server, asked first: 1 = it answered (*body malloc'd, *len, *status); 0 = it did not, the pretend GitHub above answers
    int (*http_hook)(const char *url, uint8_t **body, size_t *len, int *status);
    size_t installed_len;
    // going back: the version kept from before (NULL: none), the newer one that "did not start" (NULL: this start is no roll-back; said once), and the asks
    const char *kept_ver, *rolled_ver;
    int roll_backs, started_oks;
};
enum { HP_NET_MISSING, HP_NET_OFFLINE, HP_NET_LATEST, HP_NET_NEWER, HP_NET_BADDL, HP_NET_HTTPERR }; // (MISSING, a 404, is how GitHub answers today)

#define HP_FB_STRIDE (SCREEN_W * 4 + 64) // a padded row, as a real framebuffer may have, so a copy that ignores the stride shows up

void hp_init(HostPlat *h, const char *outdir, const char *scenario);
void hp_free(HostPlat *h);
void hp_btn(HostPlat *h, uint64_t mask);
void hp_btn_n(HostPlat *h, uint64_t mask, int times);
void hp_btn_rep(HostPlat *h, uint64_t mask); // a press that is an auto-repeat of a held D-pad (menus don't wrap on these)
void hp_stick(HostPlat *h, uint64_t dirs);   // the left stick pushed that way (directions, no button)
// Scripted input normally waits until the screen has come to rest. This makes the press or touch just added arrive `frames` frames
// after the input before it instead, in the middle of whatever is moving (a second press mid-slide, a finger on a row that runs).
void hp_in(HostPlat *h, int frames);
void hp_wait(HostPlat *h, int ticks);
void hp_tap(HostPlat *h, float x, float y);                                   // a finger lands and lifts there
void hp_drag(HostPlat *h, float x0, float y0, float x1, float y1, int points); // a finger slides from one place to another
void hp_snap(HostPlat *h, const char *name);
void hp_snap_after(HostPlat *h, int presents, const char *name); // screenshot of the nth frame shown after this step
void hp_text(HostPlat *h, const char *text); // what the system keyboard will return for the next prompt ("" = cancel)
void hp_call(HostPlat *h, void (*fn)(HostPlat *));
// type digits on the six-digit pad; the pad's focus starts on 5
void hp_pin(HostPlat *h, const char *digits);
// type text on the on-screen keyboard (letters, digits and . - _ '), starting from its initial focus; the first letter is capital
void hp_keys(HostPlat *h, const char *text);
void hp_save_png(HostPlat *h, const char *name);
void hp_bench_report(const char *name); // with BENCH=1: print how long frames took since the last report
int hp_focus_left(HostPlat *h); // x of the focus frame's left edge in the last frame shown (-1 if none)

#endif
