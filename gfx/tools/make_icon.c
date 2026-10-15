// Draws the app icon with the same renderer as the app: 256x256 JPEG (what the Switch wants in an .nro / forwarder), plus PNGs
// to look at. Drawn at 1024 and averaged down, so the edges are clean.
//   gcc -O2 -I../../source/gfx -o make_icon make_icon.c ../../source/gfx/gfx.c -lm && ./make_icon ../../icon.jpg out
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../source/gfx/stb_image_write.h"
#include "gfx.h"

#define BIG 1024
#define PI 3.14159265f

static void person_glyph(Surf *s, float cx, float cy, float size, Col c)
{ // head and shoulders, `size` tall, centred
    float hr = size * 0.24f;
    gfx_circle(s, cx, cy - size * 0.24f, hr, c);
    gfx_rrect(s, cx - size * 0.42f, cy + size * 0.06f, size * 0.84f, size * 0.42f, size * 0.21f, c);
}

// A game card whose label is a person: "saves", one per player. Full-bleed (the console rounds the corners).
static void draw(Surf *s)
{
    gfx_rrect_grad(s, 0, 0, BIG, BIG, 0, hex(0xE9F7FD, 1), hex(0xBFE5F5, 1));
    float x = 252, y = 120, w = 520, h = 784;
    gfx_shadow(s, x, y, w, h, 64, 28, 44, hex(0x0B3550, 0.30f));
    gfx_rrect_grad(s, x, y, w, h, 64, hex(0x4B525D, 1), hex(0x2C3139, 1));
    // the label
    gfx_rrect_grad(s, x + 44, y + 56, w - 88, 470, 36, hex(0x34CBF5, 1), hex(0x0A9CD6, 1));
    person_glyph(s, 512, y + 56 + 205, 270, col(1, 1, 1, 1));
    gfx_rrect(s, x + 44 + 60, y + 56 + 380, w - 88 - 120, 26, 13, col(1, 1, 1, 0.55f));
    // the contacts
    gfx_rrect(s, x + 44, y + 566, w - 88, 150, 28, hex(0x23272E, 1));
    for (int i = 0; i < 9; i++) gfx_rrect(s, x + 76 + i * 46, y + 600, 28, 100, 6, hex(0xD9B36A, 1));
    gfx_rrect(s, x + w - 120, y + 18, 70, 14, 7, col(1, 1, 1, 0.18f));
}

int main(int argc, char **argv)
{
    const char *jpg = argc > 1 ? argv[1] : "icon.jpg";
    const char *dir = argc > 2 ? argv[2] : ".";
    Surf *big = surf_new(BIG, BIG);
    draw(big);

    enum { N = 256, K = BIG / N };
    uint8_t rgb[N * N * 3], rgba[N * N * 4];
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            int acc[3] = {0, 0, 0};
            for (int j = 0; j < K; j++)
                for (int i = 0; i < K; i++) {
                    const uint8_t *p = big->px + ((size_t)(y * K + j) * BIG + (x * K + i)) * 4;
                    for (int c = 0; c < 3; c++) acc[c] += p[c];
                }
            for (int c = 0; c < 3; c++) {
                rgb[(y * N + x) * 3 + c] = (uint8_t)((acc[c] + K * K / 2) / (K * K));
                rgba[(y * N + x) * 4 + c] = rgb[(y * N + x) * 3 + c];
            }
            rgba[(y * N + x) * 4 + 3] = 255;
        }
    stbi_write_jpg(jpg, N, N, 3, rgb, 95);
    char path[600];
    snprintf(path, sizeof path, "%s/icon-256.png", dir);
    stbi_write_png(path, N, N, 4, rgba, N * 4);
    snprintf(path, sizeof path, "%s/icon-1024.png", dir);
    stbi_write_png(path, BIG, BIG, 4, big->px, BIG * 4);
    surf_free(big);
    printf("wrote %s\n", jpg);
    return 0;
}
