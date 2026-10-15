// The Washi "skin": drawing helpers on top of gfx.c.
//
// Washi (see paper.h): paper cards on a cream pinboard, washi tape, a strip of paper along the bottom with the button prompts on it.
// Screens that have not been restyled yet still draw the old cyan focus frame on the cream theme. Rounded type (M PLUS Rounded 1c).
// Used by the real app (ui.c) and by the mockup screens (gfx/scenes.c), so they can never drift apart.
#ifndef SKIN_H
#define SKIN_H

#include "gfx.h"
#include "paper.h"

typedef struct {
    int dark;
    Col bg_top, bg_bot, glow;
    Col panel, panel_alt, line;
    Col text, text2, text3;
    Col accent, accent_text, accent_soft, danger, warn;
    Col scrim, shadow;
    Col sel;   // inside the focus frame (the console's dark theme darkens the item it is on; in the light one it stays the card)
    Col plate; // under the item that stays chosen while the focus is elsewhere (the hub's section)
    Col rule;  // the footer's line
} Theme;

Theme theme_make(int dark);

typedef struct {
    Font *reg, *med, *semi;
    Theme th;
} Skin;

// Loads the embedded fonts (once) and builds the theme. `fallback` (optional, e.g. the console's own font) is used for glyphs the
// embedded fonts lack.
void skin_init(Skin *s, int dark);
void skin_set_fallback(Font *fallback);

typedef struct { Skin *skin; Surf *s; } Cx;

// ---- text
void skin_txt(Cx *c, Font *f, float px, float tracking, float x, float baseline, int align, Col k, const char *str);
void skin_txtc(Cx *c, Font *f, float px, float tracking, float x, float cy, int align, Col k, const char *str); // vertically centred

// ---- surfaces
// Ink on the board (the stamps that missed their paper): skin_board draws it. skin_ink_add takes the sprite; (x, y) is where its anchor goes.
void skin_ink_add(PaperSpr *spr, int x, int y);
void skin_ink_clear(void);
extern unsigned skin_ink_serial;
// A rubber stamp impression of word, w x h, text size px, in `ink` (the palette's PAL(ink, PAPER_INK) on its own paper), middle at its anchor.
// Free it with paper_spr_free.
PaperSpr *skin_stamp(Cx *c, int w, int h, float size, const char *word, uint32_t seed, uint32_t ink);
void skin_board(Cx *c);      // the pinboard (paper_board_fill, made once and copied): what every screen starts from
void skin_background(Cx *c); // the same
void skin_card(Cx *c, float x, float y, float w, float h, float r, float lift); // lift: 0 resting, 1 raised, 2 dialog
void skin_key_card(Cx *c, float x, float y, float w, float h, float r);
void skin_hairline(Cx *c, float x, float y, float w);
void skin_vline(Cx *c, float x, float y, float h);
void skin_card_raised(Cx *c, float x, float y, float w, float h, float r, float lift); // a tile lifted above its resting card
void skin_focus(Cx *c, float x, float y, float w, float h, float r); // cyan frame hugging the rect, with glow
void skin_focus_a(Cx *c, float x, float y, float w, float h, float r, float k); // the same, k (0..1) as strong
// what goes inside the frame: that rect taken from `sel` (the screen drawn with its items on the theme's `sel` colour), k as strong
void skin_focus_fill(Cx *c, const Surf *sel, float x, float y, float w, float h, float r, float k);
void skin_focus_fill_off(Cx *c, const Surf *sel, float x, float y, float w, float h, float r, float k, int dx, int dy); // moved by dx, dy
// Puts the screen's ground back over a rounded rectangle (and a pixel around it), wiping out what was drawn there: for moving a
// card that sits on the background.
void skin_erase(Cx *c, float x, float y, float w, float h, float r);
void skin_card_shadow(Cx *c, float x, float y, float w, float h, float r, float lift, float k); // just the shadow of skin_card, k as strong
void skin_dim(Cx *c);
void skin_dialog(Cx *c, float x, float y, float w, float h);

// ---- avatars: flat discs in six friendly colours, handed out in turn when a profile is made
#include "swatches.h"
#define SKIN_COLORS SWATCH_COUNT // the background swatches: the console's own 72
// An avatar "look" packs the swatch and the picture into one int: swatch in the low byte, picture index + 1 above it (0 = none).
#define SKIN_LOOK(swatch, picture) (((swatch) & 0xFF) | (((picture) + 1) << 8))
#define SKIN_LOOK_SWATCH(look) ((look) & 0xFF)
#define SKIN_LOOK_PICTURE(look) (((look) >> 8) - 1)
// The profile pictures, packed by gfx/tools/pack_avatars.py. `data` must stay valid. (Also goes back to letters when there is nothing to show.)
void skin_set_avatar_pack(const uint8_t *data, size_t size);
// Or from somewhere else, a picture at a time: the console's own (avatar_src.c, through ui_set_avatar_dir). Each picture is 256 x 256 RGBA.
typedef struct {
    int (*count)(void *ctx);              // the number of pictures: numbers 0 .. count-1
    int (*at)(void *ctx, int pos);        // the picture number at grid position `pos` (the console's order), -1 past the end
    uint8_t *(*load)(void *ctx, int pic); // picture `pic` (malloc'd, which skin frees), or NULL when it can't be had
} SkinPictureSource;
void skin_set_avatar_source(const SkinPictureSource *src, void *ctx); // replaces the packed pictures; `ctx` must stay valid
// Pictures numbered SKIN_CUSTOM_BASE and up (custom_pics.h: the Extra icons on the SD card) are not the console's: each is loaded by `load` as 256 x 256 RGBA
// (malloc'd, which skin frees; NULL = a letter instead). Setting it again forgets the pictures already decoded.
#define SKIN_CUSTOM_BASE 65536
void skin_set_custom_source(uint8_t *(*load)(void *ctx, int id), void *ctx);
int skin_avatar_count(void);
// The grid shows the pictures grouped by series; a profile stores a picture's id (its number in the pack), which never changes.
int skin_avatar_at(int pos);  // the id of the picture at grid position pos
int skin_avatar_pos(int id);   // the grid position of picture id
// decoded RGBA of picture i (cached; valid until a few more pictures are asked for), or NULL
const uint8_t *skin_avatar_rgba(int i, int *w, int *h);
// the same, from the smallest copy that is still at least `size` pixels wide (for drawing it that big)
const uint8_t *skin_avatar_rgba_fit(int i, float size, int *w, int *h);
// one of the console's pictures (not a custom one) decoded fresh, malloc'd (free() it), without the cache: safe from another thread
uint8_t *skin_avatar_load_rgba(int i, int *w, int *h);
Col skin_avatar_color(int index);
// colour_index is a plain swatch, or a look (SKIN_LOOK) with a picture
void skin_avatar(Cx *c, float cx, float cy, float rad, const char *name, int color_index);
// The avatar the way the console shows a profile picture: a square of the swatch with the picture (or the first letter) on it.
void skin_avatar_square(Cx *c, float x, float y, float size, const char *name, int look, float radius);
// a rounded square with the game's initials (until a real icon is drawn over it)
void skin_game_monogram(Cx *c, float x, float y, float size, const char *name, int color_index);

// ---- icons
void skin_icon_lock(Cx *c, float cx, float cy, float sz, Col k);
void skin_icon_check(Cx *c, float cx, float cy, float sz, float w, Col k);
void skin_icon_info(Cx *c, float cx, float cy, float sz, Col k);
void skin_icon_alert(Cx *c, float cx, float cy, float sz, Col k); // "!" mark
void skin_icon_chev(Cx *c, float cx, float cy, float sz, float w, Col k);
void skin_icon_person(Cx *c, float cx, float cy, float sz, Col k);
void skin_icon_battery(Cx *c, float x, float cy, float level01, Col k);
void skin_icon_plus(Cx *c, float cx, float cy, float sz, float w, Col k);
// The admin pages' icons: an outline glyph on a soft rounded tile of its section's colour, size x size at (x, y).
enum { SKIN_ICON_PERSON, SKIN_ICON_GROUP, SKIN_ICON_TRASH, SKIN_ICON_LOCK, SKIN_ICON_GAMES, SKIN_ICON_DRIVE, SKIN_ICON_LOG,
       SKIN_ICON_CONSOLE, SKIN_ICON_CLOCK, SKIN_ICON_CONTRAST, SKIN_ICON_INFO, SKIN_ICON_CART, SKIN_ICON_INBOX, SKIN_ICON_UPDATE, SKIN_ICON_COUNT };
enum { SKIN_HUE_BLUE, SKIN_HUE_ORANGE, SKIN_HUE_GREEN, SKIN_HUE_PURPLE, SKIN_HUE_COUNT };
void skin_admin_icon(Cx *c, float x, float y, float size, int kind, int hue);
// just the glyph, in `ink`, centred at (cx, cy) in a box of `size` (for icons on a paper disc)
void skin_admin_glyph(Cx *c, float cx, float cy, float size, int kind, Col ink);

// ---- button prompts: the round letters from the Switch's footers
typedef struct { const char *g, *label; } Prompt;
#define G_MINUS "\xE2\x88\x92"
float skin_glyph_w(const char *g);
void skin_glyph(Cx *c, float x, float cy, const char *g, Col ink);
float skin_prompt_w(Cx *c, const Prompt *p, float px);
void skin_footer(Cx *c, const Prompt *p, int n);
// The prompts of the footer drawn last are buttons for the touch screen: which glyph (e.g. "A") is at this spot, or NULL.
const char *skin_footer_glyph_at(float x, float y);
void skin_footer_forget(void); // a dialog is covering the screen: nothing in the footer is touchable

// ---- chrome
typedef struct {
    char account[48]; // the console's name ("Esports Club Switch"), or the account's when the console has none; "" = none
    char clock[16];   // "2:14 PM"; "" = none
    int battery;      // percent, -1 = unknown
    int charging;
} SkinStatus;
void skin_logo(Cx *c, float x, float y, float size); // the app's icon (a game card with a person on its label), size x size
extern int g_skin_mark_tape; // tests only: 1 = the mark with the two small tapes it used to have (the user wants no tape on the stamp: 0)
extern int g_skin_no_mark; // mockups only: 1 = the top bar without the mark and the name
// The intro (ui_intro.c): the logo made on its card, S times the corner's size: the name's strip and fold without the name, the name alone (both turned as the corner's are,
// anchored at their middle) and the seal alone (anchored at its middle, in seal_ink: the corner's is PAL(mark_ink, PAPER_INK)), the strip in strip_rgb with its fold in
// fold_rgb (the corner's: PAL(title_strip, ...), PAL(title_fold, ...)); paper_spr_free them. The strip alone again (skin_logo_strip), the seal alone again
// (skin_logo_seal). The corner's own mark and title (cached, do not free). The bare board.
void skin_logo_parts(Cx *c, float S, PaperSpr **blank, PaperSpr **text, PaperSpr **seal, uint32_t seal_ink, uint32_t strip_rgb, uint32_t fold_rgb);
PaperSpr *skin_logo_strip(Cx *c, float S, uint32_t strip_rgb, uint32_t fold_rgb);
PaperSpr *skin_logo_seal(float S, uint32_t ink);
void skin_bar_logo(Cx *c, const PaperSpr **mark, const PaperSpr **title);
void skin_board_plain(Cx *c);
void skin_washi_mark(Cx *c, float cx, float cy, float scale); // the corner mark (a lifted card with a purple person), scaled about its middle
void skin_logo_mark(Cx *c, float x, float y, float h); // just the card, h tall (about 2/3 as wide), for the corner
void skin_top_bar(Cx *c, const char *right_label, const SkinStatus *st); // right_label replaces the status area when given
void skin_toast(Cx *c, const char *msg, int kind); // kind 0 = done (tick), 1 = note (i)
// One to three choices along the bottom of a dialog: hairline above, hairline between, cyan frame on the focused one.
void skin_choices(Cx *c, float x, float y, float w, float h, const Prompt *p, int n, int focus_idx, const Col *inks); // inks: NULL = default

#endif
