// End-to-end tests of the whole app on a PC: the real screens and flows (app.c + ui*.c + locker.c), a fake console, and
// scripted button presses. Each scenario also saves screenshots of what the console would show (test/out/app/<scenario>/).
// Build and run with `make -f Makefile.app`.
#define _GNU_SOURCE
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "../source/app.h"
#include "../source/fsx.h"
#include "../source/gfx/paper.h"
#include "../source/gfx/pins.h"
#include "../source/locker.h"
#include "../source/store.h"
#include "../source/sysprofile.h"
#include "../source/ui.h"
#include "../source/ui_internal.h"
#include "avatar_fixture.h"
#include "../source/custom_pics.h"
#include "../source/ui_thumbs.h"
#include "../source/cJSON.h"
#include "backend_host.h"
#include "platform_host.h"

extern const uint8_t sample_icon_jpg[];
extern const size_t sample_icon_jpg_len;

#define OUT "out/app"

// the home screen's test hooks (ui_home.c): the pixel oracle and the frame-time readout
extern int g_home_oracle, g_ui_debug_timing;
extern int g_ui_extra_pos, g_ui_extra_sel; // the Extra icons picker's focus (ui_extra.c)
extern int g_ui_icons_first;               // the Extra icons admin page's first row (ui_icons.c)
extern const char *g_ui_icons_a_says;      // what that page's footer says for A (ui_icons.c)
extern int g_ui_grid_sel, g_ui_grid_count; // the picture/colour grid's focus and size (ui_picker.c)
extern long g_home_oracle_frames, g_home_oracle_bad, g_home_oracle_soft;

static int g_checks = 0, g_failed = 0;
static const char *g_scenario = "";

#define CHECK(cond)                                                                    \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_failed++;                                                                \
            printf("  FAIL [%s] %s:%d: %s\n", g_scenario, __FILE__, __LINE__, #cond);  \
        }                                                                              \
    } while (0)

#define CHECK_MSG(cond, ...)                                                           \
    do {                                                                               \
        g_checks++;                                                                    \
        if (!(cond)) {                                                                 \
            g_failed++;                                                                \
            printf("  FAIL [%s] %s:%d: %s -- ", g_scenario, __FILE__, __LINE__, #cond); \
            printf(__VA_ARGS__);                                                       \
            printf("\n");                                                              \
        }                                                                              \
    } while (0)

enum {
    T_MK = 0x0100000000010000ull,    // Mario Kart 8 Deluxe
    T_SSBU = 0x0100000000020000ull,  // Super Smash Bros. Ultimate
    T_SPLAT = 0x0100000000030000ull, // Splatoon 3
    T_RL = 0x0100000000040000ull,    // Rocket League (installed, but nobody has a save yet)
    T_ACNH = 0x0100000000050000ull,  // Animal Crossing (a device save)
};

typedef struct {
    FakeBe fb;
    HostPlat hp;
    time_t now0;
} W;

static void w_init(W *w, const char *scenario, int console_saves)
{
    memset(w, 0, sizeof *w);
    g_scenario = scenario;
    fake_init(&w->fb);
    // DARK=1 ./app_host <scenario>: the same run with the console in its dark theme, screenshots in out/app-dark/
    int dark = getenv("DARK") != NULL;
    hp_init(&w->hp, dark ? OUT "-dark" : OUT, scenario);
    w->hp.dark_sys = dark;
    g_home_oracle = getenv("NO_ORACLE") == NULL; // every frame of the home screen is also drawn whole and compared (slower)
    g_home_oracle_frames = g_home_oracle_bad = g_home_oracle_soft = 0;
    g_ui_debug_timing = 0;
    struct tm tm;
    memset(&tm, 0, sizeof tm);
    tm.tm_year = 126; tm.tm_mon = 9; tm.tm_mday = 2; tm.tm_hour = 14; tm.tm_min = 14;
    w->now0 = timegm(&tm);
    w->fb.clock = w->now0;
    w->fb.clock_step = 0; // time stands still, so screenshots are repeatable
    snprintf(w->fb.accounts[0].uid, 33, "%032x", 0x11);
    snprintf(w->fb.accounts[0].name, 40, "Esports Switch");
    snprintf(w->fb.accounts[1].uid, 33, "%032x", 0x22);
    snprintf(w->fb.accounts[1].name, 40, "Guest");
    w->fb.naccounts = 2;
    w->fb.battery_pct = 82;
    w->fb.icon_data = sample_icon_jpg;
    w->fb.icon_size = sample_icon_jpg_len;
    fake_add_title(&w->fb, T_MK, 1, 0x400000);
    fake_add_title(&w->fb, T_SSBU, 1, 0x400000);
    fake_add_title(&w->fb, T_SPLAT, 1, 0x400000);
    fake_add_title(&w->fb, T_RL, 1, 0x400000);
    fake_add_title(&w->fb, T_ACNH, 1, 0x400000);
    fake_name_title(&w->fb, T_MK, "Mario Kart 8 Deluxe");
    fake_name_title(&w->fb, T_SSBU, "Super Smash Bros. Ultimate");
    fake_name_title(&w->fb, T_SPLAT, "Splatoon 3");
    fake_name_title(&w->fb, T_RL, "Rocket League");
    fake_name_title(&w->fb, T_ACNH, "Animal Crossing: New Horizons");
    w->fb.device_saves[0] = T_ACNH;
    w->fb.ndevice = 1;
    if (console_saves) {
        fake_put(&w->fb, T_MK, "profile.dat", "console: mario kart");
        fake_put_n(&w->fb, T_SSBU, "big.bin", 2 * 1024 * 1024 + 123, "smash");
        fake_put(&w->fb, T_SSBU, "small.dat", "console: smash");
        fake_put(&w->fb, T_SPLAT, "save.dat", "console: splatoon");
    }
}

static void w_done(W *w)
{
    // every present that only gave rectangles told the truth, and every framebuffer image ended up showing the frame
    CHECK_MSG(w->hp.dirty_violations == 0, "%ld pixels changed outside the rectangles a present claimed", w->hp.dirty_violations);
    CHECK_MSG(w->hp.fb_mismatches == 0, "%ld framebuffer pixels differ from the frame presented", w->hp.fb_mismatches);
    // the home screen made by rectangles is what a whole redraw makes (to within one level of a channel: turned pictures are sampled
    // from where a rectangle starts, which can differ from where the whole screen starts in the last bit)
    CHECK_MSG(g_home_oracle_bad == 0, "the home screen drawn by rectangles differs from a whole redraw (%ld pixels, %ld frames)", g_home_oracle_bad,
              g_home_oracle_frames);
    if (g_home_oracle_frames && getenv("ORACLE_REPORT"))
        printf("  oracle: %ld frames compared, %ld pixels differ at all, %ld by more than 1\n", g_home_oracle_frames, g_home_oracle_soft, g_home_oracle_bad);
    hp_free(&w->hp);
    fake_destroy(&w->fb);
}

static int run(W *w)
{
    return app_run(&w->hp.p, &w->fb.be, w->fb.sd);
}

// ---- seeding the SD card the way earlier sessions would have left it
static void put_profile_file(Store *st, const char *id, uint64_t title, const char *rel, const char *text)
{
    char tdir[17], p[FSX_PATH], f[FSX_PATH], parent[FSX_PATH];
    store_title_dir(title, tdir);
    snprintf(p, sizeof p, "profiles/%s/current/%s/%s", id, tdir, rel);
    store_path(st, f, sizeof f, p);
    snprintf(parent, sizeof parent, "%s", f);
    *strrchr(parent, '/') = 0;
    fsx_mkdir_p(parent);
    fsx_write_file(f, text, strlen(text));
}

static void seed_config(W *w, Store *st, int admin_pin, int intro)
{
    snprintf(st->cfg.target_uid, sizeof st->cfg.target_uid, "%s", w->fb.accounts[0].uid);
    st->cfg.sort_mode = SORT_NAME; // (the app's own default is most recently played)
    store_save_config(st);
    if (admin_pin) store_admin_set_pin(st, "123456");
    st->state.intro_done = intro;
    store_save_state(st);
}

static void seed_profile(W *w, Store *st, const char *name, const char *pin, int age_s, int ngames, char *id_out)
{
    static const uint64_t titles[4] = {T_MK, T_SSBU, T_SPLAT, T_RL};
    Profile p;
    store_profile_create(st, name, pin, &p);
    p.last_used = age_s >= 0 ? (int64_t)w->now0 - age_s : 0;
    store_profile_save(st, &p);
    for (int i = 0; i < ngames && i < 4; i++) {
        char text[96];
        snprintf(text, sizeof text, "%s's save for title %d", name, i);
        put_profile_file(st, p.id, titles[i], "save.dat", text);
    }
    if (id_out) snprintf(id_out, ID_MAX, "%s", p.id);
}

static int find_profile(Store *st, const char *name, Profile *out)
{
    Profile *list;
    size_t n;
    int found = 0;
    if (store_profile_list(st, &list, &n) == 0) {
        for (size_t i = 0; i < n; i++)
            if (!strcmp(list[i].name, name)) {
                *out = list[i];
                found = 1;
            }
        free(list);
    }
    return found;
}

static size_t count_dir(const char *dir)
{
    char **names;
    size_t n = 0;
    if (fsx_list(dir, &names, &n) != 0) return 0;
    fsx_list_free(names, n);
    return n;
}

// ------------------------------------------------------------------------------------------------ scenarios

// A brand-new console: the welcome questions, the first profile (with a password), the first swap, and leaving properly.
static void sc_first_run(void)
{
    W w;
    w_init(&w, "1-first-run", 1);
    HostPlat *h = &w.hp;
    hp_snap(h, "intro");
    hp_btn(h, BTN_A);
    hp_snap(h, "account");
    hp_btn(h, BTN_A);
    hp_snap(h, "admin-password-info");
    hp_btn(h, BTN_A);
    hp_pin(h, "246813"); // choose
    hp_pin(h, "246813"); // confirm
    hp_snap(h, "home-empty");
    hp_text(h, "Maya");
    hp_text(h, "MAY1234"); // the NetID question
    hp_btn(h, BTN_A); // the "New profile" tile
    hp_snap(h, "add-password-question");
    hp_btn(h, BTN_A);
    hp_pin(h, "135790");
    hp_pin(h, "135790");
    hp_snap(h, "home-after-create");
    hp_btn(h, BTN_A); // choose Maya
    hp_pin(h, "000000"); // wrong
    hp_snap(h, "wrong-password");
    hp_btn(h, BTN_B); // cancel
    hp_btn(h, BTN_A); // choose Maya again
    hp_pin(h, "135790");
    hp_snap(h, "confirm");
    hp_snap_after(h, 2, "switching-a");
    hp_snap_after(h, 12, "switching-b");
    hp_snap_after(h, 26, "switching-c");
    hp_btn(h, BTN_A);
    hp_snap(h, "ready");
    hp_btn(h, BTN_A);
    hp_snap(h, "home-loaded");
    hp_btn(h, BTN_X); // sign out
    hp_snap(h, "signed-out");
    int rc = run(&w);
    CHECK(rc == 0);

    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    Profile maya;
    CHECK(find_profile(&st, "Maya", &maya));
    CHECK(store_profile_has_pin(&maya) && store_profile_check_pin(&maya, "135790") && !store_profile_check_pin(&maya, "000000"));
    CHECK_MSG(!strcmp(maya.netid, "may1234"), "the NetID was kept, in lower case (%s)", maya.netid);
    CHECK(store_admin_check_pin(&st, "246813"));
    CHECK(!strcmp(st.state.holder, UNCLAIMED) && st.state.phase == PH_IDLE); // she signed out
    char cur[FSX_PATH], un[FSX_PATH];
    snprintf(cur, sizeof cur, "%s/profiles/%s/current", w.fb.sd, maya.id);
    snprintf(un, sizeof un, "%s/unclaimed", w.fb.sd);
    CHECK_MSG(count_dir(cur) == 0, "Maya played nothing, so her backup holds no games, not even empty ones (has %zu)", count_dir(cur));
    CHECK(st.state.wiped == 1); // and signing out cleared the console: nobody's saves are on it
    CHECK_MSG(count_dir(un) == 1, "the club's saves from before were kept in unclaimed/ (%zu)", count_dir(un));
    CHECK(st.state.intro_done == 1 && !strcmp(st.cfg.target_uid, w.fb.accounts[0].uid));
    w_done(&w);
}

static int px_near(const Surf *f, int x, int y, uint32_t rgb, int tol);

// How many tiles the grid's first row shows (paper cards: brighter than the board on either side of each slot's picture), noted down
// for the end.
static int g_row_tiles[8], g_row_n;

// the red of the 5 x 5 pixels round (x, y), averaged (paper is about 252, the board 241, grain and all)
static int mean_red(const Surf *f, int x, int y)
{
    int sum = 0;
    for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++) sum += f->px[((size_t)(y + j) * SCREEN_W + x + i) * 4];
    return sum / 25;
}

// the cards' paper shows round (x, y): the light theme's is all but white (its mean red at least light_min); Paper night's is its night-blue (the sky
// there is bluer or darker)
static int paper_at(const Surf *f, int x, int y, int light_min)
{
    if (pal_light(g_pal.paper)) return mean_red(f, x, y) >= light_min;
    int m[3] = {0, 0, 0};
    for (int j = -2; j <= 2; j++)
        for (int i = -2; i <= 2; i++)
            for (int c = 0; c < 3; c++) m[c] += f->px[((size_t)(y + j) * SCREEN_W + x + i) * 4 + c];
    for (int c = 0; c < 3; c++)
        if (abs(m[c] / 25 - (int)((g_pal.paper >> (16 - 8 * c)) & 255)) > 6) return 0;
    return 1;
}

static void row_tiles(HostPlat *h)
{
    int n = 0;
    for (int col = 0; col < 7; col++)
        if (paper_at(h->last, 40 + col * 164 + 12, 340, 248) || paper_at(h->last, 40 + col * 164 + 138, 340, 248)) n++;
    if (g_row_n < 8) g_row_tiles[g_row_n++] = n;
}

// A big club: scrolling, letter jumps, search, long and non-Latin names.
// The A-Z rail scrolls to a far letter (the disc runs through the letters between) instead of jumping there
extern float g_ui_home_rail_cur;
static float g_rail_seen[256];
static int g_rail_n;

static void rail_frame(HostPlat *h, const Surf *f)
{
    (void)h, (void)f;
    if (g_rail_n < 256) g_rail_seen[g_rail_n++] = g_ui_home_rail_cur;
}
static void rail_watch(HostPlat *h)
{
    g_rail_n = 0;
    h->ntrace = 0;
    h->trace_on = 1;
    h->trace_png = getenv("RAIL_PNG") != NULL; // RAIL_PNG=1 ./app_host many-profiles  keeps the frames of the jump
    h->frame_hook = rail_frame;
}
static void rail_check(HostPlat *h)
{
    h->frame_hook = NULL;
    h->trace_on = 0;
    h->trace_png = 0;
    int between = 0, back = 0;
    for (int i = 0; i < g_rail_n; i++) {
        float v = g_rail_seen[i];
        if (v > 0.2f && v < 5.8f && fabsf(v - roundf(v)) > 0.05f) between++;
        if (i && v < g_rail_seen[i - 1] - 0.001f) back++;
    }
    CHECK_MSG(between >= 4, "A to G on the rail: the disc passes the letters between (%d frames part way, of %d)", between, g_rail_n);
    CHECK_MSG(!back && g_rail_n > 0 && fabsf(g_rail_seen[g_rail_n - 1] - 6.0f) < 0.001f, "always forward, and it lands on G (%.2f)", g_rail_n ? g_rail_seen[g_rail_n - 1] : -1.0f);
}

// ./app_host home-drag: the player cards follow a finger up and down, a row at a time no longer: every frame of a drag is somewhere between
// rows, and let go they run on and come to rest on a whole row. Nothing is drawn over the search bar's row or the footer while they move.
extern float g_ui_home_scroll;
static float g_hd_min, g_hd_max;
static int g_hd_frac, g_hd_frames, g_hd_spill;
static Surf *g_hd_before;

static void hd_hook(HostPlat *h, const Surf *f)
{
    float x = g_ui_home_scroll;
    if (x < g_hd_min) g_hd_min = x;
    if (x > g_hd_max) g_hd_max = x;
    g_hd_frames++;
    if (fabsf(x - roundf(x)) > 0.02f) g_hd_frac++;
    for (int y = 0; y < SCREEN_H; y++) { // above the grid's window and below it, the screen is as it was before (bar the rail's place)
        if (y >= 260 && y < 666) continue; // (the focus's tape reaches up to 263: it lies down as the grid is taken)
        if (memcmp(f->px + (size_t)y * SCREEN_W * 4, g_hd_before->px + (size_t)y * SCREEN_W * 4, 1180 * 4)) g_hd_spill++;
    }
}

static void hd_watch(HostPlat *h)
{
    surf_free(g_hd_before);
    g_hd_before = surf_clone(h->last);
    g_hd_min = 1e9f;
    g_hd_max = -1e9f;
    g_hd_frac = g_hd_frames = g_hd_spill = 0;
    h->trace_on = 1;
    h->trace_png = 0;
    h->frame_hook = hd_hook;
}

static void hd_slow(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    CHECK_MSG(g_hd_frac >= 4 && g_hd_max > 0.2f && g_hd_max < 0.8f, "a slow push moves the cards with the finger, part of a row (%d frames between rows, as far as %.2f rows)", g_hd_frac, g_hd_max);
    CHECK_MSG(fabsf(g_ui_home_scroll) < 0.001f, "and, let go short of half a row, they settle back on the first row (%.3f)", g_ui_home_scroll);
    CHECK_MSG(g_hd_spill == 0, "nothing moved over the search bar's row or the footer (%d rows changed)", g_hd_spill);
}

static void hd_flick(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    float x = g_ui_home_scroll;
    CHECK_MSG(x >= 2.0f && fabsf(x - roundf(x)) < 0.001f, "a flick throws the cards on: they come to rest on a whole row, two or more down (%.3f)", x);
    CHECK_MSG(g_hd_frac >= 8, "running through every place between (%d frames between rows of %d)", g_hd_frac, g_hd_frames);
    CHECK_MSG(g_hd_spill == 0, "and never over the search bar's row or the footer (%d rows changed)", g_hd_spill);
}

// the grids' D-pad (Picture and Background, Extra icons' pictures): off an edge, round to the other end of the same row or column
static void grid_nav_check(HostPlat *h)
{
    (void)h;
    CHECK_MSG(ui_grid_nav(5, 30, 6, 1, 0) == 0 && ui_grid_nav(6, 30, 6, -1, 0) == 11, "right off a row's end goes to its start, left off its start to its end");
    CHECK_MSG(ui_grid_nav(2, 30, 6, 0, -1) == 26 && ui_grid_nav(26, 30, 6, 0, 1) == 2, "up off the top goes to the bottom of the column, down off the bottom to its top");
    CHECK_MSG(ui_grid_nav(5, 28, 6, 0, -1) == 23 && ui_grid_nav(27, 28, 6, 1, 0) == 24 && ui_grid_nav(24, 28, 6, -1, 0) == 27,
              "a short last row: up from a column it doesn't reach goes to that column's last cell; its row wraps on itself");
    CHECK_MSG(ui_grid_nav(23, 28, 6, 0, 1) == 27 && ui_grid_nav(27, 28, 6, 0, 1) == 3, "down into the short row lands on its last cell; down from it goes round to the top");
    CHECK_MSG(ui_grid_nav(8, 30, 6, 1, 0) == 9 && ui_grid_nav(8, 30, 6, 0, 1) == 14 && ui_grid_nav(0, 1, 6, 1, 0) == -1, "inside it steps as ever; one cell has nowhere to go");
}

// L and R on Home step through the letters: past the rows showing the cards glide to the new ones (every frame on the way between rows), not jump
static void hd_letters(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    float x = g_ui_home_scroll;
    CHECK_MSG(g_hd_max >= 2.0f && fabsf(x - roundf(x)) < 0.001f && g_hd_frac >= 3, "R through the letters: the cards glide down the grid (%d frames between rows, to %.2f), and rest on a row (%.3f)", g_hd_frac, g_hd_max, x);
}

extern int g_ui_home_focus;
static void hd_off_grid(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_home_focus != 0, "Up from the top row goes off the cards (focus %d)", g_ui_home_focus);
}
static void hd_on_grid(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_home_focus == 0, "L anywhere on Home takes the focus to the cards, at the letter before (focus %d)", g_ui_home_focus);
}

static void hd_back(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    CHECK_MSG(g_hd_min < 0.0f && fabsf(g_ui_home_scroll) < 0.001f, "pulled past the top they give a little (%.2f) and spring back to the first row (%.3f)", g_hd_min, g_ui_home_scroll);
    surf_free(g_hd_before);
    g_hd_before = NULL;
}

static void sc_home_drag(void)
{
    W w;
    w_init(&w, "2b-home-drag", 1);
    static const char *names[] = {"Aaliyah", "Alex", "Ben", "Chloe", "Dani", "Eli", "Fatima", "Gus", "Hana", "Ines", "Jordan", "Kai", "Lena", "Maya",
                                  "Noor", "Omar", "Priya", "Quinn", "Ravi", "Sam", "Theo", "Uma", "Vik", "Wren", "Xia", "Yusuf", "Zoe", "Zara"};
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) seed_profile(&w, &st, names[i], NULL, (int)(i * 5400 + 90), (int)(i % 4) + 1, NULL);
    HostPlat *h = &w.hp;
    hp_wait(h, 3);
    hp_call(h, hd_watch);
    hp_drag(h, 600, 600, 600, 540, 24); // slowly, under half a row
    hp_wait(h, 3);
    hp_call(h, hd_slow);
    hp_call(h, hd_watch);
    hp_drag(h, 600, 620, 600, 330, 4); // a flick
    hp_wait(h, 4);
    hp_snap(h, "flicked");
    hp_call(h, hd_flick);
    hp_call(h, hd_watch);
    hp_drag(h, 600, 330, 600, 640, 4); // a flick back, and on past the top
    hp_drag(h, 600, 330, 600, 640, 4);
    hp_wait(h, 4);
    hp_call(h, hd_back);
    hp_call(h, hd_watch);
    hp_btn_n(h, BTN_R, 20); // twenty letters on (a row is about seven): well past the two rows showing
    hp_wait(h, 2);
    hp_snap(h, "letters");
    hp_call(h, hd_letters);
    hp_call(h, grid_nav_check);
    hp_btn_n(h, BTN_UP, 5); // off the cards, up past the top row
    hp_call(h, hd_off_grid);
    hp_btn(h, BTN_L); // L from there: back on the cards, a letter back
    hp_wait(h, 1);
    hp_call(h, hd_on_grid);
    hp_btn(h, BTN_B); // B does nothing on Home
    hp_snap(h, "after-b");
    CHECK(run(&w) == 0);
    w_done(&w);
}

// ./app_host pins: an admin pins a profile (ZL + ZR + A on its card, then the admin password): it comes first in every order, the first pinned
// first; a search ranks as if nothing were pinned; the same chord again unpins it. The pin is kept in profile.json.
extern char g_ui_home_names[3][40];
static void pins_expect(const char *a, const char *b, const char *c, const char *what)
{
    CHECK_MSG(!strcmp(g_ui_home_names[0], a) && !strcmp(g_ui_home_names[1], b) && !strcmp(g_ui_home_names[2], c), "%s: %s, %s, %s (wanted %s, %s, %s)", what,
              g_ui_home_names[0], g_ui_home_names[1], g_ui_home_names[2], a, b, c);
}
static void pins_start(HostPlat *h) { (void)h; pins_expect("Ana", "Ben", "Cal", "in name order"); }
static void pins_dee(HostPlat *h) { (void)h; pins_expect("Dee", "Ana", "Ben", "Dee pinned: first"); }
static void pins_fay(HostPlat *h) { (void)h; pins_expect("Dee", "Fay", "Ana", "Fay pinned too: after Dee, who was pinned first"); }
static void pins_search(HostPlat *h) { (void)h; pins_expect("Ana", "Cal", "Fay", "a search for \"a\" ranks as if nothing were pinned"); }
static void pins_unpinned(HostPlat *h) { (void)h; pins_expect("Fay", "Ana", "Ben", "Dee unpinned: back in her place"); }
static void pins_recent(HostPlat *h) { (void)h; CHECK_MSG(!strcmp(g_ui_home_names[0], "Fay"), "in another order (Recently played) Fay is still first (%s)", g_ui_home_names[0]); }

static void sc_pins(void)
{
    W w;
    w_init(&w, "48-pins", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    st.cfg.sort_mode = SORT_NAME;
    store_save_config(&st);
    static const char *names[] = {"Ana", "Ben", "Cal", "Dee", "Eve", "Fay"};
    for (int i = 0; i < 6; i++) seed_profile(&w, &st, names[i], NULL, 3600 * (i + 1), 1, NULL);
    HostPlat *h = &w.hp;
    hp_call(h, pins_start);
    hp_btn_n(h, BTN_RIGHT, 3); // Dee
    hp_btn(h, BTN_PIN);
    hp_pin(h, "123456"); // the admin password
    hp_wait(h, 2);
    hp_snap(h, "dee-pinned");
    hp_call(h, pins_dee);
    hp_btn_n(h, BTN_RIGHT, 5); // Fay (Dee first now: Ana, Ben, Cal, Eve, Fay)
    hp_btn(h, BTN_PIN); // (the gate is open: no password for the next pin)
    hp_wait(h, 2);
    hp_snap(h, "fay-pinned");
    hp_call(h, pins_fay);
    hp_text(h, "a");
    hp_btn(h, BTN_Y);
    hp_wait(h, 2);
    hp_call(h, pins_search);
    hp_btn(h, BTN_MINUS); // everyone again
    hp_wait(h, 2);
    hp_btn_n(h, BTN_LEFT, 8); // back to Dee, the first
    hp_btn(h, BTN_PIN);
    hp_pin(h, "123456");
    hp_wait(h, 2);
    hp_call(h, pins_unpinned);
    hp_btn(h, BTN_R3); // the order menu: Recently played
    hp_btn(h, BTN_UP);
    hp_btn(h, BTN_A);
    hp_wait(h, 2);
    hp_call(h, pins_recent);
    CHECK(run(&w) == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Profile dee, fay;
    CHECK_MSG(find_profile(&st2, "Dee", &dee) && dee.pinned == 0 && find_profile(&st2, "Fay", &fay) && fay.pinned != 0, "kept in the profiles: Fay pinned, Dee not");
    w_done(&w);
}

// ./app_host pins-film: how a pinned card looks and moves. Pinned Dee (ZL + ZR + A and the password): her strip comes off and her pin goes in (after the
// grid has shuffled her to the front), the card takes the focus and the pin rises out of it; the focus leaves and the pin goes home, comes back with it; unpinned,
// the pin is pulled out and a new strip comes down on the card. PINS_FILM=1 keeps every second frame of the two animations in out/app/48-pins-film.
extern float g_ui_home_pin_h, g_ui_home_pin_x, g_ui_home_pin_y;
static int g_pf_n, g_pf_frames, g_pf_air, g_pf_mid;
static float g_pf_hmax, g_pf_hmin;
static const char *g_pf_tag = "in";
static long g_pf_seen;
static int g_pf_all;
static FILE *g_pf_times;
static void pf_hook(HostPlat *h, const Surf *f)
{
    (void)f;
    if (g_ui_home_pa_on) {
        g_pf_frames++;
        if (g_ui_home_tape_air) g_pf_air++;
    }
    if (getenv("PINS_FILM") && g_ui_home_frames != g_pf_seen) { // (a film of the home screen's own frames: out/app/48-pins-film/, with times.txt and cues.txt for ffmpeg)
        g_pf_seen = g_ui_home_frames;
        char nm[48];
        snprintf(nm, sizeof nm, "f%05d", ++g_pf_all);
        hp_save_png(h, nm);
        if (!g_pf_times) g_pf_times = fopen(getenv("DARK") ? "out/app-dark/48-pins-film/times.txt" : "out/app/48-pins-film/times.txt", "w");
        if (g_pf_times) fprintf(g_pf_times, "%d %llu\n", g_pf_all, (unsigned long long)h->t_ms);
    }
    if (getenv("PINS_TRACE") && g_ui_home_pin_h >= 0.0f) { // (diagnostic: the pin's height and how dark the card is just under it, frame by frame)
        long sum = 0;
        int n = 0;
        for (int y = (int)g_ui_home_pin_y + 12; y < (int)g_ui_home_pin_y + 40; y++)
            for (int x = (int)g_ui_home_pin_x - 14; x < (int)g_ui_home_pin_x + 24; x++) {
                const uint8_t *q = h->last->px + ((size_t)y * SCREEN_W + x) * 4;
                sum += q[0] + q[1] + q[2];
                n++;
            }
        printf("trace t=%llu h=%.2f dark=%.1f\n", (unsigned long long)h->t_ms, g_ui_home_pin_h, (double)sum / (3.0 * n));
    }
    if (g_ui_home_pin_h >= 0.0f) {
        if (g_ui_home_pin_h > g_pf_hmax) g_pf_hmax = g_ui_home_pin_h;
        if (g_ui_home_pin_h < g_pf_hmin) g_pf_hmin = g_ui_home_pin_h;
        if (g_ui_home_pin_h > 0.3f && g_ui_home_pin_h < 13.7f) g_pf_mid++;
    }
}
static void pf_start(HostPlat *h, const char *tag)
{
    h->trace_on = 1;
    h->frame_hook = pf_hook;
    g_pf_n = g_pf_frames = g_pf_air = g_pf_mid = 0;
    g_pf_hmax = 0.0f;
    g_pf_hmin = 99.0f;
    g_pf_tag = tag;
}
static void pf_in(HostPlat *h) { pf_start(h, "in"); }
static void pf_out(HostPlat *h) { pf_start(h, "out"); }
static void pf_move(HostPlat *h) { pf_start(h, "move"); }
// how much of the box round the pin's point differs strongly from the card's paper (the pin's plastic: nothing else is there but the paper)
static int pf_coloured(const HostPlat *h)
{
    int n = 0;
    const int x0 = (int)g_ui_home_pin_x - 16, y0 = (int)g_ui_home_pin_y - 22;
    const uint8_t *ref = h->last->px + ((size_t)(int)g_ui_home_pin_y * SCREEN_W + (int)g_ui_home_pin_x + 45) * 4;
    for (int y = y0; y < y0 + 30; y++)
        for (int x = x0; x < x0 + 32; x++) {
            if (x < 0 || y < 0 || x >= SCREEN_W || y >= SCREEN_H) continue;
            const uint8_t *p = h->last->px + ((size_t)y * SCREEN_W + x) * 4;
            int d = abs(p[0] - ref[0]);
            if (abs(p[1] - ref[1]) > d) d = abs(p[1] - ref[1]);
            if (abs(p[2] - ref[2]) > d) d = abs(p[2] - ref[2]);
            if (d >= 45) n++;
        }
    return n;
}
static void pf_after_in(HostPlat *h)
{
    CHECK_MSG(g_ui_home_pa_on == 0 && g_pf_frames >= 12 && g_pf_frames <= 40, "the pin animation played %d frames and is over (short: about 360 ms)", g_pf_frames);
    CHECK_MSG(g_pf_air >= 6, "the strip was in the air for %d frames (taken off, not just gone)", g_pf_air);
    CHECK_MSG(h->sounds[SND_PIN_IN] == 1 && h->sounds[SND_PIN_UP] == 0, "the pin goes in once (%d) and does not also rise afterwards (%d)", h->sounds[SND_PIN_IN], h->sounds[SND_PIN_UP]);
    CHECK_MSG(g_ui_home_pin_h > 13.0f && g_ui_home_pin_h < 15.0f, "in focus its pin is at its focus height: %.1f px", g_ui_home_pin_h);
    CHECK_MSG(g_pf_hmax >= 33.0f, "it came down from the air: highest %.1f px", g_pf_hmax);
    CHECK_MSG(g_pf_hmin >= 13.5f, "and never went further in than its focus height and back out: lowest %.1f px", g_pf_hmin);
    int c = pf_coloured(h);
    CHECK_MSG(c >= 120, "the pin's colour is there at its point (%d px)", c);
}
static void pf_after_leave(HostPlat *h)
{
    CHECK_MSG(g_ui_home_pin_h >= 0.0f && g_ui_home_pin_h < 0.5f, "the focus left: the pin is home (%.1f px)", g_ui_home_pin_h);
    CHECK_MSG(h->sounds[SND_PIN_HOME] >= 1, "its going home was heard (%d)", h->sounds[SND_PIN_HOME]);
    int c = pf_coloured(h);
    CHECK_MSG(c >= 120, "and the pin is still there, flat on the card (%d px)", c);
}
static void pf_after_back(HostPlat *h)
{
    CHECK_MSG(g_ui_home_pin_h > 13.0f && g_ui_home_pin_h < 15.0f, "the focus came back: the pin is up again (%.1f px)", g_ui_home_pin_h);
    CHECK_MSG(g_pf_mid >= 4 && g_pf_hmax >= 14.0f && g_pf_hmin < 0.5f, "it rose over %d frames from %.1f to %.1f px", g_pf_mid, g_pf_hmin, g_pf_hmax);
    CHECK_MSG(h->sounds[SND_PIN_UP] == 1, "and rose with its sound (%d)", h->sounds[SND_PIN_UP]);
}
static void pf_after_out(HostPlat *h)
{
    CHECK_MSG(g_ui_home_pa_on == 0 && g_pf_frames >= 10 && g_pf_frames <= 40, "the pin animation (out) played %d frames and is over (short: about 300 ms)", g_pf_frames);
    CHECK_MSG(g_pf_air >= 4, "a new strip came down for %d frames before it lay on the card", g_pf_air);
    CHECK_MSG(h->sounds[SND_PIN_OUT] == 1, "the pin was pulled out once (%d)", h->sounds[SND_PIN_OUT]);
    CHECK_MSG(g_ui_home_pin_h < 0.0f, "nobody is pinned now: no pin on the screen (%.1f)", g_ui_home_pin_h);
}
static void pf_cues(HostPlat *h)
{
    if (!getenv("PINS_FILM")) return;
    if (g_pf_times) fclose(g_pf_times);
    g_pf_times = NULL;
    FILE *f = fopen(getenv("DARK") ? "out/app-dark/48-pins-film/cues.txt" : "out/app/48-pins-film/cues.txt", "w");
    if (!f) return;
    for (int i = 0; i < h->nplog; i++) fprintf(f, "%llu %d %d %.3f\n", (unsigned long long)h->plog[i].t, h->plog[i].snd, h->plog[i].delay, h->plog[i].gain);
    fclose(f);
}
static void pf_blob(HostPlat *h)
{
    (void)h;
    CHECK_MSG(pins_ready(), "the pins' pictures are at hand (data/pins.bin)");
}

static void sc_pins_film(void)
{
    W w;
    w_init(&w, "48-pins-film", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    st.cfg.sort_mode = SORT_NAME;
    store_save_config(&st);
    static const char *names[] = {"Ana", "Ben", "Cal", "Dee", "Eve", "Fay"};
    for (int i = 0; i < 6; i++) seed_profile(&w, &st, names[i], NULL, 3600 * (i + 1), 1, NULL);
    HostPlat *h = &w.hp;
    if (!getenv("NO_PINS")) hp_call(h, pf_blob);
    hp_btn_n(h, BTN_RIGHT, 3); // Dee
    hp_wait(h, 1);
    hp_call(h, pf_in);
    hp_btn(h, BTN_PIN);
    hp_pin(h, "123456");
    hp_wait(h, 4);
    hp_snap(h, "in-end");
    if (!getenv("NO_PINS")) hp_call(h, pf_after_in);
    hp_btn(h, BTN_RIGHT); // the focus leaves her (Ana is next)
    hp_wait(h, 2);
    hp_snap(h, "left");
    if (!getenv("NO_PINS")) hp_call(h, pf_after_leave);
    hp_call(h, pf_move);
    hp_btn(h, BTN_LEFT); // and comes back
    hp_wait(h, 2);
    hp_snap(h, "back");
    if (!getenv("NO_PINS")) hp_call(h, pf_after_back);
    hp_call(h, pf_out);
    hp_btn(h, BTN_PIN); // (the gate is still open: no password)
    hp_wait(h, 4);
    hp_snap(h, "out-end");
    if (!getenv("NO_PINS")) hp_call(h, pf_after_out);
    hp_call(h, pf_cues);
    CHECK(run(&w) == 0);
    w_done(&w);
}

static void sc_many_profiles(void)
{
    W w;
    w_init(&w, "2-many-profiles", 1);
    static const char *names[] = {"Aaliyah", "Alex", "Ben", "Chloe", "Dani", "Eli", "Fatima", "Gus", "Hana", "Ines", "Jordan", "Jos\xC3\xA9",
                                  "Kai", "Lena", "Maximilian-Alexander", "Maya", "Noor", "Omar", "Priya", "Quinn", "Ravi", "\xE3\x81\x95\xE3\x81\x8F\xE3\x82\x89",
                                  "Sam", "Theo", "Uma", "Vik", "Wren", "Zoe"};
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX] = "";
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        const char *pin = (i == 2 || i == 16) ? "111111" : NULL;
        char id[ID_MAX];
        seed_profile(&w, &st, names[i], pin, (int)(i * 5400 + 90), (int)(i % 4) + 1, id);
        if (!strcmp(names[i], "Maya")) snprintf(mayaid, sizeof mayaid, "%s", id);
    }
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    st.state.loaded_at = (int64_t)w.now0 - 2200;
    store_save_state(&st);

    HostPlat *h = &w.hp;
    hp_snap(h, "home-top");
    hp_call(h, rail_watch);
    hp_btn(h, BTN_DOWN); // Aaliyah to Gus, a row down: the rail from A to G
    hp_call(h, rail_check);
    hp_btn(h, BTN_DOWN);
    hp_snap(h, "scrolled");
    hp_btn(h, BTN_R); // next letter
    hp_snap(h, "next-letter");
    hp_btn(h, BTN_UP);
    hp_btn(h, BTN_LEFT);
    g_row_n = 0;
    hp_call(h, row_tiles); // a full row
    hp_text(h, "aya");
    hp_btn(h, BTN_Y); // search: Maya (it's in her name), then Aaliyah (its letters in order), then New profile
    hp_snap(h, "search-result");
    hp_call(h, row_tiles);
    hp_text(h, "zzz");
    hp_btn(h, BTN_Y);
    hp_snap(h, "search-no-match"); // only New profile
    hp_call(h, row_tiles);
    hp_wait(h, 5); // the toast fades
    hp_snap(h, "toast-gone");
    hp_btn(h, BTN_MINUS); // minus clears the search
    hp_snap(h, "search-cleared");
    hp_call(h, row_tiles);
    hp_btn(h, BTN_B); // B does nothing on Home (Washi is closed with the console's HOME button)
    hp_snap(h, "b-does-nothing");
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, mayaid), "Maya is still loaded (she was not signed out)");
    CHECK_MSG(g_row_n == 4 && g_row_tiles[0] == 7 && g_row_tiles[1] == 3 && g_row_tiles[2] == 1 && g_row_tiles[3] == 7,
              "the grid narrows to who matches and back (%d rows: %d %d %d %d)", g_row_n, g_row_tiles[0], g_row_tiles[1],
              g_row_tiles[2], g_row_tiles[3]);
    w_done(&w);
}

// Five wrong passwords, the wait, and getting back in later.
static void sc_password_lockout(void)
{
    W w;
    w_init(&w, "3-password-lockout", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Noor", "111111", 86400 * 4, 3, NULL);
    seed_profile(&w, &st, "Sam", NULL, 3600 * 5, 2, NULL);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_A); // Noor (first alphabetically)
    hp_pin(h, "000000");
    hp_snap(h, "wrong-1");
    hp_pin(h, "000000");
    hp_pin(h, "000000");
    hp_pin(h, "000000");
    hp_pin(h, "000000"); // the fifth
    hp_snap(h, "locked-out");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // try again straight away
    hp_snap(h, "still-locked");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B); // nobody is loaded, so this leaves
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK(!strcmp(st2.state.holder, UNCLAIMED));
    w_done(&w);
}

// ---- the admin hub: where each row is (section, row), and how to get to one from wherever the focus is
enum { HS_PEOPLE, HS_GAMES, HS_CONSOLE, HS_APP };
#define HUB_PROFILES HS_PEOPLE, 0
#define HUB_DELETED HS_PEOPLE, 1
#define HUB_ADMIN_PW HS_PEOPLE, 2
#define HUB_SHARED HS_GAMES, 0
#define HUB_STORAGE HS_GAMES, 1
#define HUB_UNCLAIMED HS_GAMES, 2
#define HUB_BACKUP HS_GAMES, 3
#define HUB_ACCOUNT HS_CONSOLE, 0
#define HUB_SYSPROFILE HS_CONSOLE, 1
#define HUB_REMINDER HS_CONSOLE, 2
#define HUB_REMINDER_TEST HS_CONSOLE, 3
#define HUB_APPEARANCE HS_APP, 0
#define HUB_LOG HS_APP, 1
#define HUB_UPDATE HS_APP, 2
#define HUB_ICONS HS_APP, 3
#define HUB_ABOUT HS_APP, 4
#define HUB_PEOPLE_ROWS 3

typedef struct { int sec, row, in_rows; } HubPos; // starts as {0, 0, 0}: the focus on the first section

static void hub_section(HostPlat *h, HubPos *p, int sec)
{
    if (p->in_rows) {
        hp_btn(h, BTN_LEFT);
        p->in_rows = 0;
    }
    for (; p->sec < sec; p->sec++, p->row = 0) hp_btn(h, BTN_DOWN);
    for (; p->sec > sec; p->sec--, p->row = 0) hp_btn(h, BTN_UP);
}

static void hub_open(HostPlat *h, HubPos *p, int sec, int row)
{
    if (p->sec != sec) hub_section(h, p, sec);
    if (!p->in_rows) {
        hp_btn(h, BTN_A); // into the rows
        p->in_rows = 1;
    }
    for (; p->row < row; p->row++) hp_btn(h, BTN_DOWN);
    for (; p->row > row; p->row--) hp_btn(h, BTN_UP);
    hp_btn(h, BTN_A);
}

static void hub_leave(HostPlat *h, HubPos *p)
{
    if (p->in_rows) hp_btn(h, BTN_B); // back to the sections
    hp_btn(h, BTN_B);                 // and out
    p->in_rows = 0;
}

// The admin area end to end.
static Store *g_admin_st;
static char g_admin_alex[ID_MAX];

static void hd_readonly_on(HostPlat *h)
{
    (void)h;
    Profile pp;
    CHECK(store_profile_load(g_admin_st, g_admin_alex, &pp) == 0 && pp.readonly == 1);
}

static void hd_readonly_name_kept(HostPlat *h)
{
    (void)h;
    Profile pp;
    CHECK(store_profile_load(g_admin_st, g_admin_alex, &pp) == 0 && pp.readonly == 1 && !strcmp(pp.name, "Alexander"));
}

static void sc_admin(void)
{
    W w;
    w_init(&w, "4-admin", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char alexid[ID_MAX], mayaid[ID_MAX];
    seed_profile(&w, &st, "Alex", "222222", 3600 * 3, 3, alexid);
    seed_profile(&w, &st, "Maya", NULL, 60 * 20, 3, mayaid);
    seed_profile(&w, &st, "Priya", NULL, 86400 * 2, 2, NULL);
    // Alex has earlier saves
    char hist[FSX_PATH];
    const char *stamps[3] = {"20261001-211500", "20261001-163000", "20260929-191200"};
    for (int i = 0; i < 3; i++) {
        char tdir[17], f[FSX_PATH];
        store_title_dir(i == 2 ? T_MK : T_SSBU, tdir);
        snprintf(hist, sizeof hist, "profiles/%s/history/%s/%s", alexid, stamps[i], tdir);
        store_path(&st, f, sizeof f, hist);
        fsx_mkdir_p(f);
        strcat(f, "/save.dat");
        char text[64];
        snprintf(text, sizeof text, "alex older save %d", i);
        fsx_write_file(f, text, strlen(text));
    }
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    st.state.loaded_at = (int64_t)w.now0 - 1500;
    store_save_state(&st);

    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_snap(h, "admin-password");
    hp_pin(h, "123456");
    hp_snap(h, "admin-hub");
    hub_open(h, &hub, HUB_PROFILES);
    hp_snap(h, "profiles");
    hp_btn(h, BTN_A); // Alex
    hp_snap(h, "profile-page");
    hp_text(h, "Alexander");
    hp_btn(h, BTN_A); // Rename
    hp_btn(h, BTN_DOWN);
    hp_text(h, "ABC1234");
    hp_btn(h, BTN_A); // NetID
    hp_snap(h, "profile-page-netid");
    hp_btn_n(h, BTN_DOWN, 4); // Picture and background (Remove NetID, Change password and Remove password come first)
    hp_btn(h, BTN_A);         // Edit Icon (no pictures are loaded in this scenario, so just the letter)
    hp_snap(h, "edit-icon");
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);         // the Background grid
    hp_snap(h, "background-grid");
    hp_btn(h, BTN_RIGHT);     // the next swatch
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);         // OK
    hp_snap(h, "renamed-recoloured");
    hp_btn(h, BTN_DOWN); // Roll back
    hp_btn(h, BTN_A);
    hp_snap(h, "earlier-saves");
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // the second most recent
    hp_snap(h, "roll-back-confirm");
    hp_snap_after(h, 2, "rolling-back");
    hp_btn(h, BTN_A);
    hp_snap(h, "rolled-back");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_DOWN); // Read-only: asks first
    hp_btn(h, BTN_A);
    hp_snap(h, "readonly-confirm");
    hp_btn(h, BTN_A);
    hp_snap(h, "readonly-on");
    g_admin_st = &st;
    snprintf(g_admin_alex, sizeof g_admin_alex, "%s", alexid);
    hp_call(h, hd_readonly_on); // (the script runs later, when the app is idle: so the check is a step in it)
    hp_btn_n(h, BTN_UP, 7);     // up to Rename: refused while read-only
    hp_btn(h, BTN_A);
    hp_snap(h, "readonly-locked");
    hp_btn(h, BTN_A);
    hp_call(h, hd_readonly_name_kept);
    hp_btn_n(h, BTN_DOWN, 7);   // back to Read-only
    hp_btn(h, BTN_DOWN); // Delete profile
    hp_btn(h, BTN_A);
    hp_snap(h, "delete-confirm");
    hp_btn(h, BTN_B); // cancel
    hp_btn(h, BTN_B); // back to the profile list
    hp_btn(h, BTN_B); // back to the hub
    hp_snap(h, "hub-back-on-profiles");
    hub_open(h, &hub, HUB_ADMIN_PW);
    hp_pin(h, "654321"); // a new admin password
    hp_pin(h, "654321");
    hp_snap(h, "admin-password-changed");
    hp_btn(h, BTN_A);
    hub_open(h, &hub, HUB_SHARED);
    hp_snap(h, "shared-games");
    hp_btn(h, BTN_A); // toggle the first game
    hp_snap(h, "shared-toggled");
    hp_btn(h, BTN_B);
    hub_open(h, &hub, HUB_ACCOUNT);
    hp_snap(h, "account");
    hp_btn(h, BTN_B);
    hub_open(h, &hub, HUB_SYSPROFILE);
    hp_snap(h, "console-profile");
    hp_btn(h, BTN_A); // check that it works
    hp_snap(h, "console-profile-works");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // show the player's name: asks first
    hp_snap(h, "console-profile-confirm");
    hp_btn(h, BTN_A); // turn on
    hp_snap(h, "console-profile-on");
    hp_btn(h, BTN_B);
    hub_open(h, &hub, HUB_LOG);
    hp_snap(h, "log");
    hp_btn(h, BTN_B);
    hub_open(h, &hub, HUB_ABOUT);
    hp_snap(h, "about");
    hp_btn(h, BTN_B);
    hub_open(h, &hub, HUB_APPEARANCE);
    hp_snap(h, "appearance");
    hp_btn(h, BTN_A); // Paper
    hp_btn(h, BTN_A); // Paper night
    hp_snap(h, "appearance-dark");
    hp_btn(h, BTN_B); // the hub in the dark theme, section by section
    hp_snap(h, "hub-dark-rows");
    hub_section(h, &hub, HS_PEOPLE);
    hp_snap(h, "hub-dark-people");
    hub_section(h, &hub, HS_GAMES);
    hp_snap(h, "hub-dark-games");
    hub_section(h, &hub, HS_CONSOLE);
    hp_snap(h, "hub-dark-console");
    hub_open(h, &hub, HUB_APPEARANCE);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // 24-hour
    hp_btn(h, BTN_UP);
    hp_btn(h, BTN_A); // back to following the console
    hp_btn(h, BTN_B);
    hub_leave(h, &hub); // home
    hp_snap(h, "home-24h");
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // exit without signing out
    int rc = run(&w);
    CHECK(rc == 0);

    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Profile a;
    CHECK(find_profile(&st2, "Alexander", &a));
    CHECK_MSG(!strcmp(a.netid, "abc1234"), "the admin set a NetID (%s)", a.netid);
    CHECK_MSG(a.color == SWATCH_VIVID[0] + 1, "the next swatch was chosen in the background grid (%d)", a.color);
    CHECK_MSG(st2.cfg.sync_name == 1 && st2.cfg.sync_picture == 0, "the console's profile follows the name only (%d, %d)", st2.cfg.sync_name, st2.cfg.sync_picture);
    CHECK_MSG(!strcmp(w.fb.sys_nick, "Maya"), "the console's profile shows who is loaded (%s)", w.fb.sys_nick);
    CHECK_MSG(sp_has_original(&st2) && w.fb.sys_stores >= 1, "the original was saved before the first write");
    char cur[FSX_PATH], f[FSX_PATH];
    snprintf(cur, sizeof cur, "%s/profiles/%s/current", w.fb.sd, alexid);
    char tdir[17];
    store_title_dir(T_SSBU, tdir);
    CHECK(snprintf(f, sizeof f, "%s/%s/save.dat", cur, tdir) < (int)sizeof f);
    char *got = NULL;
    CHECK(fsx_read_all(f, &got, NULL) == 0);
    CHECK_MSG(got && !strcmp(got, "alex older save 1"), "rolled back to the second most recent: %s", got ? got : "(null)");
    free(got);
    CHECK_MSG(st2.cfg.nexcluded == 1, "one game was marked shared (%d)", st2.cfg.nexcluded);
    CHECK_MSG(st2.cfg.theme_mode == 0 && st2.cfg.clock24 == 1, "theme back to following the console, clock 24h (%d, %d)", st2.cfg.theme_mode, st2.cfg.clock24);
    CHECK_MSG(store_admin_check_pin(&st2, "654321") && !store_admin_check_pin(&st2, "123456"), "the admin password was changed from the hub");
    CHECK(!strcmp(st2.state.holder, mayaid));
    w_done(&w);
}

// The hub by buttons and by touch: the right-hand card follows the section, and the focus moves between the two cards.
static int px_near(const Surf *f, int x, int y, uint32_t rgb, int tol)
{
    const uint8_t *q = f->px + ((size_t)y * SCREEN_W + x) * 4;
    return abs(q[0] - (int)(rgb >> 16 & 255)) <= tol && abs(q[1] - (int)(rgb >> 8 & 255)) <= tol && abs(q[2] - (int)(rgb & 255)) <= tol;
}

// What the hub shows right now: whose rows are on the sheet (by the colour of the selected tab's left strip), which tab has slid right
// (the focus is on the tabs), and which slip is lifted (-1 = none): a lifted slip casts a deep shadow on the sheet just below its bottom
// edge. Each call notes it down; the scenario checks the notes at the end.
static struct { int hue, sec, row; } g_hub_seen[32];
static int g_hub_nseen;
static long g_hub_cyan;

// the hub's layout (ui_hub.c): tabs 62 tall every 78 px from y 176; the sheet from y 168; slips 88 tall every 100 px from 42 px down the
// sheet (68 tall every 78 px from 40 when a section has more than three rows); the sections hold 3, 5, 4 and 2 rows
enum { HUB_TAB_Y = 176, HUB_TAB_DY = 78, HUB_TAB_H = 62, HUB_SHEET_Y = 168 };
static const int g_hub_nrows[4] = {3, 5, 4, 2};
static const uint32_t g_hub_tape[4] = {0xAED6F9, 0xBFEFE1, 0xFFFF9A, 0xFFCDCC};

static int px_lum(const Surf *f, int x, int y)
{
    const uint8_t *q = f->px + ((size_t)y * SCREEN_W + x) * 4;
    return q[0] + q[1] + q[2];
}

static void hub_probe(HostPlat *h)
{
    if (g_hub_nseen >= 32 || !h->last) return;
    int hue = -1, sec = -1, row = -1;
    for (int i = 0; i < 4; i++) {
        int ty = HUB_TAB_Y + i * HUB_TAB_DY + HUB_TAB_H / 2;
        if (px_near(h->last, 58, ty, g_pal.tab_fill[i] ? g_pal.tab_strip[i] : g_hub_tape[i], 4)) hue = i; // the white tab's strip of its colour (the palette's own, if it has tab colours)
        if (hue == i && px_near(h->last, 398, ty, g_pal.paper, 8) && px_near(h->last, 398, ty - 12, g_pal.paper, 8)) sec = i; // slid 12 px right
    }
    if (hue >= 0) {
        for (int y = 100; y < 660; y++) // no cyan frame, glow or ring anywhere on the page (the old focus)
            for (int x = 0; x < SCREEN_W; x++)
                if (px_near(h->last, x, y, 0x00B8E6, 20)) g_hub_cyan++;
        int n = g_hub_nrows[hue], compact = n > 3, rh = compact ? 68 : 88, pitch = compact ? 78 : 100, top = compact ? 40 : 42;
        int lo = 1 << 30, lo2 = 1 << 30, at = -1;
        for (int r = 0; r < n; r++) {
            int l = px_lum(h->last, 1000, HUB_SHEET_Y + top + r * pitch + rh + 4);
            if (getenv("HUB_DEBUG")) printf("   hub probe %d: row %d shadow level %d\n", g_hub_nseen, r, l);
            if (l < lo) {
                lo2 = lo;
                lo = l;
                at = r;
            } else if (l < lo2) {
                lo2 = l;
            }
        }
        if (lo < lo2 - 30) row = at;
    }
    g_hub_seen[g_hub_nseen].hue = hue;
    g_hub_seen[g_hub_nseen].sec = sec;
    g_hub_seen[g_hub_nseen].row = row;
    g_hub_nseen++;
}

#define HUB_SEEN(i, h_, s_, r_)                                                                                              \
    CHECK_MSG(g_hub_nseen > (i) && g_hub_seen[i].hue == (h_) && g_hub_seen[i].sec == (s_) && g_hub_seen[i].row == (r_),      \
              "hub step %d shows section colour %d, tab slid on %d / slip lifted %d (wanted %d, %d, %d)", (i), g_hub_seen[i].hue, \
              g_hub_seen[i].sec, g_hub_seen[i].row, (h_), (s_), (r_))

// Moving between sections, the chosen tab is one white paper tab that slides 12 px right and does not lift: in every frame of the move
// exactly one tab is white (the choice never leaves the tabs or is in two places), the white tab's top edge stays where its place is
// (no rise), and the slide itself is seen on its way (its right edge between its resting place and 12 px further). Read at the middle
// of each tab, and down a column near the tabs' right ends.
static int g_plate_frames, g_plate_bad, g_plate_moving, g_plate_lifted, g_plate_edge_max;

static void plate_frame(HostPlat *h, const Surf *f)
{
    (void)h;
    int white = 0, which = -1;
    for (int i = 0; i < 4; i++) // (white: the cards' paper, Paper night's night-blue in the dark theme)
        if (px_near(f, 372, HUB_TAB_Y + i * HUB_TAB_DY + HUB_TAB_H / 2, g_pal.paper, 8)) {
            white++;
            which = i;
        }
    g_plate_frames++;
    if (white != 1) {
        g_plate_bad++;
        return;
    }
    int ty = HUB_TAB_Y + which * HUB_TAB_DY, bottom = -1;
    for (int y = ty + HUB_TAB_H + 12; y > ty + HUB_TAB_H / 2; y--) // the tab's bottom edge: where the white ends, going up the column
        if (px_near(f, 372, y, g_pal.paper, 8) && px_near(f, 372, y - 2, g_pal.paper, 8)) {
            bottom = y;
            break;
        }
    // (the top edge is not read: the tab that has just let go of the focus is above it for a moment, and its shadow falls there)
    if (bottom < ty + HUB_TAB_H - 4 || bottom > ty + HUB_TAB_H + 2) g_plate_lifted++;
    int edge = 372;
    for (int x = 372; x < 420 && px_near(f, x, ty + HUB_TAB_H / 2, g_pal.paper, 8); x++) edge = x;
    if (edge > g_plate_edge_max) g_plate_edge_max = edge;
    if (edge >= 392 && edge <= 400) g_plate_moving++; // between the resting place (390) and the slid one (402)
}

static void plate_watch(HostPlat *h)
{
    g_plate_frames = g_plate_bad = g_plate_moving = g_plate_lifted = g_plate_edge_max = 0;
    h->ntrace = 0;
    h->trace_on = 1;
    h->trace_png = getenv("TRACE_PNG") != NULL; // TRACE_PNG=1 ./app_host hub  keeps every frame of the move
    h->frame_hook = plate_frame;
}

static void plate_check(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    CHECK_MSG(g_plate_moving >= 2, "the chosen tab slid right (%d of %d frames on the way)", g_plate_moving, g_plate_frames);
    CHECK_MSG(g_plate_edge_max >= 400, "the chosen tab ended 12 px to the right (its edge at %d)", g_plate_edge_max);
    CHECK_MSG(g_plate_bad == 0, "one white tab in every frame of the move (%d of %d frames had none or two)", g_plate_bad, g_plate_frames);
    CHECK_MSG(g_plate_lifted == 0, "the chosen tab never lifts (%d of %d frames had its top edge off its place)", g_plate_lifted, g_plate_frames);
}

static void sc_hub(void)
{
    W w;
    w_init(&w, "21-admin-hub", 1);
    w.fb.free_bytes = 13314398618ull; // 12.4 GB
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, NULL);
    seed_profile(&w, &st, "Maya", NULL, 60 * 20, 3, NULL);
    seed_profile(&w, &st, "Priya", NULL, 86400 * 2, 2, NULL);
    seed_profile(&w, &st, "benjamin", NULL, 86400 * 3, 1, NULL);
    seed_profile(&w, &st, "olivia", NULL, 86400 * 9, 1, NULL);
    g_hub_nseen = 0;
    g_hub_cyan = 0;
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hp_snap(h, "people");
    hp_call(h, hub_probe); // 0
    hp_call(h, plate_watch);
    hp_btn(h, BTN_DOWN);
    hp_snap(h, "games-and-saves");
    hp_call(h, plate_check);
    hp_call(h, hub_probe); // 1
    hp_btn(h, BTN_DOWN);
    hp_snap(h, "console");
    hp_call(h, hub_probe); // 2
    hp_btn(h, BTN_DOWN);
    hp_snap(h, "this-app");
    hp_call(h, hub_probe); // 3
    hp_btn_rep(h, BTN_DOWN);   // there is no fifth section (a held press stops; a tap would wrap to the first)
    hp_call(h, hub_probe); // 4
    hp_btn_n(h, BTN_UP, 3);
    hp_call(h, hub_probe); // 5
    hp_btn(h, BTN_A);      // into the rows
    hp_snap(h, "people-rows");
    hp_call(h, hub_probe); // 6
    hp_btn(h, BTN_DOWN);
    hp_call(h, hub_probe); // 7
    hp_btn(h, BTN_DOWN);
    hp_btn_rep(h, BTN_DOWN); // held presses stop on the last row
    hp_btn_rep(h, BTN_DOWN);
    hp_snap(h, "people-last-row");
    hp_call(h, hub_probe); // 8
    hp_btn(h, BTN_LEFT);   // back to the sections
    hp_call(h, hub_probe); // 9
    hp_btn(h, BTN_RIGHT);  // and to the row it was on
    hp_call(h, hub_probe); // 10
    hp_btn(h, BTN_B);
    hp_call(h, hub_probe); // 11
    hp_tap(h, 200, 350);   // by touch: the Console section
    hp_snap(h, "tap-console");
    hp_call(h, hub_probe); // 12
    hp_tap(h, 800, 240);   // its first row: Account opens
    hp_snap(h, "tap-row-opens-account");
    hp_call(h, hub_probe); // 13: not the hub
    hp_tap(h, 1180, 686);  // the Back prompt: the hub again, on the row that was opened
    hp_call(h, hub_probe); // 14
    hp_tap(h, 1180, 686);  // Back: the sections
    hp_call(h, hub_probe); // 15
    hp_tap(h, 1180, 686);  // Back: home
    hp_snap(h, "home");
    hp_call(h, hub_probe); // 16: not the hub
    hp_btn(h, BTN_B);      // nobody is loaded, so this leaves
    int rc = run(&w);
    CHECK(rc == 0);
    HUB_SEEN(0, 0, 0, -1);
    HUB_SEEN(1, 1, 1, -1);
    HUB_SEEN(2, 2, 2, -1);
    HUB_SEEN(3, 3, 3, -1);
    HUB_SEEN(4, 3, 3, -1);
    HUB_SEEN(5, 0, 0, -1);
    HUB_SEEN(6, 0, -1, 0);
    HUB_SEEN(7, 0, -1, 1);
    HUB_SEEN(8, 0, -1, HUB_PEOPLE_ROWS - 1);
    HUB_SEEN(9, 0, 0, -1);
    HUB_SEEN(10, 0, -1, HUB_PEOPLE_ROWS - 1);
    HUB_SEEN(11, 0, 0, -1);
    HUB_SEEN(12, 2, 2, -1);
    HUB_SEEN(13, -1, -1, -1);
    HUB_SEEN(14, 2, -1, 0);
    HUB_SEEN(15, 2, 2, -1);
    HUB_SEEN(16, -1, -1, -1);
    CHECK_MSG(g_hub_cyan == 0, "the hub shows no cyan frame or glow anywhere (%ld cyan pixels over %d looks)", g_hub_cyan, g_hub_nseen);
    w_done(&w);
}

// No system keyboard (applet mode): the on-screen one.
static void sc_builtin_keyboard(void)
{
    W w;
    w_init(&w, "5-builtin-keyboard", 0);
    w.hp.keyboard_available = 0;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    HostPlat *h = &w.hp;
    hp_snap(h, "home-empty");
    hp_btn(h, BTN_A); // New profile
    hp_snap(h, "keyboard-empty");
    hp_keys(h, "Ana");
    hp_snap(h, "keyboard-typed");
    hp_btn(h, BTN_PLUS);
    hp_snap(h, "netid-keyboard");
    hp_btn(h, BTN_B); // no NetID
    hp_btn(h, BTN_B); // no password
    hp_snap(h, "home-after");
    hp_btn(h, BTN_B);
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Profile ana;
    CHECK(find_profile(&st2, "Ana", &ana));
    CHECK(!store_profile_has_pin(&ana));
    w_done(&w);
}

// The last swap was cut short: the app offers to finish it.
static void sc_recovery(void)
{
    W w;
    w_init(&w, "6-recovery", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX], alexid[ID_MAX];
    seed_profile(&w, &st, "Maya", NULL, 600, 3, mayaid);
    seed_profile(&w, &st, "Alex", NULL, 86400, 2, alexid);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    st.state.phase = PH_RESTORING;
    snprintf(st.state.from, sizeof st.state.from, "%s", mayaid);
    snprintf(st.state.to, sizeof st.state.to, "%s", alexid);
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_snap(h, "recovery-offer");
    hp_snap_after(h, 4, "recovering");
    hp_btn(h, BTN_A);
    hp_snap(h, "recovered");
    hp_btn(h, BTN_A);
    hp_snap(h, "home");
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, alexid) && st2.state.phase == PH_IDLE, "recovery finished: Alex is loaded");
    w_done(&w);
}

static void flip_to_light(HostPlat *h) { h->dark_sys = 0; }

// The app follows the console's theme, including a change while it is open.
static void sc_theme_follows(void)
{
    W w;
    w_init(&w, "7-theme-follows-console", 1);
    w.hp.dark_sys = 1;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 7200, 2, NULL);
    seed_profile(&w, &st, "Maya", NULL, 600, 3, mayaid);
    seed_profile(&w, &st, "Priya", "333333", 86400, 1, NULL);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    st.state.loaded_at = (int64_t)w.now0 - 1700;
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_snap(h, "console-is-dark");
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_RIGHT);
    hp_snap(h, "dark-moved");
    hp_call(h, flip_to_light);
    hp_wait(h, 1);
    hp_snap(h, "console-switched-to-light");
    hp_btn(h, BTN_A);
    hp_snap(h, "password-light");
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    int rc = run(&w);
    CHECK(rc == 0);
    w_done(&w);
}

// ---- the switching screen in Washi (the tape roll): one strip per game over a whole swap, with big, empty and many saves
extern long g_prg_frames, g_prg_rect_frames, g_prg_full_frames, g_prg_fx_frames, g_prg_laid, g_prg_hops, g_prg_catches, g_prg_back, g_prg_strips,
    g_prg_zero_games, g_prg_drops;
extern int g_prg_colour[256], g_prg_kind[256];
extern int g_prg_piece_x0[256], g_prg_piece_x1[256], g_prg_npieces;
extern float g_prg_piece_rot[256];
extern float g_prg_last_disp;
extern double (*g_prg_clock_ms)(void);
extern double g_prg_ms_rect, g_prg_ms_full, g_prg_ms_rect_max, g_prg_ms_base, g_prg_ms_bake;
extern long g_prg_n_base, g_prg_n_bake;
extern long g_ui_presses, g_ui_press_changed, g_ui_press_bg_changed;
extern int g_ui_press_audit;
extern void (*g_ui_press_hook)(const uint8_t *rest, int x0, int y0, int w, int h, const Surf *pressed, Rect r, int with_over);
extern int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
long g_press_foreign;                // pixels that changed in a press, outside the item's own rectangle (and a margin for its tape and shadow) by 12 levels or more
long g_press_band;                   // of those, the ones above and below a wide item along its middle: its neighbours' (none should)
static const char *g_press_scenario; // (the scenario the next press belongs to)

// PRESS_DUMP=<dir>: every press as a picture of three panels (the region at rest, as the press shows it, and the difference times 8), and a line of
// numbers: how much changed outside the item's own rectangle, which is where a press that moves more than the item shows itself
static void press_dump(const uint8_t *rest, int x0, int y0, int w, int h, const Surf *pressed, Rect r, int with_over)
{
    static int n;
    const char *dir = getenv("PRESS_DUMP");
    long foreign = 0, band = 0;
    int bx0 = 1 << 30, by0 = 1 << 30, bx1 = -1, by1 = -1;
    uint8_t *img = dir ? malloc((size_t)w * 3 * h * 4) : NULL;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const uint8_t *a = rest + ((size_t)y * w + x) * 4, *b = pressed->px + ((size_t)(y0 + y) * pressed->w + x0 + x) * 4;
            int d = abs(a[0] - b[0]), e = abs(a[1] - b[1]), f = abs(a[2] - b[2]), m = d > e ? (d > f ? d : f) : (e > f ? e : f);
            int ax = x0 + x, ay = y0 + y, inside = ax >= r.x - 3 && ay >= r.y - 3 && ax < r.x + r.w + 3 && ay < r.y + r.h + 3;
            // a wide item (a row) has its tape at its ends: along the middle of its length, the bands above and below it are the neighbouring rows' (and the
            // gap's), and nothing there may move when this one is pushed flat (past the row's own edges and shadow, which go in with it: the shadow of a lifted
            // slip reaches about 30 px below it)
            if (m >= 12 && r.w >= 400 && ax >= r.x + 90 && ax < r.x + r.w - 90 && (ay < r.y - 30 || ay >= r.y + r.h + 40)) band++;
            if (m >= 12 && !inside) {
                foreign++;
                if (ax < bx0) bx0 = ax;
                if (ay < by0) by0 = ay;
                if (ax > bx1) bx1 = ax;
                if (ay > by1) by1 = ay;
            }
            if (img) {
                uint8_t *o = img + ((size_t)y * w * 3 + x) * 4;
                memcpy(o, a, 4);
                memcpy(o + (size_t)w * 4, b, 4);
                int v = m * 8 > 255 ? 255 : m * 8;
                uint8_t *q = o + (size_t)w * 8;
                q[0] = q[1] = q[2] = (uint8_t)v;
                q[3] = 255;
                o[3] = 255;
                o[(size_t)w * 4 + 3] = 255;
            }
        }
    g_press_foreign += foreign;
    g_press_band += band;
    static long bg_seen;
    long bg_now = g_ui_press_bg_changed - bg_seen;
    bg_seen = g_ui_press_bg_changed;
    if (dir) {
        char path[600];
        snprintf(path, sizeof path, "%s/%s-%02d.png", dir, g_press_scenario ? g_press_scenario : "x", ++n);
        stbi_write_png(path, w * 3, h, 4, img, w * 3 * 4);
        printf("  press %s #%d: item (%d,%d %dx%d), over=%d, background moved: %ld px, in the bands: %ld px, changed outside it: %ld px%s", g_press_scenario ? g_press_scenario : "x", n, r.x, r.y, r.w, r.h, with_over, bg_now, band, foreign, foreign ? "" : "\n");
        if (foreign) printf(" in (%d,%d)-(%d,%d)\n", bx0, by0, bx1, by1);
    }
    free(img);
}

static int g_seq_n, g_seq_strip, g_seq_q, g_seq_prev_strip, g_seq_laid;
static float g_seq_disp;
static long g_seq_back;
static int g_seq_mid[256];                   // frames each strip was shown part-laid (the roll between its start and its end)
static double g_seq_in[256], g_seq_out[256]; // when each strip first showed tape, and all of it (host ms; -1: not yet)
extern int g_prg_nocopy[256];
extern float g_prg_pace[256];

static double real_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

// every frame the screen shows: the roll never goes back within a strip; every sixth frame is saved as a picture (the bar filling)
static void seq_hook(HostPlat *h, const Surf *f)
{
    (void)f;
    int si = (int)g_prg_strips - 1;
    if (si >= 0 && si < 256) { // how long each strip was seen being laid
        if (g_prg_last_disp > 0.0f && g_prg_last_disp < 1.0f - 1e-6f) g_seq_mid[si]++;
        if (g_prg_last_disp > 0.0f && g_seq_in[si] < 0) g_seq_in[si] = (double)h->t_ms;
        if (g_prg_last_disp >= 1.0f - 1e-6f && g_seq_in[si] >= 0 && g_seq_out[si] < 0) g_seq_out[si] = (double)h->t_ms;
    }
    if ((int)g_prg_strips == g_seq_strip && g_prg_last_disp + 1e-6f < g_seq_disp) g_seq_back++;
    g_seq_strip = (int)g_prg_strips;
    g_seq_disp = g_prg_last_disp;
    // a picture when the roll first passes a quarter, a half, three quarters and the end of the first five strips
    int q = (int)(g_prg_last_disp * 4.0f + 0.001f);
    if (g_prg_strips <= 5 && q > g_seq_q && q > 0) {
        char nm[64];
        snprintf(nm, sizeof nm, "bar-%d-%d", (int)g_prg_strips, q * 25);
        hp_save_png(h, nm);
    }
    g_seq_q = (int)g_prg_strips == g_seq_prev_strip ? (q > g_seq_q ? q : g_seq_q) : 0;
    g_seq_prev_strip = (int)g_prg_strips;
    if (g_prg_laid != g_seq_laid) { // a picture of the bar with its first few, then more, pieces down
        g_seq_laid = (int)g_prg_laid;
        if (g_seq_laid == 3 || g_seq_laid == 10 || g_seq_laid == 22) {
            char nm[64];
            snprintf(nm, sizeof nm, "bar-laid-%d", g_seq_laid);
            hp_save_png(h, nm);
        }
    }
    g_seq_n++;
}

static void seq_start(HostPlat *h)
{
    h->trace_on = 1;
    h->frame_hook = seq_hook;
    g_prg_clock_ms = real_ms;
    g_seq_n = g_seq_strip = g_seq_q = g_seq_prev_strip = g_seq_laid = 0;
    g_seq_disp = 0;
    g_seq_back = 0;
    g_prg_strips = 0; // (what an earlier swap in this run left would be taken for this one's strips until its first frame)
    g_prg_last_disp = 0;
    for (int i = 0; i < 256; i++) {
        g_seq_mid[i] = 0;
        g_seq_in[i] = g_seq_out[i] = -1.0;
    }
}

static void washi_progress(const char *name, int icons, int many)
{
    W w;
    w_init(&w, name, 1);
    if (!icons) w.fb.icon_data = NULL; // no game has a picture: the plain coloured square with its initials
    fake_put_n(&w.fb, T_MK, "mk.bin", 3 * 1024 * 1024, "mk");
    fake_put_n(&w.fb, T_SSBU, "big2.bin", 10 * 1024 * 1024, "smash2"); // a big save: many callbacks
    fake_put_n(&w.fb, T_SPLAT, "s.bin", 1536 * 1024, "splat");
    fake_put(&w.fb, T_RL, "empty.dat", ""); // a save with nothing in it: no bytes to copy
    if (many)
        for (int i = 0; i < 10; i++) {
            uint64_t id = 0x0100000000100000ull + (uint64_t)i * 0x10000;
            char nm[40];
            snprintf(nm, sizeof nm, "Game number %d", i + 1);
            fake_add_title(&w.fb, id, 1, 0x400000);
            fake_name_title(&w.fb, id, nm);
            fake_put_n(&w.fb, id, "d.bin", 300 * 1024, "g");
        }
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX];
    seed_profile(&w, &st, "Maya", NULL, 600, 3, mayaid);
    seed_profile(&w, &st, "Priya", NULL, 90000, 3, NULL);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    st.state.loaded_at = (int64_t)w.now0 - 900;
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_A);
    hp_call(h, seq_start);
    hp_btn(h, BTN_A); // the confirm dialog: the swap runs
    hp_snap(h, "ready");
    hp_btn(h, BTN_A);
    long presses0 = g_ui_presses, changed0 = g_ui_press_changed, bg0 = g_ui_press_bg_changed;
    int rc = run(&w);
    CHECK(rc == 0);
    CHECK_MSG(g_ui_presses - presses0 >= 2 && g_ui_press_changed - changed0 == g_ui_presses - presses0, "A presses (the tile, the dialog button) pushed the item flat and changed pixels (%ld, %ld)", g_ui_presses - presses0, g_ui_press_changed - changed0);
    CHECK_MSG(g_ui_press_bg_changed == bg0, "and nothing behind them moved: the board and the paper stay as they were (%ld pixels changed)", g_ui_press_bg_changed - bg0);
    int n = (int)g_prg_strips;
    CHECK_MSG(n >= (many ? 14 : 5), "a strip for every game of the swap (%d)", n);
    for (int i = 1; i < n && i < 256; i++) {
        CHECK_MSG(g_prg_colour[i] != g_prg_colour[i - 1], "strip %d has the colour of the one before", i);
        CHECK_MSG(g_prg_kind[i] != g_prg_kind[i - 1], "strip %d has the pattern of the one before", i);
    }
    CHECK_MSG(g_prg_back == 0 && g_seq_back == 0, "the roll went back (%ld, %ld)", g_prg_back, g_seq_back);
    CHECK_MSG(g_prg_laid >= 3, "finished pieces stay on the bar (%ld)", g_prg_laid);
    CHECK_MSG(g_prg_hops >= 2 && g_prg_drops >= 2, "used-up rolls shrink away and the next ones drop in (%ld, %ld)", g_prg_hops, g_prg_drops);
    // the bar of the last step (the plan was the locker's): pieces end to end, none under the minimum, together exactly the bar, each at its own
    // angle (turning the other way from its neighbour), none leaving the box
    int np = g_prg_npieces;
    CHECK_MSG(np >= (many ? 8 : 3), "the last step's bar has its pieces (%d)", np);
    CHECK_MSG(g_prg_piece_x0[0] == 445, "the bar starts at its left end (%d)", g_prg_piece_x0[0]);
    CHECK_MSG(np > 0 && g_prg_piece_x1[np - 1] == 445 + 518, "the bar ends exactly at its right end (%d)", np > 0 ? g_prg_piece_x1[np - 1] : -1);
    for (int i = 0; i < np; i++) {
        CHECK_MSG(g_prg_piece_x1[i] - g_prg_piece_x0[i] >= (np * 26 > 466 ? 466 / np : 26) - 1, "piece %d keeps its minimum width (%d)", i, g_prg_piece_x1[i] - g_prg_piece_x0[i]);
        CHECK_MSG(g_prg_piece_rot[i] > -2.5f && g_prg_piece_rot[i] < 2.5f && fabsf(g_prg_piece_rot[i]) > 0.3f, "piece %d is a little crooked (%.2f)", i, (double)g_prg_piece_rot[i]);
        if (i) {
            CHECK_MSG(g_prg_piece_x0[i] == g_prg_piece_x1[i - 1], "piece %d starts where the one before ended (%d, %d)", i, g_prg_piece_x0[i], g_prg_piece_x1[i - 1]);
            CHECK_MSG(g_prg_piece_rot[i] * g_prg_piece_rot[i - 1] < 0, "piece %d turns the other way from the one before", i);
        }
    }
    CHECK_MSG(g_prg_zero_games >= 1, "a game with nothing to copy was shown (%ld)", g_prg_zero_games);
    // a small save is seen being laid, however quickly it was copied: the roll never goes faster than its limit
    for (int i = 0; i < n && i < 256; i++) {
        if (g_prg_nocopy[i]) continue;
        double took = g_seq_out[i] - g_seq_in[i];
        if (getenv("PRG_REPORT")) printf("    strip %d: pace %.0f ms, %d frames part-way, tape from %.0f to %.0f ms\n", i, (double)g_prg_pace[i], g_seq_mid[i], g_seq_in[i], g_seq_out[i]);
        CHECK_MSG(g_seq_mid[i] >= 4, "strip %d is seen being laid, not just there (%d frames part-way)", i, g_seq_mid[i]);
        CHECK_MSG(g_seq_in[i] >= 0 && g_seq_out[i] >= 0 && took >= 0.75 * (double)g_prg_pace[i], "strip %d takes about its pace to lay (%.0f ms; its pace %.0f ms)", i, took,
                  (double)g_prg_pace[i]);
    }
    // and the swap's sounds are quiet (the first set was "a little too punchy": a tear at .9, taps up to .65)
    CHECK_MSG(h->sounds[SND_TEAR] > 0 && h->max_gain[SND_TEAR] <= 0.55f, "the tear of a cut piece is soft (%.2f)", (double)h->max_gain[SND_TEAR]);
    CHECK_MSG(h->max_gain[SND_CRINKLE] <= 0.45f && h->max_gain[SND_SLIP0] <= 0.45f && h->max_gain[SND_SLIP1] <= 0.45f && h->max_gain[SND_SLIP2] <= 0.45f &&
                  h->max_gain[SND_SLIP3] <= 0.45f,
              "so are the taps of the turning roll and the catches (%.2f %.2f %.2f %.2f %.2f)", (double)h->max_gain[SND_CRINKLE], (double)h->max_gain[SND_SLIP0],
              (double)h->max_gain[SND_SLIP1], (double)h->max_gain[SND_SLIP2], (double)h->max_gain[SND_SLIP3]);
    CHECK_MSG(g_prg_catches >= 1, "the roll caught somewhere (%ld)", g_prg_catches);
    CHECK_MSG(g_prg_rect_frames > g_prg_full_frames, "most frames are rectangles (%ld rect, %ld full)", g_prg_rect_frames, g_prg_full_frames);
    if (getenv("PRG_REPORT"))
        printf("  %s: %ld frames (%ld rect, %ld full, %ld effect), %d strips, %ld catches; rect frame %.3f ms mean %.3f max, full paint %.2f ms\n", name,
               g_prg_frames, g_prg_rect_frames, g_prg_full_frames, g_prg_fx_frames, n, g_prg_catches,
               g_prg_rect_frames ? g_prg_ms_rect / (double)g_prg_rect_frames : 0.0, g_prg_ms_rect_max,
               g_prg_full_frames ? g_prg_ms_full / (double)g_prg_full_frames : 0.0);
    if (getenv("PRG_REPORT"))
        printf("    base drawn %ld times, %.2f ms each; a game's strip, roll and icon baked %ld times, %.2f ms each\n", g_prg_n_base,
               g_prg_n_base ? g_prg_ms_base / (double)g_prg_n_base : 0.0, g_prg_n_bake, g_prg_n_bake ? g_prg_ms_bake / (double)g_prg_n_bake : 0.0);
    g_prg_clock_ms = NULL;
    w_done(&w);
}

static void sc_washi_progress_noicon(void) { washi_progress("8c-washi-progress-noicon", 0, 0); }
static void sc_washi_progress_many(void) { washi_progress("8d-washi-progress-many", 1, 1); }

static W *g_w;

static void arm_power_cut(HostPlat *h)
{
    (void)h;
    fake_arm(&g_w->fb, 38); // lose power part-way through loading the new profile
}

static void power_back(HostPlat *h)
{
    (void)h;
    fake_reboot(&g_w->fb);
}

// The power is cut in the middle of a swap: the app says so, offers to finish, and finishes.
static void sc_interrupted(void)
{
    W w;
    w_init(&w, "9-interrupted-swap", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX], priyaid[ID_MAX];
    seed_profile(&w, &st, "Maya", NULL, 600, 3, mayaid);
    seed_profile(&w, &st, "Priya", NULL, 86400, 3, priyaid);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_A); // choose Priya
    hp_call(h, arm_power_cut);
    hp_btn(h, BTN_A); // switch (and the power goes)
    hp_snap(h, "it-stopped");
    hp_btn(h, BTN_A);
    hp_snap(h, "offer-to-finish");
    hp_call(h, power_back);
    hp_btn(h, BTN_A);
    hp_snap(h, "finished");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, priyaid) && st2.state.phase == PH_IDLE, "Priya is loaded after recovery (holder %s, phase %d)", st2.state.holder, st2.state.phase);
    w_done(&w);
    g_w = NULL;
}

static void low_battery(HostPlat *h)
{
    (void)h;
    g_w->fb.battery_pct = 9;
    g_w->fb.charging = 0;
}

static void game_running(HostPlat *h)
{
    (void)h;
    g_w->fb.battery_pct = 82;
    fake_title(&g_w->fb, T_SSBU)->running = 1;
}

// Refusals: nothing is changed, and the message says what to do.
static void sc_refusals(void)
{
    W w;
    w_init(&w, "10-refusals", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX];
    seed_profile(&w, &st, "Maya", NULL, 600, 3, mayaid);
    seed_profile(&w, &st, "Priya", NULL, 86400, 3, NULL);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_call(h, low_battery);
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_A); // Priya
    hp_btn(h, BTN_A); // switch
    hp_snap(h, "low-battery");
    hp_btn(h, BTN_A);
    hp_call(h, game_running);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_snap(h, "game-running");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, mayaid) && st2.state.phase == PH_IDLE, "both refusals changed nothing");
    w_done(&w);
    g_w = NULL;
}

// The cursor's motion on the grid: one press hops to the next tile. The frame where it was fades out while a new one fades in on the
// next tile; it is never drawn between the two, and the whole change takes a moment, not a blink.
static void glide_start(HostPlat *h)
{
    h->trace_x[0] = hp_focus_left(h); // the frame at rest, as the press finds it
    h->trace_t[0] = h->t_ms;
    h->ntrace = 1;
    h->trace_on = 1;
    h->trace_png = getenv("TRACE_PNG") != NULL; // TRACE_PNG=1 ./app_host gridscroll  keeps every frame of the move
}

// Dialogs come in and go out, screens fade in: each change is a handful of frames between the old picture and the new one, never a cut.
static void trans_start(HostPlat *h)
{
    h->ntrace = 0;
    h->trace_on = 1;
    h->trace_png = getenv("TRACE_PNG") != NULL; // TRACE_PNG=1 ./app_host storage  keeps every frame of the changes
}

static int trans_frames(HostPlat *h, const char *what)
{
    h->trace_on = 0;
    int n = h->ntrace;
    int ms = n ? (int)(h->trace_t[n - 1] - h->trace_t[0]) : 0;
    printf("  %s: %d frames, %d ms\n", what, n, ms);
    return n;
}

// The order of the grid: most recently played first by default; the chip offers others, and the choice is kept.
static char g_zoe_id[ID_MAX], g_adam_id[ID_MAX];

static void check_holder(const char *want, const char *who)
{
    Store s2;
    store_open(&s2, &g_w->fb.be, g_w->fb.sd);
    CHECK_MSG(!strcmp(s2.state.holder, want), "the first tile (%s) is the one that got loaded (holder %s)", who, s2.state.holder);
}

static void check_holder_zoe(HostPlat *h)
{
    (void)h;
    check_holder(g_zoe_id, "the latest player");
}

static void check_holder_adam(HostPlat *h)
{
    (void)h;
    check_holder(g_adam_id, "first by name");
}

static void check_holder_nobody(HostPlat *h)
{
    (void)h;
    check_holder(UNCLAIMED, "nobody");
}

// SORT_ANIM=12 ./app_host sort-anim: every frame of Home's order menu (the sleeve, ui_stickfx.c) coming out, the focus moving and an order
// picked, for looking at. SORT_NOTRACE=1: no frames kept (to time it with BENCH=1)
static void sa_on(HostPlat *h)
{
    if (getenv("SORT_NOTRACE")) return;
    h->ntrace = 0;
    h->trace_on = 1;
    h->trace_png = 1;
}
static void sa_off(HostPlat *h)
{
    if (h->trace_png) { // (when each frame was shown, for films that keep the pauses between presses: <outdir>/<scenario>/times.txt)
        char p[500];
        snprintf(p, sizeof p, "%s/%s/times.txt", h->outdir, h->scenario);
        FILE *f = fopen(p, "w");
        if (f) {
            for (int i = 0; i < h->ntrace; i++) fprintf(f, "%llu\n", (unsigned long long)h->trace_t[i]);
            fclose(f);
        }
    }
    h->trace_on = h->trace_png = 0;
}

static void sc_sort_anim(void)
{
    if (!getenv("SORT_ANIM") || atoi(getenv("SORT_ANIM")) != 12) return;
    W w;
    w_init(&w, "17b-sort-anim", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    st.cfg.sort_mode = SORT_RECENT;
    store_save_config(&st);
    seed_profile(&w, &st, "Zoe", NULL, 600, 1, NULL);
    seed_profile(&w, &st, "Mia", NULL, 7200, 2, NULL);
    seed_profile(&w, &st, "Adam", NULL, 90000, 3, NULL);
    seed_profile(&w, &st, "Beth", NULL, -1, 0, NULL);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_UP); // the frame up to the search bar's row
    hp_btn(h, BTN_RIGHT); // the order chip
    hp_wait(h, 1);
    hp_call(h, sa_on);
    // the sleeve: the next one out, the one after, back up one, and that one stuck on
    hp_btn(h, BTN_R3); // opened and put away with B
    hp_wait(h, 1);
    hp_btn(h, BTN_B);
    hp_wait(h, 1);
    hp_btn(h, BTN_R3);
    hp_wait(h, 1);
    hp_btn(h, BTN_DOWN);
    hp_wait(h, 1);
    hp_btn(h, BTN_DOWN);
    hp_wait(h, 1);
    hp_btn(h, BTN_UP);
    hp_wait(h, 1);
    hp_btn(h, BTN_A);
    hp_wait(h, 1);
    hp_btn(h, BTN_DOWN); // the focus off the chip (to the grid): a sticker picked now lands on the chip as it sits
    hp_wait(h, 1);
    hp_btn(h, BTN_R3);
    hp_wait(h, 1);
    hp_btn(h, BTN_A);
    hp_wait(h, 1);
    hp_call(h, sa_off);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

// ./app_host sticker-art: the order menu's stickers (ui_stickfx.c) on chips as the pages draw them, and close up (twice the size),
// for looking at: out/app/sticker-art<-dark>.png
#include "../source/ui_stickfx.h"
static void art_draw(HostPlat *h)
{
    (void)h;
    static const char *const L[7] = {"Recently played", "Name, A to Z", "Newest first", "Most games", "Recently downloaded", "Most icons", "On your card first"};
    Surf *s = surf_new(1240, 760), *t = surf_new(262, 50);
    if (!s || !t) {
        surf_free(s);
        surf_free(t);
        return;
    }
    paper_board_fill(s, 3u);
    for (int i = 0; i < 7; i++) ui_order_chip(s, 24, 30 + i * 100, 300, L[i], i);
    for (int i = 0; i < 7; i++) {
        for (int y = 0; y < t->h; y++) memcpy(t->px + (size_t)y * t->w * 4, s->px + ((size_t)(30 + i * 100 + y) * s->w + 340) * 4, (size_t)t->w * 4);
        Cx c = ui_cx(t);
        stk_sticker(&c, 10, 7, 240, 36, L[i], i);
        for (int y = 0; y < 100; y++)
            for (int x = 0; x < 524; x++) memcpy(s->px + ((size_t)(10 + i * 100 + y) * s->w + 360 + x) * 4, t->px + ((size_t)(y / 2) * t->w + x / 2) * 4, 4);
    }
    {
        Surf *u = surf_new(300, 60);
        if (u) {
            for (int k = 0; k < 2; k++) { // the back of one, and the place one was peeled from
                for (int y = 0; y < u->h; y++) memcpy(u->px + (size_t)y * u->w * 4, s->px + ((size_t)(30 + k * 70 + y) * s->w + 920) * 4, (size_t)u->w * 4);
                Cx c = ui_cx(u);
                if (k == 0) stk_back(&c, 20, 12, 240, 36, L[0]);
                else stk_place(&c, 20, 12, 240, 36, L[2]);
                for (int y = 0; y < u->h; y++) memcpy(s->px + ((size_t)(30 + k * 70 + y) * s->w + 920) * 4, u->px + (size_t)y * u->w * 4, (size_t)u->w * 4);
            }
            surf_free(u);
        }
    }
    char p[400];
    snprintf(p, sizeof p, "out/app/sticker-art%s.png", getenv("DARK") ? "-dark" : "");
    stbi_write_png(p, s->w, s->h, 4, s->px, s->w * 4);
    surf_free(s);
    surf_free(t);
}

// ./app_host busy-dots: a waiting page whose note ends in an ellipsis keeps its dots moving (one, two, three, none) while the work goes on
static void busy_draw(HostPlat *h)
{
    Surf *home_work = surf_clone(G.work), *home_base = surf_clone(G.base); // (Home, to be put back: the pages here are drawn over it behind its back)
    ui_busy("Extra icons", "Asking GitHub which games have icons\xE2\x80\xA6");
    for (int k = 0; k < 5; k++) {
        h->t_ms += 400; // (the work goes on; the dots get their moment)
        ui_busy_tick();
        char nm[32];
        snprintf(nm, sizeof nm, "dots-%d", k);
        hp_save_png(h, nm);
    }
    // and, as the console does it, the crayon starts drawing on the footer while the question is out (fetch_repo's wait: dots, then a pump, every 15 ms)
    int keep_after = g_ui_wait_after_ms;
    g_ui_wait_after_ms = 350;
    long shown0 = g_ui_wait_shown, frames0 = g_ui_wait_frames;
    ui_busy("Extra icons", "Asking GitHub which games have icons\xE2\x80\xA6");
    for (int k = 0; k < 140; k++) {
        h->t_ms += 15;
        ui_busy_tick();
        gfx_pump();
    }
    CHECK(g_ui_wait_shown == shown0 + 1);
    CHECK_MSG(ui_wait_shown() && g_ui_wait_frames - frames0 > 40, "the dots moving don't stop the crayon (%ld frames)", g_ui_wait_frames - frames0);
    Rect a = ui_wait_area();
    int ink = 0, off = 0;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            int in = x >= a.x && x < a.x + a.w && y >= a.y && y < a.y + a.h;
            int d = memcmp(h->last->px + ((size_t)y * SCREEN_W + x) * 4, G.work->px + ((size_t)y * SCREEN_W + x) * 4, 3) != 0;
            if (in) ink += d;
            else off += d;
        }
    CHECK_MSG(ink > 200 && off == 0, "the drawing is on the footer and everything else (the dots too) is the page as it is now (%d inked, %d elsewhere)", ink, off);
    hp_save_png(h, "crayon");
    // the answer is in: the next screen comes (here a whole frame of the page, as a dialog would), and the crayon is rubbed out over it
    ui_wait_finish();
    ui_present(G.work);
    for (int i = 0; i < 6000 && ui_wait_erasing(); i++) ui_wait_tick();
    CHECK_MSG(!ui_wait_erasing() && !memcmp(h->last->px, G.work->px, (size_t)SCREEN_W * SCREEN_H * 4), "and is gone once rubbed out");
    g_ui_wait_after_ms = keep_after;
    surf_copy(G.work, home_work); // Home back on the screen, whole, as it was: its next frames are drawn as changes to it
    surf_copy(G.base, home_base);
    ui_present(G.work);
    surf_free(home_work);
    surf_free(home_base);
}

static void sc_busy_dots(void)
{
    W w;
    w_init(&w, "17e-busy-dots", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Zoe", NULL, 600, 1, NULL);
    hp_call(&w.hp, busy_draw);
    hp_btn(&w.hp, BTN_DOWN);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

static void sc_sticker_art(void)
{
    W w;
    w_init(&w, "17c-sticker-art", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Zoe", NULL, 600, 1, NULL);
    HostPlat *h = &w.hp;
    hp_call(h, art_draw);
    hp_btn(h, BTN_DOWN);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

// ./app_host shuffle: a new order shuffles the grid (TRACE_PNG=1 keeps its frames): the tiles still on the screen are carried to their places, the
// ones that come onto it rise in, the ones that go sink away; nothing is left where it was when it has settled (the whole-frame check sees to that).
extern int g_ui_home_shuf_n;
static void shuf_zero(HostPlat *h) { (void)h; g_ui_home_shuf_n = 0; }
static void shuf_moved(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_home_shuf_n >= 10, "a new order shuffles the tiles (%d of them)", g_ui_home_shuf_n);
}
static void sc_shuffle(void)
{
    W w;
    w_init(&w, "17d-shuffle", 1);
    g_w = &w;
    static const char *names[] = {"Ines", "Ben", "Zoe", "Alex", "Omar", "Hana", "Gus", "Uma", "Kai", "Dani", "Theo", "Lena", "Ravi", "Maya", "Sam", "Wren"};
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    st.cfg.sort_mode = SORT_RECENT;
    store_save_config(&st);
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) seed_profile(&w, &st, names[i], NULL, (int)(i * 3600 + 60), (int)(i % 4) + 1, NULL);
    HostPlat *h = &w.hp;
    hp_snap(h, "recent");
    hp_call(h, shuf_zero);
    if (getenv("TRACE_PNG")) hp_call(h, sa_on);
    hp_btn(h, BTN_R3); // the sleeve; Name, A to Z is the first out of it
    hp_btn(h, BTN_A);
    hp_wait(h, 1);
    if (getenv("TRACE_PNG")) hp_call(h, sa_off);
    hp_call(h, shuf_moved);
    hp_snap(h, "by-name");
    hp_call(h, shuf_zero); // and back to Recently played: the rail slides away as they shuffle back
    if (getenv("TRACE_PNG")) hp_call(h, sa_on);
    hp_btn(h, BTN_R3);
    hp_btn(h, BTN_A);
    hp_wait(h, 1);
    if (getenv("TRACE_PNG")) hp_call(h, sa_off);
    hp_call(h, shuf_moved);
    hp_snap(h, "recent-again");
    int rc = run(&w);
    CHECK(rc == 0);
    w_done(&w);
    g_w = NULL;
}

// A load does not move the player to the front of "Recently played": the order is worked out when the app starts, so the stamp lands on the tile that
// was picked (the next start puts the player first).
static void sc_order_kept(void)
{
    W w;
    w_init(&w, "17b-order-kept", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    st.cfg.sort_mode = SORT_RECENT;
    store_save_config(&st);
    seed_profile(&w, &st, "Zoe", NULL, 600, 1, g_zoe_id);
    seed_profile(&w, &st, "Mia", NULL, 7200, 2, NULL);
    seed_profile(&w, &st, "Adam", NULL, 90000, 3, g_adam_id);
    HostPlat *h = &w.hp;
    hp_snap(h, "before");
    hp_btn_n(h, BTN_RIGHT, 2); // Adam, the third tile (played the longest ago)
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // Switch
    hp_btn(h, BTN_A); // ready
    hp_call(h, check_holder_adam);
    hp_snap(h, "adam-stays-third");
    hp_btn_n(h, BTN_LEFT, 2); // back to the first tile: still Zoe (Adam would be there if the order had moved)
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_call(h, check_holder_zoe);
    int rc = run(&w);
    CHECK(rc == 0);
    w_done(&w);
    g_w = NULL;
}

// What a player can change about themselves, and that a NetID belongs to one profile only.
static void sc_edit_profile(void)
{
    W w;
    w_init(&w, "18-edit-profile", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char alexid[ID_MAX], benid[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 600, 1, alexid);
    seed_profile(&w, &st, "Ben", NULL, 7200, 1, benid);
    Profile ben;
    store_profile_load(&st, benid, &ben);
    snprintf(ben.netid, sizeof ben.netid, "ben2222");
    store_profile_save(&st, &ben);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_PLUS); // nobody is loaded: Plus edits the profile that is selected (Alex), and nobody gets loaded
    hp_snap(h, "nobody-loaded");
    hp_btn(h, BTN_B);
    hp_call(h, check_holder_nobody);
    hp_btn(h, BTN_A); // load Alex
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_PLUS);
    hp_snap(h, "edit-profile");
    hp_btn(h, BTN_DOWN);
    hp_text(h, "Alexander");
    hp_btn(h, BTN_A); // Name
    hp_snap(h, "renamed");
    hp_btn(h, BTN_DOWN);
    hp_text(h, "BEN2222");
    hp_btn(h, BTN_A); // NetID: Ben already has it
    hp_snap(h, "netid-taken");
    hp_btn(h, BTN_A); // OK
    hp_text(h, "alx9999");
    hp_btn(h, BTN_A); // NetID again, a free one
    hp_snap(h, "netid-set");
    hp_btn(h, BTN_B);
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Profile alex, ben2;
    CHECK(find_profile(&st2, "Alexander", &alex));
    CHECK_MSG(!strcmp(alex.netid, "alx9999"), "the player set their own NetID (%s)", alex.netid);
    CHECK(find_profile(&st2, "Ben", &ben2) && !strcmp(ben2.netid, "ben2222"));
    w_done(&w);
    g_w = NULL;
}

// The Activity log: one file for every start in <washi>/logs; a start deletes the ones older than two weeks, and the page lists the rest by when they started.
extern int g_app_logs_listed;
static char g_lg_old[64], g_lg_gone[64];

static void lg_name(time_t t, char *out, size_t n)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(out, n, "%Y-%m-%d_%H-%M-%S.txt", &tm);
}

static int lg_has(Store *st, const char *name)
{
    char rel[FSX_PATH], p[FSX_PATH];
    snprintf(rel, sizeof rel, "logs/%s", name);
    store_path(st, p, sizeof p, rel);
    return fsx_exists(p);
}

static void lg_put(Store *st, const char *name, const char *text)
{
    char rel[FSX_PATH], p[FSX_PATH];
    store_path(st, p, sizeof p, "logs");
    fsx_mkdir_p(p);
    snprintf(rel, sizeof rel, "logs/%s", name);
    store_path(st, p, sizeof p, rel);
    fsx_write_file(p, text, strlen(text));
}

static void lg_check_list(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_app_logs_listed == 3, "the list has this start, yesterday's and the one from three days ago (%d)", g_app_logs_listed);
}

static void sc_logs(void)
{
    W w;
    w_init(&w, "18c-logs", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 1, NULL);
    char yest[64];
    lg_name(w.now0 - 3 * 86400, g_lg_old, sizeof g_lg_old);
    lg_name(w.now0 - 86400 + 3600, yest, sizeof yest);
    lg_name(w.now0 - 15 * 86400, g_lg_gone, sizeof g_lg_gone);
    lg_put(&st, g_lg_old, "2026-09-29 18:02:11 Maya loaded\n2026-09-29 19:40:05 Maya signed out\n");
    lg_put(&st, yest, "2026-10-01 15:14:00 started\n");
    lg_put(&st, g_lg_gone, "2026-09-17 10:00:00 too old\n");
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    g_app_logs_listed = -1;
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_open(h, &hub, HUB_LOG);
    hp_snap(h, "list");
    hp_call(h, lg_check_list);
    hp_btn_n(h, BTN_DOWN, 2); // the oldest
    hp_btn(h, BTN_A);
    hp_snap(h, "older-log");
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    CHECK(run(&w) == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!lg_has(&st2, g_lg_gone), "the log from fifteen days ago was deleted when Washi started");
    CHECK_MSG(lg_has(&st2, g_lg_old) && lg_has(&st2, yest), "the newer ones stay");
    w_done(&w);
    g_w = NULL;
}

// Plus on Home with nobody loaded edits the profile that is selected: its password first, and nobody becomes loaded.
static void sc_edit_from_home(void)
{
    W w;
    w_init(&w, "18b-edit-from-home", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Alex", "135790", 600, 1, NULL);
    seed_profile(&w, &st, "Ben", NULL, 7200, 1, NULL);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_PLUS); // Alex (selected) has a password
    hp_snap(h, "asks-password");
    hp_pin(h, "000000"); // wrong
    hp_snap(h, "wrong");
    hp_btn(h, BTN_B); // cancel: back on Home, nothing edited
    hp_btn(h, BTN_PLUS);
    hp_pin(h, "135790");
    hp_snap(h, "editing-alex");
    hp_btn(h, BTN_DOWN);
    hp_text(h, "Alexander");
    hp_btn(h, BTN_A); // Name
    hp_btn(h, BTN_B); // leave the editor
    hp_call(h, check_holder_nobody);
    hp_btn(h, BTN_RIGHT); // Ben has none
    hp_btn(h, BTN_PLUS);
    hp_snap(h, "editing-ben");
    hp_btn(h, BTN_B);
    hp_call(h, check_holder_nobody);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_A);
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Profile alex;
    CHECK_MSG(find_profile(&st2, "Alexander", &alex), "the edit was saved");
    CHECK(!strcmp(st2.state.holder, UNCLAIMED));
    w_done(&w);
    g_w = NULL;
}

// Opened hours after someone loaded their saves: "is this still you?"
static void reminder_world(W *w, const char *scenario, int hours)
{
    w_init(w, scenario, 1);
    g_w = w;
    Store st;
    store_open(&st, &w->fb.be, w->fb.sd);
    seed_config(w, &st, 1, 1);
    char mayaid[ID_MAX];
    seed_profile(w, &st, "Maya", NULL, 600, 2, mayaid);
    seed_profile(w, &st, "Priya", NULL, 9000, 1, NULL);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    st.state.loaded_at = (int64_t)w->now0 - hours * 3600;
    store_save_state(&st);
}

static void sc_reminder_signout(void)
{
    W w;
    reminder_world(&w, "19-reminder-signout", 5);
    HostPlat *h = &w.hp;
    hp_snap(h, "still-you");
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // Sign out
    hp_snap(h, "signed-out");
    hp_btn(h, BTN_B);
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, UNCLAIMED) && st2.state.wiped == 1, "the forgotten player was signed out");
    w_done(&w);
    g_w = NULL;
}

// a fresh script for another run of the app on the same console and SD card (the screenshots keep counting)
static void rerun_script(W *w)
{
    int snaps = w->hp.snaps;
    hp_free(&w->hp);
    hp_init(&w->hp, OUT, g_scenario);
    w->hp.snaps = snaps;
}

static void set_loaded_ago(W *w, int seconds)
{
    Store st;
    store_open(&st, &w->fb.be, w->fb.sd);
    st.state.loaded_at = (int64_t)w->now0 - seconds;
    store_save_state(&st);
}

static int64_t loaded_ago(W *w)
{
    Store st;
    store_open(&st, &w->fb.be, w->fb.sd);
    return (int64_t)w->now0 - st.state.loaded_at;
}

// what the reminder is set to after each step of the wheels (in minutes)
// (the flick: the finger moves the hours wheel up a value and a half, quickly; it coasts on, and the next value catches it and
// snaps it in at 6; the slow drag moves the minutes wheel by less than a value and it snaps to the nearest, without coasting)
static const int REMIND_WANT[] = {90, 0, 720, 0, 210, 390, 405, 405, 15};
static int g_remind_i;

static void remind_check(HostPlat *h)
{
    (void)h;
    Store st;
    store_open(&st, &g_w->fb.be, g_w->fb.sd);
    int i = g_remind_i++, want = i < (int)(sizeof REMIND_WANT / sizeof REMIND_WANT[0]) ? REMIND_WANT[i] : -1;
    CHECK_MSG(st.cfg.remind_minutes == want, "wheels step %d: the reminder is %d min (wanted %d)", i, st.cfg.remind_minutes, want);
}

// The reminder's setting (Admin, Console, Sign-out reminder): an hour instead of three, and what that changes on opening the app.
static void sc_reminder_setting(void)
{
    W w;
    reminder_world(&w, "22-reminder-setting", 0);
    set_loaded_ago(&w, 90 * 60); // Maya loaded her saves an hour and a half ago: under the three hours it starts with
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_section(h, &hub, HS_CONSOLE);
    hp_snap(h, "console-section");
    hub_open(h, &hub, HUB_REMINDER);
    hp_snap(h, "wheels");
    hp_btn_n(h, BTN_UP, 2); // the hours wheel from 3 back to 1
    hp_snap(h, "one-hour");
    hp_btn(h, BTN_A);
    hp_snap(h, "set-to-1-hour");
    hub_leave(h, &hub);
    CHECK(run(&w) == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(st2.cfg.remind_minutes == 60, "the reminder is set to an hour (%d min)", st2.cfg.remind_minutes);
    CHECK_MSG(loaded_ago(&w) == 90 * 60, "with three hours, nothing was asked after an hour and a half (%lld s)", (long long)loaded_ago(&w));

    rerun_script(&w); // opened again: now an hour and a half is long enough to ask
    hp_snap(h, "asks-after-an-hour");
    hp_btn(h, BTN_A); // yes, still me
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    CHECK(run(&w) == 0);
    CHECK_MSG(loaded_ago(&w) <= 5, "it asked, and \"still me\" started the clock again (%lld s)", (long long)loaded_ago(&w));

    set_loaded_ago(&w, 30 * 60); // half an hour is not long enough
    rerun_script(&w);
    hp_btn(h, BTN_A); // (had it asked, this would answer "still me"; as it is, it is Maya's tile, and she is loaded already)
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    CHECK(run(&w) == 0);
    CHECK_MSG(loaded_ago(&w) == 30 * 60, "after half an hour nothing is asked (%lld s)", (long long)loaded_ago(&w));

    // the wheels: quarter hours, round past 12 back to 0, 0:00 and "Turn off" both mean off, and by touch (tap a value, drag)
    rerun_script(&w);
    g_remind_i = 0;
    HubPos hub2 = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_open(h, &hub2, HUB_REMINDER); // at 1:00
    hp_btn(h, BTN_RIGHT);
    hp_btn_n(h, BTN_DOWN, 2); // 00, 15, 30
    hp_snap(h, "one-thirty");
    hp_btn(h, BTN_A);
    hp_call(h, remind_check); // 1:30
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_Y);
    hp_call(h, remind_check); // turned off
    hp_snap(h, "off");
    hp_btn(h, BTN_A);
    hp_snap(h, "off-opens-at-three");
    hp_btn_n(h, BTN_UP, 4); // 3, 2, 1, 0, and round to 12
    hp_snap(h, "round-to-twelve");
    hp_btn(h, BTN_A);
    hp_call(h, remind_check); // 12:00
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_DOWN); // 12 round to 0, with 00 minutes
    hp_btn(h, BTN_A);
    hp_call(h, remind_check); // 0:00 is off
    hp_btn(h, BTN_A);
    hp_tap(h, 776, 490); // the value two under the middle of the minutes wheel (00, 15, 30): 3:30
    hp_snap(h, "tapped-thirty");
    hp_tap(h, 850, 584); // OK
    hp_call(h, remind_check); // 3:30
    hp_btn(h, BTN_A);
    hp_drag(h, 490, 434, 490, 342, 8); // a flick up on the hours wheel: it spins on and snaps in at 6
    hp_snap(h, "flicked-to-six");
    hp_btn(h, BTN_A);
    hp_call(h, remind_check); // 6:30
    hp_btn(h, BTN_A);
    hp_drag(h, 780, 380, 780, 340, 24); // a slow push on the minutes wheel, under a value's height: it settles on the next, 45
    hp_snap(h, "nudged-to-forty-five");
    hp_btn(h, BTN_A);
    hp_call(h, remind_check); // 6:45
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_B); // cancelled: still 6:45
    hp_call(h, remind_check);
    hp_btn(h, BTN_A);
    hp_btn_n(h, BTN_UP, 6); // the hours wheel from 6 back to 0
    hp_btn(h, BTN_RIGHT);
    hp_btn_n(h, BTN_UP, 2); // the minutes wheel from 45 up past 30 to 15 (no 01 any more: the one-minute test is gone)
    hp_snap(h, "quarter-hour");
    hp_btn(h, BTN_A);
    hp_call(h, remind_check); // 0:15
    hp_btn(h, BTN_A);
    hp_snap(h, "quarter-hour-again");
    hp_btn(h, BTN_B);
    hub_leave(h, &hub2);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_remind_i == 9, "every step of the wheels was checked (%d of 9)", g_remind_i);
    w_done(&w);
    g_w = NULL;
}

// The D-pad above the grid: up from the first row to the search bar (or the order chip, beside it on the right), then the loaded
// profile's card (or its Sign out button, above the chip); A on each does what a tap does. Which one has the focus is read off the
// screen: it is the one that is lifted, so paper (or the button's pale green) shows just above where its top edge rests, on the board.
static int g_zone_seen[24], g_zone_n;

static void zone_probe(HostPlat *h)
{
    const Surf *f = h->last;
    int z = -1; // (the chip is at least 176 wide: its right end is what is probed)
    if (paper_at(f, 700, 97, 249)) z = 3;                             // the loaded profile's card
    else if (px_near(f, 1000, 123, g_pal.sign_fill, 12)) z = 4;       // Sign out, which has risen off the card
    else if (paper_at(f, 340, 233, 249)) z = 2;                       // the search bar
    else if (paper_at(f, 1220, 232, 249)) z = 1;                      // the chip (its top margin: lower, the probe meets its sticker's border by night)
    else
        for (int col = 0; col < 7 && z < 0; col++) // a tile of the first row
            if (paper_at(f, 40 + col * 164 + 10, 309, 249)) z = 100 + col;
    if (g_zone_n < 24) g_zone_seen[g_zone_n++] = z;
}

static void sc_home_focus(void)
{
    W w;
    reminder_world(&w, "25-home-focus", 0); // Maya's saves are loaded (just now); Priya is the other profile
    HostPlat *h = &w.hp;
    g_zone_n = 0;
    hp_snap(h, "grid");
    hp_call(h, zone_probe); // the first tile, Maya
    hp_btn(h, BTN_UP);
    hp_snap(h, "search-bar");
    hp_call(h, zone_probe);
    hp_btn(h, BTN_RIGHT);
    hp_snap(h, "chip");
    hp_call(h, zone_probe);
    hp_btn(h, BTN_UP); // above the chip: Sign out
    hp_snap(h, "sign-out-button");
    hp_call(h, zone_probe);
    hp_btn(h, BTN_UP); // nothing above it
    hp_call(h, zone_probe);
    hp_btn(h, BTN_LEFT);
    hp_snap(h, "card");
    hp_call(h, zone_probe);
    hp_btn(h, BTN_DOWN); // the search bar under it
    hp_call(h, zone_probe);
    hp_btn(h, BTN_DOWN); // and back on Maya's tile
    hp_call(h, zone_probe);

    hp_btn(h, BTN_UP);
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_A); // the chip: how to order the grid
    hp_snap(h, "chip-opens-order");
    hp_btn(h, BTN_B);
    hp_call(h, zone_probe); // still on the chip
    hp_btn(h, BTN_LEFT);
    hp_text(h, "pri"); // (what the keyboard will be given)
    hp_btn(h, BTN_A); // the search bar
    hp_snap(h, "found-priya");
    hp_call(h, zone_probe); // the frame went to who was found
    hp_btn_n(h, BTN_UP, 2); // the search bar, then the card
    hp_btn(h, BTN_A); // the card: Maya's profile
    hp_snap(h, "card-opens-profile");
    hp_btn(h, BTN_B);
    hp_call(h, zone_probe);
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_A); // Sign out
    hp_snap(h, "signed-out");
    hp_call(h, zone_probe); // the button is gone: the card
    hp_btn(h, BTN_A); // the guest's card says what being a guest means
    hp_snap(h, "guest-note");
    hp_btn(h, BTN_B); // clears the search
    hp_btn(h, BTN_B); // and leaves
    CHECK(run(&w) == 0);
    static const int want[] = {100, 2, 1, 4, 4, 3, 2, 100, 1, 100, 3, 3}; // (the search leaves Priya alone, first)
    int n = (int)(sizeof want / sizeof want[0]), same = g_zone_n == n;
    for (int i = 0; i < n && i < g_zone_n; i++) {
        CHECK_MSG(g_zone_seen[i] == want[i], "home focus step %d: the frame is on %d (wanted %d)", i, g_zone_seen[i], want[i]);
        same &= g_zone_seen[i] == want[i];
    }
    CHECK_MSG(g_zone_n == n, "every step was looked at (%d of %d)", g_zone_n, n);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, UNCLAIMED), "Sign out, picked with the D-pad, signed Maya out");
    w_done(&w);
    g_w = NULL;
}

// What the interface asks the console to play (the PC only counts it): a step of the focus peels one slip up and sticks the last one down, a
// press that leads nowhere is two low taps, a press is a tap, a dialog is pinned over the screen, the roll taps as it turns, and a sign-out
// ends with the done sound.
static int g_snd0[SND_COUNT];

static void snd_mark(HostPlat *h) { memcpy(g_snd0, h->sounds, sizeof g_snd0); }

static void snd_move(HostPlat *h)
{
    CHECK_MSG(h->sounds[SND_PEEL] > g_snd0[SND_PEEL] && h->sounds[SND_STICK] > g_snd0[SND_STICK], "a step of the focus: a peel and a tap (%d, %d)",
              h->sounds[SND_PEEL] - g_snd0[SND_PEEL], h->sounds[SND_STICK] - g_snd0[SND_STICK]);
}

static void snd_refuse(HostPlat *h) { CHECK_MSG(h->sounds[SND_REFUSE] > g_snd0[SND_REFUSE], "a press that leads nowhere is refused out loud"); }

static void snd_dialog(HostPlat *h)
{
    CHECK_MSG(h->sounds[SND_TICK] > g_snd0[SND_TICK], "A pushes the tile flat with a tap");
    CHECK_MSG(h->sounds[SND_STICK] > g_snd0[SND_STICK], "and the dialog is pinned over the screen");
}

static void snd_signed_out(HostPlat *h)
{
    CHECK_MSG(h->sounds[SND_DONE] > g_snd0[SND_DONE], "the sign-out ends with the done sound");
    CHECK_MSG(h->sounds[SND_SWISH] > g_snd0[SND_SWISH], "and the page changes with a swish");
}

static void sc_sounds(void)
{
    W w;
    reminder_world(&w, "42-sounds", 0); // Maya's saves are loaded; Priya is the other profile
    HostPlat *h = &w.hp;
    hp_call(h, snd_mark);
    hp_btn(h, BTN_RIGHT); // Priya's tile
    hp_call(h, snd_move);
    hp_btn(h, BTN_RIGHT); // the new-profile tile
    hp_call(h, snd_mark);
    hp_btn(h, BTN_RIGHT); // nothing further
    hp_call(h, snd_refuse);
    hp_btn(h, BTN_LEFT);
    hp_call(h, snd_mark);
    hp_btn(h, BTN_A); // switch to Priya? (her password is not asked: she has none)
    hp_snap(h, "dialog");
    hp_call(h, snd_dialog);
    hp_btn(h, BTN_B);
    hp_call(h, snd_mark);
    hp_btn(h, BTN_X); // sign Maya out
    hp_snap(h, "signed-out");
    hp_call(h, snd_signed_out);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

// A step that scrolls the Home grid goes on with the lift (the old tile sticks back down and the new one peels up where the grid comes to rest, with their
// sounds) instead of jumping.
extern float g_ui_home_lift, g_ui_home_lift_low;

static void snd_one_step(HostPlat *h)
{
    int peel = h->sounds[SND_PEEL] - g_snd0[SND_PEEL], stick = h->sounds[SND_STICK] - g_snd0[SND_STICK];
    CHECK_MSG(peel == 1 && stick == 1, "one step: one peel and one tap (%d, %d)", peel, stick);
}

static void home_lift_clear(HostPlat *h)
{
    snd_mark(h);
    g_ui_home_lift_low = 2.0f;
}

static void home_lift_rising(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_home_lift_low >= 0.0f && g_ui_home_lift_low < 0.7f, "the new tile peeled up from the page after the scroll (it was at least %.2f up in every frame)", g_ui_home_lift_low);
}

static void look_on(HostPlat *h)
{
    h->ntrace = 0;
    h->trace_on = 1;
    h->trace_png = 1;
}

static void look_off(HostPlat *h) { h->trace_on = 0; }

// the keypad with the focus on one key and a finger on another: every frame of the press is kept (TRACE_PNG=1) to be looked at
static void sc_pad_look(void)
{
    W w;
    w_init(&w, "46-pad-look", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    store_admin_set_pin(&st, "512345");
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, NULL);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", UNCLAIMED);
    store_save_state(&st);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_btn(h, BTN_UP); // focus on the 2
    hp_in(h, 20);
    hp_call(h, look_on);
    hp_tap(h, 640, 391); // a finger on the 5
    hp_in(h, 12);
    hp_call(h, look_off);
    hp_pin(h, "12345");
    hub_leave(h, &hub);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

// "still working": the taped scrap that comes down when the app has been busy a while between two screens, doodles, and is peeled off when the work is over.
// Driven here by the pump the long loops call (gfx_pump), on the host's virtual clock.
// A key that goes in must not uncover its dashed patch: pressed with its ghost and pressed with none, the key looks the same inside its own card (the
// console showed the dashed outline round and over a pressed key).
extern int g_ui_press_ghostless;
extern Rect g_ui_press_cover_last;
static uint8_t *g_kp_first;
static Rect g_kp_rect;
static int g_kp_on, g_kp_n;
static long g_kp_diff = -1, g_kp_px;

static void kp_hook(const uint8_t *rest, int x0, int y0, int w, int h, const Surf *pressed, Rect r, int with_over)
{
    press_dump(rest, x0, y0, w, h, pressed, r, with_over);
    if (!g_kp_on) return;
    Rect c = g_ui_press_cover_last;
    if (c.w <= 0 || c.h <= 0) return;
    if (g_kp_n++ == 0) {
        g_kp_rect = c;
        free(g_kp_first);
        g_kp_first = malloc((size_t)c.w * c.h * 4);
        for (int y = 0; y < c.h && g_kp_first; y++) memcpy(g_kp_first + (size_t)y * c.w * 4, pressed->px + ((size_t)(c.y + y) * pressed->w + c.x) * 4, (size_t)c.w * 4);
        return;
    }
    if (!g_kp_first || memcmp(&c, &g_kp_rect, sizeof c)) return;
    g_kp_diff = 0;
    // (the top three quarters: at the bottom the strip of patch the lifted key leaves showing goes in with the key when there is no patch to keep it still)
    int rows = c.h * 3 / 4;
    g_kp_px = (long)c.w * rows;
    if (getenv("KP_DUMP")) { // KP_DUMP=<file.png>: the two pressed keys side by side (with a patch, then without)
        uint8_t *img = malloc((size_t)c.w * 2 * c.h * 4);
        for (int y = 0; y < c.h && img; y++) {
            memcpy(img + (size_t)y * c.w * 8, g_kp_first + (size_t)y * c.w * 4, (size_t)c.w * 4);
            memcpy(img + (size_t)y * c.w * 8 + (size_t)c.w * 4, pressed->px + ((size_t)(c.y + y) * pressed->w + c.x) * 4, (size_t)c.w * 4);
            for (int x = 0; x < c.w * 2; x++) img[((size_t)y * c.w * 2 + x) * 4 + 3] = 255;
        }
        if (img) stbi_write_png(getenv("KP_DUMP"), c.w * 2, c.h, 4, img, c.w * 8);
        free(img);
        printf("  pad-press: card (%d,%d %dx%d)\n", c.x, c.y, c.w, c.h);
    }
    for (int y = 0; y < rows; y++)
        for (int x = 0; x < c.w; x++) {
            const uint8_t *a = g_kp_first + ((size_t)y * c.w + x) * 4, *b = pressed->px + ((size_t)(c.y + y) * pressed->w + c.x + x) * 4;
            if (abs(a[0] - b[0]) > 6 || abs(a[1] - b[1]) > 6 || abs(a[2] - b[2]) > 6) g_kp_diff++;
        }
}

static void kp_on(HostPlat *h) { (void)h; g_kp_on = 1; g_kp_n = 0; g_kp_diff = -1; memset(&g_ui_press_cover_last, 0, sizeof g_ui_press_cover_last); }
static void kp_ghostless(HostPlat *h) { (void)h; g_ui_press_ghostless = 1; }
static void kp_off(HostPlat *h) { (void)h; g_ui_press_ghostless = 0; g_kp_on = 0; }

static void sc_pad_press(void)
{
    W w;
    w_init(&w, "46b-pad-press", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    store_admin_set_pin(&st, "551234"); // (the pad opens on the 5: two presses of it are the first two digits)
    seed_profile(&w, &st, "Alex", NULL, 3600, 1, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    g_ui_press_hook = kp_hook;
    hp_btn(h, BTN_ADMIN);
    hp_in(h, 40); // (the 5 lifted and still)
    hp_call(h, kp_on);
    hp_btn(h, BTN_A);
    hp_in(h, 40); // (its wiggle over)
    hp_call(h, kp_ghostless);
    hp_btn(h, BTN_A);
    hp_call(h, kp_off);
    hp_pin(h, "1234");
    hub_leave(h, &hub);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    CHECK(run(&w) == 0);
    g_ui_press_hook = press_dump;
    CHECK_MSG(g_kp_n == 2 && g_kp_diff >= 0, "both presses of the 5 were seen, with the key's card (%d presses)", g_kp_n);
    CHECK_MSG(g_kp_diff >= 0 && g_kp_diff <= g_kp_px / 200, "a pressed key shows nothing of its dashed patch inside its card (%ld of %ld px differ from a press with no patch)", g_kp_diff, g_kp_px);
    free(g_kp_first);
    g_kp_first = NULL;
    w_done(&w);
    g_w = NULL;
}

// A profile's card pressed with A (it is lifted, and leaves a dashed patch where it sat): the press shows nothing of that patch inside the card, in any theme
// (on the console, in Paper night, the dashed outline showed round the card and over it). Pressed with its patch and pressed with none, the card looks the same.
static long g_hp_tape, g_hp_n, g_hp_right;
static void hp_press_hook(const uint8_t *rest, int x0, int y0, int w, int h, const Surf *pressed, Rect r, int with_over)
{
    kp_hook(rest, x0, y0, w, h, pressed, r, with_over);
    if (getenv("HPDBG")) printf("press region %d,%d %dx%d  r %d,%d %dx%d\n", x0, y0, w, h, r.x, r.y, r.w, r.h);
    if (g_hp_n++) return;
    // the tape stands above the card, more than the 10 px the press used to reach: it has to go in with the card
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const int ax = x0 + x, ay = y0 + y;
            if (ay >= r.y - 14 || ax < r.x - 4 || ax >= r.x + r.w + 4) continue;
            const uint8_t *a = rest + ((size_t)y * w + x) * 4, *b = pressed->px + ((size_t)ay * pressed->w + ax) * 4;
            if (abs(a[0] - b[0]) >= 10 || abs(a[1] - b[1]) >= 10 || abs(a[2] - b[2]) >= 10) g_hp_tape++;
        }
    // and the neighbours beside it, tape and all, stay exactly as they are (the card to its right, and its tape standing up from its top left corner)
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            const int ax = x0 + x;
            if (ax < r.x + r.w + 24) continue;
            const uint8_t *a = rest + ((size_t)y * w + x) * 4, *b = pressed->px + ((size_t)(y0 + y) * pressed->w + ax) * 4;
            if (abs(a[0] - b[0]) >= 6 || abs(a[1] - b[1]) >= 6 || abs(a[2] - b[2]) >= 6) { g_hp_right++; if (getenv("HPDBG")) printf("right px at %d,%d (r %d,%d %dx%d) rest %d,%d,%d now %d,%d,%d\n", ax, y0 + y, r.x, r.y, r.w, r.h, a[0], a[1], a[2], b[0], b[1], b[2]); }
        }
}

static void sc_home_press(void)
{
    W w;
    w_init(&w, "46c-home-press", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    static const char *names[] = {"Ana", "Ben", "Cal", "Dee", "Eve", "Fay"};
    for (int i = 0; i < 6; i++) seed_profile(&w, &st, names[i], NULL, 3600 * (i + 1), 1, NULL);
    HostPlat *h = &w.hp;
    g_ui_press_hook = hp_press_hook;
    g_hp_tape = g_hp_n = g_hp_right = 0;
    hp_btn(h, BTN_RIGHT);
    hp_in(h, 400); // (the card lifted and still)
    hp_call(h, kp_on);
    hp_btn(h, BTN_A); // "Play as Ben?"
    hp_btn(h, BTN_B);
    hp_in(h, 600);
    hp_call(h, kp_ghostless);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B);
    hp_call(h, kp_off);
    CHECK(run(&w) == 0);
    g_ui_press_hook = press_dump;
    CHECK_MSG(g_kp_n == 2 && g_kp_diff >= 0, "both presses of the card were seen, with its cover (%d presses)", g_kp_n);
    CHECK_MSG(g_kp_diff >= 0 && g_kp_diff <= g_kp_px / 200, "a pressed card shows nothing of its dashed patch inside it (%ld of %ld px differ from a press with no patch)", g_kp_diff, g_kp_px);
    CHECK_MSG(g_hp_tape >= 20, "its tape, standing above the card, goes in with it (%ld px of it moved)", g_hp_tape);
    CHECK_MSG(g_hp_right == 0, "the card to its right, and its tape, did not move (%ld px changed)", g_hp_right);
    free(g_kp_first);
    g_kp_first = NULL;
    w_done(&w);
    g_w = NULL;
}

// the same on a card in the second row: the tape standing above it reaches up between the rows, where the rectangles kept still for the cards of the row above
// are; it must go in with its card all the same
static void sc_home_press_row2(void)
{
    W w;
    w_init(&w, "46d-home-press-row2", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    static const char *names[] = {"Ana", "Ben", "Cal", "Dee", "Eve", "Fay", "Gus", "Hal", "Ivy", "Jon", "Kit", "Lea", "Max", "Ned", "Oli", "Pam"};
    for (int i = 0; i < 16; i++) seed_profile(&w, &st, names[i], NULL, 3600 * (i + 1), 1, NULL);
    HostPlat *h = &w.hp;
    g_ui_press_hook = hp_press_hook;
    g_hp_tape = g_hp_n = g_hp_right = 0;
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_DOWN); // (the second row)
    hp_in(h, 400);
    hp_call(h, kp_on);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B);
    hp_call(h, kp_off);
    CHECK(run(&w) == 0);
    g_ui_press_hook = press_dump;
    CHECK_MSG(g_hp_n >= 1 && g_hp_tape >= 20, "a card in the second row: its tape, standing above the card, goes in with it (%ld px of it moved)", g_hp_tape);
    CHECK_MSG(g_hp_right == 0, "and the card to its right, and its tape, did not move (%ld px changed)", g_hp_right);
    free(g_kp_first);
    g_kp_first = NULL;
    w_done(&w);
    g_w = NULL;
}

static Surf *g_wait_before;

static int wait_diff_in(const Surf *a, const Surf *b, int inside)
{
    int n = 0;
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            Rect ar = ui_wait_area();
            int in = x >= ar.x && x < ar.x + ar.w && y >= ar.y && y < ar.y + ar.h;
            if (in != inside) continue;
            if (memcmp(a->px + ((size_t)y * SCREEN_W + x) * 4, b->px + ((size_t)y * SCREEN_W + x) * 4, 4)) n++;
        }
    return n;
}

// WAIT_FRAMES=1: a picture of every `every`th frame shown from now on (up to `count`), for a contact sheet of the animation
static int g_wait_every, g_wait_left, g_wait_n;
static const char *g_wait_name = "frame";

static void wait_hook(HostPlat *h, const Surf *f)
{
    (void)f;
    if (getenv("WAIT_LOG")) printf("WAITFRAME %ld\n", (long)h->t_ms); // (the host clock at every frame shown: for pacing a played-back sequence)
    if (g_wait_every > 0 && g_wait_left > 0 && (g_wait_n++ % g_wait_every) == 0) {
        g_wait_left--;
        hp_save_png(h, g_wait_name);
    }
}

static void wait_watch(HostPlat *h, const char *name, int every, int count)
{
    g_wait_every = every;
    g_wait_left = count;
    g_wait_n = 0;
    g_wait_name = name;
    h->trace_on = every > 0 && getenv("WAIT_FRAMES") != NULL;
    h->trace_png = 0;
    h->frame_hook = h->trace_on ? wait_hook : NULL;
}

static void wait_probe(HostPlat *h)
{
    g_wait_before = surf_clone(G.work); // (Home is up and the app is idle: G.work is what is on the screen)
    int keep_after = g_ui_wait_after_ms;
    g_ui_wait_after_ms = 60;
    long full0 = h->full_presents, rect0 = h->rect_presents, shown0 = g_ui_wait_shown, fr0 = g_ui_wait_frames;
    ui_screen_done();
    for (int i = 0; i < 4; i++) gfx_pump();
    CHECK_MSG(!ui_wait_shown() && g_ui_wait_shown == shown0, "a short wait shows nothing");
    // a long one: the scrap comes down, and every later pump is a frame now and then
    int longrun = getenv("WAIT_LONG") != NULL; // (WAIT_LONG=1 WAIT_FRAMES=1: about ten doodles' worth of frames, to see every symbol)
    wait_watch(h, "run", longrun ? 6 : 4, longrun ? 240 : 40);
    for (int i = 0; i < 60000 && g_ui_wait_frames < fr0 + (longrun ? 1500 : 160); i++) gfx_pump();
    wait_watch(h, "", 0, 0);
    CHECK_MSG(g_ui_wait_shown == shown0 + 1 && ui_wait_shown(), "the scrap came down once, after the wait");
    CHECK_MSG(g_ui_wait_frames >= fr0 + 150, "and went on moving as the loops pumped (%ld frames)", g_ui_wait_frames - fr0);
    CHECK_MSG(wait_diff_in(h->last, g_wait_before, 1) > 60, "the drawing is on the screen");
    CHECK_MSG(wait_diff_in(h->last, g_wait_before, 0) == 0, "and nothing outside its place changed (%d pixels)", wait_diff_in(h->last, g_wait_before, 0));
    CHECK_MSG(wait_diff_in(G.work, g_wait_before, 1) + wait_diff_in(G.work, g_wait_before, 0) == 0, "while the app's own frame is untouched: the scrap is never drawn on it (%d + %d pixels)",
              wait_diff_in(G.work, g_wait_before, 1), wait_diff_in(G.work, g_wait_before, 0));
    CHECK_MSG(gfx_pump_hook != NULL && fsx_pump_hook != NULL, "both pumps are hooked while the app is busy");
    hp_save_png(h, "before-the-end");
    // the work is over: the rubbing out begins at once and holds nothing up
    long fr1 = g_ui_wait_frames;
    uint64_t t_fin0 = ui_now();
    ui_wait_finish();
    CHECK_MSG(ui_wait_erasing() && !ui_wait_shown() && g_ui_wait_frames == fr1 && ui_now() - t_fin0 < 100, "finishing returns at once, with the rubbing out begun and no frame waited for");
    CHECK_MSG(gfx_pump_hook != NULL, "the pumps stay hooked: the rubbing out goes on while the next screen is worked on");
    // the next screen's rectangles are shown with the ink over them: the display is the app's frame everywhere but in the place of the ink, and the app's frame never has any
    Rect dot = {700, 100, 40, 30};
    for (int y = 0; y < dot.h; y++)
        for (int x = 0; x < dot.w; x++) memset(G.work->px + ((size_t)(dot.y + y) * SCREEN_W + dot.x + x) * 4, 0x80, 3);
    ui_present_rects(G.work, &dot, 1);
    CHECK_MSG(ui_wait_erasing() && wait_diff_in(h->last, G.work, 0) == 0, "a screen shown while it is rubbed out: the display is its frame outside the place of the ink (%d pixels differ)", wait_diff_in(h->last, G.work, 0));
    CHECK_MSG(wait_diff_in(h->last, g_wait_before, 1) > 20, "and the ink is still there in its place (%d pixels)", wait_diff_in(h->last, g_wait_before, 1));
    CHECK_MSG(wait_diff_in(G.work, g_wait_before, 1) == 0, "while the app's own frame has none of it");
    wait_watch(h, "end", 3, 48);
    for (int i = 0; i < 6000 && ui_wait_erasing(); i++) ui_wait_tick();
    wait_watch(h, "", 0, 0);
    CHECK_MSG(g_ui_wait_frames > fr1 + 15, "the rubbing out played (%ld frames)", g_ui_wait_frames - fr1);
    CHECK_MSG(!ui_wait_erasing() && gfx_pump_hook == NULL && fsx_pump_hook == NULL, "and then nothing is hooked any more");
    CHECK_MSG(wait_diff_in(h->last, G.work, 1) + wait_diff_in(h->last, G.work, 0) == 0, "the display is the app's frame, none of the ink left (%d + %d pixels differ)", wait_diff_in(h->last, G.work, 1),
              wait_diff_in(h->last, G.work, 0));
    CHECK_MSG(h->full_presents == full0 && h->rect_presents > rect0 + 100, "only rectangles were presented (%ld full, %ld rectangles)", (long)(h->full_presents - full0), (long)(h->rect_presents - rect0));
    hp_save_png(h, "after-the-end");
    surf_copy(G.work, g_wait_before);
    ui_present_rects(G.work, &dot, 1);
    // asked to end before much has been drawn: it is rubbed out just the same, and the screen is the same afterwards
    ui_screen_done();
    for (int i = 0; i < 400 && !ui_wait_shown(); i++) gfx_pump();
    CHECK(ui_wait_shown());
    ui_wait_finish();
    for (int i = 0; i < 6000 && ui_wait_erasing(); i++) ui_wait_tick();
    CHECK_MSG(!ui_wait_erasing() && !ui_wait_shown() && wait_diff_in(h->last, g_wait_before, 1) + wait_diff_in(h->last, g_wait_before, 0) == 0, "ended at once, the screen is as it was");
    // a whole frame shown while it is down starts the rubbing out and carries what is left of the ink: the display is that frame, but for the ink, and then entirely
    ui_screen_done();
    for (int i = 0; i < 400 && !ui_wait_shown(); i++) gfx_pump();
    CHECK(ui_wait_shown());
    long fr_a = g_ui_wait_frames;
    for (int i = 0; i < 20000 && g_ui_wait_frames < fr_a + 25; i++) gfx_pump(); // (the drawing fades in: it is on the display only some frames in)
    surf_copy(G.work, g_wait_before);
    ui_present(G.work);
    CHECK_MSG(ui_wait_erasing() && !ui_wait_shown(), "a whole frame while it is down: the rubbing out begins");
    CHECK_MSG(wait_diff_in(h->last, g_wait_before, 0) == 0 && wait_diff_in(G.work, g_wait_before, 1) == 0, "the display is that frame outside the ink's place, and the app's frame has no ink");
    for (int i = 0; i < 6000 && ui_wait_erasing(); i++) ui_wait_tick();
    CHECK_MSG(gfx_pump_hook == NULL && fsx_pump_hook == NULL, "and when it is done the pumps are free");
    CHECK_MSG(wait_diff_in(h->last, g_wait_before, 1) + wait_diff_in(h->last, g_wait_before, 0) == 0, "the screen is that frame, with nothing of the ink left");
    // the change to the next screen (a fade) with the ink still there: it goes on over the change, and the next screen is what is left
    ui_screen_done();
    for (int i = 0; i < 400 && !ui_wait_shown(); i++) gfx_pump();
    long fr_b = g_ui_wait_frames;
    for (int i = 0; i < 20000 && g_ui_wait_frames < fr_b + 40; i++) gfx_pump();
    ui_transition();
    CHECK_MSG(ui_wait_erasing(), "the next screen's change starts the rubbing out");
    for (int y = 0; y < 300; y++)
        for (int x = 0; x < 200; x++) memset(G.work->px + ((size_t)(200 + y) * SCREEN_W + 900 + x) * 4, 0x60, 3); // (the next screen differs from this one)
    ui_present(G.work);
    CHECK_MSG(wait_diff_in(h->last, G.work, 0) == 0, "the change to the next screen played; the display is that screen outside the ink's place (%d pixels differ)", wait_diff_in(h->last, G.work, 0));
    for (int i = 0; i < 6000 && ui_wait_erasing(); i++) ui_wait_tick();
    CHECK_MSG(wait_diff_in(h->last, G.work, 1) + wait_diff_in(h->last, G.work, 0) == 0, "and when the rubbing out is over the display is that screen, entirely");
    // the case of the recording (2026-10-08): a screen that shows only the rectangles it changed while the drawing is down, with a frame of its own in G.work; the display must
    // be that frame outside the ink's place, and the app's frame must never carry any of it
    surf_copy(G.work, g_wait_before);
    ui_present(G.work);
    ui_screen_done();
    for (int i = 0; i < 400 && !ui_wait_shown(); i++) gfx_pump();
    CHECK(ui_wait_shown());
    long fr2 = g_ui_wait_frames;
    for (int i = 0; i < 20000 && g_ui_wait_frames < fr2 + 25; i++) gfx_pump(); // (the drawing fades in: it is on the display only some frames in)
    CHECK_MSG(wait_diff_in(h->last, g_wait_before, 1) > 60, "the drawing is on the display again");
    Rect dot2 = {760, 100, 40, 30}; // (a rectangle well away from the drawing)
    for (int y = 0; y < dot2.h; y++)
        for (int x = 0; x < dot2.w; x++) memset(G.work->px + ((size_t)(dot2.y + y) * SCREEN_W + dot2.x + x) * 4, 0x40, 3);
    ui_present_rects(G.work, &dot2, 1);
    CHECK_MSG(wait_diff_in(h->last, G.work, 0) == 0, "a screen that shows only a rectangle while the drawing is down: the display is its frame outside the ink's place");
    CHECK_MSG(wait_diff_in(G.work, g_wait_before, 1) == 0, "and the app's frame has none of it in the place the drawing had (%d pixels)", wait_diff_in(G.work, g_wait_before, 1));
    for (int i = 0; i < 6000 && ui_wait_erasing(); i++) ui_wait_tick();
    CHECK_MSG(wait_diff_in(h->last, G.work, 1) + wait_diff_in(h->last, G.work, 0) == 0, "and afterwards the display and the frame are the same");
    // a new screen done while the last of the ink is still being rubbed out: it is taken off at once, and the place is shown as the frame has it
    ui_screen_done();
    for (int i = 0; i < 400 && !ui_wait_shown(); i++) gfx_pump();
    CHECK(ui_wait_shown());
    ui_wait_finish();
    CHECK_MSG(ui_wait_erasing(), "finish began the rubbing out");
    ui_screen_done();
    CHECK_MSG(!ui_wait_erasing() && wait_diff_in(h->last, G.work, 1) + wait_diff_in(h->last, G.work, 0) == 0, "a screen done while the rubbing out is on takes the last of it off at once");
    surf_copy(G.work, g_wait_before);
    ui_present(G.work);
    g_ui_wait_after_ms = keep_after;
    surf_free(g_wait_before);
    g_wait_before = NULL;
}

// the banner's looks: every one of them on the Home screen with somebody loaded (pictures), and an app that starts with none chosen picks one
enum { NBANNERS_MAX = 8 }; // (how many looks ui_home.c has)
static unsigned char g_bn_crop[NBANNERS_MAX][120 * 330 * 4];
static int g_bn_idx;

static void banner_probe(HostPlat *h)
{
    if (g_bn_idx < NBANNERS_MAX) // (the left part of the card: the doodle behind the picture)
        for (int y = 0; y < 120; y++) memcpy(g_bn_crop[g_bn_idx] + y * 330 * 4, h->last->px + ((size_t)(97 + y) * SCREEN_W + 40) * 4, 330 * 4);
    g_bn_idx++;
}

static void sc_banners(void)
{
    g_bn_idx = 0;
    int saved = g_ui_banner_variant;
    for (int v = 0; v < NBANNERS_MAX; v++) {
        g_ui_banner_variant = v;
        W w;
        reminder_world(&w, "48-banners", 0);
        HostPlat *h = &w.hp;
        char name[24];
        snprintf(name, sizeof name, "banner-%d", v);
        hp_snap(h, name);
        hp_call(h, banner_probe);
        hp_btn(h, BTN_B);
        CHECK(run(&w) == 0);
        w_done(&w);
        g_w = NULL;
    }
    for (int a = 0; a < NBANNERS_MAX; a++)
        for (int b = a + 1; b < NBANNERS_MAX; b++) {
            int diff = 0;
            for (int i = 0; i < 120 * 330; i++) diff += memcmp(g_bn_crop[a] + i * 4, g_bn_crop[b] + i * 4, 4) != 0;
            CHECK_MSG(diff > 700, "banners %d and %d look different (%d pixels differ)", a, b, diff);
        }
    g_ui_banner_variant = -1; // an app that has not chosen one picks one when it first needs it
    W w;
    reminder_world(&w, "48-banners", 0);
    hp_btn(&w.hp, BTN_B);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_ui_banner_variant >= 0 && g_ui_banner_variant < NBANNERS_MAX, "a variant was picked (%d)", g_ui_banner_variant);
    w_done(&w);
    g_w = NULL;
    g_ui_banner_variant = saved;
}

static void sc_wait(void)
{
    W w;
    w_init(&w, "47-wait", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, NULL);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", UNCLAIMED);
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_call(h, wait_probe);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

// WAIT_CLIP (scenario wait-clip): the left of the footer as it is shown at every frame of one wait, saved as small pictures named <number>_<host ms>.png in CLIP_DIR, for the
// mockup of the drawing and erasing. CLIP_FINISH = how long after the drawing began the work ends (ms).
static char g_clip_dir[400];
static int g_clip_n;

static void clip_hook(HostPlat *h, const Surf *f)
{
    static uint8_t crop[520 * 76 * 4];
    for (int y = 0; y < 76; y++) memcpy(crop + (size_t)y * 520 * 4, f->px + ((size_t)(636 + y) * SCREEN_W + 40) * 4, 520 * 4);
    char p[600];
    snprintf(p, sizeof p, "%s/%03d_%ld.png", g_clip_dir, g_clip_n++, (long)h->t_ms);
    stbi_write_png(p, 520, 76, 4, crop, 520 * 4);
}

static void clip_probe(HostPlat *h)
{
    int finish = getenv("CLIP_FINISH") ? atoi(getenv("CLIP_FINISH")) : 900;
    snprintf(g_clip_dir, sizeof g_clip_dir, "%s", getenv("CLIP_DIR") ? getenv("CLIP_DIR") : "clip");
    fsx_mkdir_p(g_clip_dir);
    g_ui_wait_after_ms = 60;
    g_clip_n = 0;
    clip_hook(h, h->last);
    h->trace_on = 1;
    h->trace_png = 0;
    h->frame_hook = clip_hook;
    ui_screen_done();
    uint64_t t_show = 0;
    for (int i = 0; i < 400000; i++) {
        gfx_pump();
        if (!ui_wait_shown()) continue;
        if (!t_show) t_show = ui_now();
        if (ui_now() - t_show >= (uint64_t)finish) break;
    }
    ui_wait_finish();
    h->trace_on = 0;
    h->frame_hook = NULL;
}

static void sc_wait_clip(void)
{
    W w;
    w_init(&w, "47c-wait-clip", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, NULL);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", UNCLAIMED);
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_call(h, clip_probe);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

static void sc_sounds_scroll(void)
{
    W w;
    w_init(&w, "43b-sounds-scroll", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    for (int i = 0; i < 16; i++) { // sixteen and the new-profile tile: three rows, and the grid shows two
        char nm[16];
        snprintf(nm, sizeof nm, "P%02d", i + 1);
        seed_profile(&w, &st, nm, NULL, 3600 + i * 60, 1, NULL);
    }
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_DOWN); // the second row: nothing scrolls
    hp_call(h, home_lift_clear);
    hp_btn(h, BTN_DOWN); // the third: the grid glides there (the old tile sticks back down), and the new one peels up where it lands
    hp_wait(h, 1);
    hp_call(h, snd_one_step);
    hp_call(h, home_lift_rising);
    hp_snap(h, "scrolled-down");
    hp_btn(h, BTN_UP); // the second again: nothing scrolls
    hp_call(h, home_lift_clear);
    hp_btn(h, BTN_UP); // the first: the grid glides back
    hp_wait(h, 1);
    hp_call(h, snd_one_step);
    hp_call(h, home_lift_rising);
    hp_snap(h, "scrolled-up");
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

// Deleted profiles: delete one in Admin, find it in Deleted profiles, and put it back with its saves.
static size_t trash_count(W *w, const char *name, int *games)
{
    Store st;
    store_open(&st, &w->fb.be, w->fb.sd);
    TrashEntry *t = NULL;
    size_t n = 0, hits = 0;
    if (store_trash_list(&st, &t, &n) != 0) return 999;
    for (size_t i = 0; i < n; i++)
        if (!name || !strcmp(t[i].name, name)) {
            hits++;
            if (games) *games = t[i].games;
        }
    free(t);
    return hits;
}

static void check_nothing_deleted(HostPlat *h)
{
    (void)h;
    CHECK_MSG(trash_count(g_w, NULL, NULL) == 0, "nothing has been deleted yet");
}

// two profiles that were deleted three weeks ago
static void seed_deleted(HostPlat *h)
{
    (void)h;
    W *w = g_w;
    Store st;
    store_open(&st, &w->fb.be, w->fb.sd);
    time_t keep = w->fb.clock;
    char id[ID_MAX];
    w->fb.clock = w->now0 - 21 * 86400 - 7200;
    seed_profile(w, &st, "Sam", NULL, -1, 0, id);
    CHECK(store_profile_trash(&st, id) == 0);
    w->fb.clock += 3600;
    seed_profile(w, &st, "Priya", NULL, 86400 * 30, 1, id);
    CHECK(store_profile_trash(&st, id) == 0);
    w->fb.clock = keep;
}

static void check_jordan_deleted(HostPlat *h)
{
    (void)h;
    Store st;
    store_open(&st, &g_w->fb.be, g_w->fb.sd);
    Profile p;
    int games = -1;
    CHECK_MSG(!find_profile(&st, "Jordan", &p), "Jordan is not a profile any more");
    CHECK_MSG(trash_count(g_w, "Jordan", &games) == 1 && games == 3, "and is among the deleted ones, with the 3 games (%d)", games);
}

static void two_days_later(HostPlat *h)
{
    (void)h;
    g_w->fb.clock += 2 * 86400;
}

static void check_two_deleted(HostPlat *h)
{
    (void)h;
    CHECK_MSG(trash_count(g_w, NULL, NULL) == 2, "cancelling the delete for good leaves both (%zu)", trash_count(g_w, NULL, NULL));
}

static void check_one_deleted(HostPlat *h)
{
    (void)h;
    CHECK_MSG(trash_count(g_w, NULL, NULL) == 1, "deleting for good took one away (%zu)", trash_count(g_w, NULL, NULL));
}

static void sc_deleted(void)
{
    W w;
    w_init(&w, "23-deleted-profiles", 0);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char jordan[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 2, NULL);
    seed_profile(&w, &st, "Jordan", NULL, 86400 * 4, 3, jordan);
    seed_profile(&w, &st, "Maya", NULL, 60 * 20, 1, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_open(h, &hub, HUB_DELETED);
    hp_snap(h, "nothing-deleted");
    hp_call(h, check_nothing_deleted);
    hp_btn(h, BTN_A); // nothing to restore: nothing happens
    hp_btn(h, BTN_B);
    hp_call(h, seed_deleted);
    hub_open(h, &hub, HUB_PROFILES);
    hp_btn(h, BTN_DOWN); // Jordan
    hp_btn(h, BTN_A);
    hp_btn_n(h, BTN_DOWN, 6); // Delete profile (Read-only comes before it)
    hp_btn(h, BTN_A);
    hp_snap(h, "delete-confirm");
    hp_btn(h, BTN_A); // Delete
    hp_snap(h, "profiles-without-jordan");
    hp_call(h, check_jordan_deleted);
    hp_btn(h, BTN_B);
    hp_snap(h, "hub-three-deleted");
    hp_call(h, two_days_later);
    hub_open(h, &hub, HUB_DELETED);
    hp_snap(h, "deleted-profiles");
    hp_btn(h, BTN_A); // Jordan, the latest
    hp_snap(h, "restore-confirm");
    hp_btn(h, BTN_B); // cancel
    hp_call(h, check_jordan_deleted);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // Restore
    hp_snap(h, "jordan-is-back");
    hp_btn(h, BTN_A);
    hp_snap(h, "two-left");
    hp_btn(h, BTN_X); // delete the first one for good: asks first
    hp_snap(h, "delete-for-good-confirm");
    hp_btn(h, BTN_B); // cancel
    hp_call(h, check_two_deleted);
    hp_btn(h, BTN_X);
    hp_btn(h, BTN_A); // Delete
    hp_snap(h, "one-left");
    hp_call(h, check_one_deleted);
    hp_btn(h, BTN_B);
    hp_snap(h, "hub-one-deleted");
    hub_leave(h, &hub);
    hp_snap(h, "home-with-jordan");
    hp_btn(h, BTN_B); // nobody is loaded, so this leaves
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Profile j;
    CHECK_MSG(find_profile(&st2, "Jordan", &j) && !strcmp(j.id, jordan), "Jordan is a profile again, the same one");
    char cur[FSX_PATH];
    snprintf(cur, sizeof cur, "%s/profiles/%s/current", w.fb.sd, jordan);
    CHECK_MSG(count_dir(cur) == 3, "with the saves of all three games (%zu)", count_dir(cur));
    CHECK_MSG(trash_count(&w, NULL, NULL) == 1 && trash_count(&w, "Jordan", NULL) == 0, "one of the others was deleted for good, one is still deleted (%zu)", trash_count(&w, NULL, NULL));
    CHECK(!strcmp(st2.state.holder, UNCLAIMED));
    w_done(&w);
    g_w = NULL;
}

// X + ZR on Deleted profiles: "delete all", behind a red button that fills over three seconds. A early does nothing; B cancels.
static void check_none_deleted(HostPlat *h)
{
    (void)h;
    CHECK_MSG(trash_count(g_w, NULL, NULL) == 0, "delete all took every deleted profile (%zu left)", trash_count(g_w, NULL, NULL));
}

// ---- the dialogs' paper look: pixel probes of the wait dialog's stages, the pad's lifted key and its coloured-in rings
static int count_px(const Surf *f, int x0, int y0, int x1, int y1, int (*pred)(const uint8_t *))
{
    int n = 0;
    for (int y = y0; y < y1; y++)
        for (int x = x0; x < x1; x++) n += pred(f->px + ((size_t)y * SCREEN_W + x) * 4);
    return n;
}
static int is_yellow(const uint8_t *q) // the seal's tape, slot 2: yellow (in Paper night the moon's cream, frosted over the night paper: its colour worked out)
{
    if (!pal_light(g_pal.paper)) {
        uint32_t t = paper_tape_rgb[2];
        if (q[2] + 8 >= q[0]) return 0; // (the cream is warm; the pale writing's soft edges near it are not)
        for (int c = 0; c < 3; c++) {
            float want = ((t >> (16 - 8 * c)) & 255) * g_pal.tape_alpha + g_pal.tape_paper[c] * 255.0f * (1.0f - g_pal.tape_alpha);
            if (fabsf(q[c] - want) > 24) return 0;
        }
        return 1;
    }
    return q[0] > 235 && q[1] > 235 && q[2] < 205;
}

static void check_wait_sealed(HostPlat *h)
{
    CHECK_MSG(px_near(h->last, 665, 500, 0xF4DAD7, 10), "the sealed red button is pale");
    CHECK_MSG(count_px(h->last, 690, 440, 850, 468, is_yellow) > 200, "a strip of yellow tape lies across the button's top seam");
    CHECK_MSG(count_px(h->last, 760, 380, 860, 432, is_yellow) == 0, "nothing peeled yet");
}

static void check_wait_open(HostPlat *h)
{
    CHECK_MSG(px_near(h->last, 665, 500, 0xC6362F, 12), "after the wait the button is solid red");
    CHECK_MSG(count_px(h->last, 760, 380, 860, 432, is_yellow) > 20, "and its tape has peeled up");
}

static void sc_purge_all(void)
{
    W w;
    w_init(&w, "37-purge-all", 0);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 2, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_call(h, seed_deleted);
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_open(h, &hub, HUB_DELETED);
    hp_snap(h, "two-deleted-footer"); // the footer offers ZR + X
    hp_btn(h, BTN_PURGE);
    hp_snap(h, "wait-start");
    hp_call(h, check_wait_sealed);
    hp_btn(h, BTN_A); // too early: nothing happens
    hp_call(h, check_two_deleted);
    hp_wait(h, 1);
    hp_snap(h, "wait-1s");
    hp_wait(h, 1);
    hp_snap(h, "wait-2s");
    hp_wait(h, 1); // the button is full now
    hp_btn(h, BTN_A); // pressed the moment it fills: still ignored
    hp_call(h, check_two_deleted);
    hp_wait(h, 1);
    hp_snap(h, "ready");
    hp_call(h, check_wait_open);
    hp_btn(h, BTN_B); // cancel
    hp_call(h, check_two_deleted);
    hp_snap(h, "back-on-the-list");
    hp_btn(h, BTN_PURGE); // the wait starts again from nothing
    hp_snap(h, "wait-again");
    hp_btn(h, BTN_A);
    hp_wait(h, 1);
    hp_wait(h, 1);
    hp_wait(h, 1);
    hp_wait(h, 1);
    hp_btn(h, BTN_A); // full and settled: delete them all
    hp_call(h, check_none_deleted);
    hp_snap(h, "all-gone");
    hp_btn(h, BTN_B); // nothing is listed, so B leaves
    int rc = run(&w);
    CHECK(rc == 0);
    CHECK_MSG(trash_count(&w, NULL, NULL) == 0, "still nothing deleted at the end");
    Profile a;
    CHECK_MSG(find_profile(&st, "Alex", &a), "Alex, who was never deleted, is untouched");
    w_done(&w);
    g_w = NULL;
}

// Menus wrap: a tap past the top or bottom goes round to the other end; a held (auto-repeat) press stops there.
static void sc_wrap(void)
{
    W w;
    w_init(&w, "38-wrap", 0);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 2, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_call(h, seed_deleted);
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hp_snap(h, "hub-first-section");
    hp_btn(h, BTN_UP); // above the first section: round to the last
    hp_snap(h, "hub-wrapped-to-last");
    hp_btn(h, BTN_DOWN); // below the last: round to the first
    hp_snap(h, "hub-wrapped-to-first");
    hub_open(h, &hub, HUB_DELETED);
    hp_snap(h, "deleted-first-row");
    hp_btn(h, BTN_UP); // round to the last row (Sam)
    hp_snap(h, "deleted-wrapped-to-last");
    hp_btn(h, BTN_DOWN); // and back to the first (Priya)
    hp_btn_rep(h, BTN_UP); // a held press stops at the top
    hp_snap(h, "deleted-held-up-stays");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // Restore: it must be Priya
    hp_btn(h, BTN_B); // (the message)
    hp_btn(h, BTN_B); // back to the hub
    hub_leave(h, &hub);
    hp_btn(h, BTN_B);
    int rc = run(&w);
    CHECK(rc == 0);
    Profile p;
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(find_profile(&st2, "Priya", &p), "wrapping down then a held Up left the first row (Priya) chosen");
    CHECK_MSG(!find_profile(&st2, "Sam", &p), "Sam was not restored (the wraps took the right rows)");
    CHECK_MSG(trash_count(&w, "Sam", NULL) == 1, "Sam is still deleted");
    w_done(&w);
    g_w = NULL;
}

// Storage: what the card holds, who uses the most, how many earlier saves are kept, and deleting the older ones.
static char g_alex_id[ID_MAX], g_maya_id[ID_MAX];
static uint64_t g_trim_expect, g_profiles_before;

// a file of that many bytes in a profile's folder (rel is below profiles/<id>/)
static void put_blob(Store *st, const char *id, const char *rel, size_t bytes)
{
    char p[FSX_PATH], f[FSX_PATH], parent[FSX_PATH];
    snprintf(p, sizeof p, "profiles/%s/%s", id, rel);
    store_path(st, f, sizeof f, p);
    snprintf(parent, sizeof parent, "%s", f);
    *strrchr(parent, '/') = 0;
    fsx_mkdir_p(parent);
    char *buf = malloc(bytes + 1);
    for (size_t i = 0; i < bytes; i++) buf[i] = (char)('a' + (i * 11 + strlen(rel)) % 26);
    fsx_write_file(f, buf, bytes);
    free(buf);
}

static int has_history(W *w, const char *id, const char *stamp)
{
    char p[FSX_PATH];
    snprintf(p, sizeof p, "%s/profiles/%s/history/%s", w->fb.sd, id, stamp);
    return fsx_is_dir(p);
}

static size_t history_entries(W *w, const char *id)
{
    char p[FSX_PATH];
    snprintf(p, sizeof p, "%s/profiles/%s/history", w->fb.sd, id);
    return count_dir(p);
}

// The Storage page (ui_storage.c): its cards start at y 160. On the left the SD card, its bar of everyone's saves from x 74 to 566
// around y 329 (the saves in no profile, grey, take at most its last quarter), then the actions (rows of 84 from y 418); on the
// right the profiles (rows of 84 from y 170, each with a bar from x 722 to 1174, 54 px down its row). The frame's left side runs
// 12 px in from a card's.
enum { ST_BAR_Y = 346, ST_ROW_Y = 174, ST_ACT_Y = 432, ST_ROWH = 86, ST_BX0 = 728, ST_BW = 410, ST_BW_CARD = 492 };
extern int g_ui_storage_parts, g_ui_storage_who[40], g_ui_storage_focus;
extern float g_ui_storage_w[40], g_ui_storage_row_w[8], g_ui_storage_row_min[8];

// Seven profiles (Alex, Maya, benjamin, olivia, then Chloe, Dani and Eli together) and a deleted profile's saves.
static void storage_bars(HostPlat *h)
{
    (void)h;
    // (the bars are strips of tape now, patterned, so the page says what it laid out: parts and widths)
    CHECK_MSG(g_ui_storage_parts == 8, "the card's bar has eight parts: seven profiles and the saves nobody has (%d)", g_ui_storage_parts);
    CHECK_MSG(g_ui_storage_parts > 0 && g_ui_storage_who[g_ui_storage_parts - 1] == -1, "the saves that are nobody's are the grey end of it");
    CHECK_MSG(g_ui_storage_who[0] == 0, "Alex's part comes first");
    CHECK_MSG(g_ui_storage_row_w[0] > 0.98f * ST_BW, "Alex uses the most: a full bar (%.0f)", g_ui_storage_row_w[0]);
    CHECK_MSG(g_ui_storage_row_w[1] > 0.70f * g_ui_storage_row_w[0] && g_ui_storage_row_w[1] < 0.84f * g_ui_storage_row_w[0],
              "then Maya, about 77%% of that (%.0f of %.0f)", g_ui_storage_row_w[1], g_ui_storage_row_w[0]);
}

// where the frame is on the Storage page: a profile's row (0..4), an action (10, 11), or nowhere (-1); noted down for the end
static int g_st_seen[16], g_st_n;

static void storage_focus(HostPlat *h)
{
    (void)h;
    if (g_st_n < 16) g_st_seen[g_st_n++] = g_ui_storage_focus; // (the page says where it put the focus: the slip that is lifted)
}

// The page of everyone (a list: rows of 84 from y 194, bars from x 298 to 1014, 12 px below each row's middle), the largest first,
// each bar against Alex's. It opens on the first of the others (Chloe), so the list has scrolled by one: Maya is at the top.
static void everyone_bars(HostPlat *h)
{
    const Surf *f = h->last;
    int y[4];
    // (Washi's list: paper slips on a sheet, every 84 px from y 230; the bars run from x 306 to 982, 51 px under each slip's top, on a
    // paper-coloured track; Chloe's slip has the focus, so it is lifted 6 px)
    for (int i = 0; i < 4; i++) y[i] = 230 + i * 84 + 51 - (i == 3 ? 6 : 0);
    const int X0 = 306, BW = 676;
    const uint32_t TRACK = g_pal.pill_off; // (0xE7E1D1 in the light theme)
    CHECK_MSG(!px_near(f, X0 + BW * 70 / 100, y[0], TRACK, 6) && px_near(f, X0 + BW * 85 / 100, y[0], TRACK, 6), "Maya, 77%% of Alex");
    CHECK_MSG(!px_near(f, X0 + BW * 60 / 100, y[1], TRACK, 6) && px_near(f, X0 + BW * 72 / 100, y[1], TRACK, 6), "then benjamin");
    CHECK_MSG(!px_near(f, X0 + BW * 40 / 100, y[2], TRACK, 6) && px_near(f, X0 + BW * 55 / 100, y[2], TRACK, 6), "then olivia");
    CHECK_MSG(!px_near(f, X0 + 4, y[3], TRACK, 6) && px_near(f, X0 + BW * 20 / 100, y[3], TRACK, 6), "then Chloe, who hardly plays");
}

static void storage_grew(HostPlat *h)
{
    int n = trans_frames(h, "the profiles' card grows into the page");
    CHECK_MSG(getenv("WAIT_ON") || (n >= 6 && n <= 40), "the page grows out of the card over a quarter of a second (%d frames)", n);
}

static void storage_shrank(HostPlat *h)
{
    int n = trans_frames(h, "and shrinks back");
    CHECK_MSG(getenv("WAIT_ON") || (n >= 6 && n <= 40), "and shrinks back into it (%d frames)", n);
}

// the number kept is now three: what deleting should free, worked out apart from the app
static void storage_expect(HostPlat *h)
{
    (void)h;
    Store st;
    store_open(&st, &g_w->fb.be, g_w->fb.sd);
    Locker *lk = calloc(1, sizeof *lk);
    lk_init(lk, &g_w->fb.be, &st);
    int entries = 0;
    CHECK_MSG(st.cfg.history_keep == 3, "three are kept now (%d)", st.cfg.history_keep);
    CHECK_MSG(lk_history_trim(lk, 3, 1, &g_trim_expect, &entries) == LK_OK && entries == 8 && g_trim_expect > 0,
              "Alex's five oldest and Maya's three oldest could go (%d entries)", entries);
    lk_free(lk);
    free(lk);
    char p[FSX_PATH];
    snprintf(p, sizeof p, "%s/profiles", g_w->fb.sd);
    g_profiles_before = fsx_tree_size(p);
    CHECK_MSG(history_entries(g_w, g_alex_id) == 8 && history_entries(g_w, g_maya_id) == 6, "changing the number deletes nothing by itself");
}

static void storage_cancelled(HostPlat *h)
{
    (void)h;
    CHECK_MSG(history_entries(g_w, g_alex_id) == 8 && history_entries(g_w, g_maya_id) == 6, "Cancel deletes nothing");
}

static void storage_trimmed(HostPlat *h)
{
    (void)h;
    char p[FSX_PATH];
    snprintf(p, sizeof p, "%s/profiles", g_w->fb.sd);
    uint64_t now = fsx_tree_size(p);
    CHECK_MSG(g_profiles_before - now == g_trim_expect, "it freed what it said it would (%llu, said %llu)",
              (unsigned long long)(g_profiles_before - now), (unsigned long long)g_trim_expect);
    CHECK_MSG(history_entries(g_w, g_alex_id) == 3 && history_entries(g_w, g_maya_id) == 3, "three are left for each (%zu, %zu)",
              history_entries(g_w, g_alex_id), history_entries(g_w, g_maya_id));
    CHECK_MSG(has_history(g_w, g_alex_id, "20260906-120000") && has_history(g_w, g_alex_id, "20260908-120000") &&
                  !has_history(g_w, g_alex_id, "20260905-120000") && has_history(g_w, g_maya_id, "20260914-090000") &&
                  !has_history(g_w, g_maya_id, "20260913-090000"),
              "and they are the newest three");
}

// A pile of old saves nobody owns (a deleted profile with a lot of them): it takes only the last quarter of the bar.
static void big_trash(HostPlat *h)
{
    (void)h;
    Store st;
    store_open(&st, &g_w->fb.be, g_w->fb.sd);
    put_blob(&st, ".trash/20260901-080000-big", "current/0100000000010000/huge.bin", 20 * 1024 * 1024);
}

static void storage_capped(HostPlat *h)
{
    (void)h;
    float tot = 2.0f * (g_ui_storage_parts > 0 ? g_ui_storage_parts - 1 : 0);
    for (int i = 0; i < g_ui_storage_parts; i++) tot += g_ui_storage_w[i];
    float share = g_ui_storage_parts > 0 ? g_ui_storage_w[g_ui_storage_parts - 1] / tot : 0;
    CHECK_MSG(g_ui_storage_who[g_ui_storage_parts - 1] == -1 && share > 0.20f && share < 0.28f,
              "20 MB nobody owns, next to 2 MB in profiles: the grey is held to the last quarter of the bar (%.2f)", share);
}

static void card_stops(HostPlat *h)
{
    (void)h;
    fake_arm(&g_w->fb, 1); // the next thing asked of the console fails
}

static void sc_storage(void)
{
    W w;
    w_init(&w, "24-storage", 0);
    g_w = &w;
    w.fb.free_bytes = 13314398618ull;  // 12.4 GB
    w.fb.total_bytes = 31997506355ull; // 29.8 GB
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char id[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 2, g_alex_id);
    seed_profile(&w, &st, "Maya", NULL, 60 * 20, 2, g_maya_id);
    put_blob(&st, g_alex_id, "current/0100000000010000/big.bin", 300 * 1024);
    put_blob(&st, g_maya_id, "current/0100000000010000/big.bin", 300 * 1024);
    for (int i = 0; i < 8; i++) { // Alex has eight earlier saves, Maya six
        char rel[96];
        snprintf(rel, sizeof rel, "history/2026090%d-120000/0100000000010000/save.dat", i + 1);
        put_blob(&st, g_alex_id, rel, 40 * 1024);
        if (i >= 6) continue;
        snprintf(rel, sizeof rel, "history/2026091%d-090000/0100000000010000/save.dat", i + 1);
        put_blob(&st, g_maya_id, rel, 30 * 1024);
    }
    seed_profile(&w, &st, "benjamin", NULL, 86400 * 3, 1, id);
    put_blob(&st, id, "current/0100000000010000/big.bin", 410 * 1024);
    seed_profile(&w, &st, "olivia", NULL, 86400 * 9, 1, id);
    put_blob(&st, id, "current/0100000000010000/big.bin", 290 * 1024);
    static const char *const small[3] = {"Chloe", "Dani", "Eli"};
    for (int i = 0; i < 3; i++) { // three more who hardly play: they share the last row
        seed_profile(&w, &st, small[i], NULL, 86400 * (12 + i), 1, id);
        put_blob(&st, id, "current/0100000000010000/small.bin", (size_t)(24 - i * 8) * 1024);
    }
    put_blob(&st, ".trash/20260920-101500-zoe", "current/0100000000010000/save.dat", 60 * 1024); // a deleted profile's saves

    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    g_st_n = 0;
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_section(h, &hub, HS_GAMES);
    hp_snap(h, "games-and-saves");
    hub_open(h, &hub, HUB_STORAGE);
    hp_snap(h, "storage");
    hp_call(h, storage_bars);
    hp_call(h, storage_focus); // on Alex, who uses the most
    hp_btn(h, BTN_A);          // Alex's page
    hp_snap(h, "alex-page");
    hp_btn(h, BTN_B); // and back, the frame still on Alex
    hp_call(h, storage_focus);
    hp_btn_rep(h, BTN_UP); // nothing above Alex: a held press leans and stays
    hp_call(h, storage_focus);
    hp_btn_n(h, BTN_DOWN, 4); // the others' row
    hp_call(h, storage_focus);
    hp_call(h, trans_start);
    hp_btn(h, BTN_A); // everyone
    hp_call(h, storage_grew);
    hp_snap(h, "everyone");
    hp_call(h, everyone_bars);
    hp_btn(h, BTN_A); // the frame started on the first of the others: Chloe's page
    hp_snap(h, "chloe-page");
    hp_btn(h, BTN_B);
    hp_call(h, trans_start);
    hp_btn(h, BTN_B); // back to Storage
    hp_call(h, storage_shrank);
    hp_snap(h, "back-from-everyone");
    hp_call(h, storage_focus); // on the others' row again
    hp_btn(h, BTN_LEFT);       // the actions
    hp_call(h, storage_focus);
    hp_btn(h, BTN_A); // Backups kept per profile
    hp_snap(h, "how-many-to-keep");
    hp_btn(h, BTN_UP); // from 5 to 3
    hp_btn(h, BTN_A);
    hp_snap(h, "keeping-three");
    hp_call(h, storage_expect);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // Delete older backups now
    hp_snap(h, "delete-confirm");
    hp_btn(h, BTN_B); // cancel
    hp_call(h, storage_cancelled);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // Delete
    hp_snap(h, "freed");
    hp_call(h, storage_trimmed);
    hp_btn(h, BTN_A);
    hp_snap(h, "nothing-left-to-delete");
    hp_btn(h, BTN_A); // there is nothing to delete now: it says so
    hp_snap(h, "nothing-to-delete");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B);
    hp_call(h, big_trash);
    hp_call(h, card_stops); // the console can't say how big the card is: the page still opens
    hub_open(h, &hub, HUB_STORAGE);
    hp_snap(h, "space-unknown");
    hp_call(h, storage_capped);
    hp_call(h, power_back);
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    hp_btn(h, BTN_B); // nobody is loaded, so this leaves
    int rc = run(&w);
    CHECK(rc == 0);
    static const int want[6] = {0, 0, 0, 4, 4, 10};
    int same = g_st_n == 6;
    for (int i = 0; same && i < 6; i++) same = g_st_seen[i] == want[i];
    CHECK_MSG(same, "the frame went Alex, Alex, Alex, the others, the others, the first action (%d: %d %d %d %d %d %d)", g_st_n,
              g_st_seen[0], g_st_seen[1], g_st_seen[2], g_st_seen[3], g_st_seen[4], g_st_seen[5]);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(st2.cfg.history_keep == 3, "the number to keep was saved (%d)", st2.cfg.history_keep);
    CHECK(history_entries(&w, g_alex_id) == 3 && history_entries(&w, g_maya_id) == 3);
    char cur[FSX_PATH], lp[FSX_PATH], *log = NULL;
    snprintf(cur, sizeof cur, "%s/profiles/%s/current", w.fb.sd, g_alex_id);
    CHECK_MSG(count_dir(cur) == 2, "Alex's current saves were not touched (%zu games)", count_dir(cur));
    store_session_log(&st2, lp, sizeof lp);
    CHECK(fsx_read_all(lp, &log, NULL) == 0 && log && strstr(log, "earlier saves trimmed"));
    free(log);
    w_done(&w);
    g_w = NULL;
}

// Storage with fifty profiles, four of them with saves of any size and the rest with a few bytes: the card's bar has more parts than it can hold (the
// smallest people share its last one) and the "others" slip's bar is far smaller than a part can be (it is a minimum stub, not nothing).
static void storage_many_check(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_storage_parts == 40 && g_ui_storage_who[39] == -2, "fifty people fold into forty parts, the last one for the smallest (%d parts, last is %d)", g_ui_storage_parts,
              g_ui_storage_parts > 0 ? g_ui_storage_who[g_ui_storage_parts - 1] : 0);
    float tot = 2.0f * (g_ui_storage_parts > 0 ? g_ui_storage_parts - 1 : 0);
    int thin = 0;
    for (int i = 0; i < g_ui_storage_parts; i++) {
        tot += g_ui_storage_w[i];
        thin += g_ui_storage_w[i] < 1.99f;
    }
    CHECK_MSG(thin == 0 && fabsf(tot - ST_BW_CARD) < 1.0f, "every part is at least 2 px and together they are the bar (%d thin, %.1f of %d)", thin, (double)tot, ST_BW_CARD);
    CHECK_MSG(g_ui_storage_row_w[4] >= 7.99f && g_ui_storage_row_w[4] <= 8.5f && g_ui_storage_row_min[4] > 0.0f,
              "the others' bar is a stub of 8 px made of parts that are all there, none of them negative (%.2f px, narrowest %.3f)", (double)g_ui_storage_row_w[4],
              (double)g_ui_storage_row_min[4]);
}

static void sc_storage_many(void)
{
    W w;
    w_init(&w, "45-storage-many", 0);
    g_w = &w;
    w.fb.free_bytes = 13314398618ull;
    w.fb.total_bytes = 31997506355ull;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    static const size_t big[4] = {600 * 1024, 400 * 1024, 300 * 1024, 200 * 1024};
    for (int i = 0; i < 50; i++) {
        char nm[24], id[ID_MAX];
        snprintf(nm, sizeof nm, "Player %02d", i + 1);
        seed_profile(&w, &st, nm, NULL, 3600 + i * 600, 1, id);
        put_blob(&st, id, "current/0100000000010000/save.bin", i < 4 ? big[i] : 60 + (size_t)i);
    }
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_section(h, &hub, HS_GAMES);
    hub_open(h, &hub, HUB_STORAGE);
    hp_snap(h, "storage-fifty");
    hp_call(h, storage_many_check);
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

static void check_cleared(HostPlat *h)
{
    (void)h;
    Store st2;
    store_open(&st2, &g_w->fb.be, g_w->fb.sd);
    CHECK(!strcmp(st2.state.holder, UNCLAIMED) && st2.state.wiped == 1 && st2.state.phase == PH_IDLE);
    size_t files = 0, saves = 0;
    char **names;
    size_t n = 0;
    if (fsx_list(g_w->fb.committed, &names, &n) == 0) {
        for (size_t i = 0; i < n; i++) {
            char d[FSX_PATH];
            fsx_join(d, sizeof d, g_w->fb.committed, names[i]);
            saves++;
            files += count_dir(d);
        }
        fsx_list_free(names, n);
    }
    CHECK_MSG(saves > 0 && files == 0, "after Done playing the console holds %zu save(s) with %zu file(s) in them (all cleared)", saves, files);
}

// Done playing clears the console: the home screen shows a temporary Guest, and the next person loads straight in.
static unsigned char *load_pack(size_t *n);
static int have_pack(void);

static void sc_guest(void)
{
    W w;
    w_init(&w, "12-guest", 1);
    g_w = &w;
    size_t packn = 0;
    unsigned char *pack = load_pack(&packn); // (NULL without the pictures: the guest is a letter then)
    ui_set_avatar_pack(pack, packn); // the guest shows Miku's picture
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX], priyaid[ID_MAX];
    seed_profile(&w, &st, "Maya", NULL, 600, 3, mayaid);
    seed_profile(&w, &st, "Priya", NULL, 90000, 2, priyaid);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    st.state.loaded_at = (int64_t)w.now0 - 900;
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_snap(h, "maya-loaded");
    hp_btn(h, BTN_X); // Done playing
    hp_snap_after(h, 2, "signing-out-backup");
    hp_snap_after(h, 26, "signing-out-clearing");
    hp_snap(h, "guest-home");
    hp_call(h, check_cleared);
    hp_btn(h, BTN_X); // nothing to sign out
    hp_snap(h, "already-guest");
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_A); // Priya
    hp_snap(h, "confirm-from-guest");
    hp_snap_after(h, 4, "loading-from-guest");
    hp_btn(h, BTN_A);
    hp_snap(h, "ready");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, priyaid) && st2.state.wiped == 0, "Priya is loaded straight from the guest session");
    char un[FSX_PATH];
    snprintf(un, sizeof un, "%s/unclaimed", w.fb.sd);
    CHECK_MSG(count_dir(un) == 0, "nothing was kept in unclaimed/ (%zu)", count_dir(un));
    ui_set_avatar_pack(NULL, 0);
    free(pack);
    w_done(&w);
    g_w = NULL;
}

// Admin -> Console -> Test the in-game reminder: the app leaves a request for the reminder (the sysmodule, not running here) and
// waits up to 12 seconds for its answer. First nobody answers; then the "reminder" answers, as the real one does.
static int g_rt_asked, g_rt_withdrawn;

static void reminder_answers(HostPlat *h)
{
    (void)h;
    char ask[FSX_PATH], done[FSX_PATH];
    snprintf(ask, sizeof ask, "%s/reminder-test", g_w->fb.sd);
    snprintf(done, sizeof done, "%s/reminder-test.done", g_w->fb.sd);
    g_rt_asked = fsx_exists(ask);
    remove(ask);
    fsx_write_file(done, "ok", 2);
}

static void reminder_request_withdrawn(HostPlat *h)
{
    (void)h;
    char ask[FSX_PATH];
    snprintf(ask, sizeof ask, "%s/reminder-test", g_w->fb.sd);
    g_rt_withdrawn = !fsx_exists(ask);
}

// the repeating test: a file the reminder looks for, there while it is on
static int g_rt_on_seen, g_rt_off_seen;

static void repeat_flag_seen_on(HostPlat *h)
{
    (void)h;
    char p[FSX_PATH];
    snprintf(p, sizeof p, "%s/reminder-repeat", g_w->fb.sd);
    g_rt_on_seen = fsx_exists(p);
}

static void repeat_flag_seen_off(HostPlat *h)
{
    (void)h;
    char p[FSX_PATH];
    snprintf(p, sizeof p, "%s/reminder-repeat", g_w->fb.sd);
    g_rt_off_seen = !fsx_exists(p);
}

static void sc_reminder_test(void)
{
    W w;
    w_init(&w, "28-reminder-test", 1);
    g_w = &w;
    g_rt_asked = g_rt_withdrawn = g_rt_on_seen = g_rt_off_seen = 0;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 3, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_open(h, &hub, HUB_REMINDER_TEST);
    hp_snap(h, "which-test");
    hp_btn(h, BTN_A); // a test note now
    hp_wait(h, 24);   // nobody answers
    hp_snap(h, "not-running");
    hp_call(h, reminder_request_withdrawn);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // again ...
    hp_btn(h, BTN_A); // ... a test note now: this time it answers
    hp_call(h, reminder_answers);
    hp_wait(h, 1);
    hp_snap(h, "sent");
    hp_btn(h, BTN_A); // closes "Test reminder sent"
    hp_btn(h, BTN_A); // the test menu again
    hp_btn(h, BTN_DOWN); // the switch
    hp_snap(h, "switch-off");
    hp_btn(h, BTN_A); // switched on: a file the reminder looks for (nobody's saves are loaded: it does not matter), and the menu stays open
    hp_snap(h, "switch-on");
    hp_call(h, repeat_flag_seen_on);
    hp_btn(h, BTN_B);
    hp_snap(h, "hub-says-repeating");
    hp_btn(h, BTN_A); // the menu again: the switch is on, as the card says
    hp_btn(h, BTN_DOWN);
    hp_snap(h, "switch-still-on");
    hp_btn(h, BTN_A); // switched off
    hp_snap(h, "switch-off-again");
    hp_call(h, repeat_flag_seen_off);
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_rt_withdrawn, "when nobody answers, the request is taken back (it won't go off later)");
    CHECK_MSG(g_rt_asked, "the app asked the reminder for a test");
    char done[FSX_PATH];
    snprintf(done, sizeof done, "%s/reminder-test.done", w.fb.sd);
    CHECK_MSG(!fsx_exists(done), "and tidied the answer away");
    CHECK_MSG(g_rt_on_seen, "turning the repeating test on left its file for the reminder");
    CHECK_MSG(g_rt_off_seen, "and turning it off took it away");
    w_done(&w);
    g_w = NULL;
}

// The old "the real one, in a minute" test is gone (the repeating test replaced it). A console that was left in the middle of one has the reminder at one
// minute and the usual setting kept in remind_restore: the app puts the usual setting back when it opens, and leaves whoever is loaded, and their clock, alone.
static void sc_reminder_soon(void)
{
    W w;
    reminder_world(&w, "30-reminder-soon", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    st.cfg.remind_minutes = 1;
    st.cfg.remind_restore = 180;
    CHECK(store_save_config(&st) == 0);
    HostPlat *h = &w.hp;
    hp_snap(h, "home-with-a-leftover-test");
    CHECK(run(&w) == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(st2.cfg.remind_minutes == 180 && st2.cfg.remind_restore == -1, "three hours again (%d, %d)", st2.cfg.remind_minutes, st2.cfg.remind_restore);
    CHECK_MSG(strcmp(st2.state.holder, UNCLAIMED) != 0 && loaded_ago(&w) >= 3500, "Maya is still loaded, her clock as it was (%lld s)", (long long)loaded_ago(&w));
    w_done(&w);
    g_w = NULL;
}
// Admin -> Games and saves -> Unclaimed saves: what the console held before anyone loaded with Washi, and what was played
// without signing in. One is given to Maya (hers now; what she had is in her history), the other is deleted.
static char g_uc_maya[ID_MAX];

static void put_unclaimed(Store *st, const char *when, uint64_t title, const char *rel, const char *text)
{
    char tdir[17], p[FSX_PATH], f[FSX_PATH], parent[FSX_PATH];
    store_title_dir(title, tdir);
    snprintf(p, sizeof p, "unclaimed/%s/%s/%s", when, tdir, rel);
    store_path(st, f, sizeof f, p);
    snprintf(parent, sizeof parent, "%s", f);
    *strrchr(parent, '/') = 0;
    fsx_mkdir_p(parent);
    fsx_write_file(f, text, strlen(text));
}

static int file_says(const char *dir, const char *rel, const char *text)
{
    char f[FSX_PATH], *got = NULL;
    snprintf(f, sizeof f, "%s/%s", dir, rel);
    int same = fsx_read_all(f, &got, NULL) == 0 && got && !strcmp(got, text);
    free(got);
    return same;
}

static size_t unclaimed_left(void)
{
    char d[FSX_PATH];
    snprintf(d, sizeof d, "%s/unclaimed", g_w->fb.sd);
    return count_dir(d);
}

static void check_given(HostPlat *h)
{
    (void)h;
    char cur[FSX_PATH], hist[FSX_PATH], mk[17], splat[17];
    store_title_dir(T_MK, mk);
    store_title_dir(T_SPLAT, splat);
    snprintf(cur, sizeof cur, "%s/profiles/%s/current", g_w->fb.sd, g_uc_maya);
    snprintf(hist, sizeof hist, "%s/profiles/%s/history", g_w->fb.sd, g_uc_maya);
    char rel[64];
    snprintf(rel, sizeof rel, "%s/profile.dat", mk);
    CHECK_MSG(file_says(cur, rel, "club: mario kart"), "the club's Mario Kart save is Maya's now");
    CHECK_MSG(count_dir(cur) == 4, "with the other three games that were with it (%zu)", count_dir(cur));
    snprintf(rel, sizeof rel, "%s/save.dat", mk);
    CHECK_MSG(!file_says(cur, rel, "Maya's save for title 0"), "her own Mario Kart save isn't mixed into it");
    CHECK_MSG(count_dir(hist) == 1, "what she had is in her history, to roll back to (%zu)", count_dir(hist));
    CHECK_MSG(unclaimed_left() == 1, "and one is left unclaimed (%zu)", unclaimed_left());
}

static void check_one_unclaimed(HostPlat *h)
{
    (void)h;
    CHECK_MSG(unclaimed_left() == 1, "cancelling the delete leaves it (%zu)", unclaimed_left());
}

static void sc_unclaimed(void)
{
    W w;
    w_init(&w, "29-unclaimed-saves", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char alexid[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, alexid);
    seed_profile(&w, &st, "Maya", NULL, 86400 * 2, 1, g_uc_maya);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", alexid);
    st.state.loaded_at = (int64_t)w.now0 - 1500;
    store_save_state(&st);
    put_unclaimed(&st, "20260928-190000", T_MK, "profile.dat", "club: mario kart");
    put_unclaimed(&st, "20260928-190000", T_SSBU, "small.dat", "club: smash");
    put_unclaimed(&st, "20260928-190000", T_SPLAT, "save.dat", "club: splatoon");
    put_unclaimed(&st, "20260928-190000", T_RL, "rl.sav", "club: rocket league");
    put_unclaimed(&st, "20261001-211500", T_SPLAT, "save.dat", "someone played splatoon");

    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_section(h, &hub, HS_GAMES);
    hp_snap(h, "games-and-saves");
    hub_open(h, &hub, HUB_UNCLAIMED);
    hp_snap(h, "unclaimed-saves");
    hp_btn(h, BTN_DOWN); // the older one: the club's four games
    hp_btn(h, BTN_A);
    hp_snap(h, "give-or-delete");
    hp_btn(h, BTN_A); // Give to a profile
    hp_snap(h, "give-to");
    hp_btn(h, BTN_A); // Alex: his saves are loaded
    hp_snap(h, "alex-is-loaded");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_DOWN); // Maya
    hp_btn(h, BTN_A);
    hp_snap(h, "give-confirm");
    hp_btn(h, BTN_A); // Give
    hp_snap(h, "given-to-maya");
    hp_call(h, check_given);
    hp_btn(h, BTN_A);
    hp_snap(h, "one-left");
    hp_btn(h, BTN_A); // the one that is left
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // Delete
    hp_snap(h, "delete-confirm");
    hp_btn(h, BTN_B); // cancel
    hp_call(h, check_one_unclaimed);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // Delete
    hp_snap(h, "nothing-unclaimed");
    hp_btn(h, BTN_A); // nothing there: nothing happens
    hp_btn(h, BTN_B);
    hp_snap(h, "hub-none");
    hub_leave(h, &hub);
    CHECK(run(&w) == 0);
    CHECK_MSG(unclaimed_left() == 0, "nothing is unclaimed any more (%zu)", unclaimed_left());
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, alexid), "Alex is still the one loaded");
    char acur[FSX_PATH];
    snprintf(acur, sizeof acur, "%s/profiles/%s/current", w.fb.sd, alexid);
    CHECK_MSG(count_dir(acur) == 3, "and his backup wasn't touched (%zu)", count_dir(acur));
    w_done(&w);
    g_w = NULL;
}

// Admin -> Games and saves -> Back up everything: a verified copy of Washi's folder in "Washi Backups", named by the clock.
static void check_backup(HostPlat *h)
{
    (void)h;
    char parent[FSX_PATH], **names;
    size_t n = 0;
    snprintf(parent, sizeof parent, "%s/../Washi Backups", g_w->fb.sd);
    CHECK_MSG(fsx_list(parent, &names, &n) == 0 && n == 1, "one backup folder was made (%zu)", n);
    if (n != 1) return;
    CHECK_MSG(strlen(names[0]) == 17 && names[0][2] == '-' && names[0][5] == '-' && names[0][10] == '-' && names[0][11] == '-' &&
                  names[0][14] == '-',
              "named mm-dd-yyyy--hh-mm (%s)", names[0]);
    char dir[FSX_PATH], prof[FSX_PATH], part[FSX_PATH];
    snprintf(dir, sizeof dir, "%.300s/%.100s", parent, names[0]);
    snprintf(prof, sizeof prof, "%.400s/profiles", dir);
    snprintf(part, sizeof part, "%.400s.partial", dir);
    CHECK_MSG(fsx_is_dir(prof) && count_dir(prof) == 2, "both profiles are in it (%zu)", count_dir(prof));
    CHECK_MSG(!fsx_exists(part), "and no half-made folder is left");
    fsx_list_free(names, n);
}

extern int g_ui_backups_focus_x;

// (the Backups page with two tiles: nothing can slide, so after a pull the frame is still on Back up now, in the left slot)
static void check_frame_stayed(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_backups_focus_x > 150 && g_ui_backups_focus_x < 230, "a pull on a row too short to slide changes nothing (the lifted tile at %d)", g_ui_backups_focus_x);
}

static void sc_backup(void)
{
    W w;
    w_init(&w, "30-backup", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char alexid[ID_MAX], mayaid[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, alexid);
    seed_profile(&w, &st, "Maya", NULL, 86400 * 2, 1, mayaid);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", alexid);
    st.state.loaded_at = (int64_t)w.now0 - 1500;
    store_save_state(&st);

    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_section(h, &hub, HS_GAMES);
    hp_snap(h, "backup-row");
    hub_open(h, &hub, HUB_BACKUP); // the page of backups: only "Back up now"
    hp_snap(h, "backups-none");
    hp_btn(h, BTN_A);
    hp_snap(h, "backup-confirm");
    hp_btn(h, BTN_B); // cancel: nothing is made
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // Back up
    hp_snap(h, "backed-up");
    hp_btn(h, BTN_A);
    hp_snap(h, "backups-one");
    hp_call(h, check_backup);
    hp_snap_after(h, 5, "pulled");
    hp_drag(h, 700, 280, 420, 280, 6); // a finger pulls the two tiles sideways: they give a little and go back
    hp_call(h, check_frame_stayed);
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_A); // open it
    hp_snap(h, "restore-or-delete");
    hp_btn(h, BTN_A); // Restore: Alex is loaded
    hp_snap(h, "sign-out-first");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B); // out of the page
    hub_leave(h, &hub);
    hp_btn(h, BTN_X); // sign out: a backup is made by itself
    CHECK(run(&w) == 0);
    char parent[FSX_PATH];
    snprintf(parent, sizeof parent, "%s/../Washi Backups", w.fb.sd);
    CHECK_MSG(count_dir(parent) == 2, "signing out made a second backup (%zu)", count_dir(parent));
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Locker lk2;
    lk_init(&lk2, &w.fb.be, &st2);
    LkSnapshot *v = NULL;
    size_t nv = 0;
    CHECK(lk_snapshot_list(&lk2, parent, &v, &nv) == LK_OK && nv == 2);
    int autos = 0;
    for (size_t i = 0; i < nv; i++) autos += v[i].automatic;
    CHECK_MSG(autos == 1, "one of them is the automatic one (%d)", autos);
    free(v);
    CHECK_MSG(!strcmp(st2.state.holder, UNCLAIMED), "and Alex is signed out");
    lk_free(&lk2);
    w_done(&w);
    g_w = NULL;
}

// The Backups page with enough of them to slide: "Back up now", pinned tiles in a row that slides (L and R one tile, ZL and ZR
// two), and automatic rows under it. What each button does; how it all moves is backups-motion's business.
static void check_backups_12(HostPlat *h)
{
    (void)h;
    char parent[FSX_PATH];
    snprintf(parent, sizeof parent, "%s/../Washi Backups", g_w->fb.sd);
    CHECK_MSG(count_dir(parent) == 12, "all twelve backups are there (%zu)", count_dir(parent));
}

static void check_backups_11(HostPlat *h)
{
    (void)h;
    char parent[FSX_PATH];
    snprintf(parent, sizeof parent, "%s/../Washi Backups", g_w->fb.sd);
    CHECK_MSG(count_dir(parent) == 11, "X deleted one backup (%zu left)", count_dir(parent));
}

static void sc_backups_many(void)
{
    W w;
    w_init(&w, "31-backups-many", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char alexid[ID_MAX], mayaid[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, alexid);
    seed_profile(&w, &st, "Maya", NULL, 86400 * 2, 1, mayaid);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", UNCLAIMED);
    store_save_state(&st);
    Locker lk;
    lk_init(&lk, &w.fb.be, &st);
    char parent[FSX_PATH], out[FSX_PATH];
    snprintf(parent, sizeof parent, "%s/../Washi Backups", w.fb.sd);
    static const char *const manual[] = {"09-02-2026--20-10", "09-10-2026--19-05", "09-20-2026--21-05", "09-28-2026--21-12",
                                         "10-01-2026--18-30", "10-02-2026--19-45", "10-02-2026--20-30"};
    static const char *const autos[] = {"09-30-2026--16-20", "10-01-2026--17-10", "10-02-2026--18-02", "10-03-2026--19-40", "10-04-2026--17-15"};
    static const char *const who[] = {"Alex", "Maya", "Alex", "Maya", "Alex"};
    for (size_t i = 0; i < 7; i++) CHECK(lk_snapshot(&lk, parent, manual[i], out, sizeof out) == LK_OK);
    for (size_t i = 0; i < 5; i++) CHECK(lk_snapshot_auto(&lk, parent, autos[i], who[i], 9, out, sizeof out) == LK_OK);
    lk_free(&lk);

    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_section(h, &hub, HS_GAMES);
    hub_open(h, &hub, HUB_BACKUP);
    hp_snap(h, "top");
    hp_btn_rep(h, BTN_UP); // nowhere (held): leans
    hp_snap_after(h, 3, "leaning-left");
    hp_btn(h, BTN_LEFT); // nowhere: leans
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_RIGHT);
    hp_snap(h, "second-pinned");
    hp_btn(h, BTN_R); // one on: the row slides, the frame keeps to the middle
    hp_btn(h, BTN_R);
    hp_snap(h, "fourth-pinned");
    hp_btn(h, BTN_ZR); // two on
    hp_snap(h, "sixth-pinned");
    hp_btn(h, BTN_ZR); // (only one is left that way) the last one: the row is at its end, the frame in the right slot
    hp_snap(h, "last-pinned");
    hp_btn(h, BTN_RIGHT); // nowhere: leans
    hp_btn(h, BTN_ZL); // two back
    hp_snap(h, "two-back");
    hp_btn(h, BTN_ZL);
    hp_btn(h, BTN_ZL);
    hp_btn(h, BTN_ZL); // (one left that way) Back up now, in the left slot
    hp_snap(h, "back-up-now");
    hp_btn(h, BTN_ZL); // nowhere: leans
    hp_btn(h, BTN_R);
    hp_btn(h, BTN_DOWN); // the first automatic one
    hp_snap(h, "first-auto");
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_DOWN);
    hp_snap(h, "last-auto");
    hp_btn_rep(h, BTN_DOWN); // nowhere (held): leans
    hp_btn(h, BTN_R); // from the rows: back up to the tiles, one on from where it was
    hp_snap(h, "back-to-tiles");
    hp_btn(h, BTN_Y); // straight to Back up now
    hp_snap(h, "y-to-back-up-now");
    hp_btn(h, BTN_X); // (nothing to delete there)
    hp_call(h, check_backups_12);
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_X);
    hp_snap(h, "delete-confirm");
    hp_btn(h, BTN_B); // cancel
    hp_call(h, check_backups_12);
    hp_btn(h, BTN_X);
    hp_btn(h, BTN_A); // Delete
    hp_snap(h, "deleted-one");
    hp_call(h, check_backups_11);
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

// ---- How the Backups row moves, measured on the frames themselves. Every pinned tile has an orange star in its corner: where the
// stars are says how far the row has slid, to the pixel. The checks are of the motion's nature (off at once, never backwards, lands
// on a tile, stays under the finger, keeps the finger's speed), not of where a tuned curve happens to be at some frame.
#define BK_PITCH 310.0f // from one tile to the next
#define BK_STAR_X 442.0f // a star's centre when its tile is in the left slot
#define BK_STAR_Y 214
#define BK_FRAMES 256

static struct {
    float row[BK_FRAMES];    // how far the row has slid in each frame (pixels; 0 = as the page opens)
    float finger[BK_FRAMES]; // where the finger is (x, or y while the page is watched), NAN when it is up
    float page[BK_FRAMES];   // how far the page has scrolled (pixels), when that is being watched
    int ring[BK_FRAMES];     // the focus frame's left edge
    uint64_t t[BK_FRAMES];
    int n, lost, watch_page;
    int dir;                 // which way the row is known to be going (1, -1), when it may move more than half a tile in a frame
    int star_w;              // how wide a star is, in columns of orange
    float base;              // the row's place when the watching began
    uint32_t blue;           // the colour of the first picture in the automatic rows (it marks where the rows are)
} g_bk;

// The centre of a star between the row's faded ends, or -1. The focus frame is drawn over the tiles, so as a star slides under one
// of the frame's sides (`ring` = the frame's left edge) part of it is covered: then its other side, the one further from the
// frame's lines, says where it is. *width: how wide the widest star seen is.
static float bk_star(const Surf *f, int ring, int star_w, int *width)
{
    float best = -1, best_clear = -1, line_l = ring < 0 ? -1000.0f : (float)ring + 3, line_r = line_l + 280;
    int first = -1, last = -1;
    for (int x = 196; x < 1098; x++) {
        int hit = 0;
        for (int y = BK_STAR_Y - 8; y <= BK_STAR_Y + 8 && !hit; y++) hit = px_near(f, x, y, 0xF2A900, 40);
        if (hit) {
            if (first < 0) first = x;
            last = x;
        } else if (first >= 0 && x - last > 3) {
            if (first > 198 && last < 1094) {
                float c = (float)(first + last) / 2.0f, clear = 1000;
                if (width && last - first + 1 > *width) *width = last - first + 1;
                if (star_w && last - first + 1 < star_w) {
                    float cl = fminf(fabsf((float)first - line_l), fabsf((float)first - line_r));
                    float cr = fminf(fabsf((float)last - line_l), fabsf((float)last - line_r));
                    c = cl > cr ? (float)first + (float)(star_w - 1) / 2.0f : (float)last - (float)(star_w - 1) / 2.0f;
                    clear = fmaxf(cl, cr);
                }
                if (clear > best_clear) best = c, best_clear = clear;
            }
            first = last = -1;
        }
    }
    return best;
}

// how far the page has scrolled: the automatic rows' first pictures are squares of one colour down a column, 74 px apart, the
// first centred on y = 479 when the page is at its top
static float bk_page(const Surf *f, float guess)
{
    int first = -1;
    for (int y = 190; y < 622; y++) {
        int hit = px_near(f, 251, y, g_bk.blue, 12); // (left of the first round picture's middle, clear of its letter)
        if (hit && first < 0) first = y;
        if (!hit && first >= 0) {
            if (y - first >= 24 && first > 190) { // a whole square (30 px tall, not one cut by the page's edge)
                float frac = 479.0f - (float)(first + y - 1) / 2.0f; // = scroll - 74 * (which row)
                return frac + 74.0f * roundf((guess - frac) / 74.0f);
            }
            first = -1;
        }
    }
    return NAN;
}

static void bk_frame(HostPlat *h, const Surf *f)
{
    int k = g_bk.n;
    if (k >= BK_FRAMES) return;
    // A star's place gives the row's up to a whole number of tiles: the one nearest to where the row was heading is the one
    // (or, when the row is known to be going one way fast, the first one that way from where it was, give or take its settling).
    g_bk.ring[k] = g_ui_backups_focus_x; // (the lifted tile's left edge: Washi has no frame, the tile in focus is the one that is up)
    float guess = k == 0 ? g_bk.base : k == 1 ? g_bk.row[0] : 2 * g_bk.row[k - 1] - g_bk.row[k - 2];
    float sx = bk_star(f, g_bk.ring[k], g_bk.star_w, k == 0 ? &g_bk.star_w : NULL);
    if (sx >= 0) {
        float frac = BK_STAR_X - sx, prev = k ? g_bk.row[k - 1] : g_bk.base;
        if (g_bk.dir > 0) g_bk.row[k] = frac + BK_PITCH * ceilf((prev - 24 - frac) / BK_PITCH);
        else if (g_bk.dir < 0) g_bk.row[k] = frac + BK_PITCH * floorf((prev + 24 - frac) / BK_PITCH);
        else g_bk.row[k] = frac + BK_PITCH * roundf((guess - frac) / BK_PITCH);
    } else {
        g_bk.row[k] = guess;
        g_bk.lost++;
    }
    g_bk.finger[k] = h->touch_is_down ? (g_bk.watch_page ? h->touch_y : h->touch_x) : NAN;
    g_bk.page[k] = g_bk.watch_page ? bk_page(f, k ? g_bk.page[k - 1] : 0) : 0;
    g_bk.t[k] = h->t_ms;
    g_bk.n++;
}

// start watching: the frame on screen now is the first, as the press or the finger finds it
static void bk_watch(HostPlat *h)
{
    g_bk.n = g_bk.lost = g_bk.watch_page = g_bk.dir = g_bk.star_w = 0;
    bk_frame(h, h->last);
    h->ntrace = 0;
    h->trace_on = 1;
    h->trace_png = getenv("TRACE_PNG") != NULL; // TRACE_PNG=1 ./app_host backups-motion  keeps every frame
    h->frame_hook = bk_frame;
}

// (for a jump of two tiles: the row may cover more than half a tile in a frame, and it is known which way it is going)
static void bk_watch_on(HostPlat *h)
{
    bk_watch(h);
    g_bk.dir = 1;
}

static void bk_watch_back(HostPlat *h)
{
    bk_watch(h);
    g_bk.dir = -1;
}

static void bk_watch_page(HostPlat *h)
{
    const uint8_t *q = h->last->px + ((size_t)479 * SCREEN_W + 251) * 4;
    g_bk.blue = (uint32_t)q[0] << 16 | (uint32_t)q[1] << 8 | q[2];
    bk_watch(h);
    g_bk.watch_page = 1;
    g_bk.page[0] = bk_page(h->last, 0);
}

static void bk_stop(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    if (g_bk.n) g_bk.base = g_bk.row[g_bk.n - 1];
    if (getenv("BK_DEBUG")) {
        printf("  row:");
        for (int i = 0; i < g_bk.n; i++) printf(" %.0f", g_bk.row[i] - g_bk.row[0]);
        printf("\n  ms:");
        for (int i = 1; i < g_bk.n; i++) printf(" %d", (int)(g_bk.t[i] - g_bk.t[i - 1]));
        printf("\n  finger:");
        for (int i = 0; i < g_bk.n; i++) printf(" %.0f", g_bk.finger[i]);
        printf("\n");
    }
}

static void bk_home(HostPlat *h)
{
    (void)h;
    g_bk.base = 0; // (Y took the row back to its start)
}

// The frame never comes off the tiles: in every frame it shows in, it is where a tile is (it goes with the row), never between two.
static void bk_frame_on_tile(const char *what)
{
    int worst = 0, seen = 0;
    for (int k = 0; k < g_bk.n; k++) {
        if (g_bk.ring[k] < 0) continue;
        float off = (float)(g_bk.ring[k] - g_bk.ring[0]) + g_bk.row[k] - g_bk.row[0]; // a whole number of tiles, if it is on one
        int d = (int)lroundf(fabsf(off - BK_PITCH * roundf(off / BK_PITCH)));
        if (d > worst) worst = d;
        seen++;
    }
    CHECK_MSG(g_bk.ring[0] >= 0 && seen >= 3 && worst <= 2, "%s: the frame is always on a tile, never between two (%d px off at most, seen in %d frames)", what, worst, seen);
}

// A press moved the row `tiles` on: off in the first frame, never backwards, never past by more than a hair, there within a fifth
// of a second, with the frame on a tile all the way and in the middle slot at the end. A shoulder button's press (`bouncy`) runs
// past on purpose: one swing you can see (but no more than a sixth of a tile a tile), back, and still within a third of a second.
static void bk_pressed_as(HostPlat *h, const char *what, int tiles, int bouncy)
{
    bk_stop(h);
    int n = g_bk.n, there = -1, s = 0;
    for (int k = 0; k < n; k++)
        if (!isnan(g_bk.finger[k])) s = k; // (a tap: it acts when the finger lifts)
    float x0 = g_bk.row[0], want = (float)tiles * BK_PITCH, dir = tiles > 0 ? 1.0f : -1.0f, back = 0, past = 0;
    CHECK_MSG(n - s >= 5 && !g_bk.lost, "%s: the row was seen in every frame (%d frames, %d without a star)", what, n, g_bk.lost);
    if (n - s < 5) return;
    for (int k = 1; k < n; k++) {
        float d = (g_bk.row[k] - g_bk.row[k - 1]) * dir, over = (g_bk.row[k] - x0 - want) * dir;
        if (there < 0 && -d > back) back = -d;
        if (over > past) past = over;
        if (there < 0 && (fabsf(g_bk.row[k] - x0 - want) < 3.0f || (bouncy && over > 0))) there = k; // (a bounce may step over it)
    }
    int ms = there > 0 ? (int)(g_bk.t[there] - g_bk.t[s]) : 9999;
    float first = (g_bk.row[s + 1] - x0) * dir;
    printf("  %-34s %2d frames, there after %3d ms, first frame %3.0f px, %.1f px past at most\n", what, n - s, ms, first, past);
    CHECK_MSG(fabsf(g_bk.row[n - 1] - x0 - want) < 0.6f, "%s: the row came to rest %d tiles on (%.1f px)", what, tiles, g_bk.row[n - 1] - x0);
    CHECK_MSG(first >= 0.08f * fabsf(want), "%s: it is off in the very first frame (%.0f px)", what, first);
    CHECK_MSG(back <= 1.0f, "%s: it never goes backwards on the way (%.1f px)", what, back);
    if (!bouncy) {
        CHECK_MSG(past <= 2.5f, "%s: no bounce to see at the end (%.1f px past)", what, past);
    } else {
        // (how many times it crosses where it stops, more than a pixel and a half either side: past it and back is two)
        int swings = 0, side = -1;
        uint64_t still = g_bk.t[s];
        for (int k = s + 1; k < n; k++) {
            float off = (g_bk.row[k] - x0 - want) * dir;
            if (off > 1.5f && side < 0) side = 1, swings++;
            else if (off < -1.5f && side > 0) side = -1, swings++;
            if (fabsf(off) > 1.0f) still = g_bk.t[k];
        }
        int settle = (int)(still - g_bk.t[s]);
        printf("  %-34s runs %.0f px past, crosses %d times, still after %d ms\n", "", past, swings, settle);
        CHECK_MSG(past >= 8.0f && past <= fabsf(want) / 6.0f, "%s: it swings past its tile, so you can see it (%.0f px past)", what, past);
        CHECK_MSG(swings <= 2, "%s: once, and back: no wobble (%d crossings)", what, swings);
        CHECK_MSG(settle <= 340, "%s: and it is still within a third of a second (%d ms)", what, settle);
    }
    CHECK_MSG(ms <= 200, "%s: it is there within a fifth of a second (%d ms)", what, ms);
    bk_frame_on_tile(what);
    CHECK_MSG(g_bk.ring[n - 1] > 400 && g_bk.ring[n - 1] < 600, "%s: the frame ends up in the middle slot (%d)", what, g_bk.ring[n - 1]);
}

static void bk_pressed(HostPlat *h, const char *what, int tiles) { bk_pressed_as(h, what, tiles, 0); }
static void bk_right1(HostPlat *h) { bk_pressed_as(h, "R, one tile", 1, 1); }
static void bk_right2(HostPlat *h) { bk_pressed_as(h, "ZR, two tiles", 2, 1); }
static void bk_left2(HostPlat *h) { bk_pressed_as(h, "ZL, two tiles back", -2, 1); }
static void bk_btn_next(HostPlat *h) { bk_pressed(h, "the round > button", 1); }
static void bk_peek(HostPlat *h) { bk_pressed(h, "a tap on the tile peeking in", 2); }

// Right, then Left three frames later: the row is on its way, slows, turns and comes back, all in one motion
static void bk_bent(HostPlat *h)
{
    bk_stop(h);
    int n = g_bk.n, turns = 0, peak_at = 0;
    float x0 = g_bk.row[0], peak = 0, jump = 0, prev = 0;
    for (int k = 1; k < n; k++) {
        float d = g_bk.row[k] - g_bk.row[k - 1];
        if (g_bk.row[k] - x0 > peak) peak = g_bk.row[k] - x0, peak_at = k;
        if (fabsf(d - prev) > jump) jump = fabsf(d - prev);
        if (prev > 1.0f && d < -1.0f) turns++;
        if (prev < -1.0f && d > 1.0f) turns++;
        prev = d;
    }
    printf("  %-34s %2d frames, %.0f px out (frame %d) and back, speed changes by %.0f px a frame at most\n", "Right, then Left mid-slide", n, peak, peak_at, jump);
    CHECK_MSG(fabsf(g_bk.row[n - 1] - x0) < 0.6f, "a press the other way mid-slide brings the row back where it was (%.1f px)", g_bk.row[n - 1] - x0);
    CHECK_MSG(peak > 60.0f && peak < 300.0f, "it was well on its way when it turned (%.0f px)", peak);
    CHECK_MSG(peak_at >= 3, "and it ran on a little after the second press before turning (the turn is at frame %d)", peak_at);
    CHECK_MSG(turns <= 1, "it turns once, without a wobble (%d)", turns);
    // (the row's spring is quick: turning round takes it a frame or two, about the jolt of setting off from rest)
    CHECK_MSG(jump < 1.4f * (g_bk.row[1] - x0), "and turning is no more of a jolt than setting off was (%.0f px a frame, against %.0f)", jump, g_bk.row[1] - x0);
}

// The D-pad held: seven more steps, one every five frames. They run together at one even speed.
static void bk_held(HostPlat *h)
{
    bk_stop(h);
    int n = g_bk.n, m = 0;
    float x0 = g_bk.row[0], lo = 1e9f, hi = 0, past = 0, back = 0;
    for (int k = 1; k < n; k++) {
        float d = g_bk.row[k] - g_bk.row[k - 1], at = g_bk.row[k] - x0, v = d * 1000.0f / (float)(g_bk.t[k] - g_bk.t[k - 1]);
        if (-d > back) back = -d;
        if (at - 8 * BK_PITCH > past) past = at - 8 * BK_PITCH;
        if (at > 3.0f * BK_PITCH && at < 6.5f * BK_PITCH) { // (once it is running, and before the last step)
            if (v < lo) lo = v;
            if (v > hi) hi = v;
            m++;
        }
    }
    printf("  %-34s %2d frames, running at %.0f to %.0f px a second\n", "Right held, eight tiles", n, lo, hi);
    CHECK_MSG(fabsf(g_bk.row[n - 1] - x0 - 8 * BK_PITCH) < 0.6f, "held for eight tiles the row is eight tiles on (%.1f px)", g_bk.row[n - 1] - x0);
    CHECK_MSG(m >= 8 && hi <= lo * 1.2f, "its steps run together at an even speed (%.0f to %.0f px a second over %d frames)", lo, hi, m);
    CHECK_MSG(back <= 1.0f && past <= 2.5f, "and it stops on its tile without going back or past (%.1f, %.1f px)", back, past);
}

// A finger on the row: from the moment it has hold (14 px in), the row is where the finger has taken it, to the pixel.
// Returns the last frame the finger was down in, -1 if it never was.
static int bk_follow(const char *what)
{
    int n = g_bk.n, down = -1, up = -1, held = 0;
    float worst = 0;
    for (int k = 0; k < n; k++) {
        if (isnan(g_bk.finger[k])) continue;
        if (down < 0) down = k;
        up = k;
        float moved = g_bk.finger[k] - g_bk.finger[down], want = g_bk.row[down];
        if (fabsf(moved) > 14.0f) {
            want -= moved - (moved > 0 ? 14.0f : -14.0f);
            held++;
        }
        if (fabsf(g_bk.row[k] - want) > worst) worst = fabsf(g_bk.row[k] - want);
    }
    CHECK_MSG(down >= 0 && held >= 3, "%s: the finger had hold of the row (%d frames)", what, held);
    CHECK_MSG(worst <= 1.5f, "%s: the row stays under the finger (%.1f px off at most)", what, worst);
    bk_frame_on_tile(what);
    return up;
}

// what the row did after the finger let go, in frame `up`: its speed then against the finger's, whether it only slowed, where it rests
static void bk_let_go(const char *what, int up, float *finger_v, float *first_v, float *faster, float *back)
{
    int n = g_bk.n;
    // (the finger's speed over its last three frames)
    int a = up - 3 < 0 ? 0 : up - 3;
    while (a < up && isnan(g_bk.finger[a])) a++;
    *finger_v = up > a ? -(g_bk.finger[up] - g_bk.finger[a]) * 1000.0f / (float)(g_bk.t[up] - g_bk.t[a]) : 0;
    *first_v = *faster = *back = 0;
    float dir = *finger_v >= 0 ? 1.0f : -1.0f, prev = 0;
    for (int k = up + 1; k < n; k++) {
        float v = (g_bk.row[k] - g_bk.row[k - 1]) * 1000.0f / (float)(g_bk.t[k] - g_bk.t[k - 1]) * dir;
        if (k == up + 1) *first_v = v;
        else if (v - prev > *faster) *faster = v - prev;
        if (-v > *back) *back = -v;
        prev = v;
    }
    (void)what;
}

// dragged slowly, under half a tile, and let go: it follows the finger, then goes back where it was
static void bk_nudged(HostPlat *h)
{
    bk_stop(h);
    int up = bk_follow("a slow drag");
    float x0 = g_bk.row[0], x1 = g_bk.row[g_bk.n - 1];
    CHECK_MSG(up > 0 && fabsf(g_bk.row[up] - x0) > 60.0f, "a slow drag moved the row with it (%.0f px)", up > 0 ? g_bk.row[up] - x0 : 0);
    CHECK_MSG(fabsf(x1 - x0) < 0.6f, "let go under half a tile out, it goes back where it was (%.1f px)", x1 - x0);
}

// dragged most of a tile and let go: it carries on to the next tile
static void bk_dragged(HostPlat *h)
{
    bk_stop(h);
    int up = bk_follow("a drag of most of a tile");
    float x0 = g_bk.row[0], x1 = g_bk.row[g_bk.n - 1];
    CHECK_MSG(fabsf(x1 - x0 - BK_PITCH) < 0.6f, "dragged most of a tile and let go, it settles one tile on (%.1f px)", x1 - x0);
    (void)up;
}

static float g_bk_mid; // the tile in the middle slot after the flick (it must be the one in focus)

// flicked: it keeps the finger's speed, only slows from there, and lands on a tile some tiles on
static void bk_flicked(HostPlat *h)
{
    bk_stop(h);
    int up = bk_follow("a flick"), n = g_bk.n;
    if (up < 0) return;
    float fv, v1, faster, back, x0 = g_bk.row[0], x1 = g_bk.row[n - 1], at_up = g_bk.row[up];
    bk_let_go("a flick", up, &fv, &v1, &faster, &back);
    int ms = (int)(g_bk.t[n - 1] - g_bk.t[up]);
    printf("  %-34s finger %.0f px/s, row then %.0f px/s, lands %.1f tiles on in %d ms\n", "a flick", fv, v1, (x1 - x0) / BK_PITCH, ms);
    CHECK_MSG(fv > 1500.0f, "the flick was a flick (%.0f px a second)", fv);
    CHECK_MSG(v1 > 0.7f * fv && v1 < 1.1f * fv, "let go, the row runs on at the finger's speed (%.0f against %.0f px a second)", v1, fv);
    // (a pixel more or less in a frame is 50 px a second)
    CHECK_MSG(faster < 0.04f * fv + 100.0f, "from there it only slows down (%.0f px a second faster at most, frame to frame)", faster);
    CHECK_MSG(back <= 60.0f, "and never goes back (%.0f px a second)", back);
    float tiles = (x1 - x0) / BK_PITCH;
    CHECK_MSG(fabsf(tiles - roundf(tiles)) < 0.002f && x1 - at_up > 1.5f * BK_PITCH, "it comes to rest on a tile, well past where it was let go (%.2f tiles on, %.0f px after the finger)", tiles, x1 - at_up);
    CHECK_MSG(ms < 800, "in under a second (%d ms)", ms);
    CHECK_MSG(g_bk.ring[n - 1] > 400 && g_bk.ring[n - 1] < 600, "and the frame is in the middle slot (%d)", g_bk.ring[n - 1]);
    g_bk_mid = roundf(x1 / BK_PITCH) + 1;
}

// pulled past the row's first tile: it gives, but less than the finger asks, and springs back
static void bk_pulled(HostPlat *h)
{
    bk_stop(h);
    int n = g_bk.n, down = -1, up = -1, apart = 0;
    float out = 0, finger = 0;
    for (int k = 0; k < n; k++) {
        if (!isnan(g_bk.finger[k])) {
            if (down < 0) down = k;
            up = k;
            finger = g_bk.finger[k] - g_bk.finger[down];
        }
        if (-g_bk.row[k] > out) out = -g_bk.row[k];
        if (abs(g_bk.ring[k] - g_bk.ring[0] - (int)lroundf(-g_bk.row[k])) > apart) apart = abs(g_bk.ring[k] - g_bk.ring[0] - (int)lroundf(-g_bk.row[k]));
    }
    CHECK_MSG(apart <= 2 && g_bk.ring[0] > 150 && g_bk.ring[0] < 230, "pulled past its end, the frame goes with its tile (%d px apart at most)", apart);
    printf("  %-34s finger %.0f px, the row gave %.0f px\n", "pulled past its first tile", finger, out);
    CHECK_MSG(up > 0 && finger > 300.0f, "the finger pulled a long way (%.0f px)", finger);
    CHECK_MSG(out > 60.0f && out < 0.6f * finger, "past its end the row gives, but less than it is pulled (%.0f of %.0f px)", out, finger);
    CHECK_MSG(fabsf(g_bk.row[n - 1]) < 0.6f, "and let go, it springs back to its end (%.1f px)", g_bk.row[n - 1]);
    int ms = up > 0 ? (int)(g_bk.t[n - 1] - g_bk.t[up]) : 0;
    CHECK_MSG(ms < 500, "within half a second (%d ms)", ms);
}

// a finger put on the row while it runs: it stops there and then; lifted, the row settles on the nearest tile, and nothing opens
static void bk_caught(HostPlat *h)
{
    bk_stop(h);
    int n = g_bk.n, first_up = -1, down2 = -1;
    for (int k = 1; k < n; k++) {
        if (first_up < 0 && isnan(g_bk.finger[k]) && !isnan(g_bk.finger[k - 1])) first_up = k;
        if (first_up > 0 && down2 < 0 && !isnan(g_bk.finger[k])) down2 = k;
    }
    CHECK_MSG(first_up > 0 && down2 > first_up, "a finger came down again while the row ran (frames %d, %d)", first_up, down2);
    if (down2 < 2 || down2 + 1 >= n) return;
    float before = fabsf(g_bk.row[down2 - 1] - g_bk.row[down2 - 2]), after = fabsf(g_bk.row[down2] - g_bk.row[down2 - 1]);
    float x1 = g_bk.row[n - 1], tiles = x1 / BK_PITCH, nearest = roundf(g_bk.row[down2] / BK_PITCH);
    printf("  %-34s running at %.0f px a frame, then %.0f; it rests on tile %.2f\n", "a finger on the running row", before, after, tiles);
    CHECK_MSG(before > 15.0f, "the row was running when the finger landed (%.0f px a frame)", before);
    CHECK_MSG(after <= 1.0f, "and stopped under it at once (%.1f px)", after);
    CHECK_MSG(fabsf(tiles - nearest) < 0.002f, "lifted, it settles on the nearest tile (%.2f, nearest %.0f)", tiles, nearest);
    CHECK_MSG(bk_star(h->last, -1, 0, NULL) >= 0, "and the tap chose nothing: the page is still there");
}

// the page under a finger: it follows, and let go it stays within its ends
static void bk_scrolled(HostPlat *h)
{
    bk_stop(h);
    int n = g_bk.n, down = -1, up = -1, held = 0;
    float worst = 0, most = 0;
    for (int k = 0; k < n; k++) {
        if (isnan(g_bk.page[k])) continue;
        if (g_bk.page[k] > most) most = g_bk.page[k];
        if (isnan(g_bk.finger[k])) continue;
        if (down < 0) down = k;
        up = k;
        float moved = g_bk.finger[k] - g_bk.finger[down], want = g_bk.page[down];
        if (fabsf(moved) > 14.0f) {
            want -= moved - (moved > 0 ? 14.0f : -14.0f);
            held++;
        }
        if (want >= 0 && want <= 150 && fabsf(g_bk.page[k] - want) > worst) worst = fabsf(g_bk.page[k] - want);
    }
    float end = g_bk.page[n - 1];
    printf("  %-34s %d frames under the finger, %.0f px at most, rests at %.0f px\n", "the page dragged up", held, most, end);
    CHECK_MSG(up > 0 && held >= 3 && most > 40.0f, "the finger scrolled the page (%d frames, %.0f px)", held, most);
    CHECK_MSG(worst <= 1.5f, "the page stays under the finger (%.1f px off at most)", worst);
    CHECK_MSG(!isnan(end) && end >= 0 && end <= 220.0f, "let go, it rests within its ends (%.0f px)", end);
}

static float g_bk_was;

static void bk_note(HostPlat *h)
{
    bk_watch(h);
    bk_stop(h);
    g_bk_was = g_bk.row[0];
}

// back from the box a tap opened: the row is where it was left, with the frame on the tile that was tapped (the left slot)
static void bk_same(HostPlat *h)
{
    float was = g_bk_was;
    bk_watch(h);
    bk_stop(h);
    CHECK_MSG(fabsf(g_bk.row[0] - was) < 0.6f, "back from the tile's box the row is where it was left (%.0f, was %.0f)", g_bk.row[0], was);
    CHECK_MSG(g_bk.ring[0] > 150 && g_bk.ring[0] < 230, "with the frame on the tile that was tapped, in the left slot (%d)", g_bk.ring[0]);
}

static void sc_backups_motion(void)
{
    W w;
    w_init(&w, "39-backups-motion", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char alexid[ID_MAX], mayaid[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, alexid);
    seed_profile(&w, &st, "Maya", NULL, 86400 * 2, 1, mayaid);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", UNCLAIMED);
    store_save_state(&st);
    Locker lk;
    lk_init(&lk, &w.fb.be, &st);
    char parent[FSX_PATH], out[FSX_PATH], stamp[32];
    snprintf(parent, sizeof parent, "%s/../Washi Backups", w.fb.sd);
    for (int d = 1; d <= 12; d++) { // twelve made by hand (the newest is the first tile after Back up now), five automatic
        snprintf(stamp, sizeof stamp, "09-%02d-2026--20-10", d);
        CHECK(lk_snapshot(&lk, parent, stamp, out, sizeof out) == LK_OK);
    }
    static const char *const autos[] = {"09-30-2026--16-20", "10-01-2026--17-10", "10-02-2026--18-02", "10-03-2026--19-40", "10-04-2026--17-15"};
    for (size_t i = 0; i < 5; i++) CHECK(lk_snapshot_auto(&lk, parent, autos[i], "Alex", 9, out, sizeof out) == LK_OK);
    lk_free(&lk);
    memset(&g_bk, 0, sizeof g_bk);
    g_bk_mid = -1;

    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_section(h, &hub, HS_GAMES);
    hub_open(h, &hub, HUB_BACKUP);
    hp_btn(h, BTN_R); // from Back up now to the first pinned tile: the frame steps to the middle slot, the row stays
    // by buttons
    hp_call(h, bk_watch);
    hp_btn(h, BTN_R);
    hp_call(h, bk_right1);
    hp_call(h, bk_watch_on);
    hp_btn(h, BTN_ZR);
    hp_call(h, bk_right2);
    hp_call(h, bk_watch_back);
    hp_btn(h, BTN_ZL);
    hp_call(h, bk_left2);
    hp_call(h, bk_watch);
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_LEFT); // ... and back, three frames into the slide
    hp_in(h, 3);
    hp_call(h, bk_bent);
    hp_call(h, bk_watch);
    hp_btn(h, BTN_RIGHT); // held: the press, then (0.38 s later) a step every five frames
    hp_btn_rep(h, BTN_RIGHT);
    hp_in(h, 18);
    for (int i = 0; i < 6; i++) {
        hp_btn_rep(h, BTN_RIGHT);
        hp_in(h, 5);
    }
    hp_call(h, bk_held);
    hp_snap(h, "held-to-the-tenth");
    hp_btn(h, BTN_Y); // back to Back up now
    hp_call(h, bk_home);
    hp_btn(h, BTN_R);
    hp_call(h, bk_watch);
    hp_btn(h, BTN_R);
    hp_call(h, bk_right1);
    hp_call(h, bk_watch);
    hp_btn(h, BTN_R); // the row two tiles along, the third pinned tile in the middle
    hp_call(h, bk_right1);
    // by finger
    hp_call(h, bk_watch);
    hp_drag(h, 800, 280, 700, 280, 24); // slowly, a third of a tile
    hp_call(h, bk_nudged);
    hp_call(h, bk_watch);
    hp_drag(h, 800, 280, 560, 280, 24); // most of a tile
    hp_call(h, bk_dragged);
    hp_call(h, bk_watch);
    hp_drag(h, 900, 280, 620, 280, 5); // a flick
    hp_call(h, bk_flicked);
    hp_snap(h, "flicked");
    hp_btn(h, BTN_X); // the tile in the middle is the one in focus: X asks about it ...
    hp_snap(h, "delete-the-one-in-the-middle");
    hp_btn(h, BTN_A); // ... and deletes it
    hp_btn(h, BTN_Y);
    hp_call(h, bk_home);
    hp_call(h, bk_watch);
    hp_drag(h, 400, 280, 900, 280, 8); // pulled the wrong way at the row's start
    hp_call(h, bk_pulled);
    hp_call(h, bk_watch);
    hp_drag(h, 900, 280, 620, 280, 5); // flicked again ...
    hp_tap(h, 640, 280);               // ... and a finger put on it four frames later
    hp_in(h, 4);
    hp_call(h, bk_caught);
    hp_call(h, bk_note);
    hp_tap(h, 330, 280); // a tile that shows in full (the left one): its box opens
    hp_snap(h, "tapped-a-tile");
    hp_btn(h, BTN_B);
    hp_call(h, bk_same);
    hp_call(h, bk_watch_on);
    hp_tap(h, 1250, 232); // the tile peeking in at the right edge: brought to the middle, not opened
    hp_call(h, bk_peek);
    hp_call(h, bk_watch);
    hp_tap(h, 1160, 280); // the round > button
    hp_call(h, bk_btn_next);
    hp_btn(h, BTN_Y);
    hp_call(h, bk_home);
    hp_call(h, bk_watch_page);
    hp_drag(h, 640, 560, 640, 440, 12); // the page, up by a finger
    hp_call(h, bk_scrolled);
    hp_snap(h, "page-dragged-up");
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    CHECK(run(&w) == 0);
    CHECK_MSG(count_dir(parent) == 16, "one backup was deleted (%zu left of 17)", count_dir(parent));
    int mid = (int)g_bk_mid;
    CHECK_MSG(mid >= 3 && mid <= 12, "the flick left a pinned tile in the middle (tile %d)", mid);
    if (mid >= 1 && mid <= 12) {
        snprintf(out, sizeof out, "%.400s/09-%02d-2026--20-10", parent, 13 - mid);
        CHECK_MSG(!fsx_exists(out), "and X deleted that very one: the tile in the middle is the one in focus (tile %d)", mid);
    }
    w_done(&w);
    g_w = NULL;
}

// backups-fuzz: presses, drags, flicks and catches in a random order, many of them landing mid-slide. Whenever the page comes to
// rest the row must be on a tile (a row left hanging between two never comes to rest at all, and the run never ends: run it under
// `timeout`). BK_SEED picks the order, BK_STEPS how many.
static unsigned g_fz_seed, g_fz_seed0;
static int g_fz_step, g_fz_checks, g_fz_off;

static unsigned fz_rand(void)
{
    g_fz_seed = g_fz_seed * 1103515245u + 12345u;
    return g_fz_seed >> 8;
}

static void fz_rest(HostPlat *h)
{
    const Surf *f = h->last;
    g_fz_step++;
    if (!f) return;
    int ys = -1; // the stars' row, wherever the page has scrolled it
    for (int y = 176; y < 600 && ys < 0; y++)
        for (int x = 196; x < 1098 && ys < 0; x++)
            if (px_near(f, x, y, 0xF2A900, 40)) ys = y + 8;
    if (ys < 0) return;
    int first = -1, last = -1, widest = 0, nrun = 0;
    float c[16];
    int wd[16];
    for (int x = 196; x < 1098; x++) {
        int hit = 0;
        for (int y = ys - 9; y <= ys + 9 && !hit; y++) hit = px_near(f, x, y, 0xF2A900, 40);
        if (hit) {
            if (first < 0) first = x;
            last = x;
        } else if (first >= 0 && x - last > 3) {
            if (first > 198 && nrun < 16) {
                c[nrun] = (float)(first + last) / 2.0f;
                wd[nrun++] = last - first + 1;
                if (last - first + 1 > widest) widest = last - first + 1;
            }
            first = last = -1;
        }
    }
    float worst = 0;
    for (int i = 0; i < nrun; i++) {
        if (wd[i] < widest - 1) continue; // (a star partly under the frame's line)
        float off = fmodf(fabsf(BK_STAR_X - c[i]), BK_PITCH);
        if (off > BK_PITCH / 2) off = BK_PITCH - off;
        if (off > worst) worst = off;
    }
    g_fz_checks++;
    if (worst > 2.5f) {
        g_fz_off++;
        printf("  seed %u, step %d: the row rests %.0f px off its tiles\n", g_fz_seed0, g_fz_step, worst);
        if (getenv("TRACE_PNG")) hp_save_png(h, "fuzz-off");
    }
    if (getenv("BK_DEBUG")) {
        printf("  step %d ok\n", g_fz_step);
        fflush(stdout);
    }
}

static void sc_backups_fuzz(void)
{
    W w;
    w_init(&w, "40-backups-fuzz", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char alexid[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 3600 * 3, 3, alexid);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", UNCLAIMED);
    store_save_state(&st);
    Locker lk;
    lk_init(&lk, &w.fb.be, &st);
    char parent[FSX_PATH], out[FSX_PATH], stamp[32];
    snprintf(parent, sizeof parent, "%s/../Washi Backups", w.fb.sd);
    for (int d = 1; d <= 8; d++) {
        snprintf(stamp, sizeof stamp, "09-%02d-2026--20-10", d);
        CHECK(lk_snapshot(&lk, parent, stamp, out, sizeof out) == LK_OK);
    }
    CHECK(lk_snapshot_auto(&lk, parent, "10-04-2026--17-15", "Alex", 9, out, sizeof out) == LK_OK);
    lk_free(&lk);
    const char *se = getenv("BK_SEED"), *sn = getenv("BK_STEPS");
    unsigned seed = se ? (unsigned)strtoul(se, NULL, 10) : 1;
    int steps = sn ? atoi(sn) : 300;
    g_fz_seed = g_fz_seed0 = seed;
    g_fz_step = g_fz_checks = g_fz_off = 0;

    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_section(h, &hub, HS_GAMES);
    hub_open(h, &hub, HUB_BACKUP);
    hp_btn(h, BTN_R);
    static const uint64_t btns[] = {BTN_R, BTN_L, BTN_ZR, BTN_ZL, BTN_RIGHT, BTN_LEFT, BTN_ZR, BTN_R};
    for (int i = 0; i < steps; i++) {
        unsigned r = fz_rand() % 16;
        if (r < 5) { // a press, sometimes a held one's repeat
            uint64_t b = btns[fz_rand() % 8];
            if (fz_rand() % 3 == 0) hp_btn_rep(h, b);
            else hp_btn(h, b);
        } else if (r < 9) { // a sideways drag on the row: slow, quick or a flick, either way
            float x0 = 300.0f + (float)(fz_rand() % 700), dx = 20.0f + (float)(fz_rand() % 520);
            if (fz_rand() % 2) dx = -dx;
            hp_drag(h, x0, 230.0f + (float)(fz_rand() % 100), x0 + dx, 230.0f + (float)(fz_rand() % 100), 2 + (int)(fz_rand() % 24));
        } else if (r < 11) { // a flick, then a finger put on the row a few frames later (it stops there; a tap chooses nothing)
            float x0 = 500.0f + (float)(fz_rand() % 500), dx = 200.0f + (float)(fz_rand() % 300);
            if (fz_rand() % 2) dx = -dx;
            hp_drag(h, x0, 280, x0 - dx, 280, 3 + (int)(fz_rand() % 4));
            // (dragged, not tapped: a tap on a row at rest opens a box, and the run would stay in it)
            float cx = (float)(fz_rand() % 60) - 30, cy = 20.0f + (float)(fz_rand() % 60);
            hp_drag(h, 640, 280, 640 + cx, 280 + (fz_rand() % 2 ? cy : -cy), 2 + (int)(fz_rand() % 8));
            hp_in(h, 1 + (int)(fz_rand() % 5));
        } else if (r < 13) { // a drag up or down, starting on the row or below it
            float y0 = fz_rand() % 2 ? 280.0f : 500.0f, dy = 20.0f + (float)(fz_rand() % 130);
            if (fz_rand() % 2) dy = -dy;
            hp_drag(h, 640.0f + (float)(fz_rand() % 40) - 20, y0, 640.0f + (float)(fz_rand() % 40) - 20, y0 + dy, 2 + (int)(fz_rand() % 16));
        } else if (r < 14) { // the round buttons
            hp_tap(h, fz_rand() % 2 ? 1160.0f : 120.0f, 280);
        } else if (r < 15) {
            hp_btn(h, fz_rand() % 2 ? BTN_UP : BTN_DOWN);
        } else {
            hp_btn(h, BTN_Y);
        }
        if (fz_rand() % 5 < 3) hp_in(h, 1 + (int)(fz_rand() % 12)); // most land mid-slide
        else hp_call(h, fz_rest);
    }
    hp_call(h, fz_rest);
    hp_snap(h, "fuzz-end");
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    CHECK(run(&w) == 0);
    printf("  seed %u: %d steps, %d checks at rest, %d off a tile\n", seed, steps, g_fz_checks, g_fz_off);
    CHECK_MSG(g_fz_checks > steps / 5 && g_fz_off == 0, "random presses and swipes always leave the row resting on a tile (%d of %d off)", g_fz_off, g_fz_checks);
    w_done(&w);
    g_w = NULL;
}

// A guest plays, then makes a profile and keeps what they played: it becomes the new profile's saves, and it is loaded.
static void guest_plays(HostPlat *h)
{
    (void)h;
    fake_put(&g_w->fb, T_MK, "profile.dat", "guest: mario kart");
}

static void check_kept(HostPlat *h)
{
    (void)h;
    Store st2;
    store_open(&st2, &g_w->fb.be, g_w->fb.sd);
    Profile z;
    CHECK_MSG(find_profile(&st2, "Zed", &z), "Zed was made");
    CHECK_MSG(!strcmp(st2.state.holder, z.id) && !st2.state.wiped && st2.state.phase == PH_IDLE, "and is loaded, straight away");
    char tid[17], p[FSX_PATH];
    store_title_dir(T_MK, tid);
    snprintf(p, sizeof p, "%s/profiles/%s/current/%s/profile.dat", g_w->fb.sd, z.id, tid);
    char *got = NULL;
    if (fsx_read_all(p, &got, NULL) != 0) got = NULL;
    CHECK_MSG(got && !strcmp(got, "guest: mario kart"), "what the guest played is Zed's backup (%s)", got ? got : "(none)");
    free(got);
}

static void sc_guest_keeps(void)
{
    W w;
    w_init(&w, "27-guest-keeps", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX];
    seed_profile(&w, &st, "Maya", NULL, 600, 3, mayaid);
    snprintf(st.state.holder, sizeof st.state.holder, "%s", mayaid);
    st.state.loaded_at = (int64_t)w.now0 - 900;
    store_save_state(&st);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_X); // Maya is done: the console is cleared, a guest session
    hp_call(h, check_cleared);
    hp_call(h, guest_plays);
    hp_btn(h, BTN_RIGHT); // New profile
    hp_text(h, "Zed");
    hp_text(h, ""); // no NetID
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B); // no password
    hp_snap(h, "keep-what-you-played");
    hp_btn(h, BTN_A); // Keep it
    hp_snap(h, "playing-as-zed");
    hp_call(h, check_kept);
    hp_btn(h, BTN_A);
    hp_snap(h, "home-zed-loaded");
    hp_btn(h, BTN_B); // leaving: without signing out
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A);
    CHECK(run(&w) == 0);
    w_done(&w);
    g_w = NULL;
}

// Profile pictures: the picker from the home screen (a locked profile asks for its password first), swatches, and the result.
static unsigned char *load_file(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    unsigned char *buf = malloc((size_t)len);
    if (buf && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        buf = NULL;
    }
    fclose(f);
    *n = (size_t)len;
    return buf;
}

static unsigned char *load_pack(size_t *n)
{
    return load_file("../data/avatars.bin", n);
}

// The Switch's profile pictures are not in the repository (docs/profile-pictures.md). Without data/avatars.bin every picture is a letter, and
// the scenarios about the picture grid itself are skipped.
static int have_pack(void)
{
    FILE *f = fopen("../data/avatars.bin", "rb");
    if (f) fclose(f);
    return f != NULL;
}

static int need_pack(const char *scenario)
{
    if (have_pack()) return 1;
    printf("%-18s needs data/avatars.bin (the Switch's profile pictures, see docs/profile-pictures.md): skipped\n", scenario);
    return 0;
}

static int g_zed_picture = -1; // the random picture Zed was given

static void check_zed_on_console(HostPlat *h)
{
    (void)h;
    FakeBe *f = &g_w->fb;
    CHECK_MSG(!strcmp(f->sys_nick, "Zed"), "the console's profile now shows the loaded player (%s)", f->sys_nick);
    CHECK_MSG(f->sys_img_size > 200 && f->sys_img[0] == 0xFF && f->sys_img[1] == 0xD8 && f->sys_img[f->sys_img_size - 2] == 0xFF &&
                  f->sys_img[f->sys_img_size - 1] == 0xD9,
              "and a real JPEG picture (%zu bytes)", f->sys_img_size);
    CHECK_MSG(f->sys_ud[SP_UD_ICON] == g_zed_picture + 1 && f->sys_ud[SP_UD_BG] < SWATCH_COUNT, "and the icon id (%d) and background id (%d) go with it", f->sys_ud[SP_UD_ICON], f->sys_ud[SP_UD_BG]);
    uint8_t *px = gfx_decode_icon(f->sys_img, f->sys_img_size, 64);
    CHECK_MSG(px != NULL, "that decodes");
    free(px);
}

// a new profile starts with a picture and a background chosen at random, not with a letter
static void check_new_look(HostPlat *h)
{
    (void)h;
    Store s2;
    store_open(&s2, &g_w->fb.be, g_w->fb.sd);
    Profile zed, maya, priya;
    CHECK(find_profile(&s2, "Zed", &zed) && find_profile(&s2, "Maya", &maya) && find_profile(&s2, "Priya", &priya));
    CHECK_MSG(zed.picture >= 0 && zed.picture < skin_avatar_count(), "a new profile starts with a picture, not a letter (%d)", zed.picture);
    CHECK_MSG(zed.picture != maya.picture && zed.picture != priya.picture, "and not one somebody else already has");
    CHECK_MSG(zed.color >= 0 && zed.color < SWATCH_COUNT && zed.color % SWATCH_COLS < 3, "on the lighter half of one of the console's colours (%d)", zed.color);
    g_zed_picture = zed.picture;
}

// B on Edit Icon while making a profile drops it: nothing was made
static void check_no_quinn(HostPlat *h)
{
    (void)h;
    Store s2;
    store_open(&s2, &g_w->fb.be, g_w->fb.sd);
    Profile q;
    CHECK_MSG(!find_profile(&s2, "Quinn", &q), "a new profile that was cancelled at Edit Icon was never made");
}

static void sc_picture(void)
{
    if (!need_pack("picture")) return;
    W w;
    w_init(&w, "13-picture", 1);
    g_w = &w;
    size_t packn = 0;
    unsigned char *pack = load_pack(&packn);
    CHECK_MSG(pack != NULL, "data/avatars.bin is there");
    ui_set_avatar_pack(pack, packn);
    CHECK_MSG(skin_avatar_count() == 148, "all the pictures are in the pack (%d)", skin_avatar_count());
    {   // small pictures come from the thumbnails, big ones from the full picture, whichever was asked for first
        int pw = 0, ph = 0;
        CHECK(skin_avatar_rgba_fit(5, 84, &pw, &ph) != NULL && pw == 128 && ph == 128);
        CHECK(skin_avatar_rgba_fit(5, 256, &pw, &ph) != NULL && pw == 256);
        CHECK(skin_avatar_rgba_fit(5, 84, &pw, &ph) != NULL && pw == 128);
        CHECK(skin_avatar_rgba_fit(6, 256, &pw, &ph) != NULL && pw == 256);
        CHECK(skin_avatar_rgba_fit(7, 40, &pw, &ph) != NULL && pw == 64);
    }
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX], priyaid[ID_MAX];
    seed_profile(&w, &st, "Maya", NULL, 600, 2, mayaid);
    seed_profile(&w, &st, "Priya", "111111", 90000, 1, priyaid);
    // the console's profile follows the loaded player (name and picture), once the admin has turned that on
    st.cfg.sync_name = 1;
    st.cfg.sync_picture = 1;
    store_save_config(&st);
    HostPlat *h = &w.hp;
    hp_snap(h, "home-before");
    hp_btn(h, BTN_PLUS); // nobody is loaded: the selected profile's editor (Maya has no password), and B leaves it with nobody loaded
    hp_snap(h, "nobody-loaded");
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_A); // load Maya
    hp_btn(h, BTN_A); // Switch
    hp_btn(h, BTN_A); // ready
    hp_btn(h, BTN_PLUS); // Edit profile for the loaded player
    hp_snap(h, "edit-profile");
    hp_btn(h, BTN_A); // Picture and background
    hp_snap(h, "edit-icon");
    hp_btn(h, BTN_A); // the Picture grid
    hp_snap(h, "picture-grid");
    hp_btn_n(h, BTN_RIGHT, 2);
    hp_btn(h, BTN_DOWN);
    hp_snap(h, "picture-moved");
    hp_btn(h, BTN_A); // choose it: back to Edit Icon
    hp_snap(h, "edit-icon-picked");
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // the Background grid
    hp_snap(h, "background-grid");
    hp_btn_n(h, BTN_RIGHT, 3);
    hp_btn_n(h, BTN_DOWN, 2);
    hp_snap(h, "background-moved");
    hp_btn(h, BTN_A);
    hp_snap(h, "edit-icon-colored");
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // OK
    hp_btn(h, BTN_B); // and out of Edit profile
    hp_snap(h, "home-after");
    hp_btn(h, BTN_RIGHT); // Priya: locked
    hp_btn(h, BTN_A);
    hp_snap(h, "password-first");
    hp_pin(h, "111111");
    hp_btn(h, BTN_A); // Switch
    hp_btn(h, BTN_A); // ready
    hp_btn(h, BTN_PLUS); // her Edit profile (she is loaded: no password again)
    hp_btn(h, BTN_A); // Picture and background
    hp_btn(h, BTN_A); // Picture grid
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_A);
    hp_btn_n(h, BTN_DOWN, 2);
    hp_btn(h, BTN_A); // OK
    hp_btn(h, BTN_B); // out of Edit profile
    hp_snap(h, "home-both");
    hp_btn(h, BTN_PLUS); // Priya again, then cancel: nothing changes
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_btn_n(h, BTN_RIGHT, 5);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_RIGHT); // the New profile tile: a new profile is made at the end, by OK on Edit Icon
    hp_text(h, "Quinn");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B); // no password
    hp_snap(h, "new-profile-cancel");
    hp_btn(h, BTN_B); // never mind
    hp_snap(h, "home-cancelled");
    hp_call(h, check_no_quinn);
    hp_text(h, "Zed");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_B); // no password
    hp_snap(h, "new-profile-icon");
    hp_btn_n(h, BTN_DOWN, 2);
    hp_btn(h, BTN_A); // OK: the random picture and background are kept
    hp_call(h, check_new_look);
    hp_snap(h, "home-three");
    hp_btn(h, BTN_A); // choose Maya? (the cursor is on Zed) -- load Zed: the console's profile follows
    hp_btn(h, BTN_A); // Switch
    hp_btn(h, BTN_A); // ready
    hp_snap(h, "zed-loaded");
    hp_call(h, check_zed_on_console);
    hp_btn(h, BTN_X); // Done playing: the original profile comes back
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Profile maya, priya, zed;
    CHECK(find_profile(&st2, "Maya", &maya) && find_profile(&st2, "Priya", &priya));
    CHECK_MSG(maya.picture == skin_avatar_at(10), "Maya picked two along and a row down on the console's card (nine to a row, after the letter: id %d)", maya.picture);
    const int bg_want = SWATCH_VIVID[0] / SWATCH_COLS * SWATCH_COLS + (SWATCH_VIVID[0] % SWATCH_COLS + 3) % SWATCH_COLS + 2 * SWATCH_COLS;
    CHECK_MSG(maya.color == bg_want, "and moved the background three along (round its row's end) and down two rows (%d, wanted %d)", maya.color, bg_want);
    CHECK_MSG(priya.picture == 0, "Priya picked the first picture after her password (%d)", priya.picture);
    CHECK_MSG(find_profile(&st2, "Zed", &zed) && zed.picture == g_zed_picture, "Zed was made when Edit Icon was accepted (picture %d)", zed.picture);
    CHECK_MSG(w.fb.sys_stores >= 2, "the console's own profile was written when Zed loaded and put back after (%d writes)", w.fb.sys_stores);
    CHECK_MSG(!strcmp(w.fb.sys_nick, "Esports Switch"), "Done playing put the account's own name back (%s)", w.fb.sys_nick);
    CHECK_MSG(w.fb.sys_img_size > 200 && w.fb.sys_img[0] == 0xFF && w.fb.sys_ud[SP_UD_ICON] == 112 && w.fb.sys_ud[SP_UD_BG] == 43,
              "and the guest picture, Princess Toadette on light blue (icon %d, background %d)", w.fb.sys_ud[SP_UD_ICON], w.fb.sys_ud[SP_UD_BG]);
    w_done(&w);
    g_w = NULL;
    ui_set_avatar_pack(NULL, 0);
    free(pack);
}

// The console's own pictures, from a folder of pretend console files (test/avatar_fixture.h): the grid is in the table's order, a profile's picture is the one
// the table's id names, one whose file is missing is a letter, and with nothing to show it is letters again.
static void sc_console_pictures(void)
{
    W w;
    w_init(&w, "41-console-pictures", 1);
    g_w = &w;
    char dir[300];
    fx_dir(dir, sizeof dir, "app");
    static const uint32_t ids[] = {4, 2, 6, 1, 3, 5};
    fx_table(dir, ids, 6);
    for (int id = 1; id <= 5; id++) fx_picture(dir, id); // (id 6 has no file)
    CHECK_MSG(ui_set_avatar_dir(dir) == 6, "six pictures");
    CHECK_MSG(skin_avatar_count() == 6 && skin_avatar_at(0) == 3 && skin_avatar_at(1) == 1 && skin_avatar_at(2) == 5 && skin_avatar_pos(3) == 0 && skin_avatar_pos(0) == 3,
              "the grid follows the table (%d %d %d)", skin_avatar_at(0), skin_avatar_at(1), skin_avatar_at(2));
    int pw = 0, ph = 0;
    const uint8_t *px = skin_avatar_rgba(3, &pw, &ph);
    uint8_t want[4];
    fx_pixel(3, 100, 9, want);
    CHECK_MSG(px && pw == 256 && ph == 256 && !memcmp(px + ((size_t)9 * 256 + 100) * 4, want, 4), "a picture is the file's, 256 x 256 (%dx%d)", pw, ph);
    px = skin_avatar_rgba_fit(3, 84, &pw, &ph);
    CHECK_MSG(px && pw == 128 && ph == 128, "drawn small it comes from the half-size copy (%dx%d)", pw, ph);
    CHECK_MSG(skin_avatar_rgba_fit(5, 84, &pw, &ph) == NULL, "a picture without a file has nothing to draw");
    CHECK_MSG(strstr(ui_avatar_status(), "6 pictures") != NULL, "and the log can say so (%s)", ui_avatar_status());
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    static const char *const names[] = {"Alex", "Maya", "Priya", "Sam"};
    static const int pics[] = {0, 3, 5, -1}; // a table's first, the fourth id, the one with no file, and a letter
    for (int i = 0; i < 4; i++) {
        seed_profile(&w, &st, names[i], NULL, 600 + 3600 * i, 2, NULL);
        Profile p;
        if (find_profile(&st, names[i], &p)) {
            p.picture = pics[i];
            p.color = 20 + 6 * i;
            store_profile_save(&st, &p);
        }
    }
    HostPlat *h = &w.hp;
    hp_snap(h, "home");
    int rc = run(&w);
    CHECK(rc == 0);
    ui_set_avatar_pack(NULL, 0);
    CHECK_MSG(skin_avatar_count() == 0 && strstr(ui_avatar_status(), "every profile is a letter") != NULL, "with nothing to show it is letters (%s)", ui_avatar_status());
    CHECK_MSG(ui_set_avatar_dir("out/avatar/there-is-no-such-folder/") == 0 && skin_avatar_count() == 0, "a folder with no pictures changes nothing");
    w_done(&w);
    g_w = NULL;
}

// ---- Online icons: the page that downloads them (ui_icons.c) against a pretend GitHub, and the picker's tab for them. The pictures are made here (one colour each, no
// artwork), as files at <sd>/pictures/<game>/<name>.png the way the page puts them.
extern int stbi_write_png(char const *filename, int w, int h, int comp, const void *data, int stride_in_bytes);
static const struct { const char *slug; int n; } OI_GAMES[] = {{"game-a", 8}, {"game-b", 3}, {"game-c", 1}}; // (names A01.. B01.. C01..)

static uint8_t *oi_png(int k, size_t *len) // a 256 x 256 PNG, the bytes a download would be
{
    uint8_t *px = malloc(256 * 256 * 4);
    for (int i = 0; i < 256 * 256; i++) {
        px[4 * i] = (uint8_t)(k * 37), px[4 * i + 1] = (uint8_t)(k * 91 + 60), px[4 * i + 2] = (uint8_t)(k * 53 + 120);
        px[4 * i + 3] = (i % 256) < 24 || (i % 256) >= 232 ? 0 : 255;
    }
    mkdir("out", 0777);
    stbi_write_png("out/oi-tmp.png", 256, 256, 4, px, 256 * 4);
    free(px);
    return load_file("out/oi-tmp.png", len);
}

static int oi_hook(const char *url, uint8_t **body, size_t *len, int *status)
{
    if (strstr(url, "/git/trees/")) {
        char *j = malloc(8192);
        size_t o = (size_t)snprintf(j, 8192, "{\"sha\":\"x\",\"truncated\":false,\"tree\":[{\"path\":\"README.md\",\"type\":\"blob\",\"size\":5}");
        for (int g = 0; g < 3; g++)
            for (int i = 0; i < OI_GAMES[g].n; i++) {
                o += (size_t)snprintf(j + o, 8192 - o, ",{\"path\":\"%s/characters/%c%02d.png\",\"type\":\"blob\",\"size\":%d}", OI_GAMES[g].slug, 'A' + g, i + 1, 90000 + i);
                o += (size_t)snprintf(j + o, 8192 - o, ",{\"path\":\"%s/backgrounds/BG%02d.png\",\"type\":\"blob\",\"size\":9}", OI_GAMES[g].slug, i + 1);
            }
        snprintf(j + o, 8192 - o, "]}");
        *body = (uint8_t *)j;
        *len = strlen(j);
        *status = 200;
        return 1;
    }
    const char *c = strstr(url, "/characters/");
    if (!c) return 0;
    const char *nm = c + 12;
    if (!strcmp(nm, "B03.png")) { // game-b's third icon is not on the pretend server
        *body = (uint8_t *)strdup("Not Found");
        *len = 9;
        *status = 404;
        return 1;
    }
    static int asked;
    if (++asked == 6 && g_w && g_w->hp.last) // (the page as it is drawn while the download goes on: the frame shown now)
        stbi_write_png("out/app/42-online-icons/progress.png", SCREEN_W, SCREEN_H, 4, g_w->hp.last->px, SCREEN_W * 4);
    *body = oi_png(1 + (nm[0] - 'A') * 10 + atoi(nm + 1), len);
    *status = *body ? 200 : 0;
    return *body != NULL;
}

static char g_oi_dir[FSX_PATH];
static void oi_dirs(HostPlat *h)
{
    (void)h;
    snprintf(g_oi_dir, sizeof g_oi_dir, "%s/pictures", g_w->fb.sd);
}
static void oi_server(HostPlat *h)
{
    h->http_hook = oi_hook;
    h->net = HP_NET_LATEST;
}
static void oi_parallel(HostPlat *h) { h->p.http_parallel = 4; }
static int oi_has(const char *rel)
{
    char p[FSX_PATH + 64];
    snprintf(p, sizeof p, "%s/%s", g_oi_dir, rel);
    return fsx_exists(p);
}
static void oi_says_get(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_icons_a_says && !strcmp(g_ui_icons_a_says, "Get"), "on a game that is missing icons the footer says A Get (%s)", g_ui_icons_a_says ? g_ui_icons_a_says : "nothing");
}
static void oi_says_update(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_icons_a_says && !strcmp(g_ui_icons_a_says, "Update"), "on a game that is all there the footer says A Update (%s)", g_ui_icons_a_says ? g_ui_icons_a_says : "nothing");
}
static void oi_check_a(HostPlat *h)
{
    (void)h;
    int all = 1;
    for (int i = 1; i <= 8; i++) {
        char r[40];
        snprintf(r, sizeof r, "game-a/A%02d.png", i);
        all = all && oi_has(r);
    }
    CHECK_MSG(all && !oi_has("game-b/B01.png"), "game-a's eight icons are on the card, and only those");
    char p[FSX_PATH + 64];
    snprintf(p, sizeof p, "%s/game-a/A03.png", g_oi_dir);
    size_t n = 0;
    unsigned char *b = load_file(p, &n);
    CHECK_MSG(b && n > 100 && b[1] == 'P' && b[2] == 'N' && b[3] == 'G', "and they are the PNGs that came");
    free(b);
}
static void oi_check_b(HostPlat *h)
{
    (void)h;
    CHECK_MSG(oi_has("game-b/B01.png") && oi_has("game-b/B02.png") && !oi_has("game-b/B03.png"), "game-b has the two icons that came and not the one that was a 404");
}
static void oi_check_c(HostPlat *h)
{
    (void)h;
    CHECK_MSG(oi_has("game-c/C01.png") && !oi_has("game-b/B03.png"), "get all brought game-c's icon, and tried the missing one again");
}
static void oi_check_removed(HostPlat *h)
{
    (void)h;
    CHECK_MSG(!oi_has("game-a") && oi_has("game-b/B01.png") && oi_has("game-c/C01.png"), "removing a game takes its folder away and nothing else");
    char p[FSX_PATH + 64];
    snprintf(p, sizeof p, "%s/game-b", g_oi_dir);
    char **names = NULL;
    size_t n = 0;
    int tmp = 0;
    if (fsx_list(p, &names, &n) == 0) {
        for (size_t i = 0; i < n; i++) tmp += strstr(names[i], ".tmp") != NULL;
        fsx_list_free(names, n);
    }
    CHECK_MSG(tmp == 0, "no half-written files are left behind");
}
static void oi_check_first_c(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_icons_first == 2, "On your card first: game-c (all of its icons are here) leads (first row is game %d)", g_ui_icons_first);
}
static void oi_check_first_a(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_icons_first == 0, "by name, game-a leads (first row is game %d)", g_ui_icons_first);
}
static void oi_clean(HostPlat *h)
{
    (void)h;
    oi_dirs(h);
    fsx_rmtree(g_oi_dir);
}

static void sc_online_icons_page(void)
{
    W w;
    w_init(&w, "42-online-icons", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_call(h, oi_clean);
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hp_call(h, oi_server);
    hub_open(h, &hub, HUB_ICONS);
    hp_snap(h, "list");
    hp_call(h, oi_says_get);
    hp_btn(h, BTN_A); // game-a: eight icons
    hp_snap_after(h, 4, "downloading");
    hp_snap(h, "got-a");
    hp_call(h, oi_check_a);
    hp_btn(h, BTN_A); // the message
    hp_call(h, oi_says_update);
    hp_call(h, oi_parallel); // from here on, as the console does it: four workers
    hp_btn(h, BTN_DOWN);
    hp_call(h, oi_says_get);
    hp_btn(h, BTN_A); // game-b: its third icon is a 404
    hp_snap(h, "one-failed");
    hp_btn(h, BTN_A);
    hp_call(h, oi_check_b);
    hp_btn(h, BTN_X); // every game that is missing icons
    hp_snap(h, "confirm-all");
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A); // the message (game-b's third icon is a 404 again)
    hp_call(h, oi_check_c);
    hp_btn(h, BTN_UP);
    hp_btn(h, BTN_Y); // remove game-a
    hp_snap(h, "confirm-remove");
    hp_btn(h, BTN_A);
    hp_snap(h, "list-after");
    hp_call(h, oi_check_removed);
    hp_call(h, oi_check_first_a);
    hp_btn(h, BTN_R3); // the order menu: the sleeve, Recently downloaded out of it
    hp_snap(h, "sort-menu");
    hp_btn(h, BTN_DOWN); // Most icons
    hp_btn(h, BTN_DOWN); // On your card first
    if (getenv("TRACE_PNG")) hp_call(h, sa_on); // (the rows shuffling into the new order)
    hp_btn(h, BTN_A);
    hp_wait(h, 1);
    if (getenv("TRACE_PNG")) hp_call(h, sa_off);
    hp_snap(h, "on-card-first");
    hp_call(h, oi_check_first_c);
    hp_btn(h, BTN_X); // game-a is missing again, so this asks for it (and game-b's 404)
    hp_btn(h, BTN_B); // cancel
    hp_btn(h, BTN_B); // out of the page
    hub_leave(h, &hub);
    CHECK(run(&w) == 0);
    CHECK_MSG(w.hp.net_gets >= 1 + 8 + 3 + 2, "the page asked for the list and the icons (%d requests)", w.hp.net_gets);
    w.hp.http_hook = NULL;
    fsx_rmtree(g_oi_dir);
    w_done(&w);
    g_w = NULL;
}

static void oi_make_card(HostPlat *h)
{
    (void)h;
    oi_dirs(h);
    for (int g = 0; g < 3; g++)
        for (int i = 1; i <= OI_GAMES[g].n; i++) {
            char dir[FSX_PATH + 32], p[FSX_PATH + 64];
            snprintf(dir, sizeof dir, "%s/%s", g_oi_dir, OI_GAMES[g].slug);
            fsx_mkdir_p(dir);
            snprintf(p, sizeof p, "%s/%c%02d.png", dir, 'A' + g, i);
            size_t n = 0;
            uint8_t *png = oi_png(1 + g * 10 + i, &n);
            if (png) fsx_write_file(p, png, n);
            free(png);
        }
}

// does this profile.json hold exactly this Online icon's name and this number? (parsed: the file is pretty-printed)
static int json_picfile(const char *txt, const char *key, int number)
{
    cJSON *j = txt ? cJSON_Parse(txt) : NULL;
    int ok = 0;
    if (j) {
        cJSON *pf = cJSON_GetObjectItemCaseSensitive(j, "picfile"), *pic = cJSON_GetObjectItemCaseSensitive(j, "picture");
        ok = cJSON_IsString(pf) && !strcmp(pf->valuestring, key) && cJSON_IsNumber(pic) && (int)pic->valuedouble == number;
        cJSON_Delete(j);
    }
    return ok;
}

static void oi_check_choice(HostPlat *h)
{
    (void)h;
    Store s2;
    store_open(&s2, &g_w->fb.be, g_w->fb.sd);
    Profile maya;
    CHECK(find_profile(&s2, "Maya", &maya));
    CHECK_MSG(!strcmp(maya.picfile, "game-b/B02") && custom_is(maya.picture) && !strcmp(custom_key(maya.picture), "game-b/B02"),
              "Maya's picture is the second icon of game-b, kept by name (%s)", maya.picfile);
    char path[FSX_PATH], *txt = NULL;
    size_t n = 0;
    store_profile_dir(&s2, maya.id, path, sizeof path);
    strncat(path, "/profile.json", sizeof path - strlen(path) - 1);
    CHECK(fsx_read_all(path, &txt, &n) == 0);
    CHECK_MSG(json_picfile(txt, "game-b/B02", -1), "the file keeps the name and a -1 for the number");
    free(txt);
    Profile again;
    CHECK_MSG(store_profile_load(&s2, maya.id, &again) == 0 && again.picture == maya.picture && !strcmp(again.picfile, "game-b/B02"), "loaded again it is the same icon");
}

static void sc_online_icons_picker(void)
{
    W w;
    w_init(&w, "43-online-picker", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    hp_call(h, oi_make_card);
    hp_btn(h, BTN_A); // load Maya
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_PLUS); // Edit profile
    hp_btn(h, BTN_A);    // Picture and background
    hp_btn(h, BTN_A);    // the Picture grid
    hp_snap(h, "console-tab");
    hp_btn(h, BTN_Y); // Online icons
    hp_snap(h, "game-a");
    hp_btn(h, BTN_R);
    hp_snap(h, "game-b");
    hp_btn(h, BTN_ZR); // several: clamped to the last game
    hp_snap(h, "game-c");
    hp_btn(h, BTN_R); // no game further: nothing happens
    hp_btn(h, BTN_ZL);
    hp_btn(h, BTN_ZL); // back to the first
    hp_btn(h, BTN_ZR);
    hp_btn(h, BTN_L); // game-c, then game-b
    hp_btn(h, BTN_RIGHT);
    hp_snap(h, "game-b-second");
    hp_btn(h, BTN_A); // the second icon of game-b
    hp_snap(h, "edit-icon-picked");
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // OK
    hp_call(h, oi_check_choice);
    hp_btn(h, BTN_B); // out of Edit profile
    hp_btn(h, BTN_PLUS); // again: the picker opens on that icon's game, in the Online tab
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_snap(h, "reopened");
    hp_btn(h, BTN_Y); // back to the console's pictures
    hp_snap(h, "console-again");
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    fsx_rmtree(g_oi_dir);
    w_done(&w);
    g_w = NULL;
}

// a fourth game, with more icons than the others, so the orders differ (game-z: 12)
static void oi_make_game_z(HostPlat *h)
{
    oi_dirs(h);
    char dir[FSX_PATH + 32], p[FSX_PATH + 64];
    snprintf(dir, sizeof dir, "%s/game-z", g_oi_dir);
    fsx_mkdir_p(dir);
    for (int i = 1; i <= 12; i++) {
        snprintf(p, sizeof p, "%s/Z%02d.png", dir, i);
        size_t n = 0;
        uint8_t *png = oi_png(100 + i, &n);
        if (png) fsx_write_file(p, png, n);
        free(png);
    }
}

static void ex_expect(int pos, int sel, const char *what)
{
    CHECK_MSG((pos < 0 || g_ui_extra_pos == pos) && (sel < 0 || g_ui_extra_sel == sel), "%s: strip at %d (wanted %d), character %d (wanted %d)", what, g_ui_extra_pos, pos, g_ui_extra_sel, sel);
}
static void ex_console_first(HostPlat *h) { (void)h; ex_expect(0, 0, "opens on the console's card (first), on the letter: Maya has no picture"); }
static void ex_c_third(HostPlat *h) { (void)h; ex_expect(2, 0, "two cards along (R, R): game-b"); }
static void ex_console_again(HostPlat *h) { (void)h; ex_expect(0, 0, "back on the console's card (ZL clamps)"); }
static void ex_a_card(HostPlat *h) { (void)h; ex_expect(1, 0, "game-a, A to Z the first game, is the second card"); }
static void ex_wrap_l(HostPlat *h) { (void)h; ex_expect(1, 7, "left on the row's first character goes round to its last"); }
static void ex_wrap_r(HostPlat *h) { (void)h; ex_expect(1, 0, "right on the row's last goes round to its first"); }
static void ex_wrap_u(HostPlat *h) { (void)h; ex_expect(1, 0, "one row: up has no other end of its column to go round to (it leans)"); }
static void ex_wrap_d(HostPlat *h) { (void)h; ex_expect(1, 0, "nor down"); }
static void ex_most(HostPlat *h) { (void)h; ex_expect(2, -1, "Most icons: the console still first, game-z (12) next, game-a (8) third, and it is still the card in focus"); }
static void ex_clamp_last(HostPlat *h) { (void)h; ex_expect(4, -1, "ZR clamps to the last card (game-c, the fewest icons)"); }
static void ex_letter(HostPlat *h) { (void)h; ex_expect(0, 0, "X goes to no picture: the console's card, its letter"); }
static void ex_untouched(HostPlat *h) { (void)h; CHECK_MSG(g_ui_extra_pos == -2, "the Background grid has no Extra icons tab (Y did nothing there)"); }
static void ex_mark(HostPlat *h) { (void)h; g_ui_extra_pos = -2; }

static void sc_extra_icons(void)
{
    W w;
    w_init(&w, "43b-extra-icons", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    hp_call(h, oi_make_card);
    hp_call(h, oi_make_game_z);
    hp_btn(h, BTN_A); // load Maya
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_PLUS); // Edit profile
    hp_btn(h, BTN_A);    // Picture and background
    hp_btn(h, BTN_A);    // the Picture page: the strip of cards
    hp_snap(h, "a-to-z");
    hp_call(h, ex_console_first);
    hp_btn(h, BTN_R);
    hp_snap_after(h, 3, "sliding");
    hp_btn(h, BTN_R);
    hp_call(h, ex_c_third);
    hp_btn(h, BTN_L);
    hp_btn(h, BTN_L);
    hp_btn(h, BTN_ZL); // (nowhere: the strip leans)
    hp_call(h, ex_console_again);
    hp_btn(h, BTN_R);
    hp_snap(h, "game-a");
    hp_call(h, ex_a_card);
    hp_btn(h, BTN_LEFT);
    hp_call(h, ex_wrap_l);
    hp_btn(h, BTN_RIGHT);
    hp_call(h, ex_wrap_r);
    hp_btn(h, BTN_UP);
    hp_call(h, ex_wrap_u);
    hp_btn(h, BTN_DOWN);
    hp_call(h, ex_wrap_d);
    hp_btn(h, BTN_R3); // the sleeve, Recently downloaded out of it
    hp_snap(h, "sort-menu");
    hp_btn(h, BTN_DOWN);
    if (getenv("TRACE_PNG")) hp_call(h, sa_on); // (the cards shuffling into the new order)
    hp_btn(h, BTN_A); // Most icons
    hp_wait(h, 1);
    if (getenv("TRACE_PNG")) hp_call(h, sa_off);
    hp_snap(h, "most-icons");
    hp_call(h, ex_most);
    hp_btn(h, BTN_ZR);
    hp_call(h, ex_clamp_last);
    hp_btn(h, BTN_X); // no picture
    hp_call(h, ex_letter);
    hp_snap_after(h, 30, "letter");
    hp_btn(h, BTN_B); // out of the strip, back on Edit Icon
    hp_call(h, ex_mark);
    hp_btn(h, BTN_DOWN);
    hp_btn(h, BTN_A); // the Background grid: no tab
    hp_btn(h, BTN_Y);
    hp_snap(h, "background-no-tab");
    hp_call(h, ex_untouched);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    fsx_rmtree(g_oi_dir);
    w_done(&w);
    g_w = NULL;
}

// The small pictures made on their own thread, as on the console: the page opens at once (stand-ins where pictures are on their way) and the pictures
// come in; nothing waits on a PNG. (The other scenarios make them at once, so their frames don't depend on a thread's timing.)
extern int g_ui_extra_waiting;
extern long g_ui_extra_rebakes;
static int g_ta_ready;

static void ta_async(HostPlat *h)
{
    (void)h;
    thumbs_flush();
    g_thumbs_sync = 0;
    g_ui_extra_rebakes = 0;
}

static void ta_wait(HostPlat *h) // (real time: the thread works while the scripted frames take none)
{
    (void)h;
    g_ta_ready = 0;
    for (int t = 0; t < 3000 && !g_ta_ready; t += 5) {
        int all = 1;
        for (int i = 1; i <= 8; i++) {
            char key[32];
            snprintf(key, sizeof key, "game-a/A%02d", i);
            if (!thumbs_ready(custom_id(key))) all = 0;
        }
        g_ta_ready = all;
        if (!all) {
            struct timespec ts = {0, 5000000L};
            nanosleep(&ts, NULL);
        }
    }
}

static void ta_check(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ta_ready, "the thread made game-a's eight small pictures");
    CHECK_MSG(!g_ui_extra_waiting, "and the rows showing were drawn again with them (no stand-ins left)");
    CHECK_MSG(g_ui_extra_pos == 1, "on game-a's card (%d)", g_ui_extra_pos);
}

static void ta_sync(HostPlat *h)
{
    (void)h;
    g_thumbs_sync = 1;
}

static void sc_thumbs_async(void)
{
    W w;
    w_init(&w, "43c-thumbs-async", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    hp_call(h, oi_make_card);
    hp_btn(h, BTN_A); // load Maya
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_PLUS); // Edit profile
    hp_btn(h, BTN_A);    // Picture and background
    hp_call(h, ta_async);
    hp_btn(h, BTN_A); // the strip, on the console's card
    hp_btn(h, BTN_R); // game-a
    hp_snap(h, "game-a-first-frames");
    hp_call(h, ta_wait);
    hp_wait(h, 2); // (two frames: the page sees the pictures have come)
    hp_snap(h, "game-a-loaded");
    hp_call(h, ta_check);
    hp_btn(h, BTN_B);
    hp_call(h, ta_sync);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    g_thumbs_sync = getenv("THUMBS_ASYNC") == NULL;
    fsx_rmtree(g_oi_dir);
    w_done(&w);
    g_w = NULL;
}

// PICKER_BENCH=1 BENCH=1 ./app_host picker-bench: how long the Picture page's frames take with a card of many games (15, one of 300 icons; 256 px PNGs
// with detail, so they decode like real ones), sliding through every card and scrolling the big one. THUMBS_ASYNC=1 makes the pictures on their thread,
// as the console does; without it they are made on the drawing thread, as before the thread (the frames then carry the decoding).
static void pb_make(HostPlat *h)
{
    oi_dirs(h);
    uint8_t *px = malloc(256 * 256 * 4);
    uint32_t rnd = 12345;
    for (int g = 0; g < 15; g++) {
        char dir[FSX_PATH + 32];
        snprintf(dir, sizeof dir, "%s/bench-%02d", g_oi_dir, g);
        fsx_mkdir_p(dir);
        int n = g == 0 ? 300 : 30;
        for (int i = 0; i < n; i++) {
            for (int p = 0; p < 256 * 256; p++) {
                int bx = (p % 256) / 16, by = (p / 256) / 16;
                rnd = rnd * 1103515245u + 12345u;
                uint8_t noise = (uint8_t)((rnd >> 16) & 15);
                px[4 * p] = (uint8_t)(bx * 13 + i * 7 + noise), px[4 * p + 1] = (uint8_t)(by * 11 + g * 17 + noise), px[4 * p + 2] = (uint8_t)((bx ^ by) * 9 + noise);
                int dx = p % 256 - 128, dy = p / 256 - 128;
                px[4 * p + 3] = dx * dx + dy * dy < 120 * 120 ? 255 : 0;
            }
            char p[FSX_PATH + 64];
            snprintf(p, sizeof p, "%s/B%03d.png", dir, i);
            stbi_write_png(p, 256, 256, 4, px, 256 * 4);
        }
    }
    free(px);
    ui_icon_set(1);
}

static void pb_report(HostPlat *h)
{
    (void)h;
    hp_bench_report(getenv("THUMBS_ASYNC") ? "picker (thread)" : "picker (drawing thread)");
}

static void sc_picker_bench(void)
{
    if (!getenv("PICKER_BENCH")) return;
    W w;
    w_init(&w, "43d-picker-bench", 1);
    g_w = &w;
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    hp_call(h, pb_make);
    hp_btn(h, BTN_A); // load Maya
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_PLUS);
    hp_btn(h, BTN_A);
    hp_call(h, pb_report); // (what came before isn't the page's)
    hp_btn(h, BTN_A); // the strip
    for (int i = 0; i < 15; i++) hp_btn(h, BTN_R);
    for (int i = 0; i < 15; i++) hp_btn(h, BTN_L);
    hp_btn(h, BTN_R); // the big one (A to Z: bench-00)
    for (int i = 0; i < 30; i++) hp_btn_rep(h, BTN_DOWN);
    hp_call(h, pb_report);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    fsx_rmtree(g_oi_dir);
    w_done(&w);
    g_w = NULL;
}

// Everything by touch: tap a tile, the dialog's buttons, the Done playing button and the footer prompts; the pad's keys; drag a grid.
// the colour grid scrolled by a finger: the focus's dashed outline goes under the sheet's top edge with its swatch, never over what is above it
static Surf *g_bg_before;
static int g_bg_spill, g_bg_frames;

static void bg_hook(HostPlat *h, const Surf *f)
{
    (void)h;
    g_bg_frames++;
    for (int y = 140; y < 172; y++) // (a whole cell in the top row has its outline up to 6 px over the window: cut only when the cell is)
        if (memcmp(f->px + ((size_t)y * SCREEN_W + 120) * 4, g_bg_before->px + ((size_t)y * SCREEN_W + 120) * 4, 440 * 4)) g_bg_spill++;
}

static void bg_watch(HostPlat *h)
{
    surf_free(g_bg_before);
    g_bg_before = surf_clone(h->last);
    g_bg_spill = g_bg_frames = 0;
    h->trace_on = 1;
    h->trace_png = 0;
    h->frame_hook = bg_hook;
}

static void bg_check(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    CHECK_MSG(g_bg_frames > 5 && g_bg_spill == 0, "the focus outline stays under the sheet's edge as the grid scrolls (%d rows over it in %d frames)", g_bg_spill, g_bg_frames);
    surf_free(g_bg_before);
    g_bg_before = NULL;
}

// how much of ring i (0..5) of the password pad is inked in frame f, against the card's colour (the empty sixth ring's middle)
static int ring_ink(const Surf *f, int i)
{
    const uint8_t *paper = f->px + ((size_t)(50 + 177) * SCREEN_W + 545 + 5 * 38) * 4;
    int ink = 0, cx = 545 + i * 38, cy = 50 + 177;
    for (int y = cy - 7; y <= cy + 7; y++)
        for (int x = cx - 7; x <= cx + 7; x++) {
            if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > 49) continue;
            const uint8_t *q = f->px + ((size_t)y * SCREEN_W + x) * 4;
            ink += abs(q[0] - paper[0]) + abs(q[1] - paper[1]) + abs(q[2] - paper[2]) > 90;
        }
    return ink;
}

// a digit's ring scribbles itself in: over a few frames, quickly, more of it inked each frame
static int g_rs_ring, g_rs_frames, g_rs_part, g_rs_fall, g_rs_last;
static uint64_t g_rs_t0, g_rs_first_at, g_rs_full_at;
static void rs_hook(HostPlat *h, const Surf *f)
{
    int k = ring_ink(f, g_rs_ring);
    g_rs_frames++;
    if (k > 0 && k < 120) g_rs_part++;
    if (k > 0 && !g_rs_first_at) g_rs_first_at = h->t_ms;
    if (k + 6 < g_rs_last) g_rs_fall++;
    if (k >= 130 && !g_rs_full_at) g_rs_full_at = h->t_ms;
    g_rs_last = k;
}
static void rs_watch(HostPlat *h, int ring)
{
    g_rs_ring = ring;
    g_rs_frames = g_rs_part = g_rs_fall = g_rs_last = 0;
    g_rs_t0 = h->t_ms;
    g_rs_first_at = g_rs_full_at = 0;
    h->trace_on = 1;
    h->trace_png = 0;
    h->frame_hook = rs_hook;
}
static void rs_watch3(HostPlat *h) { rs_watch(h, 3); }
static void rs_check(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    CHECK_MSG(g_rs_part >= 2 && g_rs_fall == 0, "a typed digit's ring is scribbled in over a few frames, only ever more of it (%d part-way frames, %d steps back)", g_rs_part, g_rs_fall);
    CHECK_MSG(g_rs_first_at && g_rs_full_at && g_rs_full_at - g_rs_first_at <= 180, "and quickly: full %llu ms after the first ink",
              (unsigned long long)(g_rs_full_at - g_rs_first_at));
}

// the password rings: a typed digit's ring is the empty ring with a crayon scribble under it, each scribbled differently, and nearly full
static void pin_fills_check(HostPlat *h)
{
    const Surf *f = h->last;
    int ink[6] = {0}, inside[6] = {0};
    const uint8_t *paper = f->px + ((size_t)(50 + 177) * SCREEN_W + 545 + 5 * 38) * 4; // (the empty sixth ring's middle: the card, light or dark)
    for (int i = 0; i < 6; i++) {
        int cx = 545 + i * 38, cy = 50 + 177; // (the pad's card is at y 50)
        for (int y = cy - 7; y <= cy + 7; y++)
            for (int x = cx - 7; x <= cx + 7; x++) {
                if ((x - cx) * (x - cx) + (y - cy) * (y - cy) > 49) continue;
                const uint8_t *p = f->px + ((size_t)y * SCREEN_W + x) * 4;
                inside[i]++;
                ink[i] += abs(p[0] - paper[0]) + abs(p[1] - paper[1]) + abs(p[2] - paper[2]) > 90; // (not the card: the ink)
            }
    }
    int full = 1;
    for (int i = 0; i < 5; i++) full &= ink[i] * 10 >= inside[i] * 7;
    CHECK_MSG(full, "the five typed rings are scribbled in nearly full (%d %d %d %d %d of %d)", ink[0], ink[1], ink[2], ink[3], ink[4], inside[0]);
    CHECK_MSG(ink[5] * 10 < inside[5], "and the sixth is empty (%d)", ink[5]);
    int same = 0;
    for (int i = 1; i < 5; i++) {
        int d = 0;
        for (int y = -9; y <= 9; y++)
            for (int x = -9; x <= 9; x++) {
                const uint8_t *a = f->px + ((size_t)(227 + y) * SCREEN_W + 545 + x) * 4, *b = f->px + ((size_t)(227 + y) * SCREEN_W + 545 + i * 38 + x) * 4;
                d += abs(a[0] - b[0]) > 40;
            }
        same += d < 15;
    }
    CHECK_MSG(same == 0, "each is scribbled in its own way (%d look like the first)", same);
}

static void sc_touch(void)
{
    W w;
    w_init(&w, "14-touch", 1);
    size_t packn = 0;
    unsigned char *pack = load_pack(&packn); // (NULL without the pictures: letters)
    ui_set_avatar_pack(pack, packn);
    size_t qrn = 0;
    unsigned char *qr = load_file("../data/qr.bin", &qrn);
    CHECK_MSG(qr != NULL, "data/qr.bin is there");
    ui_set_qr(qr, qrn);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    char mayaid[ID_MAX], priyaid[ID_MAX];
    seed_profile(&w, &st, "Alex", NULL, 7200, 2, NULL);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, mayaid);
    seed_profile(&w, &st, "Priya", "111111", 90000, 1, priyaid);
    HostPlat *h = &w.hp;
    hp_snap(h, "home");
    hp_tap(h, 283, 364); // Maya's tile
    hp_snap(h, "confirm");
    hp_tap(h, 790, 515); // Switch
    hp_snap(h, "ready");
    hp_tap(h, 640, 472); // OK
    hp_snap(h, "loaded");
    hp_tap(h, 100, 44); // the logo: About
    hp_snap(h, "about");
    hp_tap(h, 1180, 686); // its Back prompt
    hp_btn(h, BTN_PLUS); // Edit profile for the loaded player
    hp_snap(h, "edit-profile");
    hp_tap(h, 640, 330); // Picture and background (the first slip of the page's sheet)
    hp_snap(h, "edit-icon");
    hp_tap(h, 300, 323); // the Background row
    hp_snap(h, "background");
    hp_call(h, bg_watch);
    hp_drag(h, 300, 540, 300, 260, 14); // scroll the grid up with a finger (it settles on three rows)
    hp_wait(h, 2);
    hp_call(h, bg_check);
    hp_snap(h, "scrolled");
    hp_tap(h, 290, 222); // a colour (third column, fourth row: 20)
    hp_snap(h, "edit-icon-colored");
    hp_tap(h, 300, 445); // OK
    hp_btn(h, BTN_B);
    hp_snap(h, "home-recolored");
    hp_tap(h, 1090, 136); // Sign out
    hp_snap(h, "signed-out");
    hp_tap(h, 640, 264); // the search bar: asks what to look for (nothing typed: cancelled)
    hp_snap(h, "search-bar");
    hp_tap(h, 451, 364); // Priya: she has a password
    hp_snap(h, "password");
    hp_tap(h, 502, 309);
    hp_tap(h, 502, 309);
    hp_btn(h, BTN_B); // Back takes the last digit off
    hp_snap(h, "one-digit");
    hp_tap(h, 502, 309);
    hp_tap(h, 502, 309);
    hp_call(h, rs_watch3);
    hp_tap(h, 502, 309); // the fourth: watched as it is scribbled in
    hp_wait(h, 1);
    hp_call(h, rs_check);
    hp_tap(h, 502, 309);
    hp_snap(h, "five-digits"); // (each ring the same as an empty one, scribbled in under it, every one a little differently)
    hp_call(h, pin_fills_check);
    hp_tap(h, 502, 309);
    hp_snap(h, "confirm-priya");
    hp_tap(h, 480, 515); // Cancel
    int rc = run(&w);
    CHECK(rc == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    Profile maya;
    CHECK(find_profile(&st2, "Maya", &maya));
    CHECK_MSG(maya.color == 20, "a colour picked in a scrolled grid by touch (%d)", maya.color);
    CHECK_MSG(!strcmp(st2.state.holder, UNCLAIMED) && st2.state.wiped == 1, "Maya was loaded by touch and signed out by touch");
    w_done(&w);
    ui_set_avatar_pack(NULL, 0);
    ui_set_qr(NULL, 0);
    free(qr);
    free(pack);
}

// Scrolling the picture grid glides: one row down takes a number of frames, not one.
static void scroll_check(HostPlat *h)
{
    h->trace_on = 0;
    CHECK_MSG(h->ntrace >= 8, "moving down a row of pictures takes %d frames (it glides)", h->ntrace);
}

static void sc_gridscroll(void)
{
    if (!need_pack("gridscroll")) return;
    W w;
    w_init(&w, "15-grid-scroll", 1);
    size_t packn = 0;
    unsigned char *pack = load_pack(&packn);
    ui_set_avatar_pack(pack, packn);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_A); // load Maya
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_PLUS); // Edit profile
    hp_btn(h, BTN_A); // Picture and background
    hp_btn(h, BTN_A); // the Picture grid
    hp_btn_n(h, BTN_DOWN, 4);
    hp_snap(h, "row-4");
    hp_call(h, glide_start);
    hp_btn(h, BTN_DOWN); // row 5: the grid has to scroll
    hp_call(h, scroll_check);
    hp_snap(h, "row-5");
    hp_btn_n(h, BTN_DOWN, 6);
    hp_snap(h, "row-11");
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    int rc = run(&w);
    CHECK(rc == 0);
    w_done(&w);
    ui_set_avatar_pack(NULL, 0);
    free(pack);
}

// ---- leaning: a press that leads nowhere (Up on the top row, Left on the first tile, ...) makes the thing in focus lean that way,
// frame and all, and spring back. Each check keeps the screen as the press finds it and watches every frame of the lean: it must be
// an animation; in every frame, what is inside the changed area must be a clean shift of that screen along the way that was pressed
// (the item moves as one piece); at its furthest it is 5 px or more that way, and the cyan frame has moved as far (the item itself
// moved, not just its frame); and the last frame must be the first again (it came back, and nothing else changed, so the focus
// stayed where it was).
static Surf *g_lean_ref;
static int g_lean_min_y, g_lean_max_y; // the rows outside this band are left out of the shifted-copy check (0: no limit)
static int g_lean_dx, g_lean_dy, g_lean_frames, g_lean_moved, g_lean_torn, g_lean_shift, g_lean_frame_shift, g_lean_n, g_lean_built;
static double g_lean_err, g_lean_err0, g_lean_last;
static const char *g_lean_names[64];

static int is_accent(const uint8_t *q) // the focus frame's cyan, light or dark theme
{
    return (abs(q[0] - 0x00) < 24 && abs(q[1] - 0xB8) < 24 && abs(q[2] - 0xE6) < 24) ||
           (abs(q[0] - 0x5A) < 24 && abs(q[1] - 0xC4) < 24 && abs(q[2] - 0xDE) < 24);
}

// how far the cyan inside (x0..x1, y0..y1) moved along (dx, dy): the shift under which most of it lands on cyan again
static int accent_shift(const Surf *ref, const Surf *f, int x0, int y0, int x1, int y1, int dx, int dy)
{
    int best = 0, best_n = -1;
    for (int s = 0; s <= 14; s++) {
        int n = 0;
        for (int y = y0; y < y1; y++) {
            int ty = y + s * dy;
            if (ty < 0 || ty >= SCREEN_H) continue;
            for (int x = x0; x < x1; x++) {
                int tx = x + s * dx;
                if (tx < 0 || tx >= SCREEN_W) continue;
                if (is_accent(ref->px + ((size_t)y * SCREEN_W + x) * 4) && is_accent(f->px + ((size_t)ty * SCREEN_W + tx) * 4)) n++;
            }
        }
        if (n > best_n) {
            best_n = n;
            best = s;
        }
    }
    return best;
}

// mean difference (summed over the channels) between the reference at p and frame f at p + (sx, sy), over a rectangle
static double shifted_diff(const Surf *ref, const Surf *f, int x0, int y0, int x1, int y1, int sx, int sy)
{
    double sum = 0;
    long n = 0;
    for (int y = y0; y < y1; y += 2) {
        int ty = y + sy;
        if (ty < 0 || ty >= SCREEN_H) continue;
        for (int x = x0; x < x1; x += 2) {
            int tx = x + sx;
            if (tx < 0 || tx >= SCREEN_W) continue;
            const uint8_t *a = ref->px + ((size_t)y * SCREEN_W + x) * 4, *b = f->px + ((size_t)ty * SCREEN_W + tx) * 4;
            sum += abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]);
            n++;
        }
    }
    return n ? sum / n : 0;
}

static void lean_frame(HostPlat *h, const Surf *f)
{
    (void)h;
    g_lean_frames++;
    int x0 = SCREEN_W, y0 = SCREEN_H, x1 = -1, y1 = -1, most = 0;
    int thr = pal_light(g_pal.paper) ? 60 : 20; // (Paper night's cards are close to the night sky they move over)
    for (int y = 0; y < SCREEN_H; y++)
        for (int x = 0; x < SCREEN_W; x++) {
            const uint8_t *a = g_lean_ref->px + ((size_t)y * SCREEN_W + x) * 4, *b = f->px + ((size_t)y * SCREEN_W + x) * 4;
            int d = abs(a[0] - b[0]) + abs(a[1] - b[1]) + abs(a[2] - b[2]);
            if (d > most) most = d;
            if (d > thr) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    g_lean_last = most;
    if (x1 < 0) return;
    g_lean_moved++;
    // inside the changed area, away from its edge (the frame, its glow, and any card edge it passed over), shifted back by s: how
    // well it matches the screen before the press
    int ix0 = x0 + 16, iy0 = y0 + 16, ix1 = x1 + 1 - 16, iy1 = y1 + 1 - 16;
    if (iy0 < g_lean_min_y) iy0 = g_lean_min_y; // (a card that leans up slides over the top bar and the ghost under it, which stay put)
    if (g_lean_max_y && iy1 > g_lean_max_y) iy1 = g_lean_max_y;
    if (ix1 - ix0 < 8 || iy1 - iy0 < 8) return;
    int best = 0;
    double best_e = 1e9, e0 = 0;
    for (int s = -4; s <= 14; s++) { // (it swings back a little past its place before it settles)
        double e = shifted_diff(g_lean_ref, f, ix0, iy0, ix1, iy1, s * g_lean_dx, s * g_lean_dy);
        if (s == 0) e0 = e;
        if (e < best_e) {
            best_e = e;
            best = s;
        }
    }
    if (getenv("LEAN_DEBUG2")) // LEAN_DEBUG2=1 ./app_host lean: per frame, the changed box, the box compared and how well the best shift fits
        printf("    frame %d: changed %d,%d..%d,%d inner %d,%d..%d,%d best shift %d err %.1f e0 %.1f\n", g_lean_frames, x0, y0, x1, y1, ix0, iy0, ix1, iy1, best, best_e, e0);
    if (best_e > 0.35 * e0) g_lean_torn++; // no shift explains this frame
    if (best > g_lean_shift) {
        g_lean_shift = best;
        g_lean_err = best_e;
        g_lean_err0 = e0;
        g_lean_frame_shift = accent_shift(g_lean_ref, f, x0, y0, x1 + 1, y1 + 1, g_lean_dx, g_lean_dy);
    }
}

static void lean_watch(HostPlat *h, int dx, int dy)
{
    for (int i = 0; i < 6000 && ui_wait_erasing(); i++) ui_wait_tick(); // (WAIT_ON runs: the last of the "still working" ink is rubbed out before the screen is measured)
    if (!g_lean_ref) g_lean_ref = surf_new(SCREEN_W, SCREEN_H);
    surf_copy(g_lean_ref, h->last);
    const char *what = g_lean_n < 64 && g_lean_names[g_lean_n] ? g_lean_names[g_lean_n] : "";
    int hero_up = strstr(what, "loaded profile") != NULL; // the banner: its tape overlaps the status card, and it leans up over the top bar and the ghost under it
    g_lean_min_y = hero_up ? 150 : 0;
    g_lean_max_y = hero_up ? 196 : 0;
    g_lean_dx = dx;
    g_lean_dy = dy;
    g_lean_frames = g_lean_moved = g_lean_torn = g_lean_shift = g_lean_frame_shift = 0;
    g_lean_err = g_lean_err0 = g_lean_last = 0;
    h->ntrace = 0;
    h->trace_on = 1;
    h->trace_png = getenv("TRACE_PNG") != NULL && (!getenv("LEAN_ONLY") || atoi(getenv("LEAN_ONLY")) == g_lean_n); // TRACE_PNG=1 ./app_host lean  keeps every frame (LEAN_ONLY=18: of press 18 only)
    h->frame_hook = lean_frame;
    if (getenv("LEAN_DEBUG") && (!getenv("LEAN_ONLY") || atoi(getenv("LEAN_ONLY")) == g_lean_n)) { // LEAN_DEBUG=1 TRACE_PNG=1 ./app_host lean: the screen before each press, then its frames
        hp_save_png(h, "lean-ref");
        printf("  (lean reference: snap %d)\n", h->snaps);
    }
}

static void watch_left(HostPlat *h) { lean_watch(h, -1, 0); }
static void watch_right(HostPlat *h) { lean_watch(h, 1, 0); }
static void watch_up(HostPlat *h) { lean_watch(h, 0, -1); }
static void watch_down(HostPlat *h) { lean_watch(h, 0, 1); }

static void lean_check(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    int i = g_lean_n++;
    const char *what = i < 64 && g_lean_names[i] ? g_lean_names[i] : "?";
    printf("  lean %2d %-32s %2d frames, item %2d px (error %4.1f moved, %5.1f in place), frame %2d px\n", i, what, g_lean_frames,
           g_lean_shift, g_lean_err, g_lean_err0, g_lean_frame_shift);
    CHECK_MSG(g_lean_moved >= 4 && g_lean_frames >= 8 && g_lean_frames <= 40, "%s: an animation (%d frames, %d moved)", what, g_lean_frames, g_lean_moved);
    CHECK_MSG(g_lean_shift >= 5, "%s: it leans the way that was pressed (%d px)", what, g_lean_shift);
    CHECK_MSG(g_lean_err < 0.35 * g_lean_err0, "%s: the item itself moved (error %.1f shifted, %.1f not)", what, g_lean_err, g_lean_err0);
    CHECK_MSG(g_lean_torn == 0, "%s: it moves as one piece (%d of %d frames are not a clean shift)", what, g_lean_torn, g_lean_moved);
    // (Washi has no frame: focus is a lift, on the home screen, the hub, the lists, the order menu, the dialogs and the keyboards)
    int paper = !strncmp(what, "home", 4) || !strncmp(what, "hub", 3) || !strncmp(what, "list", 4) || !strncmp(what, "order", 5) ||
                !strncmp(what, "switch dialog", 13) || !strncmp(what, "keyboard", 8) || !strncmp(what, "message", 7) || !strncmp(what, "pad", 3) ||
                !strncmp(what, "Edit Icon", 9) || !strncmp(what, "pictures", 8) || !strncmp(what, "storage", 7) || !strncmp(what, "wheels", 6) ||
                !strncmp(what, "backups", 7);
    if (!paper)
        CHECK_MSG(abs(g_lean_frame_shift - g_lean_shift) <= 1, "%s: the frame moved with it (%d px, the item %d)", what, g_lean_frame_shift, g_lean_shift);
    CHECK_MSG(g_lean_last <= 3, "%s: it comes back and nothing else changed (last frame off by %.0f)", what, g_lean_last);
}

// press toward nothing and check the lean
static void lean_press(HostPlat *h, uint64_t btn, const char *what)
{
    if (g_lean_built < 64) g_lean_names[g_lean_built] = what;
    g_lean_built++;
    hp_call(h, btn == BTN_LEFT ? watch_left : btn == BTN_RIGHT ? watch_right : btn == BTN_UP ? watch_up : watch_down);
    if (btn == BTN_UP || btn == BTN_DOWN) hp_btn_rep(h, btn); // (a plain tap at the end of a menu wraps; the lean is for a held press)
    else hp_btn(h, btn);
    hp_call(h, lean_check);
}

// ---- Washi's lists (the admin's Profiles page: 12 profiles, slips every 84 px from y 204, five show). The focused slip is unstuck: lifted,
// its tape peeling, and it sticks back when the focus leaves; a held D-pad does it lighter and quicker; a jump to a new page snaps and the
// slips settle in a wave (not on a held D-pad).
typedef struct {
    int x, y, w, h;
    uint32_t last;
    int changes;
} LsWatch;

static LsWatch g_lw[2];
static int g_ls_frames, g_ls_ms_full, g_ls_ms_held, g_ls_max_off;
extern int g_ui_list_off; // (ui_list.c: how far out of line the slips were in the frame last made)
static uint64_t g_ls_t0;
static uint32_t g_ls_prev_hash;

static uint32_t region_hash(const Surf *f, int x0, int y0, int w, int hgt)
{
    uint32_t k = 2166136261u;
    for (int y = y0; y < y0 + hgt; y++)
        for (int x = x0; x < x0 + w; x++) {
            const uint8_t *p = f->px + ((size_t)y * SCREEN_W + x) * 4;
            k = (k ^ p[0]) * 16777619u;
            k = (k ^ p[1]) * 16777619u;
            k = (k ^ p[2]) * 16777619u;
        }
    return k;
}

// the tape's corner of slip number `row` (rows of 84 from y 204)
static void ls_tape_region(LsWatch *w, const Surf *f, int row)
{
    w->x = 190;
    w->y = 204 + row * 84 - 24;
    w->w = 90;
    w->h = 56;
    w->last = region_hash(f, w->x, w->y, w->w, w->h);
    w->changes = 0;
}

static void ls_frame(HostPlat *h, const Surf *f)
{
    (void)h;
    g_ls_frames++;
    if (g_ui_list_off > g_ls_max_off) g_ls_max_off = g_ui_list_off;
    for (int i = 0; i < 2; i++) {
        uint32_t k = region_hash(f, g_lw[i].x, g_lw[i].y, g_lw[i].w, g_lw[i].h);
        if (k != g_lw[i].last) {
            g_lw[i].changes++;
            g_lw[i].last = k;
        }
    }
}

static int g_ls_old_row, g_ls_new_row;

static void ls_start(HostPlat *h)
{
    ls_tape_region(&g_lw[0], h->last, g_ls_old_row);
    ls_tape_region(&g_lw[1], h->last, g_ls_new_row);
    g_ls_frames = 0;
    g_ls_t0 = h->t_ms;
    h->ntrace = 0;
    h->trace_on = 1;
    h->frame_hook = ls_frame;
}

static void ls_start_0_1(HostPlat *h) { g_ls_old_row = 0; g_ls_new_row = 1; ls_start(h); }
static void ls_start_1_2(HostPlat *h) { g_ls_old_row = 1; g_ls_new_row = 2; ls_start(h); }

static int ls_stop(HostPlat *h)
{
    h->trace_on = 0;
    h->frame_hook = NULL;
    return h->ntrace ? (int)(h->trace_t[h->ntrace - 1] - g_ls_t0) : 0;
}

static void ls_check_full(HostPlat *h)
{
    int ms = ls_stop(h);
    g_ls_ms_full = ms;
    printf("  list, a press: %d frames, %d ms, old tape %d changes, new tape %d changes\n", g_ls_frames, ms, g_lw[0].changes, g_lw[1].changes);
    CHECK_MSG(g_ls_frames >= 6 && g_ls_frames <= 40, "the unstick and the stick back are an animation (%d frames)", g_ls_frames);
    CHECK_MSG(g_lw[1].changes >= 3, "the new slip's tape peels over several frames (%d changes)", g_lw[1].changes);
    CHECK_MSG(g_lw[0].changes >= 3, "the old slip sticks back over several frames (%d changes)", g_lw[0].changes);
    CHECK_MSG(ms >= 100 && ms <= 450, "it takes a moment (%d ms)", ms);
}

static void ls_check_held(HostPlat *h)
{
    int ms = ls_stop(h);
    g_ls_ms_held = ms;
    printf("  list, a held press: %d frames, %d ms\n", g_ls_frames, ms);
    CHECK_MSG(g_lw[1].changes >= 2, "a held press still moves the tape (%d changes)", g_lw[1].changes);
    CHECK_MSG(ms + 15 <= g_ls_ms_full, "a held D-pad is lighter and quicker (%d ms against %d)", ms, g_ls_ms_full);
}

static void ls_scroll_start(HostPlat *h)
{
    g_ls_frames = 0;
    g_ls_max_off = 0;
    h->trace_png = getenv("TRACE_PNG") != NULL; // TRACE_PNG=1 ./app_host lists  keeps every frame of the jumps
    g_ls_t0 = h->t_ms;
    g_ls_prev_hash = region_hash(h->last, 620, 168, 40, 14); // where the arrow for "more above" goes
    h->ntrace = 0;
    h->trace_on = 1;
    h->frame_hook = ls_frame;
    g_lw[0].x = g_lw[1].x = 200;
    g_lw[0].y = g_lw[1].y = 204;
    g_lw[0].w = g_lw[1].w = 880;
    g_lw[0].h = g_lw[1].h = 84;
    g_lw[0].last = g_lw[1].last = region_hash(h->last, 200, 204, 880, 84);
    g_lw[0].changes = g_lw[1].changes = 0;
}

static void ls_scroll_check(HostPlat *h)
{
    ls_stop(h);
    uint32_t after = region_hash(h->last, 620, 168, 40, 14);
    printf("  list, jumping a row: %d frames, the top slip's band changed %d times, slips up to %d px out of line\n", g_ls_frames, g_lw[0].changes,
           g_ls_max_off);
    CHECK_MSG(g_ls_frames >= 6, "the slips settle over several frames (%d frames)", g_ls_frames);
    CHECK_MSG(g_ls_max_off >= 6 && g_ls_max_off <= 12, "the list snaps to its new page, the slips a few px out of line to begin with, not a row (%d px)", g_ls_max_off);
    CHECK_MSG(g_lw[0].changes >= 4, "the top slip settles over several frames (%d changes)", g_lw[0].changes);
    CHECK_MSG(after != g_ls_prev_hash, "the arrow for more above the sheet appears");
}

static void ls_held_scroll_check(HostPlat *h)
{
    ls_stop(h);
    printf("  list, a held press that jumps a row: %d frames, slips up to %d px out of line\n", g_ls_frames, g_ls_max_off);
    CHECK_MSG(g_ls_max_off == 0, "a held D-pad snaps with no wave (it would never stop) (%d px)", g_ls_max_off);
}

static void sc_lists(void)
{
    W w;
    w_init(&w, "27-lists", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    static const char *names[] = {"Alex", "Ben", "Chloe", "Dani", "Eli", "Fatima", "Gus", "Hana", "Ines", "Jordan", "Kai", "Lena"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) seed_profile(&w, &st, names[i], NULL, (int)(i * 3600 + 90), (int)(i % 3) + 1, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_open(h, &hub, HUB_PROFILES);
    hp_wait(h, 3);
    hp_snap(h, "profiles-first");
    hp_call(h, ls_start_0_1);
    hp_btn(h, BTN_DOWN);
    hp_call(h, ls_check_full);
    hp_snap(h, "second-slip-lifted");
    hp_call(h, ls_start_1_2);
    hp_btn_rep(h, BTN_DOWN); // a held D-pad's repeat
    hp_call(h, ls_check_held);
    hp_btn(h, BTN_UP);
    hp_btn(h, BTN_DOWN);
    hp_in(h, 3); // two presses in a row, the second in the middle of the first's animation
    hp_btn(h, BTN_DOWN);
    hp_in(h, 2);
    hp_snap(h, "overlapping-presses");
    hp_btn(h, BTN_DOWN); // the fifth slip; the next one scrolls the sheet
    hp_call(h, ls_scroll_start);
    hp_btn(h, BTN_DOWN);
    hp_call(h, ls_scroll_check);
    hp_snap(h, "scrolled");
    hp_call(h, ls_scroll_start);
    hp_btn_rep(h, BTN_DOWN); // held: the next row, the list jumps again, but with no wave
    hp_call(h, ls_held_scroll_check);
    hp_btn_n(h, BTN_DOWN, 5);
    hp_snap(h, "near-the-end");
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    int rc = run(&w);
    CHECK(rc == 0);
    w_done(&w);
}

// ---- About (ui_about.c): the club's code, read off data/qr.bin and coloured in with wax crayon, filling most of its print
extern int g_ui_about_qr;

// a pixel of the code's wax: dark, and violet (the blue is the most, then the red, the green the least; the picture's own ink blue has more green than red)
static int is_wax(const Surf *f, int x, int y)
{
    const uint8_t *p = &f->px[((size_t)y * SCREEN_W + x) * 4];
    return p[0] < 150 && p[1] <= p[0] && p[2] > p[0];
}

static void about_check(HostPlat *h)
{
    CHECK_MSG(g_ui_about_qr == 29, "the club's code was read off its picture: 29 modules to a side (%d)", g_ui_about_qr);
    // the print is the 280 px square at (844, 172); wax-coloured pixels in it are the code (read 8 px in from its edge: it is turned a little, and Paper night's
    // sheet round it is as dark and violet as the wax; the code's quiet margin is wider than that)
    int minx = 9999, maxx = -1, miny = 9999, maxy = -1;
    long ink = 0;
    for (int y = 172 + 8; y < 452 - 8; y++)
        for (int x = 844 + 8; x < 1124 - 8; x++)
            if (is_wax(h->last, x, y)) {
                ink++;
                if (x < minx) minx = x;
                if (x > maxx) maxx = x;
                if (y < miny) miny = y;
                if (y > maxy) maxy = y;
            }
    CHECK_MSG(ink > 15000, "there is a code on the print (%ld wax pixels)", ink);
    CHECK_MSG(maxx - minx >= 218 && maxx - minx <= 240 && maxy - miny >= 218 && maxy - miny <= 240, "and it fills most of the print: %d x %d of 280", maxx - minx + 1, maxy - miny + 1);
    CHECK_MSG(abs((minx + maxx) / 2 - 984) <= 4 && abs((miny + maxy) / 2 - 312) <= 4, "centred on it (%d, %d)", (minx + maxx) / 2, (miny + maxy) / 2);
    // the maker's mark and name sit left of the version at the bottom: grey pixels where the GitHub mark is
    long grey = 0;
    for (int y = 584; y < 606; y++)
        for (int x = 920; x < 945; x++) grey += px_near(h->last, x, y, g_pal.text_soft, 60);
    CHECK_MSG(grey > 60, "the GitHub mark is there, small and grey (%ld pixels)", grey);
}

// ---- Updates (This app, Check for updates): the page's looks in each state (ui_update_preview); on the console the row does nothing yet
static int g_up_state;
static void up_next(HostPlat *h)
{
    (void)h;
    g_ui_update_preview = g_up_state++;
}
static void up_off(HostPlat *h)
{
    (void)h;
    g_ui_update_preview = -1;
}

static void sc_update_mock(void)
{
    static const char *const NAMES[6] = {"0-checking", "1-up-to-date", "2-available", "3-downloading", "4-ready", "5-offline"};
    W w;
    w_init(&w, "52-update", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    g_up_state = 0;
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hp_call(h, up_off);
    hub_open(h, &hub, HUB_UPDATE); // nothing happens: still the hub
    hp_snap(h, "hub-this-app");
    for (int i = 0; i < 6; i++) {
        hp_call(h, up_next);
        hp_btn(h, BTN_A);
        hp_snap(h, NAMES[i]);
        hp_btn(h, BTN_B);
    }
    hp_call(h, up_off);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_up_state == 6, "every state of the page was shown (%d)", g_up_state);
    g_ui_update_preview = -1;
    w_done(&w);
    g_w = NULL;
}

// ---- Check for updates for real, against a pretend GitHub (platform_host.c): each answer shows its page; a newer release downloads, is checked and put
// in place, and Restart now closes the app for the new one; a download that is not whole is turned away and nothing is installed
extern int g_ui_update_last;
static const int UP_SEQ[] = {HP_NET_MISSING, HP_NET_OFFLINE, HP_NET_HTTPERR, HP_NET_LATEST, HP_NET_BADDL, HP_NET_NEWER};
static const int UP_WANT[] = {6, 5, 7, 1, 7, 4}; // (ui_update.c's UP_MISSING, UP_OFFLINE, UP_ERROR, UP_LATEST, UP_ERROR after the bad download, UP_READY)
static int g_up_i, g_up_seen[6];
static void up_net(HostPlat *h) { h->net = UP_SEQ[g_up_i]; }
static void up_seen(HostPlat *h)
{
    (void)h;
    g_up_seen[g_up_i++] = g_ui_update_last;
}

static void sc_update_real(void)
{
    W w;
    w_init(&w, "53-update-real", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    g_up_i = 0;
    memset(g_up_seen, -1, sizeof g_up_seen);
    g_ui_update_preview = -1;
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    static const char *const SNAP[6] = {"missing", "offline", "http-error", "up-to-date", "bad-download", "ready"};
    for (int i = 0; i < 6; i++) {
        hp_call(h, up_net);
        if (i == 0) hub_open(h, &hub, HUB_UPDATE);
        else hp_btn(h, BTN_A); // (Check for updates again: the hub kept its place)
        if (UP_SEQ[i] == HP_NET_NEWER || UP_SEQ[i] == HP_NET_BADDL) hp_btn(h, BTN_A); // Update: it downloads
        hp_snap(h, SNAP[i]);
        hp_call(h, up_seen);
        if (i < 5) hp_btn(h, BTN_B); // back to the hub
    }
    hp_btn(h, BTN_A); // Restart now: the app closes for the new one
    CHECK(run(&w) == 0);
    for (int i = 0; i < 6; i++) CHECK_MSG(g_up_seen[i] == UP_WANT[i], "answer %d shows its page (%d, wanted %d)", i, g_up_seen[i], UP_WANT[i]);
    CHECK_MSG(h->installs == 1 && h->installed_len == 4096, "only the whole download is installed (%d installs, %zu bytes)", h->installs, h->installed_len);
    CHECK_MSG(h->restarts == 1, "Restart now asks for the new one to open (%d)", h->restarts);
    w_done(&w);
    g_w = NULL;
}

// ---- Going back (trial.c on the console): a start that came back from a newer version that never got going says so over the home screen; the home screen up
// 2 s marks this version as going; Y on the update page goes back to the version kept from before, asked first (Cancel keeps the page), then restarts on it
static void sc_update_back(void)
{
    W w;
    w_init(&w, "54-update-back", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    HostPlat *h = &w.hp;
    h->rolled_ver = "1.1.0";
    h->kept_ver = "1.0.0";
    h->net = HP_NET_LATEST;
    HubPos hub = {0, 0, 0};
    g_ui_update_preview = -1;
    hp_snap(h, "rolled-back"); // the note over the home screen
    hp_btn(h, BTN_A);
    hp_wait(h, 3); // the home screen up a while
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_open(h, &hub, HUB_UPDATE);
    hp_snap(h, "up-to-date-y");
    hp_btn(h, BTN_Y);
    hp_snap(h, "go-back-ask");
    hp_btn(h, BTN_B); // Cancel: the page again
    hp_btn(h, BTN_Y);
    hp_btn(h, BTN_A); // Go back: the kept one opens next
    CHECK(run(&w) == 0);
    CHECK_MSG(h->rolled_ver == NULL, "the start asks once whether it came back from a newer version");
    CHECK_MSG(h->started_oks == 1, "the home screen up a while marks this version as going, once (%d)", h->started_oks);
    CHECK_MSG(h->roll_backs == 1 && h->restarts == 1, "Y, then Go back, puts the kept version back and restarts on it (%d, %d)", h->roll_backs, h->restarts);
    w_done(&w);
    g_w = NULL;
}

static void sc_about(void)
{
    W w;
    w_init(&w, "44-about", 1);
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    seed_profile(&w, &st, "Maya", NULL, 600, 2, NULL);
    size_t qn = 0;
    unsigned char *qr = load_file("../data/qr.bin", &qn);
    CHECK_MSG(qr && qn > 1000, "the club's code is on disk (%zu bytes)", qn);
    ui_set_qr(qr, qn);
    // the code coloured in beforehand: data/qr_wax.bin must be what the page would make itself (QR_WAX_WRITE=1 writes it, after the wax or the code changes)
    size_t wn = 0, sn = 0;
    uint8_t *fresh = ui_about_wax_blob(qr, qn, &wn);
    CHECK_MSG(fresh != NULL, "the page's colouring of the code can be made (%zu bytes)", wn);
    unsigned char *stored = load_file("../data/qr_wax.bin", &sn);
    if (fresh && getenv("QR_WAX_WRITE")) {
        FILE *f = fopen("../data/qr_wax.bin", "wb");
        if (f) {
            fwrite(fresh, 1, wn, f);
            fclose(f);
            printf("  wrote data/qr_wax.bin (%zu bytes)\n", wn);
        }
        free(stored);
        stored = load_file("../data/qr_wax.bin", &sn);
    }
    CHECK_MSG(stored && fresh && sn == wn && !memcmp(stored, fresh, wn), "data/qr_wax.bin is the code coloured in (QR_WAX_WRITE=1 makes it again)");
    ui_set_qr_wax(stored, sn);
    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hub_open(h, &hub, HUB_ABOUT);
    hp_snap(h, "about");
    hp_call(h, about_check);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    hp_btn(h, BTN_B);
    int rc = run(&w);
    CHECK(rc == 0);
    ui_set_qr(NULL, 0);
    ui_set_qr_wax(NULL, 0);
    free(fresh);
    free(stored);
    free(qr);
    w_done(&w);
}

// The pictures go round the ends of their rows and columns instead of leaning there (the user asked for it, on a press and on a hold): on the
// console's card, the letter and then every picture, nine to a row
static void grid_at_last(HostPlat *h)
{
    (void)h;
    int rowend = skin_avatar_count() < 8 ? skin_avatar_count() : 8; // (with no pictures at all, the row is the letter alone)
    CHECK_MSG(g_ui_extra_pos == 0 && g_ui_extra_sel == rowend, "pictures: Left (held) on the first goes round to the end of its row (%d)", g_ui_extra_sel);
}
static void grid_at_first(HostPlat *h)
{
    (void)h;
    CHECK_MSG(g_ui_extra_sel == 0, "pictures: Right on the row's end goes round to its start (%d)", g_ui_extra_sel);
}
static void grid_up_round(HostPlat *h)
{
    (void)h;
    int n = skin_avatar_count() + 1, last = (n - 1) / 9, want = last * 9 + 1 < n ? last * 9 + 1 : (last - 1) * 9 + 1;
    if (n == 1) want = 0; // (the letter alone)
    CHECK_MSG(g_ui_extra_sel == want, "pictures: Up from the top row goes round to the bottom of its column (%d, wanted %d)", g_ui_extra_sel, want);
}

static void sc_lean(void)
{
    W w;
    reminder_world(&w, "26-lean", 0); // Maya's saves are loaded; Priya is the other profile
    w.hp.keyboard_available = 0;      // the on-screen keyboard
    size_t packn = 0;
    unsigned char *pack = load_pack(&packn);
    ui_set_avatar_pack(pack, packn);
    g_lean_n = g_lean_built = 0;
    HostPlat *h = &w.hp;
    // home: the grid is one row (Maya, Priya, New profile)
    lean_press(h, BTN_LEFT, "home, first tile, Left");
    lean_press(h, BTN_DOWN, "home, one row, Down");
    hp_btn_n(h, BTN_RIGHT, 2);
    lean_press(h, BTN_RIGHT, "home, New profile, Right");
    hp_btn(h, BTN_UP); // the search bar, above New profile
    lean_press(h, BTN_LEFT, "home, search bar, Left");
    hp_btn(h, BTN_RIGHT);
    lean_press(h, BTN_RIGHT, "home, order chip, Right");
    hp_btn(h, BTN_UP); // Sign out, above the chip
    lean_press(h, BTN_RIGHT, "home, Sign out, Right");
    lean_press(h, BTN_UP, "home, Sign out, Up");
    hp_btn(h, BTN_LEFT);
    hp_wait(h, 60); // (the card finishes lifting, wiggle and all, before the lean is measured)
    lean_press(h, BTN_UP, "home, loaded profile, Up");
    lean_press(h, BTN_LEFT, "home, loaded profile, Left");
    // (the order menu is the sleeve: no list to lean; at its ends the sticker out is pushed in a hair, ui_stickfx.c)
    hp_btn(h, BTN_DOWN);  // the search bar
    hp_btn(h, BTN_RIGHT); // the chip
    // a confirm: switch to Priya
    hp_btn(h, BTN_DOWN); // the grid, on New profile
    hp_btn(h, BTN_LEFT); // Priya
    hp_btn(h, BTN_A);
    lean_press(h, BTN_LEFT, "switch dialog, Left");
    lean_press(h, BTN_DOWN, "switch dialog, Down");
    hp_btn(h, BTN_B);
    // the keyboard (a new profile), then a message: a space is not a name
    hp_btn(h, BTN_RIGHT); // New profile
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_UP); // the digits
    lean_press(h, BTN_UP, "keyboard, top row, Up");
    hp_btn_n(h, BTN_LEFT, 4);
    lean_press(h, BTN_LEFT, "keyboard, first key, Left");
    hp_btn_n(h, BTN_DOWN, 4); // Shift
    lean_press(h, BTN_DOWN, "keyboard, bottom row, Down");
    hp_btn(h, BTN_RIGHT); // space
    hp_btn(h, BTN_A);
    hp_btn(h, BTN_PLUS); // done
    hp_snap(h, "not-a-name");
    lean_press(h, BTN_UP, "message, Up");
    hp_btn(h, BTN_A);
    hp_snap(h, "home-again");
    // Edit profile (a list), Edit Icon and the pictures
    hp_btn(h, BTN_PLUS);
    lean_press(h, BTN_UP, "list, first row, Up");
    lean_press(h, BTN_LEFT, "list, first row, Left");
    hp_btn(h, BTN_A); // Picture and background
    hp_snap(h, "edit-icon");
    lean_press(h, BTN_UP, "Edit Icon, Picture, Up");
    lean_press(h, BTN_RIGHT, "Edit Icon, Picture, Right");
    hp_btn(h, BTN_A); // the pictures: on the first, the letter
    hp_btn_rep(h, BTN_LEFT); // the first goes round to its row's end, held or not
    hp_call(h, grid_at_last);
    hp_btn(h, BTN_RIGHT);
    hp_call(h, grid_at_first);
    hp_btn(h, BTN_RIGHT); // the second picture: Up from the top row goes round to the bottom of its column
    hp_btn(h, BTN_UP);
    hp_call(h, grid_up_round);
    hp_btn(h, BTN_B);
    hp_btn_n(h, BTN_DOWN, 2);
    lean_press(h, BTN_DOWN, "Edit Icon, OK, Down");
    hp_btn(h, BTN_B);
    hp_btn_n(h, BTN_DOWN, 2);
    lean_press(h, BTN_DOWN, "list, last row, Down");
    hp_btn(h, BTN_B);
    // the admin password pad, from 5
    hp_btn(h, BTN_ADMIN);
    hp_btn(h, BTN_UP);
    lean_press(h, BTN_UP, "pad, top row, Up");
    hp_btn(h, BTN_LEFT);
    lean_press(h, BTN_LEFT, "pad, first column, Left");
    hp_btn(h, BTN_RIGHT);
    hp_btn(h, BTN_DOWN);
    hp_pin(h, "123456");
    // the hub
    lean_press(h, BTN_UP, "hub, first section, Up");
    lean_press(h, BTN_LEFT, "hub, section, Left");
    hp_btn_n(h, BTN_DOWN, 3);
    lean_press(h, BTN_DOWN, "hub, last section, Down");
    hp_btn_n(h, BTN_UP, 3);
    hp_btn(h, BTN_A);
    hp_snap(h, "hub-rows");
    lean_press(h, BTN_UP, "hub, first row, Up");
    lean_press(h, BTN_RIGHT, "hub, row, Right");
    hp_btn_n(h, BTN_DOWN, HUB_PEOPLE_ROWS - 1);
    lean_press(h, BTN_DOWN, "hub, last row, Down");
    // Storage: the profiles on the right, the actions on the left
    HubPos hub = {0, HUB_PEOPLE_ROWS - 1, 1};
    hub_open(h, &hub, HUB_STORAGE);
    lean_press(h, BTN_UP, "storage, first profile, Up");
    lean_press(h, BTN_RIGHT, "storage, profile, Right");
    hp_btn(h, BTN_LEFT);
    lean_press(h, BTN_LEFT, "storage, action, Left");
    lean_press(h, BTN_UP, "storage, first action, Up");
    hp_btn(h, BTN_B);
    // the wheels (Console, Sign-out reminder)
    hub_open(h, &hub, HUB_REMINDER);
    lean_press(h, BTN_LEFT, "wheels, first wheel, Left");
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    g_paper_board_flat = 1; // (a rigid shift is what is measured, and the board's flecks are not the same under one)
    CHECK(run(&w) == 0);
    g_paper_board_flat = 0;
    CHECK_MSG(g_lean_n == g_lean_built, "every lean was looked at (%d of %d)", g_lean_n, g_lean_built);
    w_done(&w);
    g_w = NULL;
    ui_set_avatar_pack(NULL, 0);
    free(pack);
}

// The pictures in the README and docs (docs/design/gen.py --render). One cast throughout, with letter avatars (the guest has
// its real picture): Nia, Uma, Eli, Sam and a few more. Uma is loaded; Sam picks their name next (Uma's saves are backed up, then Sam's are loaded);
// Sam signs out with X; then Admin's People section and the Backups page, whose automatic backups name the cast.
static void sc_readme_shots(void)
{
    const char *matte = getenv("MATTE"); // MATTE=black or MATTE=white: the board is plain black or white (the intro mockups get the alpha of the Home screen from the two)
    if (matte) g_paper_board_flat = !strcmp(matte, "black") ? 2 : 3;
    if (getenv("TAPE")) g_skin_mark_tape = 1; // (the seal with the tapes it used to have; the app has none)
    if (getenv("NOMARK")) g_skin_no_mark = 1; // (the intro mockups draw the corner mark and the name themselves)
    W w;
    w_init(&w, "40-readme-shots", 1);
    g_w = &w;
    size_t packn = 0;
    unsigned char *pack = load_pack(&packn);
    ui_set_avatar_pack(pack, packn); // the guest shows its picture, as on the console
    fake_put_n(&w.fb, T_MK, "ghosts.bin", 24 * 1024 * 1024, "eli's ghosts"); // a big save, so the bar is caught part-way
    w.fb.free_bytes = 13314398618ull; // 12.4 GB
    Store st;
    store_open(&st, &w.fb.be, w.fb.sd);
    seed_config(&w, &st, 1, 1);
    static const char *const names[] = {"Nia", "Uma", "Eli", "Sam", "Ava", "Jun", "Kai", "Leo", "Mira", "Theo"};
    static const int ages[] = {3600 * 5, 2700, 86400 + 3600, 86400 * 2, 3600 * 9, 86400 * 3, 3600 * 26, 86400 * 4, 3600 * 3, 86400 * 6};
    char loadedid[ID_MAX] = ""; // Eli (Wind Waker Zelda) is loaded; Sam (Toadette) picks their name next
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        char id[ID_MAX];
        seed_profile(&w, &st, names[i], NULL, ages[i], (int)(i % 3) + 1, id);
        if (!strcmp(names[i], "Eli")) snprintf(loadedid, sizeof loadedid, "%s", id);
    }
    // each person's colour and picture, the same in every README picture. The four of the banner keep Northwestern purple and blue
    // (swatches.h): N #E4CCFE (48) Judd, U #D3A9FF (49) Li'l Judd, E #B56BFE (50) Wind Waker Zelda, S #80BDFE (43) Toadette. The rest
    // of the grid is the console's full rainbow. `pos` is the picture's place in the picture grid (skin_avatar_at gives its id).
    static const struct { int swatch, pos; } look[] = {{48, 110}, {49, 111}, {50, 59}, {43, 29}, {37, 52}, {6, 8}, {12, 3}, {30, 4}, {24, 71}, {54, 69}};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; i++) {
        Profile p;
        if (find_profile(&st, names[i], &p)) {
            p.color = look[i].swatch; // (in the order of names[])
            p.picture = skin_avatar_at(look[i].pos);
            store_profile_save(&st, &p);
        }
    }
    snprintf(st.state.holder, sizeof st.state.holder, "%s", loadedid);
    st.state.loaded_at = (int64_t)w.now0 - 2700;
    store_save_state(&st);
    Locker lk;
    lk_init(&lk, &w.fb.be, &st);
    char parent[FSX_PATH], out[FSX_PATH];
    snprintf(parent, sizeof parent, "%s/../Washi Backups", w.fb.sd);
    static const char *const manual[] = {"09-24-2026--20-10", "09-28-2026--19-05", "10-01-2026--21-05"};
    static const char *const autos[] = {"09-29-2026--17-40", "09-30-2026--18-15", "10-01-2026--16-50"};
    static const char *const who[] = {"Uma", "Nia", "Ava"};
    for (size_t i = 0; i < 3; i++) CHECK(lk_snapshot(&lk, parent, manual[i], out, sizeof out) == LK_OK);
    for (size_t i = 0; i < 3; i++) CHECK(lk_snapshot_auto(&lk, parent, autos[i], who[i], 9, out, sizeof out) == LK_OK);
    lk_free(&lk);

    HostPlat *h = &w.hp;
    HubPos hub = {0, 0, 0};
    hp_btn(h, BTN_DOWN); // Sam, the first tile of the second row
    hp_snap(h, "home"); // Eli loaded, the frame on Sam, who picks their name next
    hp_btn(h, BTN_A);
    hp_snap(h, "confirm");
    hp_snap_after(h, 5, "swap"); // about half way through backing up Eli's big save
    hp_btn(h, BTN_A); // Switch
    hp_snap(h, "ready");
    hp_btn(h, BTN_A);
    hp_snap(h, "sam-loaded");
    hp_btn(h, BTN_X); // Done playing
    hp_snap(h, "signed-out"); // the guest home, with "Sam's saves are backed up."
    hp_btn(h, BTN_ADMIN);
    hp_pin(h, "123456");
    hp_snap(h, "admin-people");
    hub_section(h, &hub, HS_GAMES);
    hub_open(h, &hub, HUB_BACKUP);
    hp_snap(h, "backups");
    hp_btn(h, BTN_B);
    hub_leave(h, &hub);
    CHECK(run(&w) == 0);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(!strcmp(st2.state.holder, UNCLAIMED) && st2.state.wiped == 1, "Sam signed out and the console was cleared");
    ui_set_avatar_pack(NULL, 0);
    free(pack);
    w_done(&w);
    g_w = NULL;
    g_paper_board_flat = 0;
    g_skin_no_mark = 0;
}

// The app used to be called Save Locker: its folders are renamed on the first start, never merged or overwritten.
static void sc_adopt_names(void)
{
    const char *tmp = "/tmp/washi-adopt-test";
    char root[200], old_root[200], old_b[200], new_b[200], p[FSX_PATH];
    fsx_rmtree(tmp);
    snprintf(root, sizeof root, "%s/switch/washi", tmp);
    snprintf(old_root, sizeof old_root, "%s/switch/save-locker", tmp);
    snprintf(old_b, sizeof old_b, "%s/Save Locker Backups", tmp);
    snprintf(new_b, sizeof new_b, "%s/Washi Backups", tmp);
    snprintf(p, sizeof p, "%s/profiles/uma-1234", old_root);
    CHECK(fsx_mkdir_p(p) == 0);
    snprintf(p, sizeof p, "%s/profiles/uma-1234/profile.json", old_root);
    CHECK(fsx_write_file(p, "{}", 2) == 0);
    snprintf(p, sizeof p, "%s/05-10-2026--21-00", old_b);
    CHECK(fsx_mkdir_p(p) == 0);
    app_adopt_old_names(root);
    snprintf(p, sizeof p, "%s/profiles/uma-1234/profile.json", root);
    CHECK_MSG(fsx_exists(p) && !fsx_exists(old_root), "the old data folder is now the new one");
    snprintf(p, sizeof p, "%s/05-10-2026--21-00", new_b);
    CHECK_MSG(fsx_exists(p) && !fsx_exists(old_b), "the old backups folder is now the new one");
    // a second start changes nothing; and if the new name is already taken the old folder is left alone, not merged
    CHECK(fsx_mkdir_p(old_root) == 0 && fsx_mkdir_p(old_b) == 0);
    app_adopt_old_names(root);
    CHECK_MSG(fsx_exists(old_root) && fsx_exists(old_b), "nothing is merged or overwritten");
    fsx_rmtree(tmp);
}

#include "intro_assets.c"

// ---- The intro (ui_intro.c), as the console plays it before the first home screen of a start: washi laid over the black by rolls, the card the logo is made on, the
// wall torn off row by row and the home screen coming in piece by piece. Frames drawn at set times are saved (intro-<ms>.png). It starts on black and once the wall is
// down no black shows; the music and the tape's sounds are asked for; it ends on the very frame the home screen starts with. Any button skips it until the card's seal starts coming
// down, the stick never does (that press goes no further; the seal comes down at once, the stamp's chord with it), and "is this still you?" is asked after it, over the home screen. INTRO_SEED=<n>: another start's wall (7 by default).
extern int g_ui_intro_ncards, g_ui_intro_frames, g_ui_intro_played, g_ui_intro_skip_frames, g_ui_intro_skip_fall, g_ui_intro_skip_down, g_ui_intro_late;
extern float g_ui_intro_third_at;
extern uint32_t g_ui_intro_seed;
extern void (*g_ui_intro_shot)(const Surf *frame, float t);
extern const float *g_ui_intro_times;
extern int g_ui_intro_ntimes;

static const float INTRO_T[] = {0.05f, 0.25f, 0.45f, 0.65f, 0.85f, 1.05f, 1.25f, 1.40f, 1.55f, 1.70f, 1.85f, 2.10f,
                                2.36f, 2.48f, 2.58f, 2.68f, 2.80f, 2.95f, 3.10f, 3.30f, 3.50f, 3.70f, 3.78f};
enum { NINTRO_T = sizeof INTRO_T / sizeof INTRO_T[0] };
static long g_intro_black[NINTRO_T];
static int g_intro_shots;
static Surf *g_intro_end; // the intro's own last frame
extern int g_ui_intro_card;
int ui_intro_pick(uint32_t seed, int n);
// the cards main.c gives the console (read from data/ here), and where each has the logo
static const struct { const char *key; float lx, ly; } INTRO_CARDS[4] = {{"a4", -42, -46}, {"d2", 0, -50}, {"f1", 0, -36}, {"f2", 0, -36}};
static uint8_t *g_intro_card[4];
static size_t g_intro_card_n[4];

static void intro_shot(const Surf *f, float t)
{
    int i = g_intro_shots++;
    if (i >= NINTRO_T) return;
    long black = 0;
    for (size_t p = 0, n = (size_t)SCREEN_W * SCREEN_H; p < n; p++) black += f->px[p * 4] < 8 && f->px[p * 4 + 1] < 8 && f->px[p * 4 + 2] < 8;
    g_intro_black[i] = black;
    if (i == NINTRO_T - 1) {
        if (!g_intro_end) g_intro_end = surf_new(SCREEN_W, SCREEN_H);
        surf_copy(g_intro_end, f);
    }
    if (getenv("NOPNG") || !g_w) return;
    char p[500];
    snprintf(p, sizeof p, "%s/%s/intro-%04d.png", g_w->hp.outdir, g_w->hp.scenario, (int)lroundf(t * 1000));
    stbi_write_png(p, SCREEN_W, SCREEN_H, 4, f->px, SCREEN_W * 4);
}

// every frame shown: the first one after the intro (its own frames, the last included, come before it says it has played) is the home screen's first, which must be
// the frame the intro ended on (later ones may differ: the home screen goes on to bake the focused tile's peeling tape and shows it)
static long g_intro_first_diff;
static void intro_first_home_frame(HostPlat *h, const Surf *f)
{
    (void)h;
    if (!g_ui_intro_played || g_intro_first_diff >= 0 || !g_intro_end) return;
    long d = 0;
    for (size_t p = 0, n = (size_t)SCREEN_W * SCREEN_H * 4; p < n; p++) d += abs(f->px[p] - g_intro_end->px[p]) > 2;
    g_intro_first_diff = d;
}

static void intro_no_press(HostPlat *h) { CHECK_MSG(h->sounds[SND_TICK] == 0, "the press that skipped it went no further (%d taps)", h->sounds[SND_TICK]); }

// the logo's pieces made beforehand: data/intro_logo.bin must be what skin.c makes now (INTRO_LOGO_WRITE=1 writes it, after the logo's drawing changes)
static unsigned char *g_intro_logo;
static size_t g_intro_logo_n;
static void intro_logo_check(HostPlat *h)
{
    (void)h;
    size_t n = 0;
    uint8_t *fresh = ui_intro_logo_blob(&n);
    if (fresh && getenv("INTRO_LOGO_WRITE")) {
        FILE *f = fopen("../data/intro_logo.bin", "wb");
        if (f) {
            fwrite(fresh, 1, n, f);
            fclose(f);
            printf("  wrote data/intro_logo.bin (%zu bytes)\n", n);
        }
    }
    size_t sn = 0;
    unsigned char *stored = load_file("../data/intro_logo.bin", &sn);
    CHECK_MSG(fresh && stored && sn == n && !memcmp(stored, fresh, n), "data/intro_logo.bin is the logo as skin.c makes it (INTRO_LOGO_WRITE=1 makes it again)");
    free(stored);
    free(fresh);
}

static void intro_world(W *w, const char *name, int hours)
{
    reminder_world(w, name, hours);
    g_w = w;
    char p[500];
    snprintf(p, sizeof p, "%s", w->hp.outdir);
    mkdir(p, 0755);
    snprintf(p, sizeof p, "%s/%s", w->hp.outdir, w->hp.scenario);
    mkdir(p, 0755);
}

// the four cards' pictures (the console has them built in; here they are read from data/), 0 if one is missing
static int intro_load_cards(void)
{
    for (int k = 0; k < 4; k++) {
        if (g_intro_card[k]) continue;
        char p[64];
        snprintf(p, sizeof p, "../data/intro_card_%s.png", INTRO_CARDS[k].key);
        FILE *fp = fopen(p, "rb");
        if (fp) {
            fseek(fp, 0, SEEK_END);
            long n = ftell(fp);
            fseek(fp, 0, SEEK_SET);
            g_intro_card[k] = n > 0 ? malloc((size_t)n) : NULL;
            if (g_intro_card[k] && fread(g_intro_card[k], 1, (size_t)n, fp) == (size_t)n) g_intro_card_n[k] = (size_t)n;
            fclose(fp);
        }
        CHECK_MSG(g_intro_card_n[k] > 0, "the card's picture is there (%s)", p);
        if (!g_intro_card_n[k]) return 0;
    }
    return 1;
}

static void sc_intro(void)
{
    g_scenario = "50-intro";
    if (!intro_load_cards()) return;
    // every card is as likely to come up at a start
    int seen[4] = {0};
    for (uint32_t s = 1; s <= 400; s++) seen[ui_intro_pick(s * 2654435761u, 4)]++;
    CHECK_MSG(seen[0] > 70 && seen[1] > 70 && seen[2] > 70 && seen[3] > 70, "each card comes up about as often (%d %d %d %d of 400)", seen[0], seen[1], seen[2], seen[3]);
    // all four as on the console, or INTRO_CARD=<key> for that one alone (to put its frames next to the page's: tools/cvp.mjs <seed> <KEY>)
    g_ui_intro_ncards = 0;
    const char *only = getenv("INTRO_CARD");
    for (int k = 0; k < 4; k++)
        if (!only || !strcmp(only, INTRO_CARDS[k].key)) ui_intro_add_card(g_intro_card[k], g_intro_card_n[k], INTRO_CARDS[k].lx, INTRO_CARDS[k].ly);
    const char *file = getenv("INTRO_CARD_FILE"); // (mockups: one card's picture from a file, alone, its logo at INTRO_CARD_LX, INTRO_CARD_LY)
    if (file) {
        size_t n = 0;
        unsigned char *png = load_file(file, &n);
        g_ui_intro_ncards = 0;
        if (png) ui_intro_add_card(png, n, getenv("INTRO_CARD_LX") ? (float)atof(getenv("INTRO_CARD_LX")) : 0.0f, getenv("INTRO_CARD_LY") ? (float)atof(getenv("INTRO_CARD_LY")) : -50.0f);
    }
    g_ui_intro_seed = getenv("INTRO_SEED") ? (uint32_t)strtoul(getenv("INTRO_SEED"), NULL, 10) : 7;
    if (!g_intro_logo) g_intro_logo = load_file("../data/intro_logo.bin", &g_intro_logo_n); // (as main.c hands it over; without it the intro makes the logo)
    ui_intro_set_logo(g_intro_logo, g_intro_logo_n);
    int light = getenv("DARK") == NULL;

    // 1 played to the end
    g_ui_intro_shot = intro_shot;
    g_ui_intro_times = INTRO_T;
    g_ui_intro_ntimes = NINTRO_T;
    g_intro_shots = 0;
    g_ui_intro_played = 0;
    W w;
    intro_world(&w, "50-intro", 0);
    HostPlat *h = &w.hp;
    g_intro_first_diff = -1;
    h->trace_on = 1;
    h->frame_hook = intro_first_home_frame;
    hp_call(h, intro_logo_check);
    hp_snap(h, "home");
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_ui_intro_played == 1 && g_ui_intro_frames > 100, "it played to the end, frame by frame (%d, %d frames)", g_ui_intro_played, g_ui_intro_frames);
    int whole = g_ui_intro_frames; // (the PC's clock goes by its calls, not its frames: 2b presses part way through by this)
    // (when depends on when the home screen is made: here at the card's rest, or early on with INTRO_ASYNC_HOME=1 as on the console; the tearing begins at 2.42 s)
    CHECK_MSG(g_ui_intro_third_at > 0 && g_ui_intro_third_at < 2.42f, "a third drawer takes turns once the home screen is made, before the wall is torn off (from %.2f s)",
              (double)g_ui_intro_third_at);
    if (getenv("INTRO_REPORT")) printf("  intro: card %d of %d, %d frames\n", g_ui_intro_card, g_ui_intro_ncards, g_ui_intro_frames);
    CHECK_MSG(g_intro_first_diff == 0, "it ends on the very frame the home screen starts with (%ld channel values differ)", g_intro_first_diff);
    CHECK_MSG(g_intro_shots == NINTRO_T && g_intro_black[0] > SCREEN_W * SCREEN_H / 2, "it starts on black (%ld black pixels)", g_intro_black[0]);
    for (int i = 0; i < NINTRO_T && light; i++)
        if (INTRO_T[i] >= 1.25f) CHECK_MSG(g_intro_black[i] == 0, "once the wall is down no black shows (%ld black pixels at %.2f s)", g_intro_black[i], (double)INTRO_T[i]);
    int slips = h->sounds[SND_SLIP0] + h->sounds[SND_SLIP1] + h->sounds[SND_SLIP2] + h->sounds[SND_SLIP3];
    CHECK_MSG(h->sounds[SND_INTRO] == 1, "the music starts once (%d)", h->sounds[SND_INTRO]);
    CHECK_MSG(h->sounds[SND_STAMP] == 1 && h->sounds[SND_PEEL] >= 5 && h->sounds[SND_TEAR] >= 10 && slips >= 10,
              "the seal is stamped, the rolls tap, the strips are cut and torn off, out loud (stamp %d, peel %d, tear %d, taps %d)", h->sounds[SND_STAMP],
              h->sounds[SND_PEEL], h->sounds[SND_TEAR], slips);
    CHECK_MSG(h->fade_offs == 1 && h->hushes == 0 && h->sounds[SND_CHORD] == 0, "it comes out of the black itself and nothing is cut short (%d, %d, %d)",
              h->fade_offs, h->hushes, h->sounds[SND_CHORD]);
    w_done(&w);
    g_w = NULL;
    g_ui_intro_shot = NULL;

    // 2 skipped: a press a little way in ends it at once, on the home screen (the stick pushed before it does not)
    g_ui_intro_played = 0;
    intro_world(&w, "50-intro", 0);
    h = &w.hp;
    hp_stick(h, BTN_LEFT);
    hp_in(h, 20);
    hp_btn(h, BTN_A);
    hp_in(h, 20);
    hp_snap(h, "home-after-skip");
    hp_call(h, intro_no_press);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_ui_intro_played == 2 && g_ui_intro_frames >= 38 && g_ui_intro_frames <= 42, "a press skips it at once (%d, after %d frames)", g_ui_intro_played,
              g_ui_intro_frames);
    CHECK_MSG(h->hushes == 1 && h->sounds[SND_CHORD] == 1, "and stops its sounds, ending on the stamp's chord (%d, %d)", h->hushes, h->sounds[SND_CHORD]);
    CHECK_MSG(g_ui_intro_skip_frames >= 8 && h->sounds[SND_STAMP] == 1 && h->sounds[SND_STICK] >= 2,
              "on the way to the home screen the seal is stamped and flies to its corner (%d frames, stamp %d, stick %d)", g_ui_intro_skip_frames,
              h->sounds[SND_STAMP], h->sounds[SND_STICK]);
    CHECK_MSG(g_ui_intro_skip_fall >= 4 && g_ui_intro_skip_down == 1, "the seal comes down over the intro as it plays on, before the home screen is waited for (%d, %d)",
              g_ui_intro_skip_fall, g_ui_intro_skip_down);
    w_done(&w);
    g_w = NULL;

    // 2b a press once the card's seal has started coming down (1.77 s; here 60% of the way, about 2.3 s) does not skip it: it plays out, and the press goes no
    // further
    g_ui_intro_played = 0;
    intro_world(&w, "50-intro", 0);
    h = &w.hp;
    hp_btn(h, BTN_A);
    hp_in(h, whole * 6 / 10);
    hp_call(h, intro_no_press);
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_ui_intro_played == 1 && g_ui_intro_late == 1 && g_ui_intro_skip_frames == 0 && h->hushes == 0 && h->sounds[SND_CHORD] == 0,
              "a press after the card's seal starts coming down is too late to skip (%d, %d late, %d skip frames)", g_ui_intro_played, g_ui_intro_late,
              g_ui_intro_skip_frames);
    w_done(&w);
    g_w = NULL;

    // 3 hours after someone loaded their saves: the intro first, then "is this still you?" over the home screen it ended on
    g_ui_intro_played = 0;
    intro_world(&w, "50-intro", 5);
    h = &w.hp;
    hp_snap(h, "still-you");
    hp_btn(h, BTN_A); // yes, still me
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_ui_intro_played == 1 && h->sounds[SND_INTRO] == 1, "the question does not stop the intro (%d, %d)", g_ui_intro_played, h->sounds[SND_INTRO]);
    Store st2;
    store_open(&st2, &w.fb.be, w.fb.sd);
    CHECK_MSG(st2.state.loaded_at >= (int64_t)w.now0 - 5, "and it is asked after it (%lld)", (long long)(st2.state.loaded_at - (int64_t)w.now0));
    w_done(&w);
    g_w = NULL;

    g_ui_intro_ncards = 0; // (no other scenario has an intro)
    g_ui_intro_seed = 0;
}

// ---- Skips filmed: mockups of the ways a skip can end on the home screen (ui_intro.c, g_ui_intro_skip_style 1-3), each drawn by the intro's own code. SKIP_AT=<s,s,..>
// (0.9 and 2.0 by default) with card a4 (INTRO_CARD=<key>); every frame at 60 a second goes to out/app/51-intro-skip/<style>-<ms>/f<nnn>.png.
extern void (*g_ui_intro_film)(const Surf *frame, int style, float at, int i);
extern const float *g_ui_intro_film_at;
extern int g_ui_intro_film_n, g_ui_intro_film_wraps;
static int g_film_frames;
static void intro_film(const Surf *f, int style, float at, int i)
{
    char p[500];
    snprintf(p, sizeof p, "%s/%s/%d-%04d", g_w->hp.outdir, g_w->hp.scenario, style, (int)lroundf(at * 1000));
    if (i == 0) mkdir(p, 0755);
    size_t l = strlen(p);
    snprintf(p + l, sizeof p - l, "/f%03d.png", i);
    stbi_write_png(p, SCREEN_W, SCREEN_H, 4, f->px, SCREEN_W * 4);
    g_film_frames++;
}
static void intro_no_shot(const Surf *f, float t)
{
    (void)f;
    (void)t;
}

static void sc_intro_skip(void)
{
    g_scenario = "51-intro-skip";
    if (!intro_load_cards()) return;
    const char *only = getenv("INTRO_CARD") ? getenv("INTRO_CARD") : "a4";
    g_ui_intro_ncards = 0;
    for (int k = 0; k < 4; k++)
        if (!strcmp(only, INTRO_CARDS[k].key)) ui_intro_add_card(g_intro_card[k], g_intro_card_n[k], INTRO_CARDS[k].lx, INTRO_CARDS[k].ly);
    g_ui_intro_seed = getenv("INTRO_SEED") ? (uint32_t)strtoul(getenv("INTRO_SEED"), NULL, 10) : 7;
    if (!g_intro_logo) g_intro_logo = load_file("../data/intro_logo.bin", &g_intro_logo_n);
    ui_intro_set_logo(g_intro_logo, g_intro_logo_n);
    static float at[8];
    int n = 0;
    char *s = getenv("SKIP_AT") ? getenv("SKIP_AT") : "0.9,2.0";
    while (*s && n < 8) {
        at[n++] = strtof(s, &s);
        if (*s != ',') break;
        s++;
    }
    g_ui_intro_shot = intro_no_shot;
    g_ui_intro_times = NULL;
    g_ui_intro_ntimes = 0;
    g_ui_intro_film = intro_film;
    g_ui_intro_film_at = at;
    g_ui_intro_film_n = n;
    g_ui_intro_film_wraps = getenv("WRAPS") != NULL; // WRAPS=1: the ways the seal's ink can meet a roll under it, as 10-14 (SKIP_AT=-1: a roll under its middle,
                                                     // -2: one under its top left)
    g_film_frames = 0;
    W w;
    intro_world(&w, "51-intro-skip", 0);
    HostPlat *h = &w.hp;
    hp_btn(h, BTN_A); // (the intro itself is skipped at once; the films are made before it plays)
    hp_btn(h, BTN_B);
    CHECK(run(&w) == 0);
    CHECK_MSG(g_film_frames > n * 3 * 60, "the skips were filmed (%d frames)", g_film_frames);
    w_done(&w);
    g_w = NULL;
    g_ui_intro_shot = NULL;
    g_ui_intro_film = NULL;
    g_ui_intro_film_wraps = 0;
    g_ui_intro_ncards = 0;
    g_ui_intro_seed = 0;
}

int main(int argc, char **argv)
{
    const char *only = argc > 1 ? argv[1] : NULL;
    g_ui_press_audit = 1; // (every press is checked for the background staying put)
    g_ui_press_hook = press_dump;
    g_thumbs_sync = getenv("THUMBS_ASYNC") == NULL; // (small pictures made at once, so what a frame shows doesn't depend on a thread's timing; the scenario
                                                    // "thumbs-async" runs the thread)
    g_ui_banner_variant = getenv("BANNER") ? atoi(getenv("BANNER")) : 0; // (the first banner: the pixel probes know it; the scenario "banners" shows them all)
    g_ui_wait_count = getenv("WAIT_COUNT") ? atoi(getenv("WAIT_COUNT")) : 5;
    { // the push pins' pictures, as main.c hands them over (NO_PINS=1: as if the file were missing, nothing drawn)
        static unsigned char *pins_blob;
        size_t pins_n = 0;
        if (!getenv("NO_PINS")) pins_blob = load_file("../data/pins.bin", &pins_n);
        pins_set_blob(pins_blob, pins_n);
    }
    g_ui_wait_after_ms = getenv("WAIT_ON") ? atoi(getenv("WAIT_ON")) : 1000000000; // (the "still working" scrap stays away from every scenario but its own, unless WAIT_ON=<ms> asks for it in all of them: it comes down once the app has been busy that long)
    struct { const char *name; void (*fn)(void); } all[] = {
        {"first-run", sc_first_run}, {"many-profiles", sc_many_profiles}, {"password-lockout", sc_password_lockout},
        {"admin", sc_admin}, {"builtin-keyboard", sc_builtin_keyboard}, {"recovery", sc_recovery},
        {"theme-follows", sc_theme_follows}, {"washi-progress-noicon", sc_washi_progress_noicon}, {"washi-progress-many", sc_washi_progress_many}, {"interrupted", sc_interrupted},
        {"refusals", sc_refusals}, {"guest", sc_guest}, {"picture", sc_picture}, {"touch", sc_touch}, {"gridscroll", sc_gridscroll}, {"console-pictures", sc_console_pictures}, {"online-icons", sc_online_icons_page}, {"online-picker", sc_online_icons_picker}, {"extra-icons", sc_extra_icons}, {"thumbs-async", sc_thumbs_async}, {"picker-bench", sc_picker_bench}, {"shuffle", sc_shuffle}, {"sort-anim", sc_sort_anim}, {"sticker-art", sc_sticker_art}, {"busy-dots", sc_busy_dots}, {"pins", sc_pins}, {"pins-film", sc_pins_film}, {"home-drag", sc_home_drag}, {"order-kept", sc_order_kept}, {"edit-profile", sc_edit_profile}, {"edit-from-home", sc_edit_from_home}, {"logs", sc_logs}, {"pad-press", sc_pad_press}, {"home-press", sc_home_press}, {"home-press-row2", sc_home_press_row2},
        {"reminder-signout", sc_reminder_signout}, {"hub", sc_hub},
        {"reminder-setting", sc_reminder_setting}, {"deleted", sc_deleted}, {"purge-all", sc_purge_all}, {"wrap", sc_wrap}, {"storage", sc_storage}, {"storage-many", sc_storage_many}, {"home-focus", sc_home_focus}, {"sounds", sc_sounds}, {"pad-look", sc_pad_look}, {"wait", sc_wait}, {"wait-clip", sc_wait_clip}, {"banners", sc_banners}, {"sounds-scroll", sc_sounds_scroll},
        {"lean", sc_lean}, {"lists", sc_lists}, {"about", sc_about}, {"guest-keeps", sc_guest_keeps}, {"adopt-names", sc_adopt_names},
        {"reminder-test", sc_reminder_test}, {"intro-assets", sc_intro_assets}, {"unclaimed", sc_unclaimed}, {"reminder-soon", sc_reminder_soon}, {"backup", sc_backup}, {"backups-many", sc_backups_many}, {"backups-motion", sc_backups_motion}, {"readme-shots", sc_readme_shots}, {"backups-fuzz", sc_backups_fuzz},
        {"intro", sc_intro}, {"intro-skip", sc_intro_skip}, {"update", sc_update_mock}, {"update-real", sc_update_real}, {"update-back", sc_update_back},
    };
    for (size_t i = 0; i < sizeof all / sizeof all[0]; i++) {
        if (only && strcmp(only, all[i].name) != 0) continue;
        if (!only && (!strcmp(all[i].name, "wait-clip") || !strcmp(all[i].name, "intro-skip"))) continue; // (tools for the mockups, not tests)
        int before = g_failed, checks0 = g_checks;
        clock_t t0 = clock();
        g_press_scenario = all[i].name;
        all[i].fn();
        printf("%-18s %-6s %5d checks %6.1f s\n", all[i].name, g_failed == before ? "ok" : "FAILED", g_checks - checks0, (double)(clock() - t0) / CLOCKS_PER_SEC);
        hp_bench_report(all[i].name);
    }
    if (!only) {
        CHECK_MSG(g_ui_presses > 10 && g_ui_press_changed == g_ui_presses, "every press in the whole run (%ld) changed pixels (%ld did)", g_ui_presses, g_ui_press_changed);
        CHECK_MSG(g_ui_press_bg_changed == 0, "and in none of them did the background move (%ld pixels)", g_ui_press_bg_changed);
        CHECK_MSG(g_press_band == 0, "and in none of them did a neighbouring row move with the one pressed (%ld pixels above or below a row changed)", g_press_band);
    }
    if (g_ui_presses) printf("\n%ld presses: %ld px of background moved, %ld px changed above or below a row\n", g_ui_presses, g_ui_press_bg_changed, g_press_band);
    if (getenv("WAIT_ON")) printf("\nthe \"still working\" scrap came down %ld times in this run and drew %ld frames\n", g_ui_wait_shown, g_ui_wait_frames);
    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
