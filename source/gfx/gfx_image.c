// Game icons: the console hands them over as JPEG; decode and shrink them once.
// Profile pictures: PNG with transparency (avatars.bin).
#include <stdlib.h>
#include <string.h>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_NO_LINEAR
#define STBI_NO_HDR
#define STBI_NO_FAILURE_STRINGS
#include "stb_image.h"

#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STB_IMAGE_WRITE_STATIC // the PC tools have their own copy (PNG output); this one only encodes JPEG
// (not STBI_WRITE_NO_STDIO: with it the library declares its HDR writer but leaves it out, which is a warning of its own)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function" // (only its JPEG writer is used; the library's other writers stay unused)
#include "stb_image_write.h"
#pragma GCC diagnostic pop

#include "gfx.h"

typedef struct {
    uint8_t *p;
    size_t n, cap;
} JBuf;

static void jpg_chunk(void *ctx, void *data, int size)
{
    JBuf *b = ctx;
    if (!b->p && b->cap == (size_t)-1) return; // an earlier chunk didn't fit: give up
    if (b->n + (size_t)size > b->cap) {
        size_t cap = (b->n + (size_t)size) * 2;
        uint8_t *q = realloc(b->p, cap);
        if (!q) {
            free(b->p);
            b->p = NULL;
            b->cap = (size_t)-1;
            return;
        }
        b->p = q;
        b->cap = cap;
    }
    memcpy(b->p + b->n, data, (size_t)size);
    b->n += (size_t)size;
}

// Raw DEFLATE (no zlib header) to bytes; malloc'd, free() it. NULL on bad data.
uint8_t *gfx_inflate_raw(const uint8_t *data, size_t n, size_t *out_size)
{
    int outlen = 0;
    char *p = stbi_zlib_decode_noheader_malloc((const char *)data, (int)n, &outlen);
    if (!p || outlen < 0) {
        free(p);
        return NULL;
    }
    *out_size = (size_t)outlen;
    return (uint8_t *)p;
}

uint8_t *gfx_encode_jpeg(const Surf *s, int quality, size_t *size)
{
    JBuf b = {0, 0, 0};
    if (!stbi_write_jpg_to_func(jpg_chunk, &b, s->w, s->h, 4, s->px, quality) || !b.p) {
        free(b.p);
        return NULL;
    }
    *size = b.n;
    return b.p;
}

uint8_t *gfx_decode_icon(const uint8_t *jpeg, size_t n, int out_size)
{
    int w = 0, h = 0, comp = 0;
    uint8_t *src = stbi_load_from_memory(jpeg, (int)n, &w, &h, &comp, 4);
    if (!src || w < 8 || h < 8) {
        if (src) stbi_image_free(src);
        return NULL;
    }
    uint8_t *out = malloc((size_t)out_size * out_size * 4);
    if (!out) {
        stbi_image_free(src);
        return NULL;
    }
    // area average: every output pixel is the mean of the source box it covers
    for (int oy = 0; oy < out_size; oy++) {
        int sy0 = oy * h / out_size, sy1 = (oy + 1) * h / out_size;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (int ox = 0; ox < out_size; ox++) {
            int sx0 = ox * w / out_size, sx1 = (ox + 1) * w / out_size;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            unsigned acc[4] = {0, 0, 0, 0}, cnt = 0;
            for (int yy = sy0; yy < sy1 && yy < h; yy++) {
                for (int xx = sx0; xx < sx1 && xx < w; xx++) {
                    const uint8_t *p = src + ((size_t)yy * w + xx) * 4;
                    acc[0] += p[0]; acc[1] += p[1]; acc[2] += p[2]; acc[3] += p[3];
                    cnt++;
                }
            }
            uint8_t *o = out + ((size_t)oy * out_size + ox) * 4;
            for (int c = 0; c < 4; c++) o[c] = (uint8_t)(cnt ? acc[c] / cnt : 0);
        }
    }
    stbi_image_free(src);
    return out;
}

// A PNG as RGBA (malloc'd; free() it), or NULL. The size comes back in *w and *h.
uint8_t *gfx_decode_png(const uint8_t *png, size_t n, int *w, int *h)
{
    int comp = 0;
    uint8_t *src = stbi_load_from_memory(png, (int)n, w, h, &comp, 4);
    if (!src) return NULL;
    size_t bytes = (size_t)*w * *h * 4;
    uint8_t *out = malloc(bytes);
    if (out) memcpy(out, src, bytes);
    stbi_image_free(src);
    return out;
}
