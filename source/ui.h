// The Washi interface toolkit, in the Switch's own style (see gfx/skin.h). Platform-independent: it draws with gfx.c and
// talks to the machine only through platform.h. Every call here is blocking: it shows a screen, handles buttons, and returns
// when the user has decided something.
#ifndef UI_H
#define UI_H

#include "icons.h"
#include <stddef.h>
#include <stdint.h>

#include "gfx/skin.h"
#include "platform.h"

typedef enum { UI_INFO, UI_OK, UI_WARN, UI_ERROR } UiKind;
enum { THEME_AUTO = 0, THEME_LIGHT = 1, THEME_DARK = 2 };

// ---- setup
// `status_fn` fills in the clock, battery and account shown in the top bar (called whenever a screen is drawn).
void ui_init(Platform *p, int theme_mode, void (*status_fn)(void *user, SkinStatus *out), void *status_user);
void ui_shutdown(void);
void ui_set_theme_mode(int mode);          // THEME_*; AUTO follows the console and re-themes while the app runs
int ui_dark(void);                         // the theme in use right now (dark = Paper night)
int ui_in_transition(void);               // a screen change is being played right now (for tests that count frames)
void ui_x_on_release(int on);             // on: X is reported when let go, as BTN_PURGE if ZR was pressed while it was held (else BTN_X)
int ui_quit_requested(void);             // the system asked the app to exit; every call returns promptly from now on
void ui_set_clock24(int on);
// Game icons for the progress screen: JPEG bytes for a title (or return non-zero for none).
void ui_set_icon_source(int (*fn)(void *user, uint64_t title, const uint8_t **jpeg, size_t *size), void *user);

// ---- small helpers
void ui_format_clock(time_t t, char *out, size_t n);                       // "2:14 PM" or "14:14"
void ui_ago_short(int64_t then, int64_t now, char *out, size_t n);        // "2d ago"
void ui_ago_long(int64_t then, int64_t now, char *out, size_t n);         // "yesterday"
void ui_format_stamp(time_t t, char *out, size_t n);                       // "10-05-2026--14-30" (a folder name)
void ui_format_bytes(uint64_t b, char *out, size_t n);
void ui_format_minutes(int minutes, char *out, size_t n); // "45 min", "1 hour", "2 hours 30 min"
void ui_format_when(time_t t, time_t now, char *out, size_t n);          // "Today, 1:48 PM", "Yesterday, ...", "Oct 2, ..."

// ---- home: the grid of profiles
typedef struct {
    char name[40];
    char detail[48];       // "6 games  ·  2d ago"
    int locked;
    int color;             // avatar look: a swatch, or SKIN_LOOK(swatch, picture)
    int pinned;            // an admin pinned it: it comes first, and its card shows it
} UiPerson;

typedef struct {
    const UiPerson *people; // who the grid shows: everyone, or those who match `query`
    int n;
    int total;             // how many profiles there are in all
    int loaded;            // index into people of the loaded profile, or -1 (nobody, or the search hides them)
    UiPerson card;         // the loaded profile, for its card above the grid (whatever the grid shows)
    int card_on;           // someone is loaded (else the card shows Nobody, or the guest)
    char query[40];        // what the grid is narrowed to, shown in the search bar ("" = everyone)
    char sort_label[24];   // how the grid is ordered, as the chip above it says ("Recently played")
    const char *const *sort_names; // the orders the chip's menu offers, and which is in use (in/out: HOME_SORT = changed)
    int nsorts, sort_mode;
    int rail;              // show the A-Z rail and jump by letter (only when the grid is in name order)
    char loaded_sub[96];   // line under the loaded name (or under "Nobody"/"Guest" when nothing is loaded and this is set)
    int guest;             // nobody is loaded and the console was cleared: shown as "Guest" (with Miku's picture)
    int guest_look;        // the avatar look (swatch + picture) shown for the guest
    int sel;               // 0..n, where n is the "New profile" tile (in/out)
    int top_row;           // first visible grid row (in/out)
    int focus;             // 0 = the grid; else what above it the D-pad is on: the chip, the search bar, the card, Sign out (in/out)
    char toast[96];        // shown for toast_ms, then cleared
    int toast_ms;
    int toast_kind;        // 0 = done (tick), 1 = note (i)
    int pin_anim;          // +1: the person in focus was just pinned, -1: unpinned. Home plays the pin going in or out after its shuffle, and clears this (in/out)
} UiHome;

// HOME_BANNER: A (or a tap) on the loaded profile's card, or on the guest's. HOME_SORT: another order was picked in the chip's menu
// (sort_mode). HOME_CLEAR: minus (or a tap on the search bar's minus mark) while the grid is narrowed by a search.
typedef enum { HOME_CHOOSE, HOME_DONE, HOME_SEARCH, HOME_NEW, HOME_ADMIN, HOME_ABOUT, HOME_PICTURE, HOME_SORT, HOME_BANNER,
               HOME_CLEAR, HOME_PIN } UiHomeEvent;
int ui_home(UiHome *h); // a UiHomeEvent, or -1 if the app was asked to quit
// Draws the home screen as it is now and remembers it as what dialogs dim (no waiting for buttons).
void ui_home_backdrop(UiHome *h);

// ---- the intro (ui_intro.c): washi laid over the black, a card the logo is made on, the wall torn off and the home screen coming in piece by piece
// A card's printing (a PNG made by the intro mockup page; `png` must stay valid) and where the logo goes on it (px from the card's middle). Each start picks one.
// Without any the app has no intro.
void ui_intro_add_card(const uint8_t *png, size_t n, float lx, float ly);
// The logo's pieces made beforehand (data/intro_logo.bin; `blob` must stay valid), so a start does not have to make them; and, for the build, that blob made the
// slow way (free it; after ui_init).
void ui_intro_set_logo(const uint8_t *blob, size_t size);
uint8_t *ui_intro_logo_blob(size_t *out_size);
void ui_intro_prepare(void); // picks this start's wall and card and starts making them (on the console's other cores, while the app loads); once is enough
// Plays it, ending on this home screen (ui_home then carries on from that very frame), if nothing else has been shown yet; any button skips it.
// 0 = it played, -1 = not this time (already played or dropped, a screen shown before it, or it could not be made).
int ui_intro_play(UiHome *h);
void ui_intro_drop(void); // the run ends without it: waits for what is being made and frees it all

// ---- dialogs (drawn over whatever is on screen, which is dimmed)
void ui_message(UiKind kind, const char *headline, const char *detail);
// B = `no`, A = `yes`. With a destructive action the yes label is red. avatar_name may be NULL.
int ui_confirm(const char *headline, const char *detail, const char *yes, const char *no, int destructive,
               const char *avatar_name, int avatar_color);
// For what can't be undone: Cancel (B) is focused and the red `yes` button (A) starts dim and fills over hold_ms; A does nothing
// until it is full. 1 = confirmed.
int ui_confirm_wait(const char *headline, const char *detail, const char *yes, const char *no, int hold_ms);
typedef struct { const char *title; const char *sub; int dim; int sw; } UiChoice; // sw: 0 an ordinary item, 1 a switch that is off, 2 a switch that is on (ui_choice_sw)
int ui_choice(const char *headline, const char *detail, const UiChoice *items, int n, int sel); // index, or -1 for B
// A list with switches in it. An item with sw != 0 is a switch: A (or a tap) asks flip(user, index, want) to set it and the list stays open; flip returns what is TRUE afterwards
// (0 off, 1 on: read back, not assumed), or -1 when it could not be done (the switch stays as it was, and says so). The other items close the list as in ui_choice and
// give their index. B (or -1 from the platform's quit) closes it with -1. With switches in it the rows read left to right (words, then the switch).
int ui_choice_sw(const char *headline, const char *detail, const UiChoice *items, int n, int sel, int (*flip)(void *user, int index, int want), void *user);
// Six digits, accepted the moment the sixth is entered. who may be NULL (shows a lock instead of an avatar). 1 = entered.
int ui_pin_entry(const char *who, int who_color, const char *prompt, const char *problem, char out[7]);
// 1 = text entered, 0 = cancelled. Uses the system keyboard unless builtin_only or it isn't available.
int ui_text_input(const char *prompt, const char *initial, char *out, size_t cap, int builtin_only);
// where a character sits on the on-screen keyboard (for tests); returns 0 if it isn't on it
int ui_keyboard_pos(char c, int *row, int *col);

// ---- profile pictures
// Hands over the packed pictures (data/avatars.bin); `data` must stay valid. Without them every avatar is a letter.
void ui_set_avatar_pack(const uint8_t *data, size_t size);
// Or the console's own pictures, read a picture at a time from `dir` ("avatar:/" is its system data mounted; a folder with chara/ in it on a PC).
// Returns how many pictures there are, 0 if there are none (then nothing changes). Replaces the packed file, so call it after ui_set_avatar_pack.
int ui_set_avatar_dir(const char *dir);
// A note on why the console's pictures couldn't be had, kept for ui_avatar_status, which says where the pictures come from (for the log).
void ui_set_avatar_note(const char *note);
const char *ui_avatar_status(void);
// The Extra icons (custom_pics.h, icons.h): PNG files at <dir>/<game>/<name>.png, shown as pictures numbered from CUSTOM_PIC_BASE. `dir` is set once the
// SD card's folder is known (<washi folder>/pictures); without one there are no such pictures.
void ui_set_icon_dir(const char *dir);
const char *ui_icon_dir(void);
// What is in that folder, scanned the first time it is asked for and then kept (NULL: nothing). rescan: look again (after a download or a delete).
const IconSet *ui_icon_set(int rescan);
// Admin, This app, Extra icons (ui_icons.c): asks GitHub which games have icons (henry-debruin/nso-icons), lists them with what is on the card, downloads the ones you
// pick into that folder, or removes them.
void ui_icons_admin(void);
// A profile's avatar as a 256x256 JPEG, the way the console wants its profile picture (malloc'd; free() it). NULL on failure.
uint8_t *ui_render_avatar_jpeg(const char *name, int look, size_t *size);
// The picker: a grid of pictures and a row of background swatches. *picture (-1 = the first letter of the name) and *swatch are
// the starting choice and, when it returns 1 (A), the new one. 0 = cancelled.
int ui_picture_picker(const char *name, int *picture, int *swatch);

// ---- About: what this is, who made it, and a QR code (the PNG from data/qr.bin, white with the code as its transparency)
void ui_set_qr(const uint8_t *png, size_t size);
void ui_set_qr_wax(const uint8_t *blob, size_t size); // the code coloured in beforehand (data/qr_wax.bin), so About does not have to
uint8_t *ui_about_wax_blob(const uint8_t *png, size_t size, size_t *out_size); // for the build: that blob, made the slow way (free it)
void ui_about(void);
// Updates (This app, Check for updates): only its looks so far (ui_update.c). g_ui_update_preview >= 0 (tests) shows that state; -1: the row does nothing yet.
extern int g_ui_update_preview;
void ui_update_preview(int state);
void ui_update(void); // the real thing: asks GitHub (lii-lac/washi), downloads, installs, restarts (Platform's online/http_get/install_update/restart)
int ui_rolled_back(char *newer, size_t cap); // 1 (once) if this start went back to this version because the newer one (newer: its version, or "") didn't get going
const char *ui_version(void); // "1.6.1": the version the Makefile gave the build (the one hbmenu shows)

// ---- settings-style list page
enum { ITEM_NORMAL = 0, ITEM_DANGER, ITEM_DIM };
typedef struct {
    const char *label;
    const char *detail;       // right side, quiet
    int style;                // ITEM_*
    const char *badge;        // a small pill after the label
    int toggle;               // -1 none, 0 off, 1 on
    const char *avatar_name;  // NULL = none
    int avatar_color;         // an avatar look (swatch, or SKIN_LOOK(swatch, picture))
    int chevron;              // draw a >
    int swatch;               // -1 none, else an avatar colour index shown as a dot before the detail
    const char *sub;          // a second, quiet line under the label (NULL = none)
    int has_bar;              // a bar under the label instead: `bar` (0..1) of it filled in bar_rgb (0xRRGGBB)
    float bar;
    uint32_t bar_rgb;
} UiItem;

typedef struct {
    const char *title;
    const char *subtitle;
    const char *right;                 // top-bar text on the right
    UiItem *items;
    int n;
    int sel, scroll;                   // in/out
    const Prompt *prompts;             // footer; NULL = "A Choose, B Back"
    int nprompts;
    uint64_t extra;                    // extra buttons that end the call
    const char *empty;                 // shown when n == 0
    void (*draw_header)(Cx *c, void *user, float y); // optional block between the title and the list
    void *user;
    float header_h;
    float row_h;                       // 0 = default
} UiList;
// the chosen index (A), or -1 (B, or the app was asked to quit); *pressed is the button that ended it
int ui_list(UiList *l, uint64_t *pressed);
// A page that only says something is being worked out (reading the SD card, say). It is shown and the call returns at once: the
// caller does the slow thing, then opens the page that takes its place.
void ui_busy(const char *title, const char *note);
void ui_wait(int ms); // keeps what is on screen for ms (presses meanwhile are dropped): for waiting on something outside the app

// ---- storage: the SD card and what can be done about it on the left, whose saves use the most on the right (built like the hub)
typedef struct {
    const char *name;
    int look;      // an avatar look
    uint64_t bytes;
    uint32_t tint; // 0xRRGGBB, the profile's own colour (profile_tint): its bar, and its part of the SD card's bar
} UiStoragePerson;
typedef struct {
    int have_space;
    uint64_t free_b, total_b;
    uint64_t other_b;               // saves that are nobody's now (deleted profiles, unclaimed backups): grey in the bar
    const UiStoragePerson *people;  // every profile, the largest first
    int npeople, shown;             // shown: how many have a row of their own; the rest share one "N others" row after them
    const UiItem *actions;          // the rows under the SD card
    int nactions;
    int sel, act, in_actions;       // in/out: the profile row (`shown` = the others' row), the action row, and which card has focus
} UiStorage;
// 1 = a row was chosen (in_actions says which card, then sel or act which row), 0 = back (B), or the app was asked to quit
int ui_storage(UiStorage *s);
void ui_storage_card_rect(const UiStorage *s, float r[4]); // where the profiles' card is

// ---- Backups: the ones made by hand as pinned tiles (and "Back up now" first), the automatic ones as rows under them
typedef struct {
    char when[40];     // "Today, 5:20 PM"
    char what[64];     // automatic ones: "After Alex signed out"
    char size[24];
    int manual;        // made by hand (the items are given with these first)
    int nprofiles;
    int npeople;       // pictures to show (0..4)
    char name[4][48];
    int look[4];
} UiBackup;
typedef struct {
    const UiBackup *items;
    int n;
    int sel;   // in/out: 0 = Back up now, 1 + i = items[i]
    int first; // in/out: how far the row of tiles has slid (the tile in its left slot), so the page comes back as it was left
} UiBackups;
// 1 = something was chosen (A; sel says what), 2 = X on a backup (sel is its item: delete it), 0 = back (B), or the app was asked to quit
int ui_backups(UiBackups *b);
// a backup folder's name ("10-05-2026--14-30") as "Today, 2:30 PM", "Yesterday, ..." or "Oct 5, 2:30 PM"
void ui_format_stamp_when(const char *stamp, time_t now, char *out, size_t n);

// The next screen grows out of this rectangle of the one on screen now (a card opening into a page of its own), or shrinks back
// into it on its way out: for the next screen only, in place of the usual crossfade.
void ui_next_grows_from(float x, float y, float w, float h);
void ui_next_shrinks_into(float x, float y, float w, float h);

// ---- two-pane hub: sections on the left, the rows of the selected one on the right (like the console's System Settings)
typedef struct { const char *label, *detail; int icon; } UiHubRow;   // detail may be NULL; icon: SKIN_ICON_*
typedef struct {
    const char *label;
    int icon, hue;                     // SKIN_ICON_*, SKIN_HUE_* (its rows take the same colour)
    const UiHubRow *rows;
    int nrows;
    const char *blurb;                 // one quiet line under the rows (may be NULL)
} UiHubSection;
typedef struct {
    const char *title, *summary;       // summary: small and quiet, after the title (may be NULL)
    const UiHubSection *sections;
    int nsections;
    int sel_section, sel_row, in_rows; // in/out; in_rows: the focus is on the rows, not on the sections
} UiHub;
// 1 = a row was chosen (sel_section and sel_row say which); 0 = back (B on the sections), or the app was asked to quit
int ui_hub(UiHub *h);

// ---- wheels in a dialog, like the iPhone's timer: each goes round through its values
typedef struct {
    const char *const *values;
    int n, sel;                  // sel: in/out
    const char *unit, *unit_one; // after the chosen value ("hours"; "hour" when the value is "1"); may be NULL
} UiWheel;
// 1 = OK (each sel set), 0 = cancelled (B, or the app was asked to quit), 2 = the extra button (Y, labelled `extra`; NULL = none)
int ui_wheels(const char *headline, const char *detail, UiWheel *w, int nw, const char *extra);

void ui_text_viewer(const char *title, char **lines, int n, int start_at_end);
// word-wrap body text to what the viewer can show; returns the number of lines (each at most 159 bytes)
int ui_wrap_body(const char *text, float max_width, char lines[][160], int max_lines);

// ---- progress
void ui_progress_begin(const char *title, const char *const *steps, int nsteps);
void ui_progress_step(int step); // 0-based; earlier steps show as done
void ui_progress_title(const char *title);
// the byte sizes of the games the current step will report, in order (n 0: unknown); applied when `serial` is not the one last applied,
// so it is fine to call on every report. The bar gives each game a piece of tape as wide as its share of the bytes.
void ui_progress_plan(unsigned serial, const uint64_t *bytes, size_t n);
void ui_progress_update(const char *game, uint64_t title_id, uint64_t cur, uint64_t total);
void ui_progress_end(void);

#endif
