// THE LOGIC: check out / check in of per-person saves (PLAN.md sections 1-3). Talks to the console only through
// backend.h and to the SD card through store.h/fsx.h, so test/test_locker.c runs it against plain folders.
#ifndef LOCKER_H
#define LOCKER_H

#include "backend.h"
#include "store.h"

#define LK_MAX_TITLES 1024
#define LK_MAX_WARN 12

typedef enum {
    LK_OK = 0,
    LK_NOOP,               // nothing to do (already loaded / already signed out)
    LK_ERR_BUSY_STATE,     // the journal isn't idle: run recovery first
    LK_ERR_NO_PROFILE,
    LK_ERR_NO_TARGET,      // no account chosen yet
    LK_ERR_BATTERY,
    LK_ERR_SPACE,
    LK_ERR_OPEN,           // a save couldn't be opened (a game is probably running)
    LK_ERR_IO,
    LK_ERR_VERIFY,
    LK_ERR_BACKEND,
    LK_ERR_IS_HOLDER,      // can't rename/roll back/delete/retarget while this profile's saves are loaded
} LkStatus;

typedef struct {
    uint64_t v[LK_MAX_TITLES];
    size_t n;
} TitleSet;

// what = "Backing up Smash Ultimate", cur/total in bytes for the current game
typedef void (*LkProgressFn)(void *user, const char *what, uint64_t cur, uint64_t total);

typedef struct Locker {
    Backend *be;
    Store *st;
    LkProgressFn progress;
    void *progress_user;

    char err[320];                       // human-readable reason for the last failure
    char warn[LK_MAX_WARN][200];         // things that didn't stop the swap (e.g. a game that isn't installed)
    int nwarn;
    int swap_titles;                     // games backed up in the last operation
    uint64_t swap_bytes;
    int unclaimed_saved;                 // the last backup landed in unclaimed/ (someone played without the app)

    // internals
    uint8_t *buf;
    size_t bufsz;
    uint64_t uncommitted, journal_limit;
    uint64_t cur, total;
    unsigned wipe_count;
    char label[128];
    // for the progress screen: what is happening right now
    int phase;                           // 0 checking, 1 backing up, 2 loading, 3 clearing the console
    uint64_t cur_title;                  // title id of the game being copied (0 = none)
    char cur_name[96];                   // its name
    TitleSet sys, tgt, uni;
    // for the progress bar: the byte size of each game the current phase will report, in order (plan_n 0: not known), and a number that
    // changes whenever a new plan is made. sys_bytes parallels `sys` (read while preflight has every save open; LK_NO_BYTES = nothing in it).
    uint64_t plan[LK_MAX_TITLES];
    size_t plan_n;
    unsigned plan_serial;
    uint64_t sys_bytes[LK_MAX_TITLES];
    size_t sys_bytes_n;
    struct LkManifest *man;              // while a backup of everything is copied: each file, with the CRC of what was read (the check after reads only the copy)
} Locker;
#define LK_NO_BYTES UINT64_MAX

int lk_init(Locker *lk, Backend *be, Store *st); // 0 ok
void lk_free(Locker *lk);

int lk_needs_recovery(const Locker *lk);
int lk_startup(Locker *lk);                      // heals half-finished history rotations; call once at launch
int lk_recover(Locker *lk);
int lk_swap_to(Locker *lk, const char *profile_id);
int lk_sign_out(Locker *lk);                     // backs the holder up, then clears the console's saves (guest session)
// A guest who has been playing makes a profile and keeps what they played: the console's saves are backed up into that profile
// (verified) and it becomes the loaded one. Nothing is cleared or loaded: the console already holds them. Only in a guest session
// (LK_NOOP if that profile is already loaded).
int lk_claim_guest(Locker *lk, const char *profile_id);
int lk_played_titles(Locker *lk);                // how many games have anything in their save on the console right now
int lk_rollback(Locker *lk, const char *profile_id, const char *history_name);
int lk_delete_profile(Locker *lk, const char *profile_id);
int lk_set_target(Locker *lk, const char *uid_hex);

int lk_profile_summary(Locker *lk, const char *profile_id, int *ngames, int64_t *last_used);
int lk_history_list(Locker *lk, const char *profile_id, char ***names, size_t *n); // oldest first

// Unclaimed saves: what the console held the first time anyone loaded with Washi, or what was played without it, kept in
// unclaimed/<when>. Each can be given to a profile or deleted.
#define LK_UNCLAIMED_TITLES 6
typedef struct {
    char leaf[64];                         // its folder in unclaimed/
    int64_t when;                          // when it was kept (0 = unknown)
    uint64_t bytes;
    int ngames;
    uint64_t titles[LK_UNCLAIMED_TITLES];  // the first few of its games (for their names)
} LkUnclaimed;
int lk_unclaimed_list(Locker *lk, LkUnclaimed **out, size_t *n); // the newest first; free() it; 0 ok
// Makes it the profile's saves: like a backup, the profile's current saves go into its history (Roll back has them), and its
// games that aren't in it stay as they are. Then it is gone from unclaimed/. Not while that profile is loaded (LK_ERR_IS_HOLDER).
int lk_unclaimed_give(Locker *lk, const char *leaf, const char *profile_id);
int lk_unclaimed_delete(Locker *lk, const char *leaf);
// Deletes every profile's earlier saves beyond its newest `keep` (at least 1): what a backup does for one profile, for everyone at
// once. *bytes = what that frees, *entries = how many are deleted (either may be NULL). With dry_run nothing is deleted: it only
// counts. Only ever touches profiles/<id>/history/; refuses (LK_ERR_BUSY_STATE) while a swap or sign-out is unfinished.
int lk_history_trim(Locker *lk, int keep, int dry_run, uint64_t *bytes, int *entries);
// A copy of everything Washi keeps (profiles, history, settings, the log) into <parent>/<leaf> (<leaf>-02 and so on if it is
// taken), made under <leaf>.partial and verified before it gets its name; a failed copy leaves nothing behind. *out = the folder.
int lk_snapshot(Locker *lk, const char *parent, const char *leaf, char *out, size_t outn);
// Snapshots in <parent> (newest first; free() the list). `automatic` = made by the app when somebody signed out.
#define LK_AUTO_BACKUPS_KEPT 4
typedef struct {
    char leaf[96];
    int automatic;
    char note[64];               // an automatic one: the name of whoever signed out (""= unknown)
    uint64_t bytes;
    int nprofiles;               // how many profiles it holds
    int npeople;                 // of them, how many are listed below (at most 4)
    char name[4][48];
    int color[4], picture[4];    // each one's look
} LkSnapshot;
int lk_snapshot_list(Locker *lk, const char *parent, LkSnapshot **out, size_t *n);
// 1 if no automatic snapshot was made on the day <leaf> names yet.
int lk_snapshot_auto_due(Locker *lk, const char *parent, const char *leaf);
// lk_snapshot, marked automatic, but only the first one of a day (LK_NOOP otherwise); then only the newest `keep` automatic ones
// stay (manual ones are never removed).
int lk_snapshot_auto(Locker *lk, const char *parent, const char *leaf, const char *note, int keep, char *out, size_t outn);
int lk_snapshot_delete(Locker *lk, const char *parent, const char *leaf);
// Puts a snapshot's profiles, unclaimed saves, console-profile originals and settings in place of what is kept now (who is loaded,
// the journal and the log stay). Only with nobody loaded. First a manual snapshot of what is kept now is made as <safety_leaf>; each
// part is copied and verified before anything is replaced, and a failure puts everything back.
int lk_snapshot_restore(Locker *lk, const char *parent, const char *leaf, const char *safety_leaf);
void lk_title_label(Locker *lk, uint64_t title, char *out, size_t n);
int lk_list_titles(Locker *lk, TitleSet *out);   // account saves on the target account (no exclusions applied)
// 1 if exactly one account is called "Esports Switch" (case-insensitive); fills *out.
int lk_default_account(Locker *lk, BeAccount *out);

#endif
