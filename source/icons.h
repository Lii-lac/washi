// Extra icons: the character pictures that come with Nintendo Switch Online and the games, collected (as 256 x 256 PNGs) in the public repository
// henry-debruin/nso-icons, one folder per game (<game>/characters/<name>.png). Washi never ships them: the admin's "Extra icons" page downloads the games
// you pick into <washi folder>/pictures/<game>/<name>.png, and Edit Icon's Picture page shows them as cards, game by game.
// This file is the part that needs no screen: what is on the SD card (a scan of the folder), what the repository lists (its tree, as GitHub's API
// gives it), and the addresses. Pure C (stdio, malloc, cJSON), so the host tests run it.
#ifndef ICONS_H
#define ICONS_H

#include <stddef.h>
#include <stdint.h>

#define ICONS_REPO "henry-debruin/nso-icons"

// ---- what is on the SD card
typedef struct IconSet IconSet;
// Reads <dir>/<game>/<name>.png (names that make a valid key, see custom_pics.h; only games with at least one icon). NULL if there are none.
IconSet *icons_scan(const char *dir);
void icons_free(IconSet *s);
int icons_games(const IconSet *s);                       // games, in name order
const char *icons_slug(const IconSet *s, int g);         // the folder's name, like "fire-emblem-three-houses"
int icons_count(const IconSet *s, int g);                // its icons
const char *icons_name(const IconSet *s, int g, int i);  // an icon's file name without ".png", in name order
int icons_total(const IconSet *s);
int64_t icons_added(const IconSet *s, int g);            // when something was last downloaded into the game's folder (seconds since 1970; 0 unknown)
int icons_find(const IconSet *s, const char *slug);      // the game's number, or -1

// A folder's name as a title: "fire-emblem-three-houses" -> "Fire Emblem Three Houses" (with the few words that need more: "kirbys" -> "Kirby's").
void icons_title(const char *slug, char *out, size_t n);

// ---- what the repository lists
typedef struct IconRepo IconRepo;
// The answer of GitHub's tree API (.../git/trees/HEAD?recursive=1): every <game>/characters/<name>.png in it. NULL if it isn't that or lists none.
// *truncated (may be NULL) is set when GitHub says the list was cut short.
IconRepo *icons_repo_parse(const char *json, size_t len, int *truncated);
void icons_repo_free(IconRepo *r);
int icons_repo_games(const IconRepo *r);
const char *icons_repo_slug(const IconRepo *r, int g);
int icons_repo_count(const IconRepo *r, int g);
const char *icons_repo_name(const IconRepo *r, int g, int i);
uint64_t icons_repo_bytes(const IconRepo *r, int g);     // what the game's characters come to, for "about 6 MB"
int icons_repo_find(const IconRepo *r, const char *slug);

// ---- addresses
void icons_tree_url(char *out, size_t n);
void icons_file_url(char *out, size_t n, const char *slug, const char *name);
// A downloaded file is a PNG whose header says a square between 16 and 1024 pixels: anything else is not written.
int icons_png_ok(const uint8_t *data, size_t len);

#endif
