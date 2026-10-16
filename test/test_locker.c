// Host tests: the real state machine (locker.c + store.c) against folders. Build and run with `make -f Makefile.host`.
#define _GNU_SOURCE
#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../source/custom_pics.h"
#include "../source/cJSON.h"
#include "../source/fsx.h"
#include "../source/hash.h"
#include "../source/locker.h"
#include "../source/nacp_name.h"
#include "../source/sysprofile.h"
#include "../source/store.h"
#include "../source/swatches.h"
#include "../source/gfx/tint.h"
#include "backend_host.h"

// ------------------------------------------------------------------------------------------------ mini test kit

static int g_checks = 0, g_failed = 0;
static const char *g_test = "";

#define CHECK(cond)                                                                       \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (!(cond)) {                                                                    \
            g_failed++;                                                                   \
            printf("  FAIL [%s] %s:%d: %s\n", g_test, __FILE__, __LINE__, #cond);          \
        }                                                                                 \
    } while (0)

#define CHECK_MSG(cond, ...)                                                              \
    do {                                                                                  \
        g_checks++;                                                                       \
        if (!(cond)) {                                                                    \
            g_failed++;                                                                   \
            printf("  FAIL [%s] %s:%d: %s -- ", g_test, __FILE__, __LINE__, #cond);        \
            printf(__VA_ARGS__);                                                          \
            printf("\n");                                                                 \
        }                                                                                 \
    } while (0)

// ------------------------------------------------------------------------------------------------ the world

enum {
    A = 0x0100000000010000ull, // on the account and in Alex's backup
    B = 0x0100000000020000ull, // only in Alex's backup, installed          -> created
    C = 0x0100000000030000ull, // only in Alex's backup, NOT installed      -> skipped with a warning
    D = 0x0100000000040000ull, // only on the account                        -> emptied
    E = 0x0100000000050000ull, // excluded: never swapped
    F_ = 0x0100000000060000ull, // only in Maya's old backup, not installed  -> carried over
};

typedef struct {
    FakeBe fb;
    Store st;
    Locker lk;
    char maya[ID_MAX], alex[ID_MAX];
} World;

static void tpath(const World *w, char *out, size_t n, const char *rel)
{
    fsx_join(out, n, w->st.root, rel);
}

static void put_profile_file(World *w, const char *id, uint64_t title, const char *rel, const char *text)
{
    char tdir[17], p[FSX_PATH], f[FSX_PATH], parent[FSX_PATH];
    store_title_dir(title, tdir);
    snprintf(p, sizeof p, "profiles/%s/current/%s/%s", id, tdir, rel);
    tpath(w, f, sizeof f, p);
    snprintf(parent, sizeof parent, "%s", f);
    *strrchr(parent, '/') = 0;
    fsx_mkdir_p(parent);
    fsx_write_file(f, text, strlen(text));
}

static void world_open(World *w)
{
    store_open(&w->st, &w->fb.be, w->fb.sd);
    lk_init(&w->lk, &w->fb.be, &w->st);
}

// "Restart the app": throw away in-memory state and read everything back from the SD card.
static void world_restart(World *w)
{
    lk_free(&w->lk);
    fake_reboot(&w->fb);
    world_open(w);
}

static void world_new(World *w, int build_saves)
{
    memset(w, 0, sizeof *w);
    fake_init(&w->fb);
    world_open(w);
    snprintf(w->st.cfg.target_uid, sizeof w->st.cfg.target_uid, "%032x", 1);
    store_save_config(&w->st);
    w->lk.be->set_target(w->lk.be, w->st.cfg.target_uid);

    Profile p;
    CHECK(store_profile_create(&w->st, "Maya", NULL, &p) == 0);
    snprintf(w->maya, sizeof w->maya, "%s", p.id);
    CHECK(store_profile_create(&w->st, "Alex", NULL, &p) == 0);
    snprintf(w->alex, sizeof w->alex, "%s", p.id);

    fake_add_title(&w->fb, A, 1, 0x100000);
    fake_add_title(&w->fb, B, 1, 0x100000);
    fake_add_title(&w->fb, C, 0, 0x100000);
    fake_add_title(&w->fb, D, 1, 0x100000);
    fake_add_title(&w->fb, E, 1, 0x100000);
    fake_add_title(&w->fb, F_, 0, 0x100000);

    w->st.cfg.excluded[w->st.cfg.nexcluded++] = E;
    store_save_config(&w->st);

    if (!build_saves) return;
    // the account currently holds Maya's latest progress
    fake_put(&w->fb, A, "save.dat", "A: maya progress v2");
    fake_put(&w->fb, A, "sub/x.bin", "A: maya sub v2");
    fake_put(&w->fb, D, "d.dat", "D: maya's d");
    fake_put(&w->fb, E, "e.dat", "E: shared club save");
    // Maya's stored backup is older
    put_profile_file(w, w->maya, A, "save.dat", "A: maya progress v1");
    put_profile_file(w, w->maya, F_, "f.dat", "F: maya's f");
    // Alex's backup
    put_profile_file(w, w->alex, A, "save.dat", "A: alex progress");
    put_profile_file(w, w->alex, A, "dir/a.bin", "A: alex deep file");
    put_profile_file(w, w->alex, B, "b.dat", "B: alex");
    put_profile_file(w, w->alex, C, "c.dat", "C: alex");
    put_profile_file(w, w->alex, E, "e.dat", "E: alex's old copy");
    snprintf(w->st.state.holder, sizeof w->st.state.holder, "%s", w->maya);
    store_save_state(&w->st);
}

static void world_free(World *w)
{
    lk_free(&w->lk);
    fake_destroy(&w->fb);
}

// ------------------------------------------------------------------------------------------------ inspection helpers

static void sav(World *w, uint64_t title, char *out, size_t n)
{
    fake_save_path(&w->fb, title, out, n);
}

static int sav_exists(World *w, uint64_t title)
{
    char p[FSX_PATH];
    sav(w, title, p, sizeof p);
    return fsx_is_dir(p);
}

// the account's save for a game exists but has nothing in it (what "cleared" and "never played" look like)
static int save_is_empty(World *w, uint64_t title)
{
    char p[FSX_PATH], **names;
    size_t n = 0;
    sav(w, title, p, sizeof p);
    if (fsx_list(p, &names, &n) != 0) return 1;
    fsx_list_free(names, n);
    return n == 0;
}

static char *read_in(const char *dir, const char *rel)
{
    char p[FSX_PATH], *data = NULL;
    fsx_join(p, sizeof p, dir, rel);
    if (fsx_read_all(p, &data, NULL) != 0) return NULL;
    return data;
}

static int sav_is(World *w, uint64_t title, const char *rel, const char *text)
{
    char d[FSX_PATH];
    sav(w, title, d, sizeof d);
    char *got = read_in(d, rel);
    int ok = got && !strcmp(got, text);
    free(got);
    return ok;
}

static int bak_is(World *w, const char *id, uint64_t title, const char *rel, const char *text)
{
    char tdir[17], p[FSX_PATH], d[FSX_PATH];
    store_title_dir(title, tdir);
    snprintf(p, sizeof p, "profiles/%s/current/%s", id, tdir);
    tpath(w, d, sizeof d, p);
    char *got = read_in(d, rel);
    int ok = got && !strcmp(got, text);
    free(got);
    return ok;
}

static int bak_has_title(World *w, const char *id, uint64_t title)
{
    char tdir[17], p[FSX_PATH], d[FSX_PATH];
    store_title_dir(title, tdir);
    snprintf(p, sizeof p, "profiles/%s/current/%s", id, tdir);
    tpath(w, d, sizeof d, p);
    return fsx_is_dir(d);
}

static size_t count_children(const char *dir)
{
    char **names;
    size_t n = 0;
    if (fsx_list(dir, &names, &n) != 0) return 0;
    fsx_list_free(names, n);
    return n;
}

static size_t history_count(World *w, const char *id)
{
    char rel[FSX_PATH], d[FSX_PATH];
    snprintf(rel, sizeof rel, "profiles/%s/history", id);
    tpath(w, d, sizeof d, rel);
    return count_children(d);
}

static size_t unclaimed_count(World *w)
{
    char d[FSX_PATH];
    tpath(w, d, sizeof d, "unclaimed");
    return count_children(d);
}

// ---- a text "snapshot" of everything that matters, for comparing two worlds / before-and-after
typedef struct {
    char *buf;
    size_t len, cap;
} Str;

static void str_add(Str *s, const char *fmt, ...)
{
    char line[1200];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    if (s->len + (size_t)n + 1 > s->cap) {
        s->cap = (s->len + (size_t)n + 1) * 2;
        s->buf = realloc(s->buf, s->cap);
    }
    memcpy(s->buf + s->len, line, (size_t)n + 1);
    s->len += (size_t)n;
}

static void snap_tree(Str *s, const char *label, const char *root, const char *rel, int skip_scratch)
{
    char path[FSX_PATH];
    if (rel[0]) fsx_join(path, sizeof path, root, rel);
    else snprintf(path, sizeof path, "%s", root);
    char **names;
    size_t n;
    if (fsx_list(path, &names, &n) != 0) return;
    for (size_t i = 0; i < n; i++) {
        char childrel[FSX_PATH], childpath[FSX_PATH];
        if (skip_scratch && (!strncmp(names[i], "staging-", 8) || !strncmp(names[i], "state.json", 10) ||
                             !strcmp(names[i], "logs")))
            continue;
        if (rel[0]) snprintf(childrel, sizeof childrel, "%s/%s", rel, names[i]);
        else snprintf(childrel, sizeof childrel, "%s", names[i]);
        fsx_join(childpath, sizeof childpath, root, childrel);
        if (fsx_is_dir(childpath)) {
            str_add(s, "%s D %s\n", label, childrel);
            snap_tree(s, label, root, childrel, skip_scratch);
        } else {
            uint32_t crc = 0;
            uint64_t size = 0;
            fsx_file_crc(childpath, &crc, &size);
            str_add(s, "%s F %s %" PRIu64 " %08x\n", label, childrel, size, crc);
        }
    }
    fsx_list_free(names, n);
}

// everything the user cares about: the account's saves, and each profile's current/ (not history, staging, logs)
static char *snapshot_logical(World *w)
{
    Str s = {0};
    str_add(&s, "");
    snap_tree(&s, "SAVE", w->fb.committed, "", 0);
    Profile *list;
    size_t n;
    store_profile_list(&w->st, &list, &n);
    for (size_t i = 0; i < n; i++) {
        char rel[FSX_PATH];
        snprintf(rel, sizeof rel, "profiles/%s/current", list[i].id);
        char lab[80];
        snprintf(lab, sizeof lab, "BAK(%s)", list[i].name);
        char cur[FSX_PATH];
        tpath(w, cur, sizeof cur, rel);
        snap_tree(&s, lab, cur, "", 0);
    }
    free(list);
    str_add(&s, "HOLDER %s\n", w->st.state.holder);
    return s.buf;
}

// literally everything on disk except scratch/log files (to prove a refused swap changed nothing)
static char *snapshot_all(World *w)
{
    Str s = {0};
    str_add(&s, "");
    snap_tree(&s, "SAVE", w->fb.committed, "", 0);
    snap_tree(&s, "SD", w->st.root, "", 1);
    return s.buf;
}

// ---- "no data lost" tracking: every file's (size, crc) that existed at the start must still exist somewhere safe
typedef struct {
    uint64_t size;
    uint32_t crc;
    char where[FSX_PATH];
} Sig;

typedef struct {
    Sig *v;
    size_t n, cap;
} SigSet;

static void sig_add(SigSet *s, uint64_t size, uint32_t crc, const char *where)
{
    if (s->n == s->cap) {
        s->cap = s->cap ? s->cap * 2 : 64;
        s->v = realloc(s->v, s->cap * sizeof *s->v);
    }
    s->v[s->n].size = size;
    s->v[s->n].crc = crc;
    snprintf(s->v[s->n].where, sizeof s->v[s->n].where, "%s", where);
    s->n++;
}

static int sig_has(const SigSet *s, uint64_t size, uint32_t crc)
{
    for (size_t i = 0; i < s->n; i++)
        if (s->v[i].size == size && s->v[i].crc == crc) return 1;
    return 0;
}

static void collect_sigs(SigSet *out, const char *dir, int skip_staging)
{
    char **names;
    size_t n;
    if (fsx_list(dir, &names, &n) != 0) return;
    for (size_t i = 0; i < n; i++) {
        if (skip_staging && !strncmp(names[i], "staging-", 8)) continue;
        if (!strncmp(names[i], "state.json", 10) || !strncmp(names[i], "log.", 4) || !strcmp(names[i], "config.json") ||
            !strcmp(names[i], "profile.json"))
            continue;
        char p[FSX_PATH];
        fsx_join(p, sizeof p, dir, names[i]);
        if (fsx_is_dir(p)) {
            collect_sigs(out, p, skip_staging);
        } else {
            uint32_t crc;
            uint64_t size;
            if (fsx_file_crc(p, &crc, &size) == 0) sig_add(out, size, crc, p);
        }
    }
    fsx_list_free(names, n);
}

// valuables = every file in every account save and every profile's current/ right now
static int g_ro_run; // a read-only holder is left: the console's saves are thrown away on purpose, so they are not valuables

static void collect_valuables(World *w, SigSet *out)
{
    if (!g_ro_run) collect_sigs(out, w->fb.committed, 0);
    char dir[FSX_PATH];
    tpath(w, dir, sizeof dir, "profiles");
    char **names;
    size_t n;
    if (fsx_list(dir, &names, &n) != 0) return;
    for (size_t i = 0; i < n; i++) {
        if (names[i][0] == '.') continue;
        char cur[FSX_PATH], rel[FSX_PATH];
        snprintf(rel, sizeof rel, "profiles/%s/current", names[i]);
        tpath(w, cur, sizeof cur, rel);
        collect_sigs(out, cur, 0);
    }
    fsx_list_free(names, n);
}

// where a file is "safe": account saves, any profile folder (current/history), unclaimed/, .trash -- never staging
static void collect_safe(World *w, SigSet *out)
{
    collect_sigs(out, w->fb.committed, 1);
    char d[FSX_PATH];
    tpath(w, d, sizeof d, "profiles");
    collect_sigs(out, d, 1);
    tpath(w, d, sizeof d, "unclaimed");
    collect_sigs(out, d, 1);
}

static int nothing_lost(World *w, const SigSet *valuables, char *why, size_t whyn)
{
    SigSet safe = {0};
    collect_safe(w, &safe);
    int ok = 1;
    for (size_t i = 0; i < valuables->n; i++) {
        if (!sig_has(&safe, valuables->v[i].size, valuables->v[i].crc)) {
            snprintf(why, whyn, "lost: %s", valuables->v[i].where);
            ok = 0;
            break;
        }
    }
    free(safe.v);
    return ok;
}

// ------------------------------------------------------------------------------------------------ tests

static void test_hashes(void)
{
    g_test = "hashes";
    char h[65];
    sha256_hex("abc", 3, h);
    CHECK(!strcmp(h, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    sha256_hex("", 0, h);
    CHECK(!strcmp(h, "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    const char *two = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";
    sha256_hex(two, strlen(two), h);
    CHECK(!strcmp(h, "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"));
    CHECK(crc32_update(0, "123456789", 9) == 0xCBF43926u);
    uint32_t c = crc32_update(0, "1234", 4);
    c = crc32_update(c, "56789", 5);
    CHECK(c == 0xCBF43926u);
}

static void test_store_profiles_and_pins(void)
{
    g_test = "store";
    World w;
    world_new(&w, 0);

    Profile a1, a2, z;
    CHECK(store_profile_create(&w.st, "Alex", NULL, &a1) == 0);
    CHECK(store_profile_create(&w.st, "alex", "123456", &a2) == 0);
    CHECK(strcmp(a1.id, a2.id) != 0); // two Alexes coexist
    CHECK(store_profile_create(&w.st, "Zed Z.", NULL, &z) == 0);
    CHECK(!strncmp(z.id, "zed-z-", 6));
    CHECK(store_profile_create(&w.st, "   ", NULL, &z) != 0);
    CHECK(store_profile_create(&w.st, "Bad Pin", "12345", &z) != 0);   // 5 digits
    CHECK(store_profile_create(&w.st, "Bad Pin", "1234567", &z) != 0); // 7 digits
    CHECK(store_profile_create(&w.st, "Bad Pin", "12a456", &z) != 0);

    Profile *list;
    size_t n;
    CHECK(store_profile_list(&w.st, &list, &n) == 0);
    CHECK(n == 5); // Maya, Alex (world), Alex, alex, Zed Z.
    for (size_t i = 1; i < n; i++) CHECK(strcasecmp(list[i - 1].name, list[i].name) <= 0);
    free(list);

    // passwords: exactly 6 digits, salted
    Profile l2;
    CHECK(store_profile_load(&w.st, a2.id, &l2) == 0);
    CHECK(store_profile_has_pin(&l2));
    CHECK(store_profile_check_pin(&l2, "123456"));
    CHECK(!store_profile_check_pin(&l2, "123457"));
    CHECK(!store_profile_check_pin(&l2, "1234"));
    CHECK(!store_profile_check_pin(&l2, ""));
    CHECK(strstr(l2.pin_hash, "123456") == NULL);
    CHECK(store_profile_check_pin(&a1, NULL)); // no password: always passes
    CHECK(store_profile_set_pin(&w.st, &l2, "654321") == 0);
    CHECK(store_profile_load(&w.st, a2.id, &l2) == 0);
    CHECK(store_profile_check_pin(&l2, "654321") && !store_profile_check_pin(&l2, "123456"));
    CHECK(store_profile_set_pin(&w.st, &l2, "1234") != 0);
    CHECK(store_profile_set_pin(&w.st, &l2, NULL) == 0);
    CHECK(store_profile_load(&w.st, a2.id, &l2) == 0 && !store_profile_has_pin(&l2));

    CHECK(!store_admin_has_pin(&w.st));
    CHECK(store_admin_set_pin(&w.st, "000000") == 0);
    CHECK(store_admin_has_pin(&w.st) && store_admin_check_pin(&w.st, "000000") && !store_admin_check_pin(&w.st, "000001"));
    CHECK(store_admin_set_pin(&w.st, "12345") != 0);
    world_restart(&w); // persisted
    CHECK(store_admin_check_pin(&w.st, "000000") && !store_admin_check_pin(&w.st, "999999"));

    // delete = move to .trash, never rm
    char dir[FSX_PATH], trash[FSX_PATH];
    store_profile_dir(&w.st, a1.id, dir, sizeof dir);
    CHECK(fsx_is_dir(dir));
    CHECK(lk_delete_profile(&w.lk, a1.id) == LK_OK);
    CHECK(!fsx_exists(dir));
    tpath(&w, trash, sizeof trash, "profiles/.trash");
    CHECK(count_children(trash) == 1);
    CHECK(lk_delete_profile(&w.lk, w.maya) == LK_OK);
    CHECK(count_children(trash) == 2);
    CHECK(store_profile_list(&w.st, &list, &n) == 0 && n == 3);
    free(list);

    world_free(&w);
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

// An Online icon is kept by name: the profile file has the name and a -1 for the number, a restart (every number forgotten, the others handed out in another order)
// finds the icon again, a console picture keeps its number and no name, and a damaged name is ignored.
static void test_store_online_icon(void)
{
    g_test = "store-online-icon";
    World w;
    world_new(&w, 0);
    custom_reset();
    Profile p, l;
    CHECK(store_profile_create(&w.st, "Eli", NULL, &p) == 0);
    snprintf(p.picfile, sizeof p.picfile, "%s", "game-b/B02");
    p.picture = custom_id(p.picfile);
    CHECK(custom_is(p.picture) && store_profile_save(&w.st, &p) == 0);
    char path[FSX_PATH], *txt = NULL;
    size_t n = 0;
    store_profile_dir(&w.st, p.id, path, sizeof path);
    strncat(path, "/profile.json", sizeof path - strlen(path) - 1);
    CHECK(fsx_read_all(path, &txt, &n) == 0 && json_picfile(txt, "game-b/B02", -1));
    free(txt);
    custom_reset(); // a new start of the app, where another icon got the first number
    CHECK(custom_id("zz/first") == CUSTOM_PIC_BASE);
    CHECK_MSG(store_profile_load(&w.st, p.id, &l) == 0 && !strcmp(l.picfile, "game-b/B02") && custom_is(l.picture) && l.picture != CUSTOM_PIC_BASE && !strcmp(custom_key(l.picture), "game-b/B02"),
          "it finds its icon again (%d)", l.picture);
    // the console's own picture: its number, no name
    p.picture = 12;
    p.picfile[0] = 0;
    CHECK(store_profile_save(&w.st, &p) == 0 && store_profile_load(&w.st, p.id, &l) == 0 && l.picture == 12 && l.picfile[0] == 0);
    CHECK_MSG(fsx_read_all(path, &txt, &n) == 0 && txt && !strstr(txt, "picfile"), "and the file has no name");
    free(txt);
    // a name that is not a key (someone edited the file): ignored, a letter
    snprintf(p.picfile, sizeof p.picfile, "%s", "../../x");
    p.picture = -1;
    CHECK_MSG(store_profile_save(&w.st, &p) == 0 && store_profile_load(&w.st, p.id, &l) == 0 && l.picfile[0] == 0 && l.picture == -1, "a damaged name is a letter (%d)", l.picture);
    custom_reset();
    world_free(&w);
}

// The activity log is one file for every start, in the logs folder, named by the start's date and time; starting deletes the ones older than two weeks (by the
// date in the name, else by when the file was written), and moves the old single log.txt / log.old in.
// everything the logs of every start say (a test restarts the world, which starts a new log)
static char *all_logs(const World *w)
{
    char dir[FSX_PATH], **names = NULL;
    size_t n = 0, len = 0;
    char *out = calloc(1, 1);
    tpath(w, dir, sizeof dir, "logs");
    if (fsx_list(dir, &names, &n) == 0) {
        for (size_t i = 0; i < n; i++) {
            char p[FSX_PATH], *text = NULL;
            size_t tl = 0;
            if (fsx_join(p, sizeof p, dir, names[i]) != 0 || fsx_read_all(p, &text, &tl) != 0) continue;
            out = realloc(out, len + tl + 1);
            memcpy(out + len, text, tl);
            len += tl;
            out[len] = 0;
            free(text);
        }
        fsx_list_free(names, n);
    }
    return out;
}

static void put_log(const World *w, const char *name, const char *text)
{
    char dir[FSX_PATH], p[FSX_PATH];
    tpath(w, dir, sizeof dir, "logs");
    fsx_mkdir_p(dir);
    fsx_join(p, sizeof p, dir, name);
    fsx_write_file(p, text, strlen(text));
}

static int log_exists(const World *w, const char *name)
{
    char rel[FSX_PATH], p[FSX_PATH];
    snprintf(rel, sizeof rel, "logs/%s", name);
    tpath(w, p, sizeof p, rel);
    return fsx_exists(p);
}

static void log_named(time_t t, const char *tail, char *out, size_t n)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &tm);
    snprintf(out, n, "%s%s.txt", stamp, tail);
}

static void test_logs(void)
{
    g_test = "logs";
    World w;
    world_new(&w, 0);
    time_t now = time(NULL);
    w.fb.clock = now;
    w.fb.clock_step = 0; // (time stands still)
    char d21[64], d15[64], d13[64], d1[64], keep_note[64];
    log_named(now - 21 * 86400, "", d21, sizeof d21);
    log_named(now - 15 * 86400, "", d15, sizeof d15);
    log_named(now - 13 * 86400, "", d13, sizeof d13);
    log_named(now - 86400, "", d1, sizeof d1);
    snprintf(keep_note, sizeof keep_note, "notes.txt"); // (no date in its name: it was written just now)
    put_log(&w, d21, "old\n");
    put_log(&w, d15, "old\n");
    put_log(&w, d13, "recent\n");
    put_log(&w, d1, "yesterday\n");
    put_log(&w, keep_note, "mine\n");
    char p[FSX_PATH];
    tpath(&w, p, sizeof p, "log.txt");
    fsx_write_file(p, "one log before the folder\n", 26);
    tpath(&w, p, sizeof p, "log.old");
    fsx_write_file(p, "and the one before\n", 19);
    store_logs_start(&w.st);
    CHECK_MSG(!log_exists(&w, d21) && !log_exists(&w, d15), "logs older than two weeks are deleted");
    CHECK_MSG(log_exists(&w, d13) && log_exists(&w, d1) && log_exists(&w, keep_note), "newer ones, and a file written just now, stay");
    tpath(&w, p, sizeof p, "log.txt");
    char p2[FSX_PATH];
    tpath(&w, p2, sizeof p2, "log.old");
    CHECK_MSG(!fsx_exists(p) && !fsx_exists(p2), "the old single logs are gone from the folder's top");
    char earlier[64], older[64];
    log_named(now, "-earlier", earlier, sizeof earlier);
    log_named(now, "-older", older, sizeof older);
    CHECK_MSG(log_exists(&w, earlier) && log_exists(&w, older), "and are in the logs folder, under the time they were written");
    // this start's file
    char session[FSX_PATH], *text = NULL, want[64], rel[FSX_PATH];
    store_session_log(&w.st, session, sizeof session);
    log_named(now, "", want, sizeof want);
    snprintf(rel, sizeof rel, "logs/%s", want);
    tpath(&w, p, sizeof p, rel);
    CHECK_MSG(!strcmp(session, p), "this start's log is named by the start: %s", session);
    store_logf(&w.st, "hello %d", 7);
    CHECK_MSG(fsx_read_all(session, &text, NULL) == 0 && text && strstr(text, "hello 7"), "a line goes in this start's file");
    free(text);
    char **names = NULL;
    size_t n = 0;
    CHECK(store_logs_list(&w.st, &names, &n) == 0);
    int at_session = -1, at_d1 = -1, at_d13 = -1;
    for (size_t i = 0; i < n; i++) {
        if (!strcmp(names[i], want)) at_session = (int)i;
        if (!strcmp(names[i], d1)) at_d1 = (int)i;
        if (!strcmp(names[i], d13)) at_d13 = (int)i;
    }
    CHECK_MSG(n == 6 && at_session >= 0 && at_d1 > at_session && at_d13 > at_d1, "the list has all six, the newest first (this start %d, yesterday %d, 13 days ago %d of %zu)", at_session, at_d1, at_d13, n);
    fsx_list_free(names, n);
    // a second start a moment later has a file of its own
    w.fb.clock = now + 5;
    Store again;
    CHECK(store_open(&again, &w.fb.be, w.fb.sd) == 0);
    char second[FSX_PATH];
    store_session_log(&again, second, sizeof second);
    CHECK_MSG(strcmp(second, session) != 0, "the next start writes to a new file");
    // a backup of everything leaves the logs and the downloaded icons out
    world_free(&w);
}

static void test_state_files(void)
{
    g_test = "state-files";
    World w;
    world_new(&w, 0);
    snprintf(w.st.state.holder, sizeof w.st.state.holder, "someone-1234");
    w.st.state.phase = PH_RESTORING;
    snprintf(w.st.state.from, sizeof w.st.state.from, "a-0001");
    snprintf(w.st.state.to, sizeof w.st.state.to, "b-0002");
    CHECK(store_save_state(&w.st) == 0);
    world_restart(&w);
    CHECK(w.st.state.phase == PH_RESTORING && !strcmp(w.st.state.to, "b-0002") && !strcmp(w.st.state.holder, "someone-1234"));

    // crash window inside the atomic replace: state.json gone, state.json.tmp complete
    char p[FSX_PATH], t[FSX_PATH];
    tpath(&w, p, sizeof p, "state.json");
    CHECK(snprintf(t, sizeof t, "%s.tmp", p) < (int)sizeof t);
    CHECK(rename(p, t) == 0);
    world_restart(&w);
    CHECK(w.st.state.phase == PH_RESTORING && !strcmp(w.st.state.to, "b-0002"));

    // a damaged state file is set aside, defaults take over
    fsx_write_file(t, "{ not json", 10);
    world_restart(&w);
    CHECK(!strcmp(w.st.state.holder, UNCLAIMED) && w.st.state.phase == PH_IDLE && w.st.notice[0]);
    world_free(&w);
}

static void check_swap_result(World *w, const char *label)
{
    char why[256];
    (void)why;
    CHECK_MSG(sav_is(w, A, "save.dat", "A: alex progress"), "%s: A save.dat", label);
    CHECK_MSG(sav_is(w, A, "dir/a.bin", "A: alex deep file"), "%s: A dir/a.bin", label);
    CHECK_MSG(sav_exists(w, B) && sav_is(w, B, "b.dat", "B: alex"), "%s: B created", label);
    CHECK_MSG(!sav_exists(w, C), "%s: C (not installed) not created", label);
    CHECK_MSG(sav_exists(w, D), "%s: D still registered", label);
    {
        char d[FSX_PATH];
        sav(w, D, d, sizeof d);
        CHECK_MSG(count_children(d) == 0, "%s: D emptied", label);
    }
    CHECK_MSG(sav_is(w, E, "e.dat", "E: shared club save"), "%s: excluded E untouched", label);
    {
        char a[FSX_PATH];
        sav(w, A, a, sizeof a);
        CHECK_MSG(!sav_exists(w, A) || count_children(a) == 2, "%s: A has exactly alex's two entries", label);
    }

    // Maya's profile now holds what was on the account, plus her old F
    CHECK_MSG(bak_is(w, w->maya, A, "save.dat", "A: maya progress v2"), "%s: maya A", label);
    CHECK_MSG(bak_is(w, w->maya, A, "sub/x.bin", "A: maya sub v2"), "%s: maya A sub", label);
    CHECK_MSG(bak_is(w, w->maya, D, "d.dat", "D: maya's d"), "%s: maya D", label);
    CHECK_MSG(bak_is(w, w->maya, F_, "f.dat", "F: maya's f"), "%s: maya F carried over", label);
    CHECK_MSG(!bak_has_title(w, w->maya, E), "%s: excluded E never backed up", label);
    CHECK_MSG(history_count(w, w->maya) == 1, "%s: one history entry for maya (got %zu)", label, history_count(w, w->maya));
    {
        char hist[FSX_PATH], **names;
        size_t n;
        snprintf(hist, sizeof hist, "profiles/%s/history", w->maya);
        tpath(w, hist, sizeof hist, hist);
        if (fsx_list(hist, &names, &n) == 0 && n == 1) {
            char h1[FSX_PATH];
            fsx_join(h1, sizeof h1, hist, names[0]);
            char tdir[17], p[FSX_PATH];
            store_title_dir(A, tdir);
            fsx_join(p, sizeof p, h1, tdir);
            char *got = read_in(p, "save.dat");
            CHECK_MSG(got && !strcmp(got, "A: maya progress v1"), "%s: history holds maya's previous backup", label);
            free(got);
            fsx_list_free(names, n);
        }
    }
    // Alex's own backup is only read
    CHECK_MSG(bak_is(w, w->alex, A, "save.dat", "A: alex progress") && bak_is(w, w->alex, C, "c.dat", "C: alex"),
              "%s: alex backup untouched", label);
    CHECK_MSG(!strcmp(w->st.state.holder, w->alex) && w->st.state.phase == PH_IDLE, "%s: holder is alex, idle", label);
}

static void test_swap_basic(void)
{
    g_test = "swap-basic";
    World w;
    world_new(&w, 1);
    int rc = lk_swap_to(&w.lk, w.alex);
    CHECK_MSG(rc == LK_OK, "rc=%d err=%s", rc, w.lk.err);
    check_swap_result(&w, "clean");
    CHECK(w.lk.nwarn == 1); // C isn't installed
    CHECK(strstr(w.lk.warn[0], "isn't installed") != NULL);
    CHECK(w.lk.swap_titles == 2); // A and D (E excluded)

    // already loaded
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_NOOP);
    CHECK(lk_swap_to(&w.lk, "nobody-0000") == LK_ERR_NO_PROFILE);
    world_free(&w);
}

// the progress bar's plan: in every phase the locker reports exactly the games its plan lists, in that order, each with the plan's size
typedef struct {
    int n, bad;
    int phase, k, games[4];
    unsigned serial;
    uint64_t title;
    Locker *lk;
} PlanSeen;

static void plan_cb(void *user, const char *what, uint64_t cur, uint64_t total)
{
    (void)what;
    (void)cur;
    PlanSeen *s = user;
    Locker *lk = s->lk;
    s->n++;
    if (lk->phase != s->phase || lk->plan_serial != s->serial) {
        if (s->phase >= 1 && s->phase <= 3) s->games[s->phase] = s->k;
        s->phase = lk->phase;
        s->serial = lk->plan_serial;
        s->k = 0;
        s->title = 0;
    }
    if (lk->cur_title != s->title) {
        s->title = lk->cur_title;
        if (lk->plan_n == 0 || (size_t)s->k >= lk->plan_n || (lk->plan[s->k] != total && total != 0)) s->bad++; // (total 0: a game that was skipped)
        s->k++;
    }
}

static void test_progress_plan(void)
{
    g_test = "progress-plan";
    World w;
    world_new(&w, 1);
    PlanSeen seen = {0}, *sp = &seen;
    seen.lk = &w.lk;
    w.lk.progress = plan_cb;
    w.lk.progress_user = sp;
    int rc = lk_swap_to(&w.lk, w.alex);
    CHECK_MSG(rc == LK_OK, "rc=%d err=%s", rc, w.lk.err);
    CHECK_MSG(seen.n > 0, "the swap reported progress");
    CHECK_MSG(seen.bad == 0, "%d games were reported with a size or place the plan did not have", seen.bad);
    CHECK_MSG(w.lk.plan_n == (size_t)seen.k, "the last phase reported every game of its plan (%zu planned, %d reported)", w.lk.plan_n, seen.k);
    w.lk.progress = NULL;
    world_free(&w);
}

static void test_history_keeps_five(void)
{
    g_test = "history-5";
    World w;
    world_new(&w, 1);
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    for (int i = 0; i < 9; i++) {
        char text[64];
        snprintf(text, sizeof text, "A: alex round %d", i);
        fake_put(&w.fb, A, "save.dat", text); // alex plays
        CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
        snprintf(text, sizeof text, "A: maya round %d", i);
        fake_put(&w.fb, A, "save.dat", text); // maya plays
        CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    }
    CHECK_MSG(history_count(&w, w.alex) == 5, "alex history=%zu", history_count(&w, w.alex));
    CHECK_MSG(history_count(&w, w.maya) == 5, "maya history=%zu", history_count(&w, w.maya));
    // the five kept are the newest ones
    char **names;
    size_t n;
    char hist[FSX_PATH], rel[FSX_PATH];
    snprintf(rel, sizeof rel, "profiles/%s/history", w.alex);
    tpath(&w, hist, sizeof hist, rel);
    CHECK(lk_history_list(&w.lk, w.alex, &names, &n) == 0 && n == 5);
    char tdir[17];
    store_title_dir(A, tdir);
    char oldest[FSX_PATH], p[FSX_PATH];
    CHECK(snprintf(oldest, sizeof oldest, "%s/%s/%s", hist, names[0], tdir) < (int)sizeof oldest);
    char *got = read_in(oldest, "save.dat");
    // alex's backups in history, oldest first: rounds 3..7 (round 8 is in current)
    CHECK_MSG(got && !strcmp(got, "A: alex round 3"), "oldest kept = %s", got ? got : "(null)");
    free(got);
    fsx_list_free(names, n);

    // history_keep is configurable
    w.st.cfg.history_keep = 2;
    fake_put(&w.fb, A, "save.dat", "A: alex again");
    CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
    CHECK_MSG(history_count(&w, w.alex) == 2, "alex history=%zu", history_count(&w, w.alex));
    (void)p;
    world_free(&w);
}

// If X plays game A but never B, and Y plays B but never A, each profile holds only its own games, and the console only ever
// shows the loaded person's.
static void test_games_stay_with_their_player(void)
{
    g_test = "per-game";
    World w;
    world_new(&w, 0);
    char bdir[FSX_PATH];
    fake_put(&w.fb, A, "tmp", "x"); // both games already have a save on the console (an earlier player's, or one a game
    fake_put(&w.fb, B, "tmp", "x"); // made when it was launched); what matters is what each person actually plays
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    CHECK(save_is_empty(&w, A) && save_is_empty(&w, B)); // the first load leaves a clean slate for a new profile
    CHECK(fake_put(&w.fb, A, "x.dat", "A: X's progress") == 0); // X plays A, never B

    CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
    CHECK(bak_has_title(&w, w.alex, A) && !bak_has_title(&w, w.alex, B));
    CHECK(save_is_empty(&w, A)); // X's game is not on Y's console
    CHECK(fake_put(&w.fb, B, "y.dat", "B: Y's progress") == 0); // Y plays B, never A

    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    CHECK(bak_has_title(&w, w.maya, B) && !bak_has_title(&w, w.maya, A));
    CHECK(bak_is(&w, w.alex, A, "x.dat", "A: X's progress"));
    CHECK(sav_is(&w, A, "x.dat", "A: X's progress"));
    CHECK(save_is_empty(&w, B)); // Y's game is not on X's console

    int ng = -1;
    CHECK(lk_profile_summary(&w.lk, w.alex, &ng, NULL) == 0 && ng == 1);
    CHECK(lk_profile_summary(&w.lk, w.maya, &ng, NULL) == 0 && ng == 1);

    // backups made by older versions kept an empty folder for every game on the console: those don't count as games
    char rel[FSX_PATH], tdir[17];
    store_title_dir(B, tdir);
    snprintf(rel, sizeof rel, "profiles/%s/current/%s", w.alex, tdir);
    tpath(&w, bdir, sizeof bdir, rel);
    CHECK(fsx_mkdir_p(bdir) == 0);
    CHECK(lk_profile_summary(&w.lk, w.alex, &ng, NULL) == 0 && ng == 1);
    CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
    CHECK(!bak_has_title(&w, w.alex, B)); // and the next backup tidies it away
    world_free(&w);
}

// The console is only ever cleared for someone whose backup is complete.
static void test_clearing_needs_a_complete_backup(void)
{
    g_test = "wipe-guard";
    World w;
    world_new(&w, 1);
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    // pretend the journal says "clearing" but the console holds progress the backup doesn't have
    CHECK(fake_put(&w.fb, A, "save.dat", "A: progress that was never backed up") == 0);
    w.st.state.phase = PH_WIPING;
    snprintf(w.st.state.from, sizeof w.st.state.from, "%s", w.alex);
    w.st.state.to[0] = 0;
    store_save_state(&w.st);
    world_restart(&w);
    CHECK(lk_needs_recovery(&w.lk));
    int rc = lk_recover(&w.lk);
    CHECK_MSG(rc == LK_ERR_VERIFY, "rc=%d", rc);
    CHECK(strstr(w.lk.err, "nothing was cleared") != NULL);
    CHECK(sav_is(&w, A, "save.dat", "A: progress that was never backed up"));
    world_free(&w);
}

static void test_refusals(void)
{
    g_test = "refusals";
    World w;
    world_new(&w, 1);
    char *before = snapshot_all(&w);

    w.fb.battery_pct = 10;
    w.fb.charging = 0;
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_ERR_BATTERY);
    CHECK(strstr(w.lk.err, "10%") != NULL);
    w.fb.battery_pct = 14;
    CHECK(lk_sign_out(&w.lk) == LK_ERR_BATTERY);
    char *after = snapshot_all(&w);
    CHECK(!strcmp(before, after));
    free(after);
    w.fb.charging = 1; // charging is fine
    w.fb.battery_pct = 5;

    // not enough space
    w.fb.free_bytes = 10;
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_ERR_SPACE);
    after = snapshot_all(&w);
    CHECK(!strcmp(before, after));
    free(after);
    w.fb.free_bytes = 1ull << 40;

    // a game is running: its save can't be opened; refuse BEFORE touching anything
    fake_title(&w.fb, D)->running = 1;
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_ERR_OPEN);
    CHECK(strstr(w.lk.err, "Close the game") != NULL);
    after = snapshot_all(&w);
    CHECK_MSG(!strcmp(before, after), "a refused swap changed something");
    free(after);
    CHECK(w.st.state.phase == PH_IDLE);
    CHECK(lk_sign_out(&w.lk) == LK_ERR_OPEN);
    after = snapshot_all(&w);
    CHECK(!strcmp(before, after));
    free(after);
    fake_title(&w.fb, D)->running = 0;

    // and then it works
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    free(before);
    world_free(&w);
}

static void test_verify(void)
{
    g_test = "verify";
    World w;
    world_new(&w, 1);
    char a[FSX_PATH], b[FSX_PATH];
    tpath(&w, a, sizeof a, "va");
    tpath(&w, b, sizeof b, "vb");
    fsx_mkdir_p(a);
    fsx_mkdir_p(b);
    char f1[FSX_PATH], f2[FSX_PATH];
    fsx_join(f1, sizeof f1, a, "x.bin");
    fsx_join(f2, sizeof f2, b, "x.bin");
    fsx_write_file(f1, "hello world, this is a save", 27);
    fsx_write_file(f2, "hello world, this is a save", 27);
    CHECK(fsx_tree_equal(a, b) == 1);
    fsx_write_file(f2, "hello world, this is a sav", 26); // truncated
    CHECK(fsx_tree_equal(a, b) == 0);
    fsx_write_file(f2, "hello world, this is a savX", 27); // same size, flipped byte
    CHECK(fsx_tree_equal(a, b) == 0);
    fsx_write_file(f2, "hello world, this is a save", 27);
    char sub[FSX_PATH];
    fsx_join(sub, sizeof sub, b, "extra");
    fsx_mkdir_p(sub);
    CHECK(fsx_tree_equal(a, b) == 0); // an extra folder
    world_free(&w);
}

static void test_silent_write_corruption_is_caught(void)
{
    g_test = "bad-write";
    World w;
    world_new(&w, 1);
    // the console "accepts" a write but stores half of it: the post-restore verification must notice
    w.fb.corrupt_next_write = 1;
    int rc = lk_swap_to(&w.lk, w.alex);
    CHECK_MSG(rc == LK_ERR_VERIFY, "rc=%d err=%s", rc, w.lk.err);
    CHECK(w.st.state.phase == PH_RESTORING);
    // the account's old contents had already been backed up and verified before this
    CHECK(bak_is(&w, w.maya, A, "save.dat", "A: maya progress v2"));
    // app restarted: recovery finishes the job
    world_restart(&w);
    CHECK(lk_needs_recovery(&w.lk));
    CHECK(lk_recover(&w.lk) == LK_OK);
    check_swap_result(&w, "after-bad-write-recovery");
    world_free(&w);
}

static void test_journal_size_commits(void)
{
    g_test = "journal";
    World w;
    world_new(&w, 0);
    // a 1 MiB file into a save whose journal only takes 256 KiB between commits
    fake_title(&w.fb, A)->journal = 256 * 1024;
    CHECK(fake_put(&w.fb, A, "old.dat", "old") == 0);
    snprintf(w.st.state.holder, sizeof w.st.state.holder, "%s", UNCLAIMED);
    store_save_state(&w.st);
    put_profile_file(&w, w.alex, A, "tiny.dat", "tiny");
    char big[FSX_PATH];
    {
        char tdir[17], rel[FSX_PATH];
        store_title_dir(A, tdir);
        snprintf(rel, sizeof rel, "profiles/%s/current/%s/big.bin", w.alex, tdir);
        tpath(&w, big, sizeof big, rel);
        char *buf = malloc(1024 * 1024);
        for (size_t i = 0; i < 1024 * 1024; i++) buf[i] = (char)(i * 31 + (i >> 8));
        fsx_write_file(big, buf, 1024 * 1024);
        free(buf);
    }
    // the fake itself enforces the limit (otherwise this test proves nothing)
    {
        char p[FSX_PATH];
        sav(&w, A, p, sizeof p);
        CHECK(w.fb.be.mount(&w.fb.be, A) == BE_OK);
        char f[FSX_PATH];
        fsx_join(f, sizeof f, w.fb.be.save_root, "probe.bin");
        FILE *fp = fopen(f, "wb");
        char chunk[64 * 1024] = {0};
        int err = 0;
        for (int i = 0; i < 8 && !err; i++) err = w.fb.be.write(&w.fb.be, fp, chunk, sizeof chunk);
        CHECK(err == BE_ERR_JOURNAL);
        fclose(fp);
        w.fb.be.unmount(&w.fb.be);
    }
    int rc = lk_swap_to(&w.lk, w.alex);
    CHECK_MSG(rc == LK_OK, "rc=%d err=%s", rc, w.lk.err);
    CHECK(sav_is(&w, A, "tiny.dat", "tiny"));
    char d[FSX_PATH], dst[FSX_PATH];
    sav(&w, A, d, sizeof d);
    fsx_join(dst, sizeof dst, d, "big.bin");
    uint32_t c1, c2;
    uint64_t s1, s2;
    CHECK(fsx_file_crc(big, &c1, &s1) == 0 && fsx_file_crc(dst, &c2, &s2) == 0 && c1 == c2 && s1 == s2 && s1 == 1024 * 1024);
    world_free(&w);
}

static void test_rollback_and_delete(void)
{
    g_test = "rollback";
    World w;
    world_new(&w, 1);
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK); // maya's v1 is now in history, v2 is current
    char **names;
    size_t n;
    CHECK(lk_history_list(&w.lk, w.maya, &names, &n) == 0 && n == 1);
    char hname[64];
    snprintf(hname, sizeof hname, "%s", names[0]);
    fsx_list_free(names, n);

    // the holder can't be rolled back or deleted
    CHECK(lk_rollback(&w.lk, w.alex, hname) == LK_ERR_IS_HOLDER);
    CHECK(lk_delete_profile(&w.lk, w.alex) == LK_ERR_IS_HOLDER);

    CHECK(lk_rollback(&w.lk, w.maya, hname) == LK_OK);
    CHECK(bak_is(&w, w.maya, A, "save.dat", "A: maya progress v1"));
    CHECK_MSG(history_count(&w, w.maya) == 2, "v2 kept in history too (%zu)", history_count(&w, w.maya));
    CHECK(lk_rollback(&w.lk, w.maya, "../evil") == LK_ERR_IO);
    CHECK(lk_rollback(&w.lk, w.maya, "19990101-000000") == LK_ERR_IO);

    // loading maya now gives her the older save
    CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
    CHECK(sav_is(&w, A, "save.dat", "A: maya progress v1"));
    world_free(&w);
}

static void test_default_account_and_target(void)
{
    g_test = "accounts";
    World w;
    world_new(&w, 0);
    BeAccount out;
    snprintf(w.fb.accounts[0].uid, 33, "%032x", 1);
    snprintf(w.fb.accounts[0].name, 40, "Someone");
    snprintf(w.fb.accounts[1].uid, 33, "%032x", 2);
    snprintf(w.fb.accounts[1].name, 40, "esports switch");
    w.fb.naccounts = 2;
    CHECK(lk_default_account(&w.lk, &out) == 1 && !strcmp(out.uid, w.fb.accounts[1].uid));
    snprintf(w.fb.accounts[0].name, 40, "ESPORTS SWITCH");
    CHECK(lk_default_account(&w.lk, &out) == 0); // two candidates: ask instead of guessing
    w.fb.naccounts = 1;
    snprintf(w.fb.accounts[0].name, 40, "Mine");
    CHECK(lk_default_account(&w.lk, &out) == 0);

    CHECK(lk_set_target(&w.lk, w.fb.accounts[0].uid) == LK_OK);
    world_restart(&w);
    CHECK(!strcmp(w.st.cfg.target_uid, w.fb.accounts[0].uid));
    snprintf(w.st.state.holder, sizeof w.st.state.holder, "%s", w.maya);
    CHECK(lk_set_target(&w.lk, w.fb.accounts[1].uid) == LK_ERR_IS_HOLDER);

    // before an account is chosen nothing can be swapped
    World w2;
    memset(&w2, 0, sizeof w2);
    fake_init(&w2.fb);
    world_open(&w2);
    Profile p;
    store_profile_create(&w2.st, "X", NULL, &p);
    CHECK(lk_swap_to(&w2.lk, p.id) == LK_ERR_NO_TARGET);
    world_free(&w2);
    world_free(&w);
}

static void test_nacp_names(void)
{
    g_test = "nacp-names";
    static uint8_t nacp[NACP_SIZE];
    char out[96];
    // the old layout: 16 entries of name[0x200] + author[0x100]; here only Japanese has a name
    memset(nacp, 0, sizeof nacp);
    memcpy(nacp + 2 * 0x300, "Old Layout Game", 15);
    CHECK(nacp_name(nacp, out, sizeof out) == 0 && !strcmp(out, "Old Layout Game"));
    memcpy(nacp + 0 * 0x300, "English Name", 12);
    CHECK_MSG(nacp_name(nacp, out, sizeof out) == 0 && !strcmp(out, "English Name"), "English is preferred (%s)", out);
    memset(nacp, 0, sizeof nacp);
    CHECK(nacp_name(nacp, out, sizeof out) == -1);

    // the layout of games made for firmware 21 and later: the names are DEFLATE-compressed
    static const uint8_t blob[91] = {
        0xed, 0xd2, 0x31, 0x11, 0x80, 0x30, 0x14, 0x44, 0x41, 0xa4, 0xe0, 0x85, 0x02, 0x01, 0x28, 0x60, 0x86, 0x5f, 0x86, 0x30, 0xc4, 0x01,
        0x31, 0x82, 0x06, 0x6a, 0xdc, 0xc4, 0x08, 0x26, 0xe8, 0xb2, 0x2b, 0xe1, 0xee, 0x0d, 0x03, 0xf4, 0x6b, 0xca, 0xe9, 0x38, 0xa3, 0x94,
        0xd8, 0xc6, 0x79, 0x4d, 0x61, 0x90, 0xce, 0x2c, 0x39, 0x45, 0xde, 0xfb, 0xfd, 0xbd, 0x5d, 0x4f, 0xab, 0x6f, 0xab, 0xb7, 0x14, 0xf4,
        0x0f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0xfc, 0xe2, 0x03};
    nacp[0] = 91;
    memcpy(nacp + 2, blob, sizeof blob);
    nacp[0x3215] = 1;
    CHECK(nacp_names_compressed(nacp));
    CHECK_MSG(nacp_name(nacp, out, sizeof out) == 0 && !strcmp(out, "Compressed Game"), "compressed name (%s)", out);
    // treating those bytes as text (what an older toolkit does) would have given rubbish; damaged data must never give a bogus name
    for (int flip = 2; flip < 60; flip += 7) {
        uint8_t keep = nacp[flip];
        nacp[flip] ^= 0x5A;
        int rc = nacp_name(nacp, out, sizeof out);
        int clean = 1;
        for (const unsigned char *p = (const unsigned char *)out; *p; p++)
            if (*p < 0x20) clean = 0;
        CHECK_MSG(rc != 0 || clean, "damaged data at byte %d gave a name with control characters", flip);
        nacp[flip] = keep;
    }
    nacp[0] = 0; // no size
    CHECK(nacp_name(nacp, out, sizeof out) == -1);
}

static void test_system_profile(void)
{
    g_test = "system-profile";
    World w;
    world_new(&w, 1);
    char nick[40], err[200];
    static const uint8_t pic1[] = {0xFF, 0xD8, 'o', 'n', 'e', 0xFF, 0xD9}, pic2[] = {0xFF, 0xD8, 't', 'w', 'o', 'o', 0xFF, 0xD9};

    // reading is always safe
    CHECK(sp_probe(&w.fb.be, nick, NULL, NULL, err, sizeof err) == SP_OK && !strcmp(nick, "Esports Switch"));
    // a console that refuses: nothing is written and no "original" is claimed
    w.fb.sys_allowed = 0;
    CHECK(sp_probe(&w.fb.be, nick, NULL, NULL, err, sizeof err) == SP_ERR && strstr(err, "didn't allow"));
    CHECK(sp_apply(&w.st, &w.fb.be, "Maya", pic1, sizeof pic1, NULL, err, sizeof err) == SP_ERR);
    CHECK(w.fb.sys_stores == 0 && !sp_has_original(&w.st));
    w.fb.sys_allowed = 1;

    // restoring before anything was changed is a no-op
    CHECK(sp_restore(&w.st, &w.fb.be, err, sizeof err) == SP_NOTHING);

    // the first write saves the original (name and picture) and then changes both
    CHECK_MSG(sp_apply(&w.st, &w.fb.be, "Maya", pic1, sizeof pic1, NULL, err, sizeof err) == SP_OK, "%s", err);
    CHECK(sp_has_original(&w.st) && !strcmp(w.fb.sys_nick, "Maya") && w.fb.sys_img_size == sizeof pic1);
    // later writes don't replace the saved original; nick NULL keeps the name, jpeg NULL keeps the picture
    CHECK(sp_apply(&w.st, &w.fb.be, "Alex", NULL, 0, NULL, err, sizeof err) == SP_OK && !strcmp(w.fb.sys_nick, "Alex") && w.fb.sys_img_size == sizeof pic1);
    CHECK(sp_apply(&w.st, &w.fb.be, NULL, pic2, sizeof pic2, NULL, err, sizeof err) == SP_OK && !strcmp(w.fb.sys_nick, "Alex") &&
          w.fb.sys_img_size == sizeof pic2 && !memcmp(w.fb.sys_img, pic2, sizeof pic2));
    // and the original comes back exactly
    CHECK(sp_restore(&w.st, &w.fb.be, err, sizeof err) == SP_OK);
    CHECK(!strcmp(w.fb.sys_nick, "Esports Switch") && w.fb.sys_img_size == 24 && !memcmp(w.fb.sys_img, "\xFF\xD8" "the original picture", 22));

    // a failed write says so and leaves the console as it was
    w.fb.sys_fail_store = 2;
    int stores = w.fb.sys_stores;
    CHECK(sp_apply(&w.st, &w.fb.be, "Maya", pic1, sizeof pic1, NULL, err, sizeof err) == SP_ERR && strstr(err, "didn't accept"));
    CHECK(w.fb.sys_stores == stores && !strcmp(w.fb.sys_nick, "Esports Switch"));
    w.fb.sys_fail_store = 0;

    // the icon id and background id the console keeps beside the picture: set with the picture, put back with the original
    {
        int icon = 0, bg = 0;
        CHECK(sp_probe(&w.fb.be, nick, &icon, &bg, err, sizeof err) == SP_OK && icon == 9 && bg == 5); // the fake console's original
        SpIcon ic = {42, 17};
        w.fb.sys_ud[0x10] = 0xAA; // a Mii id: it is cleared when a character icon is set
        CHECK(sp_apply(&w.st, &w.fb.be, "Maya", pic1, sizeof pic1, &ic, err, sizeof err) == SP_OK);
        CHECK_MSG(w.fb.sys_ud[SP_UD_ICON] == 42 && w.fb.sys_ud[SP_UD_BG] == 17 && w.fb.sys_ud[0x10] == 0, "icon %d, background %d", w.fb.sys_ud[SP_UD_ICON], w.fb.sys_ud[SP_UD_BG]);
        // other fields of the user data are left alone
        CHECK(w.fb.sys_ud[0x40] == 0 && w.fb.sys_ud[5] == 0);
        // an Extra icon (id 0) clears the character and any Mii, and keeps its background
        SpIcon none = {0, 23};
        w.fb.sys_ud[0x10] = 0xAA;
        CHECK(sp_apply(&w.st, &w.fb.be, "Maya", pic2, sizeof pic2, &none, err, sizeof err) == SP_OK);
        CHECK_MSG(w.fb.sys_ud[SP_UD_ICON] == 0 && w.fb.sys_ud[SP_UD_BG] == 23 && w.fb.sys_ud[0x10] == 0, "extra icon: icon %d, background %d", w.fb.sys_ud[SP_UD_ICON], w.fb.sys_ud[SP_UD_BG]);
        CHECK(sp_apply(&w.st, &w.fb.be, "Maya", pic1, sizeof pic1, &ic, err, sizeof err) == SP_OK);
        // a change without an icon leaves the ids as they are
        CHECK(sp_apply(&w.st, &w.fb.be, "Alex", NULL, 0, NULL, err, sizeof err) == SP_OK && w.fb.sys_ud[SP_UD_ICON] == 42 && w.fb.sys_ud[SP_UD_BG] == 17);
        CHECK(sp_restore(&w.st, &w.fb.be, err, sizeof err) == SP_OK);
        CHECK_MSG(w.fb.sys_ud[SP_UD_ICON] == 9 && w.fb.sys_ud[SP_UD_BG] == 5 && w.fb.sys_ud[0x10] == 0, "the original ids came back");
    }

    // names: at most 31 bytes, never half a character, no control characters
    char clean[40];
    sp_clean_nick("Maximilian-Alexander-The-Third-Of-Them-All", clean);
    CHECK_MSG(strlen(clean) == 31 && !strncmp(clean, "Maximilian-Alexander", 20), "long name -> '%s'", clean);
    sp_clean_nick("\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82\xE3\x81\x82",
                  clean); // eleven 3-byte characters
    CHECK_MSG(strlen(clean) == 30, "multi-byte name cut on a character boundary (%zu bytes)", strlen(clean));
    sp_clean_nick("Ma\x01ya\n", clean);
    CHECK(!strcmp(clean, "Maya"));
    world_free(&w);

    // if the original can't be saved first, nothing is written: here the console's picture is too big to keep
    world_new(&w, 1);
    w.fb.sys_img_size = SP_IMG_MAX + 1;
    CHECK(sp_apply(&w.st, &w.fb.be, "Maya", pic1, sizeof pic1, NULL, err, sizeof err) == SP_ERR && strstr(err, "nothing was changed"));
    CHECK(w.fb.sys_stores == 0 && !sp_has_original(&w.st));
    // but a name-only change doesn't need the picture, and restoring then puts back just the name
    CHECK(sp_apply(&w.st, &w.fb.be, "Maya", NULL, 0, NULL, err, sizeof err) == SP_OK && !strcmp(w.fb.sys_nick, "Maya"));
    CHECK(sp_restore(&w.st, &w.fb.be, err, sizeof err) == SP_OK && !strcmp(w.fb.sys_nick, "Esports Switch"));
    world_free(&w);
}

static void test_new_fields(void)
{
    g_test = "new-fields";
    World w;
    world_new(&w, 1);

    // avatar colours are handed out evenly: the least-used one, lowest first
    Profile p;
    int counts[6] = {0, 0, 0, 0, 0, 0};
    Profile *list;
    size_t n;
    CHECK(store_profile_list(&w.st, &list, &n) == 0);
    for (size_t i = 0; i < n; i++)
        for (int k = 0; k < 6; k++)
            if (list[i].color == SWATCH_VIVID[k]) counts[k]++;
    free(list);
    for (int i = 0; i < 12; i++) {
        char name[16];
        snprintf(name, sizeof name, "P%d", i);
        CHECK(store_profile_create(&w.st, name, NULL, &p) == 0);
        int known = 0;
        for (int k = 0; k < 6; k++)
            if (p.color == SWATCH_VIVID[k]) {
                counts[k]++;
                known = 1;
            }
        CHECK_MSG(known, "a new profile gets one of the lively swatches (%d)", p.color);
    }
    int lo = counts[0], hi = counts[0];
    for (int i = 1; i < 6; i++) {
        if (counts[i] < lo) lo = counts[i];
        if (counts[i] > hi) hi = counts[i];
    }
    CHECK_MSG(hi - lo <= 1, "colours are evenly spread (min %d, max %d)", lo, hi);
    Profile again;
    CHECK(store_profile_load(&w.st, p.id, &again) == 0 && again.color == p.color && again.picture == -1); // and it persists
    // NetIDs: three letters and four digits, kept lower-case, and they persist
    CHECK(store_valid_netid("abc1234") && store_valid_netid("XyZ0042") && !store_valid_netid("ab1234") && !store_valid_netid("abcd123") &&
          !store_valid_netid("abc123") && !store_valid_netid("abc12345") && !store_valid_netid("abc 234") && !store_valid_netid(""));
    char nid[12];
    store_netid_clean("  AbC1234 ", nid);
    CHECK_MSG(!strcmp(nid, "abc1234"), "a NetID is trimmed and lower-cased (%s)", nid);
    store_netid_clean("nope", nid);
    CHECK(nid[0] == 0);
    snprintf(again.netid, sizeof again.netid, "xyz9876");
    CHECK(store_profile_save(&w.st, &again) == 0);
    {   // a NetID belongs to one profile
        Profile who;
        CHECK_MSG(store_netid_owner(&w.st, "XYZ9876", NULL, &who) && !strcmp(who.id, again.id), "a NetID is found whatever its case");
        CHECK_MSG(!store_netid_owner(&w.st, "xyz9876", again.id, NULL), "a profile doesn't clash with itself");
        CHECK_MSG(!store_netid_owner(&w.st, "abc0000", NULL, NULL) && !store_netid_owner(&w.st, "nonsense", NULL, NULL), "a free NetID, or no NetID, clashes with nobody");
    }
    CHECK(w.st.cfg.sort_mode == SORT_RECENT && w.st.cfg.remind_minutes == 180);
    w.st.cfg.sort_mode = SORT_NAME;
    w.st.cfg.remind_minutes = 6 * 60 + 15;
    CHECK(store_save_config(&w.st) == 0);
    {
        Store again_st;
        CHECK(store_open(&again_st, &w.fb.be, w.fb.sd) == 0 && again_st.cfg.sort_mode == SORT_NAME && again_st.cfg.remind_minutes == 6 * 60 + 15);
    }
    {   // a config written before the reminder had minutes says whole hours
        char cp[FSX_PATH], *txt = NULL;
        tpath(&w, cp, sizeof cp, "config.json");
        CHECK(fsx_read_all(cp, &txt, NULL) == 0 && txt);
        char *k = txt ? strstr(txt, "\"remind_minutes\"") : NULL;
        CHECK(k != NULL);
        if (k) {
            char *e = k + strlen("\"remind_minutes\""), *old = malloc(strlen(txt) + 32);
            if (!old) abort();
            while (*e == ':' || *e == ' ' || *e == '\t' || (*e >= '0' && *e <= '9')) e++;
            sprintf(old, "%.*s\"remind_hours\": 5%s", (int)(k - txt), txt, e);
            CHECK(fsx_write_file(cp, old, strlen(old)) == 0);
            Store old_st;
            CHECK_MSG(store_open(&old_st, &w.fb.be, w.fb.sd) == 0 && old_st.cfg.remind_minutes == 300 && old_st.cfg.sort_mode == SORT_NAME,
                      "an older config's 5 hours are 300 minutes (%d)", old_st.cfg.remind_minutes);
            CHECK(fsx_write_file(cp, txt, strlen(txt)) == 0);
            free(old);
        }
        free(txt);
    }
    {   // the usual reminder setting, kept while "the real one, in a minute" is being tested: none by default, and it is saved
        Store rs;
        CHECK_MSG(w.st.cfg.remind_restore == -1, "no reminder test is on to begin with (%d)", w.st.cfg.remind_restore);
        w.st.cfg.remind_restore = 180;
        CHECK(store_save_config(&w.st) == 0 && store_open(&rs, &w.fb.be, w.fb.sd) == 0 && rs.cfg.remind_restore == 180);
        w.st.cfg.remind_restore = 0; // (the reminder was off before the test)
        CHECK(store_save_config(&w.st) == 0 && store_open(&rs, &w.fb.be, w.fb.sd) == 0 && rs.cfg.remind_restore == 0);
        w.st.cfg.remind_restore = 999999;
        CHECK(store_save_config(&w.st) == 0 && store_open(&rs, &w.fb.be, w.fb.sd) == 0 && rs.cfg.remind_restore == -1);
        w.st.cfg.remind_restore = -1;
        CHECK(store_save_config(&w.st) == 0);
    }
    Profile nback;
    CHECK(store_profile_load(&w.st, p.id, &nback) == 0 && !strcmp(nback.netid, "xyz9876"));
    {   // random backgrounds: always the lighter half of a colour, and every colour comes up
        int seen_rows[12] = {0}, dark = 0;
        for (int i = 0; i < 400; i++) {
            int s = store_random_swatch(&w.st);
            if (s < 0 || s >= SWATCH_COUNT || s % SWATCH_COLS >= 3) dark++;
            else seen_rows[s / SWATCH_COLS] = 1;
        }
        int all = 1;
        for (int i = 0; i < 12; i++) all &= seen_rows[i];
        CHECK_MSG(dark == 0, "random backgrounds are never the darker half (%d were)", dark);
        CHECK_MSG(all, "and every colour can come up");
    }
    // picture and swatch persist; an older profile.json (with one of the original six colours) maps onto the console's swatches
    again.picture = 17;
    again.color = 63;
    CHECK(store_profile_save(&w.st, &again) == 0);
    Profile back;
    CHECK(store_profile_load(&w.st, p.id, &back) == 0 && back.picture == 17 && back.color == 63);
    {
        char dir[FSX_PATH], pj[FSX_PATH];
        store_profile_dir(&w.st, p.id, dir, sizeof dir);
        fsx_join(pj, sizeof pj, dir, "profile.json");
        const char *old = "{\"name\":\"Old\",\"pin_hash\":\"\",\"salt\":\"\",\"created\":1,\"last_used\":2,\"color\":4}";
        CHECK(fsx_write_file(pj, old, strlen(old)) == 0);
        Profile o;
        CHECK(store_profile_load(&w.st, p.id, &o) == 0 && o.color == SWATCH_VIVID[4] && o.picture == -1);
    }

    // "loaded since": set by a swap, cleared by a sign-out
    CHECK(w.st.state.loaded_at == 0);
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    CHECK_MSG(w.st.state.loaded_at > 0, "swap records when the saves were loaded");
    world_restart(&w);
    CHECK(w.st.state.loaded_at > 0); // and it survives a restart
    CHECK(lk_sign_out(&w.lk) == LK_OK);
    CHECK(w.st.state.loaded_at == 0);

    // theme and clock settings persist
    CHECK(w.st.cfg.theme_mode == 0 && w.st.cfg.clock24 == 0); // defaults: follow the console, 12-hour
    w.st.cfg.theme_mode = 2;
    w.st.cfg.clock24 = 1;
    CHECK(store_save_config(&w.st) == 0);
    world_restart(&w);
    CHECK(w.st.cfg.theme_mode == 2 && w.st.cfg.clock24 == 1);
    world_free(&w);
}

// a deleted profile: listed from profiles/.trash, and put back with everything it had
static void trash_path(World *w, char *out, size_t n, const char *leaf, const char *rel)
{
    char p[FSX_PATH];
    snprintf(p, sizeof p, "profiles/.trash/%s%s%s", leaf, rel[0] ? "/" : "", rel);
    tpath(w, out, n, p);
}

static const TrashEntry *trash_find(const TrashEntry *t, size_t n, const char *leaf)
{
    for (size_t i = 0; i < n; i++)
        if (!strcmp(t[i].leaf, leaf)) return &t[i];
    return NULL;
}

static void test_deleted_profiles(void)
{
    g_test = "deleted-profiles";
    World w;
    world_new(&w, 1); // Maya's saves are loaded
    TrashEntry *t = NULL;
    size_t n = 99;
    CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 0); // nothing was ever deleted: an empty list, not an error
    free(t);

    Profile zed, noor;
    CHECK(store_profile_create(&w.st, "Zed", "123456", &zed) == 0);
    snprintf(zed.netid, sizeof zed.netid, "zed1234");
    zed.picture = 7;
    zed.color = 30;
    CHECK(store_profile_save(&w.st, &zed) == 0);
    put_profile_file(&w, zed.id, A, "save.dat", "A: zed");
    put_profile_file(&w, zed.id, B, "b.dat", "B: zed");
    put_profile_file(&w, zed.id, E, "e.dat", "E: zed's copy of the shared game"); // shared: not one of "his games"
    {   // and an earlier save in his history
        char rel[FSX_PATH], f[FSX_PATH], tdir[17];
        store_title_dir(A, tdir);
        snprintf(rel, sizeof rel, "profiles/%s/history/20260101-120000/%s", zed.id, tdir);
        tpath(&w, f, sizeof f, rel);
        fsx_mkdir_p(f);
        strcat(f, "/save.dat");
        fsx_write_file(f, "A: zed earlier", 14);
    }
    CHECK(store_profile_create(&w.st, "Noor", NULL, &noor) == 0);

    CHECK(lk_delete_profile(&w.lk, noor.id) == LK_OK);
    w.fb.clock += 3600; // an hour later
    int64_t when = (int64_t)w.fb.clock;
    CHECK(lk_delete_profile(&w.lk, zed.id) == LK_OK);

    char zleaf[sizeof t[0].leaf] = "", nleaf[sizeof t[0].leaf] = "";
    CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 2);
    if (n == 2) {
        CHECK_MSG(!strcmp(t[0].name, "Zed") && !strcmp(t[0].id, zed.id) && t[0].restorable, "the latest deletion comes first (%s)", t[0].name);
        CHECK_MSG(t[0].games == 2 && t[0].picture == 7 && t[0].color == 30, "with its games and its look (%d games, picture %d, colour %d)",
                  t[0].games, t[0].picture, t[0].color);
        CHECK_MSG(t[0].deleted >= when && t[0].deleted <= when + 10, "and when it was deleted, read from the folder's name (%lld, deleted at %lld)",
                  (long long)t[0].deleted, (long long)when);
        CHECK(!strcmp(t[1].name, "Noor") && !strcmp(t[1].id, noor.id) && t[1].games == 0 && t[1].restorable && t[1].picture == -1);
        CHECK_MSG(t[0].deleted - t[1].deleted >= 3600 && t[0].deleted - t[1].deleted < 3700, "an hour apart (%lld s)", (long long)(t[0].deleted - t[1].deleted));
        snprintf(zleaf, sizeof zleaf, "%s", t[0].leaf);
        snprintf(nleaf, sizeof nleaf, "%s", t[1].leaf);
    }
    free(t);

    // put Zed back: everything he had comes with him, and the trash no longer lists him
    char rid[ID_MAX] = "";
    int cleared = -1;
    CHECK(store_profile_restore(&w.st, zleaf, rid, &cleared) == 0 && !strcmp(rid, zed.id) && cleared == 0);
    Profile back;
    CHECK(store_profile_load(&w.st, zed.id, &back) == 0 && !strcmp(back.name, "Zed") && !strcmp(back.netid, "zed1234") && back.picture == 7 && back.color == 30);
    CHECK(store_profile_check_pin(&back, "123456") && !store_profile_check_pin(&back, "000000"));
    CHECK(bak_is(&w, zed.id, A, "save.dat", "A: zed") && bak_is(&w, zed.id, B, "b.dat", "B: zed") && bak_is(&w, zed.id, E, "e.dat", "E: zed's copy of the shared game"));
    CHECK_MSG(history_count(&w, zed.id) == 1, "his history came back too (%zu)", history_count(&w, zed.id));
    int games = -1;
    CHECK_MSG(lk_profile_summary(&w.lk, zed.id, &games, NULL) == 0 && games == 2, "the trash counted his games the way his page does (%d)", games);
    CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 1 && !strcmp(t[0].name, "Noor"));
    free(t);
    CHECK_MSG(!strcmp(w.st.state.holder, w.maya) && w.st.state.phase == PH_IDLE, "restoring doesn't touch who is loaded (%s)", w.st.state.holder);
    world_restart(&w);
    CHECK(!strcmp(w.st.state.holder, w.maya) && w.st.state.phase == PH_IDLE);
    CHECK(store_profile_restore(&w.st, zleaf, NULL, NULL) == -1); // he is not in the trash any more
    {
        char *log = all_logs(&w);
        CHECK(log && strstr(log, "profile restored: Zed"));
        free(log);
    }

    // a profile with the same id is in the way: refused, and nothing changes
    CHECK(lk_delete_profile(&w.lk, zed.id) == LK_OK);
    CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 2 && !strcmp(t[0].id, zed.id));
    if (n == 2) snprintf(zleaf, sizeof zleaf, "%s", t[0].leaf);
    free(t);
    Profile imp;
    memset(&imp, 0, sizeof imp);
    snprintf(imp.id, sizeof imp.id, "%s", zed.id);
    snprintf(imp.name, sizeof imp.name, "Impostor");
    imp.picture = -1;
    CHECK(store_profile_save(&w.st, &imp) == 0);
    CHECK(store_profile_restore(&w.st, zleaf, rid, &cleared) == -2);
    CHECK(store_profile_load(&w.st, zed.id, &back) == 0 && !strcmp(back.name, "Impostor"));
    CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 2 && !strcmp(t[0].name, "Zed") && t[0].games == 2 && t[0].restorable);
    free(t);
    char dir[FSX_PATH];
    store_profile_dir(&w.st, zed.id, dir, sizeof dir);
    fsx_rmtree(dir);

    // his NetID was given to Alex in the meantime: Zed comes back without it, and Alex keeps it
    Profile alex, owner;
    CHECK(store_profile_load(&w.st, w.alex, &alex) == 0);
    snprintf(alex.netid, sizeof alex.netid, "zed1234");
    CHECK(store_profile_save(&w.st, &alex) == 0);
    CHECK(store_profile_restore(&w.st, zleaf, rid, &cleared) == 0 && cleared == 1 && !strcmp(rid, zed.id));
    CHECK(store_profile_load(&w.st, zed.id, &back) == 0 && !strcmp(back.name, "Zed") && back.netid[0] == 0);
    CHECK(store_profile_check_pin(&back, "123456") && back.picture == 7); // the rest of his profile is as it was
    CHECK(store_netid_owner(&w.st, "zed1234", NULL, &owner) && !strcmp(owner.id, w.alex));
    CHECK(bak_is(&w, zed.id, A, "save.dat", "A: zed"));

    // a deleted profile whose profile.json is damaged or gone is still listed, under its folder's name, but can't be put back
    CHECK(lk_delete_profile(&w.lk, zed.id) == LK_OK);
    CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 2 && !strcmp(t[0].id, zed.id));
    if (n == 2) snprintf(zleaf, sizeof zleaf, "%s", t[0].leaf);
    free(t);
    char pj[FSX_PATH], zdir[FSX_PATH];
    trash_path(&w, pj, sizeof pj, zleaf, "profile.json");
    trash_path(&w, zdir, sizeof zdir, zleaf, "");
    for (int pass = 0; pass < 2; pass++) {
        if (pass == 0) CHECK(fsx_write_file(pj, "{ not json", 10) == 0);
        else CHECK(remove(pj) == 0);
        CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 2);
        const TrashEntry *e = trash_find(t, n, zleaf);
        CHECK_MSG(e && !e->restorable && !strcmp(e->name, zleaf) && !strcmp(e->id, zed.id) && e->games == 2 && e->deleted > 0,
                  "a damaged one is listed by its folder's name and marked (pass %d)", pass);
        free(t);
        CHECK(store_profile_restore(&w.st, zleaf, rid, NULL) == -1);
        CHECK(fsx_is_dir(zdir) && !fsx_exists(dir));
    }

    // a folder with the "-02" that a second deletion in the same second gets is read the same way
    char from[FSX_PATH], to[FSX_PATH], leaf2[sizeof t[0].leaf + 4];
    snprintf(leaf2, sizeof leaf2, "%s-02", nleaf);
    trash_path(&w, from, sizeof from, nleaf, "");
    trash_path(&w, to, sizeof to, leaf2, "");
    CHECK(rename(from, to) == 0);
    CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 2);
    {
        const TrashEntry *e = trash_find(t, n, leaf2);
        CHECK(e && e->restorable && !strcmp(e->id, noor.id) && !strcmp(e->name, "Noor") && e->deleted > 0);
    }
    free(t);
    CHECK(store_profile_restore(&w.st, leaf2, rid, NULL) == 0 && !strcmp(rid, noor.id));
    CHECK(store_profile_load(&w.st, noor.id, &back) == 0 && !strcmp(back.name, "Noor"));

    // things that are not deleted profiles
    CHECK(store_profile_restore(&w.st, "../x", NULL, NULL) == -1 && store_profile_restore(&w.st, "", NULL, NULL) == -1 &&
          store_profile_restore(&w.st, ".hidden-20260101-120000", NULL, NULL) == -1 && store_profile_restore(&w.st, "no-timestamp", NULL, NULL) == -1 &&
          store_profile_restore(&w.st, "ghost-0000-20260101-120000", NULL, NULL) == -1 && store_profile_restore(&w.st, NULL, NULL, NULL) == -1);
    trash_path(&w, to, sizeof to, "stray", "");
    CHECK(fsx_mkdir_p(to) == 0);
    CHECK(store_trash_list(&w.st, &t, &n) == 0 && n == 2);
    {
        const TrashEntry *e = trash_find(t, n, "stray");
        CHECK(e && !e->restorable && e->id[0] == 0 && e->deleted == 0 && e == &t[n - 1]); // unknown date: last
    }
    free(t);
    CHECK(!strcmp(w.st.state.holder, w.maya));
    world_free(&w);
}

// a file of that many bytes somewhere on the SD card
static void put_file(World *w, const char *rel, size_t bytes)
{
    char f[FSX_PATH], parent[FSX_PATH];
    tpath(w, f, sizeof f, rel);
    snprintf(parent, sizeof parent, "%s", f);
    *strrchr(parent, '/') = 0;
    fsx_mkdir_p(parent);
    char *buf = malloc(bytes + 1);
    for (size_t i = 0; i < bytes; i++) buf[i] = (char)('a' + (i * 7 + strlen(rel)) % 26);
    fsx_write_file(f, buf, bytes);
    free(buf);
}

// takes every line that mentions `needle` out of a snapshot
static void strip_lines(char *text, const char *needle)
{
    char *out = text;
    for (char *p = text; *p;) {
        char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) + 1 : strlen(p);
        char keep = p[len];
        p[len] = 0;
        int drop = strstr(p, needle) != NULL;
        p[len] = keep;
        if (!drop) {
            memmove(out, p, len);
            out += len;
        }
        p += len;
    }
    *out = 0;
}

// The SD card's size, and deleting earlier saves beyond the newest few: only ever those, for every profile, and never mid-swap.
static void test_storage(void)
{
    g_test = "storage";
    World w;
    world_new(&w, 1);

    uint64_t b = 0;
    w.fb.total_bytes = 32ull << 30;
    w.fb.free_bytes = 5ull << 30;
    CHECK(w.fb.be.total_space(&w.fb.be, &b) == BE_OK && b == 32ull << 30);
    CHECK(w.fb.be.free_space(&w.fb.be, &b) == BE_OK && b == 5ull << 30);
    fake_arm(&w.fb, 1); // the card stops answering
    b = 7;
    CHECK_MSG(w.fb.be.total_space(&w.fb.be, &b) == BE_ERR && b == 7, "a failed reading is an error, and reports no size");
    fake_reboot(&w.fb);
    CHECK(w.fb.be.total_space(&w.fb.be, &b) == BE_OK && b == 32ull << 30);

    // Alex has eight earlier saves and Maya two; a deleted profile has six; and there are the other places old saves live
    char tdir[17], rel[FSX_PATH];
    store_title_dir(A, tdir);
    uint64_t oldest5 = 0;
    for (int i = 0; i < 8; i++) {
        snprintf(rel, sizeof rel, "profiles/%s/history/2026090%d-120000/%s/save.dat", w.alex, i + 1, tdir);
        put_file(&w, rel, (size_t)1000 * (i + 1));
        snprintf(rel, sizeof rel, "profiles/%s/history/2026090%d-120000/%s/sub/more.bin", w.alex, i + 1, tdir);
        put_file(&w, rel, (size_t)10 + i);
        if (i < 5) oldest5 += (uint64_t)1000 * (i + 1) + 10 + i;
    }
    for (int i = 0; i < 2; i++) {
        snprintf(rel, sizeof rel, "profiles/%s/history/2026090%d-090000/%s/save.dat", w.maya, i + 1, tdir);
        put_file(&w, rel, 500);
    }
    for (int i = 0; i < 6; i++) {
        snprintf(rel, sizeof rel, "profiles/.trash/gone-0000-20260801-000000/history/2026070%d-120000/%s/save.dat", i + 1, tdir);
        put_file(&w, rel, 300);
    }
    snprintf(rel, sizeof rel, "unclaimed/20260101-000000/%s/save.dat", tdir);
    put_file(&w, rel, 700);
    put_file(&w, "system-profile/original.jpg", 900);

    // counting first: the right number, and nothing is touched
    char *before = snapshot_all(&w), *after;
    uint64_t bytes = 1;
    int entries = -1;
    CHECK(lk_history_trim(&w.lk, 3, 1, &bytes, &entries) == LK_OK);
    CHECK_MSG(bytes == oldest5 && entries == 5, "counting says Alex's five oldest would go (%llu bytes, %d entries; wanted %llu)",
              (unsigned long long)bytes, entries, (unsigned long long)oldest5);
    after = snapshot_all(&w);
    CHECK_MSG(!strcmp(before, after), "counting deletes nothing");
    free(after);

    // refused while a swap is unfinished: nothing is touched (counting still works)
    w.st.state.phase = PH_RESTORING;
    CHECK(lk_history_trim(&w.lk, 3, 0, &bytes, &entries) == LK_ERR_BUSY_STATE && bytes == 0 && entries == 0 && w.lk.err[0]);
    after = snapshot_all(&w);
    CHECK_MSG(!strcmp(before, after), "a refused trim deletes nothing");
    free(after);
    CHECK(lk_history_trim(&w.lk, 3, 1, &bytes, &entries) == LK_OK && bytes == oldest5 && entries == 5);
    w.st.state.phase = PH_IDLE;

    // for real: exactly Alex's five oldest go, and nothing else on the card changes
    uint64_t freed = 0;
    CHECK(lk_history_trim(&w.lk, 3, 0, &freed, &entries) == LK_OK);
    CHECK_MSG(freed == oldest5 && entries == 5, "the real run frees what counting said (%llu bytes, %d entries)", (unsigned long long)freed, entries);
    for (int i = 0; i < 5; i++) {
        snprintf(rel, sizeof rel, "profiles/%s/history/2026090%d-120000", w.alex, i + 1);
        strip_lines(before, rel);
    }
    after = snapshot_all(&w);
    CHECK_MSG(!strcmp(before, after), "only those five are gone: current/, unclaimed/, system-profile/, the trash and everyone's newest three are as they were");
    free(after);
    CHECK(history_count(&w, w.alex) == 3 && history_count(&w, w.maya) == 2);
    char **names;
    size_t n;
    CHECK(lk_history_list(&w.lk, w.alex, &names, &n) == 0 && n == 3 && !strcmp(names[0], "20260906-120000") && !strcmp(names[2], "20260908-120000"));
    fsx_list_free(names, n);

    // again, and keeping more than there is: nothing to delete
    CHECK(lk_history_trim(&w.lk, 3, 0, &freed, &entries) == LK_OK && freed == 0 && entries == 0);
    CHECK(lk_history_trim(&w.lk, 10, 1, &bytes, &entries) == LK_OK && bytes == 0 && entries == 0);
    CHECK(lk_history_trim(&w.lk, 10, 0, &freed, &entries) == LK_OK && freed == 0 && history_count(&w, w.alex) == 3);
    after = snapshot_all(&w);
    CHECK(!strcmp(before, after));
    free(after);

    // never everything: asked to keep none, each profile's newest still stays
    CHECK(lk_history_trim(&w.lk, 0, 0, &freed, &entries) == LK_OK && entries == 3);
    CHECK(history_count(&w, w.alex) == 1 && history_count(&w, w.maya) == 1);
    CHECK(lk_history_list(&w.lk, w.alex, &names, &n) == 0 && n == 1 && !strcmp(names[0], "20260908-120000"));
    fsx_list_free(names, n);
    CHECK(bak_is(&w, w.alex, A, "save.dat", "A: alex progress") && bak_is(&w, w.maya, A, "save.dat", "A: maya progress v1"));
    {
        char *log = all_logs(&w);
        CHECK(log && strstr(log, "earlier saves trimmed"));
        free(log);
    }
    free(before);
    world_free(&w);
}

// Unclaimed saves (what the console held before anyone loaded with the app) can be given to a profile, or deleted.
static int newest_unclaimed(World *w, char *leaf, size_t n)
{
    LkUnclaimed *u = NULL;
    size_t cnt = 0;
    if (lk_unclaimed_list(&w->lk, &u, &cnt) != 0 || cnt == 0) {
        free(u);
        return 0;
    }
    snprintf(leaf, n, "%s", u[0].leaf);
    free(u);
    return 1;
}

static void test_unclaimed(void)
{
    g_test = "unclaimed";
    World w;
    world_new(&w, 1);
    LkUnclaimed *u = NULL;
    size_t n = 9;
    CHECK(lk_unclaimed_list(&w.lk, &u, &n) == 0 && n == 0);
    free(u);

    // the club's saves were on the console when Alex loaded: they are kept, unclaimed
    snprintf(w.st.state.holder, sizeof w.st.state.holder, "%s", UNCLAIMED);
    store_save_state(&w.st);
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    CHECK(lk_unclaimed_list(&w.lk, &u, &n) == 0 && n == 1);
    char leaf[64] = "";
    if (n == 1) {
        snprintf(leaf, sizeof leaf, "%s", u[0].leaf);
        CHECK_MSG(u[0].ngames == 2 && u[0].titles[0] == A && u[0].titles[1] == D, "its games are A and D, not the shared one (%d)", u[0].ngames);
        CHECK_MSG(u[0].bytes > 0 && u[0].when > 0, "with its size and when it was kept (%llu, %lld)", (unsigned long long)u[0].bytes, (long long)u[0].when);
    }
    free(u);

    // what can't be done
    CHECK(lk_unclaimed_give(&w.lk, leaf, w.alex) == LK_ERR_IS_HOLDER);
    CHECK(lk_unclaimed_give(&w.lk, leaf, "nobody") == LK_ERR_NO_PROFILE);
    CHECK(lk_unclaimed_give(&w.lk, "../profiles", w.maya) == LK_ERR_IO);
    CHECK(lk_unclaimed_give(&w.lk, "19990101-000000", w.maya) == LK_ERR_IO);
    CHECK(lk_unclaimed_delete(&w.lk, "../profiles") == LK_ERR_IO && lk_unclaimed_delete(&w.lk, "") == LK_ERR_IO);
    w.st.state.phase = PH_BACKING_UP;
    CHECK(lk_unclaimed_give(&w.lk, leaf, w.maya) == LK_ERR_BUSY_STATE && lk_unclaimed_delete(&w.lk, leaf) == LK_ERR_BUSY_STATE);
    w.st.state.phase = PH_IDLE;
    CHECK_MSG(unclaimed_count(&w) == 1 && bak_is(&w, w.maya, A, "save.dat", "A: maya progress v1"), "none of that changed anything");

    // given to Maya: they are her saves now, what she had is in her history, and her game that isn't in them stays
    size_t hist = history_count(&w, w.maya);
    CHECK_MSG(lk_unclaimed_give(&w.lk, leaf, w.maya) == LK_OK, "%s", w.lk.err);
    CHECK(bak_is(&w, w.maya, A, "save.dat", "A: maya progress v2") && bak_is(&w, w.maya, A, "sub/x.bin", "A: maya sub v2"));
    CHECK(bak_is(&w, w.maya, D, "d.dat", "D: maya's d"));
    CHECK_MSG(bak_is(&w, w.maya, F_, "f.dat", "F: maya's f"), "her game that wasn't among them is still hers");
    CHECK_MSG(history_count(&w, w.maya) == hist + 1, "what she had went to her history (%zu)", history_count(&w, w.maya));
    CHECK_MSG(unclaimed_count(&w) == 0, "and it is gone from unclaimed/ (%zu)", unclaimed_count(&w));
    CHECK(lk_unclaimed_give(&w.lk, leaf, w.maya) == LK_ERR_IO);
    // the older saves are still there to roll back to
    char **names;
    size_t nh;
    CHECK(lk_history_list(&w.lk, w.maya, &names, &nh) == 0 && nh == hist + 1);
    if (nh) CHECK(lk_rollback(&w.lk, w.maya, names[nh - 1]) == LK_OK && bak_is(&w, w.maya, A, "save.dat", "A: maya progress v1"));
    fsx_list_free(names, nh);
    // and they load like any backup
    CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK && sav_is(&w, A, "save.dat", "A: maya progress v1"));

    // two more, and something half done: the list has the two, the newest first; deleting one leaves the other
    const char *made[2] = {"20260101-000000", "20260301-120000"};
    for (int i = 0; i < 2; i++) {
        char tdir[17], rel[FSX_PATH], f[FSX_PATH];
        store_title_dir(A, tdir);
        snprintf(rel, sizeof rel, "unclaimed/%s/%s", made[i], tdir);
        tpath(&w, f, sizeof f, rel);
        fsx_mkdir_p(f);
        strcat(f, "/save.dat");
        fsx_write_file(f, made[i], strlen(made[i]));
    }
    char half[FSX_PATH];
    tpath(&w, half, sizeof half, "unclaimed/staging-20260401-000000");
    fsx_mkdir_p(half);
    CHECK(lk_unclaimed_list(&w.lk, &u, &n) == 0 && n == 2);
    if (n == 2) CHECK_MSG(!strcmp(u[0].leaf, made[1]) && !strcmp(u[1].leaf, made[0]) && u[0].ngames == 1, "the newest first, without the half-done one");
    free(u);
    CHECK(lk_unclaimed_delete(&w.lk, made[1]) == LK_OK);
    CHECK(newest_unclaimed(&w, leaf, sizeof leaf) && !strcmp(leaf, made[0]));
    CHECK_MSG(unclaimed_count(&w) == 1, "one left, and the half-done one was cleared away (%zu)", unclaimed_count(&w));
    {
        char *log = all_logs(&w);
        CHECK(log && strstr(log, "given to") && strstr(log, "deleted"));
        free(log);
    }
    world_free(&w);
}

// ------------------------------------------------------------------------------------------------ crash loops

typedef int (*Scenario)(World *w);

static int scen_swap(World *w) { return lk_swap_to(&w->lk, w->alex); }
static int scen_signout(World *w) { return lk_sign_out(&w->lk); }
static int scen_swap_from_unclaimed(World *w) { return lk_swap_to(&w->lk, w->alex); }

static void prep_unclaimed(World *w)
{
    snprintf(w->st.state.holder, sizeof w->st.state.holder, "%s", UNCLAIMED);
    store_save_state(&w->st);
}

// Alex played and signed out: the console is cleared and nobody is loaded
static void prep_cleared(World *w)
{
    lk_swap_to(&w->lk, w->alex);
    fake_put(&w->fb, A, "save.dat", "A: alex progress +1");
    lk_sign_out(&w->lk);
}

static void prep_signout_after_play(World *w)
{
    lk_swap_to(&w->lk, w->alex);
    fake_put(&w->fb, A, "save.dat", "A: alex progress +1");
    fake_put(&w->fb, D, "d.dat", "D: alex played d too");
}

static void prep_big_world_for_swap_from_alex(World *w)
{
    // Alex is the holder: swap to Maya (reverse direction, exercises the other profile's data)
    snprintf(w->st.state.holder, sizeof w->st.state.holder, "%s", w->alex);
    store_save_state(&w->st);
}

static int scen_swap_to_maya(World *w) { return lk_swap_to(&w->lk, w->maya); }

// Alex signed out; a guest played two games and made a profile, Zed, to keep them
static char g_zed[ID_MAX];

static void prep_guest_played(World *w)
{
    prep_cleared(w);
    fake_put(&w->fb, A, "save.dat", "A: guest progress");
    fake_put(&w->fb, D, "d.dat", "D: guest's d");
    Profile z;
    store_profile_create(&w->st, "Zed", NULL, &z);
    snprintf(g_zed, sizeof g_zed, "%s", z.id);
}

static int scen_claim(World *w) { return lk_claim_guest(&w->lk, g_zed); }

// the club's saves were kept as unclaimed when Alex first loaded; they are given to Maya
static void prep_unclaimed_kept(World *w)
{
    prep_unclaimed(w);
    lk_swap_to(&w->lk, w->alex);
}

static int scen_give(World *w)
{
    char leaf[64];
    if (!newest_unclaimed(w, leaf, sizeof leaf)) return LK_OK; // (given already, before the power went)
    return lk_unclaimed_give(&w->lk, leaf, w->maya);
}

static void set_ro(World *w, const char *id, int ro)
{
    Profile p;
    CHECK(store_profile_load(&w->st, id, &p) == 0);
    p.readonly = ro;
    CHECK(store_profile_save(&w->st, &p) == 0);
}

// what a profile's current/ holds, as text (sizes and checksums)
static char *profile_text(World *w, const char *id)
{
    Str s = {0};
    str_add(&s, "");
    char rel[FSX_PATH], cur[FSX_PATH];
    snprintf(rel, sizeof rel, "profiles/%s/current", id);
    tpath(w, cur, sizeof cur, rel);
    snap_tree(&s, "CUR", cur, "", 0);
    return s.buf;
}

// Alex is loaded, played in a game he has and one he hasn't saved before, and is then made read-only
static void prep_ro_holder_played(World *w)
{
    lk_swap_to(&w->lk, w->alex);
    fake_put(&w->fb, A, "save.dat", "A: played in a read-only session");
    fake_put(&w->fb, D, "d.dat", "D: new progress in a read-only session");
    set_ro(w, w->alex, 1);
}

static void crash_loop(const char *name, Scenario scen, void (*prep)(World *))
{
    g_test = name;
    // 1. the clean run: the reference result
    World clean;
    world_new(&clean, 1);
    if (prep) prep(&clean);
    world_restart(&clean);
    int rc = scen(&clean);
    CHECK_MSG(rc == LK_OK, "clean run failed: rc=%d %s", rc, clean.lk.err);
    char *want = snapshot_logical(&clean);
    size_t want_hist_maya = history_count(&clean, clean.maya), want_hist_alex = history_count(&clean, clean.alex);
    size_t want_unclaimed = unclaimed_count(&clean);
    long total_ops = clean.fb.ops; // not armed => 0; count by arming beyond the end below
    (void)total_ops;
    world_free(&clean);

    // 2. cut the power at every counted call, one run per call
    int crashes = 0, noops = 0;
    for (long n = 1; n < 5000; n++) {
        World w;
        world_new(&w, 1);
        if (prep) prep(&w);
        world_restart(&w);

        SigSet valuables = {0};
        collect_valuables(&w, &valuables);
        char *ro_before = g_ro_run ? profile_text(&w, w.alex) : NULL;
        size_t ro_hist = g_ro_run ? history_count(&w, w.alex) : 0;

        fake_arm(&w.fb, n);
        rc = scen(&w);
        int crashed = w.fb.crashed;
        if (!crashed) { // the scenario finished before the nth call existed: we've covered every step
            CHECK_MSG(rc == LK_OK, "n=%ld finished with rc=%d %s", n, rc, w.lk.err);
            free(ro_before);
            free(valuables.v);
            world_free(&w);
            noops++;
            break;
        }
        crashes++;
        CHECK_MSG(rc != LK_OK, "n=%ld crash at '%s' but the call reported success", n, w.fb.crash_name);

        char why[300] = "";
        CHECK_MSG(nothing_lost(&w, &valuables, why, sizeof why), "n=%ld (crash at '%s'): %s", n, w.fb.crash_name, why);

        // power back on, app restarted
        char crash_name[64];
        snprintf(crash_name, sizeof crash_name, "%s", w.fb.crash_name);
        world_restart(&w);
        CHECK_MSG(nothing_lost(&w, &valuables, why, sizeof why), "n=%ld after reboot (crash at '%s'): %s", n, crash_name, why);

        if (lk_needs_recovery(&w.lk)) {
            lk_startup(&w.lk);
            rc = lk_recover(&w.lk);
            CHECK_MSG(rc == LK_OK, "n=%ld recovery failed (crash at '%s'): rc=%d %s", n, crash_name, rc, w.lk.err);
        } else {
            lk_startup(&w.lk);
            rc = scen(&w); // nothing had been journaled yet (or the final journal write had landed): just try again
            if (rc == LK_NOOP) rc = LK_OK;
            CHECK_MSG(rc == LK_OK, "n=%ld retry failed (crash at '%s'): rc=%d %s", n, crash_name, rc, w.lk.err);
        }
        CHECK_MSG(w.st.state.phase == PH_IDLE, "n=%ld not idle after recovery", n);

        char *got = snapshot_logical(&w);
        CHECK_MSG(!strcmp(got, want), "n=%ld (crash at '%s'): the result differs from a clean run", n, crash_name);
        if (strcmp(got, want) != 0 && g_failed < 6) {
            printf("---- wanted\n%s---- got\n%s----\n", want, got);
        }
        free(got);
        CHECK_MSG(history_count(&w, w.maya) == want_hist_maya, "n=%ld maya history %zu != %zu (crash at '%s')", n,
                  history_count(&w, w.maya), want_hist_maya, crash_name);
        CHECK_MSG(history_count(&w, w.alex) == want_hist_alex, "n=%ld alex history %zu != %zu (crash at '%s')", n,
                  history_count(&w, w.alex), want_hist_alex, crash_name);
        CHECK_MSG(unclaimed_count(&w) == want_unclaimed, "n=%ld unclaimed %zu != %zu (crash at '%s')", n, unclaimed_count(&w),
                  want_unclaimed, crash_name);
        CHECK_MSG(nothing_lost(&w, &valuables, why, sizeof why), "n=%ld after recovery: %s", n, why);
        if (ro_before) { // the read-only profile was never touched, whatever the power did
            char *now = profile_text(&w, w.alex);
            CHECK_MSG(!strcmp(now, ro_before), "n=%ld (crash at '%s'): the read-only profile's saves changed", n, crash_name);
            free(now);
            CHECK_MSG(history_count(&w, w.alex) == ro_hist, "n=%ld (crash at '%s'): the read-only profile's history changed", n, crash_name);
            CHECK_MSG(!w.st.state.discard, "n=%ld the discard mark was left in state.json", n);
        }
        free(ro_before);

        // a second pass of the same operation must be a clean no-op or at least not break anything
        free(valuables.v);
        world_free(&w);
    }
    CHECK_MSG(noops == 1, "never reached a crash-free run");
    printf("    %s: power cut at %d different points, every one recovered with nothing lost\n", name, crashes);
    free(want);
}


// A game with hundreds of small files (Breath of the Wild has 400) is loaded in a handful of commits, not one per file: on the console every
// commit costs tens of milliseconds, which made loading it take minutes.
static void test_loading_commits_in_batches(void)
{
    g_test = "loading many files";
    World w;
    world_new(&w, 1);
    for (int i = 0; i < 300; i++) {
        char rel[40], txt[40];
        snprintf(rel, sizeof rel, "slot/f%03d.sav", i);
        snprintf(txt, sizeof txt, "file %d of a big game", i);
        put_profile_file(&w, w.alex, A, rel, txt);
    }
    long before = w.fb.commits;
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    long used = w.fb.commits - before;
    CHECK_MSG(used < 60, "300 files took %ld commits", used);
    CHECK(sav_is(&w, A, "slot/f299.sav", "file 299 of a big game") && sav_is(&w, A, "slot/f000.sav", "file 0 of a big game"));
    world_free(&w);
}

// ... but a save with a small journal is committed often enough that the files' own entries never overflow it.
static void test_small_journal_many_files(void)
{
    g_test = "small journal";
    World w;
    world_new(&w, 1);
    fake_title(&w.fb, A)->journal = 256 * 1024; // takes 128 KiB between commits
    for (int i = 0; i < 100; i++) {
        char rel[40];
        snprintf(rel, sizeof rel, "d/f%03d.sav", i);
        put_profile_file(&w, w.alex, A, rel, "tiny");
    }
    long before = w.fb.commits;
    CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
    long used = w.fb.commits - before;
    CHECK_MSG(used >= 10, "100 files on a small journal took only %ld commits", used);
    CHECK(sav_is(&w, A, "d/f099.sav", "tiny"));
    world_free(&w);
}

// ------------------------------------------------------------------------------------------------ read-only profiles

// Alex is loaded as usual, then marked read-only; what he plays is thrown away when he is left, and nothing may write into him.
static void test_readonly(void)
{
    g_test = "read-only profiles";
    {   // (a) a swap away from him: his saves and history are exactly as they were, and Maya's are loaded
        World w;
        world_new(&w, 1);
        CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
        char *before = profile_text(&w, w.alex);
        size_t hist = history_count(&w, w.alex);
        fake_put(&w.fb, A, "save.dat", "A: played in a read-only session");
        fake_put(&w.fb, D, "d.dat", "D: new progress");
        set_ro(&w, w.alex, 1);
        CHECK(lk_profile_readonly(&w.lk, w.alex) == 1 && lk_profile_readonly(&w.lk, w.maya) == 0);
        CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
        char *after = profile_text(&w, w.alex);
        CHECK(!strcmp(before, after));
        CHECK(history_count(&w, w.alex) == hist);
        CHECK(bak_is(&w, w.alex, A, "save.dat", "A: alex progress"));
        CHECK(!bak_has_title(&w, w.alex, D));
        CHECK(sav_is(&w, A, "save.dat", "A: maya progress v2") && sav_is(&w, D, "d.dat", "D: maya's d")); // Maya's own play from before, loaded; nothing of Alex's session
        CHECK(strcmp(w.st.state.holder, w.maya) == 0 && w.st.state.phase == PH_IDLE && !w.st.state.discard);
        CHECK(unclaimed_count(&w) == 0);
        free(before);
        free(after);
        world_free(&w);
    }
    {   // (b) a sign-out: the console is cleared without a backup, and without the "backup wasn't complete" refusal
        World w;
        world_new(&w, 1);
        prep_ro_holder_played(&w);
        world_restart(&w);
        char *before = profile_text(&w, w.alex);
        CHECK(lk_sign_out(&w.lk) == LK_OK);
        char *after = profile_text(&w, w.alex);
        CHECK(!strcmp(before, after));
        CHECK(save_is_empty(&w, A) && save_is_empty(&w, D));
        CHECK(!strcmp(w.st.state.holder, UNCLAIMED) && w.st.state.wiped && !w.st.state.discard);
        free(before);
        free(after);
        world_free(&w);
    }
    {   // (c) the three ways of writing into a profile are refused, and change nothing
        World w;
        world_new(&w, 1);
        prep_cleared(&w); // Alex signed out: a guest session
        fake_put(&w.fb, A, "save.dat", "A: guest progress");
        set_ro(&w, w.alex, 1);
        char *all = snapshot_all(&w);
        CHECK(lk_claim_guest(&w.lk, w.alex) == LK_ERR_READONLY);
        CHECK(lk_rollback(&w.lk, w.alex, "x") == LK_ERR_READONLY);
        char *all2 = snapshot_all(&w);
        CHECK(!strcmp(all, all2));
        free(all);
        free(all2);
        world_free(&w);
        World u;
        world_new(&u, 1);
        prep_unclaimed_kept(&u);
        set_ro(&u, u.maya, 1);
        char leaf[64];
        CHECK(newest_unclaimed(&u, leaf, sizeof leaf));
        all = snapshot_all(&u);
        CHECK(lk_unclaimed_give(&u.lk, leaf, u.maya) == LK_ERR_READONLY);
        all2 = snapshot_all(&u);
        CHECK(!strcmp(all, all2));
        free(all);
        free(all2);
        world_free(&u);
    }
    {   // (d) the control: a profile that is not read-only still backs up, and turning the mark off brings that back
        World w;
        world_new(&w, 1);
        CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
        fake_put(&w.fb, A, "save.dat", "A: played normally");
        CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
        CHECK(bak_is(&w, w.alex, A, "save.dat", "A: played normally"));
        set_ro(&w, w.alex, 1);
        CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
        fake_put(&w.fb, A, "save.dat", "A: played, read-only");
        CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
        CHECK(bak_is(&w, w.alex, A, "save.dat", "A: played normally")); // kept, not replaced
        set_ro(&w, w.alex, 0);
        CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
        fake_put(&w.fb, A, "save.dat", "A: played, no longer read-only");
        CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
        CHECK(bak_is(&w, w.alex, A, "save.dat", "A: played, no longer read-only"));
        world_free(&w);
    }
    {   // (e) a profile.json that is there but can't be read: nothing is guessed, nothing is changed
        World w;
        world_new(&w, 1);
        CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
        fake_put(&w.fb, A, "save.dat", "A: played");
        char rel[FSX_PATH], pj[FSX_PATH];
        snprintf(rel, sizeof rel, "profiles/%s/profile.json", w.alex);
        tpath(&w, pj, sizeof pj, rel);
        FILE *f = fopen(pj, "wb");
        CHECK(f != NULL);
        if (f) {
            fputs("{ this is not json", f);
            fclose(f);
        }
        CHECK(lk_profile_readonly(&w.lk, w.alex) == -1);
        char *all = snapshot_all(&w);
        CHECK(lk_swap_to(&w.lk, w.maya) != LK_OK);
        CHECK(lk_sign_out(&w.lk) != LK_OK);
        char *all2 = snapshot_all(&w);
        CHECK(!strcmp(all, all2));
        free(all);
        free(all2);
        world_free(&w);
    }
    {   // (f) trimming earlier saves leaves a read-only profile's history alone
        World w;
        world_new(&w, 1);
        for (int i = 0; i < 4; i++) {
            char txt[40];
            snprintf(txt, sizeof txt, "A: alex v%d", i);
            CHECK(lk_swap_to(&w.lk, w.alex) == LK_OK);
            fake_put(&w.fb, A, "save.dat", txt);
            CHECK(lk_swap_to(&w.lk, w.maya) == LK_OK);
        }
        size_t hist = history_count(&w, w.alex);
        CHECK(hist >= 3);
        set_ro(&w, w.alex, 1);
        CHECK(lk_history_trim(&w.lk, 1, 0, NULL, NULL) == LK_OK);
        CHECK(history_count(&w, w.alex) == hist);
        set_ro(&w, w.alex, 0);
        CHECK(lk_history_trim(&w.lk, 1, 0, NULL, NULL) == LK_OK);
        CHECK(history_count(&w, w.alex) == 1);
        world_free(&w);
    }
    {   // (g) the mark is kept in profile.json only when set
        World w;
        world_new(&w, 1);
        char rel[FSX_PATH], dir[FSX_PATH];
        snprintf(rel, sizeof rel, "profiles/%s", w.alex);
        tpath(&w, dir, sizeof dir, rel);
        char *txt = read_in(dir, "profile.json");
        CHECK(txt && !strstr(txt, "readonly"));
        free(txt);
        set_ro(&w, w.alex, 1);
        txt = read_in(dir, "profile.json");
        CHECK(txt && strstr(txt, "readonly"));
        free(txt);
        world_restart(&w);
        Profile p;
        CHECK(store_profile_load(&w.st, w.alex, &p) == 0 && p.readonly == 1);
        set_ro(&w, w.alex, 0);
        txt = read_in(dir, "profile.json");
        CHECK(txt && !strstr(txt, "readonly"));
        free(txt);
        world_free(&w);
    }
}

// A power cut in the middle of leaving a read-only profile, and the mark switched off before the app comes back: the journal decided
// (discard), so recovery finishes the same way instead of failing a comparison with a backup that was never made.
static void test_readonly_flag_flip(void)
{
    g_test = "read-only mark changed during a power cut";
    int wiping = 0;
    for (long n = 1; n < 2000; n++) {
        World w;
        world_new(&w, 1);
        prep_ro_holder_played(&w);
        world_restart(&w);
        char *before = profile_text(&w, w.alex);
        fake_arm(&w.fb, n);
        int rc = scen_signout(&w);
        if (!w.fb.crashed) {
            CHECK(rc == LK_OK);
            free(before);
            world_free(&w);
            break;
        }
        world_restart(&w);
        if (w.st.state.phase == PH_WIPING) {
            wiping++;
            CHECK(w.st.state.discard == 1);
            set_ro(&w, w.alex, 0); // someone switches the mark off before the app comes back
            world_restart(&w);
            lk_startup(&w.lk);
            rc = lk_recover(&w.lk);
            CHECK_MSG(rc == LK_OK, "n=%ld recovery failed: rc=%d %s", n, rc, w.lk.err);
            CHECK(save_is_empty(&w, A) && save_is_empty(&w, D));
            char *after = profile_text(&w, w.alex);
            CHECK(!strcmp(before, after));
            free(after);
            CHECK(w.st.state.phase == PH_IDLE && !w.st.state.discard);
        }
        free(before);
        world_free(&w);
    }
    CHECK(wiping > 0);
    printf("    read-only mark switched off after a power cut at %d different points of a sign-out: finished the same way\n", wiping);
}

// ------------------------------------------------------------------------------------------------ main

// ------------------------------------------------------------------------------------------------ profile colours

// A guest plays, then makes a profile and keeps what they played.
static void test_claim_guest(void)
{
    g_test = "keep guest play";
    World w;
    world_new(&w, 1);
    Profile z;
    store_profile_create(&w.st, "Zed", NULL, &z);
    CHECK_MSG(lk_claim_guest(&w.lk, z.id) == LK_ERR_BUSY_STATE, "not while someone's saves are loaded");
    prep_guest_played(&w);
    CHECK_MSG(lk_played_titles(&w.lk) == 2, "the guest played two games (%d)", lk_played_titles(&w.lk));
    int rc = lk_claim_guest(&w.lk, g_zed);
    CHECK_MSG(rc == LK_OK, "kept: rc=%d %s", rc, w.lk.err);
    CHECK_MSG(!strcmp(w.st.state.holder, g_zed) && !w.st.state.wiped && w.st.state.loaded_at > 0, "Zed is loaded now");
    CHECK_MSG(bak_is(&w, g_zed, A, "save.dat", "A: guest progress") && bak_is(&w, g_zed, D, "d.dat", "D: guest's d"),
              "the guest's games are Zed's backup");
    CHECK_MSG(sav_is(&w, A, "save.dat", "A: guest progress"), "and still on the console");
    CHECK_MSG(lk_claim_guest(&w.lk, g_zed) == LK_NOOP, "a second time does nothing");
    fake_put(&w.fb, A, "save.dat", "A: zed played on");
    CHECK(lk_sign_out(&w.lk) == LK_OK);
    CHECK_MSG(bak_is(&w, g_zed, A, "save.dat", "A: zed played on"), "and Zed's next sign-out backs up as usual");
    world_free(&w);
}

static void test_snapshot(void)
{
    g_test = "snapshot";
    World w;
    world_new(&w, 1);
    Profile z;
    store_profile_create(&w.st, "Zed", NULL, &z);
    CHECK(lk_swap_to(&w.lk, z.id) == LK_OK);
    CHECK(lk_sign_out(&w.lk) == LK_OK);
    char parent[FSX_PATH], out[FSX_PATH], stale[FSX_PATH], marker[FSX_PATH];
    fsx_join(parent, sizeof parent, w.fb.base, "Washi Backups");
    fsx_join(stale, sizeof stale, parent, "old.partial");
    fsx_join(marker, sizeof marker, stale, "x.dat");
    fsx_mkdir_p(stale);
    fsx_write_file(marker, "x", 1);
    int rc = lk_snapshot(&w.lk, parent, "10-05-2026--14-30", out, sizeof out);
    CHECK_MSG(rc == LK_OK, "snapshot: rc=%d %s", rc, w.lk.err);
    char want[FSX_PATH];
    fsx_join(want, sizeof want, parent, "10-05-2026--14-30");
    CHECK_MSG(!strcmp(out, want), "named as asked (%s)", out);
    CHECK_MSG(!fsx_exists(stale), "a stale half-made one is cleared");
    char prof[FSX_PATH], cfg[FSX_PATH];
    fsx_join(prof, sizeof prof, out, "profiles");
    fsx_join(cfg, sizeof cfg, out, "config.json");
    CHECK_MSG(fsx_is_dir(prof) && fsx_exists(cfg), "profiles and settings are in it");
    CHECK_MSG(fsx_tree_size(out) > 0, "not empty");
    // the same minute again: a second folder, the first untouched
    char out2[FSX_PATH];
    CHECK(lk_snapshot(&w.lk, parent, "10-05-2026--14-30", out2, sizeof out2) == LK_OK);
    CHECK_MSG(strcmp(out, out2) != 0 && fsx_is_dir(out) && fsx_is_dir(out2), "a second one gets its own folder (%s)", out2);
    // no room: refused, nothing left behind
    w.fb.free_bytes = 1024;
    char out3[FSX_PATH];
    rc = lk_snapshot(&w.lk, parent, "10-06-2026--09-00", out3, sizeof out3);
    CHECK_MSG(rc == LK_ERR_SPACE && !out3[0], "a full card refuses (rc=%d)", rc);
    char leftover[FSX_PATH];
    fsx_join(leftover, sizeof leftover, parent, "10-06-2026--09-00");
    CHECK_MSG(!fsx_exists(leftover), "and leaves nothing");
    world_free(&w);
}

// A backup of everything leaves out what comes back by itself or is someone else's to write: the Extra icons, the logs, the reminder's log.
static void test_snapshot_skips(void)
{
    g_test = "snapshot skips";
    World w;
    world_new(&w, 1);
    Profile z;
    store_profile_create(&w.st, "Zed", NULL, &z);
    CHECK(lk_swap_to(&w.lk, z.id) == LK_OK);
    CHECK(lk_sign_out(&w.lk) == LK_OK);
    char p[FSX_PATH];
    tpath(&w, p, sizeof p, "pictures/game-a");
    fsx_mkdir_p(p);
    tpath(&w, p, sizeof p, "pictures/game-a/A01.png");
    fsx_write_file(p, "png", 3);
    tpath(&w, p, sizeof p, "reminder.log");
    fsx_write_file(p, "note shown\n", 11);
    char parent[FSX_PATH], out[FSX_PATH];
    fsx_join(parent, sizeof parent, w.fb.base, "Washi Backups");
    int rc = lk_snapshot(&w.lk, parent, "10-11-2026--10-37", out, sizeof out);
    CHECK_MSG(rc == LK_OK, "snapshot: rc=%d %s", rc, w.lk.err);
    char q[FSX_PATH];
    fsx_join(q, sizeof q, out, "pictures");
    CHECK_MSG(!fsx_exists(q), "the Extra icons are not in it");
    fsx_join(q, sizeof q, out, "logs");
    CHECK_MSG(!fsx_exists(q), "the logs are not in it");
    fsx_join(q, sizeof q, out, "reminder.log");
    CHECK_MSG(!fsx_exists(q), "the reminder's log is not in it");
    char src[FSX_PATH];
    tpath(&w, src, sizeof src, "profiles");
    fsx_join(q, sizeof q, out, "profiles");
    CHECK_MSG(fsx_tree_equal(src, q) == 1, "the profiles are, byte for byte");
    world_free(&w);
}

// The check after the copy reads the copy back: a file that changed on the card after it was written fails the backup, and nothing is left of it.
static char g_bad_partial[FSX_PATH];
static int g_bad_done;

static int flip_first_done(const char *dir)
{
    char **names;
    size_t n;
    int done = 0;
    if (fsx_list(dir, &names, &n) != 0) return 0;
    for (size_t i = 0; i < n && !done; i++) {
        char p[FSX_PATH];
        fsx_join(p, sizeof p, dir, names[i]);
        if (fsx_is_dir(p)) {
            done = flip_first_done(p);
            continue;
        }
        FILE *f = fopen(p, "r+b");
        if (!f) continue;
        int c = fgetc(f);
        if (c != EOF) { // (a file with something on the card: one already written and closed)
            fseek(f, 0, SEEK_SET);
            fputc(c ^ 0xFF, f);
            done = 1;
        }
        fclose(f);
    }
    fsx_list_free(names, n);
    return done;
}

static void corrupt_cb(void *user, const char *what, uint64_t cur, uint64_t total)
{
    (void)user, (void)what, (void)cur, (void)total;
    if (!g_bad_done && fsx_is_dir(g_bad_partial)) g_bad_done = flip_first_done(g_bad_partial);
}

static void test_snapshot_catches_bad_copy(void)
{
    g_test = "snapshot catches a bad copy";
    World w;
    world_new(&w, 1);
    Profile z;
    store_profile_create(&w.st, "Zed", NULL, &z);
    CHECK(lk_swap_to(&w.lk, z.id) == LK_OK);
    CHECK(lk_sign_out(&w.lk) == LK_OK);
    char parent[FSX_PATH], out[FSX_PATH];
    fsx_join(parent, sizeof parent, w.fb.base, "Washi Backups");
    fsx_join(g_bad_partial, sizeof g_bad_partial, parent, "10-11-2026--11-00.partial");
    g_bad_done = 0;
    LkProgressFn keep = w.lk.progress;
    w.lk.progress = corrupt_cb;
    int rc = lk_snapshot(&w.lk, parent, "10-11-2026--11-00", out, sizeof out);
    w.lk.progress = keep;
    CHECK_MSG(g_bad_done, "a copied file was changed during the backup");
    CHECK_MSG(rc == LK_ERR_VERIFY, "the check caught it (rc=%d %s)", rc, w.lk.err);
    char done[FSX_PATH];
    fsx_join(done, sizeof done, parent, "10-11-2026--11-00");
    CHECK_MSG(!fsx_exists(done) && !fsx_exists(g_bad_partial), "and nothing of it is left");
    world_free(&w);
}

static size_t snap_count(Locker *lk, const char *parent, int automatic_only)
{
    LkSnapshot *v;
    size_t n, c = 0;
    if (lk_snapshot_list(lk, parent, &v, &n) != LK_OK) return 0;
    for (size_t i = 0; i < n; i++) c += !automatic_only || v[i].automatic;
    free(v);
    return c;
}

static void test_snapshot_keep_and_restore(void)
{
    g_test = "snapshot keep + restore";
    World w;
    world_new(&w, 1);
    char parent[FSX_PATH], out[FSX_PATH];
    fsx_join(parent, sizeof parent, w.fb.base, "Washi Backups");
    CHECK(lk_snapshot(&w.lk, parent, "10-01-2026--09-00", out, sizeof out) == LK_OK); // a manual one, oldest
    static const char *const when[] = {"10-02-2026--09-00", "10-03-2026--09-00", "10-04-2026--09-00", "10-05-2026--09-00",
                                       "10-06-2026--09-30", "10-07-2026--08-00"};
    for (size_t i = 0; i < 6; i++) CHECK_MSG(lk_snapshot_auto(&w.lk, parent, when[i], i == 0 ? "Zed" : "", 4, out, sizeof out) == LK_OK, "auto %zu: %s", i, w.lk.err);
    CHECK_MSG(snap_count(&w.lk, parent, 1) == 4, "only the newest 4 automatic ones stay (%zu)", snap_count(&w.lk, parent, 1));
    CHECK_MSG(snap_count(&w.lk, parent, 0) == 5, "the manual one stays too (%zu)", snap_count(&w.lk, parent, 0));
    CHECK_MSG(lk_snapshot_auto(&w.lk, parent, "10-07-2026--20-00", "", 4, out, sizeof out) == LK_NOOP, "a second automatic one the same day isn't made");
    CHECK_MSG(lk_snapshot_auto_due(&w.lk, parent, "10-08-2026--00-05") == 1 && lk_snapshot_auto_due(&w.lk, parent, "10-07-2026--23-59") == 0,
              "but the next day it is due again");
    CHECK_MSG(snap_count(&w.lk, parent, 0) == 5, "and nothing was added (%zu)", snap_count(&w.lk, parent, 0));
    LkSnapshot *v;
    size_t n;
    CHECK(lk_snapshot_list(&w.lk, parent, &v, &n) == LK_OK && n == 5);
    CHECK_MSG(!strcmp(v[0].leaf, "10-07-2026--08-00") && v[0].automatic && !strcmp(v[1].leaf, "10-06-2026--09-30"), "newest first (%s, %s)", v[0].leaf, v[1].leaf);
    CHECK_MSG(!strcmp(v[4].leaf, "10-01-2026--09-00") && !v[4].automatic, "and the manual one is last");
    CHECK_MSG(v[0].note[0] == 0 && v[0].nprofiles == 2 && v[0].npeople == 2, "each lists its people (%d, %d)", v[0].nprofiles, v[0].npeople);
    free(v);

    // restore: a profile made after the backup is gone, one deleted after it is back
    CHECK(lk_snapshot(&w.lk, parent, "10-07-2026--12-00", out, sizeof out) == LK_OK);
    Profile z;
    CHECK(store_profile_create(&w.st, "Zed", NULL, &z) == 0);
    char zdir[FSX_PATH], mdir[FSX_PATH];
    store_profile_dir(&w.st, z.id, zdir, sizeof zdir);
    store_profile_dir(&w.st, w.maya, mdir, sizeof mdir);
    fsx_rmtree(mdir);
    CHECK(lk_snapshot_restore(&w.lk, parent, "../etc", "10-07-2026--13-00") != LK_OK);
    CHECK_MSG(fsx_is_dir(zdir), "a bad name changes nothing");
    snprintf(w.st.state.holder, sizeof w.st.state.holder, "%s", w.alex);
    CHECK_MSG(lk_snapshot_restore(&w.lk, parent, "10-07-2026--12-00", "10-07-2026--13-00") == LK_ERR_IS_HOLDER, "not while someone is loaded");
    snprintf(w.st.state.holder, sizeof w.st.state.holder, "%s", UNCLAIMED);
    int rc = lk_snapshot_restore(&w.lk, parent, "10-07-2026--12-00", "10-07-2026--13-00");
    CHECK_MSG(rc == LK_OK, "restored: rc=%d %s", rc, w.lk.err);
    CHECK_MSG(!fsx_exists(zdir) && fsx_is_dir(mdir), "Zed is gone and Maya is back");
    CHECK_MSG(snap_count(&w.lk, parent, 0) == 7, "and a backup of what was kept before was made (%zu)", snap_count(&w.lk, parent, 0));
    char safe[FSX_PATH], zin[FSX_PATH], leftover[FSX_PATH];
    (void)zin;
    fsx_join(safe, sizeof safe, parent, "10-07-2026--13-00");
    fsx_join(zin, sizeof zin, safe, "profiles");
    CHECK_MSG(fsx_is_dir(zin), "it has the old state");
    snprintf(leftover, sizeof leftover, "%s/profiles.replaced", w.st.root);
    CHECK_MSG(!fsx_exists(leftover), "nothing set aside is left");
    // a restore that was cut short is put right when the app starts
    char cur[FSX_PATH], aside[FSX_PATH];
    snprintf(cur, sizeof cur, "%s/profiles", w.st.root);
    snprintf(aside, sizeof aside, "%s/profiles.replaced", w.st.root);
    rename(cur, aside);
    CHECK(lk_startup(&w.lk) == 0);
    CHECK_MSG(fsx_is_dir(cur) && !fsx_exists(aside), "a half-done restore is put back at start-up");
    CHECK(lk_snapshot_delete(&w.lk, parent, "10-07-2026--13-00") == LK_OK && !fsx_exists(safe));
    CHECK_MSG(lk_snapshot_delete(&w.lk, parent, "../x") != LK_OK, "a delete can't leave the folder");
    world_free(&w);
}

static void test_trash_purge(void)
{
    g_test = "delete for good";
    World w;
    world_new(&w, 1);
    Profile z, n;
    CHECK(store_profile_create(&w.st, "Zed", NULL, &z) == 0);
    CHECK(store_profile_create(&w.st, "Noor", NULL, &n) == 0);
    put_profile_file(&w, z.id, A, "save.dat", "A: zed");
    CHECK(lk_delete_profile(&w.lk, z.id) == LK_OK);
    CHECK(lk_delete_profile(&w.lk, n.id) == LK_OK);
    TrashEntry *t = NULL;
    size_t cnt = 0;
    CHECK(store_trash_list(&w.st, &t, &cnt) == 0 && cnt == 2);
    char zleaf[sizeof t[0].leaf] = "", nleaf[sizeof t[0].leaf] = "";
    for (size_t i = 0; i < cnt; i++) snprintf(!strcmp(t[i].name, "Zed") ? zleaf : nleaf, sizeof zleaf, "%s", t[i].leaf);
    free(t);
    CHECK(store_trash_purge(&w.st, "") == -1 && store_trash_purge(&w.st, "../profiles") == -1 && store_trash_purge(&w.st, ".gone-x") == -1);
    CHECK_MSG(store_trash_purge(&w.st, "no-such-entry") == -1, "an entry that isn't there");
    char dir[FSX_PATH];
    snprintf(dir, sizeof dir, "profiles/.trash/%s", zleaf);
    char full[FSX_PATH];
    tpath(&w, full, sizeof full, dir);
    CHECK(fsx_is_dir(full));
    CHECK_MSG(store_trash_purge(&w.st, zleaf) == 0 && !fsx_exists(full), "Zed is gone from the trash folder");
    CHECK(store_trash_list(&w.st, &t, &cnt) == 0 && cnt == 1 && !strcmp(t[0].name, "Noor"));
    free(t);
    CHECK_MSG(store_profile_restore(&w.st, zleaf, NULL, NULL) == -1, "and can't be restored any more");
    // an interrupted delete: the half-removed folder isn't listed, and is cleared next time
    char gone[FSX_PATH], gone_full[FSX_PATH];
    snprintf(gone, sizeof gone, "profiles/.trash/.gone-%s", nleaf);
    tpath(&w, gone_full, sizeof gone_full, gone);
    snprintf(dir, sizeof dir, "profiles/.trash/%s", nleaf);
    tpath(&w, full, sizeof full, dir);
    rename(full, gone_full);
    CHECK(store_trash_list(&w.st, &t, &cnt) == 0 && cnt == 0);
    free(t);
    store_trash_tidy(&w.st);
    CHECK_MSG(!fsx_exists(gone_full), "what an interrupted delete left is cleared");
    world_free(&w);
}

static void test_name_match(void)
{
    g_test = "search";
    static const struct { const char *q; int want; } adrian[] = {
        {"adr", 4}, {"ADRIAN", 4}, {"ria", 2}, {"ian", 2}, {"arn", 1}, {"aia", 1}, {"n", 2}, {"a d r", 4},
        {"adrianx", 0}, {"nai", 0}, {"z", 0}, {"", 0}, {"   ", 0},
    };
    for (size_t i = 0; i < sizeof adrian / sizeof adrian[0]; i++)
        CHECK_MSG(store_name_match("adrian", adrian[i].q) == adrian[i].want, "\"%s\" in adrian: %d (wanted %d)", adrian[i].q,
                  store_name_match("adrian", adrian[i].q), adrian[i].want);
    CHECK_MSG(store_name_match("Mary Jane", "jane") == 3, "a later word");
    CHECK_MSG(store_name_match("Mary Jane", "ryj") == 1, "letters in order across a space");
    CHECK_MSG(store_name_match("Jos\xC3\xA9", "jos\xC3\xA9") == 4, "letters beyond ASCII match themselves");
    CHECK_MSG(store_name_match(NULL, "a") == 0, "no name, no match");
}

static float lab_dist(const float a[3], const float b[3])
{
    return sqrtf((a[0] - b[0]) * (a[0] - b[0]) + (a[1] - b[1]) * (a[1] - b[1]) + (a[2] - b[2]) * (a[2] - b[2]));
}

static float hue_gap(const float a[3], const float b[3]) // degrees, 0..180
{
    float d = fabsf(atan2f(a[2], a[1]) - atan2f(b[2], b[1])) * 180.0f / 3.14159265f;
    return d > 180 ? 360 - d : d;
}

static void test_profile_tint(void)
{
    g_test = "tint";
    static const char *people[][2] = {{"Alex", "abc1234"}, {"Maya", "xyz9876"}, {"benjamin", ""}, {"olivia", "olv2201"},
                                      {"Jos\xC3\xA9", "jse1000"}, {"q", ""},      {"1", "aaa0001"},   {"Miku", ""},
                                      {"Chloe", "chl4410"},  {"Dani", ""},        {"Eli", "eli7777"}, {"Hana", "han0420"}};
    enum { NP = sizeof people / sizeof people[0] };
    CHECK_MSG(profile_tint(0xFF999A, "Alex", "abc1234") == profile_tint(0xFF999A, "Alex", "abc1234"), "the same input, the same colour");
    CHECK_MSG(profile_tint(0xFF999A, "Alex", "abc1234") == profile_tint(0xFF999A, "ALEX", "ABC1234"), "letter case doesn't matter");
    int differ = 1, near = 1, hue = 1, apart = 1, kept = 1;
    float worst_d = 0, worst_h = 0;
    for (int s = 0; s < SWATCH_COUNT; s++) {
        uint32_t sw = SWATCH_RGB[s];
        float ls[3];
        tint_oklab(sw, ls);
        float cs = hypotf(ls[1], ls[2]);
        if (profile_tint(sw, "Alex", "abc1234") == profile_tint(sw, "Maya", "xyz9876")) differ = 0;
        for (int p = 0; p < NP; p++) {
            uint32_t t = profile_tint(sw, people[p][0], people[p][1]);
            float lt[3];
            tint_oklab(t, lt);
            float d = lab_dist(ls, lt), hg = cs >= 0.04f ? hue_gap(ls, lt) : 0;
            if (d > worst_d) worst_d = d;
            if (hg > worst_h) worst_h = hg;
            if (d > 0.13f) { near = 0; printf("    swatch %d %06X -> %06X is %.3f away\n", s, sw, t, d); }
            if (hg > 20.0f) { hue = 0; printf("    swatch %d %06X -> %06X turned %.1f degrees\n", s, sw, t, hg); }
            for (int dark = 0; dark < 2; dark++) { // on the light theme's card, track and focused fill, and the dark one's
                uint32_t card = dark ? 0x333333 : 0xFFFFFF, track = dark ? 0x575757 : 0xE4E4E4, fill = dark ? 0x1F2227 : 0xFFFFFF;
                uint32_t v = tint_for_theme(t, dark);
                float lv[3], lk[3], lr[3], lf[3];
                tint_oklab(v, lv);
                tint_oklab(card, lk);
                tint_oklab(track, lr);
                tint_oklab(fill, lf);
                float dk = hypotf(2 * (lv[0] - lk[0]), hypotf(lv[1] - lk[1], lv[2] - lk[2]));
                float dr = hypotf(2 * (lv[0] - lr[0]), hypotf(lv[1] - lr[1], lv[2] - lr[2]));
                float df = hypotf(2 * (lv[0] - lf[0]), hypotf(lv[1] - lf[1], lv[2] - lf[2]));
                if (dk < 0.155f || dr < 0.155f || df < 0.155f) {
                    apart = 0;
                    printf("    %06X on %06X/%06X/%06X: %.3f %.3f %.3f\n", v, card, track, fill, dk, dr, df);
                }
                if (hypotf(lv[1], lv[2]) >= 0.05f && hue_gap(lv, lt) > 6.0f) { kept = 0; printf("    %06X -> %06X changed hue\n", t, v); }
            }
        }
    }
    CHECK_MSG(differ, "two people on one background get two colours");
    CHECK_MSG(near, "every colour stays close to its background (worst %.3f)", worst_d);
    CHECK_MSG(hue, "and within 20 degrees of its hue (worst %.1f)", worst_h);
    CHECK_MSG(apart, "every colour stands apart from the card, the bar's track and a focused row's fill, in both themes");
    CHECK_MSG(kept, "standing apart changes lightness, not hue");
    float lr[3], lt[3];
    tint_oklab(0xFF999A, lr);
    for (int p = 0; p < NP; p++) { // light red: a light red, salmon or pink, never another family
        tint_oklab(profile_tint(0xFF999A, people[p][0], people[p][1]), lt);
        float h = atan2f(lt[2], lt[1]) * 180.0f / 3.14159265f;
        CHECK_MSG(h > -5 && h < 45 && lt[0] > 0.7f, "light red stays light red (%s: hue %.0f, L %.2f)", people[p][0], h, lt[0]);
    }
    printf("  tint: worst distance %.3f, worst hue turn %.1f\n", worst_d, worst_h);
}

int main(void)
{
    test_hashes();
    test_store_profiles_and_pins();
    test_store_online_icon();
    test_logs();
    test_state_files();
    test_swap_basic();
    test_progress_plan();
    test_history_keeps_five();
    test_games_stay_with_their_player();
    test_clearing_needs_a_complete_backup();
    test_refusals();
    test_verify();
    test_silent_write_corruption_is_caught();
    test_journal_size_commits();
    test_rollback_and_delete();
    test_default_account_and_target();
    test_nacp_names();
    test_system_profile();
    test_new_fields();
    test_deleted_profiles();
    test_storage();
    test_profile_tint();
    test_name_match();
    test_claim_guest();
    test_snapshot();
    test_snapshot_skips();
    test_snapshot_catches_bad_copy();
    test_trash_purge();
    test_snapshot_keep_and_restore();
    test_unclaimed();

    crash_loop("crash:swap maya->alex", scen_swap, NULL);
    crash_loop("crash:swap alex->maya", scen_swap_to_maya, prep_big_world_for_swap_from_alex);
    crash_loop("crash:sign-out", scen_signout, NULL);
    crash_loop("crash:first swap (unclaimed)", scen_swap_from_unclaimed, prep_unclaimed);
    crash_loop("crash:swap from a cleared console", scen_swap_to_maya, prep_cleared);
    crash_loop("crash:sign-out after playing", scen_signout, prep_signout_after_play);
    crash_loop("crash:a guest keeps their play", scen_claim, prep_guest_played);
    crash_loop("crash:unclaimed saves given to a profile", scen_give, prep_unclaimed_kept);
    g_ro_run = 1;
    crash_loop("crash:swap away from a read-only profile", scen_swap_to_maya, prep_ro_holder_played);
    crash_loop("crash:sign-out of a read-only profile", scen_signout, prep_ro_holder_played);
    g_ro_run = 0;
    test_readonly();
    test_readonly_flag_flip();
    test_loading_commits_in_batches();
    test_small_journal_many_files();

    printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
