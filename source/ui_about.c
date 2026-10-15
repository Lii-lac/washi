// About, in Washi: a taped sheet on the board with the mark, what the app is, three tape labels for the steps, the club's QR code on a
// taped print (its blocks coloured in with wax crayon), and the Discord link underlined in ink-blue crayon. Along the bottom, small and
// soft: the maker's GitHub mark and name, and the version. Nothing on it moves, so a frame is the base copied.
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "gfx/crayon.h"
#include "ui_scene.h"

#ifndef APP_VERSION
#define APP_VERSION "dev" // the Makefile passes the real one, the same that hbmenu shows
#endif

const char *ui_version(void) { return APP_VERSION; }

static const uint8_t *g_qr_png;
static size_t g_qr_size;
int g_ui_about_qr; // tests: modules to a side of the code the page last read off its picture (0: none)

void ui_set_qr(const uint8_t *png, size_t size)
{
    g_qr_png = png;
    g_qr_size = size;
}

static const char *const BLURB =
    "Washi is a user-based save file manager, made for community consoles in places like cafes and gaming lounges. "
    "Players get their own save files for single-player games, and can come back whenever they want.";
static const char *const BLURB2 = "Made by the Esports Club! Check us out on Discord, or scan the QR code.";
static const char *const LINK = "discord.gg/NUEsports";
static const char *const MAKER = "Lii-lac";

enum { SX = 90, SY = 125, SW = 1100, SH = 508 };
enum { PRINT_X = 844, PRINT_Y = 172, PRINT_S = 280, QR_MOD = 8 }; // the print the code is on (square), and the px of one module of the code

// ---- the club's code, read off its picture (data/qr.bin: white on clear, a margin round it), so it can be drawn in Washi's own way

enum { QR_MAXN = 73 };

typedef struct {
    int n; // modules to a side (21, 25, 29 ...)
    uint8_t m[QR_MAXN * QR_MAXN];
} QrMat;

static int qr_at(const QrMat *q, int i, int j) { return i >= 0 && j >= 0 && i < q->n && j < q->n && q->m[j * q->n + i]; }

// A reading is right when the fixed parts of a QR code are where they must be: the three eyes (a ring, a gap, a 3 x 3 middle), the light line
// round each, and the dotted timing lines between them. A wrong guess of the size almost never gets all of that.
static int qr_sound(const QrMat *q)
{
    int n = q->n;
    const int ex[3] = {0, n - 7, 0}, ey[3] = {0, 0, n - 7};
    for (int e = 0; e < 3; e++)
        for (int dy = -1; dy <= 7; dy++)
            for (int dx = -1; dx <= 7; dx++) {
                int i = ex[e] + dx, j = ey[e] + dy;
                if (i < 0 || j < 0 || i >= n || j >= n) continue;
                int in = dx >= 0 && dx <= 6 && dy >= 0 && dy <= 6;
                int want = in && (dx == 0 || dx == 6 || dy == 0 || dy == 6 || (dx >= 2 && dx <= 4 && dy >= 2 && dy <= 4));
                if (qr_at(q, i, j) != want) return 0;
            }
    for (int k = 8; k < n - 8; k++)
        if (qr_at(q, k, 6) != (k % 2 == 0) || qr_at(q, 6, k) != (k % 2 == 0)) return 0;
    return 1;
}

// The modules of the code in `rgba` (w x h): the ink is the alpha channel; the code is the box round all of it; the size is the one whose
// modules, read at their middles, make a sound code. 0 when there is none (then the picture itself is drawn).
static int qr_read(const uint8_t *rgba, int w, int h, QrMat *q)
{
    int x0 = w, y0 = h, x1 = -1, y1 = -1;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            if (rgba[((size_t)y * w + x) * 4 + 3] > 128) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
    if (x1 < x0 || y1 < y0) return 0;
    float side = (float)(x1 - x0 + 1);
    if (fabsf(side - (float)(y1 - y0 + 1)) > 3.0f) return 0;
    for (int n = 21; n <= QR_MAXN; n += 4) {
        float m = side / (float)n;
        if (m < 4.0f) break;
        q->n = n;
        for (int j = 0; j < n; j++)
            for (int i = 0; i < n; i++) {
                int cx = x0 + (int)((i + 0.5f) * m), cy = y0 + (int)((j + 0.5f) * m), sum = 0;
                for (int dy = -1; dy <= 1; dy++)
                    for (int dx = -1; dx <= 1; dx++) sum += rgba[((size_t)(cy + dy) * w + (cx + dx)) * 4 + 3];
                q->m[j * n + i] = sum / 9 > 128;
            }
        if (qr_sound(q)) return 1;
    }
    return 0;
}

// The code as blocks coloured in with wax crayon (crayon_grid): a sprite as big as the code with a hair of room for the edges to wander. Its seed is
// one of the many that read as well as plain squares under a harsh test (shrunk to a third, blurred, noisy). 0x302B4C is the dark wax, 0x66608A the light.
enum { QR_ROOM = 4, QR_SEED = 47 };

static uint8_t *g_wax; // the last one made, kept: colouring it in takes a moment on the console, and the page is opened again and again
static QrMat g_wax_mat;

static uint8_t *qr_crayon(const QrMat *q)
{
    if (g_wax && g_wax_mat.n == q->n && !memcmp(g_wax_mat.m, q->m, (size_t)q->n * q->n)) return g_wax;
    int side = q->n * QR_MOD + 2 * QR_ROOM;
    free(g_wax);
    g_wax = crayon_grid(q->m, q->n, (float)QR_MOD, side, side, (float)QR_ROOM, (float)QR_ROOM, 0x302B4Cu, 0x66608Au, QR_SEED);
    g_wax_mat = *q;
    return g_wax;
}

// ---- the wax picture, made beforehand. Colouring the code in takes the console most of a second, and it is always the same picture for the same code, so the
// build keeps it (data/qr_wax.bin, made by the PC test: see ui_about_wax_blob) and the page just uses it. Blob: "WQR1", modules to a side, the side in px, a hash of
// the module matrix (all 32 bits little endian), then side x side RGBA. If the code changes (the hash differs) the page colours it in itself, as before.
static const uint8_t *g_qr_wax;
static size_t g_qr_wax_size;

void ui_set_qr_wax(const uint8_t *blob, size_t size)
{
    g_qr_wax = blob;
    g_qr_wax_size = size;
}

static uint32_t qr_hash(const QrMat *q)
{
    uint32_t h = 2166136261u ^ (uint32_t)q->n;
    for (int i = 0; i < q->n * q->n; i++) h = (h ^ q->m[i]) * 16777619u;
    return h;
}

static uint32_t le32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

// the picture inside the stored blob when it is for this code, else NULL
static const uint8_t *qr_stored(const QrMat *q)
{
    int side = q->n * QR_MOD + 2 * QR_ROOM;
    if (!g_qr_wax || g_qr_wax_size != 16 + (size_t)side * side * 4 || memcmp(g_qr_wax, "WQR1", 4)) return NULL;
    if (le32(g_qr_wax + 4) != (uint32_t)q->n || le32(g_qr_wax + 8) != (uint32_t)side || le32(g_qr_wax + 12) != qr_hash(q)) return NULL;
    return g_qr_wax + 16;
}

// for the build: the blob for the code in `png` (made the slow way); free() it. NULL when the picture is no code.
uint8_t *ui_about_wax_blob(const uint8_t *png, size_t size, size_t *out_size)
{
    int iw, ih;
    uint8_t *img = gfx_decode_png(png, size, &iw, &ih);
    static QrMat q;
    if (!img || !qr_read(img, iw, ih, &q)) {
        free(img);
        return NULL;
    }
    free(img);
    int side = q.n * QR_MOD + 2 * QR_ROOM;
    uint8_t *wax = crayon_grid(q.m, q.n, (float)QR_MOD, side, side, (float)QR_ROOM, (float)QR_ROOM, 0x302B4Cu, 0x66608Au, QR_SEED);
    uint8_t *blob = wax ? malloc(16 + (size_t)side * side * 4) : NULL;
    if (blob) {
        memcpy(blob, "WQR1", 4);
        uint32_t v[3] = {(uint32_t)q.n, (uint32_t)side, qr_hash(&q)};
        for (int i = 0; i < 3; i++)
            for (int b = 0; b < 4; b++) blob[4 + i * 4 + b] = (uint8_t)(v[i] >> (8 * b));
        memcpy(blob + 16, wax, (size_t)side * side * 4);
        *out_size = 16 + (size_t)side * side * 4;
    }
    free(wax);
    return blob;
}


typedef struct {
    const uint8_t *img; // the picture itself, drawn as a mask when its modules could not be read
    int iw, ih;
    QrMat mat;
    int have_mat;
    uint8_t *wax; // the code coloured in (NULL when it could not be made)
    int ws;       // its side
} Qr;

// what is printed on the print that holds the code: the code, as big as the print allows with a quiet margin round it
static void draw_print(Surf *s, float ox, float oy, void *ud)
{
    const Qr *q = ud;
    if (!pal_light(g_pal.paper)) // (Paper night: the print stays a white print, dark wax on it, so a phone can still read the code)
        gfx_rrect(s, ox, oy, PRINT_S, PRINT_S, 4.0f, hex(PAPER_PAPER, 1));
    if (q->wax) {
        gfx_blit_over(s, q->wax, q->ws, q->ws, (int)(ox + (PRINT_S - q->ws) * 0.5f), (int)(oy + (PRINT_S - q->ws) * 0.5f));
    } else if (q->img) {
        gfx_blit_mask(s, q->img, q->iw, q->ih, ox + 24, oy + 24, PRINT_S - 48, PRINT_S - 48, hex(PAPER_INK, 1));
    }
}

// ---- the GitHub mark: its outline as an SVG path in a 24 x 24 box (only M, c and C), cut into short lines and filled with 4 x 4 samples to
// the pixel (even-odd)

typedef struct {
    float x, y;
} Pt;

static const char GITHUB_PATH[] =
    "M12 .297c-6.63 0-12 5.373-12 12 0 5.303 3.438 9.8 8.205 11.385.6.113.82-.258.82-.577 0-.285-.01-1.04-.015-2.04-3.338.724-4.042-1.61-4.042-1.61"
    "C4.422 18.07 3.633 17.7 3.633 17.7c-1.087-.744.084-.729.084-.729 1.205.084 1.838 1.236 1.838 1.236 1.07 1.835 2.809 1.305 3.495.998.108-.776.417-1.305.76-1.605"
    "-2.665-.3-5.466-1.332-5.466-5.93 0-1.31.465-2.38 1.235-3.22-.135-.303-.54-1.523.105-3.176 0 0 1.005-.322 3.3 1.23.96-.267 1.98-.399 3-.405 1.02.006 2.04.138 3 .405"
    " 2.28-1.552 3.285-1.23 3.285-1.23.645 1.653.24 2.873.12 3.176.765.84 1.23 1.91 1.23 3.22 0 4.61-2.805 5.625-5.475 5.92.42.36.81 1.096.81 2.22 0 1.606-.015 2.896-.015 3.286"
    " 0 .315.21.69.825.57C20.565 22.092 24 17.592 24 12.297c0-6.627-5.373-12-12-12";

enum { PATH_PTS = 400 };

static int path_points(const char *d, Pt *p, int cap)
{
    int n = 0;
    float cx = 0, cy = 0;
    char cmd = 0;
    const char *s = d;
    while (*s) {
        while (*s == ' ' || *s == ',') s++;
        if (!*s) break;
        if (isalpha((unsigned char)*s)) cmd = *s++;
        float a[6];
        int need = cmd == 'M' ? 2 : 6;
        for (int k = 0; k < need; k++) {
            char *e;
            a[k] = strtof(s, &e);
            if (e == s) return n;
            s = e;
            while (*s == ' ' || *s == ',') s++;
        }
        if (cmd == 'M') {
            cx = a[0];
            cy = a[1];
            if (n < cap) p[n++] = (Pt){cx, cy};
            continue;
        }
        float x1 = a[0], y1 = a[1], x2 = a[2], y2 = a[3], x3 = a[4], y3 = a[5];
        if (cmd == 'c') { // (relative to where the last one ended)
            x1 += cx; y1 += cy; x2 += cx; y2 += cy; x3 += cx; y3 += cy;
        }
        for (int k = 1; k <= 8 && n < cap; k++) {
            float t = k / 8.0f, u = 1.0f - t;
            p[n++] = (Pt){u * u * u * cx + 3 * u * u * t * x1 + 3 * u * t * t * x2 + t * t * t * x3,
                          u * u * u * cy + 3 * u * u * t * y1 + 3 * u * t * t * y2 + t * t * t * y3};
        }
        cx = x3;
        cy = y3;
    }
    return n;
}

// the mark `size` px square with its top left at (x, y)
void ui_github_mark(Surf *s, float x, float y, float size, Col c)
{
    Pt p[PATH_PTS];
    int n = path_points(GITHUB_PATH, p, PATH_PTS);
    if (n < 3) return;
    float k = size / 24.0f;
    int px0 = (int)floorf(x), py0 = (int)floorf(y), px1 = (int)ceilf(x + size), py1 = (int)ceilf(y + size);
    for (int py = py0; py < py1; py++)
        for (int px = px0; px < px1; px++) {
            int hit = 0;
            for (int sy = 0; sy < 4; sy++)
                for (int sx = 0; sx < 4; sx++) {
                    float fx = (px + (sx + 0.5f) / 4.0f - x) / k, fy = (py + (sy + 0.5f) / 4.0f - y) / k;
                    int in = 0;
                    for (int e = 0; e < n; e++) {
                        Pt a = p[e], b = p[(e + 1) % n];
                        if ((a.y > fy) != (b.y > fy) && fx < a.x + (fy - a.y) * (b.x - a.x) / (b.y - a.y)) in ^= 1;
                    }
                    hit += in;
                }
            if (!hit) continue;
            Col k2 = c;
            k2.a = c.a * (float)hit / 16.0f;
            gfx_rect(s, (float)px, (float)py, 1.0f, 1.0f, k2);
        }
}

static void about_base(const Qr *q)
{
    Skin *u = &G.skin;
    scene_chrome(G.base, NULL, NULL, (const Prompt[]){{"B", "Back"}}, 1);
    Cx c = ui_cx(G.base);
    const Col ink = hex(PAL(text, PAPER_TEXT), 1), soft = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);

    SceneTape sheet_tapes[2] = {{{100, 26, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, -34.0f, 71u}, -16, -12},
                                {{100, 26, paper_tape_rgb[1], PAT_DOTS, PAPER_TAPE_ZIG, 34.0f, 72u}, SW - 84, -14}};
    scene_card_on(G.base, SX, SY, SW, SH, 6.0f, 81u, 0.0f, 0, sheet_tapes, 2, NULL, NULL);

    // the mark on a little card of its own, the name, and what it is
    float lx = SX + 55, ly = SY + 52;
    skin_washi_mark(&c, lx + 37, ly + 44, 1.45f);
    // the name: a pink highlighter stroke behind it, the name in bold, and two waves of ink-blue crayon under it
    float tx0 = lx + 107, tbase = ly + 58, tw = text_width(u->semi, 58, -0.5f, "Washi");
    PaperSpr *hl = paper_hatch((int)tw + 14, 34, pal_mix(0xFFC9D4, g_pal.paper, PAL(hi_mix, 0.0f)), 0.85f, 41u); // (Paper night: towards the paper, as on Home)
    if (hl) paper_spr_put(G.base, hl, (int)(tx0 - 6), (int)(tbase - 20));
    paper_spr_free(hl);
    skin_txt(&c, u->semi, 58, -0.5f, tx0, tbase, 0, ink, "Washi");
    PaperSpr *wv = paper_wave((int)tw + 6, &PAPER_CRAYON_BLUE, 43u);
    if (wv) paper_spr_put(G.base, wv, (int)(tx0 - 3), (int)(tbase + 15));
    paper_spr_free(wv);
    skin_txt(&c, u->med, 14.5f, 3.0f, lx + 109, ly + 105, 0, soft, "A SAVE MANAGER FOR SHARED CONSOLES");

    char lines[8][160];
    int n = ui_wrap(u->med, 22, 640, BLURB, lines, 8);
    float ty = ly + 150;
    for (int i = 0; i < n; i++) skin_txt(&c, u->med, 22, 0, lx, ty + i * 37.5f, 0, soft, lines[i]);
    ty += (n - 1) * 37.5f + 62;
    n = ui_wrap(u->semi, 22, 590, BLURB2, lines, 8);
    for (int i = 0; i < n; i++) skin_txt(&c, u->semi, 22, 0, lx, ty + i * 37.5f, 0, ink, lines[i]);

    // how it goes, in three steps: a strip of tape each
    static const char *const steps[3] = {"Pick your name", "Play", "Press X to sign out"};
    static const int kind[3] = {PAT_DOTS, PAT_STRIPES, PAT_GRID};
    static const int rgb[3] = {5, 3, 2};
    float sx = lx, sy = SY + SH - 66;
    for (int i = 0; i < 3; i++) {
        float tw = text_width(u->semi, 20, 0, steps[i]);
        int pw = (int)tw + 78;
        pw += pw & 1;
        // (Paper night's tape is mid-toned: neither dark nor pale writing reads on it. There the labels' tape is the same hue taken down toward the night's
        // ink, its pattern printed softer, and the writing is the night's pale one)
        int night = !pal_light(g_pal.paper);
        unsigned soft = night ? PAPER_TAPE_PAT(6) : 0u;
        uint32_t trgb = paper_tape_rgb[rgb[i]];
        if (night) {
            const uint32_t deep = 0x24223A;
            const float k = 0.48f;
            uint32_t mixed = 0;
            for (int sh = 0; sh <= 16; sh += 8) {
                float a = (float)((trgb >> sh) & 0xFF), b = (float)((deep >> sh) & 0xFF);
                mixed |= (uint32_t)lroundf(a + (b - a) * k) << sh;
            }
            trgb = mixed;
        }
        Col write = night ? hex(PAL(text, PAPER_TEXT), 1) : hex(PAPER_TEXT, 1);
        PaperTape t = {pw, 44, trgb, kind[i], PAPER_TAPE_ZIG | soft, 0.0f, 90u + (uint32_t)i};
        PaperSpr *sp = paper_tape(&t);
        if (sp) paper_spr_put(G.base, sp, (int)(sx + pw * 0.5f), (int)(sy + 22));
        paper_spr_free(sp);
        gfx_ring(G.base, sx + 33, sy + 22, 12.5f, 2.0f, night ? with_a(write, 0.9f) : hex(PAPER_CRAYON_1, 0.9f));
        char num[2] = {(char)('1' + i), 0};
        skin_txtc(&c, u->semi, 16, 0, sx + 33, sy + 22.5f, 1, write, num);
        skin_txtc(&c, u->semi, 20, 0, sx + 56, sy + 22.5f, 0, write, steps[i]);
        sx += pw + 20;
    }

    // the print with the code, taped, and the link under it
    SceneTape qt = {{72, 22, paper_tape_rgb[4], PAT_STRIPES, PAPER_TAPE_ZIG, -36.0f, 99u}, -18, -4};
    scene_card_on(G.base, PRINT_X, PRINT_Y, PRINT_S, PRINT_S, 4.0f, 82u, 0.4f, 0, &qt, 1, draw_print, (void *)q);
    float mx = PRINT_X + PRINT_S * 0.5f, by = PRINT_Y + PRINT_S;
    skin_txt(&c, u->med, 20, 0, mx, by + 45, 1, soft, "Scan it, or type");
    skin_txt(&c, u->semi, 31, -0.2f, mx, by + 90, 1, ink, LINK);
    int uw = (int)ceilf(text_width(u->semi, 31, -0.2f, LINK)) + 6;
    const PaperCrayon ONE_LINE = {PAL(ink, PAPER_INK), PAL(ink, PAPER_INK), 0.92f, 0.0f, 1.0f}; // (the second, paler line is left out)
    PaperSpr *ul = paper_underline(uw, &ONE_LINE, 7u);
    if (ul) paper_spr_put(G.base, ul, (int)(mx - uw * 0.5f), (int)by + 109);
    paper_spr_free(ul);

    // along the bottom, small and soft: the maker's GitHub mark and name, a dot, and the version
    char ver[40];
    snprintf(ver, sizeof ver, "Version %s", APP_VERSION);
    float vx1 = SX + SW - 40, vy = SY + SH - 26, vw = text_width(u->med, 17, 0, ver), mw = text_width(u->med, 17, 0, MAKER);
    skin_txt(&c, u->med, 17, 0, vx1, vy, 2, soft, ver);
    gfx_circle(G.base, vx1 - vw - 17, vy - 5.5f, 2.0f, hex(PAL(text_soft, PAPER_TEXT_SOFT), 0.55f));
    float mx1 = vx1 - vw - 34;
    skin_txt(&c, u->med, 17, 0, mx1, vy, 2, soft, MAKER);
    ui_github_mark(G.base, mx1 - mw - 10 - 20, vy - 16.5f, 20.0f, soft);
}

void ui_about(void)
{
    if (G.quit) return;
    static Qr q; // (the code's reading is 5 KB: not for the stack)
    memset(&q, 0, sizeof q);
    uint8_t *img = g_qr_png ? gfx_decode_png(g_qr_png, g_qr_size, &q.iw, &q.ih) : NULL;
    q.img = img;
    q.have_mat = img && qr_read(img, q.iw, q.ih, &q.mat);
    g_ui_about_qr = q.have_mat ? q.mat.n : 0;
    if (q.have_mat) {
        q.ws = q.mat.n * QR_MOD + 2 * QR_ROOM;
        q.wax = (uint8_t *)qr_stored(&q.mat);
        if (!q.wax) q.wax = qr_crayon(&q.mat);
    }
    about_base(&q);
    ui_transition();
    for (;;) {
        surf_copy(G.work, G.base);
        ui_present(G.work);
        uint64_t b = ui_poll(-1);
        if (b == UI_QUIT || (b & (BTN_B | BTN_A))) break;
        if (b & UI_REDRAW) about_base(&q);
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            while (ui_touch_next(&te)) {}
        }
    }
    free(img);
    ui_screen_done();
}
