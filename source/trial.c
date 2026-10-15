// An update's trial and going back to the version kept from before (trial.h). Plain stdio, so the PC tests run it as the console does.
#include "trial.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

enum { TRIAL_TRIES = 2 }; // starts that never got going before the kept version is put back

static void with(char *out, size_t cap, const char *self, const char *ext) { snprintf(out, cap, "%s%s", self, ext); }

static int exists(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0;
}

static int read_count(const char *p)
{
    FILE *f = fopen(p, "r");
    if (!f) return -1;
    int n = 0;
    if (fscanf(f, "%d", &n) != 1 || n < 0) n = 0;
    fclose(f);
    return n;
}

static void write_text(const char *p, const char *s)
{
    FILE *f = fopen(p, "w");
    if (!f) return;
    fputs(s, f);
    fclose(f);
}

static uint32_t rd32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }

// The NRO: "NRO0" at 0x10, its size at 0x18; after it the assets ("ASET", then the icon's, the NACP's and the RomFS's offset and size, 64 bits each, from the
// assets' start). The NACP's display version is 16 bytes at 0x3060.
int nro_version(const char *path, char *ver, size_t cap)
{
    if (cap) ver[0] = 0;
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    uint8_t h[0x20], a[0x38];
    char v[17] = {0};
    int ok = fread(h, 1, sizeof h, f) == sizeof h && !memcmp(h + 0x10, "NRO0", 4);
    uint32_t size = ok ? rd32(h + 0x18) : 0;
    ok = ok && fseek(f, (long)size, SEEK_SET) == 0 && fread(a, 1, sizeof a, f) == sizeof a && !memcmp(a, "ASET", 4);
    uint32_t nacp = ok ? rd32(a + 0x18) : 0, nacp_size = ok ? rd32(a + 0x20) : 0; // (the low halves: an NRO is far smaller than 4 GB)
    ok = ok && nacp_size >= 0x3070 && fseek(f, (long)size + (long)nacp + 0x3060, SEEK_SET) == 0 && fread(v, 1, 16, f) == 16;
    fclose(f);
    if (!ok || !v[0]) return -1;
    snprintf(ver, cap, "%s", v);
    return 0;
}

int trial_swap(const char *self)
{
    char bak[600], old[600], trial[600];
    with(bak, sizeof bak, self, ".bak");
    with(old, sizeof old, self, ".old");
    with(trial, sizeof trial, self, ".trial");
    if (!exists(bak)) return -1;
    remove(old);
    if (rename(self, old) != 0) return -1;
    if (rename(bak, self) != 0) {
        rename(old, self); // (put it back as it was)
        return -1;
    }
    rename(old, bak); // (if this one fails, it stays as .old: nothing is lost)
    remove(trial);
    return 0;
}

int trial_start(const char *self)
{
    char trial[600], note[600], ver[32];
    with(trial, sizeof trial, self, ".trial");
    int n = read_count(trial);
    if (n < 0) return 0; // (no trial: the usual start)
    if (n < TRIAL_TRIES) {
        char s[16];
        snprintf(s, sizeof s, "%d\n", n + 1);
        write_text(trial, s);
        return 0;
    }
    nro_version(self, ver, sizeof ver); // (the one that would not start, for the note)
    if (trial_swap(self) != 0) { // nothing to go back to: carry on with it
        remove(trial);
        return 0;
    }
    with(note, sizeof note, self, ".rolledback");
    write_text(note, ver);
    return 1;
}

void trial_begin(const char *self)
{
    char trial[600];
    with(trial, sizeof trial, self, ".trial");
    write_text(trial, "0\n");
}

void trial_ok(const char *self)
{
    char trial[600];
    with(trial, sizeof trial, self, ".trial");
    remove(trial);
}

int trial_kept(const char *self, char *ver, size_t cap)
{
    char bak[600];
    with(bak, sizeof bak, self, ".bak");
    if (cap) ver[0] = 0;
    if (!exists(bak)) return 0;
    nro_version(bak, ver, cap);
    return 1;
}

int trial_note(const char *self, char *ver, size_t cap)
{
    char note[600];
    with(note, sizeof note, self, ".rolledback");
    if (cap) ver[0] = 0;
    FILE *f = fopen(note, "r");
    if (!f) return 0;
    if (cap && fgets(ver, (int)cap, f)) ver[strcspn(ver, "\r\n")] = 0;
    fclose(f);
    remove(note);
    return 1;
}
