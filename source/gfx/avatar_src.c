// The console's own profile pictures (see avatar_src.h for the files).
#include "avatar_src.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
    MAX_IDS = 512,        // picture numbers 0 .. MAX_IDS-1 (the picture code's limit)
    MAX_FILE = 1 << 20,   // a picture packs to about 230 KB at most; anything near a megabyte isn't one
    MAX_TABLE = 4 * 1024, // 4 bytes an id
    MAX_PATH = 1024,
};

struct AvatarSrc {
    char *dir; // always ends in '/'
    int count; // the highest id
    int n_order;
    uint16_t order[MAX_IDS]; // grid position -> picture number
};

int yaz0_unpack(const uint8_t *src, size_t src_len, uint8_t *dst, size_t dst_len)
{
    if (!src || !dst || src_len < 16 || memcmp(src, "Yaz0", 4) != 0) return -1;
    size_t want = (size_t)src[4] << 24 | (size_t)src[5] << 16 | (size_t)src[6] << 8 | src[7];
    if (want != dst_len) return -1;
    size_t in = 16, out = 0;
    while (out < dst_len) {
        if (in >= src_len) return -1;
        unsigned code = src[in++];
        for (int bit = 0; bit < 8 && out < dst_len; bit++, code <<= 1) {
            if (code & 0x80) { // one byte as it is
                if (in >= src_len) return -1;
                dst[out++] = src[in++];
            } else { // a copy of bytes already made: how far back (1..4096) and how many (3..273)
                if (in + 1 >= src_len) return -1;
                unsigned b1 = src[in], b2 = src[in + 1];
                in += 2;
                size_t dist = ((size_t)(b1 & 0x0F) << 8 | b2) + 1, len = b1 >> 4;
                if (len == 0) {
                    if (in >= src_len) return -1;
                    len = (size_t)src[in++] + 0x12;
                } else {
                    len += 2;
                }
                if (dist > out || len > dst_len - out) return -1;
                for (size_t k = 0; k < len; k++, out++) dst[out] = dst[out - dist]; // (byte by byte: a copy may overlap what it is copying)
            }
        }
    }
    return 0;
}

// the whole file, at most `max` bytes; NULL if it isn't there or is bigger
static uint8_t *read_file(const char *path, size_t max, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    uint8_t *buf = NULL;
    if (fseek(f, 0, SEEK_END) == 0) {
        long len = ftell(f);
        if (len > 0 && (size_t)len <= max && fseek(f, 0, SEEK_SET) == 0) {
            buf = malloc((size_t)len);
            if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
                free(buf);
                buf = NULL;
            }
            if (buf) *n = (size_t)len;
        }
    }
    fclose(f);
    return buf;
}

AvatarSrc *avsrc_open(const char *dir)
{
    if (!dir || !*dir) return NULL;
    AvatarSrc *s = calloc(1, sizeof *s);
    if (!s) return NULL;
    size_t dl = strlen(dir);
    s->dir = malloc(dl + 2);
    if (!s->dir) {
        free(s);
        return NULL;
    }
    memcpy(s->dir, dir, dl);
    if (dir[dl - 1] != '/') s->dir[dl++] = '/';
    s->dir[dl] = 0;

    char path[MAX_PATH];
    size_t tn = 0;
    uint8_t *tab = NULL;
    if (snprintf(path, sizeof path, "%schara/table.bin", s->dir) < (int)sizeof path) tab = read_file(path, MAX_TABLE, &tn);
    if (tab) {
        unsigned char seen[MAX_IDS + 1] = {0};
        for (size_t i = 0; i + 4 <= tn && s->n_order < MAX_IDS; i += 4) {
            uint32_t id = tab[i] | tab[i + 1] << 8 | tab[i + 2] << 16 | (uint32_t)tab[i + 3] << 24;
            if (id < 1 || id > MAX_IDS || seen[id]) continue; // (nothing real is outside 1 .. MAX_IDS, or twice)
            seen[id] = 1;
            s->order[s->n_order++] = (uint16_t)(id - 1);
            if ((int)id > s->count) s->count = (int)id;
        }
        for (int id = 1; id <= s->count && s->n_order < MAX_IDS; id++) // (an id the table leaves out still gets a place)
            if (!seen[id]) s->order[s->n_order++] = (uint16_t)(id - 1);
        free(tab);
    }
    if (s->count <= 0) {
        avsrc_close(s);
        return NULL;
    }
    return s;
}

void avsrc_close(AvatarSrc *s)
{
    if (!s) return;
    free(s->dir);
    free(s);
}

int avsrc_count(const AvatarSrc *s)
{
    return s ? s->count : 0;
}

int avsrc_at(const AvatarSrc *s, int pos)
{
    return s && pos >= 0 && pos < s->n_order ? s->order[pos] : -1;
}

uint8_t *avsrc_load(const AvatarSrc *s, int pic)
{
    if (!s || pic < 0 || pic >= s->count) return NULL;
    char path[MAX_PATH];
    if (snprintf(path, sizeof path, "%schara/%08X.szs", s->dir, (unsigned)pic + 1) >= (int)sizeof path) return NULL;
    size_t n = 0;
    uint8_t *packed = read_file(path, MAX_FILE, &n);
    if (!packed) return NULL;
    size_t bytes = (size_t)AVSRC_SIZE * AVSRC_SIZE * 4;
    uint8_t *px = malloc(bytes);
    if (px && yaz0_unpack(packed, n, px, bytes) != 0) {
        free(px);
        px = NULL;
    }
    free(packed);
    return px;
}
