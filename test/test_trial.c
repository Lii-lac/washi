// An update's trial and going back (source/trial.c), on real files in a scratch folder: a new version that never gets going twice is swapped back out at the
// third start, with a note; one that gets going stays; nothing to go back to means carrying on; swaps go both ways; versions are read from the NROs' NACPs.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "trial.h"

static int g_checks, g_failed;
#define CHECK(c, ...)                                                                                                                                          \
    do {                                                                                                                                                       \
        g_checks++;                                                                                                                                            \
        if (!(c)) {                                                                                                                                            \
            g_failed++;                                                                                                                                        \
            printf("FAIL %s:%d: ", __FILE__, __LINE__);                                                                                                        \
            printf(__VA_ARGS__);                                                                                                                               \
            printf("\n");                                                                                                                                      \
        }                                                                                                                                                      \
    } while (0)

static char g_dir[256], g_self[300];

static void path(char *out, const char *ext) { snprintf(out, 320, "%s%s", g_self, ext); }

static int there(const char *ext)
{
    char p[320];
    path(p, ext);
    struct stat st;
    return stat(p, &st) == 0;
}

// a small NRO: the header's size at 0x18, the assets after it with the NACP's offset and size, the display version at 0x3060 in it
static void write_nro(const char *ext, const char *ver)
{
    enum { NRO = 0x100, ASET = 0x38, NACP = 0x4000 };
    size_t n = NRO + ASET + NACP;
    unsigned char *b = calloc(1, n);
    memcpy(b + 0x10, "NRO0", 4);
    b[0x18] = NRO & 0xFF;
    b[0x19] = NRO >> 8;
    memcpy(b + NRO, "ASET", 4);
    b[NRO + 0x18] = ASET;
    b[NRO + 0x20] = NACP & 0xFF;
    b[NRO + 0x21] = NACP >> 8;
    snprintf((char *)b + NRO + ASET + 0x3060, 16, "%s", ver);
    char p[320];
    path(p, ext);
    FILE *f = fopen(p, "wb");
    fwrite(b, 1, n, f);
    fclose(f);
    free(b);
}

static void ver_of(const char *ext, char *ver)
{
    char p[320];
    path(p, ext);
    if (nro_version(p, ver, 32) != 0) snprintf(ver, 32, "?");
}

static void clean(void)
{
    static const char *const E[] = {"", ".bak", ".old", ".trial", ".rolledback", ".new"};
    for (int i = 0; i < 6; i++) {
        char p[320];
        path(p, E[i]);
        remove(p);
    }
}

int main(void)
{
    snprintf(g_dir, sizeof g_dir, "/tmp/washi-trial-%d", (int)getpid());
    mkdir(g_dir, 0755);
    snprintf(g_self, sizeof g_self, "%s/washi.nro", g_dir);
    char v[32];

    // no update: a start is a start
    clean();
    write_nro("", "1.0.0");
    CHECK(trial_start(g_self) == 0 && !there(".trial"), "no trial: carry on, nothing written");
    CHECK(trial_kept(g_self, v, sizeof v) == 0, "nothing kept");
    CHECK(trial_note(g_self, v, sizeof v) == 0 && v[0] == 0, "no note");
    ver_of("", v);
    CHECK(!strcmp(v, "1.0.0"), "the version is read from the NACP (%s)", v);

    // an update that never gets going: two starts, then the third puts the kept one back
    write_nro(".bak", "1.0.0");
    write_nro("", "1.1.0");
    trial_begin(g_self);
    CHECK(trial_kept(g_self, v, sizeof v) == 1 && !strcmp(v, "1.0.0"), "1.0.0 is kept (%s)", v);
    CHECK(trial_start(g_self) == 0, "first start on trial: carry on");
    CHECK(trial_start(g_self) == 0, "second start (the first never got going): carry on");
    CHECK(trial_start(g_self) == 1, "third start: the kept one is put back");
    ver_of("", v);
    CHECK(!strcmp(v, "1.0.0"), "1.0.0 is in place (%s)", v);
    ver_of(".bak", v);
    CHECK(!strcmp(v, "1.1.0"), "1.1.0 is kept in its place (%s)", v);
    CHECK(!there(".trial") && !there(".old"), "the trial is over, nothing left over");
    CHECK(trial_start(g_self) == 0, "the next start is the usual one");
    CHECK(trial_note(g_self, v, sizeof v) == 1 && !strcmp(v, "1.1.0"), "the note names the one that didn't start (%s)", v);
    CHECK(trial_note(g_self, v, sizeof v) == 0, "said once");

    // an update that gets going stays, however many starts follow
    clean();
    write_nro(".bak", "1.0.0");
    write_nro("", "1.1.0");
    trial_begin(g_self);
    CHECK(trial_start(g_self) == 0, "first start on trial");
    trial_ok(g_self);
    CHECK(!there(".trial"), "it got going: the trial is over");
    for (int i = 0; i < 4; i++) CHECK(trial_start(g_self) == 0, "start %d: carry on", i);
    ver_of("", v);
    CHECK(!strcmp(v, "1.1.0"), "1.1.0 stays (%s)", v);

    // an update with nothing kept to go back to: carry on with it
    clean();
    write_nro("", "1.1.0");
    trial_begin(g_self);
    trial_start(g_self);
    trial_start(g_self);
    CHECK(trial_start(g_self) == 0 && !there(".trial") && there(""), "nothing to go back to: carry on, the trial dropped");
    CHECK(trial_note(g_self, v, sizeof v) == 0, "no note");

    // by hand: swaps go both ways, and end a trial
    clean();
    write_nro(".bak", "1.0.0");
    write_nro("", "1.1.0");
    trial_begin(g_self);
    CHECK(trial_swap(g_self) == 0 && !there(".trial"), "a swap ends the trial");
    ver_of("", v);
    CHECK(!strcmp(v, "1.0.0"), "back on 1.0.0 (%s)", v);
    CHECK(trial_swap(g_self) == 0, "and forward again");
    ver_of("", v);
    CHECK(!strcmp(v, "1.1.0"), "on 1.1.0 (%s)", v);
    remove(strcat(strcpy((char[320]){0}, g_self), ".bak"));
    CHECK(trial_swap(g_self) == -1 && there(""), "nothing kept: no swap, the app stays");

    // a kept file that is no NRO: kept, version unknown
    clean();
    write_nro("", "1.1.0");
    {
        char p[320];
        path(p, ".bak");
        FILE *f = fopen(p, "wb");
        fputs("not an nro", f);
        fclose(f);
    }
    CHECK(trial_kept(g_self, v, sizeof v) == 1 && v[0] == 0, "kept, its version unknown (%s)", v);

    clean();
    rmdir(g_dir);
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed != 0;
}
