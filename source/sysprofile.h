// Keeps the console's own profile for the account (its name and picture, which games and the Home menu show) in step with whoever
// is loaded. Optional, and never allowed to get in the way of a swap. Platform-independent: the console is reached through backend.h.
//
// Safety: the first time anything is written, the original name and picture are saved to the SD card (and the write is refused if
// they couldn't be saved), so "restore the original" can always put the account back the way it was.
#ifndef SYSPROFILE_H
#define SYSPROFILE_H

#include <stddef.h>
#include <stdint.h>

#include "backend.h"
#include "store.h"

#define SP_NICK_BYTES 31 // what the console keeps of a nickname (32 including the end)
#define SP_IMG_MAX 0x20000 // the largest picture the console accepts

enum { SP_OK = 0, SP_NOTHING = 1, SP_ERR = -1 };

// The 0x80 bytes of user data of a profile: a u32 at 4 is the icon id (0 = a Mii, else one of the console's character icons), a
// byte at 8 is the background colour id, and 0x10 bytes at 0x10 are the Mii id (all zero for a character icon).
#define SP_UD_SIZE 0x80
#define SP_UD_ICON 4
#define SP_UD_BG 8
#define SP_UD_MII 0x10
typedef struct {
    int icon_id; // the console's id for the character icon (1..); the pictures here are the console's own, in its order
    int bg_id;   // the console's id for the background colour (0..71, as in swatches.h)
} SpIcon;

// Trims a name to what the console accepts (at most SP_NICK_BYTES, never cutting a character in half).
void sp_clean_nick(const char *in, char out[40]);
// 1 if the original name and picture have been saved
int sp_has_original(Store *st);
// Read-only check: can the console's profile be opened? Gives the current name. SP_OK or SP_ERR (with a message in err).
int sp_probe(Backend *be, char nick[40], int *icon_id, int *bg_id, char *err, size_t en); // icon_id, bg_id may be NULL
// Sets the console's profile name (nick == NULL keeps it) and picture (jpeg == NULL keeps it); icon, if given, also sets the icon id
// and background colour id the console keeps beside the picture. Saves the original first.
int sp_apply(Store *st, Backend *be, const char *nick, const uint8_t *jpeg, size_t n, const SpIcon *icon, char *err, size_t en);
// The name the account had before Washi first changed it. 1 if it was saved.
int sp_original_nick(Store *st, char out[40]);
// Puts the saved original back. SP_NOTHING if nothing was ever changed.
int sp_restore(Store *st, Backend *be, char *err, size_t en);

#endif
