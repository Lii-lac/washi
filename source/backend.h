// The only way locker.c touches the console. backend_nx.c implements it with libnx; test/backend_host.c implements it
// with plain folders so the whole swap state machine can be tested on a PC.
//
// File I/O inside a mounted save is plain stdio/dirent on `be->save_root` (the mount point). Only the operations that
// need the console (listing, mounting, creating, committing, battery, ...) go through this table.
#ifndef BACKEND_H
#define BACKEND_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>

enum {
    BE_OK = 0,
    BE_ERR = 1,                // anything else (details in be->last_rc)
    BE_ERR_OPEN = 2,           // an existing save couldn't be opened/mounted (usually: its game is running)
    BE_ERR_NOT_INSTALLED = 3,  // can't create a save: the game isn't installed
    BE_ERR_NO_ACCOUNT_SAVE = 4,// the game doesn't use per-account saves
    BE_ERR_JOURNAL = 5,        // too many uncommitted bytes (fake backend; the real console fails inside fwrite)
};

typedef struct {
    char uid[33];  // 32 hex digits
    char name[40];
} BeAccount;

typedef struct Backend Backend;
struct Backend {
    void *ctx;
    const char *save_root; // where the currently mounted save appears ("save:/" on the console)
    uint32_t last_rc;      // raw platform result code of the last failure, for the log

    // Which account's saves we manage (32 hex digits). Must be called before list_saves/mount/create.
    int (*set_target)(Backend *, const char *uid_hex);
    // Titles that have an account save for the target account, user space. Returns BE_OK.
    int (*list_saves)(Backend *, uint64_t *titles, size_t max, size_t *count);
    // Titles with a DEVICE save (shared by every account; never swapped). For the About screen.
    int (*list_device_saves)(Backend *, uint64_t *titles, size_t max, size_t *count);
    // Upper bound of the bytes held by the target account's save for `title` (free-space estimates).
    int (*save_size)(Backend *, uint64_t title, uint64_t *bytes);
    // Mount the existing save at save_root. BE_ERR_OPEN if that fails.
    int (*mount)(Backend *, uint64_t title);
    // Create an empty save sized from the game's NACP. Does not mount it.
    int (*create)(Backend *, uint64_t title);
    int (*commit)(Backend *);
    void (*unmount)(Backend *);
    // NACP journal size for the target account's save (0 if unknown).
    uint64_t (*journal_size)(Backend *, uint64_t title);
    // fwrite() into the mounted save. The backend may count/limit uncommitted bytes. BE_OK on success.
    int (*write)(Backend *, FILE *f, const void *buf, size_t n);

    int (*title_name)(Backend *, uint64_t title, char *out, size_t n); // BE_OK or BE_ERR
    // The game's icon as JPEG bytes (valid until the next backend call). BE_ERR if there is none.
    int (*title_icon)(Backend *, uint64_t title, const uint8_t **jpeg, size_t *size);
    // The target account's profile as the console shows it (the name and the picture). Optional (NULL = not supported).
    // Reads the nickname into nick (40 bytes); if user_data is not NULL also the profile's 0x80 bytes of user data (which hold the
    // icon id and background colour id); if img is not NULL also the picture's JPEG into it (cap bytes; *img_size = its length).
    int (*sys_profile_read)(Backend *, char nick[40], uint8_t *user_data, uint8_t *img, size_t cap, size_t *img_size);
    // Replaces the nickname, the user data if it is not NULL, and the picture if jpeg is not NULL. Nothing else changes.
    int (*sys_profile_store)(Backend *, const char *nick, const uint8_t *user_data, const uint8_t *jpeg, size_t n);
    int (*list_accounts)(Backend *, BeAccount *out, size_t max, size_t *count);
    int (*battery)(Backend *, int *percent, int *charging); // BE_OK or BE_ERR (unknown: swap is allowed)
    int (*free_space)(Backend *, uint64_t *bytes);           // free bytes on the SD card
    int (*total_space)(Backend *, uint64_t *bytes);          // the size of the SD card
    time_t (*now)(Backend *);
    uint32_t (*random)(Backend *);
    // Called at every point where a crash would leave the SD card in a distinct state. The real backend returns 0;
    // the test backend returns non-zero to simulate the power being cut at exactly that point.
    int (*checkpoint)(Backend *, const char *name);
};

#endif
