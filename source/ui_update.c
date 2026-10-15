// Updates (Admin, This app, Check for updates): what the newest Washi on GitHub is, and getting it. The page asks GitHub for the repo's latest release
// (api.github.com/repos/lii-lac/washi/releases/latest: its tag is the version, its body the notes, its washi.nro asset the download) on a worker thread
// while the crayon draws, then says how things stand; a newer one is downloaded (the tape bar), checked (its size, the NRO's magic), put in place of the
// running one with the old one kept as a backup (Platform.install_update), and opened on a restart (Platform.restart).
// Each state is a taped sheet on the board with a parcel on the left: a new Washi comes by post, rubber-stamped with how things stand.
// ui_update_preview(state) shows a state with made-up data (the mockups, and the tests' pictures).
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cJSON.h"
#include "ui_scene.h"

#ifdef __SWITCH__
#include <switch.h>
#else
#include <pthread.h>
#endif

#define UPDATE_REPO "lii-lac/washi"
#define UPDATE_API "https://api.github.com/repos/" UPDATE_REPO "/releases/latest"

int g_ui_update_preview = -1; // tests: the state the page shows instead of asking GitHub (-1: it asks)
int g_ui_update_last = -1;    // tests: the last state the page showed

enum { UP_CHECKING, UP_LATEST, UP_AVAILABLE, UP_DOWNLOADING, UP_READY, UP_OFFLINE, UP_MISSING, UP_ERROR, UP_NSTATES };

#define SX 150.0f
#define SY 176.0f
#define SW 980.0f
#define SH 432.0f
#define TX (SX + 300.0f) // the words' column
#define TW 600.0f
#define BTN_W 250.0f
#define BTN_H 64.0f
#define BTN_GAP 20.0f
#define COL_GO PAL(go, 0xD4FAC7)
#define COL_CANCEL PAL(quiet, 0xF1EFF4)
#define BAR_W 560.0f
#define BAR_H 36.0f

enum { LIST_DOTS, LIST_TICKS };
typedef struct {
    const char *head, *detail;
    const char *no, *yes;   // the buttons (NULL: none)
    const char *stamp;      // what is rubber-stamped on the parcel (NULL: nothing yet)
    const char *label;      // a small heading over the list (NULL: none)
    int kind, n;            // the list: a dot or a tick of crayon before each line
    const char *items[4];
} UpText;

// what the page shows now: the words (pointing into the buffers here when they are made up as it goes), and the state's own bits
typedef struct {
    int st;
    UpText t;
    char head[96], detail[220], label[48], items[4][160];
    char newv[32];       // the version underlined in "Version x is out"
    char prev[32];       // the version kept from before the last update (Y goes back to it)
    int kept;            // one is kept
    char ylabel[48];
    uint64_t got, total; // the download so far
    Rect dyn;            // where the moving part is (the crayon, the bar): drawn over the base frame by frame
} Page;

// ---- the made-up release of the mockups
static const UpText MOCK[UP_NSTATES] = {
    {"Looking for a newer Washi\xE2\x80\xA6", "Asking GitHub for the newest release.", NULL, NULL, NULL, NULL, 0, 0, {NULL}},
    {"Washi is up to date", "Version 1.0.0 is the newest. Checked just now.", NULL, "OK", "UP TO DATE", "IN 1.0.0", LIST_DOTS, 3,
     {"Profiles, guests, and saves swapped on sign-out", "Backups of everything, kept day by day", "The tape wall when Washi opens"}},
    {"Version 1.1.0 is out", "You have 1.0.0. The update is 18.4 MB.", "Not now", "Update", "NEW", "WHAT\xE2\x80\x99S NEW", LIST_DOTS, 4,
     {"A skip stamps the seal and sends it to its corner", "The whole button presses in, edges and all", "Check for updates from This app",
      "A smoother tear-off at the end of the intro"}},
    {"Downloading 1.1.0", "Keep Washi open. Nobody\xE2\x80\x99s saves are touched.", "Stop", NULL, "ON ITS WAY", NULL, 0, 0, {NULL}},
    {"1.1.0 is ready", "Washi closes and opens again on the new version.", "Later", "Restart now", "DELIVERED", NULL, LIST_TICKS, 3,
     {"Downloaded, all 18.4 MB of it", "Checked that nothing is missing", "1.0.0 kept as a backup, and nobody\xE2\x80\x99s saves change"}},
    {"Couldn\xE2\x80\x99t reach GitHub", "Checking for updates needs the internet.", "Back", "Try again", "RETURNED", "THINGS TO TRY", LIST_DOTS, 3,
     {"Wi-Fi on: System Settings, Internet", "Airplane Mode off", "GitHub may be busy: try again in a minute"}},
    {"No releases to be found", "GitHub has no release of Washi at github.com/" UPDATE_REPO " yet.", "Back", "Try again", "ADDRESS UNKNOWN", NULL,
     LIST_DOTS, 1, {"Nothing to do: this Washi keeps working as it is"}},
    {"Something went wrong", "GitHub answered with an error (403). Try again in a few minutes.", "Back", "Try again", "DAMAGED", NULL, 0, 0, {NULL}},
};

// ---- the parcel: a new Washi comes by post. Brown paper on the board, taped shut with washi, a postage stamp with GitHub's mark printed on it, and a rubber
// stamp saying how things stand (as the loaded person's tile gets LOADED)
#define PW 212.0f
#define PH 156.0f
static void parcel_print(Surf *s, float ox, float oy, void *ud)
{
    (void)ud;
    gfx_rrect(s, ox, oy, PW, PH, 4.0f, hex(0xE4CCA2, 1)); // brown paper
    for (int i = 0; i < 9; i++) // its fibres: a few faint long streaks
        gfx_line(s, ox + 8, oy + 14 + i * 13.5f + (i % 3) * 2, ox + PW - 10, oy + 16 + i * 13.5f - (i % 2) * 3, 1.0f, hex(0xC9AE80, 0.25f));
    // the postage stamp, top right: white, perforated, GitHub's mark printed in ink
    float sx = ox + PW - 66, sy = oy + 12, sw = 50, sh = 58;
    gfx_rect(s, sx, sy, sw, sh, hex(PAPER_PAPER, 1));
    for (float t = 3.5f; t < sw; t += 7.2f) {
        gfx_circle(s, sx + t, sy, 2.4f, hex(0xE4CCA2, 1));
        gfx_circle(s, sx + t, sy + sh, 2.4f, hex(0xE4CCA2, 1));
    }
    for (float t = 3.5f; t < sh; t += 7.2f) {
        gfx_circle(s, sx, sy + t, 2.4f, hex(0xE4CCA2, 1));
        gfx_circle(s, sx + sw, sy + t, 2.4f, hex(0xE4CCA2, 1));
    }
    gfx_rect(s, sx + 6, sy + 6, sw - 12, sh - 12, hex(0xE8ECFA, 1));
    ui_github_mark(s, sx + 11, sy + 12, sw - 22, hex(PAPER_INK, 1));
    // the address, in pen: the console's own name in small letters, on two lines if it needs them (clear of the stamp)
    Cx c = ui_cx(s);
    SkinStatus stt;
    ui_status(&stt);
    char to[64], lines[2][160];
    snprintf(to, sizeof to, "to: %s", stt.account[0] ? stt.account : "the club");
    for (char *p = to; *p; p++)
        if (*p >= 'A' && *p <= 'Z') *p = (char)(*p - 'A' + 'a');
    int n = ui_wrap(G.skin.med, 15, 118, to, lines, 2);
    for (int i = 0; i < n; i++) { // (each line ruled the whole way across to the stamp, as many lines as the name takes)
        skin_txt(&c, G.skin.med, 15, 0, ox + 18, oy + 44 + i * 20, 0, hex(0x3A3550, 0.85f), lines[i]);
        gfx_line(s, ox + 16, oy + 50 + i * 20, ox + PW - 74, oy + 49 + i * 20, 1.2f, hex(0x3A3550, 0.4f));
    }
}

static void parcel_draw(float cx, float cy, const char *word, int st)
{
    SceneTape tp[2] = {{{(int)PW + 24, 30, paper_tape_rgb[2], PAT_STRIPES, PAPER_TAPE_ZIG, -1.5f, 401u}, -12, PH - 52},
                       {{70, 24, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, -38.0f, 402u}, -20, -8}};
    scene_card_on(G.base, cx - PW * 0.5f, cy - PH * 0.5f, PW, PH, 4.0f, 411u, -3.0f, 1, tp, 2, parcel_print, NULL);
    if (!word) return;
    Cx c = ui_cx(G.base);
    float size = strlen(word) > 10 ? 18.0f : 21.0f, w = text_width(G.skin.semi, size, size * 0.14f, word) + 34;
    int iw = (int)w + ((int)w & 1);
    PaperSpr *sp = skin_stamp(&c, iw, 40, size, word, 421u + (uint32_t)st, PAPER_INK); // (ink blue on the brown paper, whatever the theme)
    if (sp) gfx_blit_xform(G.base, sp->px, sp->w, sp->h, cx - 6, cy + 18, 1.0f, -9.0f * 3.14159265f / 180.0f, 0.92f);
    paper_spr_free(sp);
}

// ---- the moving parts, drawn into a surface at a place the page keeps
// the crayon drawing its symbols, t ms in (every ~5 s it starts over)
static void doodles_draw(Surf *s, float x, float cy, float t)
{
    PaperSpr *dd = ui_wait_still((float)((int)t % 5200), 77u + (uint32_t)((int)t / 5200));
    if (dd) paper_spr_xform(s, dd, x + dd->w * 0.7f - 6, cy, 1.4f, 0.0f, 1.0f);
    paper_spr_free(dd);
}

// the bar: a dashed outline as long as the whole download, tape laid as far as it has come, and how much that is
static void bar_draw(Surf *s, float bx, float by, uint64_t got, uint64_t total)
{
    Cx c = ui_cx(s);
    scene_dashed_rrect(s, bx, by, BAR_W, BAR_H, 8.0f, 2.0f, 9.0f, 7.0f, hex(PAL(text_soft, PAPER_TEXT_SOFT), 0.55f));
    float frac = total ? (float)((double)got / (double)total) : 0.0f;
    frac = frac < 0 ? 0 : frac > 1 ? 1 : frac;
    int tl = (int)(BAR_W * frac);
    tl += tl & 1;
    if (tl >= 24) {
        PaperTape t = {tl, 34, paper_tape_rgb[6], PAT_GRID, PAPER_TAPE_ZIG, 0.0f, 311u};
        PaperSpr *sp = paper_tape(&t);
        if (sp) paper_spr_put(s, sp, (int)(bx + tl * 0.5f), (int)(by + BAR_H * 0.5f));
        paper_spr_free(sp);
    }
    char a[24], b[24], of[40];
    ui_format_bytes(got, a, sizeof a);
    if (total) {
        ui_format_bytes(total, b, sizeof b);
        snprintf(of, sizeof of, "of %s", b);
    } else
        snprintf(of, sizeof of, "so far");
    skin_txt(&c, G.skin.semi, 20, 0, bx, by + BAR_H + 38, 0, hex(PAL(text, PAPER_TEXT), 1), a);
    skin_txt(&c, G.skin.med, 20, 0, bx + text_width(G.skin.semi, 20, 0, a) + 8, by + BAR_H + 38, 0, hex(PAL(text_soft, PAPER_TEXT_SOFT), 1), of);
}

// Y goes back to the version kept from before the last update, on the pages that stand still (not just after a new one was put in place: that kept one is this)
static int can_go_back(const Page *pg) { return pg->kept && pg->st != UP_CHECKING && pg->st != UP_DOWNLOADING && pg->st != UP_READY; }

// the page's still part into G.base (with `moving` the crayon or the bar too, as the mockups show them); pg->dyn says where the moving part goes
static void update_base(Page *pg, int moving)
{
    Skin *u = &G.skin;
    const UpText *tx = &pg->t;
    int st = pg->st;
    Prompt pr[3];
    int np = 0;
    if (can_go_back(pg)) {
        if (pg->prev[0]) snprintf(pg->ylabel, sizeof pg->ylabel, "Back to %s", pg->prev);
        else snprintf(pg->ylabel, sizeof pg->ylabel, "Previous version");
        pr[np++] = (Prompt){"Y", pg->ylabel};
    }
    if (tx->yes) pr[np++] = (Prompt){"A", tx->yes};
    pr[np++] = (Prompt){"B", st == UP_CHECKING ? "Cancel" : tx->no ? tx->no : "Back"};
    char sub[64];
    snprintf(sub, sizeof sub, "Version %s on this console", APP_VERSION);
    scene_chrome(G.base, "Updates", sub, pr, np);
    Cx c = ui_cx(G.base);
    const Col ink = hex(PAL(text, PAPER_TEXT), 1), soft = hex(PAL(text_soft, PAPER_TEXT_SOFT), 1);

    SceneTape tapes[2] = {{{100, 26, paper_tape_rgb[3], PAT_DOTS, PAPER_TAPE_ZIG, -32.0f, 171u}, -16, -12},
                          {{100, 26, paper_tape_rgb[5], PAT_STRIPES, PAPER_TAPE_ZIG, 33.0f, 172u}, SW - 84, -14}};
    scene_card_on(G.base, SX, SY, SW, SH, 6.0f, 181u, 0.0f, 0, tapes, 2, NULL, NULL);
    parcel_draw(SX + 152, SY + SH * 0.5f - 6, tx->stamp, st);

    // the words
    float y = SY + 76;
    char hfit[160];
    text_fit(u->semi, 36, -0.3f, tx->head, TW + 40, hfit, sizeof hfit);
    skin_txt(&c, u->semi, 36, -0.3f, TX, y, 0, ink, hfit);
    if (st == UP_AVAILABLE && pg->newv[0]) { // the new version's number underlined in crayon
        float w0 = text_width(u->semi, 36, -0.3f, "Version "), w1 = text_width(u->semi, 36, -0.3f, pg->newv);
        PaperSpr *ul = paper_underline((int)w1 + 6, &PAPER_CRAYON_BLUE, 303u);
        if (ul) paper_spr_put(G.base, ul, (int)(TX + w0 - 3), (int)(y + 12));
        paper_spr_free(ul);
    }
    char lines[6][160];
    int n = ui_wrap(u->med, 21, TW, tx->detail, lines, 3);
    y += 50;
    for (int i = 0; i < n; i++) skin_txt(&c, u->med, 21, 0, TX, y + i * 33, 0, soft, lines[i]);
    y += (n - 1) * 33;

    pg->dyn = (Rect){0, 0, 0, 0};
    if (st == UP_CHECKING) { // the "still working" crayon, as every wait in the app has it (here on the sheet, bigger)
        pg->dyn = (Rect){(int)TX - 12, (int)(y + 92 - 40), 680, 80};
        if (moving) doodles_draw(G.base, TX, y + 92, 3400.0f);
    } else if (st == UP_DOWNLOADING) {
        pg->dyn = (Rect){(int)TX - 4, (int)(y + 50 - 4), (int)BAR_W + 12, (int)BAR_H + 56};
        if (moving) bar_draw(G.base, TX, y + 50, pg->got, pg->total);
    } else if (tx->n) { // a list: the release's notes, what this version has, what is done, what to try; a dot of ink or a tick of crayon each
        float ly0 = y + 50;
        if (tx->label) {
            skin_txt(&c, u->med, 14.5f, 3.0f, TX, y + 50, 0, soft, tx->label);
            ly0 = y + 84;
        }
        for (int i = 0; i < tx->n && i < 4; i++) {
            float ly = ly0 + i * (tx->kind == LIST_TICKS ? 38 : 32);
            if (tx->kind == LIST_TICKS) {
                PaperSpr *tk = paper_tick(26, &PAPER_CRAYON_BLUE, 501u + (uint32_t)i);
                if (tk) paper_spr_put(G.base, tk, (int)(TX + 10), (int)(ly - 8));
                paper_spr_free(tk);
            } else
                gfx_circle(G.base, TX + 6, ly - 7, 4.5f, hex(PAL(ink, PAPER_INK), 0.85f));
            float ix = TX + (tx->kind == LIST_TICKS ? 32 : 22);
            char fit[160];
            text_fit(u->med, 20, 0, tx->items[i], SX + SW - 40 - ix, fit, sizeof fit);
            skin_txt(&c, u->med, 20, 0, ix, ly, 0, ink, fit);
        }
    }
}

static DItem *up_button(Scene *sc, float x, float y, const char *label, const char *glyph, uint32_t fill, uint32_t gfill)
{
    Skin *u = &G.skin;
    DItem *it = item_add(sc, x, y, BTN_W, BTN_H, 10.0f, LIFT_BTN, paper_lift_k[LIFT_BTN].scale);
    if (!it) return NULL;
    IBake ib = {BTN_W, BTN_H, 10.0f, fill, NULL, label, u->semi, 24, pal_light(fill) ? hex(PAPER_TEXT, 1) : u->th.text, glyph, gfill, u->th.text2, NULL, NULL, NULL}; // (a Paper night slip: pale writing)
    item_bake(it, 0, &ib);
    return it;
}

// the page's buttons, as a scene over G.base (the A one up); *focus = the one up, -1 if none
static Scene *page_scene(const Page *pg, int *ia, int *ib)
{
    Scene *sc = scene_new(G.base);
    *ia = *ib = -1;
    if (!sc) return NULL;
    float by = SY + SH - 40 - BTN_H, bx1 = SX + SW - 56 - BTN_W, bx0 = bx1 - BTN_GAP - BTN_W;
    int k = 0;
    if (pg->t.no) {
        up_button(sc, pg->t.yes ? bx0 : bx1, by, pg->t.no, "B", COL_CANCEL, paper_tape_rgb[0]);
        *ib = k++;
    }
    if (pg->t.yes) {
        up_button(sc, bx1, by, pg->t.yes, "A", COL_GO, PAPER_PAPER);
        *ia = k++;
    }
    return sc;
}

// shows a still state until A (1), B (0) or Y (2: back to the kept version, when there is one); a tap on a button is that button
static int page_wait(Page *pg)
{
    g_ui_update_last = pg->st;
    update_base(pg, 0);
    int ia, ib;
    Scene *sc = page_scene(pg, &ia, &ib);
    if (!sc) return 0;
    ui_transition();
    scene_frame(sc, 1);
    if (ia >= 0 || ib >= 0) scene_focus(sc, ia >= 0 ? ia : ib, 0);
    int rc = 0;
    for (;;) {
        int anim = scene_frame(sc, 0);
        uint64_t b = ui_poll(anim ? 0 : -1);
        if (b == UI_QUIT) break;
        if (b & BTN_A) {
            if (ia >= 0) {
                scene_press(sc, ia);
                rc = 1;
                break;
            }
            if (!pg->t.no) break; // (nothing to say yes to: A goes back like B)
        }
        if (b & BTN_B) {
            if (ib >= 0) scene_press(sc, ib);
            break;
        }
        if ((b & BTN_Y) && can_go_back(pg)) {
            rc = 2;
            break;
        }
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            while (ui_touch_next(&te))
                if (te.kind == TOUCH_TAP) {
                    int h = scene_hit(sc, te.x, te.y);
                    if (h >= 0 && h == ia) rc = 1;
                    if (h >= 0) goto done;
                }
        }
    }
done:
    scene_free(sc);
    return rc;
}

// ---- the work, on a thread of its own (the network waits; the page keeps drawing and listening for B)
typedef struct {
    char ver[32], url[512], date[24];
    uint64_t size;
    char notes[4][160];
    int nnotes;
} Rel;

enum { R_NONE, R_LATEST, R_NEWER, R_OFFLINE, R_MISSING, R_ERROR, R_DL_OK, R_DL_FAIL, R_SAVE_FAIL };

static struct {
    int job; // 1 asks for the latest release, 2 downloads it
    atomic_int done;
    volatile int cancel;
    int result, http;
    Rel rel;
    _Atomic uint64_t got, total;
#ifdef __SWITCH__
    Thread th;
#else
    pthread_t th;
#endif
} J;

// "v1.2.3" or "1.2.3" > "1.2.0"?
static int newer(const char *a, const char *b)
{
    int x[3] = {0}, y[3] = {0};
    sscanf(a + (*a == 'v' || *a == 'V'), "%d.%d.%d", &x[0], &x[1], &x[2]);
    sscanf(b + (*b == 'v' || *b == 'V'), "%d.%d.%d", &y[0], &y[1], &y[2]);
    for (int i = 0; i < 3; i++)
        if (x[i] != y[i]) return x[i] > y[i];
    return 0;
}

// a line of the release's notes as plain words: list marks, **bold**, `code` and [links](...) taken off
static void note_plain(const char *in, size_t n, char *out, size_t cap)
{
    size_t o = 0;
    for (size_t i = 0; i < n && o + 1 < cap; i++) {
        char ch = in[i];
        if (ch == '*' || ch == '`' || ch == '_' || ch == '[') continue;
        if (ch == ']' && i + 1 < n && in[i + 1] == '(') { // (the link's address goes)
            while (i < n && in[i] != ')') i++;
            continue;
        }
        if (ch == '\r') continue;
        out[o++] = ch;
    }
    out[o] = 0;
}

static int parse_release(const char *txt, size_t len, Rel *r)
{
    memset(r, 0, sizeof *r);
    cJSON *j = cJSON_ParseWithLength(txt, len);
    if (!j) return 0;
    const cJSON *tag = cJSON_GetObjectItemCaseSensitive(j, "tag_name");
    int ok = cJSON_IsString(tag) && tag->valuestring[0];
    if (ok) snprintf(r->ver, sizeof r->ver, "%s", tag->valuestring + (tag->valuestring[0] == 'v' || tag->valuestring[0] == 'V'));
    const cJSON *pub = cJSON_GetObjectItemCaseSensitive(j, "published_at");
    if (cJSON_IsString(pub)) snprintf(r->date, sizeof r->date, "%.10s", pub->valuestring);
    const cJSON *assets = cJSON_GetObjectItemCaseSensitive(j, "assets"), *a;
    cJSON_ArrayForEach(a, assets)
    {
        const cJSON *name = cJSON_GetObjectItemCaseSensitive(a, "name"), *url = cJSON_GetObjectItemCaseSensitive(a, "browser_download_url"),
                    *size = cJSON_GetObjectItemCaseSensitive(a, "size");
        if (cJSON_IsString(name) && cJSON_IsString(url) && !strcasecmp(name->valuestring, "washi.nro")) {
            snprintf(r->url, sizeof r->url, "%s", url->valuestring);
            r->size = cJSON_IsNumber(size) && size->valuedouble > 0 ? (uint64_t)size->valuedouble : 0;
        }
    }
    const cJSON *body = cJSON_GetObjectItemCaseSensitive(j, "body");
    if (cJSON_IsString(body)) { // the notes: the lines that are list items, the first four
        const char *p = body->valuestring;
        while (*p && r->nnotes < 4) {
            const char *e = strchr(p, '\n');
            size_t n = e ? (size_t)(e - p) : strlen(p);
            const char *q = p;
            while (q < p + n && (*q == ' ' || *q == '\t')) q++;
            if (q + 1 < p + n && (*q == '-' || *q == '*' || *q == '+') && q[1] == ' ') {
                note_plain(q + 2, (size_t)(p + n - q - 2), r->notes[r->nnotes], sizeof r->notes[0]);
                if (r->notes[r->nnotes][0]) r->nnotes++;
            }
            p += n + (e ? 1 : 0);
        }
    }
    cJSON_Delete(j);
    return ok;
}

static void dl_progress(void *ud, uint64_t got, uint64_t total)
{
    (void)ud;
    atomic_store(&J.got, got);
    if (total) atomic_store(&J.total, total);
}

static void job_run(void *arg)
{
    (void)arg;
    Platform *P = G.plat;
    uint8_t *b = NULL;
    size_t n = 0;
    int status = 0;
    if (J.job == 1) {
        int on = P->online ? P->online(P) : -1;
        if (on == 0) J.result = R_OFFLINE;
        else if (P->http_get(P, UPDATE_API, &b, &n, &status, &J.cancel, NULL, NULL) != 0) J.result = J.cancel ? R_NONE : R_OFFLINE;
        else {
            J.http = status;
            J.result = status == 404 ? R_MISSING : status != 200 ? R_ERROR : !parse_release((const char *)b, n, &J.rel) ? R_ERROR : newer(J.rel.ver, APP_VERSION) && J.rel.url[0] ? R_NEWER : R_LATEST;
        }
    } else {
        if (J.rel.size) atomic_store(&J.total, J.rel.size);
        if (P->http_get(P, J.rel.url, &b, &n, &status, &J.cancel, dl_progress, NULL) != 0) J.result = J.cancel ? R_NONE : R_DL_FAIL;
        else if (status != 200 || n < 0x80 || (J.rel.size && n != J.rel.size) || memcmp(b + 0x10, "NRO0", 4)) J.result = R_DL_FAIL; // (whole, and an NRO)
        else J.result = P->install_update && P->install_update(P, b, n) == 0 ? R_DL_OK : R_SAVE_FAIL;
    }
    free(b);
    atomic_store(&J.done, 1);
}

#ifndef __SWITCH__
static void *job_pt(void *arg)
{
    job_run(arg);
    return NULL;
}
#endif

static int job_start(int job)
{
    J.job = job;
    J.cancel = 0;
    J.result = R_NONE;
    J.http = 0;
    atomic_store(&J.done, 0);
    atomic_store(&J.got, 0);
    atomic_store(&J.total, 0);
#ifdef __SWITCH__
    if (R_FAILED(threadCreate(&J.th, job_run, NULL, NULL, 0x80000, 0x2D, -2))) return -1; // (below the main thread; curl and TLS want a big stack)
    if (R_FAILED(threadStart(&J.th))) {
        threadClose(&J.th);
        return -1;
    }
#else
    if (pthread_create(&J.th, NULL, job_pt, NULL) != 0) return -1;
#endif
    return 0;
}

static void job_join(void)
{
#ifdef __SWITCH__
    threadWaitForExit(&J.th);
    threadClose(&J.th);
#else
    pthread_join(J.th, NULL);
#endif
}

// ---- the pages made from what GitHub said
static void page_set(Page *pg, int st)
{
    memset(pg, 0, sizeof *pg);
    pg->st = st;
    pg->t = MOCK[st]; // (the fixed words; what depends on the release is made below)
    const Rel *r = &J.rel;
    char size[24] = "";
    if (r->size) ui_format_bytes(r->size, size, sizeof size);
    pg->t.n = 0;
    switch (st) {
    case UP_LATEST:
        snprintf(pg->detail, sizeof pg->detail, "Version %s is the newest. Checked just now.", APP_VERSION);
        if (r->nnotes) {
            snprintf(pg->label, sizeof pg->label, "IN %s", r->ver);
            pg->t.label = pg->label;
            pg->t.kind = LIST_DOTS;
        } else
            pg->t.label = NULL;
        for (int i = 0; i < r->nnotes; i++) snprintf(pg->items[i], sizeof pg->items[i], "%s", r->notes[i]);
        pg->t.n = r->nnotes;
        break;
    case UP_AVAILABLE:
        snprintf(pg->newv, sizeof pg->newv, "%s", r->ver);
        snprintf(pg->head, sizeof pg->head, "Version %s is out", r->ver);
        pg->t.head = pg->head;
        if (size[0]) snprintf(pg->detail, sizeof pg->detail, "You have %s. The update is %s.", APP_VERSION, size);
        else snprintf(pg->detail, sizeof pg->detail, "You have %s.", APP_VERSION);
        pg->t.label = r->nnotes ? "WHAT\xE2\x80\x99S NEW" : NULL;
        for (int i = 0; i < r->nnotes; i++) snprintf(pg->items[i], sizeof pg->items[i], "%s", r->notes[i]);
        pg->t.n = r->nnotes;
        break;
    case UP_DOWNLOADING:
        snprintf(pg->head, sizeof pg->head, "Downloading %s", r->ver);
        pg->t.head = pg->head;
        break;
    case UP_READY:
        snprintf(pg->head, sizeof pg->head, "%s is ready", r->ver);
        pg->t.head = pg->head;
        if (size[0]) snprintf(pg->items[0], sizeof pg->items[0], "Downloaded, all %s of it", size);
        else snprintf(pg->items[0], sizeof pg->items[0], "Downloaded, all of it");
        snprintf(pg->items[1], sizeof pg->items[1], "Checked that nothing is missing");
        snprintf(pg->items[2], sizeof pg->items[2], "%s kept as a backup, and nobody\xE2\x80\x99s saves change", APP_VERSION);
        pg->t.n = 3;
        break;
    case UP_OFFLINE:
        for (int i = 0; i < 3; i++) snprintf(pg->items[i], sizeof pg->items[i], "%s", MOCK[st].items[i]);
        pg->t.n = 3;
        break;
    case UP_MISSING:
        snprintf(pg->items[0], sizeof pg->items[0], "%s", MOCK[st].items[0]);
        pg->t.n = 1;
        break;
    case UP_ERROR:
        if (J.result == R_DL_FAIL) snprintf(pg->detail, sizeof pg->detail, "The download didn\xE2\x80\x99t arrive whole. Nothing has changed: try again.");
        else if (J.result == R_SAVE_FAIL) snprintf(pg->detail, sizeof pg->detail, "It couldn\xE2\x80\x99t be written to the SD card. Is the card full? Nothing has changed.");
        else if (J.http) snprintf(pg->detail, sizeof pg->detail, "GitHub answered with an error (%d). Try again in a few minutes.", J.http);
        else snprintf(pg->detail, sizeof pg->detail, "GitHub\xE2\x80\x99s answer couldn\xE2\x80\x99t be read. Try again in a few minutes.");
        break;
    default:
        break;
    }
    if (pg->detail[0]) pg->t.detail = pg->detail;
    for (int i = 0; i < pg->t.n; i++) pg->t.items[i] = pg->items[i];
}

// the checking or the downloading page while the work goes on: the crayon draws (at least most of a second, so it reads) or the bar fills; B stops it.
// Returns the work's result, or -1 if it was stopped.
static int job_page(int job)
{
    Page pg;
    page_set(&pg, job == 1 ? UP_CHECKING : UP_DOWNLOADING);
    g_ui_update_last = pg.st;
    if (job_start(job) != 0) return R_ERROR;
    update_base(&pg, 0);
    Surf *clean = surf_clone(G.base); // (the page without its moving part, to draw each frame's over)
    int ia, ib;
    Scene *sc = page_scene(&pg, &ia, &ib);
    ui_transition();
    if (sc) {
        scene_frame(sc, 1);
        if (ib >= 0) scene_focus(sc, ib, 0);
    }
    uint64_t t0 = ui_now();
    int stopped = 0;
    for (;;) { // (the moving part is drawn and shown every frame, so the loop goes at the screen's pace; the poll does not wait)
        uint64_t now = ui_now();
        if (atomic_load(&J.done) && (job == 2 || now - t0 >= 900)) break;
        Rect d = pg.dyn;
        if (clean && d.w > 0) {
            for (int y = d.y; y < d.y + d.h; y++)
                memcpy(G.base->px + ((size_t)y * SCREEN_W + d.x) * 4, clean->px + ((size_t)y * SCREEN_W + d.x) * 4, (size_t)d.w * 4);
            if (job == 1) doodles_draw(G.base, TX, d.y + 40, (float)(now - t0));
            else bar_draw(G.base, TX, d.y + 4, atomic_load(&J.got), atomic_load(&J.total));
            if (sc) scene_inval(sc, d.x, d.y, d.w, d.h);
        }
        if (sc) scene_frame(sc, 0);
        uint64_t b = ui_poll(0);
        if (b == UI_QUIT || (b & BTN_B)) {
            if (sc && ib >= 0 && (b & BTN_B)) scene_press(sc, ib);
            J.cancel = 1;
            stopped = 1;
            break;
        }
    }
    job_join(); // (a stopped transfer gives up within a moment)
    scene_free(sc);
    surf_free(clean);
    return stopped ? -1 : J.result;
}

// Y: back to the version kept from before the last update (asked first); 1 if it was put in place
static int go_back(const char *prev)
{
    Platform *P = G.plat;
    char head[96], detail[240];
    if (prev[0]) snprintf(head, sizeof head, "Go back to version %s?", prev);
    else snprintf(head, sizeof head, "Go back to the version before?");
    snprintf(detail, sizeof detail, "Washi closes and opens again on it. Nobody\xE2\x80\x99s saves change, and Check for updates can bring %s back.", APP_VERSION);
    if (ui_confirm(head, detail, "Go back", "Cancel", 0, NULL, 0) != 1 || G.quit) return 0;
    if (P->roll_back(P) != 0) {
        ui_message(UI_ERROR, "Couldn\xE2\x80\x99t go back", "Nothing has changed: this version stays.");
        return 0;
    }
    if (P->restart && P->restart(P)) { // (the kept one opens when this one closes)
        G.quit = 1;
        return 1;
    }
    ui_message(UI_OK, "Close Washi and open it again", "The earlier version opens then.");
    return 1;
}

int ui_rolled_back(char *newer, size_t cap)
{
    if (cap) newer[0] = 0;
    Platform *P = G.plat;
    return P && P->rolled_back && P->rolled_back(P, newer, cap);
}

// This app -> Check for updates
void ui_update(void)
{
    if (G.quit) return;
    if (g_ui_update_preview >= 0) {
        ui_update_preview(g_ui_update_preview);
        return;
    }
    Platform *P = G.plat;
    memset(&J.rel, 0, sizeof J.rel);
    if (!P->http_get) { // (a platform that can't: say so plainly)
        Page pg;
        page_set(&pg, UP_ERROR);
        pg.t.detail = "This console can\xE2\x80\x99t check for updates.";
        pg.t.yes = NULL;
        page_wait(&pg);
        ui_screen_done();
        return;
    }
    char prev[32] = "";
    int kept = P->previous && P->roll_back && P->previous(P, prev, sizeof prev);
    int st = UP_CHECKING;
    while (!G.quit) {
        if (st == UP_CHECKING) {
            int r = job_page(1);
            if (r < 0) break;
            st = r == R_LATEST ? UP_LATEST : r == R_NEWER ? UP_AVAILABLE : r == R_OFFLINE ? UP_OFFLINE : r == R_MISSING ? UP_MISSING : UP_ERROR;
            continue;
        }
        if (st == UP_DOWNLOADING) {
            int r = job_page(2);
            st = r < 0 || r == R_NONE ? UP_AVAILABLE : r == R_DL_OK ? UP_READY : UP_ERROR;
            continue;
        }
        Page pg;
        page_set(&pg, st);
        pg.kept = kept;
        snprintf(pg.prev, sizeof pg.prev, "%s", prev);
        int yes = page_wait(&pg);
        if (yes == 2) {
            if (go_back(prev)) break;
            continue; // (not now: the same page again)
        }
        if (G.quit || !yes) break;
        if (st == UP_LATEST) break;
        if (st == UP_AVAILABLE) st = UP_DOWNLOADING;
        else if (st == UP_READY) {
            if (P->restart && P->restart(P)) { // (the new one opens when this one closes)
                G.quit = 1;
                break;
            }
            ui_message(UI_OK, "Close Washi and open it again", "The new version opens then.");
            break;
        } else
            st = UP_CHECKING; // (try again)
    }
    ui_screen_done();
}

// the page in state `st` with the mockups' made-up release, until A or B (nothing happens)
void ui_update_preview(int st)
{
    if (G.quit || st < 0 || st >= UP_NSTATES) return;
    Page pg;
    memset(&pg, 0, sizeof pg);
    pg.st = st;
    pg.t = MOCK[st];
    snprintf(pg.newv, sizeof pg.newv, "1.1.0");
    pg.got = 9646899; // 9.2 MB of 18.4
    pg.total = 19293798;
    g_ui_update_last = st;
    update_base(&pg, 1);
    int ia, ib;
    Scene *sc = page_scene(&pg, &ia, &ib);
    if (!sc) return;
    ui_transition();
    scene_frame(sc, 1);
    if (ia >= 0 || ib >= 0) scene_focus(sc, ia >= 0 ? ia : ib, 0);
    for (;;) {
        int anim = scene_frame(sc, 0);
        uint64_t b = ui_poll(anim ? 0 : -1);
        if (b == UI_QUIT || (b & (BTN_A | BTN_B))) break;
        if (b & UI_TOUCH) {
            UiTouchEvt te;
            while (ui_touch_next(&te)) {}
        }
    }
    scene_free(sc);
    ui_screen_done();
}
