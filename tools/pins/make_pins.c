// Makes data/pins.bin: the push pin's pictures for the app (see source/gfx/pins.h), rendered with pp.c. PC only; takes a minute or two.
//   make_pins <out.bin> [<sheet.png>]    the sheet shows every picture of both themes over its card colour, for a look
// The pin: solid moulded plastic (the user's pick, P2), 3.6 px a mm (its flange 36 px across), leaning a hair. Its colour is the tape's colour
// slot as dyed plastic: the light theme's as on the review page, Paper night's the night tape's hues, a little deeper.
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "paper.h"
#include "pins.h"
#include "pp.h"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

static const float PIN_H[PINS_PIN_HS] = {0, 5, 14, 34};
static const float SHADOW_H[PINS_SHADOW_HS] = {0, 1, 2, 3, 4, 5.5f, 7, 8.5f, 10, 11.5f, 13, 14, 15, 17, 20, 24, 29, 34};
// the plastic, slot by slot (pink, peach, yellow, mint, teal, blue, lilac, magenta)
static const uint32_t DYE[PINS_THEMES][PINS_COLOURS] = {
    {0xD9483F, 0xEC8B3A, 0xEFC232, 0x4FA862, 0x2FA89E, 0x3A78D6, 0x7F63C9, 0xDD5EA6},
    {0xB65C78, 0xD08E4A, 0xD6BE84, 0x4E8A68, 0x3E8A90, 0x5069B0, 0x7D6CC0, 0x93589C}, // (the dusk's rose, the window's amber, the moon's cream ...)
};

typedef struct { uint8_t *p; size_t n, cap; } Buf;
static void put(Buf *b, const void *d, size_t n)
{
    if (b->n + n > b->cap) {
        b->cap = (b->n + n) * 2;
        b->p = realloc(b->p, b->cap);
    }
    memcpy(b->p + b->n, d, n);
    b->n += n;
}
static void put_spr(Buf *b, const PaperSpr *s)
{
    uint16_t w = (uint16_t)s->w, h = (uint16_t)s->h;
    int16_t ox = (int16_t)s->ox, oy = (int16_t)s->oy;
    put(b, &w, 2);
    put(b, &h, 2);
    put(b, &ox, 2);
    put(b, &oy, 2);
    put(b, s->px, (size_t)s->w * s->h * 4);
}

// the card's paper round where a pin goes (what it reflects and what its shadow is laid over)
static uint32_t card_colour(void)
{
    PaperSpr *c = paper_card_ex(150, 164, 6, 11u, 14);
    double a[3] = {0, 0, 0};
    int n = 0;
    for (int y = 14 + 6; y < 14 + 30; y++)
        for (int x = 14 + 40; x < 14 + 110; x++, n++)
            for (int k = 0; k < 3; k++) a[k] += c->px[((size_t)y * c->w + x) * 4 + k];
    paper_spr_free(c);
    return ((uint32_t)(a[0] / n + 0.5) << 16) | ((uint32_t)(a[1] / n + 0.5) << 8) | (uint32_t)(a[2] / n + 0.5);
}

// the sheet: every picture laid in rows over the card's colour
typedef struct { Surf *s; int x, y, row_h; } Sheet;
static void sheet_put(Sheet *sh, const PaperSpr *p, uint32_t bg)
{
    if (!sh->s) return;
    if (sh->x + p->w + 4 > sh->s->w) { sh->x = 4; sh->y += sh->row_h + 4; sh->row_h = 0; }
    if (sh->y + p->h > sh->s->h) return;
    for (int y = 0; y < p->h; y++)
        for (int x = 0; x < p->w; x++) {
            uint8_t *q = sh->s->px + ((size_t)(sh->y + y) * sh->s->w + sh->x + x) * 4;
            q[0] = (bg >> 16) & 255, q[1] = (bg >> 8) & 255, q[2] = bg & 255, q[3] = 255;
        }
    paper_spr_put(sh->s, p, sh->x + p->ox, sh->y + p->oy);
    sh->x += p->w + 4;
    if (p->h > sh->row_h) sh->row_h = p->h;
}
static void sheet_row(Sheet *sh) { sh->x = 4; sh->y += sh->row_h + 10; sh->row_h = 0; }

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "make_pins <out.bin> [<sheet.png>]\n");
        return 1;
    }
    PinsHead hd;
    memset(&hd, 0, sizeof hd);
    hd.magic = PINS_MAGIC;
    hd.version = PINS_VERSION;
    hd.px_mm = 3.6f;
    hd.k = 0.42f;
    memcpy(hd.pin_h, PIN_H, sizeof PIN_H);
    memcpy(hd.shadow_h, SHADOW_H, sizeof SHADOW_H);
    Buf packed[PINS_THEMES];
    memset(packed, 0, sizeof packed);
    Sheet sh = {argc > 2 ? surf_new(1600, 1500) : NULL, 4, 4, 0};
    if (sh.s) memset(sh.s->px, 255, (size_t)sh.s->w * sh.s->h * 4);
    for (int th = 0; th < PINS_THEMES; th++) {
        paper_pal_set(th);
        const uint32_t card = card_colour();
        Buf raw;
        memset(&raw, 0, sizeof raw);
        PPLook L = {PP_GLOSS, hd.px_mm, DYE[th][0], 0, 0.03f, 0.0f, 16, card, 144, 4, th ? 1.25f : 1.0f, th ? 0x000000u : 0};
        for (int c = 0; c < PINS_COLOURS; c++) {
            L.dye = DYE[th][c];
            L.photons = 48; // (the pin's own picture only needs the card's light roughly: what its underside reflects)
            for (int i = 0; i < PINS_PIN_HS; i++) {
                PaperSpr *pin = NULL;
                pp_render(&L, PIN_H[i], &pin, NULL);
                put_spr(&raw, pin);
                sheet_put(&sh, pin, card);
                paper_spr_free(pin);
            }
            fprintf(stderr, "theme %d colour %d\n", th, c);
        }
        sheet_row(&sh);
        L.dye = DYE[th][5];
        for (int i = 0; i < PINS_SHADOW_HS; i++) {
            PaperSpr *s = NULL;
            L.photons = SHADOW_H[i] > 12 ? 96 : 160;
            pp_render(&L, SHADOW_H[i], NULL, &s);
            put_spr(&raw, s);
            sheet_put(&sh, s, card);
            paper_spr_free(s);
        }
        PaperSpr *d = pp_dent(&L);
        put_spr(&raw, d);
        sheet_put(&sh, d, card);
        paper_spr_free(d);
        sheet_row(&sh);
        int zn = 0;
        unsigned char *z = stbi_zlib_compress(raw.p, (int)raw.n, &zn, 9);
        if (!z || zn < 6) return 2;
        put(&packed[th], z + 2, (size_t)zn - 6); // (raw DEFLATE: without zlib's two-byte head and four-byte check)
        STBIW_FREE(z);
        hd.raw[th] = (uint32_t)raw.n;
        hd.packed[th] = (uint32_t)(zn - 6);
        fprintf(stderr, "theme %d: card %06X, %zu bytes, %d packed\n", th, card, raw.n, zn - 6);
        free(raw.p);
    }
    FILE *f = fopen(argv[1], "wb");
    if (!f) return 3;
    fwrite(&hd, sizeof hd, 1, f);
    for (int th = 0; th < PINS_THEMES; th++) fwrite(packed[th].p, 1, packed[th].n, f);
    fclose(f);
    if (sh.s) stbi_write_png(argv[2], sh.s->w, sh.y + sh.row_h + 4 < sh.s->h ? sh.y + sh.row_h + 4 : sh.s->h, 4, sh.s->px, sh.s->w * 4);
    return 0;
}
