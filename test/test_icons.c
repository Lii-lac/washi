// The Extra icons' parts that need no screen: the keys that name them and the numbers those get (source/custom_pics.c), the scan of the SD card's folder, the
// reading of GitHub's list of the repository, the addresses and the check on a downloaded PNG (source/icons.c).
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "custom_pics.h"
#include "fsx.h"
#include "icons.h"

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

static void test_keys(void)
{
    custom_reset();
    CHECK(custom_key_valid("fire-emblem-three-houses/FETHc001") && custom_key_valid("a/b") && custom_key_valid("game_1/x.y-z"), "good keys");
    CHECK(!custom_key_valid(NULL) && !custom_key_valid("") && !custom_key_valid("a") && !custom_key_valid("a/") && !custom_key_valid("/b"), "keys need two parts");
    CHECK(!custom_key_valid("a/b/c") && !custom_key_valid("../x") && !custom_key_valid("a/..") && !custom_key_valid(".a/b") && !custom_key_valid("a/.b"), "no way out of the folder");
    CHECK(!custom_key_valid("a b/c") && !custom_key_valid("a/b:c") && !custom_key_valid("a\\b/c") && !custom_key_valid("a/b\n") && !custom_key_valid("\xC3\xA9/b"), "only plain characters");
    char part63[64], part64[65], key[160];
    memset(part63, 'x', 63), part63[63] = 0;
    memset(part64, 'x', 64), part64[64] = 0;
    snprintf(key, sizeof key, "%s/%s", part63, part63);
    CHECK(custom_key_valid(key), "63 bytes a part is the longest");
    snprintf(key, sizeof key, "%s/%s", part64, part63);
    CHECK(!custom_key_valid(key), "64 is too many");
    CHECK(custom_key_valid("super-nintendo-entertainment-system-nintendo-switch-online/SNESNSOc001"), "the longest real game folder");

    int a = custom_id("g/one"), b = custom_id("g/two"), a2 = custom_id("g/one");
    CHECK(a == CUSTOM_PIC_BASE && b == CUSTOM_PIC_BASE + 1 && a2 == a, "numbers are handed out in turn and a key keeps its own (%d %d %d)", a, b, a2);
    CHECK(custom_is(a) && !custom_is(CUSTOM_PIC_BASE - 1) && !custom_is(-1) && !custom_is(147), "which numbers are custom ones");
    CHECK(!strcmp(custom_key(a), "g/one") && !strcmp(custom_key(b), "g/two"), "and the number gives the key back");
    CHECK(custom_key(CUSTOM_PIC_BASE + 2) == NULL && custom_key(5) == NULL && custom_key(-1) == NULL && custom_key(CUSTOM_PIC_BASE - 1) == NULL, "a number that stands for nothing");
    CHECK(custom_id("bad key") == -1 && custom_id(NULL) == -1 && custom_id("a/b/c") == -1, "an invalid key gets no number");
    // many keys: the table grows, every key keeps its number
    int ok = 1;
    for (int i = 0; i < 6000; i++) {
        snprintf(key, sizeof key, "game%d/icon%d", i % 70, i);
        int id = custom_id(key);
        if (id != CUSTOM_PIC_BASE + 2 + i) ok = 0;
    }
    for (int i = 0; i < 6000 && ok; i += 7) {
        snprintf(key, sizeof key, "game%d/icon%d", i % 70, i);
        if (custom_id(key) != CUSTOM_PIC_BASE + 2 + i || strcmp(custom_key(CUSTOM_PIC_BASE + 2 + i), key) != 0) ok = 0;
    }
    CHECK(ok, "six thousand keys, each with its number");
    custom_reset();
    CHECK(custom_key(CUSTOM_PIC_BASE) == NULL && custom_id("z/z") == CUSTOM_PIC_BASE, "reset forgets them all");
    custom_reset();
}

static void test_titles(void)
{
    char t[120];
    icons_title("fire-emblem-three-houses", t, sizeof t);
    CHECK(!strcmp(t, "Fire Emblem Three Houses"), "plain words (%s)", t);
    icons_title("the-legend-of-zelda-breath-of-the-wild", t, sizeof t);
    CHECK(!strcmp(t, "The Legend of Zelda Breath of the Wild"), "small words stay small, except at the start (%s)", t);
    icons_title("kirbys-return-to-dream-land-deluxe", t, sizeof t);
    CHECK(!strcmp(t, "Kirby's Return to Dream Land Deluxe"), "a possessive (%s)", t);
    icons_title("mario-vs-donkey-kong", t, sizeof t);
    CHECK(!strcmp(t, "Mario vs. Donkey Kong"), "vs. (%s)", t);
    icons_title("fzero-99", t, sizeof t);
    CHECK(!strcmp(t, "F-Zero 99"), "F-Zero (%s)", t);
    icons_title("pokemon-legends-za", t, sizeof t);
    CHECK(!strcmp(t, "Pok\xC3\xA9mon Legends ZA"), "Pokemon with its accent (%s)", t);
    icons_title("dk-challenge", t, sizeof t);
    CHECK(!strcmp(t, "DK Challenge"), "DK (%s)", t);
    icons_title("default", t, sizeof t);
    CHECK(!strcmp(t, "Default"), "one word (%s)", t);
    char small[8];
    icons_title("fire-emblem-three-houses", small, sizeof small);
    CHECK(strlen(small) == 7 && !strncmp(small, "Fire Em", 7), "it never writes past the end (%s)", small);
    icons_title("", t, sizeof t);
    CHECK(t[0] == 0, "nothing in, nothing out");
}

static void put(const char *dir, const char *rel, const char *text)
{
    char p[FSX_PATH];
    snprintf(p, sizeof p, "%s/%s", dir, rel);
    char d[FSX_PATH];
    snprintf(d, sizeof d, "%s", p);
    char *slash = strrchr(d, '/');
    if (slash) {
        *slash = 0;
        fsx_mkdir_p(d);
    }
    fsx_write_file(p, text, strlen(text));
}

static void test_scan(void)
{
    const char *dir = "out/icons-scan";
    fsx_rmtree(dir);
    fsx_mkdir_p(dir);
    CHECK(icons_scan(dir) == NULL, "an empty folder has no icons");
    CHECK(icons_scan("out/icons-no-such-folder") == NULL && icons_scan(NULL) == NULL, "no folder, no icons");
    put(dir, "game-b/x.png", "png");
    put(dir, "game-a/b.png", "png");
    put(dir, "game-a/a.png", "png");
    put(dir, "game-a/notes.txt", "text");
    put(dir, "game-a/.hidden.png", "png");
    put(dir, "game-a/c.png.tmp", "half");
    put(dir, "bad name/y.png", "png");
    put(dir, "empty-game/readme.txt", "no icons here");
    put(dir, "loose.png", "png");
    IconSet *s = icons_scan(dir);
    CHECK(s != NULL && icons_games(s) == 2 && icons_total(s) == 3, "two games with icons, three icons (%d games)", icons_games(s));
    CHECK(!strcmp(icons_slug(s, 0), "game-a") && !strcmp(icons_slug(s, 1), "game-b"), "games in name order");
    CHECK(icons_count(s, 0) == 2 && !strcmp(icons_name(s, 0, 0), "a") && !strcmp(icons_name(s, 0, 1), "b"), "icons in name order, without .png, only the .png ones");
    CHECK(icons_count(s, 1) == 1 && !strcmp(icons_name(s, 1, 0), "x"), "the other game");
    CHECK(icons_find(s, "game-b") == 1 && icons_find(s, "game-a") == 0 && icons_find(s, "nope") == -1 && icons_find(NULL, "x") == -1, "finding a game");
    CHECK(icons_count(s, 5) == 0 && icons_count(s, -1) == 0 && icons_name(s, 0, 9)[0] == 0 && icons_slug(s, 9)[0] == 0, "out of range is empty");
    icons_free(s);
    icons_free(NULL);
    fsx_rmtree(dir);
}

static const char TREE[] =
    "{\"sha\":\"abc\",\"truncated\":false,\"tree\":["
    "{\"path\":\"README.md\",\"type\":\"blob\",\"size\":10},"
    "{\"path\":\"game-b\",\"type\":\"tree\"},"
    "{\"path\":\"game-b/characters\",\"type\":\"tree\"},"
    "{\"path\":\"game-b/characters/B2.png\",\"type\":\"blob\",\"size\":2000},"
    "{\"path\":\"game-b/characters/B1.png\",\"type\":\"blob\",\"size\":1000},"
    "{\"path\":\"game-b/backgrounds/BG1.png\",\"type\":\"blob\",\"size\":5000},"
    "{\"path\":\"game-b/frames/F1.png\",\"type\":\"blob\",\"size\":5000},"
    "{\"path\":\"game-a/characters/A1.png\",\"type\":\"blob\",\"size\":300},"
    "{\"path\":\"game-a/characters/A1.jpg\",\"type\":\"blob\",\"size\":300},"
    "{\"path\":\"game-a/characters/sub/A9.png\",\"type\":\"blob\",\"size\":300},"
    "{\"path\":\"game-a/characters/../x.png\",\"type\":\"blob\",\"size\":300},"
    "{\"path\":\"bad name/characters/Z1.png\",\"type\":\"blob\",\"size\":300},"
    "{\"path\":\"game-c/characters/.png\",\"type\":\"blob\",\"size\":300},"
    "{\"path\":\"game-d/characters/D1.png\",\"type\":\"commit\",\"size\":300},"
    "{\"path\":\"characters/C1.png\",\"type\":\"blob\",\"size\":300}"
    "]}";

static void test_repo(void)
{
    int trunc = -1;
    IconRepo *r = icons_repo_parse(TREE, strlen(TREE), &trunc);
    CHECK(r != NULL && trunc == 0, "the list reads");
    CHECK(icons_repo_games(r) == 2, "only the games with characters (%d)", icons_repo_games(r));
    CHECK(!strcmp(icons_repo_slug(r, 0), "game-a") && !strcmp(icons_repo_slug(r, 1), "game-b"), "in name order");
    CHECK(icons_repo_count(r, 0) == 1 && !strcmp(icons_repo_name(r, 0, 0), "A1"), "game-a has A1 only (not the jpg, the sub-folder or ..)");
    CHECK(icons_repo_count(r, 1) == 2 && !strcmp(icons_repo_name(r, 1, 0), "B1") && !strcmp(icons_repo_name(r, 1, 1), "B2"), "game-b's characters, in name order, not its backgrounds or frames");
    CHECK(icons_repo_bytes(r, 1) == 3000 && icons_repo_bytes(r, 0) == 300, "and their sizes added up (%llu)", (unsigned long long)icons_repo_bytes(r, 1));
    CHECK(icons_repo_find(r, "game-b") == 1 && icons_repo_find(r, "game-x") == -1 && icons_repo_find(NULL, "g") == -1, "finding a game");
    CHECK(icons_repo_count(r, 7) == 0 && icons_repo_name(r, 0, 4)[0] == 0 && icons_repo_slug(r, -1)[0] == 0 && icons_repo_bytes(r, 9) == 0, "out of range is empty");
    icons_repo_free(r);
    icons_repo_free(NULL);

    const char *cut = "{\"truncated\":true,\"tree\":[{\"path\":\"g/characters/a.png\",\"type\":\"blob\",\"size\":1}]}";
    r = icons_repo_parse(cut, strlen(cut), &trunc);
    CHECK(r != NULL && trunc == 1, "GitHub's note that the list was cut short is passed on");
    icons_repo_free(r);

    CHECK(icons_repo_parse("not json", 8, &trunc) == NULL && trunc == 0, "not JSON");
    CHECK(icons_repo_parse("{\"message\":\"Not Found\"}", 23, NULL) == NULL, "an error message from GitHub");
    CHECK(icons_repo_parse("{\"tree\":[]}", 11, NULL) == NULL, "an empty list");
    const char *none = "{\"tree\":[{\"path\":\"a/b.png\",\"type\":\"blob\"}]}";
    CHECK(icons_repo_parse(none, strlen(none), NULL) == NULL, "a list with no characters");
    CHECK(icons_repo_parse(NULL, 0, NULL) == NULL, "nothing");
}

static void test_urls(void)
{
    char u[300];
    icons_tree_url(u, sizeof u);
    CHECK(!strcmp(u, "https://api.github.com/repos/henry-debruin/nso-icons/git/trees/HEAD?recursive=1"), "the tree's address (%s)", u);
    icons_file_url(u, sizeof u, "fire-emblem-three-houses", "FETHc001");
    CHECK(!strcmp(u, "https://raw.githubusercontent.com/henry-debruin/nso-icons/HEAD/fire-emblem-three-houses/characters/FETHc001.png"), "a picture's address (%s)", u);
    char tiny[20];
    icons_file_url(tiny, sizeof tiny, "game", "x");
    CHECK(strlen(tiny) == 19, "an address that does not fit is cut, not overrun");
}

static void png_head(uint8_t *b, uint32_t w, uint32_t h)
{
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    memset(b, 0, 64);
    memcpy(b, SIG, 8);
    b[11] = 13;
    memcpy(b + 12, "IHDR", 4);
    b[16] = (uint8_t)(w >> 24), b[17] = (uint8_t)(w >> 16), b[18] = (uint8_t)(w >> 8), b[19] = (uint8_t)w;
    b[20] = (uint8_t)(h >> 24), b[21] = (uint8_t)(h >> 16), b[22] = (uint8_t)(h >> 8), b[23] = (uint8_t)h;
}

static void test_png(void)
{
    uint8_t b[64];
    png_head(b, 256, 256);
    CHECK(icons_png_ok(b, 64), "a 256 x 256 PNG");
    png_head(b, 16, 16);
    CHECK(icons_png_ok(b, 64), "the smallest");
    png_head(b, 1024, 1024);
    CHECK(icons_png_ok(b, 64), "the largest");
    png_head(b, 256, 128);
    CHECK(!icons_png_ok(b, 64), "not square");
    png_head(b, 8, 8);
    CHECK(!icons_png_ok(b, 64), "too small");
    png_head(b, 2048, 2048);
    CHECK(!icons_png_ok(b, 64), "too big");
    png_head(b, 256, 256);
    b[1] = 'X';
    CHECK(!icons_png_ok(b, 64), "wrong signature");
    png_head(b, 256, 256);
    memcpy(b + 12, "IDAT", 4);
    CHECK(!icons_png_ok(b, 64), "no header chunk");
    png_head(b, 256, 256);
    CHECK(!icons_png_ok(b, 20) && !icons_png_ok(b, 0) && !icons_png_ok(NULL, 64), "too short, or nothing");
    CHECK(!icons_png_ok((const uint8_t *)"<html>Not Found</html> not a picture at all", 44), "an error page");
}

// the real thing, if there is some: the actual list (WASHI_TREE_JSON=<a saved answer of the repository's tree API>)
static void test_real_tree(const char *path)
{
    char *txt = NULL;
    size_t n = 0;
    CHECK(fsx_read_all(path, &txt, &n) == 0 && n > 100000, "the real list is there (%zu bytes)", n);
    if (!txt) return;
    int trunc = -1;
    IconRepo *r = icons_repo_parse(txt, n, &trunc);
    free(txt);
    CHECK(r != NULL && trunc == 0, "the real list reads, and is not cut short");
    int games = icons_repo_games(r), total = 0, bad = 0;
    uint64_t bytes = 0;
    for (int g = 0; g < games; g++) {
        total += icons_repo_count(r, g);
        bytes += icons_repo_bytes(r, g);
        char t[100], u[300];
        icons_title(icons_repo_slug(r, g), t, sizeof t);
        icons_file_url(u, sizeof u, icons_repo_slug(r, g), icons_repo_count(r, g) ? icons_repo_name(r, g, 0) : "");
        if (!t[0] || !icons_repo_count(r, g) || strlen(u) < 60) bad++;
    }
    printf("real list: %d games, %d character icons, %llu MB\n", games, total, (unsigned long long)(bytes >> 20));
    CHECK(games >= 60 && total >= 4800, "a real list has dozens of games and thousands of characters (%d, %d)", games, total);
    CHECK(bad == 0, "every real game has a title, icons and an address");
    icons_repo_free(r);
}

int main(void)
{
    test_keys();
    test_titles();
    test_scan();
    test_repo();
    test_urls();
    test_png();
    if (getenv("WASHI_TREE_JSON")) test_real_tree(getenv("WASHI_TREE_JSON"));
    printf("%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
