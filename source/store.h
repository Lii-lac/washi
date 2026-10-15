// SD layout (see PLAN.md section 4): config, journal/state, profiles, log. No libnx here, so it runs in the host tests.
#ifndef STORE_H
#define STORE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>

#include "backend.h"
#include "custom_pics.h"
#include "fsx.h"
#include "swatches.h"

#define PIN_LEN 6 // profile passwords and the admin password are exactly this many digits
#define ID_MAX 48
#define NAME_MAX_BYTES 40
#define MAX_EXCLUDED 64
#define UNCLAIMED "unclaimed"

typedef struct {
    char target_uid[33];                 // "" until chosen
    uint64_t excluded[MAX_EXCLUDED];     // titles that are never swapped
    int nexcluded;
    char admin_pin_hash[65];             // "" until set
    char admin_pin_salt[33];
    int history_keep;                    // default 5
    int builtin_keyboard;                // 1 = skip the system keyboard (applet mode may not offer it)
    int theme_mode;                      // 0 = follow the console, 1 = light, 2 = dark
    int clock24;                         // 1 = 24-hour clock in the header
    int sync_name;                       // 1 = the console's own profile for the account shows the loaded player's name
    int sync_picture;                    // 1 = ... and their picture
    int guest_picture;                   // what the console's profile shows while nobody is loaded: a picture id (-1 = a letter) ...
    int guest_swatch;                    // ... on this background (the club account's own: Princess Toadette on light blue)
    int sort_mode;                       // the order of the home grid: SORT_RECENT, SORT_NAME, SORT_NEWEST, SORT_GAMES
    int remind_minutes;                  // on opening, ask "still you?" when the loaded player has been on this long (0 = never)
    int remind_restore;                  // left by the old "the real one, in a minute" test (gone now): the usual remind_minutes, put back when
                                         // the app next opens (-1 = none; every config written now has -1)
} Config;
enum { SORT_RECENT = 0, SORT_NAME, SORT_NEWEST, SORT_GAMES, SORT_COUNT };

typedef enum { PH_IDLE = 0, PH_BACKING_UP, PH_RESTORING, PH_WIPING } Phase;

typedef struct {
    char holder[ID_MAX];                 // profile id, or UNCLAIMED
    Phase phase;
    char from[ID_MAX];                   // journal: who is being backed up (profile id or UNCLAIMED)
    char to[ID_MAX];                     // journal: who is being loaded ("" for a sign-out)
    int64_t started;
    char unclaimed_base[ID_MAX];         // after a sign-out: the profile whose backup equals what is on the account
    int intro_done;                      // "back up with JKSV first" shown
    int64_t loaded_at;                   // when the holder's saves were loaded (0 = unknown / nobody)
    int wiped;                           // nobody is loaded AND the console's saves were cleared by a sign-out: a guest session
} State;

#define PROFILE_SWATCHES SWATCH_COUNT // a profile's `color` is one of the console's 72 background colours (swatches.h)

typedef struct {
    char id[ID_MAX];
    char name[NAME_MAX_BYTES];
    char pin_hash[65];                   // "" = no password
    char salt[33];
    int64_t created;
    int64_t last_used;
    int color;                           // background swatch (0..71, see swatches.h); new profiles get SWATCH_VIVID in turn
    int picture;                         // profile picture index into the packed pictures, or -1 for the first letter of the name; an Online icon has a number
                                         // from CUSTOM_PIC_BASE up (custom_pics.h), which stands for picfile only while the app runs
    char picfile[CUSTOM_KEY_MAX];        // "" or the Online icon's key, "<game>/<name>": what the profile file keeps instead of that number
    char netid[12];                      // "" or a NetID like abc1234 (lower case): never shown, only used to find the profile
    int64_t pinned;                      // 0, or when an admin pinned it: pinned profiles come first in every order (the first pinned first), not in a search
} Profile;

typedef struct {
    char root[FSX_PATH - 256];           // e.g. "sdmc:/switch/washi"
    Backend *be;
    Config cfg;
    State state;
    char notice[160];                    // set when a damaged file had to be set aside
    char session[64];                    // this start's log file, from the root: "logs/2026-10-11_00-23-45.txt"
} Store;

// Opens (creating folders as needed) and loads config + state. A damaged file is renamed to *.corrupt-<ts> and
// replaced by defaults (the journal is written atomically, so this should never happen).
int store_open(Store *st, Backend *be, const char *root);
void store_reload(Store *st); // re-read config + state from the SD card (after a failed operation)
int store_save_config(Store *st);
int store_save_state(Store *st);

void store_path(const Store *st, char *out, size_t n, const char *rel);
void store_profile_dir(const Store *st, const char *id, char *out, size_t n);
int store_ts(Store *st, char *out, size_t n);                      // "YYYYMMDD-HHMMSS" (UTC)
int store_unique_child(const char *parent, const char *base, char *out, size_t n); // base, base-02, base-03...
void store_logf(Store *st, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
// The activity log is one file for every start, in <root>/logs (named by the start's UTC date and time); the ones older than LOG_KEEP_DAYS go when the app starts.
#define LOG_KEEP_DAYS 14
void store_logs_start(Store *st);                                // names this start's file, moves the old single log in, deletes the old ones (store_open does it)
void store_session_log(const Store *st, char *out, size_t n);    // the full path of this start's file ("" if there is none)
int store_logs_list(const Store *st, char ***names, size_t *n);  // the file names in the logs folder, the newest first (fsx_list_free them); 0 ok
int64_t store_log_started(const Store *st, const char *name);    // when that log's start was (seconds since 1970, UTC): from its name, else its file time

// Titles
int store_is_excluded(const Store *st, uint64_t title);
void store_title_dir(uint64_t title, char out[17]);
int store_parse_title(const char *name, uint64_t *title);          // 16 hex digits only

// Profiles
int store_valid_name(const char *name);
int store_valid_pin(const char *pin);                              // exactly PIN_LEN digits
int store_valid_netid(const char *s);                              // three letters and four digits, like abc1234 (any case)
void store_netid_clean(const char *in, char out[12]);              // trimmed and lower-cased; "" if it isn't a NetID
// How well a name matches what was typed in the search, ignoring case and spaces in the query: 4 = the name starts with it,
// 3 = a later word of the name does, 2 = it is somewhere in the name, 1 = its letters are in the name in that order ("arn" in
// "adrian"), 0 = no match (an empty query matches nothing).
int store_name_match(const char *name, const char *query);
int store_random_swatch(Store *st);
// Is this NetID already some other profile's? 1 and that profile (in *owner, may be NULL), else 0. except_id: a profile to skip (its own).
int store_netid_owner(Store *st, const char *netid, const char *except_id, Profile *owner);                                // a random background from the lighter half of each colour
int store_profile_create(Store *st, const char *name, const char *pin /* NULL = none */, Profile *out);
int store_profile_load(Store *st, const char *id, Profile *out);
int store_profile_save(Store *st, const Profile *p);
int store_profile_list(Store *st, Profile **out, size_t *n);       // sorted by name; free() the array
int store_profile_trash(Store *st, const char *id);                // moves to profiles/.trash/<id>-<ts>
int store_profile_has_pin(const Profile *p);

// Deleted profiles: the folders in profiles/.trash, each a whole profile as it was (saves, history, profile.json).
typedef struct {
    char leaf[ID_MAX + 24];              // the folder's name: <id>-<YYYYMMDD-HHMMSS>, or that with -02, -03...
    char id[ID_MAX];                     // the id it had and gets back ("" if the folder's name doesn't say)
    char name[ID_MAX + 24];              // from its profile.json; the folder's name when that can't be read
    int64_t deleted;                     // when it was deleted (from the folder's name, UTC); 0 = unknown
    int color, picture;
    int games;                           // games it holds saves for (shared games not counted, as on its page before)
    int restorable;                      // 0 = its profile.json is missing or damaged: listed, but it can't be put back
} TrashEntry;
int64_t store_parse_ts(const char *s);                             // "YYYYMMDD-HHMMSS" (UTC, what store_ts writes) -> seconds; 0 = not one
int store_trash_list(Store *st, TrashEntry **out, size_t *n);      // newest first; free() the array
// Moves a deleted profile back to profiles/<id> with everything it had. 0 = ok; -1 = it can't be restored (not there, damaged);
// -2 = a profile with that id exists. Nothing changes unless it returns 0. A NetID belongs to one profile: if this one's has been
// given to someone else since, the restored profile comes back without it and *netid_cleared (may be NULL) is set.
int store_profile_restore(Store *st, const char *leaf, char *id_out /* ID_MAX bytes, may be NULL */, int *netid_cleared);
// Deletes a deleted profile for good (its folder in profiles/.trash): first renamed to .gone-<leaf>, so the list never shows a
// half-deleted one, then removed. 0 = gone; -1 = no such entry, or it couldn't be removed (a leftover .gone-* is cleared by
// store_trash_tidy). Never touches anything outside profiles/.trash.
int store_trash_purge(Store *st, const char *leaf);
void store_trash_tidy(Store *st); // removes what an interrupted purge left behind
int store_profile_check_pin(const Profile *p, const char *pin);
int store_profile_set_pin(Store *st, Profile *p, const char *pin /* NULL clears */);
int store_admin_has_pin(const Store *st);
int store_admin_check_pin(const Store *st, const char *pin);
int store_admin_set_pin(Store *st, const char *pin);

#endif
