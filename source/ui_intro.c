// The intro, before the first home screen of a run. Rows of washi are laid over the black by rolls, each from off the screen, a different wall at every start; a
// paper card comes down on it, the name's strip is pressed on by a thumb, the name is written, the seal is stamped; then the card is pulled away, the wall is
// ripped off row by row (the newest first), the corner's logo is stamped on the board and the home screen comes in piece by piece. Any button skips it: the
// screen gives way to the home screen while the seal is stamped in the middle and flies up into its corner.
// This is the intro mockup's "B4" (the design mock-up's B4.js, not kept here) in C, with its numbers. Its pictures are the app's own (the tape and roll of
// gfx/wall.c, the logo from skin.c, the home screen as ui_home.c draws it) except the card's printing (a picture made by the mockup page; there may be several,
// one is picked at random) and the music (made from the page's synthesised notes, data/intro_music.wav).
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "gfx/wall.h"
#include "ui_internal.h"

#ifdef __SWITCH__
#include <switch.h>
#endif

#define PI_F 3.14159265358979f
#define D2R (PI_F / 180.0f)
#define WK 4.5f // the wall's tape is the app's tape 4.5 times bigger
#define MAXROW 16
#define MAXCARD 4

// ---- the timeline (B4T, B4_HOME), seconds
static const float T_START = 0.10f, T_SPREAD = 0.50f, T_DUR = 0.40f, T_DURVAR = 0.06f, T_BUDGET = 0.18f;
static const float T_PAPER0 = 1.18f, T_PAPER1 = 1.32f, T_SEAL = 1.89f, T_TOUCH = 1.28f, T_TDUR = 0.30f, T_FALL = 0.10f, T_W0 = 1.60f, T_W1 = 1.86f;
static const float T_TAPESOFF = 2.29f, T_PULL0 = 2.34f, T_PULL1 = 2.79f, T_RIP0 = 2.42f, T_RIPSPAN = 0.45f, T_RIPDUR = 0.30f, T_SETTLE = 3.64f, T_END = 3.78f;
static const float H_BOARD0 = 1.30f, H_BOARD1 = 1.35f, H_TILE0 = 2.84f, H_STEP = 0.045f, H_TDUR = 0.28f;
// the snags: the "lesser" kind (a few soft creases and small hitches; the user found the calmer wall "perfect" with them)
static const float M_CRIMP = 0.14f, M_SNAG0 = 0.8f, M_SNAG1 = 0.96f, M_FX = 0.5f, M_RIDGE = 0.5f, M_JOG = 0.35f, M_HOLD = 40, M_EASE = 110, M_WIN = 260;
// the logo on the card: 2.5 times the corner's; the corner lockup's middle and the title's and seal's places in it (core.js LC, TITLE_C, SEAL_AT)
static const float LOGO_K = 2.5f, LC_X = 167, LC_Y = 52.5f, TITLE_CX = 202, TITLE_CY = 54, SEAL_CX = 83.44f, SEAL_CY = 53.8f;

#ifdef UI_INTRO_PROF // (PC builds only: timing)
static double prof_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}
#endif

static inline float clamp01(float v) { return v < 0 ? 0 : v > 1 ? 1 : v; }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }
static inline float prog(float t, float a, float b) { return clamp01((t - a) / (b - a)); }
static inline float e_in2(float x) { return x * x; }
static inline float e_in3(float x) { return x * x * x; }
static inline float e_out2(float x) { return 1 - (1 - x) * (1 - x); }
static inline float e_out3(float x) { return 1 - (1 - x) * (1 - x) * (1 - x); }
static inline float e_smooth(float x) { return x * x * (3 - 2 * x); }
static inline float e_back(float x, float s) { float q = x - 1; return 1 + (s + 1) * q * q * q + s * q * q; }
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

// ------------------------------------------------------------------------------------------------ one row of the wall

typedef struct { float p, s, tau; int crimp; } Catch;
typedef struct {
    // the plan
    int th1, colour, kind, dir;
    float hpx, tilt, t, half, cx, cy, L1, crimp_r, pressed, dur;
    uint32_t seed;
    WallNick nicks[WALL_NICK_MAX];
    int nn;
    // made
    int ok, crimp_on, pad;
    PaperSpr *strip;  // the strip as it is seen (mirrored for a roll that runs right to left, so the light stays on its top left edges)
    PaperSpr *turned; // turned by its tilt, with its shadow; its middle is the strip's middle
    int16_t *runs;    // its rows' solid runs (wall_runs)
    WallTex *tex;     // its roll's pattern
    float sx, sk, ky, kr, ph[65], r0, r1, rb, circ0, Lpx, Hpx, ang, back[3];
    int nc;
    Catch c[3];
    float t0, d_end, rip_at;
} Row;

typedef struct { float back, shiver, shiver_deg, pucker; } Fx;

// What one drawer of frames keeps: the frame it draws, and the pictures it keeps from frame to frame (each goes on from where it was while the time moves on,
// and is made again if it goes back: the tests draw frames out of order): black with the rows laid so far, the first wall_n of them (once all are, the whole
// wall); that with the card resting on it and the logo's pieces that are done (rest_lv of them); the board with the first under_k rows, while the ones after
// them are being torn off. A NULL picture is drawn round (the frame is then drawn whole).
typedef struct {
    Surf *frame, *c_wall, *c_rest, *c_under;
    int wall_n, rest_lv, under_k;
    float wall_t, rest_t;
    WallScratch sc;
} Ctx;

typedef struct { const uint8_t *png; size_t n; float lx, ly; } CardDef;
static CardDef g_cards[MAXCARD];
int g_ui_intro_ncards; // the cards added (tests set it back to 0: no intro)

typedef struct { PaperSpr *spr; float px, py; } Piece; // a part of the home screen, and the point it turns and grows about
static struct {
    int state; // 0 nothing, 1 being made, 2 ready
    uint32_t seed;
    int nrows;
    Row rows[MAXROW];
    int card;
    uint8_t *card_px;
    int cw, ch;
    uint8_t *sh_rest, *sh_lift; // its two shadows as planes of alpha (shw x shh, the card's middle at theirs), in SH_RGB
    int shw, shh, sh_hole[4];   // and where they are clear for sure (under the card's solid middle): x0, y0, x1, y1
    float lx, ly;
    atomic_int next, done;
    int njobs;
#ifdef __SWITCH__
    Thread th[2];
    int nth;
#endif
    // made on the main thread when it plays
    PaperSpr *blank, *text, *seal, *lay; // the logo's pieces at 2.5 times; the name's strip cut for laying
    uint8_t *written, *glow;             // the name once it is all written (the text's size); the seal's bloom, a plane of alpha (gw x gh) in ink blue
    int gw, gh;
    const PaperSpr *mark1, *title1;      // the corner's own (skin.c's, do not free)
    Surf *home, *board;                  // the home screen as it ends; the bare board
    Piece top, rail, footer, tiles[14];
    int ntiles, order[14];
    float corner_at;
    Ctx ctx[3]; // the drawers (on the console two cores that take turns, and the main core joins them once the home screen is made)
} I;

// tests: frames drawn at set times before the intro plays (each handed to the hook), and the intro's numbers
void (*g_ui_intro_shot)(const Surf *frame, float t);
const float *g_ui_intro_times;
int g_ui_intro_ntimes;
uint32_t g_ui_intro_seed; // 0: from the clock
int g_ui_intro_frames;    // frames the last intro showed
float g_ui_intro_third_at; // the intro's time when the third drawer began to take turns in the last intro (0: it did not)
int g_ui_intro_played;    // 1 played to the end, 2 skipped

int g_ui_intro_card; // tests: the card the last start picked

// which of n cards a start with this seed has (each as likely)
int ui_intro_pick(uint32_t seed, int n)
{
    WallRng r;
    wall_rng_seed(&r, seed ^ 0xCA4Du);
    return n > 0 ? (int)(wall_rng(&r) * n) % n : 0;
}

void ui_intro_add_card(const uint8_t *png, size_t n, float lx, float ly)
{
    if (!png || !n || g_ui_intro_ncards >= MAXCARD) return;
    g_cards[g_ui_intro_ncards++] = (CardDef){png, n, lx, ly};
}

// ---- the plan (b4Plan): everything a start's wall is made of, from one number
static int pick_other(WallRng *r, int n, int a, int b)
{
    for (int g = 0; g < 30; g++) {
        int v = (int)floorf(wall_rng(r) * n);
        if (v != a && v != b) return v;
    }
    return (a + 1) % n;
}

static int plan_rows(uint32_t seed, Row *rows)
{
    WallRng r;
    wall_rng_seed(&r, seed ^ 0x5EEDu);
    int n = 0;
    float sgn = wall_rng(&r) < 0.5f ? 1.0f : -1.0f;
    int dir = wall_rng(&r) < 0.5f ? 1 : -1, c1 = -1, c2 = -1, k1 = -1, k2 = -1;
    for (int i = 0; i < MAXROW; i++) {
        Row *R = &rows[n];
        memset(R, 0, sizeof *R);
        R->th1 = 35 + (int)floorf(wall_rng(&r) * 9);
        R->hpx = R->th1 * WK;
        R->tilt = sgn * (0.35f + wall_rng(&r) * 1.05f);
        R->cx = 640 + (wall_rng(&r) - 0.5f) * 44;
        sgn = -sgn;
        R->colour = pick_other(&r, 8, c1, c2);
        R->kind = pick_other(&r, 8, k1, k2);
        c2 = c1;
        c1 = R->colour;
        k2 = k1;
        k1 = R->kind;
        R->t = tanf(R->tilt * D2R);
        R->half = R->hpx / (2 * cosf(R->tilt * D2R));
        R->dir = dir;
        R->crimp_r = wall_rng(&r);
        R->seed = (uint32_t)floor((double)wall_rng(&r) * 4294967295.0);
        R->dur = T_DUR + T_DURVAR * wall_rng(&r);
        R->pressed = wall_rng(&r);
        R->L1 = roundf((358 + floorf(wall_rng(&r) * 9)) * WK) / WK; // (a whole number of pixels long)
        dir = -dir;
        float cx = R->cx, t = R->t;
        if (i == 0) { // the top edge is above the screen by at least this much, at both sides
            float m0 = 16 + wall_rng(&r) * 10;
            R->cy = R->half - m0 - fmaxf((0 - cx) * t, (SCREEN_W - cx) * t);
        } else { // overlaps the row before by at least m at the worse of the two sides
            const Row *p = &rows[n - 1];
            float m = 30 + wall_rng(&r) * 16, f0 = (0 - cx) * t - (0 - p->cx) * p->t, f1 = (SCREEN_W - cx) * t - (SCREEN_W - p->cx) * p->t;
            R->cy = p->cy + p->half + R->half - m - fmaxf(f0, f1);
        }
        // nicks in the long edges: a bite, a slit, a scuff; none in some strips and up to four in others
        float nf = wall_rng(&r);
        int nn = nf < 0.15f ? 0 : nf < 0.45f ? 1 : nf < 0.75f ? 2 : nf < 0.92f ? 3 : 4;
        for (int q = 0; q < nn; q++) {
            float g = wall_rng(&r);
            int top = wall_rng(&r) < 0.5f;
            float x = 44 + (R->L1 - 88) * wall_rng(&r), sk = (wall_rng(&r) - 0.5f) * 0.8f, j = 0.12f + 0.2f * wall_rng(&r);
            uint32_t sd = 1 + (uint32_t)floorf(wall_rng(&r) * 9999);
            WallNick *k = &R->nicks[q];
            k->x = x;
            k->top = top;
            k->sk = sk;
            k->j = j;
            k->sd = sd;
            if (g < 0.5f) {
                k->w = 3 + wall_rng(&r) * 5;
                k->d = 0.9f + wall_rng(&r) * 1.1f;
                k->e = 0.6f + 0.5f * wall_rng(&r);
            } else if (g < 0.8f) {
                k->w = 1.4f + wall_rng(&r) * 1.6f;
                k->d = 1.8f + wall_rng(&r) * 0.7f;
                k->e = 0.9f + 0.8f * wall_rng(&r);
            } else {
                k->w = 8 + wall_rng(&r) * 6;
                k->d = 1.0f + wall_rng(&r) * 0.7f;
                k->sk = sk * 1.4f;
                k->e = 0.5f + 0.3f * wall_rng(&r);
                k->j = j * 1.3f;
            }
        }
        R->nn = nn;
        n++;
        float low = R->cy + fminf((0 - cx) * t, (SCREEN_W - cx) * t) + R->half; // the lowest the bottom edge gets at the screen's two sides
        if (low >= SCREEN_H + 16) {
            float over = low - (SCREEN_H + 16); // what is left over is spread as extra overlap
            for (int j = 1; j < n; j++) rows[j].cy -= over * j / (n - 1);
            break;
        }
    }
    return n;
}

// ---- the catches (b4Catches) and the roll's progress with them (b4ShownAt, b4Sim, b4Fx)
static void plan_catches(Row *R)
{
    R->nc = 0;
    if (R->crimp_on) R->c[R->nc++] = (Catch){clamp01((R->sx + 2 * R->sk + 10) / R->L1), M_FX, 0, 1};
    WallRng g;
    wall_rng_seed(&g, R->seed ^ 0xCA7C4u);
    float f = wall_rng(&g);
    int want = f < M_SNAG0 ? 0 : f < M_SNAG1 ? 1 : 2, snags = 0;
    for (int tries = 0; tries < 24 && snags < want && R->nc < 3; tries++) {
        float p = 0.12f + 0.78f * wall_rng(&g), st = (0.45f + 0.5f * wall_rng(&g)) * M_FX;
        int near = 0;
        for (int i = 0; i < R->nc; i++)
            if (fabsf(R->c[i].p - p) < 0.16f) near = 1;
        if (near) continue;
        R->c[R->nc++] = (Catch){p, st, 0, 0};
        snags++;
    }
    for (int i = 1; i < R->nc; i++) // by where they are along the strip
        for (int j = i; j > 0 && R->c[j].p < R->c[j - 1].p; j--) {
            Catch tmp = R->c[j];
            R->c[j] = R->c[j - 1];
            R->c[j - 1] = tmp;
        }
}

static float shown_at(const Row *R, float tau)
{
    float u = clamp01(tau / R->dur), sh = u;
    for (int i = 0; i < R->nc; i++) {
        const Catch *c = &R->c[i];
        if (c->tau > tau || u <= c->p) continue;
        float ms = (tau - c->tau) * 1000;
        if (ms >= M_HOLD + M_EASE) continue;
        float lim = ms < M_HOLD ? c->p : c->p + (u - c->p) * wall_smooth((ms - M_HOLD) / M_EASE);
        if (lim < sh) sh = lim;
    }
    return sh;
}

static void simulate(Row *R)
{
    for (int i = 0; i < R->nc; i++) R->c[i].tau = 1e9f;
    R->d_end = R->dur + 1;
    for (int k = 0; k < (int)((R->dur + 1) * 1000); k++) {
        float tau = k * 0.001f, sh = shown_at(R, tau);
        for (int i = 0; i < R->nc; i++)
            if (R->c[i].tau > 1e8f && sh >= R->c[i].p - 1e-6f) R->c[i].tau = tau;
        if (sh >= 1 - 1e-6f) {
            R->d_end = tau;
            break;
        }
    }
}

static int fx_at(const Row *R, float tau, Fx *o)
{
    int any = 0;
    memset(o, 0, sizeof *o);
    for (int i = 0; i < R->nc; i++) {
        const Catch *c = &R->c[i];
        float ms = (tau - c->tau) * 1000;
        if (!(ms >= 0 && ms < M_WIN)) continue;
        float s = c->s, decay = (1 - ms / M_WIN) * (1 - ms / M_WIN), back = 0;
        if (ms < 55) back = wall_smooth(ms / 55);
        else if (ms < 170) back = 1 - wall_smooth((ms - 55) / 115);
        float pk = ms < 40 ? ms / 40 : 1 - wall_smooth((ms - 40) / (M_WIN - 40));
        o->back = fmaxf(o->back, 3.5f * s * back);
        o->shiver += 1.3f * s * sinf(ms * 0.176f) * decay;
        o->shiver_deg += 2 * s * sinf(ms * 0.176f + 1) * decay;
        o->pucker = fmaxf(o->pucker, s * pk);
        any = 1;
    }
    return any;
}

// ---- making a row (b4Make): on a worker thread, so nothing here may touch the interface
static void mirror(PaperSpr *p)
{
    for (int y = 0; y < p->h; y++) {
        uint32_t *row = (uint32_t *)(p->px + (size_t)y * p->w * 4);
        for (int a = 0, b = p->w - 1; a < b; a++, b--) {
            uint32_t t = row[a];
            row[a] = row[b];
            row[b] = t;
        }
    }
}

static void make_row(Row *R)
{
    WallTapeOpt o;
    wall_tape_defaults(&o);
    o.L1 = R->L1;
    o.h1 = (float)R->th1;
    o.k = WK;
    o.kind = R->kind;
    o.colour = R->colour;
    o.seed = R->seed;
    o.crimp = R->crimp_r < M_CRIMP;
    o.nnicks = R->nn;
    memcpy(o.nicks, R->nicks, sizeof o.nicks);
    // firm and clear (B4P): more of the tape's own colour against a light host, patches and edge light cut back, little brown in the dark patches, few faint fibres
    o.strength = 1;
    o.alpha = 0.88f + (R->pressed - 0.5f) * 0.06f;
    o.host[0] = 236 / 255.0f;
    o.host[1] = 232 / 255.0f;
    o.host[2] = 222 / 255.0f;
    o.press = 0.12f;
    o.patch = 0.8f;
    o.edge = 0.85f;
    o.edge_w = 0.8f;
    o.dark_tint = 0.3f;
    o.ridge = M_RIDGE;
    o.wrinkles = 0;
    o.jog = M_JOG;
    o.seal = 1;
    o.fib_n = 0.6f;
    o.fib_l = 0.55f;
    o.fib_d = 0.25f;
    o.fib_len = 0.8f;
    o.fib_soft = 0.45f * WK;
    WallTape t;
#ifdef UI_INTRO_PROF
    double m0 = prof_ms();
#endif
    if (wall_tape_make(&o, &t) != 0) return;
#ifdef UI_INTRO_PROF
    double m1 = prof_ms();
#endif
    if (R->dir < 0) mirror(t.spr);
    R->strip = t.spr;
    R->pad = t.pad;
    R->crimp_on = t.crimp;
    R->sx = t.sx;
    R->sk = t.sk;
    R->ky = t.ky;
    R->kr = t.kr;
    R->Lpx = roundf(R->L1 * WK);
    R->Hpx = R->th1 * WK;
    R->ang = R->tilt * D2R;
#ifdef UI_INTRO_PROF
    double m2 = prof_ms();
#endif
    R->turned = wall_turned(R->strip, R->ang, 16, 2.4f, 6, 0x1E1608, 0.4f);
    R->runs = wall_runs(R->turned);
#ifdef UI_INTRO_PROF
    double m3 = prof_ms();
#endif
    R->r0 = 24.0f + (float)(R->th1 % 5);
    R->r1 = fmaxf(15, R->th1 / 2.0f + 3);
    R->rb = R->th1 + 4.0f;
    wall_roll_phase(R->L1, R->r0, R->r1, R->ph);
    R->circ0 = WALL_ROLL_CIRC * R->r0;
    R->tex = wall_roll_tex(R->colour, R->kind, WK, R->rb, R->circ0);
#ifdef UI_INTRO_PROF
    printf("  intro row: tape %.1f ms, mirror %.1f, turned %.1f, roll texture %.1f\n", m1 - m0, m2 - m1, m3 - m2, prof_ms() - m3);
#endif
    uint32_t tc = paper_tape_rgb[R->colour & 7]; // the back of washi is a pale version of its front
    R->back[0] = roundf(((tc >> 16) & 255) + (255 - ((tc >> 16) & 255)) * 0.72f);
    R->back[1] = roundf(((tc >> 8) & 255) + (255 - ((tc >> 8) & 255)) * 0.72f);
    R->back[2] = roundf((tc & 255) + (255 - (tc & 255)) * 0.72f);
    plan_catches(R);
    for (int g = 0; g < 4; g++) { // a roll held up for too long would still be on its way when the paper lands: the last snag goes
        simulate(R);
        if (R->d_end - R->dur <= T_BUDGET) break;
        int idx = -1;
        for (int i = 0; i < R->nc; i++)
            if (!R->c[i].crimp) idx = i;
        if (idx < 0) break;
        for (int i = idx; i < R->nc - 1; i++) R->c[i] = R->c[i + 1];
        R->nc--;
    }
    R->ok = R->strip && R->turned && R->tex;
}

#define SH_RGB 0x140E04u   // the card's shadows' colour
#define GLOW_RGB PAL(mark_ink, 0x2F3A8Fu) // the seal's ink, blooming (Paper night's seal is the moon's cream)

static uint8_t *alpha_plane(const PaperSpr *p) // (a picture in one colour: only its alpha is kept)
{
    uint8_t *a = malloc((size_t)p->w * p->h);
    if (a)
        for (size_t i = 0, n = (size_t)p->w * p->h; i < n; i++) a[i] = p->px[i * 4 + 3];
    return a;
}

static void make_card(void)
{
    const CardDef *d = &g_cards[I.card];
    int w = 0, h = 0;
    I.card_px = gfx_decode_png(d->png, d->n, &w, &h);
    if (!I.card_px) return;
    I.cw = w;
    I.ch = h;
    I.lx = d->lx;
    I.ly = d->ly;
    // its shadows (bakeShadowOnly of the page's card, which is 1/0.6 times this size): lying on the wall, and lifted while it comes down and is pulled away
    PaperSpr *r = wall_shadow_only(I.card_px, w, h, 54, 3.0f, 8.4f, 7, SH_RGB, 0.5f), *l = wall_shadow_only(I.card_px, w, h, 54, 9.6f, 26.4f, 19, SH_RGB, 0.42f);
    if (r && l) {
        I.shw = r->w;
        I.shh = r->h;
        I.sh_rest = alpha_plane(r);
        I.sh_lift = alpha_plane(l);
    }
    wall_spr_free(r);
    wall_spr_free(l);
    // the card's solid middle (where both shadows were taken out entirely): the run of opaque pixels across its middle row, then as many rows up and down as are
    // opaque all along it; in the shadows' texels (54 px of room round the card)
    int my = h / 2, a = w / 2, b = w / 2;
    const uint8_t *row = I.card_px + (size_t)my * w * 4;
    while (a > 0 && row[(a - 1) * 4 + 3] == 255) a--;
    while (b < w && row[b * 4 + 3] == 255) b++;
    int c = my, e = my + 1;
    for (int y = my - 1, ok = 1; y >= 0 && ok; y--) {
        for (int x = a; x < b && ok; x++) ok = I.card_px[((size_t)y * w + x) * 4 + 3] == 255;
        if (ok) c = y;
    }
    for (int y = my + 1, ok = 1; y < h && ok; y++) {
        for (int x = a; x < b && ok; x++) ok = I.card_px[((size_t)y * w + x) * 4 + 3] == 255;
        if (ok) e = y + 1;
    }
    if (b - a > 4 && e - c > 4) {
        I.sh_hole[0] = 54 + a;
        I.sh_hole[1] = 54 + c;
        I.sh_hole[2] = 54 + b;
        I.sh_hole[3] = 54 + e;
    }
}

static void run_jobs(void *arg)
{
    (void)arg;
    for (;;) {
        int j = atomic_fetch_add(&I.next, 1);
        if (j >= I.njobs) break;
#ifdef UI_INTRO_PROF
        double j0 = prof_ms();
#endif
        if (j < I.nrows) make_row(&I.rows[j]);
        else make_card();
#ifdef UI_INTRO_PROF
        printf("  intro job %d (%s): %.1f ms\n", j, j < I.nrows ? "a row" : "the card", prof_ms() - j0);
#endif
        atomic_fetch_add(&I.done, 1);
    }
}

void ui_intro_prepare(void)
{
    if (g_ui_intro_ncards <= 0 || I.state) return;
    uint32_t seed = g_ui_intro_seed;
    if (!seed) { // (this start's own; on the console this runs before the interface is set up, so its clock is read directly)
#ifdef __SWITCH__
        uint64_t tk = armGetSystemTick();
#else
        uint64_t tk = G.plat ? G.plat->now_ms(G.plat) : 0;
#endif
        seed = ((uint32_t)tk ^ (uint32_t)(tk >> 32)) * 2654435761u ^ (uint32_t)time(NULL) * 2246822519u;
        if (!seed) seed = 1;
    }
    I.seed = seed;
    I.card = g_ui_intro_card = ui_intro_pick(seed, g_ui_intro_ncards);
    I.nrows = plan_rows(seed, I.rows);
    I.njobs = I.nrows + 1;
    atomic_init(&I.next, 0);
    atomic_init(&I.done, 0);
    I.state = 1;
#ifdef __SWITCH__
    // the strips are made on the console's other two cores while the app reads the SD card; whatever is left when it plays, the main thread helps with
    for (int i = 0; i < 2; i++)
        if (R_SUCCEEDED(threadCreate(&I.th[I.nth], run_jobs, NULL, NULL, 0x20000, 0x2C, i + 1))) {
            if (R_SUCCEEDED(threadStart(&I.th[I.nth]))) I.nth++;
            else threadClose(&I.th[I.nth]);
        }
#endif
}

static void wait_jobs(void)
{
    run_jobs(NULL); // (helps until there is nothing left to start)
#ifdef __SWITCH__
    for (int i = 0; i < I.nth; i++) {
        threadWaitForExit(&I.th[i]);
        threadClose(&I.th[i]);
    }
    I.nth = 0;
#endif
}

// ------------------------------------------------------------------------------------------------ the home screen, in pieces (buildHome)

static PaperSpr *cut(const uint8_t *layer, int x, int y, int w, int h)
{
    PaperSpr *p = wall_spr_new(w, h);
    if (!p) return NULL;
    for (int j = 0; j < h; j++) memcpy(p->px + (size_t)j * w * 4, layer + ((size_t)(y + j) * SCREEN_W + x) * 4, (size_t)w * 4);
    p->ox = x; // (where it is on the screen)
    p->oy = y;
    return p;
}

// a screen-sized picture, NULL if there is no memory for it; not cleared (every one here is filled before it is read: zeroing them all cost the console tens of ms
// at each start), and without surf_new's "still working" pump, so another thread may make one
static Surf *screen_new(void)
{
    Surf *s = malloc(sizeof *s);
    if (!s) return NULL;
    s->w = SCREEN_W;
    s->h = SCREEN_H;
    if (!(s->px = malloc((size_t)SCREEN_W * SCREEN_H * 4))) {
        free(s);
        return NULL;
    }
    return s;
}

static float hat(float p, int k, int n)
{
    if (k == 0 && p < 0) return 1;
    if (k == n - 1 && p > n - 1) return 1;
    float d = fabsf(p - k);
    return d < 1 ? 1 - d : 0;
}

static int build_home(UiHome *h)
{
    Surf *black = screen_new(), *white = screen_new();
    I.home = screen_new();
    I.board = screen_new();
    uint8_t *layer = malloc((size_t)SCREEN_W * SCREEN_H * 4);
    if (!black || !white || !I.home || !I.board || !layer) {
        surf_free(black);
        surf_free(white);
        free(layer);
        return -1;
    }
    UiHomeGeo geo;
#ifdef UI_INTRO_PROF
    double b0 = prof_ms();
#endif
    ui_home_capture(h, I.home, black, white, &geo);
#ifdef UI_INTRO_PROF
    double b1 = prof_ms();
#endif
    Cx bc = ui_cx(I.board);
    skin_board_plain(&bc);
    // what is on the board, with its alpha: drawn on black, a = 1 - (white - black), the colour is black / a
    for (size_t i = 0, n = (size_t)SCREEN_W * SCREEN_H; i < n; i++) {
        const uint8_t *b = black->px + i * 4, *w = white->px + i * 4;
        float a = 1 - ((w[0] - b[0]) + (w[1] - b[1]) + (w[2] - b[2])) / (3 * 255.0f);
        a = clamp01(a);
        uint8_t *o = layer + i * 4;
        if (a < 1 / 255.0f) {
            o[0] = o[1] = o[2] = o[3] = 0;
            continue;
        }
        for (int c = 0; c < 3; c++) {
            float v = b[c] / a;
            o[c] = (uint8_t)(v > 255 ? 255 : v + 0.5f);
        }
        o[3] = (uint8_t)(a * 255 + 0.5f);
    }
    surf_free(black);
    surf_free(white);
#ifdef UI_INTRO_PROF
    printf("  intro home: capture (three renders) %.1f ms, board and mattes %.1f ms\n", b1 - b0, prof_ms() - b1);
#endif
    // the top (the corner bar, the loaded profile's card, the search row), the A-Z rail, the footer, plain cut-outs
    I.top = (Piece){cut(layer, 0, 0, SCREEN_W, 296), 640, 148};
    I.rail = (Piece){cut(layer, 1190, 296, 90, 356), 1235, 474};
    I.footer = (Piece){cut(layer, 0, 652, SCREEN_W, 68), 640, 686};
    // The tiles share the rest by smooth weights (sharpened, v^16) that add up to one everywhere, so the pieces put back together are the picture. A tile's weight is
    // its hat across (along its row) times its hat down (along its column): the two are kept apart, and a pixel only asks the tiles whose hat across is not 0 in its
    // column (a couple in each row).
    const int TX = 0, TY = 296, TW2 = 1190, TH2 = 356, N = geo.ntiles;
    I.ntiles = N;
    int ncol[7] = {0};
    for (int i = 0; i < N; i++) ncol[geo.col[i]] = geo.row[i] + 1 > ncol[geo.col[i]] ? geo.row[i] + 1 : ncol[geo.col[i]];
    enum { MAXC = 8 };
    float *hx = malloc((size_t)(N ? N : 1) * TW2 * sizeof(float)), *hy = malloc((size_t)(N ? N : 1) * TH2 * sizeof(float));
    int8_t *cand = malloc((size_t)TW2 * MAXC);
    if (!hx || !hy || !cand) {
        free(hx);
        free(hy);
        free(cand);
        free(layer);
        return -1;
    }
    for (int i = 0; i < N; i++) {
        for (int x = 0; x < TW2; x++) hx[(size_t)i * TW2 + x] = hat((x + TX - geo.x0) / geo.pitch_x, geo.col[i], geo.row_n[geo.row[i]]);
        for (int y = 0; y < TH2; y++) hy[(size_t)i * TH2 + y] = hat((y + TY - geo.y0) / geo.pitch_y, geo.row[i], ncol[geo.col[i]]);
    }
    for (int x = 0; x < TW2; x++) {
        int nc = 0;
        for (int i = 0; i < N && nc < MAXC; i++)
            if (hx[(size_t)i * TW2 + x] > 0) cand[(size_t)x * MAXC + nc++] = (int8_t)i;
        if (nc < MAXC) cand[(size_t)x * MAXC + nc] = -1;
    }
// the weights at (x, y) of the tiles asked there: w[j] for tile id[j], j < n
#define WEIGHTS(x, y, id, w, n)                                                                       \
    do {                                                                                              \
        float sum_ = 0;                                                                               \
        n = 0;                                                                                        \
        for (int j_ = 0; j_ < MAXC && cand[(size_t)(x) * MAXC + j_] >= 0; j_++) {                     \
            int i_ = cand[(size_t)(x) * MAXC + j_];                                                   \
            float v_ = hx[(size_t)i_ * TW2 + (x)] * hy[(size_t)i_ * TH2 + (y)];                       \
            v_ *= v_;                                                                                 \
            v_ *= v_;                                                                                 \
            v_ *= v_;                                                                                 \
            v_ *= v_;                                                                                 \
            id[n] = i_;                                                                               \
            w[n++] = v_;                                                                              \
            sum_ += v_;                                                                               \
        }                                                                                             \
        for (int j_ = 0; j_ < n; j_++) w[j_] = sum_ > 0 ? w[j_] / sum_ : 0;                           \
    } while (0)
    // each tile's box (where its weight is over .003), then its piece
    int bx0[14], by0[14], bx1[14], by1[14];
    for (int i = 0; i < N; i++) {
        bx0[i] = TW2;
        by0[i] = TH2;
        bx1[i] = by1[i] = -1;
    }
    for (int y = 0; y < TH2; y++)
        for (int x = 0; x < TW2; x++) {
            int id[MAXC], n;
            float w[MAXC];
            WEIGHTS(x, y, id, w, n);
            for (int j = 0; j < n; j++)
                if (w[j] > 0.003f) {
                    int i = id[j];
                    if (x < bx0[i]) bx0[i] = x;
                    if (x > bx1[i]) bx1[i] = x;
                    if (y < by0[i]) by0[i] = y;
                    if (y > by1[i]) by1[i] = y;
                }
        }
    for (int i = 0; i < N; i++) {
        I.tiles[i] = (Piece){NULL, geo.cx[i], geo.cy[i]};
        if (bx1[i] < 0) continue;
        PaperSpr *p = wall_spr_new(bx1[i] - bx0[i] + 1, by1[i] - by0[i] + 1);
        if (!p) continue;
        p->ox = TX + bx0[i];
        p->oy = TY + by0[i];
        I.tiles[i].spr = p;
    }
    for (int y = 0; y < TH2; y++)
        for (int x = 0; x < TW2; x++) {
            int id[MAXC], n;
            float w[MAXC];
            WEIGHTS(x, y, id, w, n);
            const uint8_t *s = layer + ((size_t)(y + TY) * SCREEN_W + x + TX) * 4;
            for (int j = 0; j < n; j++) {
                PaperSpr *p = I.tiles[id[j]].spr;
                if (!p) continue;
                int lx = x + TX - p->ox, ly = y + TY - p->oy;
                if (lx < 0 || ly < 0 || lx >= p->w || ly >= p->h) continue;
                uint8_t *d = p->px + ((size_t)ly * p->w + lx) * 4;
                d[0] = s[0];
                d[1] = s[1];
                d[2] = s[2];
                d[3] = (uint8_t)(s[3] * w[j] + 0.5f);
            }
        }
#undef WEIGHTS
    free(hx);
    free(hy);
    free(cand);
    free(layer);
    // the order the tiles come down in (B4_HOME.tiles.order: every other one, then the ones between)
    int half = (N + 1) / 2;
    for (int i = 0; i < N; i++) I.order[i] = i < half ? 2 * i : 2 * (i - half) + 1;
    return 0;
}

// a piece of the home screen: alpha a, moved by dy, grown by s and turned by r degrees about its point
static void piece_draw(Surf *s, const Piece *p, float a, float dx, float dy, float sc, float rdeg)
{
    if (!p->spr || a <= 0.003f) return;
    const PaperSpr *q = p->spr;
    if (sc == 1 && rdeg == 0) { // only moved: on whole pixels (gfx_blit_xform's plain blit, faded), which is much quicker than sampling between them
        gfx_blit_xform(s, q->px, q->w, q->h, roundf(q->ox + dx) + q->w * 0.5f, roundf(q->oy + dy) + q->h * 0.5f, 1, 0, a);
        return;
    }
    float cx = q->ox + q->w * 0.5f, cy = q->oy + q->h * 0.5f, rx = cx - p->px, ry = cy - p->py, ca = cosf(rdeg * D2R), sa = sinf(rdeg * D2R);
    float X = p->px + dx + (rx * ca - ry * sa) * sc, Y = p->py + dy + (rx * sa + ry * ca) * sc;
    gfx_blit_xform(s, q->px, q->w, q->h, X, Y, sc, rdeg * D2R, a);
}

static float board_a(float t) { return e_smooth(prog(t, H_BOARD0, H_BOARD1)); }

static void draw_board(Surf *s, float t)
{
    float b = board_a(t);
    if (b >= 1) surf_copy(s, I.board);
    else if (b > 0) gfx_lerp(s, s, I.board, (int)(b * 256 + 0.5f));
}

static void draw_pieces(Surf *s, float t)
{
    if (t < H_TILE0) return;
    float u = prog(t, H_TILE0, 3.14f);
    piece_draw(s, &I.top, e_out2(u), 0, -20 * (1 - e_out3(u)), 1, 0);
    u = prog(t, 3.24f, 3.49f);
    piece_draw(s, &I.rail, e_out2(u), 0, 0, 1, 0);
    u = prog(t, 3.29f, 3.59f);
    piece_draw(s, &I.footer, e_out2(u), 0, 16 * (1 - e_out3(u)), 1, 0);
    for (int i = 0; i < I.ntiles; i++) {
        float t0 = H_TILE0 + I.order[i] * H_STEP, v = prog(t, t0, t0 + H_TDUR), e = e_back(v, 1.4f), sg = i % 2 ? 1.0f : -1.0f;
        piece_draw(s, &I.tiles[i], clamp01(v * 3.2f), 0, 34 * (1 - e), 1 + 0.05f * (1 - e), sg * 3 * (1 - e));
    }
}

// ------------------------------------------------------------------------------------------------ drawing the wall (b4Draw, b4Laid, b4Roll, b4Pucker)

static void turned_xy(const Row *R, int *x, int *y)
{
    *x = (int)lroundf(R->cx - R->turned->w * 0.5f) + R->turned->ox;
    *y = (int)lroundf(R->cy - R->turned->h * 0.5f) + R->turned->oy;
}

// the roll's middle on the screen and its turn (roll_local: past the pressed-down place the far half runs along its own line, so the roll follows the jog)
static WallRoll roll_at(const Row *R, float p, const Fx *fx)
{
    float len = R->L1 * clamp01(p), dr = (float)R->dir, Rr = lerpf(R->r0, R->r1, clamp01(p));
    float lx = len, ly = 0, extra = 0, phi = wall_roll_phi(R->ph, p);
    if (R->crimp_on && len > R->sx) {
        float d = len - R->sx + 1, kk = R->kr * D2R;
        lx = R->sx - 1 + d * cosf(kk);
        ly = R->ky + d * sinf(kk);
        extra = kk;
    }
    float X = dr * (lx - R->L1 / 2) * WK, Y = ly * WK;
    if (fx) { // pulled back along the strip and shivering sideways and in its turn
        float ca = cosf(extra), sa = sinf(extra);
        X += dr * (-ca * fx->back - sa * fx->shiver) * WK;
        Y += (-sa * fx->back + ca * fx->shiver) * WK;
        phi -= fx->back / Rr;
        extra += fx->shiver_deg * D2R;
    }
    float cs = cosf(R->ang), sn = sinf(R->ang);
    WallRoll o = {R->cx + cs * X - sn * Y, R->cy + sn * X + cs * Y, R->ang + dr * extra, WK, Rr, R->rb, phi, R->circ0, 1, 1, R->dir < 0};
    return o;
}

// the pucker (ridge_over): a light, dark, light ridge across the strip a hair behind the roll, 10 px (1x) wide, on the tape only
static void pucker(Surf *s, const Row *R, float p, float a)
{
    float laid = R->L1 * p, Rr = lerpf(R->r0, R->r1, p), kk = R->kr * D2R;
    int useb = R->crimp_on && laid > R->sx + 2 * R->sk + 10;
    float pc = (useb ? laid - R->sx + 1 : laid) - Rr - 5, lx = useb ? R->sx - 1 + pc * cosf(kk) : pc, X = R->dir * (lx - R->L1 / 2) * WK;
    int bw = (int)ceilf(10 * WK) + 4, sw = R->strip->w, sh = R->strip->h, x0 = (int)lroundf(X - bw / 2.0f + sw / 2.0f);
    if (x0 < 0 || x0 + bw > sw) return;
    uint8_t *b = malloc((size_t)bw * sh * 4);
    if (!b) return;
    static const float ST[5] = {0, 0.24f, 0.52f, 0.78f, 1}, SC[5][4] = {{255, 255, 255, 0}, {255, 255, 255, 0.95f}, {69, 48, 26, 0.46f}, {255, 255, 255, 0.6f}, {255, 255, 255, 0}};
    float mid = X - (x0 - sw / 2.0f), W10 = 10 * WK;
    for (int x = 0; x < bw; x++) {
        float g = (x + 0.5f - (mid - W10 / 2)) / W10, col4[4] = {0, 0, 0, 0};
        if (R->dir < 0) g = 1 - g;
        if (g > 0 && g < 1) {
            int k = 0;
            while (k < 3 && g > ST[k + 1]) k++;
            float f = (g - ST[k]) / (ST[k + 1] - ST[k]), a0 = SC[k][3], a1 = SC[k + 1][3];
            col4[3] = lerpf(a0, a1, f);
            for (int c = 0; c < 3; c++) col4[c] = col4[3] > 0 ? lerpf(SC[k][c] * a0, SC[k + 1][c] * a1, f) / col4[3] : 0;
        }
        for (int y = 0; y < sh; y++) {
            const uint8_t *sp = R->strip->px + ((size_t)y * sw + x0 + x) * 4;
            uint8_t *d = b + ((size_t)y * bw + x) * 4;
            d[0] = (uint8_t)col4[0];
            d[1] = (uint8_t)col4[1];
            d[2] = (uint8_t)col4[2];
            d[3] = (uint8_t)(col4[3] * sp[3] + 0.5f);
        }
    }
    float bxc = x0 + bw / 2.0f - sw / 2.0f, cs = cosf(R->ang), sn = sinf(R->ang);
    gfx_blit_xform(s, b, bw, sh, R->cx + cs * bxc, R->cy + sn * bxc, 1, R->ang, clamp01(a * 0.7f));
    free(b);
}

// the strip so far: its finished picture cut off square across the strip at the roll
static void laid(Surf *s, const Row *R, float p, const Fx *fx)
{
    if (p <= 0) return;
    float e = R->dir * (p * R->Lpx - R->Lpx / 2), cs = cosf(R->ang), sn = sinf(R->ang);
    int x, y;
    turned_xy(R, &x, &y);
    if (R->dir > 0) wall_blit_half(s, R->turned, R->runs, x, y, R->cx, R->cy, cs, sn, e);
    else wall_blit_half(s, R->turned, R->runs, x, y, R->cx, R->cy, -cs, -sn, -e);
    if (fx && fx->pucker > 0.01f) pucker(s, R, p, fx->pucker);
}

#ifdef UI_INTRO_PROF
double g_rowprof[2];
#endif
static int g_roll_keying;            // the rolls still rolling out are drawn as their shapes alone in a key colour each (ink_land: where each shows)
static uint32_t g_roll_key[MAXROW];  // (by row)
static void row_draw(Surf *s, const Row *R, float t, WallScratch *sc)
{
    float tau = t - R->t0;
    if (tau < 0 || !R->ok) return;
    if (t >= R->rip_at) { // torn off from the end it was begun at, the end curling over
        float u = prog(t, R->rip_at, R->rip_at + T_RIPDUR);
        if (u >= 1) return;
        float e = powf(u, 1.6f), ex = R->crimp_on ? ceilf(fabsf(R->ky) * WK) + 6 : 6;
        int right = R->dir < 0, x, y;
        turned_xy(R, &x, &y);
        WallBox box = {R->strip, R->pad, (int)lroundf(R->strip->h / 2.0f - (R->Hpx / 2 + ex)), (int)R->Lpx, (int)lroundf(R->Hpx + 2 * ex), right};
        float cs = cosf(R->ang), sn = sinf(R->ang), hl = R->Lpx / 2;
        WallPeel o;
        memset(&o, 0, sizeof o);
        o.x0 = right ? R->cx + cs * hl : R->cx - cs * hl;
        o.y0 = right ? R->cy + sn * hl : R->cy - sn * hl;
        o.ang = right ? R->ang + PI_F : R->ang;
        o.L = R->Lpx;
        o.sc = 1;
        o.lf = 30 + (R->Lpx - 30) * e;
        o.psi = 0.6f + 0.35f * e + 0.06f * (float)((int)(R - I.rows) % 3);
        o.roll = fmaxf(12, WK * 5);
        o.vis = 60 + WK * 22;
        o.fade = 90;
        o.alpha = 1;
        memcpy(o.back, R->back, sizeof o.back);
        o.baked = R->turned;
        o.runs = R->runs;
        o.bx = x;
        o.by = y;
        wall_peel(s, &box, &o);
        return;
    }
    if (tau < R->d_end) { // being laid
        float sh = shown_at(R, tau);
        Fx fx;
        int has = fx_at(R, tau, &fx);
#ifdef UI_INTRO_PROF
        double q0 = prof_ms();
#endif
        laid(s, R, sh, has ? &fx : NULL);
#ifdef UI_INTRO_PROF
        double q1 = prof_ms();
#endif
        WallRoll rr = roll_at(R, sh, has ? &fx : NULL);
        if (g_roll_keying) {
            rr.keyed = 1;
            rr.key = g_roll_key[R - I.rows];
        }
        wall_roll_draw(s, R->tex, &rr, sc);
#ifdef UI_INTRO_PROF
        g_rowprof[0] += q1 - q0;
        g_rowprof[1] += prof_ms() - q1;
#endif
        return;
    }
    int x, y;
    turned_xy(R, &x, &y);
    wall_put(s, R->turned, R->runs, x, y);
}

// ------------------------------------------------------------------------------------------------ the card and the logo made on it (b4Paper, b4CardLogo, b4LayPiece)

// the card's frame on the stage at t: its middle, turn and size (1 = at rest)
static void card_frame(float t, float *cx, float *cy, float *rot, float *sc)
{
    float pull = e_in3(prog(t, T_PULL0, T_PULL1));
    *cx = 640 + 160 * pull;
    *cy = 360 - 840 * pull;
    *rot = (-0.8f - 9 * pull) * D2R;
    *sc = 1 + 0.1f * pull;
}

// where a point of the card's frame (stage px from its middle at rest) is on the screen
static void card_pt(float t, float x, float y, float *ox, float *oy)
{
    float cx, cy, rot, sc;
    card_frame(t, &cx, &cy, &rot, &sc);
    float ca = cosf(rot), sa = sinf(rot);
    *ox = cx + (x * ca - y * sa) * sc;
    *oy = cy + (x * sa + y * ca) * sc;
}

static void paper_draw(Surf *s, float t)
{
    float pa = prog(t, T_PAPER0, T_PAPER1), land = e_in2(pa), pull = e_in3(prog(t, T_PULL0, T_PULL1));
    if (pa <= 0 || pull >= 1 || !I.card_px) return;
    float cx, cy, rot, sc;
    card_frame(t, &cx, &cy, &rot, &sc);
    float k = lerpf(1.18f, 1, land) * sc, lm = clamp01(1 - land + 0.5f * pull), a = clamp01(pa * 8);
    if (I.sh_rest && a * (1 - lm) > 0.003f) wall_blit_shade(s, I.sh_rest, I.shw, I.shh, cx, cy, k, rot, a * (1 - lm), SH_RGB, I.sh_hole[2] ? I.sh_hole : NULL);
    if (I.sh_lift && a * lm > 0.003f) wall_blit_shade(s, I.sh_lift, I.shw, I.shh, cx, cy, k, rot, a * lm, SH_RGB, I.sh_hole[2] ? I.sh_hole : NULL);
    gfx_blit_xform(s, I.card_px, I.cw, I.ch, cx, cy, k, rot, a);
}

// how high the name's strip is above the card at s (0 its left end, 1 its right end) while a thumb presses it on from the left (layZ, no ring)
static float lay_z(float t, float s)
{
    const float hover = 110, air = 90;
    float f = (t - T_TOUCH) / T_TDUR;
    if (f < 0) {
        float e = e_out2(prog(t, T_TOUCH - T_FALL, T_TOUCH)), top = lerpf(hover, air, e), tilt = e_smooth(prog(t, T_TOUCH - T_FALL * 0.7f, T_TOUCH));
        return top * lerpf(1, s, tilt);
    }
    if (f < 1) {
        float ahead = clamp01((s - f) / (1 - f + 1e-4f));
        return air * powf(1 - f, 0.7f) * ahead;
    }
    return 0;
}

// the letters of "Washi" across the name's picture, as the page has them at 3.2 times the corner's size (WRITE_X), and their weights in the time it takes to write
static const float WRITE_X[6] = {88, 192, 268, 336, 410, 448}, WRITE_W[5] = {0.3f, 0.2f, 0.17f, 0.2f, 0.13f};

// The name as written by t, into m (the name's picture where it shows, clear elsewhere): each letter is uncovered by a pen sweeping across it from its bottom left
// (an edge leaning right at the top), one after the other. The page's clip: for each letter begun, the quad (x0, 60) (min(xr + sl, lim), 60) (min(xr - sl, lim), 230)
// (x0, 230) of its 3.2 times picture; here f times that, with the pen's edge a pixel soft.
static void write_mask(const PaperSpr *tx, float t, uint8_t *m)
{
    const float f = LOGO_K / 3.2f;
    float tot = 0, acc = 0;
    for (int j = 0; j < 5; j++) tot += WRITE_W[j];
    memset(m, 0, (size_t)tx->w * tx->h * 4);
    for (int j = 0; j < 5; j++) {
        float j0 = T_W0 + (T_W1 - T_W0) * acc / tot;
        acc += WRITE_W[j];
        float u = prog(t, j0, T_W0 + (T_W1 - T_W0) * acc / tot);
        if (u <= 0) continue;
        float x0 = (WRITE_X[j] - 4) * f, lim = (WRITE_X[j + 1] + 32) * f, xr = lerpf(x0, lim, u), sl = 36 * f, ya = 60 * f, yb = 230 * f;
        float xt = fminf(xr + sl, lim), xb = fminf(xr - sl, lim);
        for (int y = (int)ya; y < (int)ceilf(yb) && y < tx->h; y++) {
            float xe = lerpf(xt, xb, (y + 0.5f - ya) / (yb - ya)); // the pen's edge on this row
            for (int x = x0 < 0 ? 0 : (int)x0; x < (int)ceilf(xe) && x < tx->w; x++) {
                size_t q = ((size_t)y * tx->w + x) * 4;
                uint8_t a = (uint8_t)(tx->px[q + 3] * clamp01(xe - x) + 0.5f);
                if (a <= m[q + 3]) continue; // (neighbouring letters' quads overlap a little)
                memcpy(m + q, tx->px + q, 3);
                m[q + 3] = a;
            }
        }
    }
}

// the logo's pieces, in order: 0 the name's strip, 1 the name, 2 the seal; how many of them are done by t, to lie on the card as they are for good (rest_cache)
// once the card's seal starts coming down (what is shown), a press no longer skips: the intro plays out (the user: "locked out of skipping once the intro's
// stamp starts playing")
#define T_NOSKIP (T_SEAL - 0.12f)

static int logo_done(float t) { return (t >= T_TOUCH + T_TDUR) + (t >= T_W1) + (t >= T_SEAL + 0.4f); }

// the logo on the card at t: its pieces from `from` up to (not with) `to`
static void logo_draw(Surf *s, float t, int from, int to)
{
    float pull = e_in3(prog(t, T_PULL0, T_PULL1));
    if (pull >= 1 || t < T_TOUCH - T_FALL || !I.blank) return;
    float K = LOGO_K, tpx = I.lx + (TITLE_CX - LC_X) * K, tpy = I.ly + (TITLE_CY - LC_Y) * K, spx = I.lx + (SEAL_CX - LC_X) * K, spy = I.ly + (SEAL_CY - LC_Y) * K;
    float cx, cy, rot, sc;
    card_frame(t, &cx, &cy, &rot, &sc);
    float done = T_TOUCH + T_TDUR, f = K / 3.2f; // (f: the page's sprites were baked at 3.2 times; ours at 2.5)
#define PIECE(i) (from <= (i) && (i) < to)
    // 1 the name's strip: laid by a thumb in the stage's own frame while the card is still, then part of the card
    if (t < done && I.lay && PIECE(0)) {
        float e = e_out2(prog(t, T_TOUCH - T_FALL, T_TOUCH)), fade = clamp01((t - (T_TOUCH - T_FALL)) / 0.07f), ang = -0.8f * D2R + 0.08f * (1 - e);
        float bx0, by0;
        card_pt(t, tpx + 2 * f, tpy + 14 * f, &bx0, &by0);
        bx0 += -22 * (1 - e);
        by0 += -26 * (1 - e);
        enum { NS = 18 };
        WallPt pts[NS + 1];
        WallGround gr[NS + 1];
        float L = (float)I.lay->w, ca = cosf(ang), sa = sinf(ang), zm = 0;
        for (int i = 0; i <= NS; i++) {
            float u = (float)i / NS, d = (u - 0.5f) * L, bx = bx0 + ca * d, by = by0 + sa * d, z = lay_z(t, u), X, Y, S;
            wall_lifted(bx, by, z, &X, &Y, &S);
            pts[i] = (WallPt){X, Y, S, u * L, 1 - 0.5f * clamp01(z / (90 * 0.6f)), z, 0, 0};
            gr[i] = (WallGround){bx + z * 0.30f, by + z * 0.62f, z};
            if (z > zm) zm = z;
        }
        wall_ribbon_shadow(s, gr, NS + 1, I.lay->h * 0.4f, 0.2f * fade * clamp01(zm / 14), 3);
        WallBox box = {I.lay, 0, 0, I.lay->w, I.lay->h, 0};
        static const float noback[3] = {255, 255, 255};
        wall_ribbon(s, &box, pts, NS + 1, 1, ang, fade, noback, 1);
    }
    float ca = cosf(rot), sa = sinf(rot);
#define AT(px, py, X, Y) (X = cx + ((px) * ca - (py) * sa) * sc, Y = cy + ((px) * sa + (py) * ca) * sc)
    float X, Y;
    if (t >= done && PIECE(0)) {
        AT(tpx, tpy, X, Y);
        gfx_blit_xform(s, I.blank->px, I.blank->w, I.blank->h, X, Y, sc, rot, 1);
    }
    // 2 the name, written (write_mask)
    if (t > T_W0 && I.text && PIECE(1)) {
        const PaperSpr *tx = I.text;
        AT(tpx, tpy, X, Y);
        if (t >= T_W1 && I.written) gfx_blit_xform(s, I.written, tx->w, tx->h, X, Y, sc, rot, 1);
        else {
            uint8_t *m = malloc((size_t)tx->w * tx->h * 4);
            if (m) {
                write_mask(tx, t, m);
                gfx_blit_xform(s, m, tx->w, tx->h, X, Y, sc, rot, 1);
                free(m);
            }
        }
    }
    // 3 the seal is stamped: it comes down from a little bigger and turned, the ink blooms round it for a moment
    if (t >= T_SEAL - 0.12f && I.seal && PIECE(2)) {
        float sd = prog(t, T_SEAL - 0.12f, T_SEAL), bloom = 1 - prog(t, T_SEAL, T_SEAL + 0.4f), ks = lerpf(1.45f, 1, e_in2(sd)), r2 = lerpf(-10, 0, e_in2(sd)) * D2R;
        AT(spx, spy, X, Y);
        if (bloom > 0 && t > T_SEAL && I.glow) wall_blit_shade(s, I.glow, I.gw, I.gh, X, Y, sc * ks, rot + r2, 0.5f * bloom, GLOW_RGB, NULL);
        gfx_blit_xform(s, I.seal->px, I.seal->w, I.seal->h, X, Y, sc * ks, rot + r2, clamp01(sd * 3));
    }
#undef AT
#undef PIECE
}

// ------------------------------------------------------------------------------------------------ a frame (drawB4)

// ---- a drawer's kept pictures (Ctx)
// Black and the rows laid by t, in order: the first wall_n of them, every one done by t (a row after one still going is drawn on top each frame).
static Surf *wall_cache(Ctx *c, float t)
{
    if (!c->c_wall) return NULL;
    if (c->wall_n < 0 || t < c->wall_t) {
        gfx_fill(c->c_wall, col(0, 0, 0, 1));
        c->wall_n = 0;
    }
    c->wall_t = t;
    while (c->wall_n < I.nrows && t >= I.rows[c->wall_n].t0 + I.rows[c->wall_n].d_end) row_draw(c->c_wall, &I.rows[c->wall_n++], t, &c->sc);
    return c->c_wall;
}

// The whole wall with the card resting on it and the logo's pieces done by t (rest_lv of them): for t between the card's landing and its pull.
static Surf *rest_cache(Ctx *c, float t)
{
    Surf *w = wall_cache(c, t);
    if (!w || c->wall_n < I.nrows || !c->c_rest) return NULL;
    int lv = logo_done(t);
    if (c->rest_lv < 0 || t < c->rest_t || lv < c->rest_lv) {
        surf_copy(c->c_rest, w);
        paper_draw(c->c_rest, t);
        c->rest_lv = 0;
    }
    if (lv > c->rest_lv) logo_draw(c->c_rest, t, c->rest_lv, lv); // (the pieces done since, each as it now lies for good)
    c->rest_lv = lv;
    c->rest_t = t;
    return c->c_rest;
}

// The board with the rows still whole while the others are torn off: the first k (they go newest first).
static Surf *under_cache(Ctx *c, int k)
{
    if (!c->c_under) return NULL;
    if (c->under_k != k) {
        surf_copy(c->c_under, I.board);
        for (int i = 0; i < k; i++) {
            int x, y;
            turned_xy(&I.rows[i], &x, &y);
            wall_put(c->c_under, I.rows[i].turned, I.rows[i].runs, x, y);
        }
        c->under_k = k;
    }
    return c->c_under;
}


// a drawer, made on the main thread (surf_new may run the interface's "still working" hook); its frame is a must, its kept pictures are not
static int ctx_init(Ctx *c)
{
    memset(c, 0, sizeof *c);
    if (!(c->frame = screen_new())) return -1;
    c->c_wall = screen_new();
    c->c_rest = screen_new();
    c->c_under = screen_new();
    c->wall_n = c->rest_lv = c->under_k = -1;
    return 0;
}

static void ctx_free(Ctx *c)
{
    surf_free(c->frame);
    surf_free(c->c_wall);
    surf_free(c->c_rest);
    surf_free(c->c_under);
    wall_scratch_free(&c->sc);
    memset(c, 0, sizeof *c);
}

static void corner_draw(Surf *s, float t)
{
    float cs = prog(t, I.corner_at, I.corner_at + 0.15f);
    if (cs <= 0 || !I.mark1 || !I.title1) return;
    float a = clamp01(cs * 3), e = e_in2(cs);
    const PaperSpr *m = I.mark1, *n = I.title1; // (where the top bar puts them: their anchors at (42, 9) and (112, 12); each grows and turns about its middle)
    gfx_blit_xform(s, m->px, m->w, m->h, 42 - m->ox + m->w * 0.5f, 9 - m->oy + m->h * 0.5f, lerpf(1.3f, 1, e), lerpf(-6, 0, e) * D2R, a);
    gfx_blit_xform(s, n->px, n->w, n->h, 112 - n->ox + n->w * 0.5f, 12 - n->oy + n->h * 0.5f, lerpf(1.15f, 1, e), 0, a);
}

#ifdef UI_INTRO_PROF // (PC builds only: how long frames take to draw in each part of the intro, and each step of a frame, printed at its end)
enum { NPH = 5, NPART = 6 };
static double prof_sum[NPH], prof_max[NPH], prof_psum[NPH][NPART], prof_mark;
static int prof_n[NPH], prof_ph;
static int prof_phase(float t) { return t < 1.25f ? 0 : t < T_RIP0 ? 1 : t < H_TILE0 ? 2 : t < T_SETTLE ? 3 : 4; }
static void prof_begin(float t)
{
    prof_ph = prof_phase(t);
    prof_mark = prof_ms();
}
static void prof_part(int k)
{
    double now = prof_ms();
    prof_psum[prof_ph][k] += now - prof_mark;
    prof_mark = now;
}
static void prof_add(float t, double ms)
{
    int k = prof_phase(t);
    prof_sum[k] += ms;
    prof_n[k]++;
    if (ms > prof_max[k]) prof_max[k] = ms;
}
extern double g_wall_peel_ms[4];
static void prof_report(void)
{
    extern double g_roll_ms[3];
    printf("  laying: laid parts %.1f ms, rolls %.1f ms (shadow %.1f, body %.1f, its blit %.1f) (all frames)\n", g_rowprof[0], g_rowprof[1], g_roll_ms[0], g_roll_ms[1],
           g_roll_ms[2]);
    memset(g_roll_ms, 0, sizeof g_roll_ms);
    g_rowprof[0] = g_rowprof[1] = 0;
    printf("  peels: stuck part %.1f ms, shadow %.1f ms, flap %.1f ms, set-up %.1f ms (all frames)\n", g_wall_peel_ms[0], g_wall_peel_ms[1], g_wall_peel_ms[2],
           g_wall_peel_ms[3]);
    memset(g_wall_peel_ms, 0, sizeof g_wall_peel_ms);
    static const char *const NAME[NPH] = {"laying the wall", "card and logo", "tearing it off", "home coming in", "settling"};
    for (int k = 0; k < NPH; k++)
        if (prof_n[k])
            printf("  intro %-16s %3d frames  mean %5.2f ms  max %5.2f ms  (base %.2f corner %.2f rows %.2f card %.2f logo %.2f settle %.2f)\n", NAME[k], prof_n[k],
                   prof_sum[k] / prof_n[k], prof_max[k], prof_psum[k][0] / prof_n[k], prof_psum[k][1] / prof_n[k], prof_psum[k][2] / prof_n[k],
                   prof_psum[k][3] / prof_n[k], prof_psum[k][4] / prof_n[k], prof_psum[k][5] / prof_n[k]);
    memset(prof_sum, 0, sizeof prof_sum);
    memset(prof_max, 0, sizeof prof_max);
    memset(prof_psum, 0, sizeof prof_psum);
    memset(prof_n, 0, sizeof prof_n);
}
#define PT(k) prof_part(k)
#else
#define PT(k) ((void)0)
#endif

// the frame at t, into c->frame (only what is shared is read, and c's own pictures written: two of these may run at once)
static void draw_frame(Ctx *c, float t)
{
#ifdef UI_INTRO_PROF
    prof_begin(t);
#endif
    Surf *s = c->frame;
    if (t < T_RIP0) { // the wall going up over the black, then whole (nothing under it shows)
        Surf *r = t >= T_PAPER1 && t < T_PULL0 ? rest_cache(c, t) : NULL;
        if (r) {
            surf_copy(s, r);
            PT(0);
            logo_draw(s, t, c->rest_lv, 3);
            PT(4);
        } else {
            Surf *w = wall_cache(c, t);
            if (w) surf_copy(s, w);
            else gfx_fill(s, col(0, 0, 0, 1));
            PT(0);
            for (int i = w ? c->wall_n : 0; i < I.nrows; i++) row_draw(s, &I.rows[i], t, &c->sc);
            PT(2);
            paper_draw(s, t);
            PT(3);
            logo_draw(s, t, 0, 3);
            PT(4);
        }
    } else { // torn off, newest row first, the home screen coming in on the board under it
        int k = 0;
        while (k < I.nrows && t < I.rows[k].rip_at) k++;
        // (with rows still whole the home screen has not begun to come in: what is under them is the bare board; once its pieces do come in under the last row,
        // that row is torn off too)
        Surf *u = (k == 0 || t < H_TILE0) && board_a(t) >= 1 ? under_cache(c, k) : NULL;
        if (u) {
            surf_copy(s, u);
            PT(0);
            if (k == 0) {
                draw_pieces(s, t);
                corner_draw(s, t); // (on the board under the wall: it shows where the strips have gone)
            }
            PT(1);
            for (int i = k; i < I.nrows; i++) row_draw(s, &I.rows[i], t, &c->sc);
        } else {
            gfx_fill(s, col(0, 0, 0, 1));
            draw_board(s, t);
            draw_pieces(s, t);
            PT(0);
            corner_draw(s, t);
            PT(1);
            for (int i = 0; i < I.nrows; i++) row_draw(s, &I.rows[i], t, &c->sc);
        }
        PT(2);
        paper_draw(s, t);
        PT(3);
        logo_draw(s, t, 0, 3);
        PT(4);
    }
    float u = prog(t, T_SETTLE, T_SETTLE + 0.14f); // the last frames land on the home screen itself
    if (u >= 1) surf_copy(s, I.home);
    else if (u > 0) gfx_lerp(s, s, I.home, (int)(u * 256 + 0.5f));
    PT(5);
}

// ------------------------------------------------------------------------------------------------ a skip: from the frame it was skipped on to the home screen

// 0 a cut, 1 the frame torn off in strips (the wall's own tearing), 2 the frame pulled away like the card, 3 the seal stamped and sent to its corner (the user's
// pick of the three mockups; 1 and 2 stay for the mockups' scenario)
int g_ui_intro_skip_style = 3;
int g_ui_intro_skip_frames; // tests: frames the last skip showed
int g_ui_intro_skip_fall;   // tests: of them, the seal coming down over the intro as it played on
int g_ui_intro_skip_down;   // tests: 1 if the seal was down before the home screen was waited for
int g_ui_intro_late;        // tests: presses that came too late to skip it (the card's seal coming down or later)
enum { SK_N = 8 };
static const float SK_RIP = 0.24f, SK_GAP = 0.024f; // 1: each strip's tearing, and how long after the one above the next begins
static const float SK_PULL = 0.34f;                  // 2: the pull
// 3: the seal is down (over the intro, still playing; then it waits there while the home screen is got ready), the frame gives way to the home screen, the seal
// flies to the corner, the name's strip is stamped beside it, then onto the home screen itself
static const float SK_BIG = 1.75f; // the seal in the middle, times its size on the card (the user: "about 1.75x"); it shrinks into the corner's mark as it flies
static const float SK_FALL = 0.14f, SK_FADE = 0.12f, SK_FLY0 = 0.20f, SK_FLY1 = 0.38f, SK_TITLE0 = 0.34f, SK_TITLE1 = 0.46f, SK_SETTLE = 0.04f;
static struct {
    int style;
    const Surf *from;
    PaperSpr *band[SK_N]; // 1: the frame cut into strips, ragged along their long edges (anchor 0, 0 at (0, by))
    int16_t *runs[SK_N];
    int by[SK_N];
    uint8_t *sh; // 2: the sheet's shadow, a plane of alpha drawn 8 times bigger
    int shw, shh, sh_hole[4];
    Surf *bare; // 3: the home screen without its corner (the corner is the seal's)
} K;

static float skip_len(int style)
{
    return style == 1 ? SK_RIP + (SK_N - 1) * SK_GAP : style == 2 ? SK_PULL : style == 3 ? SK_TITLE1 + SK_SETTLE : 0;
}

static void skip_end(void)
{
    for (int i = 0; i < SK_N; i++) {
        wall_spr_free(K.band[i]);
        free(K.runs[i]);
    }
    free(K.sh);
    surf_free(K.bare);
    memset(&K, 0, sizeof K);
}

// how far a torn edge comes in at x (0..1): two octaves of value noise
static float sk_edge(int x, uint32_t seed) { return 0.65f * wall_vnoise(x / 42.0f, 0.5f, seed) + 0.35f * wall_vnoise(x / 11.0f, 7.5f, seed + 1); }

// what a skip draws, made from the frame it came on (0, or -1: then it is a cut)
static int skip_begin(const Surf *from, int style)
{
    memset(&K, 0, sizeof K);
    K.style = style;
    K.from = from;
    if (style == 1) {
        // Eight strips, each 2 M deeper than its share above and below; its top and bottom edges come in by 0.15..1.85 M, so where two meet one of them always covers
        // the line between them, and the one torn away leaves the other's ragged edge, with a pale rim like torn paper
        const int H = SCREEN_H / SK_N;
        const float M = 7;
        for (int i = 0; i < SK_N; i++) {
            int y0 = i ? i * H - (int)(2 * M) : 0, y1 = i < SK_N - 1 ? (i + 1) * H + (int)(2 * M) : SCREEN_H, h = y1 - y0;
            PaperSpr *p = wall_spr_new(SCREEN_W, h);
            if (!p) return -1;
            p->ox = p->oy = 0;
            memcpy(p->px, from->px + (size_t)y0 * SCREEN_W * 4, (size_t)h * SCREEN_W * 4);
            for (int x = 0; x < SCREEN_W; x++) {
                float et = i ? i * H - M * (0.15f + 1.7f * sk_edge(x, 900 + i * 2)) - y0 : -9, eb = i < SK_N - 1 ? (i + 1) * H + M * (0.15f + 1.7f * sk_edge(x, 901 + i * 2)) - y0 : h + 9;
                for (int y = 0; y < h; y++) {
                    float d = fminf(y + 0.5f - et, eb - (y + 0.5f)); // px inside the nearer edge
                    if (d >= 2) {
                        if (y > 2 * M + 2 && y < h - 2 * M - 3) y = h - (int)(2 * M) - 3; // (nothing to do in the middle)
                        continue;
                    }
                    uint8_t *q = p->px + ((size_t)y * SCREEN_W + x) * 4;
                    float rim = 0.5f * clamp01(1 - d / 2);
                    q[0] = (uint8_t)lerpf(q[0], 246, rim);
                    q[1] = (uint8_t)lerpf(q[1], 241, rim);
                    q[2] = (uint8_t)lerpf(q[2], 230, rim);
                    q[3] = (uint8_t)(clamp01(d + 0.5f) * 255 + 0.5f);
                }
            }
            K.band[i] = p;
            K.runs[i] = wall_runs(p);
            K.by[i] = y0;
        }
    } else if (style == 2) {
        // the sheet's shadow at an eighth of the size: the screen's rectangle blurred by 3 (24 px), 6 of room all round; its middle is under the sheet for sure
        enum { SC = 8, P = 6 };
        int w = SCREEN_W / SC + 2 * P, h = SCREEN_H / SC + 2 * P;
        float *a = calloc((size_t)w * h, sizeof(float)), *tmp = malloc((size_t)w * h * sizeof(float));
        K.sh = a && tmp ? malloc((size_t)w * h) : NULL;
        if (K.sh) {
            for (int y = P; y < h - P; y++)
                for (int x = P; x < w - P; x++) a[(size_t)y * w + x] = 1;
            wall_blur_alpha(a, tmp, w, h, 3);
            for (size_t i = 0, n = (size_t)w * h; i < n; i++) K.sh[i] = (uint8_t)(clamp01(a[i]) * 255 + 0.5f);
            K.shw = w;
            K.shh = h;
            K.sh_hole[0] = P + 10;
            K.sh_hole[1] = P + 10;
            K.sh_hole[2] = w - P - 10;
            K.sh_hole[3] = h - P - 10;
        }
        free(a);
        free(tmp);
        if (!K.sh) return -1;
    } else if (style == 3) {
        if (!I.seal || !I.mark1 || !I.title1 || !(K.bare = screen_new())) return -1;
        surf_copy(K.bare, I.board);
        piece_draw(K.bare, &I.top, 1, 0, 0, 1, 0);
        piece_draw(K.bare, &I.rail, 1, 0, 0, 1, 0);
        piece_draw(K.bare, &I.footer, 1, 0, 0, 1, 0);
        for (int i = 0; i < I.ntiles; i++) piece_draw(K.bare, &I.tiles[i], 1, 0, 0, 1, 0);
    } else
        return -1;
    return 0;
}

// ---- 3: the seal's ink on what it lands on. It is printed, not laid on top (the user: it "reads as a layer floating on top"): on tape or the card the ink is
// multiplied into them, so their pattern and shading show through it, and just below a step up to a strip, the card or the name's strip lying over it the stamp
// barely reaches, so the ink thins there, a little unevenly. Worked out once, at the frame it lands on (that frame stays under it while the home screen is got ready).
// The rolls still rolling out stand on the tape: the print goes under them (ink_land draws the frame it lands on again: the wall without them, the print, then
// them). As the seal lifts to fly off, the print fades out under it.
static struct {
    uint8_t *m; // per pixel of the box: 0 nothing under it (black: the ink goes on as it is), else 1 + how much of the ink reaches it (0..254)
    int x0, y0, w, h;
    Surf *landed; // the frame it landed on with the seal printed into it
} INK;
// below a step the ink thins: to INK_FLOOR of itself right at it, all back INK_REACH px away (the user: the breaks were "too much" when the ink went missing)
enum { INK_REACH = 4 };
static const float INK_FLOOR = 0.6f;
// How much of the print is the ink's own colour laid on, the rest being multiplied into what is under it (the pattern showing through). A third of it always (the
// user: the stamp was too transparent), and most of it when the ink is lighter than what it lands on (Paper night's cream seal on its tape: a multiplied cream
// is darker than the tape and all but disappears).
static const float INK_BODY = 0.35f, INK_BODY_LIGHT = 0.9f;

static float ink_body(const float *ink, const float *under)
{
    const float li = 0.3f * ink[0] + 0.59f * ink[1] + 0.11f * ink[2], lu = 0.3f * under[0] + 0.59f * under[1] + 0.11f * under[2];
    return lerpf(INK_BODY, INK_BODY_LIGHT, clamp01((li - lu) / 40.0f));
}

static void ink_free(void)
{
    free(INK.m);
    surf_free(INK.landed);
    memset(&INK, 0, sizeof INK);
}

// how much of the seal is still printed at t (0 before it is down, and when there is no print)
static float ink_w(float t) { return INK.landed && t >= SK_FALL ? 1 - e_smooth(prog(t, SK_FALL + 0.01f, SK_FLY0)) : 0; }

// the surfaces under the seal at the intro's time ft, as heights (0 black, 1 + i the i-th row laid, nrows + 1 the card), into INK
static void ink_make(float ft)
{
    ink_free();
    const PaperSpr *sl = I.seal;
    if (!sl) return;
    const int P = INK_REACH + 2; // (the heights are needed this far round the box, for the steps just outside it)
    float hw = sl->w * 0.5f * SK_BIG, hh = sl->h * 0.5f * SK_BIG;
    int x0 = imax(0, (int)floorf(640 - hw) - 1), y0 = imax(0, (int)floorf(360 - hh) - 1);
    int x1 = imin(SCREEN_W, (int)ceilf(640 + hw) + 1), y1 = imin(SCREEN_H, (int)ceilf(360 + hh) + 1);
    int hx0 = imax(0, x0 - P), hy0 = imax(0, y0 - P), hx1 = imin(SCREEN_W, x1 + P), hy1 = imin(SCREEN_H, y1 + P), HW = hx1 - hx0, HH = hy1 - hy0;
    uint8_t *H = calloc((size_t)HW * HH, 1);
    INK.m = malloc((size_t)(x1 - x0) * (y1 - y0));
    if (!H || !INK.m) {
        free(H);
        ink_free();
        return;
    }
    INK.x0 = x0;
    INK.y0 = y0;
    INK.w = x1 - x0;
    INK.h = y1 - y0;
    // the rows laid so far, each over the ones before it: where the tape itself is (its picture, torn ends and jog included; not its shadow), only as far as it
    // has been rolled out
    for (int i = 0; i < I.nrows && i < 248; i++) {
        const Row *R = &I.rows[i];
        float tau = ft - R->t0;
        if (!R->ok || tau < 0 || ft >= R->rip_at) continue;
        float p = tau < R->d_end ? shown_at(R, tau) : 1, cs = cosf(R->ang), sn = sinf(R->ang), e = R->dir * (p * R->Lpx - R->Lpx / 2);
        if (p <= 0) continue;
        int tx, ty;
        turned_xy(R, &tx, &ty);
        const PaperSpr *T = R->turned;
        int sx0 = tx - T->ox, sy0 = ty - T->oy;
        for (int y = hy0; y < hy1; y++)
            for (int x = hx0; x < hx1; x++) {
                int u = x - sx0, v = y - sy0;
                if (u < 0 || v < 0 || u >= T->w || v >= T->h || T->px[((size_t)v * T->w + u) * 4 + 3] < 160) continue;
                float along = (x + 0.5f - R->cx) * cs + (y + 0.5f - R->cy) * sn; // (laid from its start up to the roll)
                if (p < 1 && (R->dir > 0 ? along >= e : -along >= -e)) continue;
                H[(size_t)(y - hy0) * HW + (x - hx0)] = (uint8_t)(1 + i);
            }
    }
    // the card, once it is down
    if (ft >= T_PAPER1 && ft < T_PULL0 && I.card_px) {
        float cx, cy, rot, sc;
        card_frame(ft, &cx, &cy, &rot, &sc);
        float ca = cosf(rot), sa = sinf(rot);
        for (int y = hy0; y < hy1; y++)
            for (int x = hx0; x < hx1; x++) {
                float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
                int u = (int)floorf((dx * ca + dy * sa) / sc + I.cw * 0.5f), v = (int)floorf((-dx * sa + dy * ca) / sc + I.ch * 0.5f);
                if (u >= 0 && v >= 0 && u < I.cw && v < I.ch && I.card_px[((size_t)v * I.cw + u) * 4 + 3] >= 128) H[(size_t)(y - hy0) * HW + (x - hx0)] = 250;
            }
        // and the name's strip on it, once it is pressed down (logo_draw's piece 0: the tape, not its clear room or shadow)
        const PaperSpr *b = I.blank;
        if (b && ft >= T_TOUCH + T_TDUR) {
            float K = LOGO_K, tpx = I.lx + (TITLE_CX - LC_X) * K, tpy = I.ly + (TITLE_CY - LC_Y) * K;
            float bx = cx + (tpx * ca - tpy * sa) * sc, by = cy + (tpx * sa + tpy * ca) * sc;
            for (int y = hy0; y < hy1; y++)
                for (int x = hx0; x < hx1; x++) {
                    float dx = x + 0.5f - bx, dy = y + 0.5f - by;
                    int u = (int)floorf((dx * ca + dy * sa) / sc + b->w * 0.5f), v = (int)floorf((-dx * sa + dy * ca) / sc + b->h * 0.5f);
                    if (u >= 0 && v >= 0 && u < b->w && v < b->h && b->px[((size_t)v * b->w + u) * 4 + 3] >= 160) H[(size_t)(y - hy0) * HW + (x - hx0)] = 251;
                }
        }
    }
    // how far each pixel is from a higher surface, within reach; the ink comes back over INK_REACH px, the edge wavering a little
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) {
            int h = H[(size_t)(y - hy0) * HW + (x - hx0)];
            uint8_t *o = &INK.m[(size_t)(y - y0) * INK.w + (x - x0)];
            if (!h) {
                *o = 0;
                continue;
            }
            int best = (INK_REACH + 1) * (INK_REACH + 1);
            for (int dy = -INK_REACH; dy <= INK_REACH; dy++) {
                int yy = y + dy;
                if (yy < hy0 || yy >= hy1 || dy * dy >= best) continue;
                const uint8_t *row = H + (size_t)(yy - hy0) * HW;
                for (int dx = -INK_REACH; dx <= INK_REACH; dx++) {
                    int xx = x + dx, d2 = dx * dx + dy * dy;
                    if (xx >= hx0 && xx < hx1 && d2 < best && row[xx - hx0] > h) best = d2;
                }
            }
            float d = sqrtf((float)best) + 1.0f * (wall_vnoise(x / 7.0f, y / 7.0f, 0x51A1u) - 0.5f);
            float g = lerpf(INK_FLOOR, 1, e_smooth(clamp01((d - 0.5f) / (INK_REACH - 0.5f))));
            *o = (uint8_t)(1 + lroundf(g * 254));
        }
    free(H);
}

// ---- how the seal's ink meets a roll still rolling out where it comes down (the user: printed under it "isn't right either. it needs to warp around it
// accurately"). A roll is drawn as a cylinder seen from straight above (wall_roll_draw): radius rho along the strip, its axis across it, a point at angle th from
// its top showing rho sin th from its middle line, its pattern laid round it by arc length (rho th) and the light laid over that. Mockups of the ways the
// stamp's rubber can meet it (g_ui_intro_wrap):
//   0 under it: printed on the tape only, the roll over the print
//   1 a label on a can: the tape's print as it is; on the roll the design wraps the cylinder, s = rho (2 / pi) th (meets the tape's print at its sides)
//   2 wrapped like its own pattern: the rubber touches the roll's top line first and wraps outward without slipping, s = rho th (as its pattern is: a wider slice
//     of the seal, pressed together toward its sides); beside the roll the rubber stretches down to the tape (the print there drawn out and thinner)
//   3 held up by it (a stiff stamp): only the roll's crown takes ink (not warped), and the tape only some way off it
//   4 soft, straight down: every point of the rubber comes down where it is, so seen from above the design is not warped; on the roll the ink thins toward its
//     sides (the same ink over more of its surface: cos th)
// In every way the roll's own light lies over the ink, as over its pattern. The frame it lands on is the one shown with the ink worked into it pixel by pixel:
// tape and card where they show, each roll where it shows (found by drawing the frame twice more with the rolls as flat keys: whatever is drawn over a roll is
// the same in both and drops out).
int g_ui_intro_wrap = 4;
int g_ui_intro_film_wraps; // tests: the films are of the ways above instead of the skip's styles

typedef struct { float x, y, ca, sa, inv, hw, hh, rad; int row; } RollBody; // a roll's body on the screen: middle, turn, 1 / scale, half its size, corners
static RollBody g_rb[MAXROW];
static int g_nrb;

// the rolls still rolling out at ft
static void rolls_at(float ft)
{
    g_nrb = 0;
    for (int i = 0; i < I.nrows; i++) { // (as row_draw draws them)
        const Row *R = &I.rows[i];
        float tau = ft - R->t0;
        if (!R->ok || tau < 0 || tau >= R->d_end || ft >= R->rip_at) continue;
        Fx fx;
        int has = fx_at(R, tau, &fx);
        WallRoll rr = roll_at(R, shown_at(R, tau), has ? &fx : NULL);
        float sc = rr.scale > 0 ? rr.scale : 1;
        g_rb[g_nrb++] = (RollBody){rr.x, rr.y, cosf(rr.ang), sinf(rr.ang), 1 / sc, rr.R * rr.k, rr.rb * rr.k / 2, 3 * rr.k, i};
    }
}

// a screen point in a roll's own frame: u along the strip (across its axis), v along its axis
static void roll_local(const RollBody *b, float x, float y, float *u, float *v)
{
    float dx = x - b->x, dy = y - b->y;
    *u = (dx * b->ca + dy * b->sa) * b->inv;
    *v = (-dx * b->sa + dy * b->ca) * b->inv;
}

// and back: the screen point at (u, v) in its frame
static void roll_screen(const RollBody *b, float u, float v, float *x, float *y)
{
    float sc = 1 / b->inv;
    *x = b->x + (u * b->ca - v * b->sa) * sc;
    *y = b->y + (u * b->sa + v * b->ca) * sc;
}

// how far a point (in its frame) is outside the body (0 inside)
static float roll_out(const RollBody *b, float u, float v)
{
    float du = fmaxf(0, fabsf(u) - b->hw), dv = fmaxf(0, fabsf(v) - b->hh);
    return sqrtf(du * du + dv * dv);
}

// the seal's colour and ink at a point of its design, given where the design lies on the screen as it comes straight down (bilinear, premultiplied); 0 if none
static int seal_at(float qx, float qy, float c[4])
{
    const PaperSpr *sl = I.seal;
    float u = (qx - 640) / SK_BIG + sl->w * 0.5f - 0.5f, v = (qy - 360) / SK_BIG + sl->h * 0.5f - 0.5f;
    int iu = (int)floorf(u), iv = (int)floorf(v);
    if (iu < -1 || iv < -1 || iu >= sl->w || iv >= sl->h) return 0;
    float fu = u - iu, fv = v - iv;
    c[0] = c[1] = c[2] = c[3] = 0;
    for (int k = 0; k < 4; k++) {
        int su = iu + (k & 1), sv = iv + (k >> 1);
        if (su < 0 || sv < 0 || su >= sl->w || sv >= sl->h) continue;
        const uint8_t *p = sl->px + ((size_t)sv * sl->w + su) * 4;
        float wt = ((k & 1) ? fu : 1 - fu) * ((k >> 1) ? fv : 1 - fv), a = p[3] / 255.0f * wt;
        c[0] += p[0] * a;
        c[1] += p[1] * a;
        c[2] += p[2] * a;
        c[3] += a;
    }
    if (c[3] < 0.002f) return 0;
    for (int ch = 0; ch < 3; ch++) c[ch] /= c[3]; // (straight colour, c[3] its ink)
    return 1;
}

// 2: how far the design on the tape at (u, v) beside a roll is pushed out along the strip by what went round the roll: rho (pi / 2 - 1) at its sides (where
// the wrapped design ends), nothing 1.2 rho off them; past its ends it fades toward its middle line (no push there: the rubber went past the end unwrapped)
static float wrap_push(const RollBody *b, float u, float v)
{
    float au = fabsf(u), L = 1.2f * b->hw, dv = fmaxf(0, fabsf(v) - b->hh), du = fmaxf(0, au - b->hw), d = sqrtf(du * du + dv * dv);
    if (au <= b->hw && fabsf(v) <= b->hh) return 0; // (on the roll)
    float p = b->hw * (PI_F / 2 - 1) * (1 - e_smooth(clamp01(d / L)));
    if (au < b->hw) p *= e_smooth(au / b->hw); // (past an end: toward the middle line)
    return u < 0 ? -p : p;
}

// the tape near the rolls: where its point of the design lies (*qx, *qy) and how much of the ink reaches it, by g_ui_intro_wrap
static float tape_map(float x, float y, float *qx, float *qy)
{
    float f = 1, sx = 0, sy = 0;
    for (int i = 0; i < g_nrb; i++) {
        const RollBody *b = &g_rb[i];
        float u, v;
        roll_local(b, x, y, &u, &v);
        float e = roll_out(b, u, v);
        if (g_ui_intro_wrap == 2 && e < 1.2f * b->hw) {
            // the rubber stretched from the roll's sides down to the tape: the design pushed out, and thinner as much as it is drawn out (ds / du)
            float push = wrap_push(b, u, v), ds = 1 + wrap_push(b, u + 0.5f, v) - wrap_push(b, u - 0.5f, v);
            sx += push * b->ca / b->inv;
            sy += push * b->sa / b->inv;
            f *= clamp01(ds);
        } else if (g_ui_intro_wrap == 3) // the stiff stamp reaches the tape only from 0.4 rho off the roll, all of it 1.6 rho off
            f *= e_smooth(clamp01((e - 0.4f * b->hw) / (1.2f * b->hw)));
    }
    *qx = x + sx;
    *qy = y + sy;
    return f;
}

// a roll's point at (u, v): where its point of the design lies and how much of the ink reaches it, by g_ui_intro_wrap (0: none)
static float roll_map(const RollBody *b, float u, float v, float *qx, float *qy)
{
    float q = clamp01(fabsf(u) / b->hw), th = asinf(q), s = q * b->hw, f = 1; // th: the point's angle from the top
    switch (g_ui_intro_wrap) {
    case 1: s = b->hw * th * (2 / PI_F); break;
    case 2: s = b->hw * th; break;
    case 3: f = e_smooth(clamp01((0.6f - q) / 0.25f)); break; // (the crown)
    case 4: f = sqrtf(1 - q * q); break;                      // (cos th)
    default: return 0;
    }
    roll_screen(b, u < 0 ? -s : s, v, qx, qy);
    return f;
}


// The frame the seal lands on at ft (shown: as the intro drew it), printed, into INK.landed (main thread, the drawers stopped: c's pictures are used; after
// ink_make). Up to three rolls under the seal get ink of their own (a channel each in the keyed drawing).
static void ink_land(Ctx *c, const Surf *shown, float ft)
{
    if (!INK.m || !shown || !(INK.landed = screen_new())) return;
    surf_copy(INK.landed, shown);
    rolls_at(ft);
    // the box worked on: the seal's bloom (wider than the seal)
    int bx0 = imax(0, 640 - (int)(I.gw * SK_BIG / 2) - 1), by0 = imax(0, 360 - (int)(I.gh * SK_BIG / 2) - 1);
    int bx1 = imin(SCREEN_W, 640 + (int)(I.gw * SK_BIG / 2) + 2), by1 = imin(SCREEN_H, 360 + (int)(I.gh * SK_BIG / 2) + 2), BW = bx1 - bx0, BH = by1 - by0;
    if (!I.glow) bx0 = INK.x0, by0 = INK.y0, bx1 = INK.x0 + INK.w, by1 = INK.y0 + INK.h, BW = INK.w, BH = INK.h;
    // the rolls reaching into the box, and where each shows: the frame drawn with them black and with each in a channel of its own
    int near[3], nn = 0;
    for (int i = 0; i < g_nrb && nn < 3; i++) {
        const RollBody *b = &g_rb[i];
        float r = sqrtf(b->hw * b->hw + b->hh * b->hh) / b->inv + 2;
        if (b->x + r > bx0 && b->x - r < bx1 && b->y + r > by0 && b->y - r < by1) near[nn++] = i;
    }
    float *A = NULL; // per pixel of the box, each near roll's share of what shows there
    if (nn) {
        Surf *k0 = screen_new(), *k1 = screen_new();
        A = k0 && k1 ? calloc((size_t)BW * BH * 3, sizeof(float)) : NULL;
        if (A) {
            Surf *keep = c->frame;
            memset(g_roll_key, 0, sizeof g_roll_key);
            g_roll_keying = 1;
            c->frame = k0;
            draw_frame(c, ft);
            for (int j = 0; j < nn; j++) g_roll_key[g_rb[near[j]].row] = 0xFF0000u >> (8 * j);
            c->frame = k1;
            draw_frame(c, ft);
            g_roll_keying = 0;
            c->frame = keep;
            for (int y = by0; y < by1; y++)
                for (int x = bx0; x < bx1; x++) {
                    const uint8_t *p0 = k0->px + ((size_t)y * SCREEN_W + x) * 4, *p1 = k1->px + ((size_t)y * SCREEN_W + x) * 4;
                    float *a = A + ((size_t)(y - by0) * BW + (x - bx0)) * 3;
                    for (int j = 0; j < nn; j++) a[j] = clamp01((p1[j] - p0[j]) / 255.0f);
                }
        }
        surf_free(k0);
        surf_free(k1);
        if (!A) nn = 0;
    }
    // the bloom, on the tape only: drawn over a copy, taken where the tape shows
    Surf *bl = I.glow ? surf_clone(shown) : NULL;
    if (bl) wall_blit_shade(bl, I.glow, I.gw, I.gh, 640, 360, SK_BIG, 0, 0.5f, GLOW_RGB, NULL);
    for (int y = by0; y < by1; y++)
        for (int x = bx0; x < bx1; x++) {
            const float *a = A ? A + ((size_t)(y - by0) * BW + (x - bx0)) * 3 : NULL;
            float rolls = 0;
            for (int j = 0; j < nn; j++) rolls += a[j];
            float tape = clamp01(1 - rolls), px = x + 0.5f, py = y + 0.5f;
            uint8_t *d = INK.landed->px + ((size_t)y * SCREEN_W + x) * 4;
            const uint8_t *g = bl ? bl->px + ((size_t)y * SCREEN_W + x) * 4 : d;
            float out[3] = {0, 0, 0};
            // the tape (and card, and black) where it shows: the bloom, then the ink multiplied in (laid on as it is over black), thinning at steps
            if (tape > 0) {
                float qx, qy, c4[4], f = tape_map(px, py, &qx, &qy), under[3] = {g[0], g[1], g[2]};
                int in = x >= INK.x0 && y >= INK.y0 && x < INK.x0 + INK.w && y < INK.y0 + INK.h;
                uint8_t m = in ? INK.m[(size_t)(y - INK.y0) * INK.w + (x - INK.x0)] : 1;
                if (f > 0.001f && seal_at(qx, qy, c4)) {
                    float on = m ? 1.0f : 0.0f, ia = c4[3] * f * (m ? (m - 1) / 254.0f : 1), body = ink_body(c4, under), ink[3];
                    for (int ch = 0; ch < 3; ch++) ink[ch] = lerpf(lerpf(c4[ch], c4[ch] * under[ch] / 255, on), c4[ch], body);
                    for (int ch = 0; ch < 3; ch++) under[ch] = lerpf(under[ch], ink[ch], ia);
                }
                for (int ch = 0; ch < 3; ch++) out[ch] += tape * under[ch];
            }
            // each roll where it shows: its point of the design, the ink worked in under its light (final - ink (final - light) (1 - colour))
            for (int j = 0; j < nn; j++) {
                if (a[j] <= 0) continue;
                const RollBody *b = &g_rb[near[j]];
                float u, v, qx, qy, c4[4], lt[4], col[3] = {d[0], d[1], d[2]};
                roll_local(b, px, py, &u, &v);
                float f = roll_map(b, u, v, &qx, &qy);
                if (f > 0.001f && seal_at(qx, qy, c4)) {
                    wall_roll_light((u + b->hw) / (2 * b->hw), lt);
                    float ia = c4[3] * f, body = ink_body(c4, col), shaded[3];
                    for (int ch = 0; ch < 3; ch++) shaded[ch] = fmaxf(0, col[ch] - ia * fmaxf(0, col[ch] - lt[ch]) * (1 - c4[ch] / 255));
                    for (int ch = 0; ch < 3; ch++) col[ch] = lerpf(shaded[ch], lerpf(col[ch], c4[ch], ia), body);
                }
                for (int ch = 0; ch < 3; ch++) out[ch] += a[j] * col[ch];
            }
            float sum = tape + rolls;
            for (int ch = 0; ch < 3; ch++) d[ch] = (uint8_t)lroundf(fminf(255, sum > 0 ? out[ch] / fmaxf(1, sum) : d[ch]));
        }
    surf_free(bl);
    free(A);
}

// tests: the intro's time between t0 and t1 when a roll's middle is nearest (x, y) (the mockups of the ink on a roll)
float ui_intro_roll_near(float x, float y, float t0, float t1)
{
    float best = 1e9f, at = t0;
    for (float t = t0; t <= t1; t += 1 / 120.0f) {
        rolls_at(t);
        for (int i = 0; i < g_nrb; i++) {
            float d = hypotf(g_rb[i].x - x, g_rb[i].y - y);
            if (d < best) best = d, at = t;
        }
    }
    g_nrb = 0;
    return at;
}

// 3: the seal over s, t seconds after the press: it comes down in the middle, its ink blooming, flies to the corner and shrinks into the corner's own mark, and the
// name's strip is stamped beside it as the intro's end does it. Until it is down (SK_FALL) it needs nothing but the seal, so it can come down over the intro as it
// plays, before the home screen (and the corner's mark and name with it) is ready.
static void skip_seal(Surf *s, float t)
{
    const PaperSpr *sl = I.seal;
    if (!sl) return;
    float sd = prog(t, SK_FALL - 0.12f, SK_FALL), ks = lerpf(1.45f, 1, e_in2(sd)), r2 = lerpf(-10, 0, e_in2(sd)) * D2R;
    float fl = e_smooth(prog(t, SK_FLY0, SK_FLY1)), X = 640, Y = 360, k = SK_BIG * ks, km = 1 / LOGO_K, swap = 0;
    const PaperSpr *m = fl > 0 ? I.mark1 : NULL; // (read only once it flies: until the home screen is ready, its thread is making them)
    if (m) {
        float mx = 42 - m->ox + m->w * 0.5f, my = 9 - m->oy + m->h * 0.5f;
        X = lerpf(640, mx, fl);
        Y = lerpf(360, my, fl) - 60 * sinf(PI_F * fl) * (1 - fl);
        k = lerpf(SK_BIG, km, fl) * ks;
        swap = prog(fl, 0.7f, 1);
    }
    float bloom = 1 - prog(t, SK_FALL, SK_FALL + 0.4f), w = ink_w(t); // (w: printed, in the frame under it; the seal itself shows as it lifts)
    if (bloom > 0 && t >= SK_FALL && I.glow && !INK.landed) wall_blit_shade(s, I.glow, I.gw, I.gh, X, Y, k, r2, 0.5f * bloom, GLOW_RGB, NULL);
    if (swap < 1 && w < 1) gfx_blit_xform(s, sl->px, sl->w, sl->h, X, Y, k, r2, clamp01(sd * 3) * (1 - swap) * (1 - w));
    if (swap > 0) gfx_blit_xform(s, m->px, m->w, m->h, X, Y, k / km, 0, swap);
    float cs = prog(t, SK_TITLE0, SK_TITLE1);
    const PaperSpr *n = cs > 0 ? I.title1 : NULL;
    if (n) gfx_blit_xform(s, n->px, n->w, n->h, 112 - n->ox + n->w * 0.5f, 12 - n->oy + n->h * 0.5f, lerpf(1.15f, 1, e_in2(cs)), 0, clamp01(cs * 3));
}

// the skip's frame t seconds after the press, into s
static void skip_draw(Surf *s, float t)
{
    if (K.style == 1) {
        static const float back[3] = {250, 248, 243};
        surf_copy(s, I.home);
        // what is still stuck, top to bottom (a strip's edges lie over the one above), then the flaps over all of it; all torn from the left, one after another
        for (int pass = 0; pass < 2; pass++)
            for (int i = 0; i < SK_N; i++) {
                float t0 = i * SK_GAP, u = prog(t, t0, t0 + SK_RIP);
                if (u >= 1 || !K.band[i]) continue;
                if (t < t0) {
                    if (!pass) wall_put(s, K.band[i], K.runs[i], 0, K.by[i]);
                    continue;
                }
                int right = 0, h = K.band[i]->h;
                float e = powf(u, 1.6f), L = SCREEN_W, lf = 30 + (L - 30) * e, ang = right ? PI_F : 0, x0 = right ? SCREEN_W : 0, y0 = K.by[i] + h * 0.5f;
                if (!pass) {
                    wall_blit_half(s, K.band[i], K.runs[i], 0, K.by[i], x0, y0, -cosf(ang), -sinf(ang), -lf);
                    continue;
                }
                WallBox box = {K.band[i], 0, 0, SCREEN_W, h, right};
                WallPeel o;
                memset(&o, 0, sizeof o);
                o.x0 = x0;
                o.y0 = y0;
                o.ang = ang;
                o.L = L;
                o.sc = 1;
                o.lf = lf;
                o.psi = 0.6f + 0.35f * e + 0.06f * (float)(i % 3);
                o.roll = WK * 5;
                o.vis = 110;
                o.fade = 80;
                o.alpha = 1;
                memcpy(o.back, back, sizeof o.back);
                wall_peel(s, &box, &o); // (no baked strip: its stuck part is drawn above)
            }
    } else if (K.style == 2) {
        // lifted a little, then pulled up and away as the card is (card_frame), quicker
        float lift = e_out2(prog(t, 0, 0.09f)), e = e_in3(prog(t, 0, SK_PULL));
        float cx = 640 + 250 * e, cy = 360 - 1180 * e, rot = (-0.8f * lift - 9 * e) * D2R, sc = 1 + 0.03f * lift + 0.05f * e;
        surf_copy(s, I.home);
        wall_blit_shade(s, K.sh, K.shw, K.shh, cx + 4 + 6 * lift + 8 * e, cy + 8 + 18 * lift + 14 * e, 8 * sc, rot, 0.42f * lift, SH_RGB, K.sh_hole);
        gfx_blit_xform(s, K.from->px, SCREEN_W, SCREEN_H, cx, cy, sc, rot, 1);
    } else if (K.style == 3) {
        // the seal comes down on the frame (as on the card) and its ink blooms; then the frame gives way to the bare home screen as the seal flies to the corner
        // (printed into the frame while it is down: INK.landed, fading back to the frame as it lifts)
        float f = e_smooth(prog(t, SK_FALL, SK_FALL + SK_FADE)), w = ink_w(t);
        const Surf *src = K.from;
        if (w >= 1) src = INK.landed;
        else if (w > 0) {
            gfx_lerp(s, K.from, INK.landed, (int)(w * 256 + 0.5f));
            src = s;
        }
        if (f >= 1) surf_copy(s, K.bare);
        else if (f <= 0) {
            if (src != s) surf_copy(s, src);
        } else
            gfx_lerp(s, src, K.bare, (int)(f * 256 + 0.5f));
        skip_seal(s, t);
        float u = prog(t, SK_TITLE1, SK_TITLE1 + SK_SETTLE);
        if (u >= 1) surf_copy(s, I.home);
        else if (u > 0) gfx_lerp(s, s, I.home, (int)(u * 256 + 0.5f));
    }
}

// tests: a skip at each of these times filmed at 60 frames a second for each style (half a second of the intro, the frame it was skipped on, the skip, 0.6 s of the
// home screen), every frame handed to the hook with its number
void (*g_ui_intro_film)(const Surf *frame, int style, float at, int i);
const float *g_ui_intro_film_at;
int g_ui_intro_film_n;

static void film_skip(float at, int style, int id)
{
    Ctx *c = &I.ctx[0];
    int i = 0;
    for (int k = 30; k >= 0; k--) {
        draw_frame(c, at - k / 60.0f);
        g_ui_intro_film(c->frame, id, at, i++);
    }
    float t0 = 1 / 60.0f;
    Surf *from = NULL;
    if (style == 3) { // as it plays: the seal coming down over the intro as it goes on, printed into what it lands on, held there a moment (the loading)
        for (int k = 1; k / 60.0f < SK_FALL - 1e-4f; k++) {
            draw_frame(c, at + k / 60.0f);
            skip_seal(c->frame, k / 60.0f);
            g_ui_intro_film(c->frame, id, at, i++);
        }
        draw_frame(c, at + SK_FALL);
        from = surf_clone(c->frame);
        ink_make(at + SK_FALL);
        ink_land(c, from, at + SK_FALL);
        for (int k = 0; k < 24 && INK.landed; k++) g_ui_intro_film(INK.landed, id, at, i++);
        t0 = SK_FALL;
    } else
        from = surf_clone(c->frame);
    if (from && skip_begin(from, style) == 0) {
        float len = skip_len(style);
        for (int k = 0; t0 + k / 60.0f < len + 1e-4f; k++) {
            skip_draw(c->frame, t0 + k / 60.0f);
            g_ui_intro_film(c->frame, id, at, i++);
        }
    }
    skip_end();
    ink_free();
    surf_free(from);
    for (int k = 0; k < 36; k++) g_ui_intro_film(I.home, id, at, i++);
}

// ------------------------------------------------------------------------------------------------ the sounds (b4Cues): the app's own, where the page plays them

typedef struct { float t; int snd; float gain; } Cue;
static int cue_cmp(const void *a, const void *b)
{
    float d = ((const Cue *)a)->t - ((const Cue *)b)->t;
    return d < 0 ? -1 : d > 0 ? 1 : 0;
}

static int make_cues(Cue *q, int cap)
{
    int n = 0;
#define ADD(T, S, G) do { if (n < cap) q[n++] = (Cue){(T), (S), (G)}; } while (0)
    for (int i = 0; i < I.nrows; i++) {
        const Row *R = &I.rows[i];
        float t0 = R->t0, d = R->dur, te = t0 + R->d_end;
        int rk = I.nrows - 1 - i;
        for (int k = 0; k < 2; k++) ADD(t0 + d * (0.2f + 0.4f * k), SND_SLIP0 + (k + i) % 4, 0.09f + 0.02f * k); // the turning roll's taps
        for (int k = 0; k < R->nc; k++) {
            const Catch *c = &R->c[k];
            if (c->crimp) ADD(t0 + c->tau, SND_CRINKLE, 0.2f + 0.22f * c->s); // a crinkle at the soft crease
            else ADD(t0 + c->tau, SND_SLIP0 + (int)floorf(c->s * 7) % 4, 0.25f + 0.18f * c->s); // a slip at a hitch
        }
        ADD(te + 0.01f, SND_TEAR, 0.22f); // cut at the end
        ADD(R->rip_at, SND_PEEL, fmaxf(0.2f, 0.42f - 0.014f * rk)); // and torn off
        ADD(R->rip_at + 0.1f, SND_TEAR, 0.2f);
    }
    for (int j = 0; j < 5; j++) ADD(T_W0 + (T_W1 - T_W0) * j / 5, SND_SLIP0 + j % 4, 0.1f); // the pen
    ADD(T_PAPER0 + 0.1f, SND_SWISH, 0.3f);
    ADD(T_SEAL, SND_STAMP, 0.95f);
    ADD(T_TOUCH, SND_SWISH, 0.12f);
    ADD(T_TOUCH + T_TDUR * 0.5f, SND_STICK, 0.16f);
    ADD(T_TOUCH + T_TDUR, SND_STICK, 0.3f);
    ADD(T_TAPESOFF, SND_PEEL, 0.3f);
    ADD(T_PULL0, SND_SWISH, 0.34f);
    ADD(I.corner_at + 0.1f, SND_STICK, 0.4f);
#undef ADD
    qsort(q, (size_t)n, sizeof *q, cue_cmp);
    return n;
}

// the home screen's tiles landing (every third one ticks), once the home screen is ready: added to the cues still to come (they all come after the card's rest)
static int tile_cues(Cue *q, int n, int next, int cap)
{
    for (int i = 0; i < I.ntiles && n < cap; i += 3) q[n++] = (Cue){H_TILE0 + I.order[i] * H_STEP + H_TDUR * 0.55f, SND_STICK, 0.22f};
    qsort(q + next, (size_t)(n - next), sizeof *q, cue_cmp);
    return n;
}

// ------------------------------------------------------------------------------------------------ two drawers

// On the console the frames are drawn on its other two cores, alternate frames each, while the main thread shows them in order and listens for a button (on a PC
// the same with POSIX threads, so the tests go through it too). A drawer reads only what is shared, which nothing changes while it plays, and writes only its own
// frame and kept pictures.
#ifdef __SWITCH__
typedef Thread XThread;
typedef Mutex XMutex;
typedef CondVar XCond;
static void x_init(XMutex *m, XCond *c)
{
    mutexInit(m);
    condvarInit(c);
}
#define x_lock(m) mutexLock(m)
#define x_unlock(m) mutexUnlock(m)
#define x_wait(c, m) condvarWait(c, m)
#define x_wake(c) condvarWakeAll(c)
#else
#include <pthread.h>
typedef pthread_t XThread;
typedef pthread_mutex_t XMutex;
typedef pthread_cond_t XCond;
static void x_init(XMutex *m, XCond *c)
{
    pthread_mutex_init(m, NULL);
    pthread_cond_init(c, NULL);
}
#define x_lock(m) pthread_mutex_lock(m)
#define x_unlock(m) pthread_mutex_unlock(m)
#define x_wait(c, m) pthread_cond_wait(c, m)
#define x_wake(c) pthread_cond_broadcast(c)
#endif

typedef struct {
    Ctx *c;
    XThread th;
    int on;    // its thread runs
    int state; // 0 nothing to do, 1 a frame to draw (at t), 2 drawn
    float t;
} Drawer;
static struct {
    XMutex mx;
    XCond cv;
    int quit;
    Drawer d[3]; // (the third, on the main core below the main thread, is there only if its pictures could be made; it waits for the home screen)
} D;

static void drawer_main(void *arg)
{
    Drawer *d = arg;
    for (;;) {
        x_lock(&D.mx);
        while (d->state != 1 && !D.quit) x_wait(&D.cv, &D.mx);
        int quit = D.quit;
        float t = d->t;
        x_unlock(&D.mx);
        if (quit) return;
        draw_frame(d->c, t);
        x_lock(&D.mx);
        d->state = 2;
        x_wake(&D.cv);
        x_unlock(&D.mx);
    }
}
#ifndef __SWITCH__
static void *drawer_pt(void *arg)
{
    drawer_main(arg);
    return NULL;
}
#endif

static void drawers_stop(void) // (waits for a frame being drawn)
{
    x_lock(&D.mx);
    D.quit = 1;
    x_wake(&D.cv);
    x_unlock(&D.mx);
    for (int i = 0; i < 3; i++) {
        Drawer *d = &D.d[i];
        if (!d->on) continue;
#ifdef __SWITCH__
        threadWaitForExit(&d->th);
        threadClose(&d->th);
#else
        pthread_join(d->th, NULL);
#endif
        d->on = 0;
    }
}

static int drawers_start(void) // 1 if the first two run (else none does); the third runs if its pictures could be made (main thread: ctx_init)
{
    x_init(&D.mx, &D.cv);
    D.quit = 0;
    int third = ctx_init(&I.ctx[2]) == 0;
    for (int i = 0; i < 3; i++) {
        Drawer *d = &D.d[i];
        d->c = &I.ctx[i];
        d->state = 0;
        d->on = 0;
        if (i == 2 && !third) continue;
#ifdef __SWITCH__
        // the first two on cores 1 and 2; the third on the main core, below the main thread (which must show each frame on time) and the sound's
        if (R_SUCCEEDED(threadCreate(&d->th, drawer_main, d, NULL, 0x40000, i < 2 ? 0x2C : 0x2D, i < 2 ? i + 1 : 0))) {
            if (R_SUCCEEDED(threadStart(&d->th))) d->on = 1;
            else threadClose(&d->th);
        }
#else
        d->on = pthread_create(&d->th, NULL, drawer_pt, d) == 0;
#endif
    }
    if (D.d[0].on && D.d[1].on) return 1;
    drawers_stop();
    return 0;
}

static int drawer_done(Drawer *d) // its frame is drawn (it is not taken)
{
    x_lock(&D.mx);
    int done = d->state == 2;
    x_unlock(&D.mx);
    return done;
}

static void drawer_give(Drawer *d, float t)
{
    x_lock(&D.mx);
    d->t = t;
    d->state = 1;
    x_wake(&D.cv);
    x_unlock(&D.mx);
}

static void drawer_take(Drawer *d) // waits for its frame
{
    x_lock(&D.mx);
    while (d->state != 2) x_wait(&D.cv, &D.mx);
    d->state = 0;
    x_unlock(&D.mx);
}

// ------------------------------------------------------------------------------------------------ the home screen, got ready while it plays

// The intro needs the home screen (build_home: three of its frames drawn, cut into pieces; and the corner's mark and name) only once the card is pulled away. On the
// console a thread on the main core makes it while the wall goes up (below the main thread, which shows the frames; it touches only the home screen's own state and
// the skin, which nothing else uses while the intro plays). On a PC it is made when it is first wanted (INTRO_ASYNC_HOME=1: on a thread, as on the console), so the
// tests see the same frames every time. home_wait() returns once it is there (1) or could not be made (0).
static struct {
    UiHome *h;
    int on, ok;
    atomic_int done;
    XThread th;
} HW;

static void home_work(void *arg)
{
    (void)arg;
    int ok = build_home(HW.h) == 0;
    if (ok) { // (the corner's mark and name, as the top bar has them: drawing the home screen has made them)
        Cx c = ui_cx(I.home);
        skin_bar_logo(&c, &I.mark1, &I.title1);
    }
    HW.ok = ok;
    atomic_store(&HW.done, 1);
}
#ifndef __SWITCH__
static void *home_pt(void *arg)
{
    home_work(arg);
    return NULL;
}
#endif

static void home_start(UiHome *h)
{
    HW.h = h;
    HW.on = HW.ok = 0;
    atomic_init(&HW.done, 0);
#ifdef __SWITCH__
    // below every other thread, and free to run on any of the three cores while one is idle (the drawers' cores have time to spare while the card rests)
    if (R_SUCCEEDED(threadCreate(&HW.th, home_work, NULL, NULL, 0x100000, 0x2D, 0))) {
        svcSetThreadCoreMask(HW.th.handle, 0, 0x7); // (if it cannot, it stays on the main core)
        if (R_SUCCEEDED(threadStart(&HW.th))) HW.on = 1;
        else threadClose(&HW.th);
    }
#else
    if (getenv("INTRO_ASYNC_HOME")) HW.on = pthread_create(&HW.th, NULL, home_pt, NULL) == 0;
#endif
}

static int home_ready(void) { return atomic_load(&HW.done); }

static int home_wait(void)
{
    if (HW.on) {
#ifdef __SWITCH__
        threadWaitForExit(&HW.th);
        threadClose(&HW.th);
#else
        pthread_join(HW.th, NULL);
#endif
        HW.on = 0;
    }
    if (!atomic_load(&HW.done)) home_work(NULL);
    return HW.ok;
}

// the intro's time now: its clock stands still with the card at rest, just before it is pulled away, until the home screen is ready (*held: how long it has stood)
#define T_HOLD (T_PULL0 - 0.001f)
static float intro_clock(uint64_t t0, float *held, int *bad, int nowait) // (nowait: it stands still there without waiting)
{
    float t = (float)(ui_now() - t0) / 1000.0f - *held;
    if (t >= T_HOLD && !home_ready()) {
        if (nowait) return T_HOLD;
        if (!home_wait()) *bad = 1;
        *held = (float)(ui_now() - t0) / 1000.0f - T_HOLD;
        t = T_HOLD;
    }
    return t;
}

// ---- a skip, played (main thread, the drawers stopped and the home screen ready): its frames from the one shown at the press, with its sounds; -1 if it could not
// be made (then it is a cut)
static struct {
    float at;                         // the intro's time at the press (0: not skipped)
    double down_ms, wait_ms, first_ms; // from the press: until the seal was down, until the home screen was ready, until the skip's first frame was shown
    double sum, mx;           // the skip's frames: the time between them, summed and the longest
    int gaps;
    uint64_t first;
} SL; // (for the timing file)

static int skip_cues(int style, Cue *q)
{
    int n = 0;
    if (style == 1) {
        q[n++] = (Cue){0, SND_CHORD, 1.0f};
        for (int i = 0; i < SK_N; i++) q[n++] = i % 2 ? (Cue){i * SK_GAP + 0.1f, SND_TEAR, 0.2f} : (Cue){i * SK_GAP, SND_PEEL, 0.3f};
    } else if (style == 2) {
        q[n++] = (Cue){0, SND_CHORD, 1.0f};
        q[n++] = (Cue){0.02f, SND_SWISH, 0.34f};
    } else if (style == 3) { // the chord rings out with the stamp, as it does in the intro
        q[n++] = (Cue){SK_FALL, SND_STAMP, 0.95f};
        q[n++] = (Cue){SK_FALL, SND_CHORD, 1.0f};
        q[n++] = (Cue){SK_FLY1, SND_STICK, 0.4f};
        q[n++] = (Cue){SK_TITLE1 - 0.06f, SND_STICK, 0.22f};
    }
    return n;
}

// (t0: where it takes up from, its cues before that already played: the seal already down over the intro)
static int skip_play(const Surf *from, int style, float t0)
{
    if (!style || !from || skip_begin(from, style) != 0) {
        skip_end();
        return -1;
    }
    Cue q[SK_N + 2];
    int nq = skip_cues(style, q), next = 0;
    while (t0 > 0 && next < nq && q[next].t <= t0) next++;
    float len = skip_len(style);
    uint64_t s0 = ui_now() - (uint64_t)lroundf(t0 * 1000), prev = 0;
    for (;;) {
        float t = fminf((float)(ui_now() - s0) / 1000.0f, len);
        while (next < nq && q[next].t < t + 0.05f) { // (each at its own time from now: this frame shows now)
            int delay = (int)lroundf((q[next].t - t) * 1000);
            if (G.plat->play) G.plat->play(G.plat, q[next].snd, q[next].gain, delay > 0 ? delay : 0);
            next++;
        }
        skip_draw(G.work, t);
        G.plat->present(G.plat, G.work);
        g_ui_intro_skip_frames++;
        uint64_t now = ui_now();
        if (prev) {
            double d = (double)(now - prev);
            SL.sum += d;
            if (d > SL.mx) SL.mx = d;
            SL.gaps++;
        } else if (!SL.first)
            SL.first = now;
        prev = now;
        if (t >= len) break;
        if (G.plat->wait_input(G.plat, 0) == UI_QUIT) { // (another press does not cut it short: it is half a second)
            G.quit = 1;
            break;
        }
    }
    skip_end();
    return 0;
}

// ------------------------------------------------------------------------------------------------ playing it

// On the console, how the intro went is written to sdmc:/switch/washi/intro-timing.txt: how long it took to get ready (and how much of that was waiting for the
// strips), and for each part of it how many frames were shown and how far apart. (For now, to see how fast it really is there.)
enum { NLOG = 640 };
static void timing_log(double ready_ms, double wait_ms, float held, int par, const uint64_t *at, const float *ft, int n)
{
#ifdef __SWITCH__
    FILE *f = fopen("sdmc:/switch/washi/intro-timing.txt", "w");
    if (!f) return;
    char dr[64];
    if (par && g_ui_intro_third_at > 0) snprintf(dr, sizeof dr, "three drawers (the third from %.2f s)", g_ui_intro_third_at);
    else snprintf(dr, sizeof dr, "%s", par ? "two drawers" : "one drawer");
    fprintf(f, "ready in %.0f ms (%.0f of it waiting for the strips), %s, %d frames shown; the card waited %.0f ms for the home screen\n", ready_ms, wait_ms, dr, n,
            held * 1000);
    static const char *const NAME[5] = {"laying the wall", "card and logo", "tearing it off", "home coming in", "settling"};
    static const float END[5] = {1.25f, T_RIP0, H_TILE0, T_SETTLE, 99};
    for (int k = 0; k < 5; k++) {
        int m = 0;
        double sum = 0, mx = 0;
        for (int i = 1; i < n && i < NLOG; i++) {
            if (ft[i] >= END[k] || (k && ft[i] < END[k - 1])) continue;
            double d = (double)(at[i] - at[i - 1]);
            sum += d;
            if (d > mx) mx = d;
            m++;
        }
        if (m) fprintf(f, "%-16s %3d frames, %5.1f ms apart on average (%4.1f a second), %3.0f ms at most\n", NAME[k], m, sum / m, 1000 * m / sum, mx);
    }
    if (SL.at > 0) {
        fprintf(f, "skipped at %.2f s: the skip's first frame came %.0f ms after the press, the seal was down %.0f ms after it, the home screen ready %.0f ms after it\n",
                SL.at, SL.first_ms, SL.down_ms, SL.wait_ms);
        if (SL.gaps) fprintf(f, "the skip        %3d frames, %5.1f ms apart on average (%4.1f a second), %3.0f ms at most\n", SL.gaps, SL.sum / SL.gaps,
                             1000 * SL.gaps / SL.sum, SL.mx);
    }
    fclose(f);
#else
    (void)ready_ms, (void)wait_ms, (void)held, (void)par, (void)at, (void)ft, (void)n;
#endif
}

static void free_all(void)
{
    ink_free();
    for (int i = 0; i < I.nrows; i++) {
        wall_spr_free(I.rows[i].strip);
        wall_spr_free(I.rows[i].turned);
        free(I.rows[i].runs);
        wall_roll_tex_free(I.rows[i].tex);
    }
    free(I.card_px);
    free(I.sh_rest);
    free(I.sh_lift);
    paper_spr_free(I.blank);
    paper_spr_free(I.text);
    paper_spr_free(I.seal);
    free(I.written);
    free(I.glow);
    wall_spr_free(I.lay);
    surf_free(I.home);
    surf_free(I.board);
    wall_spr_free(I.top.spr);
    wall_spr_free(I.rail.spr);
    wall_spr_free(I.footer.spr);
    for (int i = 0; i < 14; i++) wall_spr_free(I.tiles[i].spr);
    for (int i = 0; i < 3; i++) ctx_free(&I.ctx[i]);
    memset(&I, 0, sizeof I);
}

void ui_intro_drop(void)
{
    if (I.state != 1) return;
    wait_jobs();
    free_all();
}

// ---- The logo's pieces at 2.5 times (skin_logo_parts: the name's strip without the name, the name, the seal), made beforehand. Making them takes the console more
// than half a second and they are the same at every start, so the build keeps them (data/intro_logo.bin, made by the PC test with ui_intro_logo_blob, which checks
// it is still what skin.c makes) and a start copies them. Blob: "WLG2", then for each piece w, h, ox, oy (32 bits, little endian) and w x h RGBA: the strip in the
// light theme's mint, the name, the seal in ink blue, and the strip again in Paper night's pale blue. Without it, or if it does not read right, they are made here. In
// Paper night the seal is made again at the start, in the moon's cream the corner's seal has.
static const uint8_t *g_logo_blob;
static size_t g_logo_blob_n;

void ui_intro_set_logo(const uint8_t *blob, size_t size)
{
    g_logo_blob = blob;
    g_logo_blob_n = size;
}

static uint32_t rd32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }

enum { LOGO_PIECES = 4 };

static int logo_unpack(PaperSpr *out[LOGO_PIECES])
{
    const uint8_t *p = g_logo_blob, *end = g_logo_blob + g_logo_blob_n;
    if (!p || g_logo_blob_n < 4 || memcmp(p, "WLG2", 4)) return -1;
    p += 4;
    for (int i = 0; i < LOGO_PIECES; i++) {
        if (end - p < 16) return -1;
        uint32_t w = rd32(p), h = rd32(p + 4), ox = rd32(p + 8), oy = rd32(p + 12);
        p += 16;
        if (w == 0 || h == 0 || w > 4096 || h > 4096 || (size_t)(end - p) < (size_t)w * h * 4) return -1;
        if (!(out[i] = paper_spr_new((int)w, (int)h))) return -1;
        memcpy(out[i]->px, p, (size_t)w * h * 4);
        out[i]->ox = (int)ox;
        out[i]->oy = (int)oy;
        p += (size_t)w * h * 4;
    }
    return p == end ? 0 : -1;
}

uint8_t *ui_intro_logo_blob(size_t *out_size)
{
    PaperSpr *s[LOGO_PIECES] = {NULL, NULL, NULL, NULL};
    Cx c = ui_cx(G.work);
    skin_logo_parts(&c, LOGO_K, &s[0], &s[1], &s[2], PAPER_INK, PAPER_TITLE_STRIP, PAPER_TITLE_FOLD); // (the light theme's, whatever the theme now)
    s[3] = skin_logo_strip(&c, LOGO_K, PAPER_NIGHT_TITLE_STRIP, PAPER_NIGHT_TITLE_FOLD);
    size_t n = 4;
    for (int i = 0; i < LOGO_PIECES; i++) n += s[i] ? 16 + (size_t)s[i]->w * s[i]->h * 4 : 0;
    uint8_t *b = s[0] && s[1] && s[2] && s[3] ? malloc(n) : NULL, *p = b;
    if (b) {
        memcpy(p, "WLG2", 4);
        p += 4;
        for (int i = 0; i < LOGO_PIECES; i++) {
            uint32_t v[4] = {(uint32_t)s[i]->w, (uint32_t)s[i]->h, (uint32_t)s[i]->ox, (uint32_t)s[i]->oy};
            for (int j = 0; j < 4; j++)
                for (int k = 0; k < 4; k++) *p++ = (uint8_t)(v[j] >> (8 * k));
            memcpy(p, s[i]->px, (size_t)s[i]->w * s[i]->h * 4);
            p += (size_t)s[i]->w * s[i]->h * 4;
        }
        *out_size = n;
    }
    for (int i = 0; i < LOGO_PIECES; i++) paper_spr_free(s[i]);
    return b;
}

// the logo's pieces (main thread): the strip without its name, the name, the seal (from the blob, or made), then what comes of them: the name all written, the seal
// ink's bloom, and the strip cut out for laying (the page's titleLay: the strip, its fold and shadow without the sprite's clear room)
static void make_logo(void)
{
    PaperSpr *p[LOGO_PIECES] = {NULL, NULL, NULL, NULL};
    uint32_t ink = PAL(mark_ink, PAPER_INK), strip = PAL(title_strip, PAPER_TITLE_STRIP), fold = PAL(title_fold, PAPER_TITLE_FOLD);
    if (logo_unpack(p) == 0) {
        int night = strip == PAPER_NIGHT_TITLE_STRIP; // (the blob has the strip in both themes' colours)
        I.blank = p[night ? 3 : 0];
        paper_spr_free(p[night ? 0 : 3]);
        I.text = p[1];
        I.seal = p[2];
        if (ink != PAPER_INK) { // (Paper night: the stored seal is in the light theme's ink)
            paper_spr_free(I.seal);
            I.seal = skin_logo_seal(LOGO_K, ink);
        }
        if (strip != PAPER_TITLE_STRIP && strip != PAPER_NIGHT_TITLE_STRIP) { // (a mockup palette's own strip)
            Cx c = ui_cx(G.work);
            paper_spr_free(I.blank);
            I.blank = skin_logo_strip(&c, LOGO_K, strip, fold);
        }
    } else {
        for (int i = 0; i < LOGO_PIECES; i++) paper_spr_free(p[i]);
        Cx c = ui_cx(G.work);
        skin_logo_parts(&c, LOGO_K, &I.blank, &I.text, &I.seal, ink, strip, fold);
    }
    if (I.text && (I.written = malloc((size_t)I.text->w * I.text->h * 4))) write_mask(I.text, T_W1, I.written);
    if (I.seal) { // the bloom: the seal's ink spread soft round it (drawn in ink blue)
        int w = I.seal->w, h = I.seal->h, pad = 30, W = w + 2 * pad, H = h + 2 * pad;
        float *a = malloc((size_t)W * H * sizeof(float)), *tmp = malloc((size_t)W * H * sizeof(float));
        uint8_t *g = a && tmp ? malloc((size_t)W * H) : NULL;
        if (g) {
            for (int y = 0; y < H; y++)
                for (int x = 0; x < W; x++) {
                    int sx = x - pad, sy = y - pad;
                    a[(size_t)y * W + x] = sx >= 0 && sy >= 0 && sx < w && sy < h ? I.seal->px[((size_t)sy * w + sx) * 4 + 3] / 255.0f : 0;
                }
            wall_blur_alpha(a, tmp, W, H, 11);
            for (size_t i = 0, n = (size_t)W * H; i < n; i++) g[i] = (uint8_t)(clamp01(a[i]) * 255 + 0.5f);
            I.glow = g;
            I.gw = W;
            I.gh = H;
        }
        free(a);
        free(tmp);
    }
    if (I.blank) { // (the page cut 32, 48, 516, 200 of its 3.2 times picture)
        float f = LOGO_K / 3.2f;
        int x = (int)lroundf(32 * f), y = (int)lroundf(48 * f), w = (int)lroundf(516 * f), h = (int)lroundf(200 * f);
        if (x + w > I.blank->w) w = I.blank->w - x;
        if (y + h > I.blank->h) h = I.blank->h - y;
        I.lay = wall_spr_new(w, h);
        if (I.lay)
            for (int j = 0; j < h; j++) memcpy(I.lay->px + (size_t)j * w * 4, I.blank->px + ((size_t)(y + j) * I.blank->w + x) * 4, (size_t)w * 4);
    }
}

int ui_intro_play(UiHome *h)
{
    if (I.state != 1) return -1;
    if (G.prev_ok || G.quit) { // something was already shown (a message, the first-run questions): no intro this time
        ui_intro_drop();
        return -1;
    }
#ifdef UI_INTRO_PROF // (how long each step of getting ready takes)
    double lt[8];
    int nlt = 0;
#define LT() (lt[nlt++] = prof_ms())
#else
#define LT() ((void)0)
#endif
    LT();
    uint64_t e0 = ui_now();
    wait_jobs();
    double wait_ms = (double)(ui_now() - e0);
    LT();
    int ok = 1;
    for (int i = 0; i < I.nrows; i++) ok &= I.rows[i].ok;
    if (!ok || !I.card_px) {
        free_all();
        return -1;
    }
    make_logo();
    LT();
    home_start(h); // (from here on: home_wait() before anything is freed)
    LT();
    // the rows' times (b4Build): laid one after another from 0.10 s, torn off newest first from 2.42 s
    {
        WallRng rr;
        wall_rng_seed(&rr, I.seed ^ 0x71E5u);
        int n = I.nrows;
        float step = T_SPREAD / (n - 1);
        for (int i = 0; i < n; i++) {
            I.rows[i].t0 = T_START + step * i + (wall_rng(&rr) - 0.5f) * 0.016f;
            I.rows[i].rip_at = T_RIP0 + (n - 1 - i) * (T_RIPSPAN / (n - 1));
        }
        I.corner_at = I.rows[0].rip_at + 0.05f;
    }
    Cue cues[160];
    int ncue = make_cues(cues, 160), next = 0;
    if (ctx_init(&I.ctx[0]) != 0) {
        home_wait();
        free_all();
        return -1;
    }
    LT();
#ifdef UI_INTRO_PROF
    {
        double hw0 = prof_ms();
        home_wait(); // (timed here: on a PC it is made when first wanted)
        printf("  intro ready: strips %.1f ms, logo %.1f, cues + a drawer %.1f; then the home screen (made while it plays) %.1f\n", lt[1] - lt[0], lt[2] - lt[1],
               lt[4] - lt[3], prof_ms() - hw0);
    }
#endif
#undef LT
    // test frames, drawn at set times
    if (g_ui_intro_shot) {
        if (!home_wait()) {
            free_all();
            return -1;
        }
        for (int i = 0; i < g_ui_intro_ntimes; i++) {
            draw_frame(&I.ctx[0], g_ui_intro_times[i]);
            g_ui_intro_shot(I.ctx[0].frame, g_ui_intro_times[i]);
        }
        for (int j = 0; g_ui_intro_film && j < g_ui_intro_film_n; j++) {
            float at = g_ui_intro_film_at[j];
            if (g_ui_intro_film_wraps) { // the ways the ink can meet a roll (g_ui_intro_wrap), as ids 10 + way; at -1: landing as a roll's middle is nearest the
                                         // seal's, -2: nearest a point up and to the left of it (part of the roll under the seal)
                if (at < 0) at = ui_intro_roll_near(at < -1.5f ? 560 : 640, at < -1.5f ? 330 : 360, 0.2f, 1.2f) - SK_FALL;
                int keep = g_ui_intro_wrap;
                for (int m = 0; m <= 4; m++) {
                    g_ui_intro_wrap = m;
                    film_skip(at, 3, 10 + m);
                }
                g_ui_intro_wrap = keep;
            } else
                for (int st = 1; st <= 3; st++) film_skip(at, st, st);
        }
    }
    wall_roll_ready(WK); // (what the rolls' shadows share is made before two drawers could both want it)
    int par = 0;
#ifndef UI_INTRO_PROF // (a profile times frames drawn here, one after another)
    par = ctx_init(&I.ctx[1]) == 0 && drawers_start();
#endif
    if (G.plat->fade_off) G.plat->fade_off(G.plat); // (the intro comes out of the black itself)
    uint64_t t0 = ui_now();
    double ready_ms = (double)(t0 - e0);
    static uint64_t log_at[NLOG];
    static float log_ft[NLOG];
    int skipped = 0, frames = 0, bad = 0, tiles_cued = 0, ended = 0;
    float held = 0, fall = -1, land_ft = 0; // fall: a skip's seal coming down over the intro, seconds since the press (-1: not coming down); land_ft: the
                                            // intro's time of the frame it came down on
    const Surf *shown = NULL;  // the frame on the screen (a skip goes on from it: its drawer is given no other frame once the seal is down)
    Surf *fs = NULL;           // what is shown while the seal comes down: the intro's frame with the seal over it
    uint64_t press = 0;
    memset(&SL, 0, sizeof SL);
    g_ui_intro_skip_frames = g_ui_intro_skip_fall = g_ui_intro_skip_down = g_ui_intro_late = 0;
    // The drawers take turns, in the order their frames are to be shown (q): the first two have a frame each from the start, and when one has been shown its
    // drawer draws the frame for that moment and goes to the back. The third (on the main core) joins once the home screen is made (nothing else wants that core
    // then): first it draws the frame for that moment, which is not shown (it fills its kept pictures, so that its first turn is not a slow one), then it takes
    // its turn. (Its first frame shown has the time of the one before it: one frame stands still, once.)
    Drawer *q[3];
    int qn = 0, third = par && D.d[2].on ? 1 : 0; // third: 1 waiting for the home screen, 2 filling its pictures, 3 taking turns
    float third_at = 0;
    if (par) {
        drawer_give(&D.d[0], 0);
        drawer_give(&D.d[1], 1 / 60.0f);
        q[qn++] = &D.d[0];
        q[qn++] = &D.d[1];
    }
    for (int k = 0;; k++) {
        Ctx *c = &I.ctx[0];
        Drawer *d = NULL;
        float ft; // the time the frame about to be shown was drawn for
        if (par) {
            d = q[0];
            drawer_take(d);
            c = d->c;
            ft = d->t;
        } else {
            ft = intro_clock(t0, &held, &bad, skipped);
            if (ft < T_END && !bad) {
#ifdef UI_INTRO_PROF
                double p0 = prof_ms();
                draw_frame(c, ft);
                prof_add(ft, prof_ms() - p0);
#else
                draw_frame(c, ft);
#endif
            }
        }
        if (ft >= T_END || bad) {
            ended = ft >= T_END;
            break;
        }
        // its sounds: the cues from now to a little after it, each at its own time from the moment this frame shows (the frames drawn ahead are shown late by
        // the time they took, and the sounds go with what is shown); none past the card's rest while the clock may still stand there
        if (k == 0 && G.plat->play) G.plat->play(G.plat, SND_INTRO, 1.0f, 0);
        float ahead = ft + 0.12f;
        if (!home_ready() && ahead > T_HOLD) ahead = T_HOLD;
        else if (!tiles_cued && home_ready()) {
            ncue = tile_cues(cues, ncue, next, 160);
            tiles_cued = 1;
        }
        while (!skipped && next < ncue && cues[next].t < ahead) {
            int delay = (int)lroundf((cues[next].t - ft) * 1000);
            if (G.plat->play && cues[next].t >= ft - 0.15f) G.plat->play(G.plat, cues[next].snd, cues[next].gain, delay > 0 ? delay : 0);
            next++;
        }
        if (skipped) { // the seal coming down over it
            fall = fminf((float)(ui_now() - press) / 1000.0f, SK_FALL);
            shown = c->frame;
            if (fall >= SK_FALL) { // down: this frame, printed, is shown once the drawers are stopped (its drawer is given no other frame)
                land_ft = ft;
                break;
            }
            surf_copy(fs, c->frame);
            skip_seal(fs, fall);
            G.plat->present(G.plat, fs);
            g_ui_intro_skip_frames++;
            g_ui_intro_skip_fall++;
            if (!SL.first) SL.first = ui_now();
        } else {
            G.plat->present(G.plat, c->frame);
            shown = c->frame;
            if (frames < NLOG) {
                log_at[frames] = ui_now();
                log_ft[frames] = ft;
            }
            frames++;
        }
        uint64_t b = G.plat->wait_input(G.plat, 0);
        if (b == UI_QUIT) {
            G.quit = 1;
            break;
        }
        if (G.plat->stick) b = 0; // (a button skips it, the stick does not)
        if (b && !skipped && ft >= T_NOSKIP) g_ui_intro_late++;
        if (b && !skipped && ft < T_NOSKIP) {
            skipped = 1;
            SL.at = ft > 0 ? ft : 0.001f;
            press = ui_now();
            if (G.plat->hush) G.plat->hush(G.plat); // the music and the tape's sounds stop
            if (g_ui_intro_skip_style != 3 || !(fs = screen_new())) break;
            // the seal comes down at once over the intro as it plays on (the home screen is waited for once it is down), its stamp and chord with its landing
            fall = 0;
            Cue sq[SK_N + 2];
            for (int i = 0, n = skip_cues(3, sq); i < n; i++)
                if (sq[i].t <= SK_FALL && G.plat->play) G.plat->play(G.plat, sq[i].snd, sq[i].gain, (int)lroundf(sq[i].t * 1000));
        }
        if (par) {
            float now = intro_clock(t0, &held, &bad, skipped);
            memmove(q, q + 1, (size_t)(qn - 1) * sizeof *q);
            q[qn - 1] = d;
            drawer_give(d, now);
            if (third == 1 && home_ready()) {
                drawer_give(&D.d[2], now);
                third = 2;
            } else if (third == 2 && drawer_done(&D.d[2])) {
                drawer_give(&D.d[2], now);
                q[qn++] = &D.d[2];
                third = 3;
                third_at = now;
            }
        }
    }
    if (par) drawers_stop();
    // down: printed into the frame it came down on (as g_ui_intro_wrap has it with any roll under it), shown while the home screen is got ready
    if (fall >= SK_FALL && shown && !G.quit) {
        ink_make(land_ft);
        ink_land(&I.ctx[0], shown, land_ft);
        if (INK.landed) G.plat->present(G.plat, INK.landed);
        else {
            surf_copy(fs, shown);
            skip_seal(fs, SK_FALL);
            G.plat->present(G.plat, fs);
        }
        g_ui_intro_skip_frames++;
        if (!SL.first) SL.first = ui_now();
    }
    // a seal still on its way down when the intro got to its end: it comes down on the home screen the intro ended on (ready, or the intro would not have ended)
    if (ended && fall >= 0 && fall < SK_FALL && !G.quit && home_wait()) {
        shown = I.home;
        for (;;) {
            fall = fminf((float)(ui_now() - press) / 1000.0f, SK_FALL);
            surf_copy(fs, shown);
            skip_seal(fs, fall);
            G.plat->present(G.plat, fs);
            g_ui_intro_skip_frames++;
            if (!SL.first) SL.first = ui_now();
            if (fall >= SK_FALL) break;
            if (G.plat->wait_input(G.plat, 0) == UI_QUIT) {
                G.quit = 1;
                break;
            }
        }
    }
    if (fall >= SK_FALL) {
        SL.down_ms = (double)(ui_now() - press);
        g_ui_intro_skip_down = 1; // (down before the home screen is waited for)
    }
    if (!home_wait()) bad = 1; // (a skip may come before it is ready: what is on the screen, the seal down over it, stays until it is)
    if (skipped) SL.wait_ms = (double)(ui_now() - press);
    // then the skip goes on to the home screen (from the seal down, with the chord ringing out as it does at the end); if it cannot be made, a cut (with the chord,
    // unless it came with the seal)
    if (skipped && !bad && !G.quit && skip_play(shown, g_ui_intro_skip_style, fall >= SK_FALL ? SK_FALL : 0) != 0 && fall < SK_FALL && G.plat->play)
        G.plat->play(G.plat, SND_CHORD, 1.0f, 0);
    surf_free(fs);
    if (SL.first) SL.first_ms = (double)(SL.first - press);
    g_ui_intro_third_at = third == 3 ? third_at : 0;
    timing_log(ready_ms, wait_ms, held, par, log_at, log_ft, frames);
    if (bad) { // (no home screen to end on: ui_home draws it itself)
        if (G.plat->hush) G.plat->hush(G.plat);
        g_ui_intro_frames = frames;
        g_ui_intro_played = 0;
        free_all();
        return -1;
    }
    surf_copy(G.work, I.home); // (it ends on the home screen itself, which ui_home draws next)
    if (!G.quit) G.plat->present(G.plat, G.work);
    g_ui_intro_frames = frames;
    g_ui_intro_played = skipped ? 2 : 1;
#ifdef UI_INTRO_PROF
    prof_report();
#endif
    free_all();
    return 0;
}
