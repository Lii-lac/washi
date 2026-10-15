// Icon drafts in different directions, drawn with the app's renderer: 256x256 PNGs (and JPGs) in the output folder.
//   gcc -O2 -I../../source/gfx -o icon_drafts icon_drafts.c ../../source/gfx/gfx.c -lm && ./icon_drafts out
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../../source/gfx/stb_image_write.h"
#include "gfx.h"

#define BIG 1024
#define PI 3.14159265f

static Col W_ = {1, 1, 1, 1};

static void blend_px(Surf *s, int x, int y, Col c, float cov)
{
    if (x < 0 || y < 0 || x >= s->w || y >= s->h) return;
    uint8_t *p = s->px + ((size_t)y * s->w + x) * 4;
    float a = c.a * cov;
    p[0] = (uint8_t)(c.r * 255 * a + p[0] * (1 - a) + 0.5f);
    p[1] = (uint8_t)(c.g * 255 * a + p[1] * (1 - a) + 0.5f);
    p[2] = (uint8_t)(c.b * 255 * a + p[2] * (1 - a) + 0.5f);
}

// the part shared by two circles
static void lens(Surf *s, float x1, float y1, float r1, float x2, float y2, float r2, Col c)
{
    int x0 = (int)fminf(x1 - r1, x2 - r2), x9 = (int)fmaxf(x1 + r1, x2 + r2) + 1;
    int y0 = (int)fminf(y1 - r1, y2 - r2), y9 = (int)fmaxf(y1 + r1, y2 + r2) + 1;
    for (int y = y0; y < y9; y++)
        for (int x = x0; x < x9; x++) {
            float d1 = r1 - hypotf(x + 0.5f - x1, y + 0.5f - y1), d2 = r2 - hypotf(x + 0.5f - x2, y + 0.5f - y2);
            float d = fminf(d1, d2) + 0.5f;
            if (d <= 0) continue;
            blend_px(s, x, y, c, d > 1 ? 1 : d);
        }
}

static void lock_glyph(Surf *s, float cx, float cy, float size, Col c, Col hole)
{ // a small padlock centred on (cx, cy), `size` tall
    float bw = size * 0.78f, bh = size * 0.56f, by = cy - size / 2 + size * 0.44f;
    float sr = bw * 0.30f, sw = size * 0.13f;
    gfx_arc(s, cx, by, sr, sw, PI, 2 * PI, c);
    gfx_line(s, cx - sr, by, cx - sr, by + size * 0.1f, sw, c);
    gfx_line(s, cx + sr, by, cx + sr, by + size * 0.1f, sw, c);
    gfx_rrect(s, cx - bw / 2, by, bw, bh, size * 0.13f, c);
    gfx_circle(s, cx, by + bh * 0.40f, size * 0.07f, hole);
    gfx_rrect(s, cx - size * 0.028f, by + bh * 0.40f, size * 0.056f, bh * 0.30f, size * 0.02f, hole);
}

static void person_glyph(Surf *s, float cx, float cy, float size, Col c)
{ // head and shoulders, `size` tall, centred
    float hr = size * 0.24f;
    gfx_circle(s, cx, cy - size * 0.24f, hr, c);
    gfx_rrect(s, cx - size * 0.42f, cy + size * 0.06f, size * 0.84f, size * 0.42f, size * 0.21f, c);
}

// ---- A: two avatars swapping places (the overlap is the console, shared)
static void draw_a(Surf *s)
{
    gfx_rrect_grad(s, 0, 0, BIG, BIG, 0, hex(0xFAFBFD, 1), hex(0xDDE6EE, 1));
    float r = 292, y = 520;
    float xl = 372, xr = 652;
    gfx_shadow(s, xl - r, y - r, 2 * r, 2 * r, r, 26, 40, hex(0x1B3A52, 0.22f));
    gfx_shadow(s, xr - r, y - r, 2 * r, 2 * r, r, 26, 40, hex(0x1B3A52, 0.22f));
    gfx_circle_grad(s, xl, y, r, hex(0x4CC3F0, 1), hex(0x1596D2, 1));
    gfx_circle_grad(s, xr, y, r, hex(0xFF7466, 1), hex(0xE2433A, 1));
    lens(s, xl, y, r, xr, y, r, hex(0xFFFFFF, 1));
    lock_glyph(s, 512, y + 6, 250, hex(0x2A2F38, 1), hex(0xFFFFFF, 1));
    person_glyph(s, xl - 62, y - 8, 190, col(1, 1, 1, 0.95f));
    person_glyph(s, xr + 62, y - 8, 190, col(1, 1, 1, 0.95f));
}

// ---- B: a game card whose label is a person
static void draw_b(Surf *s)
{
    gfx_rrect_grad(s, 0, 0, BIG, BIG, 0, hex(0xE9F7FD, 1), hex(0xBFE5F5, 1));
    float x = 252, y = 120, w = 520, h = 784;
    gfx_shadow(s, x, y, w, h, 64, 28, 44, hex(0x0B3550, 0.30f));
    gfx_rrect_grad(s, x, y, w, h, 64, hex(0x4B525D, 1), hex(0x2C3139, 1));
    // the label
    gfx_rrect_grad(s, x + 44, y + 56, w - 88, 470, 36, hex(0x34CBF5, 1), hex(0x0A9CD6, 1));
    person_glyph(s, 512, y + 56 + 205, 270, col(1, 1, 1, 1));
    gfx_rrect(s, x + 44 + 60, y + 56 + 380, w - 88 - 120, 26, 13, col(1, 1, 1, 0.55f));
    // the notch and the contacts
    gfx_rrect(s, x + 44, y + 566, w - 88, 150, 28, hex(0x23272E, 1));
    for (int i = 0; i < 9; i++) gfx_rrect(s, x + 76 + i * 46, y + 600, 28, 100, 6, hex(0xD9B36A, 1));
    gfx_rrect(s, x + w - 120, y + 18, 70, 14, 7, col(1, 1, 1, 0.18f));
}

// ---- C: a club locker with a name plate
static void draw_c(Surf *s)
{
    gfx_rrect_grad(s, 0, 0, BIG, BIG, 0, hex(0xF6F1E8, 1), hex(0xE2DACB, 1));
    float x = 262, y = 112, w = 500, h = 800;
    gfx_shadow(s, x, y, w, h, 46, 30, 44, hex(0x3A2C14, 0.28f));
    gfx_rrect_grad(s, x, y, w, h, 46, hex(0x4C5460, 1), hex(0x2E343D, 1));
    gfx_rrect_stroke(s, x + 26, y + 26, w - 52, h - 52, 30, 4, col(1, 1, 1, 0.16f));
    for (int i = 0; i < 6; i++) gfx_rrect(s, x + 84, y + 82 + i * 38, w - 168, 16, 8, hex(0x1A1E24, 0.95f));
    // the name plate: a person, so it's yours
    gfx_rrect(s, x + 84, y + 360, 190, 120, 20, hex(0xFAFBFC, 1));
    person_glyph(s, x + 84 + 95, y + 360 + 60, 84, hex(0x14A9DF, 1));
    // the latch
    gfx_rrect_grad(s, x + w - 140, y + 330, 54, 190, 27, hex(0x34CBF5, 1), hex(0x0A9CD6, 1));
    gfx_circle(s, x + w - 113, y + 600, 34, hex(0xFAFBFC, 1));
    gfx_circle(s, x + w - 113, y + 592, 10, hex(0x2A2F38, 1));
    gfx_rrect(s, x + w - 119, y + 592, 12, 28, 5, hex(0x2A2F38, 1));
    gfx_rrect(s, x + 56, y + h - 80, w - 112, 22, 11, col(1, 1, 1, 0.10f));
}

// ---- D: a padlock whose shackle is the two Joy-Con colours
static void draw_d(Surf *s)
{
    gfx_rrect_grad(s, 0, 0, BIG, BIG, 0, hex(0xF7F9FB, 1), hex(0xDCE4EB, 1));
    float cx = 512, by = 520, sr = 150, sw = 96;
    Col blue = hex(0x00C3E8, 1), red = hex(0xFF4657, 1);
    gfx_arc(s, cx, by - 20, sr, sw, PI, 1.5f * PI, blue);
    gfx_arc(s, cx, by - 20, sr, sw, 1.5f * PI, 2 * PI, red);
    gfx_line(s, cx - sr, by - 20, cx - sr, by + 120, sw, blue);
    gfx_line(s, cx + sr, by - 20, cx + sr, by + 120, sw, red);
    float bx = 244, bw = BIG - 2 * bx, bh = 380;
    gfx_shadow(s, bx, by, bw, bh, 92, 28, 44, hex(0x14202E, 0.30f));
    gfx_rrect_grad(s, bx, by, bw, bh, 92, hex(0x4D5561, 1), hex(0x2B3038, 1));
    // the rails of a Joy-Con pair down either side
    gfx_rrect(s, bx + 22, by + 70, 26, bh - 140, 13, blue);
    gfx_rrect(s, bx + bw - 48, by + 70, 26, bh - 140, 13, red);
    // the keyhole
    gfx_circle(s, cx, by + 150, 50, hex(0xF4F7FA, 1));
    gfx_rrect(s, cx - 20, by + 160, 40, 120, 14, hex(0xF4F7FA, 1));
}

// ---- E: the profile grid, held shut by a lock
static void draw_e(Surf *s)
{
    gfx_rrect_grad(s, 0, 0, BIG, BIG, 0, hex(0xF8FAFC, 1), hex(0xE2E8EE, 1));
    static const uint32_t pal[4] = {0x34A8DE, 0xE2574C, 0xEE9440, 0x3AB67B};
    float t = 340, g = 36, x0 = 512 - t - g / 2, y0 = 512 - t - g / 2;
    for (int i = 0; i < 4; i++) {
        float x = x0 + (i % 2) * (t + g), y = y0 + (i / 2) * (t + g);
        gfx_shadow(s, x, y, t, t, 76, 14, 26, hex(0x1B3A52, 0.17f));
        gfx_rrect(s, x, y, t, t, 76, hex(0xFFFFFF, 1));
        gfx_rrect_stroke(s, x + 0.5f, y + 0.5f, t - 1, t - 1, 76, 2, hex(0xD9E0E7, 1));
        float cx = x + t / 2 + (i % 2 ? 22 : -22), cy = y + t / 2 + (i / 2 ? 22 : -22);
        gfx_circle_grad(s, cx, cy, 96, mixc(hex(pal[i], 1), W_, 0.18f), mixc(hex(pal[i], 1), hex(0, 1), 0.10f));
        person_glyph(s, cx, cy + 2, 100, col(1, 1, 1, 0.95f));
    }
    gfx_shadow(s, 512 - 128, 512 - 128, 256, 256, 128, 14, 24, hex(0x0A1A28, 0.35f));
    gfx_circle(s, 512, 512, 128, W_);
    gfx_circle_grad(s, 512, 512, 114, hex(0x3A414B, 1), hex(0x1F2328, 1));
    lock_glyph(s, 512, 512, 148, W_, hex(0x2A2F38, 1));
}

// ---- F: 8-bit padlock
static void draw_f(Surf *s)
{
    gfx_rrect_grad(s, 0, 0, BIG, BIG, 0, hex(0x1D2140, 1), hex(0x0E1024, 1));
    static const char *art[16] = {
        "................", "................", ".....oooooo.....", "....oooooooo....", "...ooo....ooo...", "...oo......oo...",
        "...oo......oo...", "...oo......oo...", "..############..", "..############..", "..####kkkk####..", "..####kkkk####..",
        "..#####kk#####..", "..#####kk#####..", "..############..", "................"};
    float u = 64;
    for (int r = 0; r < 16; r++)
        for (int c = 0; c < 16; c++) {
            char ch = art[r][c];
            if (ch == '.') continue;
            Col k = ch == 'o' ? hex(0xE9EEF5, 1) : ch == 'k' ? hex(0x00C8F0, 1) : hex(0xF4F7FB, 1);
            if (ch == '#' && (c >= 13 || r == 14)) k = hex(0xB9C4D6, 1);          // the shaded edges
            if (ch == 'o' && (c >= 10 || r == 7)) k = hex(0xB9C4D6, 1);
            gfx_rect(s, c * u, r * u, u, u, k);
        }
    for (int y = 0; y < BIG; y += 8) gfx_rect(s, 0, (float)y, BIG, 3, col(0, 0, 0, 0.10f)); // scanlines
}

// ---- G: in the style of Switch game icons: a bold object, thick outlines, saturated colour, a burst behind it, a title across the bottom
static Font *g_logo;

static void burst(Surf *s, float cx, float cy, Col inner, Col outer, int rays)
{
    float maxd = hypotf(BIG, BIG) * 0.5f;
    for (int y = 0; y < s->h; y++)
        for (int x = 0; x < s->w; x++) {
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, d = hypotf(dx, dy) / maxd;
            Col c = mixc(inner, outer, d > 1 ? 1 : powf(d, 0.8f));
            float a = atan2f(dy, dx) + PI;
            int band = (int)floorf(a / (2 * PI / rays));
            if (band & 1) c = mixc(c, W_, 0.10f * (1.0f - 0.6f * d));
            blend_px(s, x, y, c, 1.0f);
        }
}

static void sparkle(Surf *s, float x, float y, float r, Col c)
{
    gfx_line(s, x - r, y, x + r, y, r * 0.22f, c);
    gfx_line(s, x, y - r, x, y + r, r * 0.22f, c);
    gfx_line(s, x - r * 0.5f, y - r * 0.5f, x + r * 0.5f, y + r * 0.5f, r * 0.12f, c);
    gfx_line(s, x - r * 0.5f, y + r * 0.5f, x + r * 0.5f, y - r * 0.5f, r * 0.12f, c);
    gfx_circle(s, x, y, r * 0.2f, c);
}

static void outlined_text(Surf *s, Font *f, float px, float x, float base, const char *t, Col fill, Col edge, float r)
{
    for (int ring = 1; ring >= 0; ring--) // two rings, so the outline has no gaps
        for (int i = 0; i < 24; i++) {
            float a = i * 2 * PI / 24, rr = r * (ring ? 1.0f : 0.55f);
            text_draw(s, f, px, 0, x + cosf(a) * rr, base + sinf(a) * rr, 1, edge, t);
        }
    text_draw(s, f, px, 0, x, base, 1, fill, t);
}

static void draw_g(Surf *s)
{
    burst(s, 512, 430, hex(0x58E0FF, 1), hex(0x0762C4, 1), 22);
    Col navy = hex(0x0B2A55, 1);
    float cx = 512, by = 400, bx = 270, bw = BIG - 2 * bx, bh = 332, sr = 152, sw = 66, sy = by + 6;

    gfx_blob(s, 512, 770, 360, 52, col(0, 0.05f, 0.2f, 0.35f)); // the ground shadow
    // the outline goes in first, around the whole object, so the pieces share one edge
    gfx_arc(s, cx, sy, sr, sw + 56, PI, 2 * PI, navy);
    gfx_line(s, cx - sr, sy, cx - sr, sy + 80, sw + 56, navy);
    gfx_line(s, cx + sr, sy, cx + sr, sy + 80, sw + 56, navy);
    gfx_rrect(s, bx - 28, by - 28, bw + 56, bh + 56, 110, navy);
    gfx_arc(s, cx, sy, sr, sw + 28, PI, 2 * PI, W_);
    gfx_line(s, cx - sr, sy, cx - sr, sy + 80, sw + 28, W_);
    gfx_line(s, cx + sr, sy, cx + sr, sy + 80, sw + 28, W_);
    gfx_rrect(s, bx - 14, by - 14, bw + 28, bh + 28, 98, W_);
    // the shackle: steel
    Col steel = hex(0xC7D6E6, 1);
    gfx_arc(s, cx, sy, sr, sw, PI, 2 * PI, steel);
    gfx_line(s, cx - sr, sy, cx - sr, sy + 80, sw, steel);
    gfx_line(s, cx + sr, sy, cx + sr, sy + 80, sw, steel);
    gfx_arc(s, cx, sy, sr + 4, sw * 0.28f, PI * 1.05f, PI * 1.55f, col(1, 1, 1, 0.85f));
    // the body: gold
    gfx_rrect_grad(s, bx, by, bw, bh, 84, hex(0xFFD43B, 1), hex(0xFF9A1F, 1));
    gfx_rrect(s, bx + 22, by + 20, 150, 26, 13, col(1, 1, 1, 0.55f));
    gfx_circle(s, bx + 196, by + 33, 13, col(1, 1, 1, 0.55f));
    gfx_rrect(s, bx, by + bh - 40, bw, 40, 0, col(0.85, 0.35, 0, 0.0f));
    // the keyhole is a player
    gfx_circle(s, cx, by + 128, 44, navy);
    gfx_rrect(s, cx - 76, by + 186, 152, 96, 48, navy);
    sparkle(s, 196, 250, 54, W_);
    sparkle(s, 840, 322, 40, W_);
    sparkle(s, 790, 142, 30, W_);
    // the title
    float rx = 64, ry = 806, rw = BIG - 128, rh = 166;
    gfx_rrect(s, rx - 16, ry - 16, rw + 32, rh + 32, 56, navy);
    gfx_rrect(s, rx - 8, ry - 8, rw + 16, rh + 16, 50, W_);
    gfx_rrect_grad(s, rx, ry, rw, rh, 44, hex(0xFF4A4A, 1), hex(0xD6001C, 1));
    float px = 128;
    while (px > 60 && text_width(g_logo, px, 0, "WASHI") > rw - 90) px -= 4;
    outlined_text(s, g_logo, px, 512, ry + rh / 2 + px * 0.34f, "WASHI", W_, navy, 7.0f);
}

typedef void (*DrawFn)(Surf *);

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    DrawFn fns[7] = {draw_a, draw_b, draw_c, draw_d, draw_e, draw_f, draw_g};
    static const char *names[7] = {"A-swap", "B-card", "C-locker", "D-joycon", "E-grid", "F-pixel", "G-game"};
    {
        FILE *ff = fopen("../fonts_src/MPLUSRounded1c-ExtraBold.ttf", "rb");
        if (!ff) { fprintf(stderr, "run this from gfx/tools (needs ../fonts_src)\n"); return 1; }
        fseek(ff, 0, SEEK_END);
        long n = ftell(ff);
        fseek(ff, 0, SEEK_SET);
        unsigned char *buf = malloc((size_t)n);
        if (fread(buf, 1, (size_t)n, ff) != (size_t)n) return 1;
        fclose(ff);
        g_logo = font_load(buf); // kept for the life of the program
    }
    Surf *big = surf_new(BIG, BIG);
    enum { N = 256, K = BIG / N };
    for (int i = 0; i < 7; i++) {
        gfx_fill(big, col(1, 1, 1, 1));
        fns[i](big);
        uint8_t rgb[N * N * 3], rgba[N * N * 4];
        for (int y = 0; y < N; y++)
            for (int x = 0; x < N; x++) {
                int acc[3] = {0, 0, 0};
                for (int j = 0; j < K; j++)
                    for (int k = 0; k < K; k++) {
                        const uint8_t *p = big->px + ((size_t)(y * K + j) * BIG + (x * K + k)) * 4;
                        for (int c = 0; c < 3; c++) acc[c] += p[c];
                    }
                for (int c = 0; c < 3; c++) {
                    rgb[(y * N + x) * 3 + c] = (uint8_t)((acc[c] + K * K / 2) / (K * K));
                    rgba[(y * N + x) * 4 + c] = rgb[(y * N + x) * 3 + c];
                }
                rgba[(y * N + x) * 4 + 3] = 255;
            }
        char path[600];
        snprintf(path, sizeof path, "%s/%s.png", dir, names[i]);
        stbi_write_png(path, N, N, 4, rgba, N * 4);
        snprintf(path, sizeof path, "%s/%s.jpg", dir, names[i]);
        stbi_write_jpg(path, N, N, 3, rgb, 95);
        snprintf(path, sizeof path, "%s/%s-1024.png", dir, names[i]);
        stbi_write_png(path, BIG, BIG, 4, big->px, BIG * 4);
    }
    surf_free(big);
    printf("wrote 7 drafts to %s\n", dir);
    return 0;
}
