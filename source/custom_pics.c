#include "custom_pics.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum { MAX_KEYS = 16384, PART_MAX = 63 };

static char (*g_keys)[CUSTOM_KEY_MAX];
static int g_n, g_cap;
static int *g_tab; // open addressing over the keys: index + 1, or 0 for an empty place
static int g_tab_cap;

static uint32_t hash_of(const char *s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ (uint8_t)*s) * 16777619u;
    return h;
}

static int part_ok(const char *s, size_t n)
{
    if (n < 1 || n > PART_MAX || s[0] == '.') return 0;
    for (size_t i = 0; i < n; i++) {
        char c = s[i];
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-';
        if (!ok) return 0;
    }
    return 1;
}

int custom_key_valid(const char *key)
{
    if (!key) return 0;
    const char *slash = strchr(key, '/');
    if (!slash || strchr(slash + 1, '/')) return 0;
    return part_ok(key, (size_t)(slash - key)) && part_ok(slash + 1, strlen(slash + 1));
}

static int tab_find(const char *key, uint32_t h, int *free_at)
{
    for (int i = 0; i < g_tab_cap; i++) {
        int at = (int)((h + (uint32_t)i) & (uint32_t)(g_tab_cap - 1));
        int e = g_tab[at];
        if (!e) {
            if (free_at) *free_at = at;
            return -1;
        }
        if (!strcmp(g_keys[e - 1], key)) return e - 1;
    }
    return -1;
}

static int tab_grow(void)
{
    int cap = g_tab_cap ? g_tab_cap * 2 : 1024;
    int *t = calloc((size_t)cap, sizeof *t);
    if (!t) return -1;
    free(g_tab);
    g_tab = t;
    g_tab_cap = cap;
    for (int k = 0; k < g_n; k++) {
        uint32_t h = hash_of(g_keys[k]);
        for (int i = 0; i < cap; i++) {
            int at = (int)((h + (uint32_t)i) & (uint32_t)(cap - 1));
            if (!g_tab[at]) {
                g_tab[at] = k + 1;
                break;
            }
        }
    }
    return 0;
}

int custom_id(const char *key)
{
    if (!custom_key_valid(key) || strlen(key) >= CUSTOM_KEY_MAX) return -1;
    uint32_t h = hash_of(key);
    int free_at = -1;
    if (g_tab_cap) {
        int k = tab_find(key, h, &free_at);
        if (k >= 0) return CUSTOM_PIC_BASE + k;
    }
    if (g_n >= MAX_KEYS) return -1;
    if (g_n >= g_cap) {
        int cap = g_cap ? g_cap * 2 : 256;
        void *p = realloc(g_keys, (size_t)cap * CUSTOM_KEY_MAX);
        if (!p) return -1;
        g_keys = p;
        g_cap = cap;
    }
    if (g_tab_cap < 2 * (g_n + 1) && tab_grow() != 0) return -1; // (a table at most half full)
    tab_find(key, h, &free_at);
    memcpy(g_keys[g_n], key, strlen(key) + 1);
    g_tab[free_at] = g_n + 1;
    return CUSTOM_PIC_BASE + g_n++;
}

const char *custom_key(int id)
{
    int k = id - CUSTOM_PIC_BASE;
    return k >= 0 && k < g_n ? g_keys[k] : NULL;
}

void custom_reset(void)
{
    free(g_keys);
    free(g_tab);
    g_keys = NULL;
    g_tab = NULL;
    g_n = g_cap = g_tab_cap = 0;
}
