// Profile pictures that are not the console's: PNG files on the SD card at <washi folder>/pictures/<game>/<name>.png (the Extra icons, see icons.h).
// A picture is named by its key, "<game>/<name>", which is what a profile saves in its file. The interface works with small numbers, so each key
// that is seen gets one for as long as the app runs (the numbers start at CUSTOM_PIC_BASE, above every picture the console has). Pure C.
#ifndef CUSTOM_PICS_H
#define CUSTOM_PICS_H

#define CUSTOM_PIC_BASE 65536 // the same as SKIN_CUSTOM_BASE in gfx/skin.h
#define CUSTOM_KEY_MAX 128    // a key's bytes, with its NUL

// "<game>/<name>": two parts of letters, digits, '.', '_' and '-' (not starting with '.', up to 63 bytes each), so a key can never leave the folder it is in.
int custom_key_valid(const char *key);
// The number for a key (it gets one if it has none). -1 if the key isn't valid or too many are in use.
int custom_id(const char *key);
// The key a number stands for; NULL if it stands for none.
const char *custom_key(int id);
static inline int custom_is(int id) { return id >= CUSTOM_PIC_BASE; }
// Forgets every number (the tests start from nothing).
void custom_reset(void);

#endif
