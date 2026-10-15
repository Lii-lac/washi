// Shared state of the interface toolkit (ui_*.c). Not for the app.
#ifndef UI_INTERNAL_H
#define UI_INTERNAL_H

#include "ui.h"

typedef enum { TOUCH_TAP = 1, TOUCH_DRAG, TOUCH_END, TOUCH_DOWN } UiTouchKind;
// DOWN: a finger has just landed at (x, y); what follows is either a TAP (it lifted where it landed) or DRAGs and an END.
// TAP: where the finger landed (x, y). DRAG: where it is (x, y) and how far it moved since the last event (dx, dy).
// END: the finger lifted after a drag. After ui_poll returns UI_TOUCH a screen reads these with ui_touch_next.
typedef struct UiTouchEvt {
    int kind;
    float x, y, dx, dy;
} UiTouchEvt;

typedef struct {
    Platform *plat;
    Skin skin;
    int mode, dark, clock24, quit;
    Surf *work;   // the frame that gets drawn and presented
    Surf *base;   // the part of the current screen that doesn't change while it is open
    Surf *dim;    // the dimmed backdrop (plus the static part of a dialog) while a dialog is open
    Surf *last;   // the last full screen (home or a list), as the user left it: what dialogs dim
    Surf *sel;    // the screen again with its items drawn as focused (theme.sel): what goes inside the focus frame
    int have_screen;
    void (*status_fn)(void *, SkinStatus *);
    void *status_user;
    int (*icon_fn)(void *, uint64_t, const uint8_t **, size_t *);
    void *icon_user;
    int kb_unavailable;
    // touch: raw finger state and the events made from it (see ui_touch_next)
    int t_down, t_dragging;
    float t_sx, t_sy, t_lx, t_ly;
    struct UiTouchEvt *tq;
    int tq_n;
    // transitions (see ui_transition): what was on screen, the blend being shown, the dimmed screen under a dialog
    Surf *prev, *mix, *back;
    int tr, prev_ok;
    float card[4];
    int tr_next;      // what the next ui_transition plays instead of a crossfade (ui_next_grows_from, ...), with its rect in card
    uint64_t pending; // input that arrived while a transition played, for the next ui_poll
    // this run got going: when the home screen was first up, and whether Platform.started_ok was called (ui_home)
    uint64_t home_up_at;
    int started_ok;
} UiState;

extern UiState G;

// A tap on the D-pad at the end of a menu wraps round to the other end; a held one (auto-repeat) stops there instead.
static inline int wrap_ok(void) { return !G.plat->repeated; }

static inline Cx ui_cx(Surf *s) { Cx c = {&G.skin, s}; return c; }

void ui_present(Surf *s);
// Like ui_present, but only the n rectangles changed since the frame shown last (the platform then copies just those). Anything that
// is not a plain next frame (the first one, a screen change to play, a platform without present_rects) is a full present.
void ui_present_rects(Surf *s, const Rect *r, int n);
void ui_busy_tick(void); // a waiting page's moving dots (ui_busy): the next frame of them, if it is time (the long loops' pumps call it)
void ui_busy_stop(void); // they stop (the next screen is coming)
uint64_t ui_now(void);
// The item in r of the frame s (the one on screen) pushes flat for ~70 ms, then goes back: call just before the action of an A press or tap.
void ui_press(Surf *s, Rect r);
void ui_press_k(Surf *s, Rect r, float scale);
void ui_press_over(Surf *s, Rect r, float scale, const Surf *over);
// The other items on the screen (up to 8 rectangles: their slips, shadows and tape): the next ui_press leaves what lies in them exactly as it is.
void ui_press_keep(const Rect *rs, int n);
void ui_press_keep_off(Rect card);
void ui_press_ghost(const PaperSpr *ghost, float cx, float cy, float angle, float alpha); // the pressed item's ghost stays still (call before the press)
void ui_press_cover(Rect body); // and the lifted card over it: the ghost it covers stays covered (call before the press, with ui_press_ghost)
void ui_press_whole(Rect whole); // the box the item and all that sticks out of it (tape, pin, shadow) is in: all of it goes in with it (call before the press)
void ui_press_free(const Rect *rs, int n); // holes in the rectangles of ui_press_keep: the item's own tape and pin, which stand over a neighbour's shadow or edge (call before the press)
void ui_press_keep_hard(const Rect *rs, int n); // things that stay as they are whatever else is set (a bar with words on it): ui_press_free holes do not reach into them (call before the press)
void ui_github_mark(Surf *s, float x, float y, float size, Col c); // GitHub's mark, `size` px square, top left at (x, y) (ui_about.c)
PaperSpr *ui_wait_still(float t_ms, uint32_t seed);                // the "still working" crayon drawing t ms in, as a picture (ui_wait.c)
void ui_lift_sfx(int grab, int held, int kind); // what a lift being grabbed or let go sounds like (the hook of paper.h; the Backups page calls it too)
// ---- "still working" (ui_wait.c): after a screen is done the app may be busy a while before the next one shows (reading the slow card, baking pictures). Once it has been
// busy for g_ui_wait_after_ms a crayon starts drawing little symbols on the left of the footer; the long loops keep it moving (gfx_pump, fsx_pump). The next screen's first
// frame, a dialog or waiting for input starts rubbing it out at once, and that does not hold anything up: the ink is laid over every frame shown while it lasts, so the
// next screen can be used while it is still going.
void ui_wait_begin(void);  // the clock starts (ui_screen_done does this)
void ui_wait_finish(void); // the work is over: the rubbing out begins (if the drawing is there), without waiting for it
void ui_wait_abort(void);  // forget it; the place is shown again as the app's frame has it
int ui_wait_shown(void);   // the drawing is on the display now, made by its own frames
Surf *ui_wait_frame(void); // its copy of the screen while it is shown (else NULL): a change to the same screen meanwhile goes on it and is shown from it
int ui_wait_erasing(void); // it is being rubbed out: the frames shown carry what is left of the ink
Rect ui_wait_area(void);   // the rectangle it works in
// the ink over a frame about to be shown: 0 = nothing to do, 1 = laid on s (take it off with ui_wait_overlay_off(s, 1) after showing it, and show ui_wait_area() too),
// 2 = the rubbing out has just ended (show ui_wait_area() once more, nothing laid; then ui_wait_overlay_off(s, 2))
int ui_wait_overlay_on(Surf *s);
void ui_wait_overlay_off(Surf *s, int how);
void ui_wait_tick(void);   // keeps the rubbing out going while nothing else is shown (called from the waits and the long jobs)
Surf *ui_display_base(void); // the frame the display is showing, or about to be given: the old screen while the change to the next one has not played yet, else G.work
extern int g_ui_wait_after_ms; // how long the app is busy before it starts (ms; tests set it huge to keep it away, or small)
extern int g_ui_wait_count;    // how many symbols the bar may hold (1..5)
extern long g_ui_wait_shown, g_ui_wait_frames; // tests: how many times it has started, how many frames it has made
// Asks the platform for a sound (SND_*), after delay_ms. The same sound is never started twice within a moment, so a held button is not a buzz.
void ui_sound(int snd, float gain, int delay_ms);
extern long g_ui_presses, g_ui_press_changed;
extern long g_ui_key_wiggles;  // tests: presses on the keys of the keypad and keyboard that made the key wiggle
extern long g_ui_home_frames; // tests: frames the home screen has composed
extern int g_ui_home_tape_air; // tests: cards with a strip in flight in the last frame composed (the pin animation)
extern int g_ui_home_pa_on;  // tests: the pin animation playing in the last frame composed (1 in, 2 out, 0 none)
extern int g_ui_home_pa_dir; // tests: the direction (+1 in, -1 out) of the pin animation the last visit to Home began, 0 none
extern float g_ui_home_pin_h, g_ui_home_pin_x, g_ui_home_pin_y; // tests: the pin of the card with the focus in the last frame composed: its height off the card (-1: none) and where it goes in
extern float g_ui_home_lift, g_ui_home_lift_low; // tests: how far up (0..1) the Home item with the focus was in the last frame composed (-1: none), and the least it
                                                 // has been in any frame since a test last set the second one to 2
// wait for a button (see Platform.wait_input). Also follows the console's theme: returns UI_REDRAW in the result when it changed.
uint64_t ui_poll(int timeout_ms);
int ui_touch_next(UiTouchEvt *e); // 1 and the next pending touch event, or 0
static inline int in_rect(float x, float y, float rx, float ry, float rw, float rh) { return x >= rx && x < rx + rw && y >= ry && y < ry + rh; }
void ui_status(SkinStatus *st);
// A new screen is about to be shown: its first frame fades in over what is on screen now. Call when a screen starts.
void ui_transition(void);
// The backdrop and card of a dialog: G.dim = the dimmed screen with the card (x, y, w, h) drawn on it; returns a context to draw on
// it. The dialog's first frame slides up and fades in over the dimmed screen.
Cx ui_dialog_card(float x, float y, float w, float h);
// The same without the card: G.dim = the dimmed screen, and the dialog's first frame rises over (x, y, w, h); the caller paints its own card.
void ui_dialog_begin(float x, float y, float w, float h);
void ui_backdrop(void);    // G.dim = what the user was looking at, dimmed
void ui_screen_done(void); // remember G.work as the last full screen (call before a screen returns)
// The order menu (the paper slips Home's chip drops), over the screen in G.work, dropped from the chip at (chip_x, chip_y, chip_w x chip_h): names[] (at most 8),
// the order in use ticked. Returns the one picked, or -1 (B, R3 again, or a tap outside). Home, the Extra icons picker and its admin page all use it.
// chip_lifted: the chip has the focus, so it is lifted (it matters where a sticker of the sticker sheet lands).
int ui_sort_menu(const char *const *names, int nnames, int sort_mode, float chip_x, float chip_y, float chip_w, float chip_h, int chip_lifted);
// The order chip's paper slip with its R3 mark and the order's name (drawn into s, its top left at (x, y), w wide and 48 high) on a still page;
// idx: the order's place in its menu.
void ui_order_chip(Surf *s, float x, float y, float w, const char *label, int idx);
// Edit Icon's Picture page (ui_extra.c): a strip of cards, the console's own pictures first, then the Extra icons game by game. 1 = a picture was chosen
// (*picture: a console picture, an Extra icon, or -1 for the letter), 0 = back.
int ui_picture_strip(const char *name, int swatch, int *picture);
// A focus frame that glides: a critically damped spring on x, y, w, h (no overshoot). It always moves on from where it is now,
// at the speed it has, so a new press mid-glide bends the motion instead of restarting it.
typedef struct {
    float v[4], vel[4], tgt[4];
    uint64_t t;
    int init, moving;
} Glide;
void glide_snap(Glide *g, float x, float y, float w, float h); // jump there (a new screen, or the grid scrolled)
void glide_to(Glide *g, float x, float y, float w, float h);   // glide there; the first call just places it
int glide_step(Glide *g);                                      // advance to now; 1 while still moving (poll with timeout 0)
// A focus frame that hops instead of travelling (for grids, where a glide crosses the gaps between items): the frame where it was
// fades out while a new one fades in on the next item. Positions are in whatever space the screen uses.
typedef struct {
    float x[4], y[4], a[4]; // the newest frame is the last; the rest are fading out
    int n;
} Hops;
void hops_snap(Hops *h, float x, float y);       // the frame is here, nothing fading
void hops_to(Hops *h, float x, float y);         // the focus moved there (no change if it is already there)
int hops_step(Hops *h, float dt);                // advance by dt seconds; 1 while anything is still fading
float hops_strength(float a);                    // the eased strength to draw a frame with
// Items that rise a little when they are picked and settle back when they are left, each on its own clock, while the frame hops
// between them (the home grid's tiles, a keypad's keys). Only a few are ever midway, so the list is tiny.
typedef struct {
    int idx[6];
    float w[6]; // 0 = resting, 1 = fully lifted
    int n;
} Lifts;
void lifts_snap(Lifts *l, int sel);          // sel is up and nothing else is (sel < 0: nothing is)
int lifts_step(Lifts *l, int sel, float dt); // advance by dt seconds, sel rising; 1 while any is still moving
// A nudge for pressing toward nothing (Up on the top row, say): the thing in focus leans that way, frame and all, and springs
// back, in about a quarter of a second. Each screen moves its focused item by the offset while it runs.
typedef struct {
    float dx, dy;
    uint64_t t0;
    int on;
} Bump;
void bump_start(Bump *b, int dx, int dy);       // dx, dy: -1, 0 or 1 (the way that was pressed)
int bump_offset(Bump *b, float *ox, float *oy); // its offset now, in pixels; 1 while it is still moving
// the direction pressed (BTN_UP/DOWN/LEFT/RIGHT in b) as dx, dy
void bump_dir(uint64_t b, int *dx, int *dy);
// word-wrap text (honours '\n') into lines of at most maxw pixels; returns the number of lines
int ui_wrap(Font *f, float px, float maxw, const char *text, char lines[][160], int maxlines);

// ---- the home screen's hooks (ui_home.c)
// Tests: while g_home_oracle is 1, every frame the home screen shows (drawn by rectangles) is also composed whole, from scratch, and
// compared; frames that differ at all and frames that differ by more than one level of a channel are counted.
extern int g_home_oracle;
extern long g_home_oracle_frames, g_home_oracle_bad, g_home_oracle_soft;
// 1: the home screen shows its frame-time readout. -1 (the start-up value): on if the file <root>/debug-timing exists.
extern int g_ui_debug_timing;
// which of the banner's looks (the doodles on the loaded profile's card) this run has: -1 (the start-up value) picks one at random the first time it is wanted.
extern int g_ui_banner_variant;
// For the intro (ui_intro.c): the home screen's first frame drawn into `full`, and on a black and a white board without the corner's mark and name (any of the
// three may be NULL), without showing it; and where the tiles are, so the intro can bring the screen in piece by piece.
typedef struct {
    int ntiles;                              // the tiles showing (the "new profile" one included), up to two rows of seven
    float cx[14], cy[14];                    // their middles
    int row[14], col[14], row_n[2];          // which row and column each is in, and how many each row has
    float x0, y0, pitch_x, pitch_y;          // the first tile's middle and the grid's steps
    int rail;                                // the A-Z rail is showing
} UiHomeGeo;
void ui_home_capture(UiHome *h, Surf *full, Surf *black, Surf *white, UiHomeGeo *geo);

// A mark drawn over a window that scrolls up and down (a grid's focus outline) is cut off where the window ends, as its contents are: the rows
// of (x0, y0)-(x1, y1) above wy0 and from wy1 down are kept before it is drawn and put back after. 0 = no room to keep them (draw nothing). (ui_scene.c)
typedef struct {
    uint8_t *keep;
    int x0, w, ta, nt, ba, nb;
} UiKeep;
int ui_keep_outside(UiKeep *k, const Surf *s, int x0, int y0, int x1, int y1, int wy0, int wy1);
void ui_put_back(UiKeep *k, Surf *s);
// the same for the mark round a cell (its box x0..x1, y0..y1) whose own top and bottom are cy0, cy1: it is cut at an edge of the window only
// where the cell is cut there itself (a cell wholly inside keeps all of its mark, reaching past the window's edge or not)
int ui_keep_cell_mark(UiKeep *k, const Surf *s, int x0, int y0, int x1, int y1, float cy0, float cy1, int wy0, int wy1);

// A D-pad press in a grid of `count` cells `cols` wide, filled a row at a time (the last row may be short): the cell it goes to, or -1 for nowhere
// (the caller leans). Off an edge it goes round to the other end of the same row or column, on a press and on a hold (the user asked for the
// pictures to go round held too).
int ui_grid_nav(int sel, int count, int cols, int dx, int dy);

#endif
