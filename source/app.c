#include "app.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>

#include "fsx.h"
#include "gfx/paper.h"
#include "locker.h"
#include "sysprofile.h"
#include "store.h"
#include "tint.h"
#include "ui.h"

#define MAX_TRIES 5
#define DOT "\xC2\xB7"

static Backend *g_be;
static Store g_st;
static Locker *g_lk;
static char g_account[48];

// how a profile\xE2\x80\x99s avatar is drawn: its background swatch and its picture, if it has one
static int look_of(const Profile *p) { return SKIN_LOOK(p->color, p->picture); }

// UiItems start zeroed; swatch 0 would mean "blue", so every list marks its items as having none
static void items_init(UiItem *items, int n)
{
    for (int i = 0; i < n; i++) {
        items[i].swatch = -1;
        items[i].toggle = -1;
    }
}

// a message to show once the home screen has been refreshed (so it sits over the new state, not the old one)
static struct {
    int active;
    UiKind kind;
    char head[120];
    char detail[900];
} g_pending;

// ------------------------------------------------------------------------------------------------ small helpers

static int64_t now_s(void)
{
    return (int64_t)g_be->now(g_be);
}

static int quitting(void)
{
    return ui_quit_requested();
}

static void refresh_account(void)
{
    g_account[0] = 0;
    BeAccount acc[8];
    size_t n = 0;
    if (g_be->list_accounts(g_be, acc, 8, &n) != BE_OK) return;
    for (size_t i = 0; i < n; i++)
        if (!strcmp(acc[i].uid, g_st.cfg.target_uid)) snprintf(g_account, sizeof g_account, "%s", acc[i].name);
}

static void status_cb(void *user, SkinStatus *st)
{
    (void)user;
    int pct = 0, chg = 0;
    if (g_be->battery(g_be, &pct, &chg) == BE_OK) {
        st->battery = pct;
        st->charging = chg;
    }
    ui_format_clock((time_t)now_s(), st->clock, sizeof st->clock);
    snprintf(st->account, sizeof st->account, "%s", g_account);
}

static int icon_cb(void *user, uint64_t title, const uint8_t **jpeg, size_t *size)
{
    (void)user;
    return g_be->title_icon(g_be, title, jpeg, size) == BE_OK ? 0 : 1;
}

static void save_config_or_warn(void)
{
    if (store_save_config(&g_st) != 0)
        ui_message(UI_WARN, "Couldn\xE2\x80\x99t save the setting", "Is the SD card full or locked?");
}

static void profile_name_of(const char *id, char *out, size_t n)
{
    Profile p;
    if (!strcmp(id, UNCLAIMED) || !id[0]) snprintf(out, n, "the console");
    else if (store_profile_load(&g_st, id, &p) == 0) snprintf(out, n, "%s", p.name);
    else snprintf(out, n, "someone");
}

static int profile_color_of(const char *id)
{
    Profile p;
    return store_profile_load(&g_st, id, &p) == 0 ? look_of(&p) : 0;
}

// the number of games that are on the console account right now (not counting shared ones)
static int console_game_count(void)
{
    TitleSet *ts = malloc(sizeof *ts);
    if (!ts) return 0;
    int n = 0;
    if (lk_list_titles(g_lk, ts) == 0)
        for (size_t i = 0; i < ts->n; i++)
            if (!store_is_excluded(&g_st, ts->v[i])) n++;
    free(ts);
    return n;
}

// ------------------------------------------------------------------------------------------------ passwords

static struct {
    char who[ID_MAX];
    int fails;
    int64_t until;
} g_lock;

typedef int (*PinCheck)(const void *subject, const char *pin);

static int check_profile_pin(const void *p, const char *pin) { return store_profile_check_pin((const Profile *)p, pin); }
static int check_admin_pin(const void *unused, const char *pin) { (void)unused; return store_admin_check_pin(&g_st, pin); }

// Asks for a password until it's right, cancelled, or there have been too many wrong tries. `who` NULL = the admin.
static int ask_password(const char *key, const char *who, int color, const char *prompt, PinCheck check, const void *subject)
{
    if (strcmp(g_lock.who, key) != 0) {
        snprintf(g_lock.who, sizeof g_lock.who, "%s", key);
        g_lock.fails = 0;
        g_lock.until = 0;
    }
    int64_t t = now_s();
    if (g_lock.until > t) {
        char msg[120];
        snprintf(msg, sizeof msg, "Wait %d seconds.", (int)(g_lock.until - t));
        ui_message(UI_WARN, "Too many tries", msg);
        return 0;
    }
    char problem[80] = "";
    for (;;) {
        char pin[8];
        if (!ui_pin_entry(who, color, prompt, problem, pin)) return 0;
        if (check(subject, pin)) {
            g_lock.fails = 0;
            return 1;
        }
        g_lock.fails++;
        if (g_lock.fails >= MAX_TRIES) {
            g_lock.until = now_s() + 30 * (g_lock.fails - MAX_TRIES + 1);
            ui_message(UI_ERROR, "Wrong password", "Too many tries. Wait a moment.");
            return 0;
        }
        snprintf(problem, sizeof problem, "Wrong  " DOT "  %d %s left", MAX_TRIES - g_lock.fails,
                 MAX_TRIES - g_lock.fails == 1 ? "try" : "tries");
    }
}

// Two entries that must match. who NULL = no avatar. Returns 1 and fills out, or 0 if cancelled.
static int new_password(const char *who, int color, const char *what, char out[7])
{
    char problem[80] = "";
    char prompt[100];
    snprintf(prompt, sizeof prompt, "%s", what);
    for (;;) {
        char a[8], b[8];
        if (!ui_pin_entry(who, color, prompt, problem, a)) return 0;
        if (!ui_pin_entry(who, color, "Enter it again", NULL, b)) return 0;
        if (strcmp(a, b) == 0) {
            memcpy(out, a, 7);
            return 1;
        }
        snprintf(problem, sizeof problem, "No match  " DOT "  try again");
    }
}

// ------------------------------------------------------------------------------------------------ the console's own profile

static char g_sys_note[200];      // why the last update of the console's profile failed ("" = it worked, or it is off)
static char g_sys_last[sizeof g_sys_note + 16] = ""; // what happened the last time, for the Console profile page ("Failed: " + why)

// Makes the console's own profile for the account (the name and picture games and the Home menu show) match whoever is loaded, or
// puts the original back when nobody is. Only when the admin turned it on, and a failure never stops anything: it is just reported.
// Returns 1 if the console's profile was changed, 0 if there was nothing to do, -1 if it failed (g_sys_note says why).
static int sys_profile_sync(void)
{
    g_sys_note[0] = 0;
    if (!g_st.cfg.sync_name && !g_st.cfg.sync_picture) return 0;
    char err[160] = "";
    int rc;
    char who[48] = "";
    if (!strcmp(g_st.state.holder, UNCLAIMED)) {
        // nobody is loaded: the account\xE2\x80\x99s own name, and the guest picture (Princess Toadette on light blue, unless changed)
        char onick[40];
        if (!sp_original_nick(&g_st, onick)) return 0; // the console\xE2\x80\x99s profile was never changed: nothing to put back
        uint8_t *jpeg = NULL;
        size_t jn = 0;
        int gp = g_st.cfg.guest_picture < skin_avatar_count() ? g_st.cfg.guest_picture : -1;
        if (g_st.cfg.sync_picture && !(jpeg = ui_render_avatar_jpeg("Guest", SKIN_LOOK(g_st.cfg.guest_swatch, gp), &jn))) {
            snprintf(g_sys_note, sizeof g_sys_note, "Couldn\xE2\x80\x99t make the picture.");
            snprintf(g_sys_last, sizeof g_sys_last, "Failed: %s", g_sys_note);
            return -1;
        }
        SpIcon icon = {gp + 1, g_st.cfg.guest_swatch};
        rc = sp_apply(&g_st, g_be, g_st.cfg.sync_name ? onick : NULL, jpeg, jn, jpeg && gp >= 0 ? &icon : NULL, err, sizeof err);
        free(jpeg);
        if (rc == SP_OK) snprintf(g_sys_last, sizeof g_sys_last, "Guest picture set");
    } else {
        Profile p;
        if (store_profile_load(&g_st, g_st.state.holder, &p) != 0) return 0;
        snprintf(who, sizeof who, "%s", p.name);
        uint8_t *jpeg = NULL;
        size_t jn = 0;
        if (g_st.cfg.sync_picture && !(jpeg = ui_render_avatar_jpeg(p.name, SKIN_LOOK(p.color, p.picture), &jn))) {
            snprintf(g_sys_note, sizeof g_sys_note, "Couldn\xE2\x80\x99t make the picture.");
            snprintf(g_sys_last, sizeof g_sys_last, "Failed: %s", g_sys_note);
            return -1;
        }
        // the pictures are the console's own icons in its own order, so the console's icon id is the picture's number plus one
        // An Extra icon has no id on the console, so its icon id is 0 ("no character"): left at the last character's id, the Home
        // menu keeps showing that character and ignores the picture.
        SpIcon icon = {custom_is(p.picture) ? 0 : p.picture + 1, p.color};
        rc = sp_apply(&g_st, g_be, g_st.cfg.sync_name ? p.name : NULL, jpeg, jn, jpeg && p.picture >= 0 ? &icon : NULL, err, sizeof err);
        free(jpeg);
        if (rc == SP_OK) snprintf(g_sys_last, sizeof g_sys_last, "Updated for %.40s", who);
    }
    if (rc == SP_ERR) {
        snprintf(g_sys_note, sizeof g_sys_note, "%s", err);
        snprintf(g_sys_last, sizeof g_sys_last, "Failed: %.150s", err);
        store_logf(&g_st, "console profile: %s", err);
        return -1;
    }
    return rc == SP_OK ? 1 : 0;
}

static void sys_profile_sync_warn(void)
{
    sys_profile_sync();
    if (g_sys_note[0]) ui_message(UI_WARN, "The console\xE2\x80\x99s profile wasn\xE2\x80\x99t updated", g_sys_note);
}

// ------------------------------------------------------------------------------------------------ progress, recovery

typedef enum { OP_SWAP, OP_LOAD, OP_SIGNOUT, OP_RECOVER, OP_ROLLBACK, OP_CLAIM, OP_GIVE, OP_SNAPSHOT, OP_RESTORE } OpKind;

static struct {
    OpKind kind;
    int nsteps;
    char from[48], to[48];
} g_op;

static void on_progress(void *user, const char *what, uint64_t cur, uint64_t total)
{
    (void)user;
    (void)what;
    int ph = g_lk->phase; // 1 backing up, 2 loading, 3 clearing the console
    char title[140];
    if (g_op.kind == OP_SNAPSHOT || g_op.kind == OP_RESTORE) {
        ui_progress_step(1);
        snprintf(title, sizeof title, g_op.kind == OP_RESTORE ? "Restoring a backup" : "Backing up everything");
    } else if (g_op.kind == OP_ROLLBACK || g_op.kind == OP_GIVE) {
        ui_progress_step(1);
        snprintf(title, sizeof title, g_op.kind == OP_GIVE ? "Giving the saves to %s" : "Rolling back %s", g_op.to);
    } else if (ph == 3) {
        ui_progress_step(g_op.nsteps - 1);
        snprintf(title, sizeof title, "Clearing the console for the next player");
    } else if (ph == 2) {
        ui_progress_step(g_op.nsteps - 1);
        snprintf(title, sizeof title, "Loading %s\xE2\x80\x99s saves", g_op.to);
    } else {
        ui_progress_step(1);
        snprintf(title, sizeof title, "Backing up %s\xE2\x80\x99s saves", g_op.from);
    }
    ui_progress_title(title);
    ui_progress_plan(g_lk->plan_serial, g_lk->plan, g_lk->plan_n);
    ui_progress_update(g_lk->cur_name, g_lk->cur_title, cur, total);
}

static void progress_open(OpKind kind, const char *from, const char *to)
{
    g_op.kind = kind;
    snprintf(g_op.from, sizeof g_op.from, "%s", from);
    snprintf(g_op.to, sizeof g_op.to, "%s", to);
    char s1[80], s2[80];
    snprintf(s1, sizeof s1, "Backing up %s", from);
    snprintf(s2, sizeof s2, "Loading %s", to);
    const char *three[3] = {"Checked", s1, s2};
    const char *two[2] = {"Checked", "Working"};
    char title[140];
    if (kind == OP_SWAP) {
        g_op.nsteps = 3;
        snprintf(title, sizeof title, "Backing up %s\xE2\x80\x99s saves", from);
        ui_progress_begin(title, three, 3);
    } else if (kind == OP_LOAD) { // from a cleared console: nothing to back up
        const char *load[2] = {"Checked", s2};
        g_op.nsteps = 2;
        snprintf(title, sizeof title, "Loading %s\xE2\x80\x99s saves", to);
        ui_progress_begin(title, load, 2);
    } else if (kind == OP_SIGNOUT) {
        const char *sign[3] = {"Checked", s1, "Clearing the console"};
        g_op.nsteps = 3;
        snprintf(title, sizeof title, "Backing up %s\xE2\x80\x99s saves", from);
        ui_progress_begin(title, sign, 3);
    } else if (kind == OP_RECOVER) {
        g_op.nsteps = 2;
        ui_progress_begin("Finishing the swap", two, 2);
    } else if (kind == OP_CLAIM) { // a guest's play goes into their new profile: only a backup
        const char *claim[2] = {"Checked", s1};
        g_op.nsteps = 2;
        snprintf(title, sizeof title, "Backing up %s\xE2\x80\x99s saves", from);
        ui_progress_begin(title, claim, 2);
    } else if (kind == OP_SNAPSHOT) { // a copy of everything Washi keeps, into a new folder
        const char *snap[2] = {"Checked", "Copying"};
        g_op.nsteps = 2;
        ui_progress_begin("Backing up everything", snap, 2);
    } else if (kind == OP_RESTORE) { // everything Washi keeps goes back to an earlier state
        const char *res[2] = {"Checked", "Restoring"};
        g_op.nsteps = 2;
        ui_progress_begin("Restoring a backup", res, 2);
    } else if (kind == OP_GIVE) { // unclaimed saves become a profile's: only a copy into their backup
        const char *give[2] = {"Checked", "Copying"};
        g_op.nsteps = 2;
        snprintf(title, sizeof title, "Giving the saves to %s", to);
        ui_progress_begin(title, give, 2);
    } else {
        const char *roll[2] = {"Checked", "Rolling back"};
        g_op.nsteps = 2;
        snprintf(title, sizeof title, "Rolling back %s", to);
        ui_progress_begin(title, roll, 2);
    }
    g_lk->progress = on_progress;
}

static void recovery_flow(int startup)
{
    while (!quitting() && lk_needs_recovery(g_lk)) {
        const char *head = startup ? "The last swap stopped" : "That swap stopped";
        if (!ui_confirm(head, "Nothing is lost. Finish it now?",
                        "Finish now", "Later", 0, NULL, 0))
            return;
        char from[48], to[48];
        profile_name_of(g_st.state.from, from, sizeof from);
        profile_name_of(g_st.state.to, to, sizeof to);
        progress_open(OP_RECOVER, from, to);
        int rc = lk_recover(g_lk);
        ui_progress_end();
        store_reload(&g_st);
        if (rc == LK_OK || rc == LK_NOOP) {
            sys_profile_sync();
            ui_message(UI_OK, "Done", NULL);
            return;
        }
        ui_message(UI_ERROR, "Still couldn\xE2\x80\x99t finish", g_lk->err);
        startup = 0;
    }
}

static void report_failure(int rc)
{
    (void)rc;
    store_reload(&g_st);
    if (lk_needs_recovery(g_lk)) {
        ui_message(UI_ERROR, "That didn\xE2\x80\x99t finish", g_lk->err);
        recovery_flow(0);
    } else {
        ui_message(UI_WARN, "Nothing was changed", g_lk->err);
    }
}

// ------------------------------------------------------------------------------------------------ swapping

// returns 1 if the profile is now loaded
static int do_swap(const Profile *p)
{
    if (lk_needs_recovery(g_lk)) {
        recovery_flow(0);
        if (lk_needs_recovery(g_lk)) return 0;
    }
    char from[48];
    profile_name_of(g_st.state.holder, from, sizeof from);
    int guest = !strcmp(g_st.state.holder, UNCLAIMED) && g_st.state.wiped;
    progress_open(guest ? OP_LOAD : OP_SWAP, from, p->name);
    int rc = lk_swap_to(g_lk, p->id);
    ui_progress_end();
    if (rc == LK_NOOP) return 1;
    if (rc != LK_OK) {
        report_failure(rc);
        return 0;
    }
    sys_profile_sync();
    char head[120], detail[900];
    snprintf(head, sizeof head, "You\xE2\x80\x99re playing as %s", p->name);
    detail[0] = 0;
    strncat(detail, "Your saves are loaded.\n", sizeof detail - 1);
    for (int i = 0; i < g_lk->nwarn; i++) {
        strncat(detail, g_lk->warn[i], sizeof detail - strlen(detail) - 1);
        strncat(detail, "\n", sizeof detail - strlen(detail) - 1);
    }
    if (g_sys_note[0]) {
        strncat(detail, "Console profile: ", sizeof detail - strlen(detail) - 1);
        strncat(detail, g_sys_note, sizeof detail - strlen(detail) - 1);
        strncat(detail, "\n", sizeof detail - strlen(detail) - 1);
    }
    strncat(detail, "Press X to sign out when you\xE2\x80\x99re done.", sizeof detail - strlen(detail) - 1);
    g_pending.active = 1;
    g_pending.kind = UI_OK;
    snprintf(g_pending.head, sizeof g_pending.head, "%s", head);
    snprintf(g_pending.detail, sizeof g_pending.detail, "%s", detail);
    return 1;
}

// what a guest played becomes p's saves, and p is loaded; returns 1 if it worked
static int do_claim(const Profile *p)
{
    progress_open(OP_CLAIM, p->name, p->name);
    int rc = lk_claim_guest(g_lk, p->id);
    ui_progress_end();
    if (rc == LK_NOOP) return 1;
    if (rc != LK_OK) {
        report_failure(rc);
        return 0;
    }
    sys_profile_sync();
    g_pending.active = 1;
    g_pending.kind = UI_OK;
    snprintf(g_pending.head, sizeof g_pending.head, "You\xE2\x80\x99re playing as %s", p->name);
    snprintf(g_pending.detail, sizeof g_pending.detail, "What you played is saved to your profile.\nPress X to sign out when you\xE2\x80\x99re done.");
    return 1;
}

static void auto_backup(const char *who);

static int do_sign_out(void)
{
    if (lk_needs_recovery(g_lk)) {
        recovery_flow(0);
        if (lk_needs_recovery(g_lk)) return 0;
    }
    char who[48];
    profile_name_of(g_st.state.holder, who, sizeof who);
    progress_open(OP_SIGNOUT, who, "");
    int rc = lk_sign_out(g_lk);
    ui_progress_end();
    if (rc == LK_NOOP) return 1;
    if (rc != LK_OK) {
        report_failure(rc);
        return 0;
    }
    sys_profile_sync_warn(); // nobody is loaded now: the console\xE2\x80\x99s own profile goes back to how it was
    auto_backup(who);
    return 1;
}

// ------------------------------------------------------------------------------------------------ the roster

typedef struct {
    Profile *list;
    size_t n;
    UiPerson *people;
    int *plain; // each one's place in the order as it would be without pins (a search ranks by that)
    int *games; // (kept so that pinning can put the row in its new place without reading every profile again: roster_repin)
    int64_t *last;
} Roster;

static void roster_free(Roster *r)
{
    free(r->list);
    free(r->people);
    free(r->plain);
    free(r->games);
    free(r->last);
    memset(r, 0, sizeof *r);
}

typedef struct {
    Profile p;
    UiPerson u;
    int64_t last;
    int games;
    int plain;
} RosterRow;

static int g_sort_mode;

// "Recently played" is worked out once, when the app starts, and kept until it is started again: someone who loads their saves keeps their tile
// while the stamp lands on it (a tile that jumped to the front would take the stamp with it). Whoever is loaded when the app starts counts as
// the latest; a profile made since then was not played before the start, so it goes with the ones never played.
typedef struct {
    char id[ID_MAX];
    int64_t last;
} RecentMark;

static RecentMark *g_recent;
static size_t g_recent_n;
static int g_recent_set;

static void recent_forget(void)
{
    free(g_recent);
    g_recent = NULL;
    g_recent_n = 0;
    g_recent_set = 0;
}

static void recent_take(void)
{
    recent_forget();
    Profile *list = NULL;
    size_t n = 0;
    if (store_profile_list(&g_st, &list, &n) != 0) return; // (without the marks the order simply follows the latest saves, as it did)
    g_recent = calloc(n ? n : 1, sizeof *g_recent);
    if (!g_recent) {
        free(list);
        return;
    }
    int64_t now = now_s();
    for (size_t i = 0; i < n; i++) {
        int games = 0;
        int64_t last = 0;
        lk_profile_summary(g_lk, list[i].id, &games, &last);
        snprintf(g_recent[i].id, sizeof g_recent[i].id, "%s", list[i].id);
        g_recent[i].last = !strcmp(g_st.state.holder, list[i].id) ? now : last;
    }
    g_recent_n = n;
    g_recent_set = 1;
    free(list);
}

static int64_t recent_last(const char *id)
{
    for (size_t i = 0; i < g_recent_n; i++)
        if (!strcmp(g_recent[i].id, id)) return g_recent[i].last;
    return 0;
}

static int cmp_names(const Profile *a, const Profile *b)
{
    int c = strcasecmp(a->name, b->name);
    return c ? c : strcmp(a->id, b->id);
}

#define PIN_GATE_S 60
static int64_t g_pin_open_until; // pinning is open without the admin password until this time (s; 0: shut)
static int g_pins_first; // pinned profiles come first (the first pinned first), whatever the order

static int cmp_rows(const void *x, const void *y)
{
    const RosterRow *a = x, *b = y;
    if (g_pins_first && (a->p.pinned != 0) != (b->p.pinned != 0)) return a->p.pinned ? -1 : 1;
    if (g_pins_first && a->p.pinned && a->p.pinned != b->p.pinned) return a->p.pinned < b->p.pinned ? -1 : 1;
    switch (g_sort_mode) {
    case SORT_RECENT:
        if (a->last != b->last) return a->last > b->last ? -1 : 1; // the latest first; never played (0) last
        break;
    case SORT_NEWEST:
        if (a->p.created != b->p.created) return a->p.created > b->p.created ? -1 : 1;
        break;
    case SORT_GAMES:
        if (a->games != b->games) return a->games > b->games ? -1 : 1;
        break;
    default:
        break;
    }
    return cmp_names(&a->p, &b->p);
}

// both orders: without the pins first (rows[].plain is the place in it, which a search ranks by), then with them
static void rows_sort(RosterRow *rows, size_t n)
{
    g_sort_mode = g_st.cfg.sort_mode;
    g_pins_first = 0;
    qsort(rows, n, sizeof *rows, cmp_rows);
    for (size_t i = 0; i < n; i++) rows[i].plain = (int)i;
    g_pins_first = 1;
    qsort(rows, n, sizeof *rows, cmp_rows);
}

static void roster_load(Roster *r)
{
    roster_free(r);
    Profile *list = NULL;
    size_t n = 0;
    if (store_profile_list(&g_st, &list, &n) != 0) {
        list = NULL;
        n = 0;
    }
    RosterRow *rows = calloc(n ? n : 1, sizeof *rows);
    int64_t now = now_s();
    for (size_t i = 0; i < n; i++) {
        RosterRow *w = &rows[i];
        w->p = list[i];
        int64_t last = 0;
        lk_profile_summary(g_lk, list[i].id, &w->games, &last);
        w->last = g_recent_set ? recent_last(list[i].id) : !strcmp(g_st.state.holder, list[i].id) ? now : last; // (see recent_take)
        char ago[32];
        ui_ago_short(last, now, ago, sizeof ago);
        snprintf(w->u.name, sizeof w->u.name, "%s", list[i].name);
        snprintf(w->u.detail, sizeof w->u.detail, "%d game%s  " DOT "  %s", w->games, w->games == 1 ? "" : "s", ago);
        w->u.locked = store_profile_has_pin(&list[i]);
        w->u.color = look_of(&list[i]);
        w->u.pinned = list[i].pinned != 0;
    }
    rows_sort(rows, n);
    r->list = list;
    r->n = n;
    r->people = calloc(n ? n : 1, sizeof *r->people);
    r->plain = calloc(n ? n : 1, sizeof *r->plain);
    r->games = calloc(n ? n : 1, sizeof *r->games);
    r->last = calloc(n ? n : 1, sizeof *r->last);
    for (size_t i = 0; i < n; i++) {
        r->list[i] = rows[i].p;
        r->people[i] = rows[i].u;
        if (r->plain) r->plain[i] = rows[i].plain;
        if (r->games) r->games[i] = rows[i].games;
        if (r->last) r->last[i] = rows[i].last;
    }
    free(rows);
}

// A pin put in or pulled out of the profile at row k (its Profile already changed): the row goes to its new place without reading all the profiles again
// (that was most of the wait after pinning on the console). Only for the whole roster, not a search's narrowed one; returns 0 when it cannot.
static int roster_repin(Roster *r, size_t k, const Profile *p)
{
    if (k >= r->n || !r->games || !r->last || !r->plain) return 0;
    RosterRow *rows = calloc(r->n, sizeof *rows);
    if (!rows) return 0;
    r->list[k] = *p;
    r->people[k].pinned = p->pinned != 0;
    for (size_t i = 0; i < r->n; i++) {
        rows[i].p = r->list[i];
        rows[i].u = r->people[i];
        rows[i].games = r->games[i];
        rows[i].last = r->last[i];
    }
    rows_sort(rows, r->n);
    for (size_t i = 0; i < r->n; i++) {
        r->list[i] = rows[i].p;
        r->people[i] = rows[i].u;
        r->plain[i] = rows[i].plain;
        r->games[i] = rows[i].games;
        r->last[i] = rows[i].last;
    }
    free(rows);
    return 1;
}

static int roster_find(const Roster *r, const char *id)
{
    for (size_t i = 0; i < r->n; i++)
        if (!strcmp(r->list[i].id, id)) return (int)i;
    return -1;
}

// Keeps only the profiles whose name (or NetID, which is never shown) matches the search, the best matches first and otherwise in
// the grid's order as it would be without pins (a search is not pinned). Returns how many there were before.
static int match_rank(const Profile *p, const char *q)
{
    int m = store_name_match(p->name, q);
    if (p->netid[0] && !strncasecmp(p->netid, q, strlen(q))) m = 5; // (5: the NetID starts with it)
    return m;
}

static size_t roster_narrow(Roster *r, const char *q)
{
    size_t total = r->n, kept = 0;
    if (!q || !q[0]) return total;
    for (int rank = 5; rank >= 1; rank--)
        for (;;) { // the next of this rank, in the unpinned order
            size_t best = r->n;
            for (size_t i = kept; i < r->n; i++)
                if (match_rank(&r->list[i], q) == rank && (best == r->n || (r->plain && r->plain[i] < r->plain[best]))) best = i;
            if (best == r->n) break;
            Profile pl = r->list[best];
            UiPerson pu = r->people[best];
            int pp = r->plain ? r->plain[best] : 0;
            memmove(&r->list[kept + 1], &r->list[kept], (best - kept) * sizeof *r->list); // (keeps the rest in their order)
            memmove(&r->people[kept + 1], &r->people[kept], (best - kept) * sizeof *r->people);
            if (r->plain) memmove(&r->plain[kept + 1], &r->plain[kept], (best - kept) * sizeof *r->plain);
            r->list[kept] = pl;
            r->people[kept] = pu;
            if (r->plain) r->plain[kept] = pp;
            kept++;
        }
    r->n = kept;
    return total;
}

static const char *const SORT_NAMES[SORT_COUNT] = {"Recently played", "Name, A to Z", "Newest first", "Most games"};

static void home_fill(UiHome *h, Roster *r, const char *query)
{
    int k = strcmp(g_st.state.holder, UNCLAIMED) ? roster_find(r, g_st.state.holder) : -1; // (before the search narrows it)
    h->card_on = k >= 0;
    if (k >= 0) h->card = r->people[k];
    h->total = (int)roster_narrow(r, query);
    snprintf(h->query, sizeof h->query, "%s", query ? query : "");
    h->people = r->people;
    h->n = (int)r->n;
    h->sort_names = SORT_NAMES;
    h->nsorts = SORT_COUNT;
    h->sort_mode = g_st.cfg.sort_mode;
    snprintf(h->sort_label, sizeof h->sort_label, "%s", SORT_NAMES[g_st.cfg.sort_mode]);
    h->rail = g_st.cfg.sort_mode == SORT_NAME;
    h->loaded = k >= 0 ? roster_find(r, g_st.state.holder) : -1;
    h->loaded_sub[0] = 0;
    h->guest = !h->card_on && g_st.state.wiped;
    if (h->guest) {
        snprintf(h->loaded_sub, sizeof h->loaded_sub, "nothing played right now is saved");
        int gp = g_st.cfg.guest_picture < skin_avatar_count() ? g_st.cfg.guest_picture : -1;
        h->guest_look = SKIN_LOOK(g_st.cfg.guest_swatch, gp);
    }
    if (h->card_on) {
        int games = console_game_count();
        char since[48] = "";
        if (g_st.state.loaded_at > 0) {
            ui_format_when((time_t)g_st.state.loaded_at, (time_t)now_s(), since, sizeof since);
            if (!strncmp(since, "Today, ", 7)) memmove(since, since + 7, strlen(since + 7) + 1);
        }
        if (since[0]) snprintf(h->loaded_sub, sizeof h->loaded_sub, "%d game%s on the console  " DOT "  since %s", games, games == 1 ? "" : "s", since);
        else snprintf(h->loaded_sub, sizeof h->loaded_sub, "%d game%s on the console", games, games == 1 ? "" : "s");
    }
}

// ------------------------------------------------------------------------------------------------ profiles

typedef struct {
    char name[NAME_MAX_BYTES];
    char sub[96];
    int color;
    int loaded;
} PageHeader;

static void profile_header(Cx *c, void *user, float y)
{
    const PageHeader *hd = user;
    Skin *u = c->skin;
    const Theme *t = &u->th;
    float ax = 200 + 46, ay = y + 50;
    skin_avatar(c, ax, ay, 40, hd->name, hd->color);
    { // the picture is ringed in crayon, as on the home screen's tiles: in ink blue for the loaded profile
        PaperSpr *ring = paper_ring(hd->loaded ? 43.0f : 41.0f, hd->loaded ? &PAPER_CRAYON_BLUE : &PAPER_CRAYON_STD, hd->loaded ? 9u : 4u);
        if (ring) paper_spr_put(c->s, ring, (int)ax, (int)ay);
        paper_spr_free(ring);
    }
    char fit[96];
    text_fit(u->semi, 36, -0.3f, hd->name, 700, fit, sizeof fit);
    skin_txt(c, u->semi, 36, -0.3f, ax + 40 + 28, y + 46, 0, t->text, fit);
    skin_txt(c, u->med, 17.5f, 0, ax + 40 + 28, y + 74, 0, t->text2, hd->sub);
}

// A NetID can belong to one profile. 1 = it is free (or empty); 0 = taken, and the person has been told (the admin is told whose).
static int netid_free(const char *clean, const char *except_id, int admin)
{
    Profile owner;
    if (!clean[0] || !store_netid_owner(&g_st, clean, except_id, &owner)) return 1;
    char detail[160];
    if (admin) snprintf(detail, sizeof detail, "%s already belongs to %s.", clean, owner.name);
    else snprintf(detail, sizeof detail, "That NetID already belongs to another profile. Ask the admin if it\xE2\x80\x99s yours.");
    ui_message(UI_WARN, "NetID already used", detail);
    return 0;
}

// A profile's picture: the console's number, -1 for the first letter, or an Extra icon (its number now; its name is what the profile file keeps).
static void set_picture(Profile *p, int pic)
{
    const char *key = custom_is(pic) ? custom_key(pic) : NULL;
    if (custom_is(pic) && !key) pic = -1;
    p->picture = pic;
    snprintf(p->picfile, sizeof p->picfile, "%s", key ? key : "");
}

// Pick a picture and a background colour for a profile (like the Switch\xE2\x80\x99s own profile picture editor).
static void picture_flow(Profile *p)
{
    int pic = p->picture, sw = p->color;
    if (!ui_picture_picker(p->name, &pic, &sw)) return;
    if (pic == p->picture && sw == p->color) return;
    set_picture(p, pic);
    p->color = sw;
    store_profile_save(&g_st, p);
    if (!strcmp(g_st.state.holder, p->id)) sys_profile_sync_warn();
}

// A new profile starts with a random picture (one nobody else has, while there are some left) on a random background from the
// lighter half of each colour, not a letter.
static void random_look(Profile *p)
{
    p->color = store_random_swatch(&g_st);
    int n = skin_avatar_count();
    if (n <= 0) return;
    unsigned char *used = calloc((size_t)n, 1);
    int left = n;
    Profile *list = NULL;
    size_t cnt = 0;
    if (used && store_profile_list(&g_st, &list, &cnt) == 0) {
        for (size_t i = 0; i < cnt; i++)
            if (list[i].picture >= 0 && list[i].picture < n && !used[list[i].picture]) {
                used[list[i].picture] = 1;
                left--;
            }
        free(list);
    }
    if (used && left <= 0) {
        memset(used, 0, (size_t)n);
        left = n;
    }
    int k = (int)(g_be->random(g_be) % (uint32_t)(left > 0 ? left : 1)), pick = k % n;
    if (used)
        for (int i = 0; i < n; i++)
            if (!used[i] && k-- == 0) {
                pick = i;
                break;
            }
    p->picture = pick;
    free(used);
}

// Asks for a NetID (optional): fills `out` with a clean one, or "" when skipped. 0 = the app is quitting.
static int ask_netid(char out[12])
{
    out[0] = 0;
    for (;;) {
        char in[40];
        if (!ui_text_input("NetID (optional, like abc1234)", "", in, sizeof in, g_st.cfg.builtin_keyboard)) return !quitting();
        store_netid_clean(in, out);
        if (out[0] && netid_free(out, NULL, 0)) return 1;
        if (out[0]) {
            out[0] = 0;
            continue;
        }
        ui_message(UI_WARN, "That isn\xE2\x80\x99t a NetID", "Three letters and four numbers, like abc1234. Leave it empty to skip.");
        if (quitting()) return 0;
    }
}

// Name, NetID, password, then the picture: nothing is made until the picture is accepted with OK, and B there drops it all.
static void new_profile_flow(char *created_id, size_t cap)
{
    char name[NAME_MAX_BYTES];
    created_id[0] = 0;
    if (!ui_text_input("What\xE2\x80\x99s your name?", "", name, sizeof name, g_st.cfg.builtin_keyboard)) return;
    if (!store_valid_name(name)) {
        ui_message(UI_WARN, "Enter a name", "Use at least one letter.");
        return;
    }
    char netid[12];
    if (!ask_netid(netid)) return;
    char pin[8];
    char *pinp = NULL;
    if (ui_confirm("Add a 6-digit password?", "Keeps others out of your saves.", "Add one", "Skip", 0, NULL, 0)) {
        if (new_password(NULL, 0, "Choose a 6-digit password", pin)) pinp = pin;
        else if (quitting()) return;
        else ui_message(UI_INFO, "No password", "Add one later in Admin.");
    }
    Profile look;
    memset(&look, 0, sizeof look);
    look.picture = -1;
    look.color = store_random_swatch(&g_st);
    if (skin_avatar_count() > 0) {
        random_look(&look);
        if (!ui_picture_picker(name, &look.picture, &look.color)) return; // B: never mind
    }
    Profile p;
    if (store_profile_create(&g_st, name, pinp, &p) != 0) {
        ui_message(UI_ERROR, "Couldn\xE2\x80\x99t create the profile", "Is the SD card full or locked?");
        return;
    }
    set_picture(&p, look.picture);
    p.color = look.color;
    snprintf(p.netid, sizeof p.netid, "%s", netid);
    store_profile_save(&g_st, &p);
    snprintf(created_id, cap, "%s", p.id);
    // Someone who played as a guest before making their profile can keep that play: it becomes the new profile's saves, loaded.
    // (Otherwise it is thrown away when the next person loads, as always.)
    if (!strcmp(g_st.state.holder, UNCLAIMED) && g_st.state.wiped && !lk_needs_recovery(g_lk)) {
        int played = lk_played_titles(g_lk);
        if (played > 0) {
            char detail[200];
            snprintf(detail, sizeof detail, "What you played as a guest (%d game%s) is saved to %s\xE2\x80\x99s profile, and it\xE2\x80\x99s loaded now.",
                     played, played == 1 ? "" : "s", p.name);
            if (ui_confirm("Keep what you just played?", detail, "Keep it", "Start fresh", 0, p.name, look_of(&p))) do_claim(&p);
        }
    }
}

static void choose_profile(const Profile *p)
{
    if (!strcmp(g_st.state.holder, p->id)) return;
    char who[64];
    snprintf(who, sizeof who, "%s\xE2\x80\x99s password", p->name);
    if (store_profile_has_pin(p) && !ask_password(p->id, p->name, look_of(p), who, check_profile_pin, p)) return;
    if (quitting()) return;

    int games = 0;
    int64_t last = 0;
    lk_profile_summary(g_lk, p->id, &games, &last);
    char ago[48], headline[120], detail[400], holder[48];
    ui_ago_long(last, now_s(), ago, sizeof ago);
    snprintf(headline, sizeof headline, "Play as %s?", p->name);
    profile_name_of(g_st.state.holder, holder, sizeof holder);
    if (!strcmp(g_st.state.holder, UNCLAIMED) && g_st.state.wiped)
        snprintf(detail, sizeof detail, "%d game%s  " DOT "  %s%s\n\nGuest progress isn\xE2\x80\x99t kept.", games,
                 games == 1 ? "" : "s", last > 0 ? "last played " : "", ago);
    else if (!strcmp(g_st.state.holder, UNCLAIMED))
        snprintf(detail, sizeof detail,
                 "%d game%s  " DOT "  %s%s\n\nWhat\xE2\x80\x99s on the console is backed up first.", games,
                 games == 1 ? "" : "s", last > 0 ? "last played " : "", ago);
    else
        snprintf(detail, sizeof detail, "%d game%s  " DOT "  %s%s\n\n%s\xE2\x80\x99s saves are backed up first.",
                 games, games == 1 ? "" : "s", last > 0 ? "last played " : "", ago, holder);
    if (!ui_confirm(headline, detail, "Switch", "Cancel", 0, p->name, look_of(p))) return;
    do_swap(p);
}

// ------------------------------------------------------------------------------------------------ admin

// one log file in the text viewer: its last 300 lines
static void log_view(const char *path)
{
    char *text = NULL;
    size_t len = 0;
    char **lines = NULL;
    int n = 0, cap = 0;
    if (fsx_read_all(path, &text, &len) != 0 || len == 0) {
        free(text);
        text = NULL;
    }
    if (!text) {
        lines = malloc(sizeof *lines);
        lines[n++] = strdup("Nothing has happened yet.");
    } else {
        // the last 300 lines are plenty
        const char *start = text + len;
        int nl = 0;
        while (start > text && nl < 300) {
            start--;
            if (*start == '\n' && start != text + len - 1) nl++;
        }
        if (*start == '\n') start++;
        for (const char *p = start; *p;) {
            const char *e = strchr(p, '\n');
            size_t l = e ? (size_t)(e - p) : strlen(p);
            if (n == cap) {
                cap = cap ? cap * 2 : 64;
                lines = realloc(lines, (size_t)cap * sizeof *lines);
            }
            lines[n] = malloc(l + 1);
            memcpy(lines[n], p, l);
            lines[n][l] = 0;
            n++;
            p += l + (e ? 1 : 0);
            if (!e) break;
        }
        free(text);
    }
    ui_text_viewer("Activity log", lines, n, 1);
    for (int i = 0; i < n; i++) free(lines[i]);
    free(lines);
}

int g_app_logs_listed = -1; // tests: how many logs the Activity log list showed last (-1: none was listed)

// Every start of Washi has a log of its own (store.h). With only this start's, it opens at once; with earlier ones there is a list, newest first.
static void admin_log(void)
{
    char cur[FSX_PATH], dir[FSX_PATH], **names = NULL;
    size_t n = 0;
    store_session_log(&g_st, cur, sizeof cur);
    store_path(&g_st, dir, sizeof dir, "logs");
    if (store_logs_list(&g_st, &names, &n) != 0) n = 0;
    const char *cur_leaf = strrchr(cur, '/') ? strrchr(cur, '/') + 1 : cur;
    int only_this = n == 0;
    for (size_t i = 0; i < n; i++) only_this = only_this || (n == 1 && !strcmp(names[i], cur_leaf));
    if (only_this) {
        log_view(cur);
        fsx_list_free(names, n);
        return;
    }
    int sel = 0, scroll = 0;
    for (;;) {
        UiItem *items = calloc(n, sizeof *items);
        char (*label)[48] = calloc(n, sizeof *label);
        if (!items || !label) {
            free(items), free(label);
            break;
        }
        items_init(items, (int)n);
        for (size_t i = 0; i < n; i++) { // when it started, in the console's own time, as the Backups page says it ("Today, 2:14 PM")
            int64_t t = store_log_started(&g_st, names[i]);
            if (t > 0) ui_format_when((time_t)t, (time_t)now_s(), label[i], sizeof label[i]);
            else snprintf(label[i], sizeof label[i], "%s", names[i]);
            items[i].label = label[i];
            items[i].detail = NULL;
            items[i].toggle = -1;
            items[i].chevron = 1;
        }
        UiList l;
        memset(&l, 0, sizeof l);
        g_app_logs_listed = (int)n;
        l.title = "Activity log";
        l.subtitle = "One log for every start of Washi. Logs older than two weeks are deleted.";
        l.items = items;
        l.n = (int)n;
        l.sel = sel;
        l.scroll = scroll;
        static const Prompt pr[] = {{"A", "Open"}, {"B", "Back"}};
        l.prompts = pr;
        l.nprompts = 2;
        uint64_t pressed;
        int r = ui_list(&l, &pressed);
        sel = l.sel;
        scroll = l.scroll;
        free(items), free(label);
        if (r < 0 || quitting()) break;
        char p[FSX_PATH];
        if (fsx_join(p, sizeof p, dir, names[r]) == 0) log_view(p);
    }
    fsx_list_free(names, n);
}

static void admin_exclusions(void)
{
    int sel = 0, scroll = 0;
    for (;;) {
        TitleSet *ts = malloc(sizeof *ts);
        if (!ts) return;
        lk_list_titles(g_lk, ts);
        for (int i = 0; i < g_st.cfg.nexcluded; i++) {
            int has = 0;
            for (size_t k = 0; k < ts->n; k++)
                if (ts->v[k] == g_st.cfg.excluded[i]) has = 1;
            if (!has && ts->n < LK_MAX_TITLES) ts->v[ts->n++] = g_st.cfg.excluded[i];
        }
        uint64_t dev[64];
        size_t nd = 0; // games whose save is one for the whole console: they can't be swapped per person, so they are listed as always shared
        if (g_be->list_device_saves(g_be, dev, 64, &nd) != BE_OK) nd = 0;
        size_t total = ts->n + nd;
        UiItem *items = calloc(total ? total : 1, sizeof *items);
        items_init(items, (int)(total ? total : 1));
        char (*names)[110] = calloc(total ? total : 1, sizeof *names);
        for (size_t i = 0; i < nd; i++) {
            lk_title_label(g_lk, dev[i], names[ts->n + i], sizeof names[0]);
            items[ts->n + i].label = names[ts->n + i];
            items[ts->n + i].detail = "always shared";
            items[ts->n + i].style = ITEM_DIM;
            items[ts->n + i].toggle = -1;
        }
        for (size_t i = 0; i < ts->n; i++) {
            lk_title_label(g_lk, ts->v[i], names[i], sizeof names[i]);
            int ex = store_is_excluded(&g_st, ts->v[i]);
            items[i].label = names[i];
            items[i].detail = ex ? "shared by everyone" : "kept per person";
            items[i].toggle = ex;
        }
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Shared games";
        l.subtitle = "Shared games keep one save for everyone.";
        l.items = items;
        l.n = (int)total;
        l.sel = sel;
        l.scroll = scroll;
        l.empty = "No games have saves on this account yet.";
        static const Prompt pr[] = {{"A", "Toggle"}, {"B", "Done"}};
        l.prompts = pr;
        l.nprompts = 2;
        uint64_t pressed;
        int r = ui_list(&l, &pressed);
        sel = l.sel;
        scroll = l.scroll;
        if (r >= 0 && (size_t)r < ts->n && pressed == BTN_A) {
            uint64_t t = ts->v[r];
            int found = -1;
            for (int i = 0; i < g_st.cfg.nexcluded; i++)
                if (g_st.cfg.excluded[i] == t) found = i;
            if (found >= 0) g_st.cfg.excluded[found] = g_st.cfg.excluded[--g_st.cfg.nexcluded];
            else if (g_st.cfg.nexcluded < MAX_EXCLUDED) g_st.cfg.excluded[g_st.cfg.nexcluded++] = t;
            save_config_or_warn();
        }
        free(items);
        free(names);
        free(ts);
        if (r < 0 || quitting()) return;
    }
}

static void admin_system_profile(void)
{
    int sel = 0;
    for (;;) {
        char sub[200];
        snprintf(sub, sizeof sub, "Shows the player on the console\xE2\x80\x99s own \xE2\x80\x9C%s\xE2\x80\x9D profile.", g_account);
        UiItem items[8];
        int map[8], n = 0;
        memset(items, 0, sizeof items);
        items_init(items, 8);
        enum { S_TEST, S_NAME, S_PICTURE, S_GUEST, S_NOW, S_LAST, S_RESTORE };
        items[n].label = "Check it works"; items[n].detail = "changes nothing"; items[n].chevron = 1; map[n++] = S_TEST;
        items[n].label = "Show the player\xE2\x80\x99s name"; items[n].toggle = g_st.cfg.sync_name; map[n++] = S_NAME;
        items[n].label = "Show the player\xE2\x80\x99s picture"; items[n].toggle = g_st.cfg.sync_picture; map[n++] = S_PICTURE;
        items[n].label = "Guest picture"; items[n].detail = "when nobody is loaded"; items[n].swatch = g_st.cfg.guest_swatch; items[n].chevron = 1; map[n++] = S_GUEST;
        items[n].label = "Update it now"; items[n].detail = "for the loaded player"; items[n].chevron = 1; map[n++] = S_NOW;
        items[n].label = "Last update"; items[n].detail = g_sys_last[0] ? g_sys_last : "none yet"; items[n].style = ITEM_DIM; map[n++] = S_LAST;
        if (sp_has_original(&g_st)) { items[n].label = "Put the original back"; items[n].chevron = 1; map[n++] = S_RESTORE; }
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Console profile";
        l.subtitle = sub;
        l.items = items;
        l.n = n;
        l.sel = sel < n ? sel : 0;
        int r = ui_list(&l, NULL);
        sel = l.sel;
        if (r < 0 || quitting()) return;
        switch (map[r]) {
        case S_TEST: {
            char nick[40], err[160], m[220];
            int icon_id = 0, bg_id = 0;
            if (sp_probe(g_be, nick, &icon_id, &bg_id, err, sizeof err) == SP_OK) {
                snprintf(m, sizeof m, "\xE2\x80\x9C%s\xE2\x80\x9D  \xC2\xB7  icon %d, background %d", nick, icon_id, bg_id);
                ui_message(UI_OK, "It works", m);
            } else {
                ui_message(UI_WARN, "Not available", err);
            }
            break;
        }
        case S_NAME:
        case S_PICTURE: {
            int *flag = map[r] == S_NAME ? &g_st.cfg.sync_name : &g_st.cfg.sync_picture;
            if (!*flag) {
                char detail[420];
                snprintf(detail, sizeof detail,
                         "The \xE2\x80\x9C%s\xE2\x80\x9D account will show the loaded player. The original is saved first and put back after.",
                         g_account);
                if (!ui_confirm("Change the console\xE2\x80\x99s own profile?", detail, "Turn on", "Cancel", 0, NULL, 0)) break;
                *flag = 1;
                save_config_or_warn();
                sys_profile_sync_warn();
            } else {
                *flag = 0;
                save_config_or_warn();
            }
            break;
        }
        case S_GUEST: {
            int pic = g_st.cfg.guest_picture < skin_avatar_count() ? g_st.cfg.guest_picture : -1, sw = g_st.cfg.guest_swatch;
            if (ui_picture_picker("Guest", &pic, &sw)) {
                g_st.cfg.guest_picture = pic;
                g_st.cfg.guest_swatch = sw;
                save_config_or_warn();
                if (!strcmp(g_st.state.holder, UNCLAIMED)) sys_profile_sync_warn();
            }
            break;
        }
        case S_NOW: {
            if (!g_st.cfg.sync_name && !g_st.cfg.sync_picture) {
                ui_message(UI_INFO, "Turn one on first", NULL);
            } else {
                int rc = sys_profile_sync();
                char who[48], m[220];
                if (!strcmp(g_st.state.holder, UNCLAIMED)) snprintf(who, sizeof who, "the guest picture");
                else profile_name_of(g_st.state.holder, who, sizeof who);
                if (rc > 0) {
                    snprintf(m, sizeof m, "Now showing %s.", who);
                    ui_message(UI_OK, "Updated", m);
                } else if (rc < 0) {
                    ui_message(UI_WARN, "The console\xE2\x80\x99s profile wasn\xE2\x80\x99t updated", g_sys_note);
                }
            }
            break;
        }
        case S_LAST:
            break;
        case S_RESTORE:
            if (ui_confirm("Put the original back?", "Name and picture go back as they were.", "Put back", "Cancel", 0, NULL, 0)) {
                char err[160];
                int rc = sp_restore(&g_st, g_be, err, sizeof err);
                if (rc == SP_OK) ui_message(UI_OK, "Put back", NULL);
                else if (rc == SP_ERR) ui_message(UI_WARN, "Couldn\xE2\x80\x99t put it back", err);
            }
            break;
        }
    }
}

static void admin_account(void)
{
    BeAccount acc[8];
    size_t n = 0;
    if (g_be->list_accounts(g_be, acc, 8, &n) != BE_OK || n == 0) {
        ui_message(UI_ERROR, "Couldn\xE2\x80\x99t read the accounts", "No accounts found.");
        return;
    }
    UiItem items[8];
    memset(items, 0, sizeof items);
        items_init(items, (int)(sizeof items / sizeof items[0]));
    for (size_t i = 0; i < n; i++) {
        items[i].label = acc[i].name;
        items[i].toggle = -1;
        if (!strcmp(acc[i].uid, g_st.cfg.target_uid)) items[i].badge = "In use";
    }
    UiList l;
    memset(&l, 0, sizeof l);
    l.title = "Account";
    l.subtitle = "Whose saves get swapped.";
    l.items = items;
    l.n = (int)n;
    int r = ui_list(&l, NULL);
    if (r < 0) return;
    if (!strcmp(acc[r].uid, g_st.cfg.target_uid)) return;
    char msg[240];
    snprintf(msg, sizeof msg, "Swap the saves of \xE2\x80\x9C%s\xE2\x80\x9D?", acc[r].name);
    if (!ui_confirm("Use this account?", msg, "Use it", "Cancel", 0, NULL, 0)) return;
    if (lk_set_target(g_lk, acc[r].uid) != LK_OK) ui_message(UI_WARN, "Couldn\xE2\x80\x99t change the account", g_lk->err);
    refresh_account();
}

static void admin_appearance(void)
{
    int sel = 0;
    for (;;) {
        static const char *theme_names[3] = {"Follow the console", "Paper", "Paper night"};
        UiItem items[2];
        memset(items, 0, sizeof items);
        items_init(items, (int)(sizeof items / sizeof items[0]));
        items[0].label = "Theme";
        items[0].detail = theme_names[g_st.cfg.theme_mode % 3];
        items[0].toggle = -1;
        items[1].label = "Clock";
        items[1].detail = g_st.cfg.clock24 ? "24-hour" : "12-hour";
        items[1].toggle = -1;
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Appearance";
        l.items = items;
        l.n = 2;
        l.sel = sel;
        static const Prompt pr[] = {{"A", "Change"}, {"B", "Back"}};
        l.prompts = pr;
        l.nprompts = 2;
        int r = ui_list(&l, NULL);
        sel = l.sel;
        if (r < 0 || quitting()) return;
        if (r == 0) {
            g_st.cfg.theme_mode = (g_st.cfg.theme_mode + 1) % 3;
            ui_set_theme_mode(g_st.cfg.theme_mode);
        } else {
            g_st.cfg.clock24 = !g_st.cfg.clock24;
            ui_set_clock24(g_st.cfg.clock24);
        }
        save_config_or_warn();
    }
}

static void admin_rollback(const Profile *p)
{
    char **names;
    size_t n;
    if (lk_history_list(g_lk, p->id, &names, &n) != 0) {
        ui_message(UI_ERROR, "Couldn\xE2\x80\x99t read the history", "Check the SD card.");
        return;
    }
    if (n == 0) {
        ui_message(UI_INFO, "No earlier saves yet", NULL);
        if (names) fsx_list_free(names, n);
        return;
    }
    UiItem *items = calloc(n, sizeof *items);
    items_init(items, (int)n);
    char (*lab)[64] = calloc(n, sizeof *lab);
    char (*det)[32] = calloc(n, sizeof *det);
    char dir[FSX_PATH];
    store_profile_dir(&g_st, p->id, dir, sizeof dir);
    for (size_t i = 0; i < n; i++) {
        const char *nm = names[n - 1 - i]; // newest first
        ui_format_when((time_t)store_parse_ts(nm), (time_t)now_s(), lab[i], sizeof lab[i]);
        char hp[FSX_PATH], sub[FSX_PATH];
        snprintf(sub, sizeof sub, "history/%s", nm);
        fsx_join(hp, sizeof hp, dir, sub);
        char **t;
        size_t nt = 0;
        if (fsx_list(hp, &t, &nt) == 0) fsx_list_free(t, nt);
        snprintf(det[i], sizeof det[i], "%zu game%s", nt, nt == 1 ? "" : "s");
        items[i].label = lab[i];
        items[i].detail = det[i];
        items[i].badge = i == 0 ? "Most recent" : NULL;
        items[i].toggle = -1;
    }
    UiList l;
    memset(&l, 0, sizeof l);
    l.title = "Roll back";
    char sub[120];
    snprintf(sub, sizeof sub, "%s\xE2\x80\x99s earlier saves", p->name);
    l.subtitle = sub;
    l.items = items;
    l.n = (int)n;
    int r = ui_list(&l, NULL);
    if (r >= 0 && !quitting()) {
        char head[160];
        snprintf(head, sizeof head, "Roll %s back to %s?", p->name, lab[r]);
        if (ui_confirm(head, "Their current saves are kept too. Loads next time they pick their name.",
                       "Roll back", "Cancel", 0, p->name, look_of(p))) {
            progress_open(OP_ROLLBACK, p->name, p->name);
            int rc = lk_rollback(g_lk, p->id, names[n - 1 - (size_t)r]);
            ui_progress_end();
            store_reload(&g_st);
            if (rc == LK_OK) ui_message(UI_OK, "Rolled back", "Loads next time they pick their name.");
            else ui_message(UI_WARN, "Couldn\xE2\x80\x99t roll back", g_lk->err);
        }
    }
    free(items);
    free(lab);
    free(det);
    fsx_list_free(names, n);
}

// What a player can change about themselves: picture and background, name, NetID (never shown here, only whether there is one).
static void edit_profile_flow(const char *id)
{
    int sel = 0;
    for (;;) {
        Profile p;
        if (store_profile_load(&g_st, id, &p) != 0) return;
        PageHeader hd;
        snprintf(hd.name, sizeof hd.name, "%s", p.name);
        hd.color = look_of(&p);
        hd.loaded = 1;
        snprintf(hd.sub, sizeof hd.sub, "Your picture, name and NetID");
        UiItem items[3];
        memset(items, 0, sizeof items);
        items_init(items, 3);
        enum { E_PICTURE, E_NAME, E_NETID };
        items[E_PICTURE].label = "Picture and background"; items[E_PICTURE].swatch = p.color % SKIN_COLORS; items[E_PICTURE].chevron = 1; items[E_PICTURE].toggle = -1;
        items[E_NAME].label = "Name"; items[E_NAME].detail = p.name; items[E_NAME].chevron = 1; items[E_NAME].toggle = -1;
        items[E_NETID].label = "NetID"; items[E_NETID].detail = p.netid[0] ? "set" : "not set"; items[E_NETID].chevron = 1; items[E_NETID].toggle = -1;
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Edit profile";
        l.items = items;
        l.n = 3;
        l.sel = sel;
        l.draw_header = profile_header;
        l.user = &hd;
        l.header_h = 100;
        l.row_h = 68;
        int r = ui_list(&l, NULL);
        sel = l.sel;
        if (r < 0 || quitting()) return;
        if (r == E_PICTURE) {
            picture_flow(&p);
        } else if (r == E_NAME) {
            char nm[NAME_MAX_BYTES];
            if (ui_text_input("Your name", p.name, nm, sizeof nm, g_st.cfg.builtin_keyboard)) {
                if (store_valid_name(nm)) {
                    snprintf(p.name, sizeof p.name, "%s", nm);
                    store_profile_save(&g_st, &p);
                    if (!strcmp(g_st.state.holder, p.id)) sys_profile_sync_warn();
                } else {
                    ui_message(UI_WARN, "Enter a name", "Use at least one letter.");
                }
            }
        } else {
            char in[40];
            if (ui_text_input("Your NetID (like abc1234)", "", in, sizeof in, g_st.cfg.builtin_keyboard)) {
                char clean[12];
                store_netid_clean(in, clean);
                if (!clean[0]) {
                    ui_message(UI_WARN, "That isn't a NetID", "Three letters and four numbers, like abc1234.");
                } else if (netid_free(clean, p.id, 0)) {
                    snprintf(p.netid, sizeof p.netid, "%s", clean);
                    store_profile_save(&g_st, &p);
                }
            }
        }
    }
}

// what an admin_profile visit changed, for the page it was opened from: its name, picture or NetID; its saves (rolled back, or the
// profile deleted)
enum { PROFILE_LOOK = 1, PROFILE_SAVES = 2 };

static int admin_profile(const char *id)
{
    int sel = 0, changed = 0;
    for (;;) {
        Profile p;
        if (store_profile_load(&g_st, id, &p) != 0) return changed | PROFILE_SAVES;
        int games = 0;
        lk_profile_summary(g_lk, p.id, &games, NULL);
        PageHeader hd;
        snprintf(hd.name, sizeof hd.name, "%s", p.name);
        hd.color = look_of(&p);
        hd.loaded = !strcmp(g_st.state.holder, p.id);
        snprintf(hd.sub, sizeof hd.sub, "%d game%s  " DOT "  %s%s", games, games == 1 ? "" : "s",
                 store_profile_has_pin(&p) ? "password set" : "no password", hd.loaded ? "  " DOT "  loaded now" : "");

        UiItem items[8];
        int map[8], n = 0;
        memset(items, 0, sizeof items);
        items_init(items, (int)(sizeof items / sizeof items[0]));
        enum { A_RENAME, A_NETID, A_REMOVE_NETID, A_PASSWORD, A_REMOVE_PW, A_PICTURE, A_ROLLBACK, A_DELETE };
        items[n].label = "Rename"; items[n].chevron = 1; items[n].toggle = -1; map[n++] = A_RENAME;
        items[n].label = "NetID"; items[n].detail = p.netid[0] ? p.netid : "none"; items[n].chevron = 1; items[n].toggle = -1; map[n++] = A_NETID;
        if (p.netid[0]) { items[n].label = "Remove NetID"; items[n].toggle = -1; map[n++] = A_REMOVE_NETID; }
        items[n].label = store_profile_has_pin(&p) ? "Change password" : "Add a password"; items[n].chevron = 1; items[n].toggle = -1; map[n++] = A_PASSWORD;
        if (store_profile_has_pin(&p)) { items[n].label = "Remove password"; items[n].toggle = -1; map[n++] = A_REMOVE_PW; }
        items[n].label = "Picture and background"; items[n].detail = p.picture >= 0 ? "Picture" : "First letter"; items[n].swatch = p.color % SKIN_COLORS; items[n].chevron = 1; map[n++] = A_PICTURE;
        items[n].label = "Roll back to earlier saves"; items[n].chevron = 1; items[n].toggle = -1; map[n++] = A_ROLLBACK;
        items[n].label = "Delete profile"; items[n].style = ITEM_DANGER; items[n].toggle = -1; map[n++] = A_DELETE;

        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Profile";
        l.items = items;
        l.n = n;
        l.sel = sel < n ? sel : 0;
        l.draw_header = profile_header;
        l.user = &hd;
        l.header_h = 100;
        l.row_h = 64;
        int r = ui_list(&l, NULL);
        sel = l.sel;
        if (r < 0 || quitting()) return changed;
        switch (map[r]) {
        case A_RENAME: {
            char nm[NAME_MAX_BYTES];
            if (ui_text_input("New name", p.name, nm, sizeof nm, g_st.cfg.builtin_keyboard)) {
                if (store_valid_name(nm)) {
                    snprintf(p.name, sizeof p.name, "%s", nm);
                    store_profile_save(&g_st, &p);
                    changed |= PROFILE_LOOK;
                    if (!strcmp(g_st.state.holder, p.id)) sys_profile_sync_warn();
                } else {
                    ui_message(UI_WARN, "Enter a name", "Use at least one letter.");
                }
            }
            break;
        }
        case A_NETID: {
            char in[40];
            if (ui_text_input("NetID (like abc1234)", p.netid, in, sizeof in, g_st.cfg.builtin_keyboard)) {
                char clean[12];
                store_netid_clean(in, clean);
                if (clean[0]) {
                    if (netid_free(clean, p.id, 1)) {
                        snprintf(p.netid, sizeof p.netid, "%s", clean);
                        store_profile_save(&g_st, &p);
                        changed |= PROFILE_LOOK;
                    }
                } else {
                    ui_message(UI_WARN, "That isn\xE2\x80\x99t a NetID", "Three letters and four numbers, like abc1234.");
                }
            }
            break;
        }
        case A_REMOVE_NETID:
            p.netid[0] = 0;
            store_profile_save(&g_st, &p);
            changed |= PROFILE_LOOK;
            break;
        case A_PASSWORD: {
            char pin[8];
            if (new_password(p.name, look_of(&p), "Choose a 6-digit password", pin)) store_profile_set_pin(&g_st, &p, pin);
            break;
        }
        case A_REMOVE_PW:
            if (ui_confirm("Remove the password?", "Anyone can load it.", "Remove", "Cancel", 0, p.name, look_of(&p)))
                store_profile_set_pin(&g_st, &p, NULL);
            break;
        case A_PICTURE:
            picture_flow(&p);
            changed |= PROFILE_LOOK;
            break;
        case A_ROLLBACK:
            if (!strcmp(g_st.state.holder, p.id))
                ui_message(UI_WARN, "Their saves are loaded", "Sign out first.");
            else {
                admin_rollback(&p);
                changed |= PROFILE_SAVES;
            }
            break;
        case A_DELETE: {
            if (!strcmp(g_st.state.holder, p.id)) {
                ui_message(UI_WARN, "Their saves are loaded", "Sign out first.");
                break;
            }
            char head[120];
            snprintf(head, sizeof head, "Delete %s\xE2\x80\x99s profile?", p.name);
            if (ui_confirm(head, "It goes to Deleted profiles, and can be restored from there.",
                           "Delete", "Cancel", 1, p.name, look_of(&p))) {
                if (lk_delete_profile(g_lk, p.id) == LK_OK) return changed | PROFILE_SAVES;
                ui_message(UI_ERROR, "Couldn\xE2\x80\x99t delete the profile", g_lk->err);
            }
            break;
        }
        }
    }
}

static void admin_profiles(void)
{
    int sel = 0, scroll = 0;
    for (;;) {
        Roster r = {0};
        roster_load(&r);
        UiItem *items = calloc(r.n ? r.n : 1, sizeof *items);
        items_init(items, (int)(r.n ? r.n : 1));
        for (size_t i = 0; i < r.n; i++) {
            items[i].label = r.people[i].name;
            items[i].detail = r.people[i].detail;
            items[i].avatar_name = r.people[i].name;
            items[i].avatar_color = r.people[i].color;
            items[i].chevron = 1;
            items[i].toggle = -1;
            if (!strcmp(g_st.state.holder, r.list[i].id)) items[i].badge = "Loaded";
        }
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Profiles";
        l.items = items;
        l.n = (int)r.n;
        l.sel = sel;
        l.scroll = scroll;
        l.empty = "No profiles yet.";
        static const Prompt pr[] = {{"A", "Manage"}, {"B", "Back"}};
        l.prompts = pr;
        l.nprompts = 2;
        int c = ui_list(&l, NULL);
        sel = l.sel;
        scroll = l.scroll;
        char id[ID_MAX] = "";
        if (c >= 0) snprintf(id, sizeof id, "%s", r.list[c].id);
        free(items);
        roster_free(&r);
        if (c < 0 || quitting()) return;
        admin_profile(id);
    }
}

#define PURGE_WAIT_MS 3000 // how long "delete all" makes you wait before it can be confirmed

// Deleted profiles: what is in profiles/.trash, the latest first. A puts one back with its saves and history.
static void admin_deleted_page(void)
{
    int sel = 0, scroll = 0;
    for (;;) {
        TrashEntry *t = NULL;
        size_t n = 0;
        store_trash_tidy(&g_st); // (what an interrupted delete left behind)
        if (store_trash_list(&g_st, &t, &n) != 0) {
            ui_message(UI_ERROR, "Couldn\xE2\x80\x99t read the deleted profiles", "Check the SD card.");
            return;
        }
        UiItem *items = calloc(n ? n : 1, sizeof *items);
        char (*sub)[120] = calloc(n ? n : 1, sizeof *sub);
        items_init(items, (int)(n ? n : 1));
        int64_t now = now_s();
        for (size_t i = 0; i < n; i++) {
            char ago[48] = "", games[24] = "no games";
            if (t[i].games) snprintf(games, sizeof games, "%d game%s", t[i].games, t[i].games == 1 ? "" : "s");
            if (t[i].deleted > 0) ui_ago_long(t[i].deleted, now, ago, sizeof ago);
            if (!t[i].restorable) snprintf(sub[i], sizeof sub[i], "its profile file is damaged");
            else if (ago[0]) snprintf(sub[i], sizeof sub[i], "deleted %s  " DOT "  %s", ago, games);
            else snprintf(sub[i], sizeof sub[i], "%s", games);
            items[i].label = t[i].name;
            items[i].sub = sub[i];
            items[i].avatar_name = t[i].name;
            items[i].avatar_color = SKIN_LOOK(t[i].color, t[i].picture);
            items[i].chevron = t[i].restorable;
            if (!t[i].restorable) items[i].style = ITEM_DIM;
        }
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Deleted profiles";
        l.subtitle = "Restoring one brings back its saves and history.";
        l.items = items;
        l.n = (int)n;
        l.sel = sel;
        l.scroll = scroll;
        l.empty = "Nothing has been deleted.";
        static const Prompt pr[] = {{"A", "Restore"}, {"X", "Delete for good"}, {"B", "Back"}};
        static const Prompt pr_all[] = {{"A", "Restore"}, {"X", "Delete for good"}, {"ZR+X", "Delete all"}, {"B", "Back"}};
        l.prompts = n > 1 ? pr_all : pr;
        l.nprompts = n > 1 ? 4 : 3;
        if (!n) { // (with nothing listed there is only Back)
            static const Prompt back[] = {{"B", "Back"}};
            l.prompts = back;
            l.nprompts = 1;
        }
        l.extra = BTN_X | BTN_PURGE;
        uint64_t pressed = 0;
        int c = ui_list(&l, &pressed);
        sel = l.sel;
        scroll = l.scroll;
        TrashEntry pick;
        memset(&pick, 0, sizeof pick);
        char pick_sub[120] = "";
        if (c >= 0) {
            pick = t[c];
            snprintf(pick_sub, sizeof pick_sub, "%s", sub[c]);
        }
        size_t listed = n;
        free(items);
        free(sub);
        free(t);
        if (c < 0 || quitting()) return;
        if (pressed & BTN_PURGE) { // X and ZR together: every deleted profile, after a wait
            char head[120], note[160];
            snprintf(head, sizeof head, listed == 1 ? "Delete the one deleted profile for good?" : "Delete all %zu deleted profiles for good?", listed);
            snprintf(note, sizeof note, "Every one of their saves and histories will be gone. This can\xE2\x80\x99t be undone.");
            if (ui_confirm_wait(head, note, "Delete all", "Cancel", PURGE_WAIT_MS)) {
                ui_busy("Deleted profiles", "Deleting for good\xE2\x80\xA6");
                TrashEntry *all = NULL;
                size_t na = 0, failed = 0;
                if (store_trash_list(&g_st, &all, &na) == 0) {
                    for (size_t i = 0; i < na; i++)
                        if (store_trash_purge(&g_st, all[i].leaf) != 0) failed++;
                    free(all);
                } else {
                    failed = 1;
                }
                if (failed) ui_message(UI_ERROR, "Couldn\xE2\x80\x99t delete everything", "Is the SD card locked? What is left is cleared the next time you open this page.");
            }
            sel = scroll = 0;
            continue;
        }
        if (pressed & BTN_X) { // X: delete it for good
            char head[120];
            snprintf(head, sizeof head, "Delete %.60s for good?", pick.name);
            if (!ui_confirm(head, "Their saves and history can\xE2\x80\x99t be brought back.", "Delete", "Cancel", 1, pick.name, SKIN_LOOK(pick.color, pick.picture)))
                continue;
            if (store_trash_purge(&g_st, pick.leaf) != 0) ui_message(UI_ERROR, "Couldn\xE2\x80\x99t delete it", "Is the SD card locked? What is left is cleared the next time you open this page.");
            if (sel > 0 && (size_t)sel >= n - 1) sel = (int)n - 2 < 0 ? 0 : (int)n - 2; // (the list is one shorter)
            continue;
        }
        if (!pick.restorable) {
            ui_message(UI_WARN, "This one can\xE2\x80\x99t be restored", "Its profile file is damaged. Its folder is still in profiles/.trash on the SD card.");
            continue;
        }
        char head[120];
        snprintf(head, sizeof head, "Restore %.60s?", pick.name);
        if (!ui_confirm(head, pick_sub, "Restore", "Cancel", 0, pick.name, SKIN_LOOK(pick.color, pick.picture))) continue;
        int cleared = 0, rc = store_profile_restore(&g_st, pick.leaf, NULL, &cleared);
        if (rc == 0) {
            snprintf(head, sizeof head, "%.60s is back", pick.name);
            ui_message(UI_OK, head, cleared ? "Its NetID belongs to someone else now, so it came back without one." : NULL);
        } else if (rc == -2) {
            ui_message(UI_WARN, "Couldn\xE2\x80\x99t restore it", "A profile with the same folder name already exists.");
        } else {
            ui_message(UI_ERROR, "Couldn\xE2\x80\x99t restore it", "Is the SD card full or locked?");
        }
    }
}

static void admin_deleted(void)
{
    ui_x_on_release(1); // (X alone and X + ZR are told apart when they are let go)
    admin_deleted_page();
    ui_x_on_release(0);
}

// ---- storage: what is free on the SD card, whose saves use the most, and the earlier saves that could go

typedef struct {
    char name[NAME_MAX_BYTES], id[ID_MAX];
    int look;
    uint32_t tint;
    uint64_t bytes;
} StorageRow;

typedef struct {
    int have_space;                   // the card said how big it is and what is free
    uint64_t free_b, total_b;
    uint64_t other_b;                 // saves that are nobody's now: deleted profiles, and the console's own first saves (unclaimed)
    StorageRow *rows;                 // every profile, the largest first
    UiStoragePerson *view;            // the same, as the page wants them
    int n;
    uint64_t trim_bytes;              // what "Delete older backups now" would free
    int trim_entries;
} StorageInfo;

#define STORAGE_ROWS 5 // rows on the Storage page; with more profiles the last of them is everyone else together

static void storage_free(StorageInfo *si)
{
    free(si->rows);
    free(si->view);
    memset(si, 0, sizeof *si);
}

static void storage_row_fill(StorageRow *r, const Profile *p)
{
    snprintf(r->name, sizeof r->name, "%s", p->name);
    snprintf(r->id, sizeof r->id, "%s", p->id);
    r->look = look_of(p);
    r->tint = profile_tint(SWATCH_RGB[(p->color % SWATCH_COUNT + SWATCH_COUNT) % SWATCH_COUNT], p->name, p->netid);
}

static int cmp_storage(const void *a, const void *b)
{
    const StorageRow *x = a, *y = b;
    if (x->bytes != y->bytes) return x->bytes > y->bytes ? -1 : 1;
    return strcasecmp(x->name, y->name);
}

static void storage_estimate(StorageInfo *si)
{
    si->trim_bytes = 0;
    si->trim_entries = 0;
    lk_history_trim(g_lk, g_st.cfg.history_keep, 1, &si->trim_bytes, &si->trim_entries);
}

static void storage_sort(StorageInfo *si)
{
    if (si->n) qsort(si->rows, (size_t)si->n, sizeof *si->rows, cmp_storage);
    for (int i = 0; i < si->n; i++) {
        si->view[i].name = si->rows[i].name;
        si->view[i].look = si->rows[i].look;
        si->view[i].bytes = si->rows[i].bytes;
        si->view[i].tint = si->rows[i].tint;
    }
}

// Reads the card: this walks every folder Washi keeps, so it is done once on the way in and again after something is deleted.
static void storage_scan(StorageInfo *si)
{
    storage_free(si);
    uint64_t fb = 0, tb = 0;
    if (g_be->free_space(g_be, &fb) == BE_OK && g_be->total_space(g_be, &tb) == BE_OK && tb > 0 && fb <= tb) {
        si->have_space = 1;
        si->free_b = fb;
        si->total_b = tb;
    }
    Profile *list = NULL;
    size_t n = 0;
    if (store_profile_list(&g_st, &list, &n) != 0) n = 0;
    si->rows = calloc(n ? n : 1, sizeof *si->rows);
    si->view = calloc(n ? n : 1, sizeof *si->view);
    if (!si->rows || !si->view) n = 0;
    for (size_t i = 0; i < n; i++) {
        char dir[FSX_PATH];
        store_profile_dir(&g_st, list[i].id, dir, sizeof dir);
        storage_row_fill(&si->rows[i], &list[i]);
        si->rows[i].bytes = fsx_tree_size(dir);
    }
    si->n = (int)n;
    free(list);
    char p[FSX_PATH];
    store_path(&g_st, p, sizeof p, "profiles/.trash");
    si->other_b = fsx_tree_size(p);
    store_path(&g_st, p, sizeof p, "unclaimed");
    si->other_b += fsx_tree_size(p);
    storage_sort(si);
    storage_estimate(si);
}

// after a profile's page: what changed there (see admin_profile) shows on the way back
static void storage_after_profile(StorageInfo *si, const char *id, int changed)
{
    if (changed & PROFILE_SAVES) { // deleted or rolled back: the sizes changed
        ui_busy("Storage", "Adding up what is on the SD card");
        storage_scan(si);
        return;
    }
    Profile p;
    for (int i = 0; (changed & PROFILE_LOOK) && i < si->n; i++)
        if (!strcmp(si->rows[i].id, id) && store_profile_load(&g_st, id, &p) == 0) { // a new name, picture or NetID
            storage_row_fill(&si->rows[i], &p);
            storage_sort(si);
            break;
        }
}

static int storage_shown(const StorageInfo *si) { return si->n <= STORAGE_ROWS ? si->n : STORAGE_ROWS - 1; }

// Everyone, the largest first: the "N others" row opened out into a page of its own (it grows out of the profiles' card, and
// shrinks back into it). A opens someone's page, B goes back.
static void storage_everyone(StorageInfo *si, UiStorage *page)
{
    int sel = storage_shown(si), scroll = 0;
    float r[4];
    ui_storage_card_rect(page, r);
    ui_next_grows_from(r[0], r[1], r[2], r[3]);
    for (;;) {
        int n = si->n;
        UiItem *items = calloc(n ? n : 1, sizeof *items);
        char (*sizes)[24] = calloc(n ? n : 1, sizeof *sizes);
        if (!items || !sizes) {
            free(items);
            free(sizes);
            return;
        }
        items_init(items, n ? n : 1);
        uint64_t total = si->other_b, largest = n ? si->rows[0].bytes : 0;
        for (int i = 0; i < n; i++) {
            total += si->rows[i].bytes;
            ui_format_bytes(si->rows[i].bytes, sizes[i], sizeof sizes[i]);
            items[i].label = si->rows[i].name;
            items[i].detail = sizes[i];
            items[i].avatar_name = si->rows[i].name;
            items[i].avatar_color = si->rows[i].look;
            items[i].chevron = 1;
            items[i].has_bar = 1;
            items[i].bar = largest ? (float)((double)si->rows[i].bytes / (double)largest) : 0;
            items[i].bar_rgb = si->rows[i].tint;
        }
        char sub[96], all[24];
        ui_format_bytes(total, all, sizeof all);
        snprintf(sub, sizeof sub, "%d profile%s  " DOT "  %s of saves, the largest first", n, n == 1 ? "" : "s", all);
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Everyone\xE2\x80\x99s saves";
        l.subtitle = sub;
        l.items = items;
        l.n = n;
        l.sel = sel;
        l.scroll = scroll;
        l.empty = "No profiles yet.";
        static const Prompt pr[] = {{"A", "Open"}, {"B", "Back"}};
        l.prompts = pr;
        l.nprompts = 2;
        int c = ui_list(&l, NULL);
        sel = l.sel;
        scroll = l.scroll;
        char id[ID_MAX] = "";
        if (c >= 0) snprintf(id, sizeof id, "%s", si->rows[c].id);
        free(items);
        free(sizes);
        if (c < 0 || quitting()) break;
        storage_after_profile(si, id, admin_profile(id));
        for (int i = 0; i < si->n; i++) // the frame stays on them, wherever they are now
            if (!strcmp(si->rows[i].id, id)) sel = i;
    }
    page->npeople = si->n; // (someone may have been deleted on the way: the card is shorter now)
    page->shown = storage_shown(si);
    ui_storage_card_rect(page, r);
    if (!quitting()) ui_next_shrinks_into(r[0], r[1], r[2], r[3]);
}

static void admin_storage(void)
{
    static const int KEEPS[] = {1, 2, 3, 5, 10};
    enum { NKEEPS = sizeof KEEPS / sizeof KEEPS[0] };
    StorageInfo si;
    memset(&si, 0, sizeof si);
    ui_busy("Storage", "Adding up what is on the SD card");
    storage_scan(&si);
    UiStorage s;
    memset(&s, 0, sizeof s);
    for (;;) {
        int keep = g_st.cfg.history_keep;
        char d_keep[16], d_trim[64] = "nothing to delete", sz[24];
        snprintf(d_keep, sizeof d_keep, "%d", keep);
        UiItem items[2];
        memset(items, 0, sizeof items);
        items_init(items, 2);
        items[0].label = "Backups kept per profile";
        items[0].detail = d_keep;
        items[0].chevron = 1;
        items[1].label = "Delete older backups now";
        items[1].sub = d_trim;
        if (si.trim_entries) {
            ui_format_bytes(si.trim_bytes, sz, sizeof sz);
            snprintf(d_trim, sizeof d_trim, "frees about %s", sz);
            items[1].chevron = 1;
        } else {
            items[1].style = ITEM_DIM;
        }
        s.have_space = si.have_space;
        s.free_b = si.free_b;
        s.total_b = si.total_b;
        s.other_b = si.other_b;
        s.people = si.view;
        s.npeople = si.n;
        s.shown = storage_shown(&si);
        s.actions = items;
        s.nactions = 2;
        if (!ui_storage(&s) || quitting()) break;
        if (!s.in_actions) {
            if (s.sel < s.shown) {
                char id[ID_MAX];
                snprintf(id, sizeof id, "%s", si.rows[s.sel].id);
                storage_after_profile(&si, id, admin_profile(id));
                for (int i = 0; i < storage_shown(&si); i++) // the frame stays on them if they still have a row
                    if (!strcmp(si.rows[i].id, id)) s.sel = i;
            } else {
                storage_everyone(&si, &s);
                s.sel = STORAGE_ROWS; // back on the others' row (or the last row, if there are fewer now)
            }
            continue;
        }
        if (s.act == 0) { // how many earlier saves each profile keeps (what a backup trims to, and what "Delete older backups now" leaves)
            char labels[NKEEPS][8];
            UiChoice ch[NKEEPS];
            int cur = NKEEPS - 1;
            for (int i = NKEEPS - 1; i >= 0; i--) {
                snprintf(labels[i], sizeof labels[i], "%d", KEEPS[i]);
                ch[i].title = labels[i];
                ch[i].sub = NULL;
                ch[i].dim = 0;
                ch[i].sw = 0;
                if (KEEPS[i] >= keep) cur = i;
            }
            int c = ui_choice("Backups kept per profile", "Earlier saves, for rolling back. Older ones go at the next backup.", ch, NKEEPS, cur);
            if (c >= 0 && KEEPS[c] != keep) {
                g_st.cfg.history_keep = KEEPS[c];
                save_config_or_warn();
                storage_estimate(&si);
            }
        } else if (!si.trim_entries) {
            ui_message(UI_INFO, "Nothing to delete", NULL);
        } else {
            char detail[120], head[64];
            if (keep == 1) snprintf(detail, sizeof detail, "Keeps only the latest for each profile. This can\xE2\x80\x99t be undone.");
            else snprintf(detail, sizeof detail, "Keeps the latest %d for each profile. This can\xE2\x80\x99t be undone.", keep);
            if (!ui_confirm("Delete older backups?", detail, "Delete", "Cancel", 1, NULL, 0)) continue;
            ui_busy("Storage", "Deleting older backups");
            uint64_t freed = 0;
            int gone = 0, rc = lk_history_trim(g_lk, keep, 0, &freed, &gone);
            char why[sizeof g_lk->err];
            snprintf(why, sizeof why, "%s", g_lk->err);
            storage_scan(&si);
            ui_format_bytes(freed, sz, sizeof sz);
            snprintf(head, sizeof head, "Freed %s", sz);
            snprintf(detail, sizeof detail, "%d older backup%s deleted.", gone, gone == 1 ? "" : "s");
            if (rc == LK_OK) ui_message(UI_OK, head, detail);
            else ui_message(UI_WARN, gone ? "Couldn\xE2\x80\x99t delete them all" : "Nothing was deleted", why);
        }
    }
    storage_free(&si);
}

// How long someone's saves may stay loaded before the app asks, when it is opened, whether they are still playing (ask_still_there):
// hours and minutes on two wheels, in steps of 15 minutes. 0:00, or "Turn off", turns it off.
static void admin_reminder(void)
{
    static const char *const HOURS[] = {"0", "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12"};
    static const char *const MINS[] = {"00", "15", "30", "45"};
    static const int MIN_AT[] = {0, 15, 30, 45};
    int usual = g_st.cfg.remind_minutes;
    int cur = usual > 0 ? usual : 180; // off: the wheels start at the usual three hours
    if (cur > 12 * 60 + 45) cur = 12 * 60 + 45;                             // a longer one set by hand in config.json
    int mi = 0;
    for (int i = 0; i < 4; i++)
        if (cur % 60 >= MIN_AT[i]) mi = i; // (one set by hand that is none of these, the old 1-minute test's included: the one just under it)
    UiWheel w[2] = {{HOURS, 13, cur / 60, "hours", "hour"}, {MINS, 4, mi, "min", NULL}};
    int r = ui_wheels("Sign-out reminder", "Asks \xE2\x80\x9Cis this still you?\xE2\x80\x9D when the app opens.", w, 2, "Turn off");
    if (r == 0) return;
    int minutes = r == 2 ? 0 : w[0].sel * 60 + MIN_AT[w[1].sel];
    if (minutes == g_st.cfg.remind_minutes) return;
    g_st.cfg.remind_minutes = minutes;
    save_config_or_warn();
}

// Test the in-game reminder -> a test note now: asks the reminder (sysmodule/, which checks every few seconds) to show a test
// note now, and says what came of it: it showed one, it runs but Ultrahand isn't there, or it isn't running at all.
static void reminder_test_now(void)
{
    char ask[FSX_PATH], done[FSX_PATH];
    store_path(&g_st, ask, sizeof ask, "reminder-test");
    store_path(&g_st, done, sizeof done, "reminder-test.done");
    remove(done);
    if (fsx_write_file(ask, "test", 4) != 0) {
        ui_message(UI_ERROR, "Couldn\xE2\x80\x99t write to the SD card", "Is it full or locked?");
        return;
    }
    ui_busy("In-game reminder", "Asking the reminder to show a test note\xE2\x80\xA6");
    char *answer = NULL;
    for (int i = 0; i < 24 && !answer && !quitting(); i++) { // (up to 12 seconds)
        ui_wait(500);
        if (fsx_read_all(done, &answer, NULL) != 0) answer = NULL;
    }
    if (quitting()) {
        free(answer);
        return;
    }
    if (!answer) {
        remove(ask); // (so it doesn't go off later, when the reminder does start)
        ui_message(UI_WARN, "The reminder isn\xE2\x80\x99t running",
                   "It starts with the console: restart it and try again. Its files are in atmosphere/contents/4200000000534C4B on the SD card.");
    } else if (!strncmp(answer, "ok", 2)) {
        ui_message(UI_OK, "Test reminder sent", "It pops up in a moment, over this screen or a game, the way the real one will.");
    } else {
        ui_message(UI_WARN, "Ultrahand isn\xE2\x80\x99t set up",
                   "The reminder is running, but Ultrahand\xE2\x80\x99s notifications folder (config/ultrahand/notifications) isn\xE2\x80\x99t on the SD card.");
    }
    free(answer);
    remove(done);
}

// Test the in-game reminder -> repeat every 30 seconds: leaves a file (reminder-repeat) that the reminder looks for; while it is there a note comes every 30
// seconds, over a game too, whoever is loaded, until the file is taken away here. It outlasts the app: close Washi, start a game, watch for the notes.
static int reminder_repeat_on(void)
{
    char path[FSX_PATH];
    store_path(&g_st, path, sizeof path, "reminder-repeat");
    return fsx_exists(path);
}

// What the switch asks for: put the file there or take it away, then say what is TRUE (read back from the card), so the switch never shows more than the card holds.
// -1: the card would not take it.
static int repeat_flip(void *user, int index, int want)
{
    (void)user;
    (void)index;
    char path[FSX_PATH];
    store_path(&g_st, path, sizeof path, "reminder-repeat");
    if (want) {
        if (fsx_write_file(path, "30", 2) != 0) return -1;
    } else if (fsx_exists(path) && remove(path) != 0) {
        return -1;
    }
    return reminder_repeat_on();
}

// Admin -> Console -> Test the in-game reminder: a test note now, and a switch for the note that comes every 30 seconds until it is switched off
static void admin_reminder_test(void)
{
    const UiChoice items[2] = {{"Show a test note now", "Checks it\xE2\x80\x99s set up: a note pops up within seconds.", 0, 0},
                               {"Repeat every 30 seconds", "A note over a game too, until you switch it off.", 0, reminder_repeat_on() ? 2 : 1}};
    int c = ui_choice_sw("Test the in-game reminder", NULL, items, 2, 0, repeat_flip, NULL);
    if (c == 0) reminder_test_now();
}

// A leftover from the old "the real one, in a minute" test (the reminder was set to one minute, the usual setting kept in remind_restore) ends when the app opens:
// the usual reminder setting comes back.
static void end_reminder_test(void)
{
    if (g_st.cfg.remind_restore < 0) return;
    g_st.cfg.remind_minutes = g_st.cfg.remind_restore;
    g_st.cfg.remind_restore = -1;
    store_save_config(&g_st);
}

// ------------------------------------------------------------------------------------------------ unclaimed saves

// A profile to give something to; the one whose saves are loaded can't be picked. 1 = *out is it.
static int pick_profile(const char *title, const char *subtitle, Profile *out)
{
    int sel = 0, scroll = 0;
    for (;;) {
        Roster r = {0};
        roster_load(&r);
        UiItem *items = calloc(r.n ? r.n : 1, sizeof *items);
        items_init(items, (int)(r.n ? r.n : 1));
        for (size_t i = 0; i < r.n; i++) {
            items[i].label = r.people[i].name;
            items[i].detail = r.people[i].detail;
            items[i].avatar_name = r.people[i].name;
            items[i].avatar_color = r.people[i].color;
            items[i].toggle = -1;
            if (!strcmp(g_st.state.holder, r.list[i].id)) {
                items[i].badge = "Loaded";
                items[i].style = ITEM_DIM;
            }
        }
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = title;
        l.subtitle = subtitle;
        l.items = items;
        l.n = (int)r.n;
        l.sel = sel;
        l.scroll = scroll;
        l.empty = "There are no profiles yet.";
        int c = ui_list(&l, NULL);
        sel = l.sel;
        scroll = l.scroll;
        int picked = c >= 0 && (size_t)c < r.n && !quitting();
        if (picked) *out = r.list[c];
        free(items);
        roster_free(&r);
        if (!picked) return 0;
        if (strcmp(g_st.state.holder, out->id) != 0) return 1;
        ui_message(UI_WARN, "Their saves are loaded", "Sign out first.");
    }
}

// "Mario Kart 8 Deluxe, Splatoon 3 and 2 more"
static void unclaimed_games(const LkUnclaimed *u, char *out, size_t n)
{
    out[0] = 0;
    if (u->ngames == 0) {
        snprintf(out, n, "no games");
        return;
    }
    int shown = u->ngames > 3 ? 2 : u->ngames;
    for (int i = 0; i < shown && i < LK_UNCLAIMED_TITLES; i++) {
        char name[64];
        lk_title_label(g_lk, u->titles[i], name, sizeof name);
        size_t len = strlen(out);
        snprintf(out + len, n - len, "%s%s", i == 0 ? "" : i == shown - 1 && shown == u->ngames ? " and " : ", ", name);
    }
    if (shown < u->ngames) {
        size_t len = strlen(out);
        snprintf(out + len, n - len, " and %d more", u->ngames - shown);
    }
}

static int unclaimed_count(void)
{
    char dir[FSX_PATH], **names;
    size_t n;
    int k = 0;
    store_path(&g_st, dir, sizeof dir, "unclaimed");
    if (!fsx_is_dir(dir) || fsx_list(dir, &names, &n) != 0) return 0;
    for (size_t i = 0; i < n; i++) k += strncmp(names[i], "staging-", 8) != 0;
    fsx_list_free(names, n);
    return k;
}

// Admin -> Games and saves -> Unclaimed saves: what was on the console when Washi first loaded someone, and what was played
// without signing in. Each can be given to a profile (their saves now; what they had goes to Roll back) or deleted.
static void admin_unclaimed(void)
{
    int sel = 0, scroll = 0;
    for (;;) {
        LkUnclaimed *u = NULL;
        size_t n = 0;
        if (lk_unclaimed_list(g_lk, &u, &n) != 0) {
            ui_message(UI_ERROR, "Couldn\xE2\x80\x99t read the unclaimed saves", "Check the SD card.");
            return;
        }
        size_t m = n ? n : 1;
        UiItem *items = calloc(m, sizeof *items);
        char (*lab)[64] = calloc(m, sizeof *lab), (*sub)[200] = calloc(m, sizeof *sub), (*det)[24] = calloc(m, sizeof *det);
        items_init(items, (int)m);
        time_t now = (time_t)now_s();
        for (size_t i = 0; i < n; i++) {
            if (u[i].when > 0) ui_format_when((time_t)u[i].when, now, lab[i], sizeof lab[i]);
            else snprintf(lab[i], sizeof lab[i], "%s", u[i].leaf);
            unclaimed_games(&u[i], sub[i], sizeof sub[i]);
            ui_format_bytes(u[i].bytes, det[i], sizeof det[i]);
            items[i].label = lab[i];
            items[i].sub = sub[i];
            items[i].detail = det[i];
            items[i].chevron = 1;
            items[i].toggle = -1;
        }
        UiList l;
        memset(&l, 0, sizeof l);
        l.title = "Unclaimed saves";
        l.subtitle = "From before Washi, or played without signing in.";
        l.items = items;
        l.n = (int)n;
        l.sel = sel;
        l.scroll = scroll;
        l.empty = "Nothing unclaimed.";
        int c = ui_list(&l, NULL);
        sel = l.sel;
        scroll = l.scroll;
        LkUnclaimed pick;
        char pick_lab[64] = "", pick_sub[200] = "";
        if (c >= 0 && (size_t)c < n) {
            pick = u[c];
            snprintf(pick_lab, sizeof pick_lab, "%s", lab[c]);
            snprintf(pick_sub, sizeof pick_sub, "%s", sub[c]);
        }
        free(items);
        free(lab);
        free(sub);
        free(det);
        free(u);
        if (c < 0 || (size_t)c >= n || quitting()) return;

        const UiChoice what[2] = {{"Give to a profile", "They become that profile\xE2\x80\x99s saves.", 0}, {"Delete", NULL, 0}};
        int a = ui_choice(pick_lab, pick_sub, what, 2, 0);
        if (a == 0) {
            Profile p;
            if (!pick_profile("Give to", pick_sub, &p)) continue;
            char head[120];
            snprintf(head, sizeof head, "Give these saves to %s?", p.name);
            if (!ui_confirm(head, "Their current saves go to Roll back. Their games that aren\xE2\x80\x99t in these stay as they are.",
                            "Give", "Cancel", 0, p.name, look_of(&p)))
                continue;
            progress_open(OP_GIVE, "Unclaimed saves", p.name);
            int rc = lk_unclaimed_give(g_lk, pick.leaf, p.id);
            ui_progress_end();
            store_reload(&g_st);
            snprintf(head, sizeof head, "They\xE2\x80\x99re %s\xE2\x80\x99s now", p.name);
            if (rc == LK_OK) ui_message(UI_OK, head, "Loads next time they pick their name.");
            else ui_message(UI_WARN, "Couldn\xE2\x80\x99t give them", g_lk->err);
        } else if (a == 1) {
            if (ui_confirm("Delete these saves?", "They can\xE2\x80\x99t be brought back.", "Delete", "Cancel", 1, NULL, 0) &&
                lk_unclaimed_delete(g_lk, pick.leaf) != LK_OK)
                ui_message(UI_ERROR, "Couldn\xE2\x80\x99t delete them", g_lk->err);
        }
    }
}

// ------------------------------------------------------------------------------------------------ backups of everything

// "sdmc:/<leaf>": at the root of the SD card (the store lives in <root>/switch/washi)
static void backups_folder(const char *root, const char *leaf, char *out, size_t n)
{
    char base[FSX_PATH];
    snprintf(base, sizeof base, "%s", root);
    size_t len = strlen(base);
    static const char usual[] = "/switch/washi";
    if (len >= sizeof usual - 1 && !strcmp(base + len - (sizeof usual - 1), usual)) base[len - (sizeof usual - 1)] = 0;
    else if (strrchr(base, '/')) *strrchr(base, '/') = 0; // not the usual place: just beside it
    if (base[0] && base[strlen(base) - 1] == ':') strncat(base, "/", sizeof base - strlen(base) - 1);
    fsx_join(out, n, base, leaf);
}

static void snapshot_parent(char *out, size_t n)
{
    backups_folder(g_st.root, "Washi Backups", out, n);
}

// The app used to be called Save Locker and kept everything in /switch/save-locker and the backups in "Save Locker Backups". The first
// time the renamed app starts, those folders are renamed (never merged, never overwritten: if the new name is taken the old one is left).
void app_adopt_old_names(const char *root)
{
    char old_root[FSX_PATH], old_b[FSX_PATH], new_b[FSX_PATH];
    snprintf(old_root, sizeof old_root, "%s", root);
    char *slash = strrchr(old_root, '/');
    if (slash && !strcmp(slash, "/washi")) {
        strcpy(slash, "/save-locker");
        if (!fsx_exists(root) && fsx_exists(old_root)) rename(old_root, root);
    }
    backups_folder(root, "Washi Backups", new_b, sizeof new_b);
    backups_folder(root, "Save Locker Backups", old_b, sizeof old_b);
    if (!fsx_exists(new_b) && fsx_exists(old_b)) rename(old_b, new_b);
}

// A backup of everything Washi keeps, with a progress screen; 0 = made. With an auto_note (whoever signed out, "" if unknown)
// it is an automatic one: the first of its day, and only the newest few of those are kept.
static int make_backup(const char *auto_note, char *leaf_out, size_t leaf_n)
{
    char parent[FSX_PATH], stamp[32], where[FSX_PATH + 40];
    snapshot_parent(parent, sizeof parent);
    ui_format_stamp((time_t)now_s(), stamp, sizeof stamp);
    progress_open(OP_SNAPSHOT, "Washi", "Washi");
    int rc = auto_note ? lk_snapshot_auto(g_lk, parent, stamp, auto_note, LK_AUTO_BACKUPS_KEPT, where, sizeof where)
                       : lk_snapshot(g_lk, parent, stamp, where, sizeof where);
    ui_progress_end();
    if (rc == LK_OK && leaf_out) {
        const char *leaf = strrchr(where, '/');
        snprintf(leaf_out, leaf_n, "%.90s", leaf ? leaf + 1 : where);
    }
    return rc;
}

// The first time somebody signs out (the console is cleared for the next player) each day: a backup of everything, the newest few kept. It runs
// after the sign-out has finished, so the person who just left is in it. If it can't be made, sign-out has still worked.
static void auto_backup(const char *who)
{
    char parent[FSX_PATH], stamp[32];
    snapshot_parent(parent, sizeof parent);
    ui_format_stamp((time_t)now_s(), stamp, sizeof stamp);
    if (!lk_snapshot_auto_due(g_lk, parent, stamp)) return; // one a day is enough
    int rc = make_backup(who, NULL, 0);
    if (rc != LK_OK && rc != LK_NOOP) ui_message(UI_WARN, "Signed out, but the automatic backup didn\xE2\x80\x99t happen", g_lk->err);
}

static void backup_now(void)
{
    char detail[320], size[24], leaf[96];
    ui_format_bytes(fsx_tree_size(g_st.root), size, sizeof size);
    snprintf(detail, sizeof detail, "About %s, copied to \"Washi Backups\" on the SD card.", size);
    if (strcmp(g_st.state.holder, UNCLAIMED))
        strncat(detail, "\nWhoever is loaded: their latest play is only included once they sign out.", sizeof detail - strlen(detail) - 1);
    if (!ui_confirm("Back up everything?", detail, "Back up", "Cancel", 0, NULL, 0)) return;
    if (make_backup(NULL, leaf, sizeof leaf) == LK_OK) {
        snprintf(detail, sizeof detail, "Washi Backups/%.200s", leaf);
        ui_message(UI_OK, "Backed up", detail);
    } else {
        ui_message(UI_ERROR, "Couldn\xE2\x80\x99t back up", g_lk->err);
    }
}

static void restore_backup(const LkSnapshot *s)
{
    if (strcmp(g_st.state.holder, UNCLAIMED)) {
        ui_message(UI_WARN, "Sign out first", "Someone\xE2\x80\x99s saves are loaded. Sign out, then restore.");
        return;
    }
    if (!ui_confirm("Restore this backup?",
                    "Everything Washi keeps goes back to how it was then: profiles, saves, settings. A backup of how it is now is made first.",
                    "Restore", "Cancel", 1, NULL, 0))
        return;
    char parent[FSX_PATH], stamp[32];
    snapshot_parent(parent, sizeof parent);
    ui_format_stamp((time_t)now_s(), stamp, sizeof stamp);
    progress_open(OP_RESTORE, "Washi", "Washi");
    int rc = lk_snapshot_restore(g_lk, parent, s->leaf, stamp);
    ui_progress_end();
    store_reload(&g_st);
    if (rc == LK_OK) ui_message(UI_OK, "Restored", "A backup of how it was before is in Washi Backups.");
    else ui_message(UI_ERROR, "Couldn\xE2\x80\x99t restore", g_lk->err);
}

// Admin -> Games and saves -> Backups: back up now, or open one to restore or delete it. The ones made by hand come first.
static void admin_backups(void)
{
    int sel = 0, first = 0; // what is in focus, and how far the row of tiles has slid: the page comes back as it was left
    for (;;) {
        char parent[FSX_PATH];
        snapshot_parent(parent, sizeof parent);
        LkSnapshot *v = NULL;
        size_t n = 0;
        if (lk_snapshot_list(g_lk, parent, &v, &n) != LK_OK) {
            ui_message(UI_ERROR, "Couldn\xE2\x80\x99t read the backups", g_lk->err);
            return;
        }
        size_t *order = calloc(n ? n : 1, sizeof *order); // the ones made by hand first (they are never deleted), then the automatic ones
        UiBackup *items = calloc(n ? n : 1, sizeof *items);
        size_t k = 0;
        for (int pass = 0; pass < 2; pass++)
            for (size_t i = 0; i < n; i++)
                if (v[i].automatic == pass) order[k++] = i;
        time_t now = (time_t)now_s();
        for (size_t r = 0; r < n; r++) {
            const LkSnapshot *s = &v[order[r]];
            UiBackup *u = &items[r];
            ui_format_stamp_when(s->leaf, now, u->when, sizeof u->when);
            ui_format_bytes(s->bytes, u->size, sizeof u->size);
            u->manual = !s->automatic;
            if (s->automatic && s->note[0]) snprintf(u->what, sizeof u->what, "After %.40s signed out", s->note);
            else snprintf(u->what, sizeof u->what, "%s", s->automatic ? "Automatic backup" : "Made by hand");
            u->nprofiles = s->nprofiles;
            u->npeople = s->npeople;
            for (int p = 0; p < s->npeople; p++) {
                snprintf(u->name[p], sizeof u->name[p], "%s", s->name[p]);
                u->look[p] = SKIN_LOOK(s->color[p], s->picture[p]);
            }
        }
        UiBackups page = {items, (int)n, sel, first};
        int ok = ui_backups(&page);
        sel = page.sel;
        first = page.first;
        free(items);
        if (!ok || quitting()) {
            free(v);
            free(order);
            return;
        }
        if (page.sel == 0) {
            free(v);
            free(order);
            backup_now();
            continue;
        }
        LkSnapshot pick = v[order[page.sel - 1]];
        free(v);
        free(order);
        if (ok == 2) { // X: delete it (no menu first)
            char head[64], note[120];
            ui_format_stamp_when(pick.leaf, now, head, sizeof head);
            snprintf(note, sizeof note, "%s. It can\xE2\x80\x99t be brought back.", head);
            if (ui_confirm("Delete this backup?", note, "Delete", "Cancel", 1, NULL, 0) && lk_snapshot_delete(g_lk, parent, pick.leaf) != LK_OK)
                ui_message(UI_ERROR, "Couldn\xE2\x80\x99t delete it", g_lk->err);
            continue;
        }
        const UiChoice what[2] = {{"Restore this backup", "Everything goes back to how it was then.", 0}, {"Delete this backup", NULL, 0}};
        char head[64];
        ui_format_stamp_when(pick.leaf, now, head, sizeof head);
        int a = ui_choice(head, pick.automatic ? "An automatic backup." : "Made by hand.", what, 2, 0);
        if (a == 0) {
            restore_backup(&pick);
        } else if (a == 1) {
            if (ui_confirm("Delete this backup?", "It can\xE2\x80\x99t be brought back.", "Delete", "Cancel", 1, NULL, 0) &&
                lk_snapshot_delete(g_lk, parent, pick.leaf) != LK_OK)
                ui_message(UI_ERROR, "Couldn\xE2\x80\x99t delete it", g_lk->err);
        }
    }
}

// The admin hub: four sections, each with a few rows that open a page. The rows say how things stand right now.
static void admin_flow(void)
{
    if (store_admin_has_pin(&g_st) && !ask_password("admin", NULL, 0, "Admin password", check_admin_pin, NULL)) return;
    if (quitting()) return;
    enum { AD_PROFILES, AD_DELETED, AD_PASSWORD, AD_SHARED, AD_STORAGE, AD_UNCLAIMED, AD_LOG, AD_BACKUP, AD_ACCOUNT, AD_SYSPROFILE, AD_REMINDER, AD_REMINDER_TEST, AD_APPEARANCE, AD_UPDATE, AD_ICONS, AD_ABOUT };
    UiHub hub;
    memset(&hub, 0, sizeof hub);
    int ndeleted = -1; // deleted profiles that can be restored: counted on the way in, and again after a page that can change it
    for (;;) {
        Profile *plist = NULL;
        size_t nprof = 0;
        if (store_profile_list(&g_st, &plist, &nprof) == 0) free(plist);
        else nprof = 0;
        if (ndeleted < 0) {
            TrashEntry *trash = NULL;
            size_t nt = 0;
            ndeleted = 0;
            if (store_trash_list(&g_st, &trash, &nt) == 0)
                for (size_t i = 0; i < nt; i++) ndeleted += trash[i].restorable;
            free(trash);
        }
        char d_prof[32], d_del[32] = "none", d_ex[32], d_free[40] = "", freeb[24] = "", summary[160];
        if (ndeleted) snprintf(d_del, sizeof d_del, "%d can be restored", ndeleted);
        snprintf(d_prof, sizeof d_prof, "%zu profile%s", nprof, nprof == 1 ? "" : "s");
        if (g_st.cfg.nexcluded) snprintf(d_ex, sizeof d_ex, "%d shared", g_st.cfg.nexcluded);
        else snprintf(d_ex, sizeof d_ex, "none");
        uint64_t free_bytes = 0;
        if (g_be->free_space(g_be, &free_bytes) == BE_OK) ui_format_bytes(free_bytes, freeb, sizeof freeb);
        char d_unc[32] = "none";
        int nunc = unclaimed_count();
        if (nunc) snprintf(d_unc, sizeof d_unc, "%d to sort out", nunc);
        if (freeb[0]) snprintf(d_free, sizeof d_free, "%s free", freeb);
        if (freeb[0]) snprintf(summary, sizeof summary, "%s  " DOT "  %s  " DOT "  version %s", d_prof, d_free, ui_version());
        else snprintf(summary, sizeof summary, "%s  " DOT "  version %s", d_prof, ui_version());
        const char *d_sys = g_st.cfg.sync_name && g_st.cfg.sync_picture ? "name and picture" : g_st.cfg.sync_name ? "name" : g_st.cfg.sync_picture ? "picture" : "off";
        char d_rem[48] = "off";
        if (g_st.cfg.remind_minutes > 0) {
            char dur[32];
            ui_format_minutes(g_st.cfg.remind_minutes, dur, sizeof dur);
            snprintf(d_rem, sizeof d_rem, "after %s", dur);
        }
        const char *d_test = reminder_repeat_on() ? "notes every 30 s" : NULL; // (the repeating test goes on until it is turned off here: say so)

        const UiHubRow people[] = {{"Profiles", d_prof, SKIN_ICON_PERSON}, {"Deleted profiles", d_del, SKIN_ICON_TRASH}, {"Admin password", NULL, SKIN_ICON_LOCK}};
        const UiHubRow games[] = {{"Shared games", d_ex, SKIN_ICON_GAMES}, {"Storage", d_free, SKIN_ICON_DRIVE},
                                  {"Unclaimed saves", d_unc, SKIN_ICON_INBOX}, {"Backups", NULL, SKIN_ICON_DRIVE}};
        const UiHubRow console[] = {{"Account", g_account, SKIN_ICON_CONSOLE}, {"Console profile", d_sys, SKIN_ICON_CONSOLE}, {"Sign-out reminder", d_rem, SKIN_ICON_CLOCK},
                                    {"Test the in-game reminder", d_test, SKIN_ICON_CLOCK}};
        const UiHubRow app[] = {{"Appearance", NULL, SKIN_ICON_CONTRAST}, {"Activity log", NULL, SKIN_ICON_LOG}, {"Check for updates", NULL, SKIN_ICON_UPDATE},
                                {"Extra icons", NULL, SKIN_ICON_GROUP}, {"About", NULL, SKIN_ICON_INFO}};
        static const int acts[4][5] = {{AD_PROFILES, AD_DELETED, AD_PASSWORD}, {AD_SHARED, AD_STORAGE, AD_UNCLAIMED, AD_BACKUP}, {AD_ACCOUNT, AD_SYSPROFILE, AD_REMINDER, AD_REMINDER_TEST}, {AD_APPEARANCE, AD_LOG, AD_UPDATE, AD_ICONS, AD_ABOUT}};
        // (no line of words under the sheets: the rows say it)
        const UiHubSection sections[4] = {
            {"People", SKIN_ICON_GROUP, SKIN_HUE_BLUE, people, 3, NULL},
            {"Games and saves", SKIN_ICON_GAMES, SKIN_HUE_ORANGE, games, 4, NULL},
            {"Console", SKIN_ICON_CONSOLE, SKIN_HUE_GREEN, console, 4, NULL},
            {"This app", SKIN_ICON_CART, SKIN_HUE_PURPLE, app, 5, NULL},
        };
        hub.title = "Admin";
        hub.summary = summary;
        hub.sections = sections;
        hub.nsections = 4;
        if (!ui_hub(&hub) || quitting()) return;
        switch (acts[hub.sel_section][hub.sel_row]) {
        case AD_PROFILES:
            admin_profiles();
            ndeleted = -1;
            break;
        case AD_DELETED:
            admin_deleted();
            ndeleted = -1;
            break;
        case AD_PASSWORD: {
            char pin[8];
            if (new_password(NULL, 0, "Choose a 6-digit admin password", pin) && store_admin_set_pin(&g_st, pin) == 0)
                ui_message(UI_OK, "Admin password changed", NULL);
            break;
        }
        case AD_SHARED: admin_exclusions(); break;
        case AD_STORAGE: admin_storage(); break;
        case AD_UNCLAIMED: admin_unclaimed(); break;
        case AD_LOG: admin_log(); break;
        case AD_BACKUP: admin_backups(); break;
        case AD_ACCOUNT: admin_account(); break;
        case AD_SYSPROFILE: admin_system_profile(); break;
        case AD_REMINDER: admin_reminder(); break;
        case AD_REMINDER_TEST: admin_reminder_test(); break;
        case AD_APPEARANCE: admin_appearance(); break;
        case AD_UPDATE: ui_update(); break; // (GitHub, lii-lac/washi: fails with "no releases" until the repo has one)
        case AD_ICONS: ui_icons_admin(); break; // (the character pictures of Nintendo Switch Online and the games, from GitHub: henry-debruin/nso-icons)
        case AD_ABOUT: ui_about(); break;
        }
        if (quitting()) return;
    }
}

// ------------------------------------------------------------------------------------------------ about

static void about(void)
{
    ui_about();
}

// ------------------------------------------------------------------------------------------------ home

// Opened while someone's saves have been loaded for hours: ask whether they are still playing, so the next person doesn't play into
// them. "Still me" starts the clock again.
// Hours after someone loaded their saves the app asks whether it is still them, over the home screen h (filled). 1 if they signed out (h is then out of date).
static int ask_still_there(UiHome *h)
{
    if (!strcmp(g_st.state.holder, UNCLAIMED) || g_st.cfg.remind_minutes <= 0 || g_st.state.loaded_at <= 0) return 0;
    int64_t minutes = (now_s() - g_st.state.loaded_at) / 60;
    if (minutes < g_st.cfg.remind_minutes) return 0;
    char who[48], head[120], detail[160], dur[32];
    profile_name_of(g_st.state.holder, who, sizeof who);
    snprintf(head, sizeof head, "Is this still %s?", who);
    ui_format_minutes(minutes >= 60 ? (int)(minutes / 15 * 15) : (int)minutes, dur, sizeof dur); // to the quarter hour
    snprintf(detail, sizeof detail, "%s's saves have been loaded for %s.", who, dur);
    UiChoice items[2] = {{"Yes, still me", NULL, 0}, {"Sign out", "Backs up and clears the console.", 0}};
    ui_home_backdrop(h);
    int c = ui_choice(head, detail, items, 2, 0);
    if (c == 1) {
        do_sign_out();
        return 1;
    }
    if (c == 0) {
        g_st.state.loaded_at = now_s();
        store_save_state(&g_st);
    }
    return 0;
}

static void home_loop(void)
{
    Roster r = {0};
    UiHome h;
    memset(&h, 0, sizeof h);
    char keep[ID_MAX] = "", query[sizeof h.query] = ""; // query: what the grid is narrowed to
    // The first home screen is filled before anything is shown: the intro (when nothing has been shown before it) comes down on it and ends on it, and then "is
    // this still you?" and any message waiting are asked over it.
    roster_load(&r);
    home_fill(&h, &r, query);
    ui_intro_play(&h);
    int reload = quitting() ? 0 : ask_still_there(&h);
    int reload_fast = 0; // the roster was already put right (a pin): reload only the screen
    g_pin_open_until = 0; // (a run of the home screen starts with the pin gate shut)
    char newer[32];
    if (!quitting() && ui_rolled_back(newer, sizeof newer)) { // (a new version did not get going, so this start is the one kept from before it)
        g_pending.active = 1;
        g_pending.kind = UI_WARN;
        snprintf(g_pending.head, sizeof g_pending.head, "Washi went back to version %s", ui_version());
        if (newer[0]) snprintf(g_pending.detail, sizeof g_pending.detail, "Version %s didn\xE2\x80\x99t start properly, twice, so this one was put back.", newer);
        else snprintf(g_pending.detail, sizeof g_pending.detail, "The new version didn\xE2\x80\x99t start properly, twice, so this one was put back.");
    }
    while (!quitting()) {
        if (reload) {
            if (keep[0] == 0 && r.list && h.sel >= 0 && (size_t)h.sel < r.n) snprintf(keep, sizeof keep, "%s", r.list[h.sel].id);
            char toast[96], newsel[ID_MAX];
            int toast_kind = h.toast_kind;
            snprintf(toast, sizeof toast, "%s", h.toast);
            snprintf(newsel, sizeof newsel, "%s", keep);
            if (!reload_fast) roster_load(&r);
            reload_fast = 0;
            home_fill(&h, &r, query);
            int k = newsel[0] ? roster_find(&r, newsel) : -1;
            if (k >= 0) h.sel = k;
            else if (!newsel[0] && h.sel > h.n) h.sel = h.n;
            snprintf(h.toast, sizeof h.toast, "%s", toast);
            h.toast_kind = toast_kind;
            keep[0] = 0;
            reload = 0;
        }
        if (g_pending.active) {
            g_pending.active = 0;
            ui_home_backdrop(&h);
            ui_message(g_pending.kind, g_pending.head, g_pending.detail);
            if (quitting()) break;
        }
        int ev = ui_home(&h);
        if (ev < 0) break;
        if (ev != HOME_PIN) g_pin_open_until = 0; // (anything but pinning shuts the gate: the next pin asks for the password again)
        h.toast[0] = 0;
        h.toast_kind = 0;
        switch (ev) {
        case HOME_CHOOSE:
            if ((size_t)h.sel < r.n) {
                Profile p = r.list[h.sel];
                if (!strcmp(g_st.state.holder, p.id)) {
                    snprintf(h.toast, sizeof h.toast, "%s\xE2\x80\x99s saves are already loaded.", p.name);
                    h.toast_kind = 1;
                } else {
                    snprintf(keep, sizeof keep, "%s", p.id);
                    choose_profile(&p);
                    if (!strcmp(g_st.state.holder, p.id)) query[0] = 0; // found and loaded: everyone again
                    reload = 1;
                }
            }
            break;
        case HOME_BANNER: // A on the card: the loaded player's profile; for the guest, what being a guest means
            if (!strcmp(g_st.state.holder, UNCLAIMED)) {
                snprintf(h.toast, sizeof h.toast, "Pick your name below to keep what you play.");
                h.toast_kind = 1;
                break;
            }
            /* fall through */
        case HOME_PICTURE: { // Plus: edit the loaded profile; with nobody loaded, the profile that is selected (its password first, if it has one): nobody becomes loaded
            int k = strcmp(g_st.state.holder, UNCLAIMED) ? roster_find(&r, g_st.state.holder) : -1;
            if (k < 0 && h.sel >= 0 && (size_t)h.sel < r.n) {
                Profile sp = r.list[h.sel];
                char who[64];
                snprintf(who, sizeof who, "%s\xE2\x80\x99s password", sp.name);
                if (store_profile_has_pin(&sp) && !ask_password(sp.id, sp.name, look_of(&sp), who, check_profile_pin, &sp)) break;
                if (quitting()) break;
                k = h.sel;
            }
            if (k >= 0) {
                snprintf(keep, sizeof keep, "%s", r.list[k].id);
                edit_profile_flow(r.list[k].id);
                reload = 1;
            } else {
                snprintf(h.toast, sizeof h.toast, "Pick your name first.");
                h.toast_kind = 1;
            }
            break;
        }
        case HOME_SORT: // another order was picked from the chip's menu
            if (h.sort_mode >= 0 && h.sort_mode < SORT_COUNT && h.sort_mode != g_st.cfg.sort_mode) {
                g_st.cfg.sort_mode = h.sort_mode;
                save_config_or_warn();
                if (r.list && (size_t)h.sel < r.n) snprintf(keep, sizeof keep, "%s", r.list[h.sel].id);
                reload = 1;
            }
            break;
        case HOME_PIN: { // an admin pins the profile in focus (it comes first in every order) or unpins it; the grid shuffles to the new order
            if ((size_t)h.sel >= r.n) break;
            Profile p;
            // a flood gate: one admin password opens pinning, and it stays open while pins keep coming (each one holds it open another minute); it shuts
            // a minute after the last one, or at once when anything else is done on Home
            if (store_admin_has_pin(&g_st) && now_s() >= g_pin_open_until && !ask_password("admin", NULL, 0, "Admin password", check_admin_pin, NULL)) break;
            g_pin_open_until = now_s() + PIN_GATE_S;
            if (quitting() || store_profile_load(&g_st, r.list[h.sel].id, &p) != 0) break;
            int64_t t = now_s();
            for (size_t i = 0; i < r.n; i++) // (after every pin there is: the newest pinned last)
                if (r.list[i].pinned >= t) t = r.list[i].pinned + 1;
            p.pinned = p.pinned ? 0 : t;
            if (store_profile_save(&g_st, &p) != 0) {
                ui_message(UI_ERROR, "Couldn\xE2\x80\x99t save that", "The SD card couldn\xE2\x80\x99t be written to.");
                break;
            }
            snprintf(keep, sizeof keep, "%s", p.id);
            h.pin_anim = p.pinned ? 1 : -1;
            reload_fast = !query[0] && roster_repin(&r, (size_t)h.sel, &p);
            snprintf(h.toast, sizeof h.toast, p.pinned ? "%s is pinned: first in every order." : "%s isn\xE2\x80\x99t pinned any more.", p.name);
            h.toast_kind = 0;
            reload = 1;
            break;
        }
        case HOME_CLEAR: // everyone again, the frame still on whoever it was on
            query[0] = 0;
            if (r.list && (size_t)h.sel < r.n) snprintf(keep, sizeof keep, "%s", r.list[h.sel].id);
            reload = 1;
            break;
        case HOME_DONE:
            if (!strcmp(g_st.state.holder, UNCLAIMED)) {
                snprintf(h.toast, sizeof h.toast, g_st.state.wiped ? "You\xE2\x80\x99re already a guest." : "Nobody is signed in.");
                h.toast_kind = 1;
            } else {
                char who[48];
                profile_name_of(g_st.state.holder, who, sizeof who);
                if (do_sign_out()) snprintf(h.toast, sizeof h.toast, "%s\xE2\x80\x99s saves are backed up.", who);
                if (r.list && (size_t)h.sel < r.n) snprintf(keep, sizeof keep, "%s", r.list[h.sel].id);
                reload = 1;
            }
            break;
        case HOME_SEARCH: { // the grid narrows to who matches (see store_name_match), the best first
            char q[sizeof query];
            if (ui_text_input("Find a profile", query, q, sizeof q, g_st.cfg.builtin_keyboard)) {
                snprintf(query, sizeof query, "%s", q);
                roster_load(&r);
                home_fill(&h, &r, query);
                h.sel = 0;
                h.top_row = 0;
                h.focus = 0; // the frame goes to the best match, even if the search was opened from the bar
                if (!h.n) {
                    snprintf(h.toast, sizeof h.toast, "No profile matches \xE2\x80\x9C%s\xE2\x80\x9D", q);
                    h.toast_kind = 1;
                }
            }
            break;
        }
        case HOME_NEW: {
            char id[ID_MAX];
            new_profile_flow(id, sizeof id);
            if (id[0]) {
                query[0] = 0; // (the new profile shows, whatever was searched for)
                snprintf(keep, sizeof keep, "%s", id);
                reload = 1;
                Profile p;
                if (strcmp(g_st.state.holder, id) && store_profile_load(&g_st, id, &p) == 0) // (kept a guest's play: it says so instead)
                    snprintf(h.toast, sizeof h.toast, "Welcome, %s!", p.name);
            }
            break;
        }
        case HOME_ADMIN:
            admin_flow();
            if (r.list && (size_t)h.sel < r.n) snprintf(keep, sizeof keep, "%s", r.list[h.sel].id);
            reload = 1;
            break;
        case HOME_ABOUT:
            about();
            break;
        }
    }
    roster_free(&r);
}

// ------------------------------------------------------------------------------------------------ first run

static int choose_account(int first_run)
{
    BeAccount acc[8];
    size_t n = 0;
    if (g_be->list_accounts(g_be, acc, 8, &n) != BE_OK || n == 0) {
        ui_message(UI_ERROR, "No user accounts found", "Make the club\xE2\x80\x99s user account in System Settings first.");
        return 0;
    }
    BeAccount def;
    if (first_run && lk_default_account(g_lk, &def)) {
        if (ui_confirm("Use the \xE2\x80\x9C" "Esports Switch\xE2\x80\x9D account?",
                       "Its saves get swapped between people.", "Yes", "Choose another", 0, NULL, 0)) {
            int ok = lk_set_target(g_lk, def.uid) == LK_OK;
            refresh_account();
            return ok;
        }
        if (quitting()) return 0;
    }
    UiItem items[8];
    memset(items, 0, sizeof items);
        items_init(items, (int)(sizeof items / sizeof items[0]));
    for (size_t i = 0; i < n; i++) {
        items[i].label = acc[i].name;
        items[i].toggle = -1;
    }
    UiList l;
    memset(&l, 0, sizeof l);
    l.title = "Which account is shared?";
    l.subtitle = "The account whose saves get swapped.";
    l.items = items;
    l.n = (int)n;
    static const Prompt pr[] = {{"A", "Choose"}};
    l.prompts = pr;
    l.nprompts = 1;
    for (;;) {
        int r = ui_list(&l, NULL);
        if (quitting()) return 0;
        if (r >= 0) {
            char msg[240];
            snprintf(msg, sizeof msg, "Swap the saves of \xE2\x80\x9C%s\xE2\x80\x9D?", acc[r].name);
            if (ui_confirm("Use this account?", msg, "Use it", "Back", 0, NULL, 0)) {
                int ok = lk_set_target(g_lk, acc[r].uid) == LK_OK;
                refresh_account();
                return ok;
            }
        }
    }
}

static int first_run(void)
{
    if (!g_st.state.intro_done) {
        if (!ui_confirm("Back up every save with JKSV first",
                        "A second copy never hurts. Shown once.",
                        "I\xE2\x80\x99ve backed up", "Exit", 0, NULL, 0))
            return 0;
        g_st.state.intro_done = 1;
        store_save_state(&g_st);
    }
    while (!g_st.cfg.target_uid[0]) {
        if (quitting() || !choose_account(1)) return 0;
    }
    while (!store_admin_has_pin(&g_st)) {
        ui_message(UI_INFO, "Set an admin password",
                   "It protects the admin pages. Six digits.");
        char pin[8];
        if (new_password(NULL, 0, "Choose a 6-digit admin password", pin)) store_admin_set_pin(&g_st, pin);
        if (quitting()) return 0;
    }
    return 1;
}

// ------------------------------------------------------------------------------------------------ entry

void app_fatal(Platform *plat, const char *headline, const char *detail)
{
    ui_init(plat, THEME_AUTO, NULL, NULL);
    ui_message(UI_ERROR, headline, detail);
    ui_shutdown();
}

int app_run(Platform *plat, Backend *be, const char *root)
{
    g_be = be;
    memset(&g_lock, 0, sizeof g_lock);
    ui_init(plat, THEME_AUTO, status_cb, NULL);
    ui_set_icon_source(icon_cb, NULL);
    ui_intro_prepare(); // (the intro's wall is made on the other cores while the SD card is read; the console's main.c has begun it already)

    app_adopt_old_names(root);
    if (store_open(&g_st, be, root) != 0) {
        ui_message(UI_ERROR, "Couldn\xE2\x80\x99t open the SD card folder",
                   "Is the SD card full or locked?");
        ui_shutdown();
        return 1;
    }
    ui_set_theme_mode(g_st.cfg.theme_mode);
    ui_set_clock24(g_st.cfg.clock24);
    {
        char icons[FSX_PATH];
        store_path(&g_st, icons, sizeof icons, "pictures");
        ui_set_icon_dir(icons); // (the Extra icons: Admin, This app, Extra icons downloads them here)
    }
    store_logf(&g_st, "profile pictures: %s", ui_avatar_status());
    end_reminder_test();
    g_lk = calloc(1, sizeof *g_lk);
    if (!g_lk || lk_init(g_lk, be, &g_st) != 0) {
        ui_message(UI_ERROR, "Out of memory", "Close other software.");
        ui_shutdown();
        return 1;
    }
    g_lk->progress = on_progress;
    refresh_account();
    lk_startup(g_lk);
    recent_take();

    if (g_st.notice[0]) ui_message(UI_WARN, "A settings file was damaged", g_st.notice);

    if (first_run()) {
        recovery_flow(1);
        if (!quitting()) home_loop();
    }

    recent_forget();
    lk_free(g_lk);
    free(g_lk);
    g_lk = NULL;
    ui_shutdown();
    return 0;
}
