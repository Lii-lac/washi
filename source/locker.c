#include "locker.h"

#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "hash.h"

#define TRY(x)                                \
    do {                                        \
        int _r = (x);                           \
        if (_r != LK_OK) return _r;             \
    } while (0)

#define BATTERY_MIN_PERCENT 15
#define COPY_BUF (256 * 1024)

// ---------------------------------------------------------------------------------------------------- plumbing

static int fail(Locker *lk, int status, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
static int fail(Locker *lk, int status, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lk->err, sizeof lk->err, fmt, ap);
    va_end(ap);
    return status;
}

static void warn(Locker *lk, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void warn(Locker *lk, const char *fmt, ...)
{
    if (lk->nwarn >= LK_MAX_WARN) return;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(lk->warn[lk->nwarn++], sizeof lk->warn[0], fmt, ap);
    va_end(ap);
}

static void reset(Locker *lk)
{
    lk->err[0] = 0;
    lk->nwarn = 0;
    lk->swap_titles = 0;
    lk->swap_bytes = 0;
    lk->unclaimed_saved = 0;
    lk->uncommitted = 0;
    lk->cur = lk->total = 0;
    lk->label[0] = 0;
    lk->phase = 0;
    lk->cur_title = 0;
    lk->cur_name[0] = 0;
    lk->plan_n = 0;
    lk->plan_serial++;
    lk->sys_bytes_n = 0;
}

static void report(Locker *lk)
{
    if (lk->progress) lk->progress(lk->progress_user, lk->label, lk->cur, lk->total);
}

// A crash point. In production this does nothing; in tests it can "cut the power" here.
static int cp(Locker *lk, const char *name)
{
    if (lk->be->checkpoint(lk->be, name) != 0) return fail(lk, LK_ERR_BACKEND, "Interrupted at %s.", name);
    return LK_OK;
}

int lk_init(Locker *lk, Backend *be, Store *st)
{
    memset(lk, 0, sizeof *lk);
    lk->be = be;
    lk->st = st;
    lk->bufsz = COPY_BUF;
    lk->buf = malloc(lk->bufsz);
    if (!lk->buf) return -1;
    if (st->cfg.target_uid[0]) be->set_target(be, st->cfg.target_uid);
    return 0;
}

void lk_free(Locker *lk)
{
    free(lk->buf);
    lk->buf = NULL;
}

int lk_needs_recovery(const Locker *lk)
{
    return lk->st->state.phase != PH_IDLE;
}

void lk_title_label(Locker *lk, uint64_t title, char *out, size_t n)
{
    out[0] = 0;
    if (lk->be->title_name(lk->be, title, out, n) != BE_OK || !out[0]) snprintf(out, n, "%016" PRIx64, title);
}

// ---------------------------------------------------------------------------------------------------- title sets

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

static int ts_has(const TitleSet *s, uint64_t t)
{
    for (size_t i = 0; i < s->n; i++)
        if (s->v[i] == t) return 1;
    return 0;
}

static void ts_add(TitleSet *s, uint64_t t)
{
    if (s->n < LK_MAX_TITLES && !ts_has(s, t)) s->v[s->n++] = t;
}

static void ts_sort(TitleSet *s)
{
    qsort(s->v, s->n, sizeof s->v[0], cmp_u64);
}

int lk_list_titles(Locker *lk, TitleSet *out)
{
    out->n = 0;
    size_t n = 0;
    if (lk->be->list_saves(lk->be, out->v, LK_MAX_TITLES, &n) != BE_OK) return -1;
    out->n = n;
    ts_sort(out);
    return 0;
}

// the account's saves, minus excluded titles
static int load_sys(Locker *lk, TitleSet *out)
{
    TitleSet *all = &lk->uni; // scratch
    if (lk_list_titles(lk, all) != 0) return fail(lk, LK_ERR_BACKEND, "Couldn't list the saves on the account.");
    out->n = 0;
    for (size_t i = 0; i < all->n; i++)
        if (!store_is_excluded(lk->st, all->v[i])) out->v[out->n++] = all->v[i];
    return LK_OK;
}

// true if the folder has anything in it (a save with nothing in it is the same as no save)
static int dir_has_entries(const char *dir)
{
    char **names;
    size_t n = 0;
    if (fsx_list(dir, &names, &n) != 0) return 0;
    fsx_list_free(names, n);
    return n > 0;
}

// title folders (16 hex digits) inside a backup folder; excluded titles are left out unless `keep_excluded`.
// An empty folder (older versions kept one for every game on the console) doesn't count as a game.
static int load_dir_titles(Locker *lk, const char *dir, TitleSet *out, int keep_excluded)
{
    out->n = 0;
    char **names;
    size_t n;
    if (fsx_list(dir, &names, &n) != 0) return fail(lk, LK_ERR_IO, "Couldn't read %s.", dir);
    for (size_t i = 0; i < n; i++) {
        uint64_t t;
        if (store_parse_title(names[i], &t) != 0 || (!keep_excluded && store_is_excluded(lk->st, t))) continue;
        char p[FSX_PATH];
        if (fsx_join(p, sizeof p, dir, names[i]) == 0 && dir_has_entries(p)) ts_add(out, t);
    }
    fsx_list_free(names, n);
    ts_sort(out);
    return LK_OK;
}

// ---------------------------------------------------------------------------------------------------- copying

// What a backup of everything copied: every file, its size and the CRC of the bytes read for it. The check after the copy then reads only the copy (the
// folder itself was read once, to copy it), which halves the reading that kept the screen at 100% for a long while.
typedef struct LkManifest {
    struct LkManFile { char *path; uint64_t size; uint32_t crc; } *v;
    size_t n, cap;
    int oom;
} LkManifest;

static void man_add(LkManifest *m, const char *path, uint64_t size, uint32_t crc)
{
    if (m->n == m->cap) {
        size_t c = m->cap ? m->cap * 2 : 64;
        struct LkManFile *v = realloc(m->v, c * sizeof *v);
        if (!v) {
            m->oom = 1;
            return;
        }
        m->v = v;
        m->cap = c;
    }
    char *p = strdup(path);
    if (!p) {
        m->oom = 1;
        return;
    }
    m->v[m->n].path = p;
    m->v[m->n].size = size;
    m->v[m->n].crc = crc;
    m->n++;
}

static void man_free(LkManifest *m)
{
    for (size_t i = 0; i < m->n; i++) free(m->v[i].path);
    free(m->v);
    memset(m, 0, sizeof *m);
}

static int copy_file(Locker *lk, const char *src, const char *dst, int to_save)
{
    FILE *in = fopen(src, "rb");
    if (!in) return fail(lk, LK_ERR_IO, "Couldn't read %s.", src);
    FILE *out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return fail(lk, to_save ? LK_ERR_BACKEND : LK_ERR_IO, "Couldn't create %s.", dst);
    }
    size_t chunk = lk->bufsz;
    if (to_save && lk->journal_limit && chunk > lk->journal_limit / 2) {
        chunk = lk->journal_limit / 2;
        if (chunk < 4096) chunk = 4096;
    }
    int rc = LK_OK;
    uint32_t crc = 0;
    uint64_t got = 0;
    for (;;) {
        size_t n = fread(lk->buf, 1, chunk, in);
        if (n == 0) {
            if (ferror(in)) rc = fail(lk, LK_ERR_IO, "Couldn't read %s.", src);
            break;
        }
        if (lk->man) {
            crc = crc32_update(crc, lk->buf, n);
            got += n;
        }
        if (to_save) {
            // A save can only take so many uncommitted bytes: commit before we'd go over (JKSV does the same).
            if (lk->uncommitted + n > lk->journal_limit) {
                if (fflush(out) != 0 || lk->be->commit(lk->be) != BE_OK) {
                    rc = fail(lk, LK_ERR_BACKEND, "Couldn't commit the save while writing %s.", dst);
                    break;
                }
                lk->uncommitted = 0;
            }
            if (lk->be->write(lk->be, out, lk->buf, n) != BE_OK) {
                rc = fail(lk, LK_ERR_BACKEND, "Couldn't write %s into the save.", dst);
                break;
            }
            lk->uncommitted += n;
        } else if (fwrite(lk->buf, 1, n, out) != n) {
            rc = fail(lk, LK_ERR_IO, "Couldn't write %s (is the SD card full?).", dst);
            break;
        }
        lk->cur += n;
        report(lk);
    }
    if (fflush(out) != 0 && rc == LK_OK) rc = fail(lk, LK_ERR_IO, "Couldn't write %s.", dst);
    if (fclose(out) != 0 && rc == LK_OK) rc = fail(lk, LK_ERR_IO, "Couldn't write %s.", dst);
    fclose(in);
    if (rc == LK_OK && lk->man) man_add(lk->man, dst, got, crc);
    if (rc == LK_OK && to_save) {
        if (lk->be->commit(lk->be) != BE_OK) return fail(lk, LK_ERR_BACKEND, "Couldn't commit %s.", dst);
        lk->uncommitted = 0;
    }
    return rc;
}

// Copies the contents of directory `src` into directory `dst` (which must exist or be creatable).
static int copy_tree(Locker *lk, const char *src, const char *dst, int to_save)
{
    mkdir(dst, 0777); // fails harmlessly if it exists (including the root of a mounted save)
    {
        char **probe;
        size_t pn;
        if (fsx_list(dst, &probe, &pn) != 0)
            return fail(lk, to_save ? LK_ERR_BACKEND : LK_ERR_IO, "Couldn't create folder %s.", dst);
        fsx_list_free(probe, pn);
    }
    char **names;
    size_t n;
    if (fsx_list(src, &names, &n) != 0) return fail(lk, LK_ERR_IO, "Couldn't read %s.", src);
    int rc = LK_OK;
    for (size_t i = 0; i < n && rc == LK_OK; i++) {
        char s[FSX_PATH], d[FSX_PATH];
        if (fsx_join(s, sizeof s, src, names[i]) || fsx_join(d, sizeof d, dst, names[i])) {
            rc = fail(lk, LK_ERR_IO, "Path too long: %s", names[i]);
            break;
        }
        rc = fsx_is_dir(s) ? copy_tree(lk, s, d, to_save) : copy_file(lk, s, d, to_save);
    }
    fsx_list_free(names, n);
    return rc;
}

static int wipe_tick(void *user)
{
    Locker *lk = user;
    if (++lk->wipe_count % 128 == 0 && lk->be->commit(lk->be) != BE_OK) return -1;
    return 0;
}

static int empty_save(Locker *lk)
{
    lk->wipe_count = 0;
    if (fsx_rmtree_ex(lk->be->save_root, 0, wipe_tick, lk) != 0)
        return fail(lk, LK_ERR_BACKEND, "Couldn't clear the save.");
    return LK_OK;
}

static uint64_t journal_limit_for(Locker *lk, uint64_t title)
{
    uint64_t j = lk->be->journal_size(lk->be, title);
    uint64_t limit = j == 0 ? 0x80000 : (j > 0x200000 ? j - 0x100000 : j / 2);
    if (limit > 0x1000000) limit = 0x1000000;
    return limit;
}

// ---------------------------------------------------------------------------------------------------- journal

static int journal(Locker *lk, Phase phase, const char *from, const char *to)
{
    State *s = &lk->st->state;
    s->phase = phase;
    snprintf(s->from, sizeof s->from, "%s", from);
    snprintf(s->to, sizeof s->to, "%s", to);
    s->started = (int64_t)lk->be->now(lk->be);
    if (store_save_state(lk->st) != 0) return fail(lk, LK_ERR_IO, "Couldn't write the swap journal to the SD card.");
    return LK_OK;
}

static void profile_touch(Locker *lk, const char *id)
{
    Profile p;
    if (store_profile_load(lk->st, id, &p) == 0) {
        p.last_used = (int64_t)lk->be->now(lk->be);
        store_profile_save(lk->st, &p);
    }
}

static int finish_swap(Locker *lk, const char *to)
{
    State *s = &lk->st->state;
    snprintf(s->holder, sizeof s->holder, "%s", to);
    s->phase = PH_IDLE;
    s->from[0] = s->to[0] = 0;
    s->unclaimed_base[0] = 0;
    s->wiped = 0;
    s->loaded_at = (int64_t)lk->be->now(lk->be);
    if (store_save_state(lk->st) != 0) return fail(lk, LK_ERR_IO, "Couldn't update state.json.");
    profile_touch(lk, to);
    return LK_OK;
}

static int finish_signout(Locker *lk, const char *from)
{
    State *s = &lk->st->state;
    (void)from;
    s->unclaimed_base[0] = 0; // the console no longer matches anyone's backup: it has been cleared
    s->wiped = 1;
    snprintf(s->holder, sizeof s->holder, "%s", UNCLAIMED);
    s->phase = PH_IDLE;
    s->from[0] = s->to[0] = 0;
    s->loaded_at = 0;
    if (store_save_state(lk->st) != 0) return fail(lk, LK_ERR_IO, "Couldn't update state.json.");
    return LK_OK;
}

// ---------------------------------------------------------------------------------------------------- profile folders

// A crash between "current -> history/<ts>" and "staging -> current" leaves a profile without `current`.
// The newest history entry IS the old current: put it back.
static int heal_profile(Locker *lk, const char *id)
{
    char parent[FSX_PATH], cur[FSX_PATH], hist[FSX_PATH];
    store_profile_dir(lk->st, id, parent, sizeof parent);
    fsx_join(cur, sizeof cur, parent, "current");
    if (fsx_exists(cur)) return LK_OK;
    fsx_join(hist, sizeof hist, parent, "history");
    char **names;
    size_t n;
    if (fsx_list(hist, &names, &n) != 0) return LK_OK;
    if (n > 0) {
        char src[FSX_PATH];
        fsx_join(src, sizeof src, hist, names[n - 1]);
        if (rename(src, cur) == 0) store_logf(lk->st, "healed %s: restored current from history/%s", id, names[n - 1]);
    }
    fsx_list_free(names, n);
    return LK_OK;
}

static void remove_stale_staging(const char *parent)
{
    char **names;
    size_t n;
    if (fsx_list(parent, &names, &n) != 0) return;
    for (size_t i = 0; i < n; i++) {
        if (strncmp(names[i], "staging-", 8) != 0) continue;
        char p[FSX_PATH];
        fsx_join(p, sizeof p, parent, names[i]);
        fsx_rmtree(p);
    }
    fsx_list_free(names, n);
}

static void prune_history(Locker *lk, const char *parent)
{
    char hist[FSX_PATH];
    fsx_join(hist, sizeof hist, parent, "history");
    char **names;
    size_t n;
    if (fsx_list(hist, &names, &n) != 0) return;
    int keep = lk->st->cfg.history_keep;
    for (size_t i = 0; i + (size_t)keep < n; i++) { // names are sorted oldest first
        char p[FSX_PATH];
        fsx_join(p, sizeof p, hist, names[i]);
        fsx_rmtree(p);
    }
    fsx_list_free(names, n);
}

static int new_ts(Locker *lk, char *out, size_t n)
{
    if (store_ts(lk->st, out, n) != 0) return fail(lk, LK_ERR_IO, "Couldn't read the clock.");
    return LK_OK;
}

// ---------------------------------------------------------------------------------------------------- preflight

static int preflight(Locker *lk, int check_space)
{
    int pct = 100, charging = 1;
    if (lk->be->battery(lk->be, &pct, &charging) == BE_OK && pct < BATTERY_MIN_PERCENT && !charging)
        return fail(lk, LK_ERR_BATTERY, "Battery is at %d%%. Plug in the charger.", pct);

    TRY(load_sys(lk, &lk->sys));
    uint64_t need = 0;
    lk->sys_bytes_n = 0;
    for (size_t i = 0; i < lk->sys.n; i++) {
        uint64_t t = lk->sys.v[i];
        // every save must open BEFORE anything is written; a running game holds its save open
        if (lk->be->mount(lk->be, t) != BE_OK) {
            char lbl[96];
            lk_title_label(lk, t, lbl, sizeof lbl);
            return fail(lk, LK_ERR_OPEN, "Couldn't open %s's save. Close the game first.", lbl);
        }
        lk->sys_bytes[i] = dir_has_entries(lk->be->save_root) ? fsx_tree_size(lk->be->save_root) : LK_NO_BYTES; // the progress bar's plan
        lk->be->unmount(lk->be);
        uint64_t sz = 0;
        if (lk->be->save_size(lk->be, t, &sz) == BE_OK) need += sz;
    }
    lk->sys_bytes_n = lk->sys.n;
    TRY(cp(lk, "preflight:mounted"));
    uint64_t freeb = 0;
    if (check_space && lk->be->free_space(lk->be, &freeb) == BE_OK && freeb < need + need / 5)
        return fail(lk, LK_ERR_SPACE, "Not enough free space on the SD card (%" PRIu64 " MB needed).",
                    (need + need / 5) / (1024 * 1024) + 1);
    return LK_OK;
}

// ---------------------------------------------------------------------------------------------------- backup

static int backup_title(Locker *lk, uint64_t t, const char *staging)
{
    char lbl[96], tid[17], dst[FSX_PATH];
    lk_title_label(lk, t, lbl, sizeof lbl);
    snprintf(lk->label, sizeof lk->label, "Backing up %s", lbl);
    lk->cur_title = t;
    snprintf(lk->cur_name, sizeof lk->cur_name, "%s", lbl);
    store_title_dir(t, tid);
    fsx_join(dst, sizeof dst, staging, tid);
    if (lk->be->mount(lk->be, t) != BE_OK)
        return fail(lk, LK_ERR_OPEN, "Couldn't open %s's save. Close the game first.", lbl);
    if (!dir_has_entries(lk->be->save_root)) { // never played (or cleared): nothing to keep, and no empty placeholder either
        lk->be->unmount(lk->be);
        return LK_OK;
    }
    lk->cur = 0;
    lk->total = fsx_tree_size(lk->be->save_root);
    report(lk);
    int rc = fsx_mkdir_p(dst) == 0 ? LK_OK : fail(lk, LK_ERR_IO, "Couldn't create %s.", dst);
    if (rc == LK_OK) rc = copy_tree(lk, lk->be->save_root, dst, 0);
    if (rc == LK_OK && fsx_tree_equal(lk->be->save_root, dst) != 1)
        rc = fail(lk, LK_ERR_VERIFY, "The backup of %s didn't match. Nothing was changed.", lbl);
    lk->be->unmount(lk->be);
    if (rc == LK_OK) {
        lk->swap_bytes += lk->total;
        lk->swap_titles++;
        rc = cp(lk, "backup:title");
    }
    return rc;
}

// Titles that were in the old backup but aren't on the account now (e.g. a game that is uninstalled at the moment,
// or one that was excluded after it was backed up) are carried over, so a backup never silently forgets a game.
static int carry_over(Locker *lk, const char *cur, const char *staging)
{
    if (!fsx_is_dir(cur)) return LK_OK;
    TRY(load_dir_titles(lk, cur, &lk->tgt, 1));
    for (size_t i = 0; i < lk->tgt.n; i++) {
        uint64_t t = lk->tgt.v[i];
        if (ts_has(&lk->sys, t)) continue;
        char tid[17], s[FSX_PATH], d[FSX_PATH];
        store_title_dir(t, tid);
        fsx_join(s, sizeof s, cur, tid);
        fsx_join(d, sizeof d, staging, tid);
        snprintf(lk->label, sizeof lk->label, "Keeping the saved copy of %s", tid);
        lk->cur = 0;
        lk->total = fsx_tree_size(s);
        TRY(copy_tree(lk, s, d, 0));
        if (fsx_tree_equal(s, d) != 1) return fail(lk, LK_ERR_VERIFY, "Couldn't copy the saved data for %s.", tid);
    }
    return LK_OK;
}

static int commit_profile_backup(Locker *lk, const char *parent, const char *staging, const char *ts)
{
    char cur[FSX_PATH];
    fsx_join(cur, sizeof cur, parent, "current");
    if (fsx_is_dir(cur)) {
        if (fsx_tree_equal(staging, cur) == 1) { // nothing changed: don't push a duplicate into the history
            fsx_rmtree(staging);
            return LK_OK;
        }
        char hdir[FSX_PATH], leaf[64], hist[FSX_PATH];
        fsx_join(hdir, sizeof hdir, parent, "history");
        if (fsx_mkdir_p(hdir) != 0) return fail(lk, LK_ERR_IO, "Couldn't create %s.", hdir);
        store_unique_child(hdir, ts, leaf, sizeof leaf);
        fsx_join(hist, sizeof hist, hdir, leaf);
        TRY(cp(lk, "backup:before-rotate"));
        if (rename(cur, hist) != 0) return fail(lk, LK_ERR_IO, "Couldn't move the old backup into the history.");
        TRY(cp(lk, "backup:rotated"));
    }
    if (rename(staging, cur) != 0) return fail(lk, LK_ERR_IO, "Couldn't put the new backup in place.");
    TRY(cp(lk, "backup:committed"));
    prune_history(lk, parent);
    return LK_OK;
}

// true if every title folder in `staging` is identical to the same folder in `base`
static int staging_covered_by(const char *staging, const char *base)
{
    char **names;
    size_t n;
    if (fsx_list(staging, &names, &n) != 0) return 0;
    int covered = 1;
    for (size_t i = 0; i < n && covered; i++) {
        char a[FSX_PATH], b[FSX_PATH];
        fsx_join(a, sizeof a, staging, names[i]);
        fsx_join(b, sizeof b, base, names[i]);
        covered = fsx_is_dir(b) && fsx_tree_equal(a, b) == 1;
    }
    fsx_list_free(names, n);
    return covered;
}

static int commit_unclaimed_backup(Locker *lk, const char *parent, const char *staging, const char *ts)
{
    const char *base = lk->st->state.unclaimed_base;
    if (base[0]) {
        char bdir[FSX_PATH], bcur[FSX_PATH];
        store_profile_dir(lk->st, base, bdir, sizeof bdir);
        fsx_join(bcur, sizeof bcur, bdir, "current");
        if (fsx_is_dir(bcur) && staging_covered_by(staging, bcur)) { // nobody played since the sign-out
            fsx_rmtree(staging);
            return LK_OK;
        }
    }
    // identical to the newest unclaimed copy (e.g. this backup is being redone after an interruption): keep just one
    {
        char **names;
        size_t n;
        int same = 0;
        if (fsx_list(parent, &names, &n) == 0) {
            for (size_t i = n; i > 0 && !same; i--) {
                if (!strncmp(names[i - 1], "staging-", 8)) continue;
                char newest[FSX_PATH];
                fsx_join(newest, sizeof newest, parent, names[i - 1]);
                same = fsx_tree_equal(staging, newest) == 1;
                break;
            }
            fsx_list_free(names, n);
        }
        if (same) {
            fsx_rmtree(staging);
            return LK_OK;
        }
    }
    char leaf[64], dst[FSX_PATH];
    store_unique_child(parent, ts, leaf, sizeof leaf);
    fsx_join(dst, sizeof dst, parent, leaf);
    TRY(cp(lk, "backup:before-unclaimed"));
    if (rename(staging, dst) != 0) return fail(lk, LK_ERR_IO, "Couldn't save the unclaimed backup.");
    TRY(cp(lk, "backup:committed"));
    lk->unclaimed_saved = 1;
    store_logf(lk->st, "someone played without the app: saves kept in unclaimed/%s", leaf);
    return LK_OK;
}

// The progress bar's plan for a phase: the byte size of every game the phase will report, in the order it reports them.
static void plan_begin(Locker *lk)
{
    lk->plan_n = 0;
    lk->plan_serial++;
}

static void plan_add(Locker *lk, uint64_t bytes)
{
    if (lk->plan_n < LK_MAX_TITLES) lk->plan[lk->plan_n++] = bytes;
}

// the games on the account that have something in them (backing up and clearing report exactly those), sized as preflight read them
static void plan_from_sys(Locker *lk)
{
    plan_begin(lk);
    if (lk->sys_bytes_n != lk->sys.n) return;
    for (size_t i = 0; i < lk->sys.n; i++)
        if (lk->sys_bytes[i] != LK_NO_BYTES) plan_add(lk, lk->sys_bytes[i]);
}

// Back up everything on the account into `from` (a profile id, or UNCLAIMED), verified, before anything else happens.
static int do_backup(Locker *lk, const char *from)
{
    lk->phase = 1;
    int is_profile = strcmp(from, UNCLAIMED) != 0;
    char parent[FSX_PATH];
    if (is_profile) {
        TRY(heal_profile(lk, from));
        store_profile_dir(lk->st, from, parent, sizeof parent);
    } else {
        store_path(lk->st, parent, sizeof parent, "unclaimed");
    }
    if (fsx_mkdir_p(parent) != 0) return fail(lk, LK_ERR_IO, "Couldn't create %s.", parent);
    remove_stale_staging(parent);

    TRY(load_sys(lk, &lk->sys));
    if (lk->sys.n == 0) return LK_OK; // nothing on the account
    plan_from_sys(lk);

    char ts[40], leaf[64], staging[FSX_PATH];
    TRY(new_ts(lk, ts, sizeof ts));
    snprintf(leaf, sizeof leaf, "staging-%s", ts);
    fsx_join(staging, sizeof staging, parent, leaf);
    if (fsx_mkdir_p(staging) != 0) return fail(lk, LK_ERR_IO, "Couldn't create %s.", staging);

    TitleSet *sys = &lk->sys;
    for (size_t i = 0; i < sys->n; i++) TRY(backup_title(lk, sys->v[i], staging));
    if (is_profile) {
        char cur[FSX_PATH];
        fsx_join(cur, sizeof cur, parent, "current");
        TRY(carry_over(lk, cur, staging));
    }
    TRY(cp(lk, "backup:staged"));
    return is_profile ? commit_profile_backup(lk, parent, staging, ts) : commit_unclaimed_backup(lk, parent, staging, ts);
}

// ---------------------------------------------------------------------------------------------------- restore

static int restore_title(Locker *lk, uint64_t t, int in_sys, int in_bak, const char *cur_dir)
{
    char lbl[96], tid[17], src[FSX_PATH];
    lk_title_label(lk, t, lbl, sizeof lbl);
    snprintf(lk->label, sizeof lk->label, "Loading %s", lbl);
    lk->cur_title = t;
    snprintf(lk->cur_name, sizeof lk->cur_name, "%s", lbl);
    store_title_dir(t, tid);
    fsx_join(src, sizeof src, cur_dir, tid);

    if (!in_sys) { // has a backup but no save on the account yet: create it from the game's NACP
        int rc = lk->be->create(lk->be, t);
        if (rc == BE_ERR_NOT_INSTALLED || rc == BE_ERR_NO_ACCOUNT_SAVE) {
            warn(lk, "%s isn't installed; skipped.", lbl);
            lk->cur = lk->total = 0;
            report(lk); // (the progress bar's plan has a piece for it: it is shown as having nothing to copy)
            return LK_OK;
        }
        if (rc != BE_OK) return fail(lk, LK_ERR_BACKEND, "Couldn't create the save for %s.", lbl);
        TRY(cp(lk, "restore:created"));
    }
    if (lk->be->mount(lk->be, t) != BE_OK)
        return fail(lk, LK_ERR_OPEN, "Couldn't open %s's save. Close the game first.", lbl);

    lk->journal_limit = journal_limit_for(lk, t);
    lk->uncommitted = 0;
    lk->cur = 0;
    lk->total = in_bak ? fsx_tree_size(src) : 0;
    report(lk);

    int rc = empty_save(lk);
    if (rc == LK_OK && lk->be->commit(lk->be) != BE_OK) rc = fail(lk, LK_ERR_BACKEND, "Couldn't commit the save for %s.", lbl);
    if (rc == LK_OK && in_bak) {
        rc = copy_tree(lk, src, lk->be->save_root, 1);
        if (rc == LK_OK && lk->be->commit(lk->be) != BE_OK)
            rc = fail(lk, LK_ERR_BACKEND, "Couldn't commit the save for %s.", lbl);
        if (rc == LK_OK && fsx_tree_equal(src, lk->be->save_root) != 1)
            rc = fail(lk, LK_ERR_VERIFY, "The loaded save for %s didn't match its backup.", lbl);
    }
    lk->be->unmount(lk->be);
    if (rc == LK_OK) rc = cp(lk, "restore:title");
    return rc;
}

static int do_restore(Locker *lk, const char *target)
{
    lk->phase = 2;
    TRY(heal_profile(lk, target));
    char pdir[FSX_PATH], cur[FSX_PATH];
    store_profile_dir(lk->st, target, pdir, sizeof pdir);
    fsx_join(cur, sizeof cur, pdir, "current");

    TRY(load_sys(lk, &lk->sys));
    lk->tgt.n = 0;
    if (fsx_is_dir(cur)) TRY(load_dir_titles(lk, cur, &lk->tgt, 0));

    lk->uni.n = 0;
    for (size_t i = 0; i < lk->sys.n; i++) ts_add(&lk->uni, lk->sys.v[i]);
    for (size_t i = 0; i < lk->tgt.n; i++) ts_add(&lk->uni, lk->tgt.v[i]);
    ts_sort(&lk->uni);

    plan_begin(lk); // every game is reported: the size of its backup, or nothing when it is only being cleared
    for (size_t i = 0; i < lk->uni.n; i++) {
        char tid[17], src[FSX_PATH];
        store_title_dir(lk->uni.v[i], tid);
        plan_add(lk, ts_has(&lk->tgt, lk->uni.v[i]) && fsx_join(src, sizeof src, cur, tid) == 0 ? fsx_tree_size(src) : 0);
    }
    for (size_t i = 0; i < lk->uni.n; i++) {
        uint64_t t = lk->uni.v[i];
        TRY(restore_title(lk, t, ts_has(&lk->sys, t), ts_has(&lk->tgt, t), cur));
    }
    return LK_OK;
}

// ---------------------------------------------------------------------------------------------------- clearing the console

// Empties every save we manage (the "nobody is playing" state: whatever happens next is a temporary guest session).
// This destroys data, so it only ever runs for a holder whose backup is already in place, and it checks that first: every save
// that has anything in it must be identical to the same game in the holder's backup, or nothing is cleared.
static int do_wipe(Locker *lk, const char *backed_up_as)
{
    lk->phase = 3;
    char cur[FSX_PATH];
    int check = strcmp(backed_up_as, UNCLAIMED) != 0;
    if (check) {
        char pdir[FSX_PATH];
        store_profile_dir(lk->st, backed_up_as, pdir, sizeof pdir);
        fsx_join(cur, sizeof cur, pdir, "current");
    }
    TRY(load_sys(lk, &lk->sys));
    plan_from_sys(lk);
    for (size_t i = 0; i < lk->sys.n; i++) {
        uint64_t t = lk->sys.v[i];
        char lbl[96], tid[17], bak[FSX_PATH];
        lk_title_label(lk, t, lbl, sizeof lbl);
        snprintf(lk->label, sizeof lk->label, "Clearing %s", lbl);
        lk->cur_title = t;
        snprintf(lk->cur_name, sizeof lk->cur_name, "%s", lbl);
        if (lk->be->mount(lk->be, t) != BE_OK)
            return fail(lk, LK_ERR_OPEN, "Couldn't open %s's save. Close the game first.", lbl);
        int rc = LK_OK;
        if (dir_has_entries(lk->be->save_root)) {
            store_title_dir(t, tid);
            if (check && (fsx_join(bak, sizeof bak, cur, tid) != 0 || fsx_tree_equal(lk->be->save_root, bak) != 1))
                rc = fail(lk, LK_ERR_VERIFY, "The backup of %s wasn't complete, so nothing was cleared.", lbl);
            if (rc == LK_OK) {
                lk->journal_limit = journal_limit_for(lk, t);
                lk->uncommitted = 0;
                lk->cur = lk->total = 0;
                report(lk);
                rc = empty_save(lk);
                if (rc == LK_OK && lk->be->commit(lk->be) != BE_OK) rc = fail(lk, LK_ERR_BACKEND, "Couldn't commit the cleared save for %s.", lbl);
            }
        }
        lk->be->unmount(lk->be);
        if (rc != LK_OK) return rc;
        TRY(cp(lk, "wipe:title"));
    }
    return LK_OK;
}

// ---------------------------------------------------------------------------------------------------- operations

static void log_result(Locker *lk, const char *what, const char *from, const char *to, int rc)
{
    store_logf(lk->st, "%s: %s -> %s; games=%d bytes=%" PRIu64 " warnings=%d; %s%s%s", what, from[0] ? from : "-",
               to[0] ? to : "-", lk->swap_titles, lk->swap_bytes, lk->nwarn, rc == LK_OK ? "ok" : "FAILED",
               rc == LK_OK ? "" : ": ", rc == LK_OK ? "" : lk->err);
}

int lk_swap_to(Locker *lk, const char *target)
{
    reset(lk);
    if (!lk->st->cfg.target_uid[0]) return fail(lk, LK_ERR_NO_TARGET, "No account has been chosen yet (Admin).");
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    Profile p;
    if (store_profile_load(lk->st, target, &p) != 0) return fail(lk, LK_ERR_NO_PROFILE, "That profile doesn't exist.");
    if (!strcmp(lk->st->state.holder, target)) return LK_NOOP;

    char from[ID_MAX];
    snprintf(from, sizeof from, "%s", lk->st->state.holder);

    // After a sign-out the console was cleared, so anything on it now is a guest's temporary progress: it is thrown away.
    // (The first time, or after anything unexpected, the saves on the console are of unknown origin and are kept in unclaimed/.)
    int guest = !strcmp(from, UNCLAIMED) && lk->st->state.wiped;
    int rc = preflight(lk, !guest);
    if (rc == LK_OK && !guest) rc = journal(lk, PH_BACKING_UP, from, target);
    if (rc == LK_OK && !guest) rc = do_backup(lk, from);
    if (rc == LK_OK && !guest) rc = cp(lk, "swap:backed-up");
    if (rc == LK_OK) rc = journal(lk, PH_RESTORING, from, target);
    if (rc == LK_OK) rc = do_restore(lk, target);
    if (rc == LK_OK) rc = cp(lk, "swap:restored");
    if (rc == LK_OK) rc = finish_swap(lk, target);
    log_result(lk, "swap", from, target, rc);
    return rc;
}

int lk_sign_out(Locker *lk)
{
    reset(lk);
    if (!lk->st->cfg.target_uid[0]) return fail(lk, LK_ERR_NO_TARGET, "No account has been chosen yet (Admin).");
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    if (!strcmp(lk->st->state.holder, UNCLAIMED)) return LK_NOOP;

    char from[ID_MAX];
    snprintf(from, sizeof from, "%s", lk->st->state.holder);

    int rc = preflight(lk, 1);
    if (rc == LK_OK) rc = journal(lk, PH_BACKING_UP, from, "");
    if (rc == LK_OK) rc = do_backup(lk, from);
    if (rc == LK_OK) rc = cp(lk, "signout:backed-up");
    if (rc == LK_OK) rc = journal(lk, PH_WIPING, from, "");
    if (rc == LK_OK) rc = do_wipe(lk, from);
    if (rc == LK_OK) rc = cp(lk, "signout:wiped");
    if (rc == LK_OK) rc = finish_signout(lk, from);
    log_result(lk, "sign-out", from, "", rc);
    return rc;
}

int lk_claim_guest(Locker *lk, const char *id)
{
    reset(lk);
    if (!lk->st->cfg.target_uid[0]) return fail(lk, LK_ERR_NO_TARGET, "No account has been chosen yet (Admin).");
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    Profile p;
    if (store_profile_load(lk->st, id, &p) != 0) return fail(lk, LK_ERR_NO_PROFILE, "That profile doesn't exist.");
    if (!strcmp(lk->st->state.holder, id)) return LK_NOOP;
    if (strcmp(lk->st->state.holder, UNCLAIMED) || !lk->st->state.wiped)
        return fail(lk, LK_ERR_BUSY_STATE, "Nobody is playing as a guest, so there is nothing to keep.");

    // The journal says "backing up into it, then loading it": after a power cut, recovery backs the console up into the profile
    // again and loads that same backup, which leaves the console as it is.
    int rc = preflight(lk, 1);
    if (rc == LK_OK) rc = journal(lk, PH_BACKING_UP, id, id);
    if (rc == LK_OK) rc = do_backup(lk, id);
    if (rc == LK_OK) rc = cp(lk, "claim:backed-up");
    if (rc == LK_OK) rc = finish_swap(lk, id);
    log_result(lk, "keep guest play", UNCLAIMED, id, rc);
    return rc;
}

int lk_played_titles(Locker *lk)
{
    if (load_sys(lk, &lk->sys) != LK_OK) return 0;
    int n = 0;
    for (size_t i = 0; i < lk->sys.n; i++) {
        if (lk->be->mount(lk->be, lk->sys.v[i]) != BE_OK) continue;
        n += dir_has_entries(lk->be->save_root);
        lk->be->unmount(lk->be);
    }
    return n;
}

int lk_recover(Locker *lk)
{
    reset(lk);
    State *s = &lk->st->state;
    if (s->phase == PH_IDLE) return LK_NOOP;
    char from[ID_MAX], to[ID_MAX];
    snprintf(from, sizeof from, "%s", s->from);
    snprintf(to, sizeof to, "%s", s->to);
    Phase phase = s->phase;

    int rc = preflight(lk, phase == PH_BACKING_UP);
    if (rc == LK_OK && phase == PH_BACKING_UP) {
        // the account's saves are untouched while backing up: throw the half-made staging copy away and redo it
        rc = do_backup(lk, from);
        if (rc == LK_OK) rc = cp(lk, "recover:backed-up");
        if (rc == LK_OK && !to[0]) {
            rc = journal(lk, PH_WIPING, from, "");
            phase = PH_WIPING;
        }
        if (rc == LK_OK && to[0]) rc = journal(lk, PH_RESTORING, from, to);
    }
    if (rc == LK_OK && phase == PH_WIPING) {
        // the holder's backup was finished before the journal said "wiping": clear the console again (it can be done twice)
        rc = do_wipe(lk, from);
        if (rc == LK_OK) rc = cp(lk, "recover:wiped");
        if (rc == LK_OK) rc = finish_signout(lk, from);
    }
    if (rc == LK_OK && to[0]) {
        // the target's backup is only ever read while restoring, so it's intact: redo the whole restore
        rc = do_restore(lk, to);
        if (rc == LK_OK) rc = cp(lk, "recover:restored");
        if (rc == LK_OK) rc = finish_swap(lk, to);
    }
    log_result(lk, "recovery", from, to, rc);
    return rc;
}

static void restore_heal(Locker *lk);

int lk_startup(Locker *lk)
{
    reset(lk);
    restore_heal(lk);
    Profile *list = NULL;
    size_t n = 0;
    if (store_profile_list(lk->st, &list, &n) != 0) return -1;
    for (size_t i = 0; i < n; i++) heal_profile(lk, list[i].id);
    free(list);
    return 0;
}

int lk_rollback(Locker *lk, const char *id, const char *hist_name)
{
    reset(lk);
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    Profile p;
    if (store_profile_load(lk->st, id, &p) != 0) return fail(lk, LK_ERR_NO_PROFILE, "That profile doesn't exist.");
    if (!strcmp(lk->st->state.holder, id))
        return fail(lk, LK_ERR_IS_HOLDER, "%s's saves are loaded. Sign out first.", p.name);
    if (!hist_name[0] || strchr(hist_name, '/') || hist_name[0] == '.') return fail(lk, LK_ERR_IO, "Bad history entry.");

    TRY(heal_profile(lk, id));
    char parent[FSX_PATH], hdir[FSX_PATH], hsrc[FSX_PATH], cur[FSX_PATH];
    store_profile_dir(lk->st, id, parent, sizeof parent);
    fsx_join(hdir, sizeof hdir, parent, "history");
    fsx_join(hsrc, sizeof hsrc, hdir, hist_name);
    fsx_join(cur, sizeof cur, parent, "current");
    if (!fsx_is_dir(hsrc)) return fail(lk, LK_ERR_IO, "That history entry doesn't exist.");

    remove_stale_staging(parent);
    char ts[40], leaf[64], staging[FSX_PATH];
    TRY(new_ts(lk, ts, sizeof ts));
    snprintf(leaf, sizeof leaf, "staging-%s", ts);
    fsx_join(staging, sizeof staging, parent, leaf);

    snprintf(lk->label, sizeof lk->label, "Preparing %s's older saves", p.name);
    lk->cur_title = 0;
    snprintf(lk->cur_name, sizeof lk->cur_name, "%s's earlier saves", p.name);
    lk->cur = 0;
    lk->total = fsx_tree_size(hsrc);
    int rc = copy_tree(lk, hsrc, staging, 0);
    if (rc == LK_OK && fsx_tree_equal(hsrc, staging) != 1) rc = fail(lk, LK_ERR_VERIFY, "The copy didn't match; nothing was changed.");
    if (rc == LK_OK) rc = cp(lk, "rollback:staged");
    if (rc == LK_OK && fsx_is_dir(cur)) {
        char hleaf[64], hnew[FSX_PATH];
        store_unique_child(hdir, ts, hleaf, sizeof hleaf);
        fsx_join(hnew, sizeof hnew, hdir, hleaf);
        if (rename(cur, hnew) != 0) rc = fail(lk, LK_ERR_IO, "Couldn't keep the current saves in the history.");
        if (rc == LK_OK) rc = cp(lk, "rollback:rotated");
    }
    if (rc == LK_OK && rename(staging, cur) != 0) rc = fail(lk, LK_ERR_IO, "Couldn't put the older saves in place.");
    if (rc == LK_OK) rc = cp(lk, "rollback:committed");
    if (rc == LK_OK) prune_history(lk, parent);
    store_logf(lk->st, "rollback: %s to history/%s; %s%s%s", id, hist_name, rc == LK_OK ? "ok" : "FAILED",
               rc == LK_OK ? "" : ": ", rc == LK_OK ? "" : lk->err);
    return rc;
}

int lk_delete_profile(Locker *lk, const char *id)
{
    reset(lk);
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    Profile p;
    if (store_profile_load(lk->st, id, &p) != 0) return fail(lk, LK_ERR_NO_PROFILE, "That profile doesn't exist.");
    if (!strcmp(lk->st->state.holder, id))
        return fail(lk, LK_ERR_IS_HOLDER, "%s's saves are loaded. Sign out first.", p.name);
    if (store_profile_trash(lk->st, id) != 0) return fail(lk, LK_ERR_IO, "Couldn't move the profile to the trash.");
    if (!strcmp(lk->st->state.unclaimed_base, id)) {
        lk->st->state.unclaimed_base[0] = 0;
        store_save_state(lk->st);
    }
    return LK_OK;
}

int lk_set_target(Locker *lk, const char *uid_hex)
{
    reset(lk);
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    if (strcmp(lk->st->state.holder, UNCLAIMED) != 0 && lk->st->cfg.target_uid[0] && strcmp(lk->st->cfg.target_uid, uid_hex) != 0)
        return fail(lk, LK_ERR_IS_HOLDER, "Someone's saves are loaded. Sign out first.");
    snprintf(lk->st->cfg.target_uid, sizeof lk->st->cfg.target_uid, "%s", uid_hex);
    lk->be->set_target(lk->be, uid_hex);
    lk->st->state.unclaimed_base[0] = 0;
    lk->st->state.wiped = 0; // a different account: nothing is known about what is on it
    store_save_state(lk->st);
    if (store_save_config(lk->st) != 0) return fail(lk, LK_ERR_IO, "Couldn't save config.json.");
    return LK_OK;
}

// ---------------------------------------------------------------------------------------------------- queries

int lk_profile_summary(Locker *lk, const char *id, int *ngames, int64_t *last_used)
{
    Profile p;
    if (store_profile_load(lk->st, id, &p) != 0) return -1;
    if (last_used) *last_used = p.last_used;
    int n = 0;
    char dir[FSX_PATH], cur[FSX_PATH];
    store_profile_dir(lk->st, id, dir, sizeof dir);
    fsx_join(cur, sizeof cur, dir, "current");
    if (fsx_is_dir(cur)) {
        if (load_dir_titles(lk, cur, &lk->tgt, 0) == LK_OK) n = (int)lk->tgt.n;
    }
    if (ngames) *ngames = n;
    return 0;
}

int lk_history_list(Locker *lk, const char *id, char ***names, size_t *n)
{
    char dir[FSX_PATH], hist[FSX_PATH];
    store_profile_dir(lk->st, id, dir, sizeof dir);
    fsx_join(hist, sizeof hist, dir, "history");
    if (!fsx_is_dir(hist)) {
        *names = NULL;
        *n = 0;
        return 0;
    }
    return fsx_list(hist, names, n);
}

// The same rule as prune_history: an entry's name is when it was made, so the names sort oldest first, and the newest `keep`
// stay. At least one always stays: after an interrupted roll back a profile's newest entry can be its only copy (heal_profile).
// ---------------------------------------------------------------------------------------------------- unclaimed saves

static int cmp_unclaimed(const void *a, const void *b)
{
    const LkUnclaimed *x = a, *y = b;
    return strcmp(y->leaf, x->leaf); // (their names are their times: the newest first)
}

int lk_unclaimed_list(Locker *lk, LkUnclaimed **out, size_t *n)
{
    *out = NULL;
    *n = 0;
    char dir[FSX_PATH], **names;
    size_t cnt;
    store_path(lk->st, dir, sizeof dir, "unclaimed");
    if (!fsx_is_dir(dir)) return 0;
    if (fsx_list(dir, &names, &cnt) != 0) return -1;
    LkUnclaimed *u = calloc(cnt ? cnt : 1, sizeof *u);
    if (!u) {
        fsx_list_free(names, cnt);
        return -1;
    }
    size_t k = 0;
    for (size_t i = 0; i < cnt; i++) {
        char p[FSX_PATH], **t;
        size_t nt;
        if (!strncmp(names[i], "staging-", 8) || fsx_join(p, sizeof p, dir, names[i]) != 0 || !fsx_is_dir(p)) continue;
        LkUnclaimed *e = &u[k++];
        snprintf(e->leaf, sizeof e->leaf, "%s", names[i]);
        e->when = store_parse_ts(names[i]);
        e->bytes = fsx_tree_size(p);
        if (fsx_list(p, &t, &nt) == 0) {
            for (size_t j = 0; j < nt; j++) {
                uint64_t title;
                if (store_parse_title(t[j], &title) != 0) continue;
                if (e->ngames < LK_UNCLAIMED_TITLES) e->titles[e->ngames] = title;
                e->ngames++;
            }
            fsx_list_free(t, nt);
        }
    }
    fsx_list_free(names, cnt);
    if (k) qsort(u, k, sizeof *u, cmp_unclaimed);
    *out = u;
    *n = k;
    return 0;
}

static int unclaimed_dir(Locker *lk, const char *leaf, char *out, size_t n)
{
    if (!leaf || !leaf[0] || strchr(leaf, '/') || leaf[0] == '.') return fail(lk, LK_ERR_IO, "Bad unclaimed entry.");
    char dir[FSX_PATH];
    store_path(lk->st, dir, sizeof dir, "unclaimed");
    if (fsx_join(out, n, dir, leaf) != 0 || !fsx_is_dir(out)) return fail(lk, LK_ERR_IO, "Those saves aren't there any more.");
    return LK_OK;
}

// Takes an entry out of unclaimed/ in one step (a rename) and then deletes it, so a power cut can't leave half of one behind to be
// given to someone later. What is left of a "staging-" folder is cleared the next time (remove_stale_staging).
static int unclaimed_remove(Locker *lk, const char *src, const char *leaf)
{
    char dir[FSX_PATH], name[96], gone[FSX_PATH];
    store_path(lk->st, dir, sizeof dir, "unclaimed");
    snprintf(name, sizeof name, "staging-gone-%s", leaf);
    if (fsx_join(gone, sizeof gone, dir, name) != 0) return -1;
    if (fsx_exists(gone)) fsx_rmtree(gone);
    if (rename(src, gone) != 0) return -1;
    fsx_rmtree(gone);
    return 0;
}

static void unclaimed_tidy(Locker *lk)
{
    char dir[FSX_PATH];
    store_path(lk->st, dir, sizeof dir, "unclaimed");
    if (fsx_is_dir(dir)) remove_stale_staging(dir);
}

int lk_unclaimed_give(Locker *lk, const char *leaf, const char *id)
{
    reset(lk);
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    Profile p;
    if (store_profile_load(lk->st, id, &p) != 0) return fail(lk, LK_ERR_NO_PROFILE, "That profile doesn't exist.");
    if (!strcmp(lk->st->state.holder, id))
        return fail(lk, LK_ERR_IS_HOLDER, "%s's saves are loaded. Sign out first.", p.name);
    char src[FSX_PATH];
    unclaimed_tidy(lk);
    TRY(unclaimed_dir(lk, leaf, src, sizeof src));

    TRY(heal_profile(lk, id));
    char parent[FSX_PATH], cur[FSX_PATH], ts[40], sleaf[64], staging[FSX_PATH];
    store_profile_dir(lk->st, id, parent, sizeof parent);
    fsx_join(cur, sizeof cur, parent, "current");
    if (fsx_mkdir_p(parent) != 0) return fail(lk, LK_ERR_IO, "Couldn't create %s.", parent);
    remove_stale_staging(parent);
    TRY(new_ts(lk, ts, sizeof ts));
    snprintf(sleaf, sizeof sleaf, "staging-%s", ts);
    fsx_join(staging, sizeof staging, parent, sleaf);

    // its games, then the profile's own games that aren't among them (as a backup carries over games that aren't on the console)
    snprintf(lk->label, sizeof lk->label, "Copying the saves to %s", p.name);
    lk->cur_title = 0;
    snprintf(lk->cur_name, sizeof lk->cur_name, "Unclaimed saves");
    lk->cur = 0;
    lk->total = fsx_tree_size(src);
    int rc = copy_tree(lk, src, staging, 0);
    if (rc == LK_OK && fsx_tree_equal(src, staging) != 1) rc = fail(lk, LK_ERR_VERIFY, "The copy didn't match; nothing was changed.");
    if (rc == LK_OK) rc = load_dir_titles(lk, staging, &lk->sys, 1);
    if (rc == LK_OK) rc = carry_over(lk, cur, staging);
    if (rc == LK_OK) rc = cp(lk, "give:staged");
    if (rc == LK_OK) rc = commit_profile_backup(lk, parent, staging, ts);
    // (stopped before here, it is still in unclaimed/ and can be given again: the same saves, so nothing is added twice)
    if (rc == LK_OK && unclaimed_remove(lk, src, leaf) != 0)
        warn(lk, "The saves are %s's now, but their old copy couldn't be removed from unclaimed/.", p.name);
    store_logf(lk->st, "unclaimed/%s given to %s; %s%s%s", leaf, id, rc == LK_OK ? "ok" : "FAILED", rc == LK_OK ? "" : ": ",
               rc == LK_OK ? "" : lk->err);
    return rc;
}

int lk_unclaimed_delete(Locker *lk, const char *leaf)
{
    reset(lk);
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    char src[FSX_PATH];
    unclaimed_tidy(lk);
    TRY(unclaimed_dir(lk, leaf, src, sizeof src));
    if (unclaimed_remove(lk, src, leaf) != 0) return fail(lk, LK_ERR_IO, "Couldn't delete them. Is the SD card locked?");
    store_logf(lk->st, "unclaimed/%s deleted", leaf);
    return LK_OK;
}

int lk_history_trim(Locker *lk, int keep, int dry_run, uint64_t *bytes, int *entries)
{
    lk->err[0] = 0;
    if (bytes) *bytes = 0;
    if (entries) *entries = 0;
    if (keep < 1) keep = 1;
    if (!dry_run && lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    Profile *list = NULL;
    size_t n = 0;
    if (store_profile_list(lk->st, &list, &n) != 0) return fail(lk, LK_ERR_IO, "Couldn't read the profiles.");
    uint64_t total = 0;
    int count = 0, rc = LK_OK;
    for (size_t i = 0; i < n && rc == LK_OK; i++) {
        char dir[FSX_PATH], hist[FSX_PATH], **names;
        size_t cnt = 0;
        store_profile_dir(lk->st, list[i].id, dir, sizeof dir);
        if (fsx_join(hist, sizeof hist, dir, "history") != 0 || !fsx_is_dir(hist) || fsx_list(hist, &names, &cnt) != 0) continue;
        uint64_t freed = 0;
        int removed = 0;
        for (size_t k = 0; k + (size_t)keep < cnt; k++) {
            char p[FSX_PATH];
            if (fsx_join(p, sizeof p, hist, names[k]) != 0) continue;
            uint64_t size = fsx_tree_size(p);
            if (!dry_run && fsx_rmtree(p) != 0) {
                rc = fail(lk, LK_ERR_IO, "Couldn't delete an earlier save of %s.", list[i].name);
                break;
            }
            freed += size;
            removed++;
        }
        fsx_list_free(names, cnt);
        if (!dry_run && removed)
            store_logf(lk->st, "earlier saves trimmed: %s, %d deleted (%" PRIu64 " bytes), the newest %d kept", list[i].id, removed, freed, keep);
        total += freed;
        count += removed;
    }
    free(list);
    if (bytes) *bytes = total;
    if (entries) *entries = count;
    return rc;
}

int lk_default_account(Locker *lk, BeAccount *out)
{
    BeAccount acc[8];
    size_t n = 0;
    if (lk->be->list_accounts(lk->be, acc, 8, &n) != BE_OK) return 0;
    int found = 0;
    for (size_t i = 0; i < n; i++) {
        if (strcasecmp(acc[i].name, "Esports Switch") == 0) {
            *out = acc[i];
            found++;
        }
    }
    return found == 1;
}

// ---------------------------------------------------------------------------------------------------- snapshot

// Stale half-made snapshots (a copy that was cut short) are named "<leaf>.partial"; the next snapshot clears them.
static void snapshot_tidy(const char *parent)
{
    char **names;
    size_t n;
    if (fsx_list(parent, &names, &n) != 0) return;
    for (size_t i = 0; i < n; i++) {
        size_t len = strlen(names[i]);
        char p[FSX_PATH];
        if (len > 8 && !strcmp(names[i] + len - 8, ".partial") && fsx_join(p, sizeof p, parent, names[i]) == 0) fsx_rmtree(p);
    }
    fsx_list_free(names, n);
}

// What a backup of everything leaves out: the Extra icons (they come from GitHub again, and are by far the biggest thing in the folder), the logs (they
// describe this console now, and one is being written) and the in-game reminder's log (the reminder writes it whenever it likes: a line landing between the
// copy and the check would throw the whole backup away).
static int snapshot_skips(const char *name)
{
    return !strcmp(name, "pictures") || !strcmp(name, "logs") || !strcmp(name, "reminder.log");
}

static uint64_t snapshot_size(const char *root)
{
    char **names;
    size_t n;
    uint64_t total = 0;
    if (fsx_list(root, &names, &n) != 0) return 0;
    for (size_t i = 0; i < n; i++) {
        char p[FSX_PATH];
        if (!snapshot_skips(names[i]) && fsx_join(p, sizeof p, root, names[i]) == 0) total += fsx_tree_size(p);
    }
    fsx_list_free(names, n);
    return total;
}

static int snapshot_copy(Locker *lk, const char *root, const char *tmp)
{
    mkdir(tmp, 0777);
    char **names;
    size_t n;
    if (fsx_list(root, &names, &n) != 0) return fail(lk, LK_ERR_IO, "Couldn't read %s.", root);
    int rc = LK_OK;
    for (size_t i = 0; i < n && rc == LK_OK; i++) {
        char s[FSX_PATH], d[FSX_PATH];
        if (snapshot_skips(names[i])) continue;
        if (fsx_join(s, sizeof s, root, names[i]) || fsx_join(d, sizeof d, tmp, names[i])) {
            rc = fail(lk, LK_ERR_IO, "Path too long: %s", names[i]);
            break;
        }
        rc = fsx_is_dir(s) ? copy_tree(lk, s, d, 0) : copy_file(lk, s, d, 0);
    }
    fsx_list_free(names, n);
    return rc;
}

// how many files there are under dir (-1: it couldn't be read)
static long count_files(const char *dir)
{
    char **names;
    size_t n;
    if (fsx_list(dir, &names, &n) != 0) return -1;
    long total = 0;
    for (size_t i = 0; i < n && total >= 0; i++) {
        char p[FSX_PATH];
        if (fsx_join(p, sizeof p, dir, names[i]) != 0) {
            total = -1;
            break;
        }
        if (fsx_is_dir(p)) {
            long k = count_files(p);
            total = k < 0 ? -1 : total + k;
        } else {
            total++;
        }
    }
    fsx_list_free(names, n);
    return total;
}

// 1 if every file of the copy reads back as what was read to make it (same size, same CRC), and the copy has no file more
static int snapshot_check(const LkManifest *m, const char *tmp)
{
    if (m->oom) return 0;
    for (size_t i = 0; i < m->n; i++) {
        uint32_t c;
        uint64_t s;
        if (fsx_file_crc(m->v[i].path, &c, &s) != 0 || c != m->v[i].crc || s != m->v[i].size) return 0;
    }
    return count_files(tmp) == (long)m->n;
}

int lk_snapshot(Locker *lk, const char *parent, const char *leaf, char *out, size_t outn)
{
    reset(lk);
    if (out && outn) out[0] = 0;
    const char *root = lk->st->root;
    uint64_t need = snapshot_size(root), freeb = 0;
    if (lk->be->free_space(lk->be, &freeb) == BE_OK && freeb < need + need / 5 + 8 * 1024 * 1024)
        return fail(lk, LK_ERR_SPACE, "Not enough free space on the SD card (%" PRIu64 " MB needed).",
                    (need + need / 5) / (1024 * 1024) + 9);
    if (fsx_mkdir_p(parent) != 0) return fail(lk, LK_ERR_IO, "Couldn't create %s.", parent);
    snapshot_tidy(parent);

    char name[96], tmpname[112], dst[FSX_PATH], tmp[FSX_PATH];
    if (store_unique_child(parent, leaf, name, sizeof name) != 0) return fail(lk, LK_ERR_IO, "Couldn't pick a name for the backup.");
    snprintf(tmpname, sizeof tmpname, "%s.partial", name);
    if (fsx_join(dst, sizeof dst, parent, name) || fsx_join(tmp, sizeof tmp, parent, tmpname))
        return fail(lk, LK_ERR_IO, "The backup folder's path is too long.");

    snprintf(lk->label, sizeof lk->label, "Backing up everything");
    lk->cur_title = 0;
    snprintf(lk->cur_name, sizeof lk->cur_name, "Washi");
    lk->cur = 0;
    lk->total = need;
    LkManifest man;
    memset(&man, 0, sizeof man);
    lk->man = &man;
    int rc = snapshot_copy(lk, root, tmp);
    lk->man = NULL;
    if (rc == LK_OK && !snapshot_check(&man, tmp)) rc = fail(lk, LK_ERR_VERIFY, "The copy didn't match, so it was thrown away.");
    man_free(&man);
    if (rc == LK_OK && rename(tmp, dst) != 0) rc = fail(lk, LK_ERR_IO, "Couldn't finish the backup folder.");
    if (rc != LK_OK) fsx_rmtree(tmp);
    else if (out && outn) snprintf(out, outn, "%s", dst);
    store_logf(lk->st, "snapshot %s: %s%s%s", name, rc == LK_OK ? "ok" : "FAILED", rc == LK_OK ? "" : ": ", rc == LK_OK ? "" : lk->err);
    return rc;
}

// ---------------------------------------------------------------------------------------------------- snapshots: list, auto, delete, restore

#define SNAP_AUTO_MARK ".auto" // inside a snapshot that the app made by itself (only those are ever pruned)

// mm-dd-yyyy--hh-mm, then -NN when the name was taken: as a number that sorts by time (0 = not one of ours)
static uint64_t snap_key(const char *name)
{
    int mo, d, y, h, mi, nn = 0, used = 0;
    if (sscanf(name, "%2d-%2d-%4d--%2d-%2d%n", &mo, &d, &y, &h, &mi, &used) != 5 || used != 17) return 0;
    if (name[17] == '-' && sscanf(name + 18, "%d", &nn) != 1) return 0;
    return ((((((uint64_t)y * 100 + (uint64_t)mo) * 100 + (uint64_t)d) * 100 + (uint64_t)h) * 100 + (uint64_t)mi) * 100) + (uint64_t)nn;
}

static int snap_cmp(const void *a, const void *b)
{
    uint64_t ka = snap_key(((const LkSnapshot *)a)->leaf), kb = snap_key(((const LkSnapshot *)b)->leaf);
    return ka < kb ? 1 : ka > kb ? -1 : 0; // newest first
}

// who is in the snapshot at `dir`: how many profiles, and the first few with their looks (read through a copy of the store that
// points there)
static void snap_people(Locker *lk, const char *dir, LkSnapshot *s)
{
    Store tmp = *lk->st;
    if (strlen(dir) >= sizeof tmp.root) return;
    snprintf(tmp.root, sizeof tmp.root, "%.500s", dir);
    Profile *pl = NULL;
    size_t np = 0;
    if (store_profile_list(&tmp, &pl, &np) != 0) return;
    s->nprofiles = (int)np;
    for (size_t i = 0; i < np && s->npeople < 4; i++) {
        snprintf(s->name[s->npeople], sizeof s->name[0], "%s", pl[i].name);
        s->color[s->npeople] = pl[i].color;
        s->picture[s->npeople] = pl[i].picture;
        s->npeople++;
    }
    free(pl);
}

int lk_snapshot_list(Locker *lk, const char *parent, LkSnapshot **out, size_t *n)
{
    reset(lk);
    *out = NULL;
    *n = 0;
    if (!fsx_is_dir(parent)) return LK_OK;
    char **names;
    size_t cnt;
    if (fsx_list(parent, &names, &cnt) != 0) return fail(lk, LK_ERR_IO, "Couldn't read %s.", parent);
    LkSnapshot *v = calloc(cnt ? cnt : 1, sizeof *v);
    if (!v) {
        fsx_list_free(names, cnt);
        return fail(lk, LK_ERR_IO, "Out of memory.");
    }
    size_t m = 0;
    for (size_t i = 0; i < cnt; i++) {
        size_t len = strlen(names[i]);
        char p[FSX_PATH], q[FSX_PATH];
        if (!snap_key(names[i]) || len >= sizeof v[m].leaf || fsx_join(p, sizeof p, parent, names[i]) != 0 || !fsx_is_dir(p)) continue;
        fsx_join(q, sizeof q, p, "config.json");
        int ok = fsx_exists(q);
        fsx_join(q, sizeof q, p, "profiles");
        if (!ok && !fsx_is_dir(q)) continue; // not a snapshot of ours
        snprintf(v[m].leaf, sizeof v[m].leaf, "%s", names[i]);
        fsx_join(q, sizeof q, p, SNAP_AUTO_MARK);
        v[m].automatic = fsx_exists(q);
        if (v[m].automatic) {
            char *txt = NULL;
            size_t len = 0;
            if (fsx_read_all(q, &txt, &len) == 0 && txt) {
                snprintf(v[m].note, sizeof v[m].note, "%s", strcmp(txt, "1") ? txt : "");
                free(txt);
            }
        }
        v[m].bytes = fsx_tree_size(p);
        snap_people(lk, p, &v[m]);
        m++;
    }
    fsx_list_free(names, cnt);
    qsort(v, m, sizeof *v, snap_cmp);
    *out = v;
    *n = m;
    return LK_OK;
}

// the folder of a snapshot that is in the listing (so a name can't point anywhere else)
static int snap_dir(Locker *lk, const char *parent, const char *leaf, char *out, size_t outn)
{
    LkSnapshot *v;
    size_t n;
    TRY(lk_snapshot_list(lk, parent, &v, &n));
    int found = 0;
    for (size_t i = 0; i < n; i++) found |= !strcmp(v[i].leaf, leaf);
    free(v);
    if (!found || fsx_join(out, outn, parent, leaf) != 0) return fail(lk, LK_ERR_IO, "That backup isn't there any more.");
    return LK_OK;
}

// the date part of a snapshot's name, "mm-dd-yyyy"
#define SNAP_DAY 10

int lk_snapshot_auto_due(Locker *lk, const char *parent, const char *leaf)
{
    LkSnapshot *v;
    size_t n;
    if (strlen(leaf) < SNAP_DAY || lk_snapshot_list(lk, parent, &v, &n) != LK_OK) return 1;
    int due = 1;
    for (size_t i = 0; i < n; i++)
        if (v[i].automatic && !strncmp(v[i].leaf, leaf, SNAP_DAY)) due = 0;
    free(v);
    return due;
}

int lk_snapshot_auto(Locker *lk, const char *parent, const char *leaf, const char *note, int keep, char *out, size_t outn)
{
    if (!lk_snapshot_auto_due(lk, parent, leaf)) {
        reset(lk);
        return LK_NOOP; // one a day is enough
    }
    char dst[FSX_PATH];
    int rc = lk_snapshot(lk, parent, leaf, dst, sizeof dst);
    if (rc != LK_OK) return rc;
    char mark[FSX_PATH];
    if (fsx_join(mark, sizeof mark, dst, SNAP_AUTO_MARK) != 0 || fsx_write_file(mark, note && note[0] ? note : "1", note && note[0] ? strlen(note) : 1) != 0)
        warn(lk, "The backup was made, but couldn't be marked as automatic.");
    if (out && outn) snprintf(out, outn, "%s", dst);
    if (keep < 1) keep = 1;
    LkSnapshot *v;
    size_t n;
    char err[sizeof lk->err];
    snprintf(err, sizeof err, "%s", lk->err);
    if (lk_snapshot_list(lk, parent, &v, &n) == LK_OK) {
        int seen = 0;
        for (size_t i = 0; i < n; i++) {
            if (!v[i].automatic || ++seen <= keep) continue;
            char old[FSX_PATH];
            if (fsx_join(old, sizeof old, parent, v[i].leaf) == 0) fsx_rmtree(old);
            store_logf(lk->st, "snapshot %s removed (only the newest %d automatic ones are kept)", v[i].leaf, keep);
        }
        free(v);
    }
    snprintf(lk->err, sizeof lk->err, "%s", err);
    return LK_OK;
}

int lk_snapshot_delete(Locker *lk, const char *parent, const char *leaf)
{
    char dir[FSX_PATH];
    TRY(snap_dir(lk, parent, leaf, dir, sizeof dir));
    if (fsx_rmtree(dir) != 0) return fail(lk, LK_ERR_IO, "Couldn't delete it. Is the SD card locked?");
    store_logf(lk->st, "snapshot %s deleted", leaf);
    return LK_OK;
}

// What a restore swaps in. state.json (who is loaded, the journal) and the logs stay as they are: they describe this console now.
static const char *const RESTORE_ITEMS[] = {"profiles", "unclaimed", "system-profile", "config.json"};
#define N_RESTORE (sizeof RESTORE_ITEMS / sizeof RESTORE_ITEMS[0])

static void restore_path(const Locker *lk, const char *item, const char *suffix, char *out, size_t n)
{
    snprintf(out, n, "%s/%s%s", lk->st->root, item, suffix);
}

// A restore that was cut short: put back whatever was set aside, and clear what was half-copied.
static void restore_heal(Locker *lk)
{
    for (size_t i = 0; i < N_RESTORE; i++) {
        char cur[FSX_PATH], aside[FSX_PATH], stage[FSX_PATH];
        restore_path(lk, RESTORE_ITEMS[i], "", cur, sizeof cur);
        restore_path(lk, RESTORE_ITEMS[i], ".replaced", aside, sizeof aside);
        restore_path(lk, RESTORE_ITEMS[i], ".restoring", stage, sizeof stage);
        if (fsx_exists(stage)) fsx_rmtree(stage);
        if (fsx_exists(aside)) {
            if (!fsx_exists(cur)) rename(aside, cur);
            else fsx_rmtree(aside);
        }
    }
}

int lk_snapshot_restore(Locker *lk, const char *parent, const char *leaf, const char *safety_leaf)
{
    reset(lk);
    if (lk->st->state.phase != PH_IDLE) return fail(lk, LK_ERR_BUSY_STATE, "Finish the last swap first.");
    if (strcmp(lk->st->state.holder, UNCLAIMED) != 0) return fail(lk, LK_ERR_IS_HOLDER, "Someone's saves are loaded. Sign out first.");
    char src[FSX_PATH];
    TRY(snap_dir(lk, parent, leaf, src, sizeof src));
    // what is kept now goes into a backup of its own first (a manual one: it stays until someone deletes it)
    char safe[FSX_PATH];
    TRY(lk_snapshot(lk, parent, safety_leaf, safe, sizeof safe));

    uint64_t total = 0;
    for (size_t i = 0; i < N_RESTORE; i++) {
        char s[FSX_PATH];
        fsx_join(s, sizeof s, src, RESTORE_ITEMS[i]);
        if (fsx_exists(s)) total += fsx_tree_size(s);
    }
    restore_heal(lk);
    snprintf(lk->label, sizeof lk->label, "Restoring a backup");
    lk->cur_title = 0;
    snprintf(lk->cur_name, sizeof lk->cur_name, "Washi");
    lk->cur = 0;
    lk->total = total;
    int rc = LK_OK;
    int staged[N_RESTORE] = {0};
    for (size_t i = 0; i < N_RESTORE && rc == LK_OK; i++) {
        char s[FSX_PATH], stage[FSX_PATH];
        fsx_join(s, sizeof s, src, RESTORE_ITEMS[i]);
        if (!fsx_exists(s)) continue;
        restore_path(lk, RESTORE_ITEMS[i], ".restoring", stage, sizeof stage);
        if (fsx_is_dir(s)) {
            rc = copy_tree(lk, s, stage, 0);
            if (rc == LK_OK && fsx_tree_equal(s, stage) != 1) rc = fail(lk, LK_ERR_VERIFY, "The copy didn't match, so nothing was changed.");
        } else {
            rc = copy_file(lk, s, stage, 0);
            uint32_t c1, c2;
            uint64_t s1, s2;
            if (rc == LK_OK && (fsx_file_crc(s, &c1, &s1) != 0 || fsx_file_crc(stage, &c2, &s2) != 0 || c1 != c2 || s1 != s2))
                rc = fail(lk, LK_ERR_VERIFY, "The copy didn't match, so nothing was changed.");
        }
        staged[i] = rc == LK_OK;
    }
    // the swap itself: renames only
    size_t done = 0;
    if (rc == LK_OK) {
        for (; done < N_RESTORE; done++) {
            char cur[FSX_PATH], aside[FSX_PATH], stage[FSX_PATH];
            restore_path(lk, RESTORE_ITEMS[done], "", cur, sizeof cur);
            restore_path(lk, RESTORE_ITEMS[done], ".replaced", aside, sizeof aside);
            restore_path(lk, RESTORE_ITEMS[done], ".restoring", stage, sizeof stage);
            if (fsx_exists(cur) && rename(cur, aside) != 0) {
                rc = fail(lk, LK_ERR_IO, "Couldn't set %s aside.", RESTORE_ITEMS[done]);
                break;
            }
            if (staged[done] && rename(stage, cur) != 0) {
                rename(aside, cur);
                rc = fail(lk, LK_ERR_IO, "Couldn't put %s in place.", RESTORE_ITEMS[done]);
                break;
            }
        }
        if (rc != LK_OK) // undo the ones that were already swapped
            for (size_t i = 0; i < done; i++) {
                char cur[FSX_PATH], aside[FSX_PATH];
                restore_path(lk, RESTORE_ITEMS[i], "", cur, sizeof cur);
                restore_path(lk, RESTORE_ITEMS[i], ".replaced", aside, sizeof aside);
                if (fsx_exists(cur)) fsx_rmtree(cur);
                if (fsx_exists(aside)) rename(aside, cur);
            }
    }
    restore_heal(lk); // clears what is left over (or, after an undo, nothing)
    store_logf(lk->st, "restored from snapshot %s (what was kept before is in %s): %s%s%s", leaf, strrchr(safe, '/') ? strrchr(safe, '/') + 1 : safe,
               rc == LK_OK ? "ok" : "FAILED", rc == LK_OK ? "" : ": ", rc == LK_OK ? "" : lk->err);
    return rc;
}
