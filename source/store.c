#include "store.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cJSON.h"
#include "hash.h"

#define LOG_MAX_BYTES (1024 * 1024)

// ---------------------------------------------------------------------------------------------------- paths

void store_path(const Store *st, char *out, size_t n, const char *rel)
{
    if (fsx_join(out, n, st->root, rel) != 0) out[0] = 0;
}

void store_profile_dir(const Store *st, const char *id, char *out, size_t n)
{
    char rel[FSX_PATH];
    snprintf(rel, sizeof rel, "profiles/%s", id);
    store_path(st, out, n, rel);
}

int store_ts(Store *st, char *out, size_t n)
{
    time_t t = st->be->now(st->be);
    struct tm tm;
    gmtime_r(&t, &tm);
    return strftime(out, n, "%Y%m%d-%H%M%S", &tm) > 0 ? 0 : -1;
}

int store_unique_child(const char *parent, const char *base, char *out, size_t n)
{
    char path[FSX_PATH];
    snprintf(out, n, "%s", base);
    for (int i = 2; i < 100; i++) {
        if (fsx_join(path, sizeof path, parent, out) != 0) return -1;
        if (!fsx_exists(path)) return 0;
        int w = snprintf(out, n, "%s-%02d", base, i);
        if (w < 0 || (size_t)w >= n) return -1; // (it wouldn't fit: a cut-off name could be someone else's)
    }
    return -1;
}

// ---- the activity log: one file for every start of Washi, in <root>/logs, named by when the start was (UTC): "2026-10-11_00-23-45.txt". The ones older than
// LOG_KEEP_DAYS are deleted when Washi starts. (Before the logs folder there was one log.txt, and a log.old: they are moved in, named by when they were last written.)

// seconds since 1970 of a UTC date and time (no timegm on every libc)
static int64_t epoch_of(int y, int m, int d, int hh, int mm, int ss)
{
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    int64_t yoe = y - era * 400, doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1, doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return (era * 146097 + doe - 719468) * 86400 + hh * 3600 + mm * 60 + ss;
}

static void log_name_at(time_t t, const char *tail, char *out, size_t n)
{
    struct tm tm;
    gmtime_r(&t, &tm);
    char stamp[32];
    strftime(stamp, sizeof stamp, "%Y-%m-%d_%H-%M-%S", &tm);
    snprintf(out, n, "%s%s.txt", stamp, tail);
}

// when a log file was started: what its name says, else when it was last written
static int64_t log_time(const char *dir, const char *name)
{
    int y, m, d, hh, mm, ss;
    if (sscanf(name, "%4d-%2d-%2d_%2d-%2d-%2d", &y, &m, &d, &hh, &mm, &ss) == 6 && m >= 1 && m <= 12 && d >= 1 && d <= 31) return epoch_of(y, m, d, hh, mm, ss);
    char p[FSX_PATH];
    return fsx_join(p, sizeof p, dir, name) == 0 ? fsx_mtime(p) : 0;
}

// an old single log file, moved into the logs folder under the time it was last written (and a word for which it was)
static void log_move_in(const Store *st, const char *dir, const char *rel, const char *tail)
{
    char from[FSX_PATH], name[48], to[FSX_PATH];
    store_path(st, from, sizeof from, rel);
    if (!fsx_exists(from)) return;
    log_name_at((time_t)fsx_mtime(from), tail, name, sizeof name);
    if (fsx_join(to, sizeof to, dir, name) != 0 || fsx_exists(to) || rename(from, to) != 0) remove(from);
}

void store_logs_start(Store *st)
{
    char dir[FSX_PATH], name[48];
    store_path(st, dir, sizeof dir, "logs");
    if (fsx_mkdir_p(dir) != 0) return;
    time_t now = st->be->now(st->be);
    log_name_at(now, "", name, sizeof name);
    snprintf(st->session, sizeof st->session, "logs/%s", name);
    log_move_in(st, dir, "log.txt", "-earlier");
    log_move_in(st, dir, "log.old", "-older");
    char **names = NULL;
    size_t n = 0;
    if (fsx_list(dir, &names, &n) != 0) return;
    for (size_t i = 0; i < n; i++) {
        size_t l = strlen(names[i]);
        char p[FSX_PATH];
        if (l < 5 || strcmp(names[i] + l - 4, ".txt") != 0 || fsx_join(p, sizeof p, dir, names[i]) != 0) continue;
        int64_t t = log_time(dir, names[i]);
        if (t > 0 && (int64_t)now - t > (int64_t)LOG_KEEP_DAYS * 86400) remove(p);
    }
    fsx_list_free(names, n);
}

void store_session_log(const Store *st, char *out, size_t n)
{
    if (st->session[0]) store_path(st, out, n, st->session);
    else out[0] = 0;
}

int64_t store_log_started(const Store *st, const char *name)
{
    char dir[FSX_PATH];
    store_path(st, dir, sizeof dir, "logs");
    return log_time(dir, name);
}

int store_logs_list(const Store *st, char ***names, size_t *n)
{
    char dir[FSX_PATH];
    store_path(st, dir, sizeof dir, "logs");
    char **all = NULL;
    size_t cnt = 0;
    *names = NULL;
    *n = 0;
    if (fsx_list(dir, &all, &cnt) != 0) return -1;
    size_t k = 0;
    int64_t *when = calloc(cnt ? cnt : 1, sizeof *when);
    if (!when) {
        fsx_list_free(all, cnt);
        return -1;
    }
    for (size_t i = 0; i < cnt; i++) { // (the .txt files)
        size_t l = strlen(all[i]);
        if (l >= 5 && strcmp(all[i] + l - 4, ".txt") == 0) {
            all[k] = all[i];
            when[k++] = log_time(dir, all[i]);
        } else {
            free(all[i]);
        }
    }
    for (size_t i = 1; i < k; i++) { // the newest first (a small list: an insertion sort)
        char *name = all[i];
        int64_t t = when[i];
        size_t j = i;
        while (j > 0 && (when[j - 1] < t || (when[j - 1] == t && strcmp(all[j - 1], name) < 0))) {
            all[j] = all[j - 1];
            when[j] = when[j - 1];
            j--;
        }
        all[j] = name;
        when[j] = t;
    }
    free(when);
    *names = all;
    *n = k;
    return 0;
}

void store_logf(Store *st, const char *fmt, ...)
{
    char path[FSX_PATH], line[512], stamp[32];
    store_session_log(st, path, sizeof path);
    if (!path[0]) return;
    time_t t = st->be->now(st->be);
    struct tm tm;
    gmtime_r(&t, &tm);
    strftime(stamp, sizeof stamp, "%Y-%m-%d %H:%M:%S", &tm);

    va_list ap;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    FILE *f = fopen(path, "rb");
    if (f) {
        fseek(f, 0, SEEK_END);
        long size = ftell(f);
        fclose(f);
        if (size > LOG_MAX_BYTES) return; // (one start's log has a size to keep to; the next start has a new file)
    }
    f = fopen(path, "ab");
    if (!f) return;
    fprintf(f, "[%s UTC] %s\n", stamp, line);
    fclose(f);
}

// ---------------------------------------------------------------------------------------------------- json io

// 0 = parsed, 1 = missing, -1 = damaged. Falls back to <path>.tmp (see fsx_write_atomic).
static int read_json(const char *path, cJSON **out)
{
    char *text = NULL;
    char tmp[FSX_PATH];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    if (fsx_exists(path)) {
        if (fsx_read_all(path, &text, NULL) != 0) return -1;
    } else if (fsx_exists(tmp)) {
        if (fsx_read_all(tmp, &text, NULL) != 0) return -1;
    } else {
        return 1;
    }
    cJSON *j = cJSON_Parse(text);
    free(text);
    if (!j || !cJSON_IsObject(j)) {
        cJSON_Delete(j);
        return -1;
    }
    *out = j;
    return 0;
}

static const char *jstr(const cJSON *o, const char *key, const char *def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsString(v) && v->valuestring ? v->valuestring : def;
}

static double jnum(const cJSON *o, const char *key, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

static void copy_str(char *dst, size_t n, const char *src)
{
    snprintf(dst, n, "%s", src);
}

static int state_between(void *u)
{
    Store *s = u;
    return s->be->checkpoint(s->be, "state:between");
}

// `journal` = the file is the swap journal: tell the backend about the two crash windows inside the replace.
static int write_json(Store *st, const char *path, cJSON *j, int journal)
{
    char *text = cJSON_Print(j);
    cJSON_Delete(j);
    if (!text) return -1;
    int rc = fsx_write_atomic(path, text, strlen(text), journal ? state_between : NULL, st);
    free(text);
    return rc;
}

static void set_corrupt_aside(Store *st, const char *path, const char *what)
{
    char ts[32], aside[FSX_PATH];
    store_ts(st, ts, sizeof ts);
    snprintf(aside, sizeof aside, "%s.corrupt-%s", path, ts);
    rename(path, aside);
    snprintf(st->notice, sizeof st->notice, "%s was damaged and was set aside; defaults are in use.", what);
}

// ---------------------------------------------------------------------------------------------------- config/state

static void config_defaults(Config *c)
{
    memset(c, 0, sizeof *c);
    c->history_keep = 5;
    c->theme_mode = 0;
    c->clock24 = 0;
    c->sort_mode = SORT_RECENT;
    c->remind_minutes = 180;
    c->remind_restore = -1;
    c->guest_picture = 111; // Princess Toadette (Peachette: the winking one with the mushroom crown) ...
    c->guest_swatch = 43;   // ... on light blue
}

static void state_defaults(State *s)
{
    memset(s, 0, sizeof *s);
    copy_str(s->holder, sizeof s->holder, UNCLAIMED);
    s->phase = PH_IDLE;
}

static const char *phase_name(Phase p)
{
    return p == PH_BACKING_UP ? "backing_up" : p == PH_RESTORING ? "restoring" : p == PH_WIPING ? "wiping" : "idle";
}

int store_save_config(Store *st)
{
    char path[FSX_PATH];
    store_path(st, path, sizeof path, "config.json");
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "target_uid", st->cfg.target_uid);
    cJSON *ex = cJSON_AddArrayToObject(j, "excluded_titles");
    for (int i = 0; i < st->cfg.nexcluded; i++) {
        char hex[17];
        snprintf(hex, sizeof hex, "%016" PRIx64, st->cfg.excluded[i]);
        cJSON_AddItemToArray(ex, cJSON_CreateString(hex));
    }
    cJSON_AddStringToObject(j, "admin_pin_hash", st->cfg.admin_pin_hash);
    cJSON_AddStringToObject(j, "admin_pin_salt", st->cfg.admin_pin_salt);
    cJSON_AddNumberToObject(j, "history_keep", st->cfg.history_keep);
    cJSON_AddNumberToObject(j, "builtin_keyboard", st->cfg.builtin_keyboard);
    cJSON_AddStringToObject(j, "theme", st->cfg.theme_mode == 1 ? "light" : st->cfg.theme_mode == 2 ? "dark" : "auto");
    cJSON_AddNumberToObject(j, "clock24", st->cfg.clock24);
    cJSON_AddNumberToObject(j, "sort_mode", st->cfg.sort_mode);
    cJSON_AddNumberToObject(j, "remind_minutes", st->cfg.remind_minutes);
    cJSON_AddNumberToObject(j, "remind_restore", st->cfg.remind_restore);
    cJSON_AddNumberToObject(j, "sync_name", st->cfg.sync_name);
    cJSON_AddNumberToObject(j, "sync_picture", st->cfg.sync_picture);
    cJSON_AddNumberToObject(j, "guest_picture", st->cfg.guest_picture);
    cJSON_AddNumberToObject(j, "guest_swatch", st->cfg.guest_swatch);
    return write_json(st, path, j, 0);
}

int store_save_state(Store *st)
{
    char path[FSX_PATH];
    store_path(st, path, sizeof path, "state.json");
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "holder", st->state.holder);
    cJSON_AddStringToObject(j, "phase", phase_name(st->state.phase));
    cJSON_AddStringToObject(j, "from", st->state.from);
    cJSON_AddStringToObject(j, "to", st->state.to);
    cJSON_AddNumberToObject(j, "started", (double)st->state.started);
    cJSON_AddStringToObject(j, "unclaimed_base", st->state.unclaimed_base);
    cJSON_AddNumberToObject(j, "intro_done", st->state.intro_done);
    cJSON_AddNumberToObject(j, "loaded_at", (double)st->state.loaded_at);
    cJSON_AddNumberToObject(j, "wiped", st->state.wiped);
    return write_json(st, path, j, 1);
}

static void load_config(Store *st)
{
    char path[FSX_PATH];
    store_path(st, path, sizeof path, "config.json");
    config_defaults(&st->cfg);
    cJSON *j = NULL;
    int r = read_json(path, &j);
    if (r == -1) {
        set_corrupt_aside(st, path, "config.json");
        return;
    }
    if (r == 1) return;
    copy_str(st->cfg.target_uid, sizeof st->cfg.target_uid, jstr(j, "target_uid", ""));
    copy_str(st->cfg.admin_pin_hash, sizeof st->cfg.admin_pin_hash, jstr(j, "admin_pin_hash", ""));
    copy_str(st->cfg.admin_pin_salt, sizeof st->cfg.admin_pin_salt, jstr(j, "admin_pin_salt", ""));
    int keep = (int)jnum(j, "history_keep", 5);
    st->cfg.history_keep = keep < 1 ? 1 : keep > 50 ? 50 : keep;
    st->cfg.builtin_keyboard = jnum(j, "builtin_keyboard", 0) != 0;
    {
        const char *th = jstr(j, "theme", "auto");
        st->cfg.theme_mode = !strcmp(th, "light") ? 1 : !strcmp(th, "dark") ? 2 : 0;
    }
    st->cfg.clock24 = jnum(j, "clock24", 0) != 0;
    st->cfg.sort_mode = (int)jnum(j, "sort_mode", SORT_RECENT);
    if (st->cfg.sort_mode < 0 || st->cfg.sort_mode >= SORT_COUNT) st->cfg.sort_mode = SORT_RECENT;
    // minutes since 1.2 (in steps of 15); before that whole hours
    int remind = (int)jnum(j, "remind_minutes", jnum(j, "remind_hours", 3) * 60);
    st->cfg.remind_minutes = remind < 0 || remind > 72 * 60 ? 180 : remind;
    int restore = (int)jnum(j, "remind_restore", -1);
    st->cfg.remind_restore = restore < -1 || restore > 72 * 60 ? -1 : restore;
    st->cfg.sync_name = jnum(j, "sync_name", 0) != 0;
    st->cfg.sync_picture = jnum(j, "sync_picture", 0) != 0;
    st->cfg.guest_picture = (int)jnum(j, "guest_picture", 111);
    st->cfg.guest_swatch = (int)jnum(j, "guest_swatch", 43);
    if (st->cfg.guest_picture < -1) st->cfg.guest_picture = -1;
    if (st->cfg.guest_swatch < 0 || st->cfg.guest_swatch >= SWATCH_COUNT) st->cfg.guest_swatch = 43;
    const cJSON *ex = cJSON_GetObjectItemCaseSensitive(j, "excluded_titles");
    const cJSON *it;
    cJSON_ArrayForEach(it, ex)
    {
        uint64_t t;
        if (cJSON_IsString(it) && store_parse_title(it->valuestring, &t) == 0 && st->cfg.nexcluded < MAX_EXCLUDED)
            st->cfg.excluded[st->cfg.nexcluded++] = t;
    }
    cJSON_Delete(j);
}

static void load_state(Store *st)
{
    char path[FSX_PATH];
    store_path(st, path, sizeof path, "state.json");
    state_defaults(&st->state);
    cJSON *j = NULL;
    int r = read_json(path, &j);
    if (r == -1) {
        set_corrupt_aside(st, path, "state.json");
        return;
    }
    if (r == 1) return;
    copy_str(st->state.holder, sizeof st->state.holder, jstr(j, "holder", UNCLAIMED));
    if (!st->state.holder[0]) copy_str(st->state.holder, sizeof st->state.holder, UNCLAIMED);
    const char *ph = jstr(j, "phase", "idle");
    st->state.phase = !strcmp(ph, "backing_up") ? PH_BACKING_UP : !strcmp(ph, "restoring") ? PH_RESTORING : !strcmp(ph, "wiping") ? PH_WIPING : PH_IDLE;
    copy_str(st->state.from, sizeof st->state.from, jstr(j, "from", ""));
    copy_str(st->state.to, sizeof st->state.to, jstr(j, "to", ""));
    st->state.started = (int64_t)jnum(j, "started", 0);
    copy_str(st->state.unclaimed_base, sizeof st->state.unclaimed_base, jstr(j, "unclaimed_base", ""));
    st->state.intro_done = jnum(j, "intro_done", 0) != 0;
    st->state.loaded_at = (int64_t)jnum(j, "loaded_at", 0);
    st->state.wiped = jnum(j, "wiped", 0) != 0;
    cJSON_Delete(j);
}

void store_reload(Store *st)
{
    st->notice[0] = 0;
    load_config(st);
    load_state(st);
}

int store_open(Store *st, Backend *be, const char *root)
{
    memset(st, 0, sizeof *st);
    st->be = be;
    snprintf(st->root, sizeof st->root, "%s", root);
    char p[FSX_PATH];
    if (fsx_mkdir_p(st->root) != 0) return -1;
    store_path(st, p, sizeof p, "profiles");
    if (fsx_mkdir_p(p) != 0) return -1;
    store_path(st, p, sizeof p, "unclaimed");
    if (fsx_mkdir_p(p) != 0) return -1;
    load_config(st);
    load_state(st);
    store_logs_start(st);
    return 0;
}

// ---------------------------------------------------------------------------------------------------- titles

int store_is_excluded(const Store *st, uint64_t title)
{
    for (int i = 0; i < st->cfg.nexcluded; i++)
        if (st->cfg.excluded[i] == title) return 1;
    return 0;
}

void store_title_dir(uint64_t title, char out[17])
{
    snprintf(out, 17, "%016" PRIx64, title);
}

int store_parse_title(const char *name, uint64_t *title)
{
    if (strlen(name) != 16) return -1;
    uint64_t v = 0;
    for (int i = 0; i < 16; i++) {
        char c = name[i];
        int d = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'f') ? c - 'a' + 10 : (c >= 'A' && c <= 'F') ? c - 'A' + 10 : -1;
        if (d < 0) return -1;
        v = (v << 4) | (uint64_t)d;
    }
    *title = v;
    return 0;
}

// ---------------------------------------------------------------------------------------------------- pins

static int lower_ascii(int c) { return c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c; }

// does `name` from position i on start with q (case aside)?
static int starts_here(const char *name, const char *q)
{
    for (; *q; q++, name++)
        if (!*name || lower_ascii((unsigned char)*name) != lower_ascii((unsigned char)*q)) return 0;
    return 1;
}

int store_name_match(const char *name, const char *query)
{
    char q[NAME_MAX_BYTES];
    size_t n = 0;
    for (const char *p = query; p && *p && n < sizeof q - 1; p++)
        if (*p != ' ') q[n++] = *p;
    q[n] = 0;
    if (!n || !name) return 0;
    if (starts_here(name, q)) return 4;
    for (const char *p = name + 1; *p; p++)
        if (p[-1] == ' ' && starts_here(p, q)) return 3;
    for (const char *p = name + 1; *p; p++)
        if (starts_here(p, q)) return 2;
    const char *p = name; // the letters in order, anything between them
    for (size_t i = 0; i < n; i++) {
        while (*p && lower_ascii((unsigned char)*p) != lower_ascii((unsigned char)q[i])) p++;
        if (!*p) return 0;
        p++;
    }
    return 1;
}

int store_valid_name(const char *name)
{
    size_t len = strlen(name);
    if (len == 0 || len >= NAME_MAX_BYTES) return 0;
    for (size_t i = 0; i < len; i++)
        if (!isspace((unsigned char)name[i])) return 1;
    return 0;
}

int store_valid_pin(const char *pin)
{
    if (!pin || strlen(pin) != PIN_LEN) return 0;
    for (int i = 0; i < PIN_LEN; i++)
        if (pin[i] < '0' || pin[i] > '9') return 0;
    return 1;
}

int store_valid_netid(const char *s)
{
    if (!s || strlen(s) != 7) return 0;
    for (int i = 0; i < 3; i++)
        if (!((s[i] >= 'a' && s[i] <= 'z') || (s[i] >= 'A' && s[i] <= 'Z'))) return 0;
    for (int i = 3; i < 7; i++)
        if (s[i] < '0' || s[i] > '9') return 0;
    return 1;
}

void store_netid_clean(const char *in, char out[12])
{
    out[0] = 0;
    if (!in) return;
    while (*in == ' ') in++;
    char tmp[16];
    size_t n = strlen(in);
    while (n > 0 && in[n - 1] == ' ') n--;
    if (n >= sizeof tmp) return;
    memcpy(tmp, in, n);
    tmp[n] = 0;
    if (!store_valid_netid(tmp)) return;
    for (size_t i = 0; i < n; i++) out[i] = (char)tolower((unsigned char)tmp[i]);
    out[n] = 0;
}

int store_netid_owner(Store *st, const char *netid, const char *except_id, Profile *owner)
{
    char clean[12];
    store_netid_clean(netid, clean);
    if (!clean[0]) return 0;
    Profile *list = NULL;
    size_t n = 0;
    int found = 0;
    if (store_profile_list(st, &list, &n) == 0) {
        for (size_t i = 0; i < n && !found; i++)
            if (list[i].netid[0] && !strcmp(list[i].netid, clean) && (!except_id || strcmp(list[i].id, except_id) != 0)) {
                found = 1;
                if (owner) *owner = list[i];
            }
        free(list);
    }
    return found;
}

// The console's colours are laid out one hue per row, light to dark across (swatches.h): a new profile gets one of the lighter
// half of any hue, so it comes out pastel orange rather than brown.
int store_random_swatch(Store *st)
{
    uint32_t r = st->be->random(st->be);
    int rows = SWATCH_COUNT / SWATCH_COLS, row = (int)(r % (uint32_t)rows), col = (int)((r / (uint32_t)rows) % (SWATCH_COLS / 2));
    return row * SWATCH_COLS + col;
}

static void hash_pin(const char *salt, const char *pin, char out[65])
{
    char buf[128];
    int n = snprintf(buf, sizeof buf, "%s:%s", salt, pin);
    sha256_hex(buf, (size_t)n, out);
}

static void make_salt(Store *st, char out[33])
{
    for (int i = 0; i < 4; i++) snprintf(out + i * 8, 9, "%08x", st->be->random(st->be));
}

int store_profile_has_pin(const Profile *p)
{
    return p->pin_hash[0] != 0;
}

int store_profile_check_pin(const Profile *p, const char *pin)
{
    if (!store_profile_has_pin(p)) return 1;
    if (!store_valid_pin(pin)) return 0;
    char h[65];
    hash_pin(p->salt, pin, h);
    return strcmp(h, p->pin_hash) == 0;
}

int store_profile_set_pin(Store *st, Profile *p, const char *pin)
{
    if (!pin) {
        p->pin_hash[0] = 0;
        return store_profile_save(st, p);
    }
    if (!store_valid_pin(pin)) return -1;
    make_salt(st, p->salt);
    hash_pin(p->salt, pin, p->pin_hash);
    return store_profile_save(st, p);
}

int store_admin_has_pin(const Store *st)
{
    return st->cfg.admin_pin_hash[0] != 0;
}

int store_admin_check_pin(const Store *st, const char *pin)
{
    if (!store_admin_has_pin(st)) return 1;
    if (!store_valid_pin(pin)) return 0;
    char h[65];
    hash_pin(st->cfg.admin_pin_salt, pin, h);
    return strcmp(h, st->cfg.admin_pin_hash) == 0;
}

int store_admin_set_pin(Store *st, const char *pin)
{
    if (!store_valid_pin(pin)) return -1;
    make_salt(st, st->cfg.admin_pin_salt);
    hash_pin(st->cfg.admin_pin_salt, pin, st->cfg.admin_pin_hash);
    return store_save_config(st);
}

// ---------------------------------------------------------------------------------------------------- profiles

// writes p's profile.json into `dir` (the profile's folder, wherever that is)
static int save_profile_file(Store *st, const char *dir, const Profile *p)
{
    char path[FSX_PATH];
    if (fsx_join(path, sizeof path, dir, "profile.json") != 0) return -1;
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "name", p->name);
    cJSON_AddStringToObject(j, "pin_hash", p->pin_hash);
    cJSON_AddStringToObject(j, "salt", p->salt);
    cJSON_AddNumberToObject(j, "created", (double)p->created);
    cJSON_AddNumberToObject(j, "last_used", (double)p->last_used);
    cJSON_AddNumberToObject(j, "swatch", p->color);
    cJSON_AddNumberToObject(j, "picture", p->picfile[0] ? -1 : p->picture); // (an Online icon is kept by name: its number is only for now)
    if (p->picfile[0]) cJSON_AddStringToObject(j, "picfile", p->picfile);
    cJSON_AddStringToObject(j, "netid", p->netid);
    if (p->pinned) cJSON_AddNumberToObject(j, "pinned", (double)p->pinned);
    return write_json(st, path, j, 0);
}

int store_profile_save(Store *st, const Profile *p)
{
    char dir[FSX_PATH];
    store_profile_dir(st, p->id, dir, sizeof dir);
    if (fsx_mkdir_p(dir) != 0) return -1;
    return save_profile_file(st, dir, p);
}

// reads a profile.json (wherever it is) as the profile `id`
static int load_profile_file(const char *path, const char *id, Profile *out)
{
    cJSON *j = NULL;
    if (read_json(path, &j) != 0) return -1;
    memset(out, 0, sizeof *out);
    copy_str(out->id, sizeof out->id, id);
    copy_str(out->name, sizeof out->name, jstr(j, "name", id));
    copy_str(out->pin_hash, sizeof out->pin_hash, jstr(j, "pin_hash", ""));
    copy_str(out->salt, sizeof out->salt, jstr(j, "salt", ""));
    out->created = (int64_t)jnum(j, "created", 0);
    out->last_used = (int64_t)jnum(j, "last_used", 0);
    if (cJSON_GetObjectItemCaseSensitive(j, "swatch")) {
        out->color = (int)jnum(j, "swatch", 0);
    } else if (cJSON_GetObjectItemCaseSensitive(j, "color")) { // an older profile: one of the original six colours
        int old = (int)jnum(j, "color", 0);
        out->color = SWATCH_VIVID[(old < 0 ? 0 : old) % 6];
    } else { // an older one still: a stable colour from its id
        unsigned h = 0;
        for (const char *q = id; *q; q++) h = h * 31 + (unsigned char)*q;
        out->color = SWATCH_VIVID[h % 6];
    }
    if (out->color < 0 || out->color >= PROFILE_SWATCHES) out->color = 0;
    out->picture = (int)jnum(j, "picture", -1);
    if (out->picture < -1) out->picture = -1;
    const char *pf = jstr(j, "picfile", "");
    if (pf[0] && custom_key_valid(pf) && strlen(pf) < sizeof out->picfile) { // an Online icon: its key now has a number
        int cid = custom_id(pf);
        if (cid >= 0) {
            copy_str(out->picfile, sizeof out->picfile, pf);
            out->picture = cid;
        }
    }
    store_netid_clean(jstr(j, "netid", ""), out->netid);
    out->pinned = (int64_t)jnum(j, "pinned", 0);
    if (out->pinned < 0) out->pinned = 0;
    cJSON_Delete(j);
    return 0;
}

int store_profile_load(Store *st, const char *id, Profile *out)
{
    char dir[FSX_PATH], path[FSX_PATH];
    if (!id[0] || strchr(id, '/') || id[0] == '.') return -1;
    store_profile_dir(st, id, dir, sizeof dir);
    if (fsx_join(path, sizeof path, dir, "profile.json") != 0) return -1;
    return load_profile_file(path, id, out);
}

static void slugify(const char *name, char *out, size_t n)
{
    size_t o = 0;
    int dash = 0;
    for (const char *p = name; *p && o + 1 < n && o < 20; p++) {
        unsigned char c = (unsigned char)*p;
        if (isalnum(c) && c < 128) {
            out[o++] = (char)tolower(c);
            dash = 0;
        } else if (!dash && o > 0) {
            out[o++] = '-';
            dash = 1;
        }
    }
    while (o > 0 && out[o - 1] == '-') o--;
    if (o == 0) {
        snprintf(out, n, "user");
        return;
    }
    out[o] = 0;
}

int store_profile_create(Store *st, const char *name, const char *pin, Profile *out)
{
    if (!store_valid_name(name)) return -1;
    if (pin && !store_valid_pin(pin)) return -1;
    memset(out, 0, sizeof *out);
    char slug[32], dir[FSX_PATH];
    slugify(name, slug, sizeof slug);
    for (int tries = 0; tries < 50; tries++) {
        snprintf(out->id, sizeof out->id, "%s-%04x", slug, st->be->random(st->be) & 0xFFFF);
        store_profile_dir(st, out->id, dir, sizeof dir);
        if (!fsx_exists(dir)) break;
        out->id[0] = 0;
    }
    if (!out->id[0]) return -1;

    // trim the name
    const char *s = name;
    while (isspace((unsigned char)*s)) s++;
    snprintf(out->name, sizeof out->name, "%s", s);
    size_t len = strlen(out->name);
    while (len > 0 && isspace((unsigned char)out->name[len - 1])) out->name[--len] = 0;
    out->created = (int64_t)st->be->now(st->be);
    out->last_used = 0;
    out->picture = -1;
    {   // the least-used avatar colour so far (lowest index on a tie): a row of avatars stays evenly mixed
        int used[6] = {0, 0, 0, 0, 0, 0};
        Profile *others = NULL;
        size_t no = 0;
        if (store_profile_list(st, &others, &no) == 0) {
            for (size_t i = 0; i < no; i++)
                for (int k = 0; k < 6; k++)
                    if (others[i].color == SWATCH_VIVID[k]) used[k]++;
            free(others);
        }
        int best = 0;
        for (int k = 1; k < 6; k++)
            if (used[k] < used[best]) best = k;
        out->color = SWATCH_VIVID[best];
    }
    if (pin) {
        make_salt(st, out->salt);
        hash_pin(out->salt, pin, out->pin_hash);
    }
    if (store_profile_save(st, out) != 0) return -1;
    store_logf(st, "profile created: %s (%s)%s", out->name, out->id, pin ? " with password" : "");
    return 0;
}

static int cmp_profiles(const void *a, const void *b)
{
    const Profile *pa = a, *pb = b;
    const char *x = pa->name, *y = pb->name;
    for (;; x++, y++) {
        int cx = tolower((unsigned char)*x), cy = tolower((unsigned char)*y);
        if (cx != cy) return cx - cy;
        if (!cx) break;
    }
    return strcmp(pa->id, pb->id);
}

int store_profile_list(Store *st, Profile **out, size_t *n)
{
    char dir[FSX_PATH];
    store_path(st, dir, sizeof dir, "profiles");
    char **names;
    size_t cnt;
    if (fsx_list(dir, &names, &cnt) != 0) return -1;
    Profile *v = calloc(cnt ? cnt : 1, sizeof *v);
    if (!v) { fsx_list_free(names, cnt); return -1; }
    size_t k = 0;
    for (size_t i = 0; i < cnt; i++) {
        if (names[i][0] == '.') continue;
        if (store_profile_load(st, names[i], &v[k]) == 0) k++;
    }
    fsx_list_free(names, cnt);
    qsort(v, k, sizeof *v, cmp_profiles);
    *out = v;
    *n = k;
    return 0;
}

int store_profile_trash(Store *st, const char *id)
{
    char src[FSX_PATH], trash[FSX_PATH], ts[32], base[FSX_PATH], leaf[FSX_PATH], dst[FSX_PATH];
    store_profile_dir(st, id, src, sizeof src);
    if (!fsx_is_dir(src)) return -1;
    store_path(st, trash, sizeof trash, "profiles/.trash");
    if (fsx_mkdir_p(trash) != 0) return -1;
    store_ts(st, ts, sizeof ts);
    snprintf(base, sizeof base, "%s-%s", id, ts);
    if (store_unique_child(trash, base, leaf, sizeof leaf) != 0) return -1;
    fsx_join(dst, sizeof dst, trash, leaf);
    if (rename(src, dst) != 0) return -1;
    store_logf(st, "profile moved to trash: %s", id);
    return 0;
}

// ---------------------------------------------------------------------------------------------------- deleted profiles

int64_t store_parse_ts(const char *s)
{
    int y, mo, d, h, mi, sec;
    if (sscanf(s, "%4d%2d%2d-%2d%2d%2d", &y, &mo, &d, &h, &mi, &sec) != 6) return 0;
    // days from civil (Howard Hinnant)
    y -= mo <= 2;
    long era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153 * (unsigned)(mo + (mo > 2 ? -3 : 9)) + 2) / 5 + (unsigned)d - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    long days = era * 146097 + (long)doe - 719468;
    return (int64_t)days * 86400 + h * 3600 + mi * 60 + sec;
}

// A trash folder is "<id>-<YYYYMMDD>-<HHMMSS>", or that with "-NN" after it (store_unique_child). Where the id ends, or -1.
#define TRASH_GONE ".gone-" // a deleted profile being removed for good (see store_trash_purge)

static int trash_split(const char *leaf)
{
    size_t len = strlen(leaf);
    for (size_t extra = 0; extra <= 3; extra += 3) {
        if (len < 16 + extra + 1) continue;
        const char *p = leaf + len - 16 - extra; // "-YYYYMMDD-HHMMSS"
        int ok = p[0] == '-' && p[9] == '-';
        for (int i = 1; i <= 8 && ok; i++) ok = isdigit((unsigned char)p[i]) != 0;
        for (int i = 10; i <= 15 && ok; i++) ok = isdigit((unsigned char)p[i]) != 0;
        if (ok && extra) ok = p[16] == '-' && isdigit((unsigned char)p[17]) && isdigit((unsigned char)p[18]);
        if (ok) return (int)(len - 16 - extra);
    }
    return -1;
}

// the games a profile folder holds saves for: title folders with something in them, shared games left out
static int count_games(const Store *st, const char *profile_dir)
{
    char cur[FSX_PATH], **names;
    size_t n = 0;
    int games = 0;
    if (fsx_join(cur, sizeof cur, profile_dir, "current") != 0 || fsx_list(cur, &names, &n) != 0) return 0;
    for (size_t i = 0; i < n; i++) {
        uint64_t t;
        char p[FSX_PATH], **in;
        size_t nin = 0;
        if (store_parse_title(names[i], &t) != 0 || store_is_excluded(st, t)) continue;
        if (fsx_join(p, sizeof p, cur, names[i]) != 0 || fsx_list(p, &in, &nin) != 0) continue;
        fsx_list_free(in, nin);
        if (nin) games++;
    }
    fsx_list_free(names, n);
    return games;
}

static int cmp_trash(const void *a, const void *b)
{
    const TrashEntry *x = a, *y = b;
    if (x->deleted != y->deleted) return x->deleted > y->deleted ? -1 : 1;
    return strcmp(y->leaf, x->leaf);
}

int store_trash_list(Store *st, TrashEntry **out, size_t *n)
{
    char trash[FSX_PATH], **names;
    size_t cnt = 0, k = 0;
    *out = NULL;
    *n = 0;
    store_path(st, trash, sizeof trash, "profiles/.trash");
    if (!fsx_is_dir(trash)) return 0; // nothing was ever deleted
    if (fsx_list(trash, &names, &cnt) != 0) return -1;
    TrashEntry *v = calloc(cnt ? cnt : 1, sizeof *v);
    if (!v) {
        fsx_list_free(names, cnt);
        return -1;
    }
    for (size_t i = 0; i < cnt; i++) {
        char dir[FSX_PATH], pj[FSX_PATH];
        if (strlen(names[i]) >= sizeof v[0].leaf || !strncmp(names[i], TRASH_GONE, sizeof TRASH_GONE - 1) || fsx_join(dir, sizeof dir, trash, names[i]) != 0 ||
            !fsx_is_dir(dir))
            continue;
        TrashEntry *e = &v[k++];
        copy_str(e->leaf, sizeof e->leaf, names[i]);
        copy_str(e->name, sizeof e->name, names[i]);
        e->picture = -1;
        int cut = trash_split(names[i]);
        if (cut > 0 && cut < ID_MAX && names[i][0] != '.') {
            memcpy(e->id, names[i], (size_t)cut);
            e->deleted = store_parse_ts(names[i] + cut + 1);
        }
        Profile p;
        if (e->id[0] && fsx_join(pj, sizeof pj, dir, "profile.json") == 0 && load_profile_file(pj, e->id, &p) == 0) {
            copy_str(e->name, sizeof e->name, p.name);
            e->color = p.color;
            e->picture = p.picture;
            e->restorable = 1;
        }
        e->games = count_games(st, dir);
    }
    fsx_list_free(names, cnt);
    qsort(v, k, sizeof *v, cmp_trash);
    *out = v;
    *n = k;
    return 0;
}

void store_trash_tidy(Store *st)
{
    char trash[FSX_PATH], **names;
    size_t n;
    store_path(st, trash, sizeof trash, "profiles/.trash");
    if (!fsx_is_dir(trash) || fsx_list(trash, &names, &n) != 0) return;
    for (size_t i = 0; i < n; i++) {
        char p[FSX_PATH];
        if (!strncmp(names[i], TRASH_GONE, sizeof TRASH_GONE - 1) && fsx_join(p, sizeof p, trash, names[i]) == 0) fsx_rmtree(p);
    }
    fsx_list_free(names, n);
}

int store_trash_purge(Store *st, const char *leaf)
{
    if (!leaf || !leaf[0] || leaf[0] == '.' || strchr(leaf, '/')) return -1;
    char trash[FSX_PATH], src[FSX_PATH], gone[FSX_PATH], name[FSX_PATH];
    store_path(st, trash, sizeof trash, "profiles/.trash");
    if (fsx_join(src, sizeof src, trash, leaf) != 0 || !fsx_is_dir(src)) return -1;
    snprintf(name, sizeof name, TRASH_GONE "%s", leaf);
    if (fsx_join(gone, sizeof gone, trash, name) != 0) return -1;
    if (fsx_exists(gone)) fsx_rmtree(gone);
    if (rename(src, gone) != 0) return -1;
    int rc = fsx_rmtree(gone) == 0 ? 0 : -1;
    store_logf(st, "deleted profile removed for good: %s%s", leaf, rc == 0 ? "" : " (not everything could be removed yet)");
    return rc;
}

int store_profile_restore(Store *st, const char *leaf, char *id_out, int *netid_cleared)
{
    if (netid_cleared) *netid_cleared = 0;
    if (!leaf || !leaf[0] || leaf[0] == '.' || strchr(leaf, '/')) return -1;
    int cut = trash_split(leaf);
    if (cut <= 0 || cut >= ID_MAX) return -1;
    char id[ID_MAX] = "", rel[FSX_PATH], src[FSX_PATH], dst[FSX_PATH], pj[FSX_PATH];
    memcpy(id, leaf, (size_t)cut);
    snprintf(rel, sizeof rel, "profiles/.trash/%s", leaf);
    store_path(st, src, sizeof src, rel);
    if (!src[0] || !fsx_is_dir(src)) return -1;
    Profile p;
    if (fsx_join(pj, sizeof pj, src, "profile.json") != 0 || load_profile_file(pj, id, &p) != 0) return -1;
    store_profile_dir(st, id, dst, sizeof dst);
    if (!dst[0]) return -1;
    if (fsx_exists(dst)) return -2;
    // Its NetID was taken while it was away: it comes back without it. Written before the move, so that two profiles never hold
    // the same NetID, not even if the power goes right here.
    int cleared = 0;
    if (p.netid[0] && store_netid_owner(st, p.netid, NULL, NULL)) {
        p.netid[0] = 0;
        if (save_profile_file(st, src, &p) != 0) return -1;
        cleared = 1;
    }
    if (rename(src, dst) != 0) return -1;
    store_logf(st, "profile restored: %s (%s)%s", p.name, id, cleared ? "; its NetID belongs to someone else now and was removed" : "");
    if (id_out) snprintf(id_out, ID_MAX, "%s", id);
    if (netid_cleared) *netid_cleared = cleared;
    return 0;
}
