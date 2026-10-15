// The console's own profile pictures (source/gfx/avatar_src.c): the Yaz0 unpacker on good data, on every kind of bad data (it must refuse, never read or write
// outside its buffers: the sanitizers watch), and the folder reader on pretend console files made by avatar_fixture.h. With WASHI_AVATAR_DIR=<a folder with chara/ in
// it, e.g. the console's data extracted from firmware> it also reads every real picture.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "avatar_fixture.h"
#include "avatar_src.h"

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

static const uint8_t HDR6[16] = {'Y', 'a', 'z', '0', 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0, 0};

// header + body, for a picture of `size` bytes
static size_t yaz(uint8_t *out, uint32_t size, const uint8_t *body, size_t n)
{
    memcpy(out, HDR6, 16);
    out[4] = (uint8_t)(size >> 24), out[5] = (uint8_t)(size >> 16), out[6] = (uint8_t)(size >> 8), out[7] = (uint8_t)size;
    memcpy(out + 16, body, n);
    return 16 + n;
}

static void test_unpack(void)
{
    uint8_t in[64], out[64];
    // eight bytes as they are
    static const uint8_t lit[] = {0xFF, 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h'};
    size_t n = yaz(in, 8, lit, sizeof lit);
    CHECK(yaz0_unpack(in, n, out, 8) == 0 && !memcmp(out, "abcdefgh", 8), "literals");
    // 'a', then a copy of 5 from one back (it overlaps what it copies): aaaaaa
    static const uint8_t run[] = {0x80, 'a', 0x30, 0x00};
    n = yaz(in, 6, run, sizeof run);
    CHECK(yaz0_unpack(in, n, out, 6) == 0 && !memcmp(out, "aaaaaa", 6), "an overlapping copy repeats the byte");
    // 'x', then the long form: a copy of 3 + 0x12 = 21 from one back
    static const uint8_t longc[] = {0x80, 'x', 0x00, 0x00, 0x03};
    n = yaz(in, 22, longc, sizeof longc);
    CHECK(yaz0_unpack(in, n, out, 22) == 0 && out[0] == 'x' && out[21] == 'x', "the long form");
    // 'a','b','c' then a copy of 3 from three back: abcabc
    static const uint8_t back[] = {0xE0, 'a', 'b', 'c', 0x10, 0x02};
    n = yaz(in, 6, back, sizeof back);
    CHECK(yaz0_unpack(in, n, out, 6) == 0 && !memcmp(out, "abcabc", 6), "a copy from further back");

    // everything wrong
    n = yaz(in, 8, lit, sizeof lit);
    in[0] = 'X';
    CHECK(yaz0_unpack(in, n, out, 8) == -1, "not Yaz0");
    in[0] = 'Y';
    CHECK(yaz0_unpack(in, n, out, 9) == -1 && yaz0_unpack(in, n, out, 7) == -1, "it says another size");
    CHECK(yaz0_unpack(in, n - 1, out, 8) == -1, "it runs out in the middle of literals");
    CHECK(yaz0_unpack(in, 15, out, 8) == -1 && yaz0_unpack(in, 0, out, 8) == -1 && yaz0_unpack(NULL, 20, out, 8) == -1, "too short to be anything");
    static const uint8_t early[] = {0x00, 0x00, 0x00};
    n = yaz(in, 6, early, sizeof early);
    CHECK(yaz0_unpack(in, n, out, 6) == -1, "a copy from before the start");
    static const uint8_t toolong[] = {0x80, 'a', 0x70, 0x00};
    n = yaz(in, 4, toolong, sizeof toolong);
    CHECK(yaz0_unpack(in, n, out, 4) == -1, "a copy longer than what is left to make");
    static const uint8_t cut[] = {0x80, 'a', 0x30};
    n = yaz(in, 6, cut, sizeof cut);
    CHECK(yaz0_unpack(in, n, out, 6) == -1, "a copy cut off after its first byte");
    static const uint8_t cut2[] = {0x80, 'a', 0x00, 0x00};
    n = yaz(in, 30, cut2, sizeof cut2);
    CHECK(yaz0_unpack(in, n, out, 30) == -1, "the long form cut off");

    // random bodies behind a good header: refuse or finish, never go outside the buffers (the sanitizers watch)
    srand(7);
    int done = 0;
    for (int t = 0; t < 4000; t++) {
        uint8_t body[48];
        size_t bl = (size_t)(rand() % 48);
        for (size_t i = 0; i < bl; i++) body[i] = (uint8_t)rand();
        uint32_t want = (uint32_t)(rand() % 60);
        n = yaz(in, want, body, bl);
        uint8_t *exact = malloc(want ? want : 1);
        if (yaz0_unpack(in, n, exact, want) == 0) done++;
        free(exact);
    }
    CHECK(done >= 0, "random streams went through (%d unpacked)", done);
}

static void test_roundtrip(void)
{
    uint8_t *im = fx_image(3), *packed = malloc(256 * 256 * 4 + 256 * 256 / 2 + 64), *back = malloc(256 * 256 * 4);
    size_t n = fx_yaz0_pack(im, 256 * 256 * 4, packed);
    CHECK(n < 256 * 256 * 4 / 4, "the stripes pack well (%zu bytes)", n);
    CHECK(yaz0_unpack(packed, n, back, 256 * 256 * 4) == 0 && !memcmp(im, back, 256 * 256 * 4), "a whole picture comes back as it went in");
    for (int cut = 0; cut < 4; cut++) // (a damaged copy is refused, not half-used)
        CHECK(yaz0_unpack(packed, n - 1 - (size_t)cut * 100, back, 256 * 256 * 4) == -1, "truncated by %d", 1 + cut * 100);
    free(im);
    free(packed);
    free(back);
}

static int px_is(const uint8_t *px, int k, int x, int y)
{
    uint8_t e[4];
    fx_pixel(k, x, y, e);
    return !memcmp(px + ((size_t)y * 256 + x) * 4, e, 4);
}

static void test_folder(void)
{
    char dir[300];
    fx_dir(dir, sizeof dir, "plain");
    static const uint32_t ids[] = {3, 1, 2, 5}; // id 4 isn't named, id 5 has no file
    fx_table(dir, ids, 4);
    fx_picture(dir, 1);
    fx_picture(dir, 2);
    fx_picture(dir, 3);
    AvatarSrc *s = avsrc_open(dir);
    CHECK(s != NULL, "opens");
    CHECK(avsrc_count(s) == 5, "five pictures: the highest id (%d)", avsrc_count(s));
    CHECK(avsrc_at(s, 0) == 2 && avsrc_at(s, 1) == 0 && avsrc_at(s, 2) == 1 && avsrc_at(s, 3) == 4, "the console's order, as picture numbers (%d %d %d %d)", avsrc_at(s, 0), avsrc_at(s, 1), avsrc_at(s, 2), avsrc_at(s, 3));
    CHECK(avsrc_at(s, 4) == 3 && avsrc_at(s, 5) == -1 && avsrc_at(s, -1) == -1, "an id the table leaves out follows; past the end is -1");
    uint8_t *p = avsrc_load(s, 2);
    CHECK(p != NULL && px_is(p, 2, 0, 0) && px_is(p, 2, 100, 9) && px_is(p, 2, 255, 255) && px_is(p, 2, 17, 200), "picture 2 is the one that was written");
    free(p);
    p = avsrc_load(s, 0);
    CHECK(p != NULL && px_is(p, 0, 50, 50) && !px_is(p, 2, 50, 50), "and picture 0 is not picture 2");
    free(p);
    CHECK(avsrc_load(s, 4) == NULL, "a picture whose file is missing is NULL");
    CHECK(avsrc_load(s, 3) == NULL && avsrc_load(s, -1) == NULL && avsrc_load(s, 5) == NULL && avsrc_load(s, 1000) == NULL, "so is one that doesn't exist");
    avsrc_close(s);
    avsrc_close(NULL);
    CHECK(avsrc_count(NULL) == 0 && avsrc_at(NULL, 0) == -1 && avsrc_load(NULL, 0) == NULL, "nothing is safe to ask");

    // a damaged file, and one that isn't a picture
    char path[400];
    snprintf(path, sizeof path, "%schara/00000002.szs", dir);
    fx_write(path, "Yaz0 not really", 15);
    snprintf(path, sizeof path, "%schara/00000001.szs", dir);
    fx_write(path, "", 0);
    s = avsrc_open(dir);
    CHECK(avsrc_load(s, 1) == NULL && avsrc_load(s, 0) == NULL, "a damaged or empty file is NULL");
    p = avsrc_load(s, 2);
    CHECK(p != NULL && px_is(p, 2, 0, 0), "the others are fine");
    free(p);
    avsrc_close(s);

    // the folder name with or without its slash, and the ways it can be missing
    char noslash[300];
    snprintf(noslash, sizeof noslash, "%s", dir);
    noslash[strlen(noslash) - 1] = 0;
    s = avsrc_open(noslash);
    CHECK(s != NULL && avsrc_count(s) == 5, "a folder name without its slash works");
    avsrc_close(s);
    CHECK(avsrc_open("out/avatar/there-is-no-such-folder/") == NULL && avsrc_open("") == NULL && avsrc_open(NULL) == NULL, "no folder, no pictures");

    // a table that is empty, or only names ids that can't be
    fx_dir(dir, sizeof dir, "empty");
    fx_table(dir, ids, 0);
    CHECK(avsrc_open(dir) == NULL, "an empty table is no pictures");
    static const uint32_t junk[] = {0, 0x7FFFFFFF, 513, 0xFFFFFFFFu};
    fx_table(dir, junk, 4);
    CHECK(avsrc_open(dir) == NULL, "a table of impossible ids is no pictures");
    static const uint32_t dup[] = {2, 2, 1, 2, 0, 7};
    fx_table(dir, dup, 6);
    s = avsrc_open(dir);
    CHECK(s != NULL && avsrc_count(s) == 7, "the highest id counts (%d)", avsrc_count(s));
    int seen[8] = {0}, pos = 0;
    for (; avsrc_at(s, pos) >= 0; pos++) seen[avsrc_at(s, pos)]++;
    int all_once = pos == 7;
    for (int i = 0; i < 7; i++) all_once = all_once && seen[i] == 1;
    CHECK(all_once && avsrc_at(s, 0) == 1 && avsrc_at(s, 1) == 0 && avsrc_at(s, 2) == 6, "every picture has exactly one place, in the table's order first (%d places)", pos);
    avsrc_close(s);
}

// the real thing, if there is some: every picture the table names must come out, and the order must name each once
static void test_real(const char *dir)
{
    AvatarSrc *s = avsrc_open(dir);
    CHECK(s != NULL, "real pictures open (%s)", dir);
    if (!s) return;
    int n = avsrc_count(s), bad = 0, transparent_corner = 0, seen[600] = {0}, once = 1;
    printf("real pictures: %d\n", n);
    for (int pos = 0; pos < n; pos++) {
        int pic = avsrc_at(s, pos);
        if (pic < 0 || pic >= n || seen[pic]++) once = 0;
    }
    for (int pic = 0; pic < n; pic++) {
        uint8_t *px = avsrc_load(s, pic);
        if (!px) {
            bad++;
            printf("  picture %d (id %d) did not load\n", pic, pic + 1);
            continue;
        }
        if (px[3] == 0) transparent_corner++;
        free(px);
    }
    CHECK(n >= 100, "a real set has more than a hundred pictures (%d)", n);
    CHECK(once, "the real order names every picture once");
    CHECK(bad == 0, "every real picture loads (%d did not)", bad);
    CHECK(transparent_corner > n / 2, "most real pictures have transparent corners (%d of %d)", transparent_corner, n);
    avsrc_close(s);
}

int main(void)
{
    test_unpack();
    test_roundtrip();
    test_folder();
    if (getenv("WASHI_AVATAR_DIR")) test_real(getenv("WASHI_AVATAR_DIR"));
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
