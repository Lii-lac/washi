#include "icons.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "custom_pics.h"
#include "fsx.h"

// ---- a list of games, each with its icon names (the scan and the repository's listing are both this)
typedef struct {
    char slug[64];
    char **names;
    int n, cap;
    uint64_t bytes;
    int64_t added; // (the SD card) when its folder last changed: the last time something was downloaded into it
} Game;

typedef struct {
    Game *g;
    int n, cap;
} Games;

static Game *games_get(Games *gs, const char *slug)
{
    for (int i = 0; i < gs->n; i++)
        if (!strcmp(gs->g[i].slug, slug)) return &gs->g[i];
    if (gs->n >= gs->cap) {
        int cap = gs->cap ? gs->cap * 2 : 32;
        Game *p = realloc(gs->g, (size_t)cap * sizeof *p);
        if (!p) return NULL;
        gs->g = p;
        gs->cap = cap;
    }
    Game *g = &gs->g[gs->n++];
    memset(g, 0, sizeof *g);
    snprintf(g->slug, sizeof g->slug, "%s", slug);
    return g;
}

static int game_add(Game *g, const char *name, uint64_t bytes)
{
    if (g->n >= g->cap) {
        int cap = g->cap ? g->cap * 2 : 64;
        char **p = realloc(g->names, (size_t)cap * sizeof *p);
        if (!p) return -1;
        g->names = p;
        g->cap = cap;
    }
    char *s = strdup(name);
    if (!s) return -1;
    g->names[g->n++] = s;
    g->bytes += bytes;
    return 0;
}

static int cmp_str(const void *a, const void *b) { return strcmp(*(char *const *)a, *(char *const *)b); }
static int cmp_game(const void *a, const void *b) { return strcmp(((const Game *)a)->slug, ((const Game *)b)->slug); }

static void games_sort(Games *gs)
{
    for (int i = 0; i < gs->n; i++) qsort(gs->g[i].names, (size_t)gs->g[i].n, sizeof(char *), cmp_str);
    qsort(gs->g, (size_t)gs->n, sizeof(Game), cmp_game);
}

static void games_free(Games *gs)
{
    for (int i = 0; i < gs->n; i++) {
        for (int k = 0; k < gs->g[i].n; k++) free(gs->g[i].names[k]);
        free(gs->g[i].names);
    }
    free(gs->g);
    memset(gs, 0, sizeof *gs);
}

static int games_find(const Games *gs, const char *slug)
{
    for (int i = 0; i < gs->n; i++)
        if (!strcmp(gs->g[i].slug, slug)) return i;
    return -1;
}

static int key_ok(const char *slug, const char *name)
{
    char key[CUSTOM_KEY_MAX + 8];
    if (snprintf(key, sizeof key, "%s/%s", slug, name) >= (int)sizeof key) return 0;
    return custom_key_valid(key);
}

// ---- the SD card
struct IconSet {
    Games gs;
    int total;
};

IconSet *icons_scan(const char *dir)
{
    char **games = NULL;
    size_t ng = 0;
    if (!dir || fsx_list(dir, &games, &ng) != 0) return NULL;
    IconSet *s = calloc(1, sizeof *s);
    if (!s) {
        fsx_list_free(games, ng);
        return NULL;
    }
    for (size_t i = 0; i < ng; i++) {
        char gdir[FSX_PATH];
        if (!key_ok(games[i], "x") || fsx_join(gdir, sizeof gdir, dir, games[i]) != 0 || !fsx_is_dir(gdir)) continue;
        char **files = NULL;
        size_t nf = 0;
        if (fsx_list(gdir, &files, &nf) != 0) continue;
        Game *g = NULL;
        for (size_t k = 0; k < nf; k++) {
            size_t l = strlen(files[k]);
            if (l < 5 || strcmp(files[k] + l - 4, ".png") != 0) continue;
            char stem[64];
            if (l - 4 >= sizeof stem) continue;
            memcpy(stem, files[k], l - 4);
            stem[l - 4] = 0;
            if (!key_ok(games[i], stem)) continue;
            if (!g) {
                g = games_get(&s->gs, games[i]);
                if (g) g->added = fsx_mtime(gdir);
            }
            if (g && game_add(g, stem, 0) == 0) s->total++;
        }
        fsx_list_free(files, nf);
        fsx_pump();
    }
    fsx_list_free(games, ng);
    if (s->total == 0) {
        icons_free(s);
        return NULL;
    }
    games_sort(&s->gs);
    return s;
}

void icons_free(IconSet *s)
{
    if (!s) return;
    games_free(&s->gs);
    free(s);
}

int icons_games(const IconSet *s) { return s ? s->gs.n : 0; }
const char *icons_slug(const IconSet *s, int g) { return s && g >= 0 && g < s->gs.n ? s->gs.g[g].slug : ""; }
int icons_count(const IconSet *s, int g) { return s && g >= 0 && g < s->gs.n ? s->gs.g[g].n : 0; }
const char *icons_name(const IconSet *s, int g, int i) { return s && g >= 0 && g < s->gs.n && i >= 0 && i < s->gs.g[g].n ? s->gs.g[g].names[i] : ""; }
int icons_total(const IconSet *s) { return s ? s->total : 0; }
int64_t icons_added(const IconSet *s, int g) { return s && g >= 0 && g < s->gs.n ? s->gs.g[g].added : 0; }
int icons_find(const IconSet *s, const char *slug) { return s ? games_find(&s->gs, slug) : -1; }

// ---- titles
static const struct { const char *word, *shown; } WORDS[] = {
    {"dk", "DK"}, {"hd", "HD"}, {"fzero", "F-Zero"}, {"vs", "vs."}, {"kirbys", "Kirby's"}, {"luigis", "Luigi's"}, {"bowsers", "Bowser's"},
    {"mother3", "Mother 3"}, {"pokemon", "Pok\xC3\xA9mon"}, {"za", "ZA"}, {"rpg", "RPG"}, {"3d", "3D"}, {"nes", "NES"},
};
static const char *const SMALL[] = {"and", "of", "the", "in", "to"};

void icons_title(const char *slug, char *out, size_t n)
{
    if (!n) return;
    size_t o = 0;
    int first = 1;
    const char *p = slug ? slug : "";
    while (*p && o + 1 < n) {
        const char *e = strchr(p, '-');
        size_t wl = e ? (size_t)(e - p) : strlen(p);
        char w[48];
        if (wl >= sizeof w) wl = sizeof w - 1;
        memcpy(w, p, wl);
        w[wl] = 0;
        const char *shown = NULL;
        for (size_t i = 0; i < sizeof WORDS / sizeof WORDS[0]; i++)
            if (!strcmp(w, WORDS[i].word)) shown = WORDS[i].shown;
        char buf[48];
        if (!shown) {
            int small = 0;
            for (size_t i = 0; i < sizeof SMALL / sizeof SMALL[0]; i++)
                if (!strcmp(w, SMALL[i])) small = 1;
            snprintf(buf, sizeof buf, "%s", w);
            if (!(small && !first) && buf[0] >= 'a' && buf[0] <= 'z') buf[0] = (char)(buf[0] - 32);
            shown = buf;
        }
        if (!first && o + 1 < n) out[o++] = ' ';
        for (const char *c = shown; *c && o + 1 < n; c++) out[o++] = *c;
        first = 0;
        p += wl;
        if (*p == '-') p++;
    }
    out[o] = 0;
}

// ---- the repository's listing
struct IconRepo {
    Games gs;
};

static int is_name_part(const char *s, size_t n)
{
    char key[CUSTOM_KEY_MAX + 8];
    if (n == 0 || n >= sizeof key / 2) return 0;
    snprintf(key, sizeof key, "%.*s/x", (int)n, s);
    return custom_key_valid(key);
}

IconRepo *icons_repo_parse(const char *json, size_t len, int *truncated)
{
    if (truncated) *truncated = 0;
    cJSON *root = json ? cJSON_ParseWithLength(json, len) : NULL;
    if (!root) return NULL;
    IconRepo *r = NULL;
    cJSON *tree = cJSON_GetObjectItemCaseSensitive(root, "tree");
    if (cJSON_IsArray(tree)) {
        r = calloc(1, sizeof *r);
        cJSON *e;
        cJSON_ArrayForEach(e, tree)
        {
            cJSON *path = cJSON_GetObjectItemCaseSensitive(e, "path"), *type = cJSON_GetObjectItemCaseSensitive(e, "type"), *size = cJSON_GetObjectItemCaseSensitive(e, "size");
            if (!r || !cJSON_IsString(path) || !cJSON_IsString(type) || strcmp(type->valuestring, "blob") != 0) continue;
            // <game>/characters/<name>.png
            const char *p = path->valuestring, *s1 = strchr(p, '/');
            if (!s1) continue;
            const char *s2 = strchr(s1 + 1, '/');
            if (!s2 || strncmp(s1 + 1, "characters", (size_t)(s2 - s1 - 1)) != 0 || s2 - s1 - 1 != 10 || strchr(s2 + 1, '/')) continue;
            size_t pl = strlen(s2 + 1), gl = (size_t)(s1 - p);
            if (pl < 5 || strcmp(s2 + 1 + pl - 4, ".png") != 0 || gl >= sizeof ((Game *)0)->slug) continue;
            char slug[64], name[64];
            memcpy(slug, p, gl);
            slug[gl] = 0;
            if (pl - 4 >= sizeof name) continue;
            memcpy(name, s2 + 1, pl - 4);
            name[pl - 4] = 0;
            if (!is_name_part(slug, gl) || !key_ok(slug, name)) continue;
            Game *g = games_get(&r->gs, slug);
            if (g) game_add(g, name, cJSON_IsNumber(size) && size->valuedouble > 0 ? (uint64_t)size->valuedouble : 0);
        }
        if (r && r->gs.n == 0) {
            free(r);
            r = NULL;
        }
        if (r) games_sort(&r->gs);
    }
    if (truncated) *truncated = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(root, "truncated"));
    cJSON_Delete(root);
    return r;
}

void icons_repo_free(IconRepo *r)
{
    if (!r) return;
    games_free(&r->gs);
    free(r);
}

int icons_repo_games(const IconRepo *r) { return r ? r->gs.n : 0; }
const char *icons_repo_slug(const IconRepo *r, int g) { return r && g >= 0 && g < r->gs.n ? r->gs.g[g].slug : ""; }
int icons_repo_count(const IconRepo *r, int g) { return r && g >= 0 && g < r->gs.n ? r->gs.g[g].n : 0; }
const char *icons_repo_name(const IconRepo *r, int g, int i) { return r && g >= 0 && g < r->gs.n && i >= 0 && i < r->gs.g[g].n ? r->gs.g[g].names[i] : ""; }
uint64_t icons_repo_bytes(const IconRepo *r, int g) { return r && g >= 0 && g < r->gs.n ? r->gs.g[g].bytes : 0; }
int icons_repo_find(const IconRepo *r, const char *slug) { return r ? games_find(&r->gs, slug) : -1; }

// ---- addresses
void icons_tree_url(char *out, size_t n) { snprintf(out, n, "https://api.github.com/repos/" ICONS_REPO "/git/trees/HEAD?recursive=1"); }

void icons_file_url(char *out, size_t n, const char *slug, const char *name)
{
    snprintf(out, n, "https://raw.githubusercontent.com/" ICONS_REPO "/HEAD/%s/characters/%s.png", slug, name);
}

int icons_png_ok(const uint8_t *data, size_t len)
{
    static const uint8_t SIG[8] = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
    if (!data || len < 33 || memcmp(data, SIG, 8) != 0 || memcmp(data + 12, "IHDR", 4) != 0) return 0;
    uint32_t w = (uint32_t)data[16] << 24 | data[17] << 16 | data[18] << 8 | data[19];
    uint32_t h = (uint32_t)data[20] << 24 | data[21] << 16 | data[22] << 8 | data[23];
    return w == h && w >= 16 && w <= 1024;
}
