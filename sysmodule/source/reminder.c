#include "reminder.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "cJSON.h"

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    char *buf = NULL;
    size_t cap = 0, len = 0;
    for (;;) {
        if (len + 1024 + 1 > cap) {
            char *nb = realloc(buf, cap = cap ? cap * 2 : 4096);
            if (!nb || cap > 1 << 20) { // Washi's files are a few hundred bytes
                free(nb ? nb : buf);
                fclose(f);
                return NULL;
            }
            buf = nb;
        }
        size_t got = fread(buf + len, 1, 1024, f);
        len += got;
        if (got < 1024) break;
    }
    fclose(f);
    buf[len] = 0;
    return buf;
}

static cJSON *read_json(const char *root, const char *name)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, name);
    char *text = read_text(path);
    if (!text) { // Washi writes through a .tmp file: if the power went at the wrong moment, that is the one to read
        snprintf(path, sizeof path, "%s/%s.tmp", root, name);
        text = read_text(path);
    }
    if (!text) return NULL;
    cJSON *j = cJSON_Parse(text);
    free(text);
    if (j && !cJSON_IsObject(j)) {
        cJSON_Delete(j);
        j = NULL;
    }
    return j;
}

static double num(const cJSON *o, const char *key, double def)
{
    const cJSON *v = cJSON_GetObjectItemCaseSensitive(o, key);
    return cJSON_IsNumber(v) ? v->valuedouble : def;
}

int rm_read(const char *root, RmState *out)
{
    memset(out, 0, sizeof *out);
    cJSON *st = read_json(root, "state.json");
    if (!st) return -1;
    const cJSON *h = cJSON_GetObjectItemCaseSensitive(st, "holder"), *ph = cJSON_GetObjectItemCaseSensitive(st, "phase");
    if (cJSON_IsString(h)) snprintf(out->holder, sizeof out->holder, "%s", h->valuestring);
    out->loaded_at = (int64_t)num(st, "loaded_at", 0);
    out->idle = !cJSON_IsString(ph) || !strcmp(ph->valuestring, "idle");
    cJSON_Delete(st);
    // their name, for the note's title (a profile id is letters, digits and '-': anything else isn't looked up)
    int plain = out->holder[0] && strcmp(out->holder, "unclaimed") != 0;
    for (const char *c = out->holder; *c && plain; c++)
        plain = (*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_';
    if (plain) {
        char rel[96];
        snprintf(rel, sizeof rel, "profiles/%.63s/profile.json", out->holder);
        cJSON *pf = read_json(root, rel);
        const cJSON *nm = pf ? cJSON_GetObjectItemCaseSensitive(pf, "name") : NULL;
        if (cJSON_IsString(nm)) snprintf(out->name, sizeof out->name, "%s", nm->valuestring);
        cJSON_Delete(pf);
    }
    // the setting, as Washi reads it: minutes since 1.2, whole hours before; 3 hours when it was never set
    cJSON *cf = read_json(root, "config.json");
    int remind = cf ? (int)num(cf, "remind_minutes", num(cf, "remind_hours", 3) * 60) : 180;
    out->remind_minutes = remind < 0 || remind > 72 * 60 ? 180 : remind;
    cJSON_Delete(cf);
    return 0;
}

int64_t rm_every(int remind_minutes)
{
    int64_t e = (int64_t)remind_minutes * 60;
    return e > RM_EVERY_S ? RM_EVERY_S : e < 60 ? 60 : e;
}

int rm_due(const RmState *s, RmMemory *m, int64_t now)
{
    if (!s->holder[0] || !strcmp(s->holder, "unclaimed") || s->remind_minutes <= 0 || s->loaded_at <= 0 || !s->idle) return 0;
    if (s->loaded_at != m->session || s->remind_minutes != m->minutes) { // someone else, "still me", or a new setting
        m->session = s->loaded_at;
        m->minutes = s->remind_minutes;
        m->sent = 0;
    }
    int64_t first = s->loaded_at + (int64_t)s->remind_minutes * 60, every = rm_every(s->remind_minutes);
    if (now < first + (int64_t)m->sent * every) return 0;
    m->sent = (int)((now - first) / every) + 1; // the latest one that was due; any missed before it are skipped
    return 1;
}

int rm_repeat_due(const char *root, RmRepeat *m, int64_t now)
{
    char path[512];
    snprintf(path, sizeof path, "%s/%s", root, RM_REPEAT_FLAG);
    struct stat sb;
    if (stat(path, &sb) != 0) {
        m->last = 0;
        return 0;
    }
    // (it looks every five seconds: one a little short of the time is the one that was meant; a clock that was put back does not hold it up)
    if (m->last && now >= m->last && now - m->last + 3 < RM_REPEAT_EVERY_S) return 0;
    m->last = now;
    return 1;
}

int rm_test_answer(const char *root, const char *ultrahand, int64_t now, const RmIcon *icon)
{
    char ask[512], done[512], part[520];
    snprintf(ask, sizeof ask, "%s/%s", root, RM_TEST_ASK);
    snprintf(done, sizeof done, "%s/%s", root, RM_TEST_DONE);
    struct stat sb;
    if (stat(ask, &sb) != 0) return 0;
    remove(ask);
    RmState st;
    if (rm_read(root, &st) != 0) st.name[0] = 0;
    const char *answer = rm_notify(ultrahand, st.name, RM_TEXT, now, icon) == 0 ? "ok" : "no-ultrahand";
    snprintf(part, sizeof part, "%s.part", done); // (the app reads it whole or not at all)
    FILE *f = fopen(part, "wb");
    if (f) {
        fputs(answer, f);
        if (fclose(f) == 0) rename(part, done);
        else remove(part);
    }
    return 1;
}

// whether the file at `path` holds exactly these n bytes
static int same_file(const char *path, const unsigned char *bytes, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    unsigned char buf[1024];
    size_t at = 0, got;
    int same = 1;
    while (same && (got = fread(buf, 1, sizeof buf, f)) > 0) {
        same = at + got <= n && !memcmp(buf, bytes + at, got);
        at += got;
    }
    fclose(f);
    return same && at == n;
}

// the icon, unless that very icon is there already (a damaged one, or an older Washi's, is replaced): written under another
// name first, so it is never seen half done
static void put_icon(const char *ultrahand, const RmIcon *icon)
{
    if (!icon || !icon->rgba || !icon->n) return;
    char dir[512], path[600], tmp[610];
    snprintf(dir, sizeof dir, "%s/assets", ultrahand);
    mkdir(dir, 0755);
    snprintf(dir, sizeof dir, "%s/assets/notifications", ultrahand);
    mkdir(dir, 0755);
    snprintf(path, sizeof path, "%s/washi.rgba", dir);
    if (same_file(path, icon->rgba, icon->n)) return;
    snprintf(tmp, sizeof tmp, "%s.part", path);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(icon->rgba, 1, icon->n, f) == icon->n;
    if (f && fclose(f) != 0) ok = 0;
    if (ok) {
        remove(path);
        ok = rename(tmp, path) == 0;
    }
    if (!ok) remove(tmp);
}

int rm_notify(const char *ultrahand, const char *title, const char *msg, int64_t now, const RmIcon *icon)
{
    char dir[512];
    snprintf(dir, sizeof dir, "%s/notifications", ultrahand);
    struct stat sb;
    if (stat(dir, &sb) != 0 || !S_ISDIR(sb.st_mode)) return -1;
    put_icon(ultrahand, icon);
    DIR *d = opendir(dir); // one of ours at a time: an earlier one Ultrahand hasn't shown yet goes
    if (d) {
        struct dirent *e;
        while ((e = readdir(d))) {
            // (ours, and the ones the app left before it was renamed from "save-locker")
            if (strncmp(e->d_name, "washi-", 6) != 0 && strncmp(e->d_name, "save-locker-", 12) != 0) continue;
            char old[512 + 256 + 2];
            snprintf(old, sizeof old, "%s/%s", dir, e->d_name);
            remove(old);
        }
        closedir(d);
    }
    cJSON *j = cJSON_CreateObject();
    cJSON_AddStringToObject(j, "title", title && title[0] ? title : RM_TITLE);
    cJSON_AddStringToObject(j, "text", msg);
    cJSON_AddNumberToObject(j, "font_size", 24);
    cJSON_AddNumberToObject(j, "priority", 10);
    cJSON_AddNumberToObject(j, "duration", 6000); // ms (Ultrahand's own is 4 s)
    char *text = cJSON_Print(j);
    cJSON_Delete(j);
    if (!text) return -1;
    // written under another name first: Ultrahand only picks up .notify files, so it never sees half of one
    char tmp[512 + 48], path[512 + 48];
    snprintf(tmp, sizeof tmp, "%s/washi-%lld.part", dir, (long long)now);
    snprintf(path, sizeof path, "%s/washi-%lld.notify", dir, (long long)now);
    FILE *f = fopen(tmp, "wb");
    int ok = f && fwrite(text, 1, strlen(text), f) == strlen(text);
    if (f && fclose(f) != 0) ok = 0;
    free(text);
    if (!ok || rename(tmp, path) != 0) {
        remove(tmp);
        return -1;
    }
    return 0;
}
