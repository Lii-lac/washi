#include "platform_nx.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <switch.h>

#include "audio.h"
#include "gfx/skin.h"

typedef struct {
    Platform p; // must stay first
    Framebuffer fb;
    PadState pad;
    uint64_t held_since[4], last_rep[4];
    int have_setsys, have_time, have_pl;
    int kb_failed_fast;
    int touch_down;
    int x_release, x_hold, x_chord; // X acts on release (see Platform.x_on_release): on, X is down, ZR was pressed meanwhile
    float tx, ty;
    float wx, wy; // where the finger was when it last woke the interface
    Font *shared_font;
    // the fade-in from black at launch: the first frame goes out black, then from just after it every frame shown is scaled up
    int fade_pending, fading;
    uint64_t fade_t0;           // microseconds
    const Surf *last;           // the frame shown last (re-shown while the fade runs and the interface has nothing new to draw)
    FbSim fbs;                  // what each of the last presents changed (see dirty.h), so a rectangle present copies enough
} Nx;

#define FADE_SECONDS 0.4f
#define FADE_RATE 6.0f // the curve's steepness: u = (1 - e^(-rate t)) / (1 - e^(-rate)) over t = 0..1, no gamma

static uint64_t us(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000ull;
}

static uint64_t ms(void)
{
    return armTicksToNs(armGetSystemTick()) / 1000000ull;
}

static uint64_t map_buttons(uint64_t b)
{
    uint64_t o = 0;
    if (b & (HidNpadButton_A)) o |= BTN_A;
    if (b & (HidNpadButton_B)) o |= BTN_B;
    if (b & (HidNpadButton_X)) o |= BTN_X;
    if (b & (HidNpadButton_Y)) o |= BTN_Y;
    if (b & HidNpadButton_L) o |= BTN_L;
    if (b & HidNpadButton_R) o |= BTN_R;
    if (b & HidNpadButton_ZL) o |= BTN_ZL;
    if (b & HidNpadButton_ZR) o |= BTN_ZR;
    if (b & HidNpadButton_StickR) o |= BTN_R3;
    if (b & HidNpadButton_Plus) o |= BTN_PLUS;
    if (b & HidNpadButton_Minus) o |= BTN_MINUS;
    if (b & (HidNpadButton_Up | HidNpadButton_StickLUp)) o |= BTN_UP;
    if (b & (HidNpadButton_Down | HidNpadButton_StickLDown)) o |= BTN_DOWN;
    if (b & (HidNpadButton_Left | HidNpadButton_StickLLeft)) o |= BTN_LEFT;
    if (b & (HidNpadButton_Right | HidNpadButton_StickLRight)) o |= BTN_RIGHT;
    return o;
}

// the first finger on the screen (the touch screen reports 1280x720 coordinates, the same as the frame)
static void read_touch(Nx *n)
{
    HidTouchScreenState st;
    memset(&st, 0, sizeof st);
    if (hidGetTouchScreenStates(&st, 1) > 0 && st.count > 0) {
        n->touch_down = 1;
        n->tx = (float)st.touches[0].x;
        n->ty = (float)st.touches[0].y;
    } else {
        n->touch_down = 0;
    }
}

static int nx_touch(Platform *p, float *x, float *y)
{
    Nx *n = (Nx *)p;
    *x = n->tx;
    *y = n->ty;
    return n->touch_down;
}

// k = 0..256: how much of the frame shows (256 = all of it)
static void show(Nx *n, const Surf *frame, u32 k)
{
    Dirty all, copy;
    dirty_all(&all);
    fbsim_present(&n->fbs, &all, &copy); // every whole-screen show (a fade step included) counts in the history as everything changed
    u32 stride = 0;
    u8 *out = (u8 *)framebufferBegin(&n->fb, &stride);
    if (k >= 256) {
        dirty_copy(frame, out, stride, &all);
    } else {
        for (int y = 0; y < SCREEN_H; y++) {
            const u32 *src = (const u32 *)(frame->px + (size_t)y * SCREEN_W * 4);
            u32 *dst = (u32 *)(out + (size_t)y * stride);
            for (int x = 0; x < SCREEN_W; x++) { // two channels at a time
                u32 px = src[x];
                u32 rb = ((px & 0x00FF00FFu) * k >> 8) & 0x00FF00FFu;
                u32 g = (((px >> 8) & 0x00FF00FFu) * k) & 0x0000FF00u;
                dst[x] = rb | g | 0xFF000000u;
            }
        }
    }
    framebufferEnd(&n->fb);
}

static u32 fade_level(const Nx *n)
{
    float t = (float)(us() - n->fade_t0) / 1000000.0f / FADE_SECONDS;
    if (t >= 1.0f) return 256;
    float u = (1.0f - expf(-FADE_RATE * t)) / (1.0f - expf(-FADE_RATE));
    return (u32)(u * 256.0f);
}

static uint64_t nx_wait_input(Platform *p, int timeout_ms)
{
    Nx *n = (Nx *)p;
    static const uint64_t dirs[4] = {BTN_UP, BTN_DOWN, BTN_LEFT, BTN_RIGHT};
    uint64_t start = ms();
    for (;;) {
        if (!appletMainLoop()) return UI_QUIT;
        if (n->fading && n->last) { // the interface is waiting for a button, but the fade is not done: keep showing its frame
            u32 k = fade_level(n);
            if (k >= 256) n->fading = 0;
            show(n, n->last, k); // (the display's refresh paces this at 60 per second)
        }
        padUpdate(&n->pad);
        uint64_t down = map_buttons(padGetButtonsDown(&n->pad));
        const uint64_t raw_held = padGetButtons(&n->pad);
        uint64_t held = map_buttons(raw_held);
        uint64_t t = ms();
        const uint64_t fresh = down & (BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT); // (pressed just now, as opposed to repeated)
        for (int i = 0; i < 4; i++) { // the d-pad and left stick repeat while held
            if (down & dirs[i]) {
                n->held_since[i] = t;
                n->last_rep[i] = t;
            } else if (held & dirs[i]) {
                if (t - n->held_since[i] > 380 && t - n->last_rep[i] > 85) {
                    down |= dirs[i];
                    n->last_rep[i] = t;
                }
            } else {
                n->held_since[i] = 0;
            }
        }
        {   // a finger landing, sliding or lifting wakes the interface too (sliding: 1.5 px from where it last woke it, so a slow
            // finger adds up and is not lost between two looks)
            int was = n->touch_down;
            read_touch(n);
            if (n->touch_down != was || (n->touch_down && (fabsf(n->tx - n->wx) > 1.5f || fabsf(n->ty - n->wy) > 1.5f))) {
                down |= UI_TOUCH;
                n->wx = n->tx;
                n->wy = n->ty;
            }
        }
        {   // the admin chord: the four buttons held together, the last one just pressed. They give way to a single "admin" press.
            const uint64_t chord = BTN_ZL | BTN_L | BTN_UP | BTN_PLUS;
            if ((held & chord) == chord && (down & chord)) down = (down & ~chord) | BTN_ADMIN;
            else if ((down & BTN_PLUS) && (held & BTN_ZL)) down &= ~BTN_PLUS; // Plus with ZL held starts the chord; it isn't "edit"
            const uint64_t zz = BTN_ZL | BTN_ZR; // and A with ZL and ZR held is "pin"
            if ((held & zz) == zz && (down & BTN_A)) down = (down & ~(uint64_t)BTN_A) | BTN_PIN;
        }
        if (n->x_release) { // X acts when let go: alone it is X, with ZR pressed while it was held it is "purge" (ZR is not reported)
            if (down & BTN_X) {
                n->x_hold = 1;
                n->x_chord = (held & BTN_ZR) != 0;
                down &= ~(uint64_t)BTN_X;
            }
            if (n->x_hold) {
                if (down & BTN_ZR) {
                    n->x_chord = 1;
                    down &= ~(uint64_t)BTN_ZR;
                }
                if (!(held & BTN_X) && !(n->x_chord && (held & BTN_ZR))) { // everything that was held is up
                    n->x_hold = 0;
                    down |= n->x_chord ? BTN_PURGE : BTN_X;
                    n->x_chord = 0;
                }
            }
        }
        if (down) {
            const uint64_t dir = BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT;
            n->p.repeated = !fresh && (down & dir);
            // only directions, none of them held on the D-pad itself: the left stick moved (StickL* are the stick's directions, not its click)
            const uint64_t stick = HidNpadButton_StickLUp | HidNpadButton_StickLDown | HidNpadButton_StickLLeft | HidNpadButton_StickLRight;
            n->p.stick = !(down & ~dir) && !(down & map_buttons(raw_held & ~stick));
            return down;
        }
        if (timeout_ms == 0) return 0;
        if (!n->fading) svcSleepThread(4000000ull); // 4 ms: input stays snappy without spinning the CPU
        if (timeout_ms > 0 && (int)(ms() - start) >= timeout_ms) return 0;
    }
}

static void nx_present(Platform *p, const Surf *frame)
{
    Nx *n = (Nx *)p;
    n->last = frame;
    if (n->fade_pending) { // the very first frame is black; the clock starts once it is out, so the setup before it costs the fade nothing
        n->fade_pending = 0;
        show(n, frame, 0);
        n->fade_t0 = us();
        n->fading = 1;
        return;
    }
    u32 k = 256;
    if (n->fading) {
        k = fade_level(n);
        if (k >= 256) n->fading = 0;
    }
    show(n, frame, k);
}

// Only the rectangles changed, so only what the image about to be shown is missing is copied: the changes of this present and of the
// ones before it that this image has not seen (3 images, so the last three presents' worth).
static void nx_present_rects(Platform *p, const Surf *frame, const Rect *r, int count)
{
    Nx *n = (Nx *)p;
    if (n->fade_pending || n->fading) { // the screen shows a darkened frame, so every pixel is about to change
        nx_present(p, frame);
        return;
    }
    n->last = frame;
    Dirty now, copy;
    dirty_clear(&now);
    for (int i = 0; i < count; i++) dirty_add_rect(&now, r[i]);
    fbsim_present(&n->fbs, &now, &copy);
    u32 stride = 0;
    u8 *out = (u8 *)framebufferBegin(&n->fb, &stride);
    dirty_copy(frame, out, stride, &copy);
    framebufferEnd(&n->fb);
}

static uint64_t nx_now(Platform *p)
{
    (void)p;
    return ms();
}

static int nx_dark(Platform *p)
{
    Nx *n = (Nx *)p;
    if (!n->have_setsys) return -1;
    ColorSetId id;
    if (R_FAILED(setsysGetColorSetId(&id))) return -1;
    return id == ColorSetId_Dark ? 1 : 0;
}

static int nx_device_name(Platform *p, char *out, size_t cap)
{
    static char cached[0x81];
    static int tried;
    Nx *n = (Nx *)p;
    if (!tried) { // (it is asked for on every status refresh; the answer does not change while the app runs)
        tried = 1;
        SetSysDeviceNickName nick;
        if (n->have_setsys && R_SUCCEEDED(setsysGetDeviceNickname(&nick))) {
            memcpy(cached, nick.nickname, sizeof nick.nickname < sizeof cached ? sizeof nick.nickname : sizeof cached - 1);
            cached[sizeof cached - 1] = 0;
        }
    }
    if (!cached[0]) return -1;
    snprintf(out, cap, "%s", cached);
    return 0;
}

static int nx_local_time(Platform *p, time_t t, struct tm *out)
{
    Nx *n = (Nx *)p;
    if (!n->have_time) return -1;
    TimeCalendarTime cal;
    TimeCalendarAdditionalInfo info;
    if (R_FAILED(timeToCalendarTimeWithMyRule((u64)t, &cal, &info))) return -1;
    memset(out, 0, sizeof *out);
    out->tm_year = cal.year - 1900;
    out->tm_mon = cal.month - 1;
    out->tm_mday = cal.day;
    out->tm_hour = cal.hour;
    out->tm_min = cal.minute;
    out->tm_sec = cal.second;
    out->tm_wday = (int)info.wday;
    out->tm_yday = (int)info.yday;
    return 0;
}

static int nx_text_input(Platform *p, const char *prompt, const char *initial, char *out, size_t cap)
{
    Nx *n = (Nx *)p;
    SwkbdConfig kbd;
    if (R_FAILED(swkbdCreate(&kbd, 0))) return -1;
    swkbdConfigMakePresetDefault(&kbd);
    swkbdConfigSetHeaderText(&kbd, prompt);
    swkbdConfigSetInitialText(&kbd, initial ? initial : "");
    swkbdConfigSetStringLenMax(&kbd, cap - 1 < 31 ? (u32)(cap - 1) : 31);
    uint64_t t0 = ms();
    Result rc = swkbdShow(&kbd, out, cap);
    swkbdClose(&kbd);
    (void)n;
    if (R_SUCCEEDED(rc)) return out[0] ? 1 : 0;
    // cancelling takes a person a while; failing at once means the keyboard isn't available (applet mode, ...)
    return ms() - t0 > 400 ? 0 : -1;
}

static void nx_play(Platform *p, int snd, float gain, int delay_ms)
{
    (void)p;
    audio_play(snd, gain, delay_ms);
}

static void nx_hush(Platform *p)
{
    (void)p;
    audio_stop_all();
}

static void nx_fade_off(Platform *p)
{
    Nx *n = (Nx *)p;
    n->fade_pending = 0;
    n->fading = 0;
}

static void nx_x_on_release(Platform *p, int on)
{
    Nx *n = (Nx *)p;
    n->x_release = on;
    n->x_hold = n->x_chord = 0;
}

Platform *platform_nx_init(void)
{
    Nx *n = calloc(1, sizeof *n);
    if (!n) return NULL;
    if (R_FAILED(framebufferCreate(&n->fb, nwindowGetDefault(), SCREEN_W, SCREEN_H, PIXEL_FORMAT_RGBA_8888, 3))) {
        free(n);
        return NULL;
    }
    framebufferMakeLinear(&n->fb);
    hidInitializeTouchScreen();
    padConfigureInput(8, HidNpadStyleSet_NpadStandard);
    padInitializeDefault(&n->pad);
    n->have_setsys = R_SUCCEEDED(setsysInitialize());
    n->have_time = R_SUCCEEDED(timeInitialize());
    n->have_pl = R_SUCCEEDED(plInitialize(PlServiceType_User));
    if (n->have_pl) { // the console's own font covers every script it supports; we only use it for what ours lacks
        PlFontData fd;
        if (R_SUCCEEDED(plGetSharedFontByType(&fd, PlSharedFontType_Standard)) && fd.address) {
            n->shared_font = font_load((const unsigned char *)fd.address);
            if (n->shared_font) skin_set_fallback(n->shared_font);
        }
    }
    n->fade_pending = 1;
    fbsim_init(&n->fbs, 3); // (we write libnx's one linear shadow buffer, so 1 may be enough; 3 is right however the images are used)
    n->p.ctx = n;
    n->p.wait_input = nx_wait_input;
    n->p.present = nx_present;
    n->p.present_rects = nx_present_rects;
    n->p.now_ms = nx_now;
    n->p.touch = nx_touch;
    n->p.x_on_release = nx_x_on_release;
    n->p.system_dark = nx_dark;
    n->p.local_time = nx_local_time;
    n->p.device_name = nx_device_name;
    n->p.text_input = nx_text_input;
    audio_init(); // (a console with no audio just stays quiet)
    n->p.play = nx_play;
    n->p.hush = nx_hush;
    n->p.fade_off = nx_fade_off;
    return &n->p;
}

void platform_nx_exit(Platform *p)
{
    Nx *n = (Nx *)p;
    if (!n) return;
    audio_exit();
    if (n->have_pl) plExit();
    if (n->have_time) timeExit();
    if (n->have_setsys) setsysExit();
    framebufferClose(&n->fb);
    free(n);
}
