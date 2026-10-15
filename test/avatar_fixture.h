// Pretend console pictures for the tests: a folder with chara/table.bin and chara/<id>.szs made here, with a small Yaz0 packer of its own. The pictures are
// stripes whose colour depends on the picture number (no Nintendo artwork anywhere), so a test can tell which picture it got and where.
#ifndef AVATAR_FIXTURE_H
#define AVATAR_FIXTURE_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// The Yaz0 form of src[0..n) into out (which needs n + n/8 + 32 bytes); returns its size. Greedy: the longest match in the last 4096 bytes.
static size_t fx_yaz0_pack(const uint8_t *src, size_t n, uint8_t *out)
{
    memcpy(out, "Yaz0", 4);
    out[4] = (uint8_t)(n >> 24), out[5] = (uint8_t)(n >> 16), out[6] = (uint8_t)(n >> 8), out[7] = (uint8_t)n;
    memset(out + 8, 0, 8);
    size_t o = 16, i = 0;
    while (i < n) {
        size_t code_at = o++;
        uint8_t code = 0;
        for (int bit = 0; bit < 8 && i < n; bit++) {
            size_t best = 0, best_d = 0, max_d = i < 4096 ? i : 4096, max_l = n - i < 273 ? n - i : 273;
            for (size_t d = 1; d <= max_d && best < max_l; d++) {
                size_t l = 0;
                while (l < max_l && src[i + l] == src[i + l - d]) l++;
                if (l > best) best = l, best_d = d;
            }
            if (best >= 3) {
                size_t dd = best_d - 1;
                if (best < 18) {
                    out[o++] = (uint8_t)((best - 2) << 4 | dd >> 8);
                    out[o++] = (uint8_t)dd;
                } else {
                    out[o++] = (uint8_t)(dd >> 8);
                    out[o++] = (uint8_t)dd;
                    out[o++] = (uint8_t)(best - 0x12);
                }
                i += best;
            } else {
                code |= (uint8_t)(0x80 >> bit);
                out[o++] = src[i++];
            }
        }
        out[code_at] = code;
    }
    return o;
}

// picture number k, pixel (x, y): colour stripes 8 rows tall, transparent left and right edges
static void fx_pixel(int k, int x, int y, uint8_t px[4])
{
    int c = (y / 8) * 7 + k * 40;
    px[0] = (uint8_t)(c * 3), px[1] = (uint8_t)(c * 5), px[2] = (uint8_t)(k * 20 + 10);
    px[3] = x < 16 || x >= 240 ? 0 : 255;
}

static uint8_t *fx_image(int k)
{
    uint8_t *im = malloc(256 * 256 * 4);
    for (int y = 0; y < 256; y++)
        for (int x = 0; x < 256; x++) fx_pixel(k, x, y, im + ((size_t)y * 256 + x) * 4);
    return im;
}

// a fresh scratch folder out/avatar/<name>/ with chara/ in it; `dir` gets its path with a trailing '/'
static void fx_dir(char *dir, size_t cap, const char *name)
{
    char p[300];
    mkdir("out", 0777);
    mkdir("out/avatar", 0777);
    snprintf(p, sizeof p, "out/avatar/%s", name);
    mkdir(p, 0777);
    snprintf(p, sizeof p, "out/avatar/%s/chara", name);
    mkdir(p, 0777);
    snprintf(dir, cap, "out/avatar/%s/", name);
}

static void fx_write(const char *path, const void *data, size_t n)
{
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite(data, 1, n, f);
    fclose(f);
}

// chara/table.bin: the ids as 32-bit little-endian numbers
static void fx_table(const char *dir, const uint32_t *ids, int n)
{
    char p[400];
    uint8_t *b = malloc((size_t)n * 4 + 1);
    for (int i = 0; i < n; i++) b[4 * i] = (uint8_t)ids[i], b[4 * i + 1] = (uint8_t)(ids[i] >> 8), b[4 * i + 2] = (uint8_t)(ids[i] >> 16), b[4 * i + 3] = (uint8_t)(ids[i] >> 24);
    snprintf(p, sizeof p, "%schara/table.bin", dir);
    fx_write(p, b, (size_t)n * 4);
    free(b);
}

// chara/<id>.szs: picture number id-1
static void fx_picture(const char *dir, int id)
{
    char p[400];
    uint8_t *im = fx_image(id - 1), *out = malloc(256 * 256 * 4 + 256 * 256 / 2 + 64);
    size_t n = fx_yaz0_pack(im, 256 * 256 * 4, out);
    snprintf(p, sizeof p, "%schara/%08X.szs", dir, (unsigned)id);
    fx_write(p, out, n);
    free(im);
    free(out);
}

#endif
