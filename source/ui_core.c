#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ui_internal.h"
#include "avatar_src.h"
#include "custom_pics.h"
#include "fsx.h"
#include "icons.h"
#include "ui_thumbs.h"

#ifdef UI_PROFILE // (the PC profile build only: how long each screen takes from ui_screen_done to its first frame)
#include <time.h>
static double prof_now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
static double prof_t0;
static void *prof_tag;
static int prof_armed;
static void prof_present(void)
{
    if (!prof_armed) return;
    fprintf(stderr, "ENTRY %.1f %p\n", prof_now() - prof_t0, prof_tag);
    prof_armed = 0;
}
#endif

UiState G;

// Paper (cream), Paper night, or, following the console, Paper night while the console is in its dark theme
static int effective_dark(void)
{
    if (G.mode == THEME_LIGHT) return 0;
    if (G.mode == THEME_DARK) return 1;
    int d = G.plat->system_dark ? G.plat->system_dark(G.plat) : -1;
    return d > 0;
}

// the light theme (cream) or the dark one (Paper night): the palette every picture is drawn from, and the skin's colours taken from it
static void apply_theme(void)
{
    G.dark = effective_dark();
    paper_pal_set(G.dark);
    skin_init(&G.skin, G.dark);
}

// Sounds: the interface only says what happened (a slip lifted, a card stuck down, a press that leads nowhere); the platform makes them.
void ui_sound(int snd, float gain, int delay_ms)
{
    static uint64_t last[SND_COUNT];
    if (!G.plat || !G.plat->play || snd < 0 || snd >= SND_COUNT) return;
    uint64_t now = ui_now(), gap = snd >= SND_SLIP0 && snd <= SND_SLIP3 ? 18 : 60;
    if (last[snd] && now >= last[snd] && now - last[snd] < gap) return;
    last[snd] = now;
    G.plat->play(G.plat, snd, gain, delay_ms);
}

// a slip lifted off the page: the peel (not on a held D-pad's repeats); the one that was up sticks back down: a tap, softer on repeats. The keys of
// the keypad and keyboard are many and close together, and a peel and a tap for each of them wore on the ear: a key only makes one small soft pop as the
// focus lands on it (two alternating, so a run of them is not a machine), quieter again on a held D-pad.
void ui_lift_sfx(int grab, int held, int kind)
{
    int tapeless = (kind & LIFT_TAPELESS) != 0, pinned = (kind & LIFT_PINNED) != 0;
    kind &= ~(LIFT_TAPELESS | LIFT_PINNED);
    if (pinned) { // held by a push pin: the pin creaks up out of the paper a moment after the card lifts, and seats itself as the card lets go
        if (grab) {
            if (!held) ui_sound(SND_PIN_UP, 0.95f, 40);
        } else {
            ui_sound(SND_PIN_HOME, held ? 0.45f : 0.8f, 0);
        }
        return;
    }
    if (tapeless && kind != LIFT_KEY) { // nothing taped on it (the search bar, the order chip, a button): no peel and no stick, a soft brush of paper as it lifts
        if (grab) ui_sound(SND_SWISH, held ? 0.12f : 0.22f, 0);
        return;
    }
    if (kind == LIFT_KEY) {
        static unsigned n;
        if (grab) ui_sound(SND_POP0 + (int)(n++ & 1u), held ? 0.3f : 0.55f, 0);
        return;
    }
    if (grab) {
        if (!held) ui_sound(SND_PEEL, 0.6f, 0);
    } else {
        ui_sound(SND_STICK, held ? 0.4f : 0.7f, held ? 0 : 60);
    }
}

void ui_init(Platform *p, int theme_mode, void (*status_fn)(void *, SkinStatus *), void *status_user)
{
    ui_wait_abort(); // (nothing of a "still working" scrap from before is left to pump)
    memset(&G, 0, sizeof G);
    G.plat = p;
    paper_lift_hook = ui_lift_sfx;
    G.mode = theme_mode;
    G.status_fn = status_fn;
    G.status_user = status_user;
    G.work = surf_new(SCREEN_W, SCREEN_H);
    G.base = surf_new(SCREEN_W, SCREEN_H);
    G.dim = surf_new(SCREEN_W, SCREEN_H);
    G.last = surf_new(SCREEN_W, SCREEN_H);
    G.prev = surf_new(SCREEN_W, SCREEN_H);
    G.mix = surf_new(SCREEN_W, SCREEN_H);
    G.back = surf_new(SCREEN_W, SCREEN_H);
    G.sel = surf_new(SCREEN_W, SCREEN_H);
    apply_theme();
}

void ui_shutdown(void)
{
    ui_intro_drop(); // (an intro made but never played: its workers are waited for and it is freed)
    ui_wait_abort(); // (the pumps must not run on a state that is about to be gone)
    surf_free(G.work);
    surf_free(G.base);
    surf_free(G.dim);
    surf_free(G.last);
    surf_free(G.prev);
    surf_free(G.mix);
    surf_free(G.back);
    surf_free(G.sel);
    memset(&G, 0, sizeof G);
}

void ui_set_theme_mode(int mode)
{
    G.mode = mode;
    apply_theme();
}

int ui_dark(void) { return G.dark; }
void ui_set_clock24(int on) { G.clock24 = on != 0; }
// ---- where the profile pictures come from: the console's own files (avatar_src.c), else a packed file, else letters
static AvatarSrc *g_avsrc;
static char g_avatar_note[96], g_avatar_status[160] = "none, every profile is a letter";

static int src_count(void *c) { return avsrc_count(c); }
static int src_at(void *c, int pos) { return avsrc_at(c, pos); }
static uint8_t *src_load(void *c, int pic) { return avsrc_load(c, pic); }

void ui_set_avatar_pack(const uint8_t *data, size_t size)
{
    thumbs_flush(); // (the small copies are of the old pictures, and one may be being made from the source closed below)
    skin_set_avatar_pack(data, size); // (this lets go of the other source, so it can be closed)
    avsrc_close(g_avsrc);
    g_avsrc = NULL;
    if (skin_avatar_count() > 0) snprintf(g_avatar_status, sizeof g_avatar_status, "a packed file, %d pictures", skin_avatar_count());
    else snprintf(g_avatar_status, sizeof g_avatar_status, "none, every profile is a letter");
}

int ui_set_avatar_dir(const char *dir)
{
    AvatarSrc *s = avsrc_open(dir);
    if (!s) return 0;
    static const SkinPictureSource ops = {src_count, src_at, src_load};
    thumbs_flush();
    skin_set_avatar_source(&ops, s);
    avsrc_close(g_avsrc);
    g_avsrc = s;
    snprintf(g_avatar_status, sizeof g_avatar_status, "the console's own, %d pictures", skin_avatar_count());
    return skin_avatar_count();
}

void ui_set_avatar_note(const char *note) { snprintf(g_avatar_note, sizeof g_avatar_note, "%s", note ? note : ""); }

const char *ui_avatar_status(void)
{
    static char both[300];
    snprintf(both, sizeof both, "%s%s%s", g_avatar_status, g_avatar_note[0] ? "; " : "", g_avatar_note);
    return both;
}

// ---- the Extra icons: PNG files at <dir>/<game>/<name>.png, shown as pictures numbered from CUSTOM_PIC_BASE (custom_pics.h, icons.h)
static char g_icon_dir[FSX_PATH];
static IconSet *g_icons;
static int g_icons_scanned;

// a picture of another size, made 256 x 256 (bilinear)
static uint8_t *resize_rgba(const uint8_t *src, int sw, int sh, int dw, int dh)
{
    uint8_t *out = malloc((size_t)dw * dh * 4);
    if (!out) return NULL;
    for (int y = 0; y < dh; y++) {
        float fy = ((float)y + 0.5f) * sh / dh - 0.5f;
        int y0 = (int)floorf(fy), y1 = y0 + 1;
        float ty = fy - y0;
        if (y0 < 0) y0 = 0;
        if (y1 > sh - 1) y1 = sh - 1;
        if (y0 > sh - 1) y0 = sh - 1;
        for (int x = 0; x < dw; x++) {
            float fx = ((float)x + 0.5f) * sw / dw - 0.5f;
            int x0 = (int)floorf(fx), x1 = x0 + 1;
            float tx = fx - x0;
            if (x0 < 0) x0 = 0;
            if (x1 > sw - 1) x1 = sw - 1;
            if (x0 > sw - 1) x0 = sw - 1;
            for (int c = 0; c < 4; c++) {
                float a = src[((size_t)y0 * sw + x0) * 4 + c] * (1 - tx) + src[((size_t)y0 * sw + x1) * 4 + c] * tx;
                float b = src[((size_t)y1 * sw + x0) * 4 + c] * (1 - tx) + src[((size_t)y1 * sw + x1) * 4 + c] * tx;
                float v = a * (1 - ty) + b * ty;
                out[((size_t)y * dw + x) * 4 + c] = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
            }
        }
    }
    return out;
}

static uint8_t *custom_load(void *ctx, int id)
{
    (void)ctx;
    const char *key = custom_key(id);
    if (!key || !g_icon_dir[0]) return NULL;
    char path[FSX_PATH];
    if (snprintf(path, sizeof path, "%s/%s.png", g_icon_dir, key) >= (int)sizeof path) return NULL;
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    size_t n = 0;
    if (fseek(f, 0, SEEK_END) == 0) {
        long len = ftell(f);
        if (len > 0 && len <= 4 * 1024 * 1024 && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)len);
            if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
                free(buf);
                buf = NULL;
            }
            if (buf) n = (size_t)len;
        }
    }
    fclose(f);
    if (!buf) return NULL;
    int w = 0, h = 0;
    uint8_t *px = gfx_decode_png(buf, n, &w, &h);
    free(buf);
    if (!px) return NULL;
    if (w == 256 && h == 256) return px;
    uint8_t *out = w >= 1 && h >= 1 && w <= 4096 && h <= 4096 ? resize_rgba(px, w, h, 256, 256) : NULL;
    free(px);
    return out;
}

void ui_set_icon_dir(const char *dir)
{
    snprintf(g_icon_dir, sizeof g_icon_dir, "%s", dir ? dir : "");
    icons_free(g_icons);
    g_icons = NULL;
    g_icons_scanned = 0;
    thumbs_flush();
    skin_set_custom_source(g_icon_dir[0] ? custom_load : NULL, NULL);
}

const char *ui_icon_dir(void) { return g_icon_dir; }

const IconSet *ui_icon_set(int rescan)
{
    if (rescan || !g_icons_scanned) {
        icons_free(g_icons);
        g_icons = g_icon_dir[0] ? icons_scan(g_icon_dir) : NULL;
        g_icons_scanned = 1;
        if (rescan) { // (files may have been replaced: decode them again)
            thumbs_flush();
            skin_set_custom_source(g_icon_dir[0] ? custom_load : NULL, NULL);
        }
    }
    return g_icons;
}

uint8_t *ui_render_avatar_jpeg(const char *name, int look, size_t *size)
{
    Surf *s = surf_new(256, 256);
    if (!s) return NULL;
    Cx c = ui_cx(s);
    skin_avatar_square(&c, 0, 0, 256, name, look, 0); // a flat square of the swatch: the console crops it to a circle itself
    uint8_t *jpg = gfx_encode_jpeg(s, 88, size);
    surf_free(s);
    return jpg;
}

void ui_set_icon_source(int (*fn)(void *, uint64_t, const uint8_t **, size_t *), void *user)
{
    G.icon_fn = fn;
    G.icon_user = user;
}

// ------------------------------------------------------------------------------------------------ transitions

enum { TR_NONE = 0, TR_FADE, TR_DIALOG, TR_GROW, TR_SHRINK };

static uint64_t poll_raw(int timeout_ms);

static float ease_out(float p)
{
    float q = 1.0f - p;
    return 1.0f - q * q * q;
}

// one frame of the change from G.prev to the frame `s`, p = how far along (0..1)
static void tr_frame(const Surf *s, float p)
{
    if (G.tr == TR_DIALOG) {
        float e = ease_out(p);
        gfx_lerp(G.mix, G.prev, G.back, (int)(e * 256.0f + 0.5f)); // the screen dims
        int dy = (int)floorf((1.0f - e) * 22.0f + 0.5f);           // the card rises into place as it fades in
        Cx c = ui_cx(G.mix);
        skin_card_shadow(&c, G.card[0], G.card[1] + dy, G.card[2], G.card[3], 6, 2.0f, e);
        gfx_mix_card(G.mix, s, G.card[0], G.card[1], G.card[2], G.card[3], 6, 0, dy, e);
    } else if (G.tr == TR_GROW || G.tr == TR_SHRINK) {
        // A card that opens into the next screen, or closes back into the one before: a window of the card's colour between
        // G.card and the whole screen, with the bigger screen fading in inside it as it opens (out as it closes).
        int grow = G.tr == TR_GROW;
        float q = grow ? p : 1.0f - p, k = grow ? ease_out(p) : 1.0f - ease_out(p), r[4], full[4] = {0, 0, SCREEN_W, SCREEN_H};
        for (int i = 0; i < 4; i++) r[i] = G.card[i] + (full[i] - G.card[i]) * k;
        float fill = q / 0.15f > 1 ? 1 : q / 0.15f, a = (q - 0.1f) / 0.4f;
        a = a < 0 ? 0 : a > 1 ? 1 : a;
        a = a * a * (3.0f - 2.0f * a);
        surf_copy(G.mix, grow ? G.prev : s);
        Cx c = ui_cx(G.mix);
        float rad = 6.0f; // (paper's corners: the sheet it opens from and the page it becomes)
        skin_card_shadow(&c, r[0], r[1], r[2], r[3], rad, 1.0f, fill * (1.0f - k));
        gfx_rrect(G.mix, r[0], r[1], r[2], r[3], rad, hex(PAL(paper, PAPER_PAPER), fill));
        gfx_mix_card(G.mix, grow ? s : G.prev, r[0], r[1], r[2], r[3], rad, 0, 0, a);
    } else {
        float e = p * p * (3.0f - 2.0f * p);
        gfx_lerp(G.mix, G.prev, s, (int)(e * 256.0f + 0.5f));
    }
}

// Plays the change to `s` (about a sixth of a second) before it is shown for good. Anything pressed meanwhile is kept for later.
static int g_tr_playing;

int ui_in_transition(void) { return g_tr_playing; }

static void tr_play(const Surf *s)
{
    float dur = G.tr == TR_DIALOG ? 190.0f : G.tr == TR_GROW || G.tr == TR_SHRINK ? 260.0f : 150.0f;
    uint64_t t0 = ui_now();
    g_tr_playing = 1;
    for (;;) {
        float p = (float)(ui_now() - t0) / dur;
        if (p >= 1.0f) break;
        tr_frame(s, p);
        int ov = ui_wait_erasing() ? ui_wait_overlay_on(G.mix) : 0; // (what is left of the "still working" ink goes on through the change)
        G.plat->present(G.plat, G.mix);
        if (ov) ui_wait_overlay_off(G.mix, ov);
        uint64_t b = poll_raw(0);
        if (b == UI_QUIT) break;
        G.pending |= b;
    }
    g_tr_playing = 0;
}

void ui_transition(void)
{
#ifdef UI_PROFILE
    if (prof_armed && !prof_tag) prof_tag = __builtin_return_address(0);
#endif
    ui_wait_finish(); // (the "still working" ink starts to be rubbed out now and goes on over the change to the next screen and the screen itself)
    ui_busy_stop();   // (and a waiting page's dots stop: it is going)
    int next = G.tr_next;
    G.tr_next = TR_NONE;
    if (!G.prev_ok || G.quit) return;
    surf_copy(G.prev, G.work);
    G.tr = next ? next : TR_FADE;
    ui_sound(SND_SWISH, 0.5f, 0);
}

static void next_tr(int kind, float x, float y, float w, float h)
{
    G.tr_next = kind;
    G.card[0] = x;
    G.card[1] = y;
    G.card[2] = w;
    G.card[3] = h;
}

void ui_next_grows_from(float x, float y, float w, float h) { next_tr(TR_GROW, x, y, w, h); }
void ui_next_shrinks_into(float x, float y, float w, float h) { next_tr(TR_SHRINK, x, y, w, h); }

void ui_dialog_begin(float x, float y, float w, float h)
{
    ui_wait_finish();
    ui_backdrop();
    surf_copy(G.back, G.dim);
    ui_sound(SND_STICK, 0.9f, 0); // (the dialog's card is pinned over the screen)
    if (G.prev_ok && !G.quit) {
        surf_copy(G.prev, G.work);
        G.tr = TR_DIALOG;
        G.card[0] = x;
        G.card[1] = y;
        G.card[2] = w;
        G.card[3] = h;
    }
}

Cx ui_dialog_card(float x, float y, float w, float h)
{
    ui_dialog_begin(x, y, w, h);
    Cx c = ui_cx(G.dim);
    skin_dialog(&c, x, y, w, h);
    return c;
}

void ui_present(Surf *s)
{
#ifdef UI_PROFILE
    prof_present();
#endif
    if (ui_wait_shown()) ui_wait_finish(); // (the next screen is here: the mark starts to be rubbed out, over it)
    ui_busy_stop(); // (a whole frame: a waiting page's dots are not on it any more)
    if (G.tr != TR_NONE && G.prev_ok && !G.quit) tr_play(s);
    G.tr = TR_NONE;
    int ov = ui_wait_erasing() ? ui_wait_overlay_on(s) : 0;
    G.plat->present(G.plat, s);
    if (ov) ui_wait_overlay_off(s, ov);
    G.prev_ok = 1;
}

Surf *ui_display_base(void) { return G.tr != TR_NONE && G.prev_ok ? G.prev : G.work; }

void ui_present_rects(Surf *s, const Rect *r, int n)
{
#ifdef UI_PROFILE
    prof_present();
#endif
    if (ui_wait_shown()) ui_wait_finish(); // (the mark is on the display, drawn there by its own frames: from here on it is rubbed out over what is shown)
    // with no frame before it to patch, or a change of screen to play first, there is nothing the rectangles could be relative to
    if (!G.plat->present_rects || !G.prev_ok || G.quit || G.tr != TR_NONE) {
        ui_present(s);
        return;
    }
    if (ui_wait_erasing()) { // the rubbing out is on: its place is shown with the rectangles, with the ink as it is by now over the frame
        enum { MAXR = 24 };
        Rect list[MAXR + 1];
        int ov = ui_wait_overlay_on(s), m = n;
        if (n <= MAXR) memcpy(list, r, (size_t)n * sizeof *r);
        else {
            list[0] = (Rect){0, 0, SCREEN_W, SCREEN_H};
            m = 1;
        }
        if (ov) list[m++] = ui_wait_area();
        G.plat->present_rects(G.plat, s, list, m);
        if (ov) ui_wait_overlay_off(s, ov);
        return;
    }
    G.plat->present_rects(G.plat, s, r, n);
}

uint64_t ui_now(void)
{
    return G.plat->now_ms(G.plat);
}

// ---- the press: the item pushes flat for a moment before its action
long g_ui_presses, g_ui_press_changed; // tests: presses shown, and how many of them really changed pixels
long g_ui_press_bg_changed; // tests (with g_ui_press_audit on): pixels, over all presses, that changed with nothing of the item within 4 px (none should)
int g_ui_press_audit;
void (*g_ui_press_hook)(const uint8_t *rest, int x0, int y0, int w, int h, const Surf *pressed, Rect r, int with_over); // tests: sees each press (the region as it was, and as it is about to be shown)

// The item in `r` (its lift and shadow included) of the frame `s` that is on screen shrinks about its middle, 1% and never more than 3 px in from any edge (the same on every side), shadow and all (the
// shadow closes in with it), is shown that way for PRESS_MS, then goes back; the caller carries on with the action. Works on the pixels,
// so every screen can use it whatever it draws the item with; only what the frame has over G.base moves, never the board behind it (its
// grain stays put even under the item's shadow, which moves as a darkening), and nothing inside the rectangles of ui_press_keep. Waits on
// the clock without reading input: whatever is pressed meanwhile is read by the next poll.
#define PRESS_MS 70
#define PRESS_SCALE 0.99f
void ui_press(Surf *s, Rect r)
{
    // 1%, but never more than 3 px off each end of the item: on a row as wide as the screen 1% slides its words several pixels along, which reads as a warp
    float m = (float)(r.w > r.h ? r.w : r.h), k = PRESS_SCALE;
    if (m > 0 && (1.0f - k) * m * 0.5f > 3.0f) k = 1.0f - 6.0f / m;
    ui_press_k(s, r, k);
}

// the same, as deep as `scale` says (a key of the keypad goes in further: it has to show that it was pressed)
void ui_press_k(Surf *s, Rect r, float scale) { ui_press_over(s, r, scale, NULL); }

// The other items of the screen (their slips, shadows and tape): the next press leaves everything inside these rectangles exactly as it is, so a
// neighbour's edge, shadow or tape never moves with the item pressed. Set just before ui_press and used up by it.
static Rect g_press_keep[12];
static int g_press_nkeep;
void ui_press_keep(const Rect *rs, int n)
{
    g_press_nkeep = 0;
    for (int i = 0; i < n && g_press_nkeep < 12; i++)
        if (rs[i].w > 0 && rs[i].h > 0) g_press_keep[g_press_nkeep++] = rs[i];
}

// The dashed patch a lifted item leaves where it sat (its ghost): it is not the item, so the next press keeps it still. It is laid on the background the
// press compares with (as the screen draws it: the sprite turned `angle` radians about (cx, cy), faded to `alpha`). Set just before the press, used up by it.
static struct { const PaperSpr *spr; float cx, cy, angle, alpha; } g_press_ghost;
int g_ui_press_ghostless; // tests: 1 = presses lay no ghost (to compare a press with one that has none)
void ui_press_ghost(const PaperSpr *ghost, float cx, float cy, float angle, float alpha)
{
    if (g_ui_press_ghostless) alpha = 0;
    g_press_ghost.spr = ghost && alpha > 0.004f ? ghost : NULL;
    g_press_ghost.cx = cx;
    g_press_ghost.cy = cy;
    g_press_ghost.angle = angle;
    g_press_ghost.alpha = alpha;
}

// The lifted item's own card, where it lies over its ghost: what it covered stays covered when it goes in (a key goes in by several pixels, and its dashed
// patch must not come out round it, or through it). Set just before the press with the ghost, used up by it.
static Rect g_press_cover;
Rect g_ui_press_cover_last; // tests: the cover of the last press that had one
void ui_press_cover(Rect body)
{
    g_press_cover = body;
}

// Holes in the rectangles of ui_press_keep: what is in them goes in with the item after all (the item's own tape and pin, which stand over a neighbour's
// shadow or edge). Set just before the press, used up by it.
static Rect g_press_free[8];
static int g_press_nfree;
void ui_press_free(const Rect *rs, int n)
{
    g_press_nfree = 0;
    for (int i = 0; i < n && g_press_nfree < 8; i++)
        if (rs[i].w > 0 && rs[i].h > 0) g_press_free[g_press_nfree++] = rs[i];
}

// Rectangles of other things (a bar with words on it, a button) that stay as they are whatever else is set: the holes of ui_press_free do not reach into them
// (a card's tape that lies over the edge of a bar does not go in, where the bar's own words would move with it). Set just before the press, used up by it.
static Rect g_press_hard[8];
static int g_press_nhard;
void ui_press_keep_hard(const Rect *rs, int n)
{
    g_press_nhard = 0;
    for (int i = 0; i < n && g_press_nhard < 8; i++)
        if (rs[i].w > 0 && rs[i].h > 0) g_press_hard[g_press_nhard++] = rs[i];
}

// The box the item is in with all that sticks out of its rectangle (its tape, its pin, its shadow): everything of it inside the box goes in with it. Set
// just before the press, used up by it. Without one, only what is within a few pixels of the item's rectangle goes in.
static Rect g_press_whole;
void ui_press_whole(Rect whole)
{
    g_press_whole = whole;
}

// The item's own card (as it is drawn, lifted): no rectangle kept still by ui_press_keep may cover it, or that part of its edge stays behind when the rest
// goes in (a neighbour's tape that reaches over the gap would hold the card's bottom edge). Each one that does is cut back to the card's edge above or below
// it (or to its side). Call after ui_press_keep.
void ui_press_keep_off(Rect card)
{
    for (int i = 0; i < g_press_nkeep; i++) {
        Rect *k = &g_press_keep[i];
        int kx1 = k->x + k->w, ky1 = k->y + k->h, cx1 = card.x + card.w, cy1 = card.y + card.h;
        if (k->x >= cx1 || kx1 <= card.x || k->y >= cy1 || ky1 <= card.y) continue;
        int kcy = k->y + k->h / 2, ccy = card.y + card.h / 2, kcx = k->x + k->w / 2, ccx = card.x + card.w / 2;
        if (abs(kcy - ccy) * card.w >= abs(kcx - ccx) * card.h) { // above or below it
            if (kcy > ccy) {
                k->h = ky1 - cy1;
                k->y = cy1;
            } else
                k->h = card.y - k->y;
        } else if (kcx > ccx) {
            k->w = kx1 - cx1;
            k->x = cx1;
        } else
            k->w = card.x - k->x;
    }
}

// the same over a background of the caller's own: a full-size picture of the screen with the item left out (a dialog's card, the dots and words
// on it: all of that stays put, and only the item moves). NULL: the screen's G.base.
void ui_press_over(Surf *s, Rect r, float scale, const Surf *over)
{
    Rect nbr[12]; // (the neighbours set for this press; used up whether or not it is shown)
    int nnbr = g_press_nkeep;
    memcpy(nbr, g_press_keep, sizeof nbr);
    g_press_nkeep = 0;
    const PaperSpr *gspr = g_press_ghost.spr; // (and its ghost, and the card over it)
    g_press_ghost.spr = NULL;
    Rect cover = g_press_cover;
    memset(&g_press_cover, 0, sizeof g_press_cover);
    Rect whole = g_press_whole;
    memset(&g_press_whole, 0, sizeof g_press_whole);
    Rect hole[8], hard[8];
    const int nhole = g_press_nfree, nhard = g_press_nhard;
    memcpy(hole, g_press_free, sizeof hole);
    memcpy(hard, g_press_hard, sizeof hard);
    g_press_nfree = g_press_nhard = 0;
    int x0 = r.x - 10, y0 = r.y - 10, x1 = r.x + r.w + 18, y1 = r.y + r.h + 22;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > s->w) x1 = s->w;
    if (y1 > s->h) y1 = s->h;
    if (x1 - x0 < 4 || y1 - y0 < 4 || G.quit) return;
    // What is pushed flat is what the frame has over the screen's own background (G.base: the board, the paper, whatever stays put): the
    // difference of the two is shrunk about the middle and put back on the background, so the board's grain and the paper round the item do not
    // swim. (With no background to compare with, the whole rectangle shrinks.)
    const Surf *base = over ? over : G.base;
    const Surf *bg = base && base != s && base->w == s->w && base->h == s->h ? base : NULL;
    Surf *with_ghost = NULL; // the background with the item's ghost on it: the ghost stays where it is
    if (bg && gspr && (with_ghost = surf_clone(bg))) {
        paper_spr_xform(with_ghost, gspr, g_press_ghost.cx, g_press_ghost.cy, 1.0f, g_press_ghost.angle, g_press_ghost.alpha);
        if (cover.w > 0 && cover.h > 0) { // under the card the ghost was never seen: the background there stays as it is without it
            int cx0 = cover.x < 0 ? 0 : cover.x, cy0 = cover.y < 0 ? 0 : cover.y;
            int cx1 = cover.x + cover.w > s->w ? s->w : cover.x + cover.w, cy1 = cover.y + cover.h > s->h ? s->h : cover.y + cover.h;
            for (int y = cy0; y < cy1 && cx1 > cx0; y++)
                memcpy(with_ghost->px + ((size_t)y * s->w + cx0) * 4, bg->px + ((size_t)y * s->w + cx0) * 4, (size_t)(cx1 - cx0) * 4);
            g_ui_press_cover_last = cover;
        }
        bg = with_ghost;
    }
    // Everything of the item goes in, not just what is in its rectangle: its tape, its pin and its shadow stick out of it. The caller says where the whole
    // of it is (ui_press_whole): all of that box is worked on, and what in it is the item and what the background is told apart pixel by pixel, as
    // everywhere else
    if (whole.w > 0 && whole.h > 0) {
        if (whole.x < x0) x0 = whole.x;
        if (whole.y < y0) y0 = whole.y;
        if (whole.x + whole.w > x1) x1 = whole.x + whole.w;
        if (whole.y + whole.h > y1) y1 = whole.y + whole.h;
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 > s->w) x1 = s->w;
        if (y1 > s->h) y1 = s->h;
    }
    const int w = x1 - x0, h = y1 - y0;
    uint8_t *save = malloc((size_t)w * h * 4);
    if (!save) {
        surf_free(with_ghost);
        return;
    }
    for (int y = 0; y < h; y++) memcpy(save + (size_t)y * w * 4, s->px + ((size_t)(y0 + y) * s->w + x0) * 4, (size_t)w * 4);
    float cx = r.x + r.w * 0.5f - x0, cy = r.y + r.h * 0.5f - y0;
    // Every edge goes in by the same few pixels, as far as `scale` takes the longer side in: a wide row's top and bottom go in with its ends (one scale
    // for both would move them a fraction of a pixel, and the row's middle would seem to shrink inside edges that stay put). Across the shorter side no
    // more than 3.5% of it, so the words on a low row are only a little squashed.
    float in = (1.0f - scale) * (float)(r.w > r.h ? r.w : r.h) * 0.5f;
    float in_x = fminf(in, fmaxf(1.0f, 0.035f * r.w)), in_y = fminf(in, fmaxf(1.0f, 0.035f * r.h));
    float scale_x = r.w > 0 ? 1.0f - 2.0f * in_x / r.w : 1.0f, scale_y = r.h > 0 ? 1.0f - 2.0f * in_y / r.h : 1.0f;
    int changed = 0;
    // the neighbours to leave alone (this press only): what lies in their rectangles neither moves nor is carried into the item
    uint8_t *keep = NULL;
    if (nnbr || nhard) {
        keep = calloc((size_t)w * h, 1);
        for (int k = 0; k < nnbr && keep; k++) {
            int kx0 = nbr[k].x - x0, ky0 = nbr[k].y - y0, kx1 = kx0 + nbr[k].w, ky1 = ky0 + nbr[k].h;
            if (kx0 < 0) kx0 = 0;
            if (ky0 < 0) ky0 = 0;
            if (kx1 > w) kx1 = w;
            if (ky1 > h) ky1 = h;
            for (int y = ky0; y < ky1; y++) memset(keep + (size_t)y * w + kx0, 1, kx1 > kx0 ? (size_t)(kx1 - kx0) : 0);
        }
        for (int k = 0; k < nhole && keep; k++) { // (the holes: the item's own tape and pin)
            int kx0 = hole[k].x - x0, ky0 = hole[k].y - y0, kx1 = kx0 + hole[k].w, ky1 = ky0 + hole[k].h;
            if (kx0 < 0) kx0 = 0;
            if (ky0 < 0) ky0 = 0;
            if (kx1 > w) kx1 = w;
            if (ky1 > h) ky1 = h;
            for (int y = ky0; y < ky1; y++) memset(keep + (size_t)y * w + kx0, 0, kx1 > kx0 ? (size_t)(kx1 - kx0) : 0);
        }
        for (int k = 0; k < nhard && keep; k++) {
            int kx0 = hard[k].x - x0, ky0 = hard[k].y - y0, kx1 = kx0 + hard[k].w, ky1 = ky0 + hard[k].h;
            if (kx0 < 0) kx0 = 0;
            if (ky0 < 0) ky0 = 0;
            if (kx1 > w) kx1 = w;
            if (ky1 > h) ky1 = h;
            for (int y = ky0; y < ky1; y++) memset(keep + (size_t)y * w + kx0, 1, kx1 > kx0 ? (size_t)(kx1 - kx0) : 0);
        }
    }
    for (int y = 0; y < h; y++) {
        uint8_t *d = s->px + ((size_t)(y0 + y) * s->w + x0) * 4;
        const uint8_t *g0 = bg ? bg->px + ((size_t)(y0 + y) * s->w + x0) * 4 : NULL;
        for (int x = 0; x < w; x++, d += 4) {
            if (keep && keep[(size_t)y * w + x]) continue;
            float sx = cx + (x + 0.5f - cx) / scale_x - 0.5f, sy = cy + (y + 0.5f - cy) / scale_y - 0.5f;
            sx = sx < 0 ? 0 : sx > w - 1 ? (float)(w - 1) : sx;
            sy = sy < 0 ? 0 : sy > h - 1 ? (float)(h - 1) : sy;
            int ix = (int)sx, iy = (int)sy, jx = ix + 1 < w ? ix + 1 : ix, jy = iy + 1 < h ? iy + 1 : iy;
            if (keep && (keep[(size_t)iy * w + ix] | keep[(size_t)iy * w + jx] | keep[(size_t)jy * w + ix] | keep[(size_t)jy * w + jx])) continue;
            float fx = sx - ix, fy = sy - iy;
            const uint8_t *a = save + ((size_t)iy * w + ix) * 4, *b = save + ((size_t)iy * w + jx) * 4, *c = save + ((size_t)jy * w + ix) * 4,
                          *e = save + ((size_t)jy * w + jx) * 4;
            float vq[3], u[3] = {1, 1, 1};
            for (int q = 0; q < 3; q++) vq[q] = (a[q] * (1 - fx) + b[q] * fx) * (1 - fy) + (c[q] * (1 - fx) + e[q] * fx) * fy;
            if (bg) {
                const uint8_t *ga = bg->px + ((size_t)(y0 + iy) * s->w + x0 + ix) * 4, *gb = bg->px + ((size_t)(y0 + iy) * s->w + x0 + jx) * 4,
                              *gc = bg->px + ((size_t)(y0 + jy) * s->w + x0 + ix) * 4, *ge = bg->px + ((size_t)(y0 + jy) * s->w + x0 + jx) * 4;
                for (int q = 0; q < 3; q++) u[q] = (ga[q] * (1 - fx) + gb[q] * fx) * (1 - fy) + (gc[q] * (1 - fx) + ge[q] * fx) * fy;
            }
            // What is moved is the item: where it lies the frame is shown as it is. A shadow is not the item but a darkening of what is behind it, so
            // it moves as a ratio and is put on the background where it lands: the board's grain stays where it is, and does not swim under the shadow.
            // (The item is told from its shadow by being lighter than the background, or another colour, or much darker than a shadow gets.)
            // (the ratio is of the levels plus 8, so that a channel at 0 in both pictures counts as unchanged, not as a colour)
            float w_item = 1.0f, ratio[3] = {1, 1, 1};
            if (bg) {
                float lift = vq[0] - u[0], rm = 0;
                for (int q = 0; q < 3; q++) {
                    if (vq[q] - u[q] > lift) lift = vq[q] - u[q];
                    ratio[q] = (vq[q] + 8.0f) / (u[q] + 8.0f);
                    rm += ratio[q] / 3.0f;
                }
                float dev = 0;
                for (int q = 0; q < 3; q++)
                    if (fabsf(ratio[q] - rm) > dev) dev = fabsf(ratio[q] - rm);
                float w1 = lift <= 3.0f ? 0.0f : lift >= 10.0f ? 1.0f : (lift - 3.0f) / 7.0f;
                float w2 = dev <= 0.04f ? 0.0f : dev >= 0.12f ? 1.0f : (dev - 0.04f) / 0.08f;
                float w3 = rm >= 0.70f ? 0.0f : rm <= 0.55f ? 1.0f : (0.70f - rm) / 0.15f;
                w_item = w1 > w2 ? w1 : w2;
                if (w3 > w_item) w_item = w3;
            }
            // (well inside the lifted card's own box, a card hardly lighter than the board behind it, as in Paper night, is still the item: the board's cracks and
            // grain must not show through it)
            if (bg && cover.w > 8 && cover.h > 8 && x0 + x >= cover.x + 4 && x0 + x < cover.x + cover.w - 4 && y0 + y >= cover.y + 4 && y0 + y < cover.y + cover.h - 4) w_item = 1.0f;
            for (int q = 0; q < 3; q++) {
                // (the shadow's part of the picture, vq - u, scaled to how light the background is where it lands)
                float v = bg ? w_item * vq[q] + (1.0f - w_item) * (g0[x * 4 + q] + (vq[q] - u[q]) * ((g0[x * 4 + q] + 8.0f) / (u[q] + 8.0f))) : vq[q];
                int nv = (int)(v + 0.5f);
                nv = nv < 0 ? 0 : nv > 255 ? 255 : nv;
                if (nv != d[q]) changed = 1;
                d[q] = (uint8_t)nv;
            }
        }
    }
    free(keep);
    if (bg && g_ui_press_audit) { // a pixel that changed while the item layer was nowhere near it was the background moving
        // (how near: 4 px, and as far as the shrink moves the item's far edge, which is more on a wide item)
        int ar = 4 + (int)in;
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) {
                const uint8_t *now = s->px + ((size_t)(y0 + y) * s->w + x0 + x) * 4;
                if (!memcmp(now, save + ((size_t)y * w + x) * 4, 3)) continue;
                int near_item = 0;
                for (int j = y - ar; j <= y + ar && !near_item; j++)
                    for (int i = x - ar; i <= x + ar; i++) {
                        if (i < 0 || j < 0 || i >= w || j >= h) continue;
                        if (memcmp(save + ((size_t)j * w + i) * 4, bg->px + ((size_t)(y0 + j) * s->w + x0 + i) * 4, 3)) {
                            near_item = 1;
                            break;
                        }
                    }
                if (!near_item) g_ui_press_bg_changed++;
            }
    }
    surf_free(with_ghost);
    if (g_ui_press_hook) g_ui_press_hook(save, x0, y0, w, h, s, r, over != NULL);
    Rect reg = {x0, y0, w, h};
    g_ui_presses++;
    g_ui_press_changed += changed;
    ui_sound(SND_TICK, 0.6f, 0);
    ui_present_rects(s, &reg, 1);
    uint64_t t0 = ui_now();
    while (ui_now() - t0 < PRESS_MS) {
    }
    for (int y = 0; y < h; y++) memcpy(s->px + ((size_t)(y0 + y) * s->w + x0) * 4, save + (size_t)y * w * 4, (size_t)w * 4);
    free(save);
    ui_present_rects(s, &reg, 1);
}

// ---- touch: a finger becomes taps (a touch that stayed put) and drags (one that moved), each announced as it lands

enum { TQ_MAX = 24 };
static UiTouchEvt g_tq[TQ_MAX];

static void touch_push(int kind, float x, float y, float dx, float dy)
{
    if (G.tq_n == TQ_MAX) { // nobody is reading: forget the oldest
        memmove(g_tq, g_tq + 1, (TQ_MAX - 1) * sizeof g_tq[0]);
        G.tq_n--;
    }
    UiTouchEvt e = {kind, x, y, dx, dy};
    g_tq[G.tq_n++] = e;
}

int ui_touch_next(UiTouchEvt *e)
{
    if (G.tq_n == 0) return 0;
    *e = g_tq[0];
    memmove(g_tq, g_tq + 1, (size_t)(G.tq_n - 1) * sizeof g_tq[0]);
    G.tq_n--;
    return 1;
}

static uint64_t button_of_glyph(const char *g)
{
    if (!g) return 0;
    if (!strcmp(g, "A")) return BTN_A;
    if (!strcmp(g, "B")) return BTN_B;
    if (!strcmp(g, "X")) return BTN_X;
    if (!strcmp(g, "Y")) return BTN_Y;
    if (!strcmp(g, "L")) return BTN_L;
    if (!strcmp(g, "R")) return BTN_R;
    if (!strcmp(g, "ZL")) return BTN_ZL;
    if (!strcmp(g, "ZR")) return BTN_ZR;
    if (!strcmp(g, "R3")) return BTN_R3;
    if (!strcmp(g, "ZR+X")) return BTN_PURGE;
    if (!strcmp(g, "+")) return BTN_PLUS;
    if (!strcmp(g, G_MINUS)) return BTN_MINUS;
    return 0;
}

// feeds the finger's state in; returns the button a tap on the footer stands for, or UI_TOUCH if there are events to read
static uint64_t touch_update(int down, float x, float y)
{
    uint64_t out = 0;
    if (down && !G.t_down) {
        G.t_sx = G.t_lx = x;
        G.t_sy = G.t_ly = y;
        G.t_dragging = 0;
        touch_push(TOUCH_DOWN, x, y, 0, 0); // (for what is moving: a finger landing on it stops it)
    } else if (down) {
        if (!G.t_dragging && hypotf(x - G.t_sx, y - G.t_sy) >= 14.0f) {
            G.t_dragging = 1;
            G.t_lx = G.t_sx; // the drag counts from where the finger first landed
            G.t_ly = G.t_sy;
        }
        if (G.t_dragging && (x != G.t_lx || y != G.t_ly)) {
            touch_push(TOUCH_DRAG, x, y, x - G.t_lx, y - G.t_ly);
            G.t_lx = x;
            G.t_ly = y;
        }
    } else if (G.t_down) {
        if (G.t_dragging) {
            touch_push(TOUCH_END, G.t_lx, G.t_ly, 0, 0);
        } else {
            uint64_t b = button_of_glyph(skin_footer_glyph_at(G.t_sx, G.t_sy)); // the prompts along the bottom are buttons
            if (b) out |= b;
            else touch_push(TOUCH_TAP, G.t_sx, G.t_sy, 0, 0);
        }
        G.t_dragging = 0;
    }
    G.t_down = down;
    if (G.tq_n > 0) out |= UI_TOUCH;
    return out;
}

uint64_t ui_poll(int timeout_ms)
{
    ui_wait_finish();
    if (G.quit) return UI_QUIT;
    uint64_t carried = G.pending;
    G.pending = 0;
    if (carried) timeout_ms = 0; // something is already waiting: look for more, but don't hold it up
    uint64_t b = poll_raw(timeout_ms);
    if (b == UI_QUIT) return b;
    return b | carried;
}

// Waits for a button as the platform does (timeout_ms < 0 for ever, 0 only polls), but while the "still working" ink is being rubbed out the wait is cut into short
// pieces so that it keeps moving; input still ends the wait at once.
static uint64_t wait_ticking(int timeout_ms)
{
    if (timeout_ms == 0 || !ui_wait_erasing()) return G.plat->wait_input(G.plat, timeout_ms);
    uint64_t end = timeout_ms < 0 ? 0 : ui_now() + (uint64_t)timeout_ms;
    for (;;) {
        uint64_t now = ui_now();
        int slice = 16;
        if (timeout_ms > 0) {
            if (now >= end) return 0;
            if (end - now < 16) slice = (int)(end - now);
        }
        uint64_t b = G.plat->wait_input(G.plat, slice);
        if (b) return b;
        if (ui_now() - now > 200) return 0; // (a platform that lets a long time pass in one wait, as the PC test's does for a scripted pause: that wait is over)
        ui_wait_tick();
        if (!ui_wait_erasing()) { // done: the rest of the wait is as the caller asked
            if (timeout_ms < 0) return G.plat->wait_input(G.plat, -1);
            now = ui_now();
            return now >= end ? 0 : G.plat->wait_input(G.plat, (int)(end - now));
        }
    }
}

static uint64_t poll_raw(int timeout_ms)
{
    uint64_t b = wait_ticking(timeout_ms);
    if (b != UI_QUIT && G.plat->touch) {
        float tx = 0, ty = 0;
        int td = G.plat->touch(G.plat, &tx, &ty);
        b |= touch_update(td, tx, ty);
    }
    if (b == UI_QUIT) {
        G.quit = 1;
        return b;
    }
    if (G.mode == THEME_AUTO && timeout_ms != 0) { // (the console's theme changed, and it is followed)
        int d = G.plat->system_dark(G.plat);
        if (d >= 0 && d != G.dark) {
            apply_theme();
            b |= UI_REDRAW;
        }
    }
    return b;
}

int ui_quit_requested(void) { return G.quit; }

// While on, X is reported when it is let go (BTN_X, or BTN_PURGE if ZR was pressed meanwhile), so one screen can tell X from X + ZR.
void ui_x_on_release(int on)
{
    if (G.plat->x_on_release) G.plat->x_on_release(G.plat, on);
}

void ui_wait(int ms)
{
    uint64_t end = ui_now() + (uint64_t)(ms > 0 ? ms : 0);
    while (!G.quit) {
        uint64_t now = ui_now();
        if (now >= end || poll_raw((int)(end - now < 40 ? end - now : 40)) == UI_QUIT) break; // (in short waits: a waiting page's dots go on moving)
        ui_busy_tick();
    }
}

// ------------------------------------------------------------------------------------------------ focus glide

#define GLIDE_OMEGA 27.0f // 1/s: settles in about a quarter of a second

void glide_snap(Glide *g, float x, float y, float w, float h)
{
    float t[4] = {x, y, w, h};
    for (int i = 0; i < 4; i++) {
        g->v[i] = g->tgt[i] = t[i];
        g->vel[i] = 0;
    }
    g->moving = 0;
    g->init = 1;
}

void glide_to(Glide *g, float x, float y, float w, float h)
{
    if (!g->init) {
        glide_snap(g, x, y, w, h);
        return;
    }
    float t[4] = {x, y, w, h};
    int same = 1;
    for (int i = 0; i < 4; i++)
        if (fabsf(t[i] - g->tgt[i]) > 0.01f) same = 0;
    if (same) return;
    for (int i = 0; i < 4; i++) g->tgt[i] = t[i];
    if (!g->moving) { // the clock starts at the press, not at the last time the screen woke up
        g->moving = 1;
        g->t = ui_now();
    }
}

int glide_step(Glide *g)
{
    if (!g->moving) return 0;
    uint64_t now = ui_now();
    float dt = now > g->t ? (float)(now - g->t) / 1000.0f : 0.0f;
    if (dt > 0.05f) dt = 0.05f; // a slow frame never leaps
    g->t = now;
    float ex = expf(-GLIDE_OMEGA * dt);
    int done = 1;
    for (int i = 0; i < 4; i++) {
        float e = g->v[i] - g->tgt[i], c = g->vel[i] + GLIDE_OMEGA * e;
        float ne = (e + c * dt) * ex;
        g->vel[i] = (g->vel[i] - GLIDE_OMEGA * c * dt) * ex;
        g->v[i] = g->tgt[i] + ne;
        if (fabsf(ne) > 0.3f || fabsf(g->vel[i]) > 12.0f) done = 0;
    }
    if (done) {
        for (int i = 0; i < 4; i++) {
            g->v[i] = g->tgt[i];
            g->vel[i] = 0;
        }
        g->moving = 0;
    }
    return g->moving;
}

// ---- hops

void hops_snap(Hops *h, float x, float y)
{
    h->n = 1;
    h->x[0] = x;
    h->y[0] = y;
    h->a[0] = 1.0f;
}

void hops_to(Hops *h, float x, float y)
{
    if (h->n == 0) {
        hops_snap(h, x, y);
        return;
    }
    if (fabsf(h->x[h->n - 1] - x) < 0.5f && fabsf(h->y[h->n - 1] - y) < 0.5f) return;
    if (h->n == 4) { // a held button: forget the faintest
        for (int i = 0; i + 1 < h->n; i++) {
            h->x[i] = h->x[i + 1];
            h->y[i] = h->y[i + 1];
            h->a[i] = h->a[i + 1];
        }
        h->n--;
    }
    h->x[h->n] = x;
    h->y[h->n] = y;
    h->a[h->n] = 0.0f;
    h->n++;
}

int hops_step(Hops *h, float dt)
{
    int moving = 0;
    for (int i = 0; i < h->n;) {
        if (i == h->n - 1) {
            h->a[i] += dt / 0.09f;
            if (h->a[i] >= 1.0f) h->a[i] = 1.0f;
            else moving = 1;
        } else {
            h->a[i] -= dt / 0.13f;
            if (h->a[i] <= 0.0f) { // gone
                for (int j = i; j + 1 < h->n; j++) {
                    h->x[j] = h->x[j + 1];
                    h->y[j] = h->y[j + 1];
                    h->a[j] = h->a[j + 1];
                }
                h->n--;
                continue;
            }
            moving = 1;
        }
        i++;
    }
    return moving;
}

float hops_strength(float a)
{
    return a * a * (3.0f - 2.0f * a);
}

// ---- lifts

void lifts_snap(Lifts *l, int sel)
{
    l->n = sel >= 0;
    l->idx[0] = sel;
    l->w[0] = 1.0f;
}

// advance by dt seconds; returns 1 while any tile is still rising or settling
int lifts_step(Lifts *l, int sel, float dt) // sel < 0: none is picked (on home: the focus is above the grid)
{
    int have = sel < 0;
    for (int i = 0; i < l->n; i++)
        if (l->idx[i] == sel) have = 1;
    if (!have) {
        if (l->n == 6) { // full (a held button): forget the tile that is closest to resting
            int lo = 0;
            for (int i = 1; i < l->n; i++)
                if (l->w[i] < l->w[lo]) lo = i;
            for (int i = lo; i + 1 < l->n; i++) {
                l->idx[i] = l->idx[i + 1];
                l->w[i] = l->w[i + 1];
            }
            l->n--;
        }
        l->idx[l->n] = sel;
        l->w[l->n++] = 0.0f;
    }
    float a = 1.0f - expf(-dt / 0.07f);
    int moving = 0;
    for (int i = 0; i < l->n;) {
        float tg = l->idx[i] == sel ? 1.0f : 0.0f;
        l->w[i] += (tg - l->w[i]) * a;
        if (fabsf(tg - l->w[i]) < 0.012f) l->w[i] = tg;
        else moving = 1;
        if (tg == 0.0f && l->w[i] == 0.0f) { // back at rest: nothing left to draw over its resting copy
            for (int j = i; j + 1 < l->n; j++) {
                l->idx[j] = l->idx[j + 1];
                l->w[j] = l->w[j + 1];
            }
            l->n--;
            continue;
        }
        i++;
    }
    return moving;
}

#define BUMP_PX 16.0f   // (the lean peaks at about half of this)
#define BUMP_TAU 0.07f  // how fast it dies away (s)
#define BUMP_HALF 0.11f // half a swing (s): out and back, then a little the other way
#define BUMP_LEN 0.30f

void bump_start(Bump *b, int dx, int dy)
{
    if (dx || dy) ui_sound(SND_REFUSE, G.plat && G.plat->repeated ? 0.25f : 0.8f, 0); // (a press that leads nowhere: two low taps)
    b->dx = (float)dx;
    b->dy = (float)dy;
    b->t0 = ui_now();
    b->on = dx || dy;
}

int bump_offset(Bump *b, float *ox, float *oy)
{
    *ox = *oy = 0;
    if (!b->on) return 0;
    float t = (float)(ui_now() - b->t0) / 1000.0f;
    if (t >= BUMP_LEN) {
        b->on = 0;
        return 0;
    }
    float k = BUMP_PX * expf(-t / BUMP_TAU) * sinf(3.14159265f * t / BUMP_HALF);
    *ox = b->dx * k;
    *oy = b->dy * k;
    return 1;
}

void bump_dir(uint64_t b, int *dx, int *dy)
{
    *dx = (b & BTN_RIGHT) ? 1 : (b & BTN_LEFT) ? -1 : 0;
    *dy = (b & BTN_DOWN) ? 1 : (b & BTN_UP) ? -1 : 0;
}

void ui_status(SkinStatus *st)
{
    memset(st, 0, sizeof *st);
    st->battery = -1;
    if (G.status_fn) G.status_fn(G.status_user, st);
    char name[sizeof st->account]; // the top bar names the console, not the account (whose name follows whoever is loaded)
    if (G.plat && G.plat->device_name && G.plat->device_name(G.plat, name, sizeof name) == 0 && name[0])
        snprintf(st->account, sizeof st->account, "%s", name);
}

void ui_backdrop(void)
{
    skin_footer_forget(); // a dialog is open: the dimmed prompts underneath are not buttons
    if (G.have_screen) {
        surf_copy(G.dim, G.last);
        Cx c = ui_cx(G.dim);
        skin_dim(&c);
    } else { // nothing has been drawn yet (the welcome questions): the plain screen, not dimmed
        Cx c = ui_cx(G.dim);
        skin_board(&c);
        SkinStatus st;
        ui_status(&st);
        skin_top_bar(&c, NULL, &st);
    }
}

void ui_screen_done(void)
{
    surf_copy(G.last, G.work);
    G.have_screen = 1;
#ifdef UI_PROFILE
    prof_t0 = prof_now();
    prof_armed = 1;
    prof_tag = NULL;
#endif
    ui_wait_begin(); // (what the app does now, before the next screen shows, may take a while: after a moment a mark says so)
}

// ------------------------------------------------------------------------------------------------ text helpers

int ui_wrap(Font *f, float px, float maxw, const char *text, char lines[][160], int maxlines)
{
    int n = 0;
    const char *p = text;
    while (*p && n < maxlines) {
        const char *nl = strchr(p, '\n');
        size_t plen = nl ? (size_t)(nl - p) : strlen(p);
        if (plen == 0) { // an empty line
            lines[n++][0] = 0;
            p += nl ? 1 : 0;
            if (!nl) break;
            continue;
        }
        char para[512];
        if (plen >= sizeof para) plen = sizeof para - 1;
        memcpy(para, p, plen);
        para[plen] = 0;
        char cur[160] = "";
        char *save = para;
        while (*save && n < maxlines) {
            while (*save == ' ') save++;
            if (!*save) break;
            char *sp = strchr(save, ' ');
            size_t wl = sp ? (size_t)(sp - save) : strlen(save);
            char word[160];
            if (wl >= sizeof word) wl = sizeof word - 1;
            memcpy(word, save, wl);
            word[wl] = 0;
            char trial[320];
            snprintf(trial, sizeof trial, "%s%s%s", cur, cur[0] ? " " : "", word);
            if (cur[0] && text_width(f, px, 0, trial) > maxw) {
                snprintf(lines[n++], 160, "%s", cur);
                snprintf(cur, sizeof cur, "%s", word);
            } else {
                snprintf(cur, sizeof cur, "%.159s", trial);
            }
            save += wl;
        }
        if (cur[0] && n < maxlines) snprintf(lines[n++], 160, "%s", cur);
        p += (nl ? (size_t)(nl - p) + 1 : strlen(p));
    }
    return n;
}

int ui_wrap_body(const char *text, float max_width, char lines[][160], int max_lines)
{
    return ui_wrap(G.skin.med, 19, max_width, text, lines, max_lines);
}

// ------------------------------------------------------------------------------------------------ formatting

void ui_format_clock(time_t t, char *out, size_t n)
{
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    if (!G.plat->local_time || G.plat->local_time(G.plat, t, &tm) != 0) gmtime_r(&t, &tm);
    if (G.clock24) {
        snprintf(out, n, "%02d:%02d", tm.tm_hour, tm.tm_min);
    } else {
        int h = tm.tm_hour % 12;
        snprintf(out, n, "%d:%02d %s", h == 0 ? 12 : h, tm.tm_min, tm.tm_hour >= 12 ? "PM" : "AM");
    }
}

void ui_ago_short(int64_t then, int64_t now, char *out, size_t n)
{
    if (then <= 0) { snprintf(out, n, "new"); return; }
    int64_t d = now - then;
    if (d < 60) snprintf(out, n, "now");
    else if (d < 3600) snprintf(out, n, "%dm ago", (int)(d / 60));
    else if (d < 86400) snprintf(out, n, "%dh ago", (int)(d / 3600));
    else if (d < 14 * 86400) snprintf(out, n, "%dd ago", (int)(d / 86400));
    else if (d < 63 * 86400) snprintf(out, n, "%dw ago", (int)(d / (7 * 86400)));
    else snprintf(out, n, "%dmo ago", (int)(d / (30 * 86400)));
}

void ui_ago_long(int64_t then, int64_t now, char *out, size_t n)
{
    if (then <= 0) { snprintf(out, n, "never played"); return; }
    int64_t d = now - then;
    if (d < 60) snprintf(out, n, "just now");
    else if (d < 3600) snprintf(out, n, "%d minute%s ago", (int)(d / 60), d / 60 == 1 ? "" : "s");
    else if (d < 86400) snprintf(out, n, "%d hour%s ago", (int)(d / 3600), d / 3600 == 1 ? "" : "s");
    else if (d < 2 * 86400) snprintf(out, n, "yesterday");
    else if (d < 14 * 86400) snprintf(out, n, "%d days ago", (int)(d / 86400));
    else if (d < 63 * 86400) snprintf(out, n, "%d weeks ago", (int)(d / (7 * 86400)));
    else snprintf(out, n, "%d months ago", (int)(d / (30 * 86400)));
}

static void local_tm(time_t t, struct tm *tm)
{
    memset(tm, 0, sizeof *tm);
    if (!G.plat->local_time || G.plat->local_time(G.plat, t, tm) != 0) gmtime_r(&t, tm);
}

void ui_format_stamp(time_t t, char *out, size_t n)
{
    struct tm tm;
    local_tm(t, &tm);
    snprintf(out, n, "%02d-%02d-%04d--%02d-%02d", tm.tm_mon + 1, tm.tm_mday, tm.tm_year + 1900, tm.tm_hour, tm.tm_min);
}

void ui_format_stamp_when(const char *stamp, time_t now, char *out, size_t n)
{
    static const char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    int mo, d, y, h, mi;
    if (sscanf(stamp, "%2d-%2d-%4d--%2d-%2d", &mo, &d, &y, &h, &mi) != 5 || mo < 1 || mo > 12) {
        snprintf(out, n, "%s", stamp);
        return;
    }
    char clock[24], today[32], yest[32];
    if (G.clock24) snprintf(clock, sizeof clock, "%02d:%02d", h, mi);
    else snprintf(clock, sizeof clock, "%d:%02d %s", h % 12 == 0 ? 12 : h % 12, mi, h >= 12 ? "PM" : "AM");
    ui_format_stamp(now, today, sizeof today);
    ui_format_stamp(now - 86400, yest, sizeof yest);
    if (!strncmp(stamp, today, 10)) snprintf(out, n, "Today, %s", clock);
    else if (!strncmp(stamp, yest, 10)) snprintf(out, n, "Yesterday, %s", clock);
    else snprintf(out, n, "%s %d, %s", months[mo - 1], d, clock);
}

void ui_format_when(time_t t, time_t now, char *out, size_t n)
{
    static const char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    struct tm a, b, y;
    local_tm(t, &a);
    local_tm(now, &b);
    local_tm(now - 86400, &y);
    char clock[24];
    ui_format_clock(t, clock, sizeof clock);
    if (a.tm_year == b.tm_year && a.tm_yday == b.tm_yday) snprintf(out, n, "Today, %s", clock);
    else if (a.tm_year == y.tm_year && a.tm_yday == y.tm_yday) snprintf(out, n, "Yesterday, %s", clock);
    else snprintf(out, n, "%s %d, %s", months[a.tm_mon % 12], a.tm_mday, clock);
}

void ui_format_minutes(int minutes, char *out, size_t n)
{
    int h = minutes / 60, m = minutes % 60;
    if (h == 0) snprintf(out, n, "%d min", m);
    else if (m == 0) snprintf(out, n, "%d hour%s", h, h == 1 ? "" : "s");
    else snprintf(out, n, "%d hour%s %d min", h, h == 1 ? "" : "s", m);
}

void ui_format_bytes(uint64_t b, char *out, size_t n)
{
    if (b >= (1ull << 40)) snprintf(out, n, "%.1f TB", (double)b / (double)(1ull << 40));
    else if (b >= (1ull << 30)) snprintf(out, n, "%.1f GB", (double)b / (double)(1ull << 30));
    else if (b >= 100 * (1ull << 20)) snprintf(out, n, "%.0f MB", (double)b / (double)(1ull << 20));
    else if (b >= (1ull << 20)) snprintf(out, n, "%.1f MB", (double)b / (double)(1ull << 20));
    else if (b >= 1024) snprintf(out, n, "%.0f KB", (double)b / 1024.0);
    else snprintf(out, n, "%llu B", (unsigned long long)b);
}
