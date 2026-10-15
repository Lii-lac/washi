#include "nacp_name.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx.h"

#define TITLES_DATA_FORMAT 0x3215
#define ENTRY_SIZE 0x300 // name[0x200] + author[0x100]
#define NAME_BYTES 0x200

int nacp_names_compressed(const uint8_t *nacp)
{
    return nacp[TITLES_DATA_FORMAT] == 1;
}

// valid UTF-8 with no control characters (a name read from the wrong place is usually neither)
static int plausible(const uint8_t *s)
{
    for (; *s; s++) {
        if (*s < 0x20 || *s == 0x7F) return 0;
        if (*s < 0x80) continue;
        int extra = (*s & 0xE0) == 0xC0 ? 1 : (*s & 0xF0) == 0xE0 ? 2 : (*s & 0xF8) == 0xF0 ? 3 : -1;
        if (extra < 0) return 0;
        for (int k = 1; k <= extra; k++)
            if ((s[k] & 0xC0) != 0x80) return 0;
        s += extra;
    }
    return 1;
}

static int pick(const uint8_t *entries, size_t count, char *out, size_t cap)
{
    static const int prefer[2] = {0, 1}; // American English, British English
    for (size_t k = 0; k < 2 + count; k++) {
        size_t i = k < 2 ? (size_t)prefer[k] : k - 2;
        if (i >= count) continue;
        const uint8_t *name = entries + i * ENTRY_SIZE;
        if (name[0] && memchr(name, 0, NAME_BYTES) && plausible(name)) { // present, ends inside its field, and is text
            snprintf(out, cap, "%s", (const char *)name);
            return 0;
        }
    }
    return -1;
}

int nacp_name(const uint8_t *nacp, char *out, size_t cap)
{
    out[0] = 0;
    if (!nacp_names_compressed(nacp)) return pick(nacp, 16, out, cap);
    unsigned csize = nacp[0] | (unsigned)nacp[1] << 8;
    if (csize == 0 || csize > 0x2FFE) return -1;
    size_t n = 0;
    uint8_t *dec = gfx_inflate_raw(nacp + 2, csize, &n);
    if (!dec) return -1;
    int rc = pick(dec, n / ENTRY_SIZE, out, cap);
    free(dec);
    return rc;
}
