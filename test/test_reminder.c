// The in-game sign-out reminder's logic (sysmodule/source/reminder.c), on the PC: reading Washi's files, when a reminder is
// due, what it says, and the note left for Ultrahand.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../source/cJSON.h"
#include "../sysmodule/source/reminder.h"

static int g_checks, g_failed;
#define CHECK(c) CHECK_MSG(c, "%s", #c)
#define CHECK_MSG(c, ...)                                                   \
    do {                                                                    \
        g_checks++;                                                         \
        if (!(c)) {                                                         \
            g_failed++;                                                     \
            printf("  FAIL [reminder] %s:%d: ", __FILE__, __LINE__);        \
            printf(__VA_ARGS__);                                            \
            printf("\n");                                                   \
        }                                                                   \
    } while (0)

static void put(const char *dir, const char *name, const char *text)
{
    char p[512];
    snprintf(p, sizeof p, "%s/%s", dir, name);
    FILE *f = fopen(p, "wb");
    if (f) {
        fputs(text, f);
        fclose(f);
    }
}

static void wipe(const char *dir)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char p[512];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        remove(p);
    }
    closedir(d);
}

// the .notify files in dir: how many, and the text of the last one read
static int notes(const char *dir, char *text, size_t n)
{
    DIR *d = opendir(dir);
    int count = 0;
    if (!d) return -1;
    struct dirent *e;
    while ((e = readdir(d))) {
        size_t l = strlen(e->d_name);
        if (l < 7 || strcmp(e->d_name + l - 7, ".notify") != 0) continue;
        count++;
        char p[512];
        snprintf(p, sizeof p, "%s/%s", dir, e->d_name);
        FILE *f = fopen(p, "rb");
        if (f && text) {
            size_t got = fread(text, 1, n - 1, f);
            text[got] = 0;
        }
        if (f) fclose(f);
    }
    closedir(d);
    return count;
}

#define T0 1790000000LL // when Maya loaded her saves

static void test_read(const char *root)
{
    RmState s;
    wipe(root);
    CHECK_MSG(rm_read(root, &s) == -1, "no Washi on the card: nothing to read");
    put(root, "state.json", "{\"holder\": \"maya-1a2b\", \"phase\": \"idle\", \"loaded_at\": 1790000000, \"wiped\": 0}");
    CHECK(rm_read(root, &s) == 0 && !strcmp(s.holder, "maya-1a2b") && s.loaded_at == T0 && s.idle);
    CHECK_MSG(s.name[0] == 0, "her profile isn't there: no name (%s)", s.name);
    char pdir[600];
    snprintf(pdir, sizeof pdir, "%s/profiles", root);
    mkdir(pdir, 0755);
    snprintf(pdir, sizeof pdir, "%s/profiles/maya-1a2b", root);
    mkdir(pdir, 0755);
    put(pdir, "profile.json", "{\"id\": \"maya-1a2b\", \"name\": \"Maya\", \"color\": 12}");
    CHECK_MSG(rm_read(root, &s) == 0 && !strcmp(s.name, "Maya"), "her name, for the title (%s)", s.name);
    put(pdir, "profile.json", "{ broken");
    CHECK_MSG(rm_read(root, &s) == 0 && s.name[0] == 0, "a damaged profile: no name, and the rest is still read");
    wipe(pdir);
    put(pdir, "profile.json.tmp", "{\"name\": \"Maya R.\"}");
    CHECK_MSG(rm_read(root, &s) == 0 && !strcmp(s.name, "Maya R."), "a profile caught mid-save: its .tmp copy (%s)", s.name);
    wipe(pdir);
    put(root, "state.json", "{\"holder\": \"../profiles/maya-1a2b\", \"phase\": \"idle\", \"loaded_at\": 1790000000}");
    put(pdir, "profile.json", "{\"name\": \"Maya\"}");
    CHECK_MSG(rm_read(root, &s) == 0 && s.name[0] == 0, "a holder that isn't a plain id isn't looked up");
    put(root, "state.json", "{\"holder\": \"unclaimed\", \"phase\": \"idle\", \"loaded_at\": 0}");
    CHECK(rm_read(root, &s) == 0 && s.name[0] == 0);
    wipe(pdir);
    rmdir(pdir);
    snprintf(pdir, sizeof pdir, "%s/profiles", root);
    rmdir(pdir);
    put(root, "state.json", "{\"holder\": \"maya-1a2b\", \"phase\": \"idle\", \"loaded_at\": 1790000000, \"wiped\": 0}");
    CHECK_MSG(rm_read(root, &s) == 0 && s.remind_minutes == 180, "no config yet: three hours, as in the app (%d)", s.remind_minutes);
    put(root, "config.json", "{\"remind_minutes\": 75, \"sort_mode\": 1}");
    CHECK(rm_read(root, &s) == 0 && s.remind_minutes == 75);
    put(root, "config.json", "{\"remind_hours\": 5}");
    CHECK_MSG(rm_read(root, &s) == 0 && s.remind_minutes == 300, "a config from before minutes: whole hours (%d)", s.remind_minutes);
    put(root, "config.json", "{\"remind_minutes\": 0}");
    CHECK(rm_read(root, &s) == 0 && s.remind_minutes == 0);
    put(root, "config.json", "{\"remind_minutes\": 1, \"remind_restore\": 180}");
    CHECK_MSG(rm_read(root, &s) == 0 && s.remind_minutes == 1, "the app's \"real one, in a minute\" test: one minute (%d)", s.remind_minutes);
    put(root, "config.json", "{ broken");
    CHECK_MSG(rm_read(root, &s) == 0 && s.remind_minutes == 180, "a damaged config: the default");
    put(root, "state.json", "{\"holder\": \"maya-1a2b\", \"phase\": \"restoring\", \"loaded_at\": 1790000000}");
    CHECK_MSG(rm_read(root, &s) == 0 && !s.idle, "a swap half done is noticed");
    put(root, "state.json", "not json");
    CHECK(rm_read(root, &s) == -1);
    wipe(root);
    put(root, "state.json.tmp", "{\"holder\": \"noor-0000\", \"phase\": \"idle\", \"loaded_at\": 5}");
    CHECK_MSG(rm_read(root, &s) == 0 && !strcmp(s.holder, "noor-0000"), "only the .tmp copy (a power cut while saving): that one is read");
    wipe(root);
}

static void test_schedule(void)
{
    RmState s = {"maya-1a2b", T0, 1, 60, ""};
    RmMemory m = {0};
    CHECK_MSG(!rm_due(&s, &m, T0 + 59 * 60), "not before the hour");
    CHECK(rm_due(&s, &m, T0 + 60 * 60));
    CHECK_MSG(!rm_due(&s, &m, T0 + 61 * 60) && !rm_due(&s, &m, T0 + 119 * 60), "once, not every check");
    CHECK(rm_due(&s, &m, T0 + 120 * 60 + 30));
    CHECK_MSG(rm_due(&s, &m, T0 + 5 * 3600 + 37 * 60), "after a sleep: one reminder, not the three missed");
    CHECK(!rm_due(&s, &m, T0 + 5 * 3600 + 50 * 60));
    CHECK_MSG(rm_due(&s, &m, T0 + 6 * 3600), "then on the hour again");

    s.loaded_at = T0 + 6 * 3600 + 60; // "is this still you?" - "still me": the clock starts again
    CHECK(!rm_due(&s, &m, T0 + 6 * 3600 + 120));
    CHECK(rm_due(&s, &m, s.loaded_at + 3600));

    // a setting shorter than an hour repeats as often as the setting says, for as long as the same person stays loaded
    RmState q = {"maya-1a2b", T0, 1, 15, ""}; // a short one: minutes
    RmMemory mq = {0};
    CHECK(rm_due(&q, &mq, T0 + 15 * 60));
    CHECK_MSG(!rm_due(&q, &mq, T0 + 20 * 60) && rm_due(&q, &mq, T0 + 30 * 60) && rm_due(&q, &mq, T0 + 45 * 60), "after the first, every quarter hour");
    CHECK_MSG(rm_due(&q, &mq, T0 + 3 * 3600 + 7 * 60) && !rm_due(&q, &mq, T0 + 3 * 3600 + 10 * 60) && rm_due(&q, &mq, T0 + 3 * 3600 + 15 * 60),
              "after a sleep, one, and then the quarter hours go on");
    RmState f = {"maya-1a2b", T0, 1, 45, ""};
    RmMemory mf = {0};
    CHECK_MSG(!rm_due(&f, &mf, T0 + 44 * 60) && rm_due(&f, &mf, T0 + 45 * 60) && !rm_due(&f, &mf, T0 + 80 * 60) && rm_due(&f, &mf, T0 + 90 * 60) && rm_due(&f, &mf, T0 + 135 * 60),
              "45 minutes: every 45 minutes");
    RmState h3 = {"maya-1a2b", T0, 1, 180, ""};
    RmMemory m3 = {0};
    CHECK_MSG(!rm_due(&h3, &m3, T0 + 3600) && rm_due(&h3, &m3, T0 + 3 * 3600) && !rm_due(&h3, &m3, T0 + 3 * 3600 + 1800) && rm_due(&h3, &m3, T0 + 4 * 3600) && rm_due(&h3, &m3, T0 + 5 * 3600),
              "a long setting (3 hours): the first at 3 hours, then every hour, not every 3 hours");
    CHECK(rm_every(1) == 60 && rm_every(15) == 900 && rm_every(60) == 3600 && rm_every(180) == 3600 && rm_every(72 * 60) == 3600);

    // the real one set to a minute (Sign-out reminder, 0 hours 01 min): Maya had been on for hours (two reminders shown), then the setting
    // is made one minute and her clock starts again (she is swapped in again); the note comes a minute after that, and then every minute
    RmState t = {"maya-1a2b", T0, 1, 60, ""};
    RmMemory mt = {0};
    CHECK(rm_due(&t, &mt, T0 + 3600) && rm_due(&t, &mt, T0 + 7200));
    int64_t asked = T0 + 7200 + 600;
    t.remind_minutes = 1;
    t.loaded_at = asked;
    CHECK_MSG(!rm_due(&t, &mt, asked + 30), "not at once");
    CHECK_MSG(rm_due(&t, &mt, asked + 60), "a minute later");
    CHECK_MSG(!rm_due(&t, &mt, asked + 90) && rm_due(&t, &mt, asked + 120) && !rm_due(&t, &mt, asked + 150) && rm_due(&t, &mt, asked + 180), "and again every minute");
    int due = 0;
    for (int s2 = 210; s2 <= 600; s2 += 30) due += rm_due(&t, &mt, asked + s2); // looked at every half minute
    CHECK_MSG(due == 7, "over the next 6.5 minutes, one a minute (%d)", due);
    CHECK_MSG(rm_due(&t, &mt, asked + 1800) && !rm_due(&t, &mt, asked + 1830), "after a sleep, one");
    t.remind_minutes = 180; // the app was opened again: the usual three hours are back, counted from the test
    CHECK_MSG(!rm_due(&t, &mt, asked + 3600 + 60) && !rm_due(&t, &mt, asked + 2 * 3600), "and then nothing until the usual time");
    CHECK(rm_due(&t, &mt, asked + 3 * 3600));

    // the setting is changed while someone is loaded: the count starts again from the new one
    RmState c = {"maya-1a2b", T0, 1, 60, ""};
    RmMemory mc = {0};
    CHECK(rm_due(&c, &mc, T0 + 3600) && rm_due(&c, &mc, T0 + 7200));
    c.remind_minutes = 180; // an hour made three, two hours in: the next is at three hours, not four
    CHECK_MSG(!rm_due(&c, &mc, T0 + 7200 + 60) && rm_due(&c, &mc, T0 + 3 * 3600), "a longer time: at that time");
    c.remind_minutes = 30; // made shorter than they have been on: one now, then every half hour from the new first one
    CHECK_MSG(rm_due(&c, &mc, T0 + 3 * 3600 + 60) && !rm_due(&c, &mc, T0 + 3 * 3600 + 120), "a shorter time that has passed: one now");
    CHECK(rm_due(&c, &mc, T0 + 3 * 3600 + 30 * 60) && rm_due(&c, &mc, T0 + 4 * 3600));

    RmMemory fresh = {0};
    RmState off = s, guest = s, busy = s, never = s;
    off.remind_minutes = 0;
    snprintf(guest.holder, sizeof guest.holder, "unclaimed");
    busy.idle = 0;
    never.loaded_at = 0;
    int64_t late = T0 + 50 * 3600;
    CHECK_MSG(!rm_due(&off, &fresh, late), "turned off: never");
    CHECK_MSG(!rm_due(&guest, &fresh, late), "nobody's saves are loaded (a guest): never");
    CHECK_MSG(!rm_due(&busy, &fresh, late), "a swap half done: not now");
    CHECK_MSG(!rm_due(&never, &fresh, late), "no time loaded: never");
}

static const unsigned char ICON[16] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16}; // (any bytes will do)

static long file_size(const char *path)
{
    struct stat sb;
    return stat(path, &sb) == 0 ? (long)sb.st_size : -1;
}

static void test_notify(const char *base)
{
    char uh[512], dir[512 + 16], text[1024] = "", icon[600];
    snprintf(uh, sizeof uh, "%s/ultrahand", base);
    snprintf(dir, sizeof dir, "%s/notifications", uh);
    snprintf(icon, sizeof icon, "%s/assets/notifications/washi.rgba", uh);
    RmIcon ic = {ICON, sizeof ICON};
    CHECK_MSG(rm_notify(uh, "Alex", "hello", T0, &ic) == -1 && notes(dir, NULL, 0) == -1 && file_size(icon) == -1,
              "no Ultrahand (no folder): nothing is written, no folder made");
    mkdir(uh, 0755);
    mkdir(dir, 0755);
    put(dir, "other-app.notify", "{\"text\": \"not ours\"}");
    put(dir, "save-locker-1.notify", "{\"text\": \"from before the rename\"}");
    CHECK(rm_notify(uh, "Alex", RM_TEXT, T0, &ic) == 0);
    CHECK_MSG(notes(dir, NULL, 0) == 2, "ours (the one from before the rename goes), next to another app's");
    CHECK_MSG(file_size(icon) == (long)sizeof ICON, "the icon is put where Ultrahand looks for it (%ld)", file_size(icon));
    char p[600];
    snprintf(p, sizeof p, "%s/washi-%lld.notify", dir, T0);
    FILE *f = fopen(p, "rb");
    CHECK(f != NULL);
    if (f) {
        size_t got = fread(text, 1, sizeof text - 1, f);
        text[got] = 0;
        fclose(f);
    }
    cJSON *j = cJSON_Parse(text);
    const cJSON *t = cJSON_GetObjectItemCaseSensitive(j, "text"), *ti = cJSON_GetObjectItemCaseSensitive(j, "title");
    CHECK_MSG(cJSON_IsString(t) && !strcmp(t->valuestring, "Don't forget to sign out when you're done playing!") && cJSON_IsString(ti) &&
                  !strcmp(ti->valuestring, "Alex") && cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j, "font_size")) &&
                  cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j, "priority")) &&
                  cJSON_IsNumber(cJSON_GetObjectItemCaseSensitive(j, "duration")),
              "Ultrahand's fields: %s", text);
    cJSON_Delete(j);
    CHECK(rm_notify(uh, "", "the next one", T0 + 3600, &ic) == 0);
    {
        char last[1024] = "";
        CHECK(notes(dir, NULL, 0) == 2);
        snprintf(p, sizeof p, "%s/washi-%lld.notify", dir, T0 + 3600);
        FILE *g = fopen(p, "rb");
        if (g) {
            last[fread(last, 1, sizeof last - 1, g)] = 0;
            fclose(g);
        }
        CHECK_MSG(strstr(last, "\"title\":\t\"Washi\"") != NULL, "no name: Washi (%s)", last);
    }
    CHECK_MSG(notes(dir, NULL, 0) == 2, "an earlier one of ours that wasn't shown is replaced, not piled up; the other app's stays");
    snprintf(p, sizeof p, "%s/other-app.notify", dir);
    CHECK(access(p, F_OK) == 0);
    snprintf(p, sizeof p, "%s/washi-%lld.part", dir, T0 + 3600);
    CHECK_MSG(access(p, F_OK) != 0, "no half-written file is left behind");
    put(uh, "assets/notifications/washi.rgba", "short");
    CHECK(rm_notify(uh, NULL, "again", T0 + 7200, &ic) == 0);
    CHECK_MSG(file_size(icon) == (long)sizeof ICON, "a damaged icon is put right");
    put(uh, "assets/notifications/washi.rgba", "an older icon...");  // as long as ICON, other bytes
    CHECK(file_size(icon) == (long)sizeof ICON && rm_notify(uh, NULL, "and again", T0 + 10800, &ic) == 0);
    {
        unsigned char now[sizeof ICON + 1] = {0};
        FILE *g = fopen(icon, "rb");
        size_t got = g ? fread(now, 1, sizeof now, g) : 0;
        if (g) fclose(g);
        CHECK_MSG(got == sizeof ICON && !memcmp(now, ICON, sizeof ICON), "an older icon of the same size is replaced by this one");
    }
    remove(icon);
    snprintf(p, sizeof p, "%s/assets/notifications", uh);
    rmdir(p);
    snprintf(p, sizeof p, "%s/assets", uh);
    rmdir(p);
    wipe(dir);
    rmdir(dir);
    rmdir(uh);
}

static void test_test_request(const char *base)
{
    char root[512], uh[512], dir[512 + 16], p[600];
    snprintf(root, sizeof root, "%s/sl", base);
    snprintf(uh, sizeof uh, "%s/uh", base);
    snprintf(dir, sizeof dir, "%s/notifications", uh);
    mkdir(root, 0755);
    char pdir[600];
    snprintf(pdir, sizeof pdir, "%s/profiles", root);
    mkdir(pdir, 0755);
    snprintf(pdir, sizeof pdir, "%s/profiles/alex-0001", root);
    mkdir(pdir, 0755);
    put(pdir, "profile.json", "{\"name\": \"Alex\"}");
    put(root, "state.json", "{\"holder\": \"alex-0001\", \"phase\": \"idle\", \"loaded_at\": 1790000000}");
    RmIcon ic = {ICON, sizeof ICON};
    CHECK_MSG(rm_test_answer(root, uh, T0, &ic) == 0, "nothing asked: nothing done");
    put(root, RM_TEST_ASK, "test");
    CHECK_MSG(rm_test_answer(root, uh, T0, &ic) == 1, "asked: answered");
    snprintf(p, sizeof p, "%s/%s", root, RM_TEST_DONE);
    char got[32] = "";
    FILE *f = fopen(p, "rb");
    if (f) {
        size_t n = fread(got, 1, sizeof got - 1, f);
        got[n] = 0;
        fclose(f);
    }
    CHECK_MSG(!strcmp(got, "no-ultrahand"), "without Ultrahand's folder it says so (%s)", got);
    mkdir(uh, 0755);
    mkdir(dir, 0755);
    put(root, RM_TEST_ASK, "test");
    CHECK(rm_test_answer(root, uh, T0 + 5, &ic) == 1);
    f = fopen(p, "rb");
    got[0] = 0;
    if (f) {
        size_t n = fread(got, 1, sizeof got - 1, f);
        got[n] = 0;
        fclose(f);
    }
    char text[512] = "";
    CHECK_MSG(!strcmp(got, "ok") && notes(dir, text, sizeof text) == 1 && strstr(text, RM_TEXT),
              "with it: the real note is shown and the answer is ok (%s)", got);
    CHECK_MSG(strstr(text, "\"Alex\"") != NULL, "titled with whoever is loaded (%s)", text);
    snprintf(p, sizeof p, "%s/%s", root, RM_TEST_ASK);
    CHECK_MSG(access(p, F_OK) != 0, "the request is taken away (one test, one note)");
    CHECK_MSG(rm_test_answer(root, uh, T0 + 10, &ic) == 0, "and not answered twice");
    snprintf(p, sizeof p, "%s/assets/notifications/washi.rgba", uh);
    remove(p);
    snprintf(p, sizeof p, "%s/assets/notifications", uh);
    rmdir(p);
    snprintf(p, sizeof p, "%s/assets", uh);
    rmdir(p);
    wipe(dir);
    rmdir(dir);
    rmdir(uh);
    wipe(pdir);
    rmdir(pdir);
    snprintf(pdir, sizeof pdir, "%s/profiles", root);
    rmdir(pdir);
    wipe(root);
    rmdir(root);
}

// the repeating test: while its file is there a note is due at once and then every 30 seconds (looked at every five), whoever is loaded and whatever the
// setting; with the file gone it forgets
static void test_repeat(const char *base)
{
    char root[512], uh[512], dir[512 + 16], p[600];
    snprintf(root, sizeof root, "%s/rp", base);
    snprintf(uh, sizeof uh, "%s/uhr", base);
    snprintf(dir, sizeof dir, "%s/notifications", uh);
    mkdir(root, 0755);
    RmRepeat m = {0};
    CHECK_MSG(rm_repeat_due(root, &m, T0) == 0 && m.last == 0, "the repeating test is off: nothing is due");
    put(root, "state.json", "{\"holder\": \"unclaimed\", \"phase\": \"idle\", \"loaded_at\": 0}"); // (nobody loaded; the setting is off too)
    put(root, "config.json", "{\"remind_minutes\": 0}");
    put(root, RM_REPEAT_FLAG, "30");
    CHECK_MSG(rm_repeat_due(root, &m, T0) == 1, "turned on: the first is due at once, with nobody loaded and the reminder off");
    int due = 0, first_again = -1;
    for (int s = 5; s <= 120; s += 5) { // looked at every five seconds
        if (rm_repeat_due(root, &m, T0 + s)) {
            due++;
            if (first_again < 0) first_again = s;
        }
    }
    CHECK_MSG(first_again >= 27 && first_again <= 30, "the next comes about 30 seconds later (at %d)", first_again);
    CHECK_MSG(due >= 3 && due <= 4, "and so on: %d more in the next 120 seconds", due);
    // the note itself: the test's own title and text, written where Ultrahand looks
    mkdir(uh, 0755);
    mkdir(dir, 0755);
    RmIcon ic = {ICON, sizeof ICON};
    char text[512] = "";
    CHECK(rm_notify(uh, RM_REPEAT_TITLE, RM_REPEAT_TEXT, T0, &ic) == 0);
    CHECK_MSG(notes(dir, text, sizeof text) == 1 && strstr(text, RM_REPEAT_TEXT) && strstr(text, RM_REPEAT_TITLE), "it says it is the test (%s)", text);
    // turned off: it forgets, and when it is turned on again the first comes at once
    snprintf(p, sizeof p, "%s/%s", root, RM_REPEAT_FLAG);
    remove(p);
    CHECK_MSG(rm_repeat_due(root, &m, T0 + 200) == 0 && m.last == 0, "turned off: nothing is due and it is forgotten");
    put(root, RM_REPEAT_FLAG, "30");
    CHECK_MSG(rm_repeat_due(root, &m, T0 + 203) == 1, "turned on again: the first is due at once");
    CHECK_MSG(rm_repeat_due(root, &m, T0 + 208) == 0, "and the next waits");
    CHECK_MSG(rm_repeat_due(root, &m, T0 + 100) == 1, "a clock put back does not hold it up");
    wipe(dir);
    rmdir(dir);
    snprintf(p, sizeof p, "%s/assets/notifications/washi.rgba", uh);
    remove(p);
    snprintf(p, sizeof p, "%s/assets/notifications", uh);
    rmdir(p);
    snprintf(p, sizeof p, "%s/assets", uh);
    rmdir(p);
    rmdir(uh);
    wipe(root);
    rmdir(root);
}

int main(void)
{
    char base[] = "/tmp/rm-test-XXXXXX";
    if (!mkdtemp(base)) return 1;
    test_read(base);
    test_schedule();
    test_notify(base);
    test_test_request(base);
    test_repeat(base);
    wipe(base);
    rmdir(base);
    printf("%d reminder checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
