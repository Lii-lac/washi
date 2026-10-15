// The console's side of updates (platform.h: online, http_get, install_update, restart). HTTPS by libcurl from devkitPro's portlibs, which talks
// TLS through the console's own ssl service (its certificates: no bundle to carry). The network is brought up the first time it is wanted, on the
// worker thread that wants it, and taken down when the app closes.
#include <curl/curl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <switch.h>

#include "platform_nx.h"
#include "trial.h"

static Mutex g_mx;
static int g_sock, g_curl, g_nifm; // 0 not tried, 1 up, -1 failed
static char g_self[512] = "sdmc:/switch/washi.nro";

static int net_up(void)
{
    mutexLock(&g_mx);
    if (!g_sock) g_sock = R_SUCCEEDED(socketInitializeDefault()) ? 1 : -1;
    if (g_sock == 1 && !g_curl) g_curl = curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK ? 1 : -1;
    int ok = g_sock == 1 && g_curl == 1;
    mutexUnlock(&g_mx);
    return ok;
}

// The easy handles that are not in a transfer. A handle keeps its connection after a transfer (curl_easy_reset leaves the live connections alone), so
// the next request to the same server on it skips the connecting and the TLS handshake. A handle that failed, or was stopped, is thrown away instead.
#define POOL_MAX 6
#define PARALLEL 4
static CURL *g_pool[POOL_MAX];
static int g_npool;

static CURL *pool_take(void)
{
    CURL *c = NULL;
    mutexLock(&g_mx);
    if (g_npool > 0) c = g_pool[--g_npool];
    mutexUnlock(&g_mx);
    return c ? c : curl_easy_init();
}

static void pool_give(CURL *c)
{
    curl_easy_reset(c);
    mutexLock(&g_mx);
    if (g_npool < POOL_MAX) {
        g_pool[g_npool++] = c;
        c = NULL;
    }
    mutexUnlock(&g_mx);
    if (c) curl_easy_cleanup(c);
}

static void pool_drain(void)
{
    mutexLock(&g_mx);
    while (g_npool > 0) curl_easy_cleanup(g_pool[--g_npool]);
    mutexUnlock(&g_mx);
}

static int nx_online(Platform *p)
{
    (void)p;
    mutexLock(&g_mx);
    if (!g_nifm) g_nifm = R_SUCCEEDED(nifmInitialize(NifmServiceType_User)) ? 1 : -1;
    int have = g_nifm == 1;
    mutexUnlock(&g_mx);
    if (!have) return -1;
    NifmInternetConnectionType type;
    NifmInternetConnectionStatus st;
    u32 strength = 0;
    if (R_FAILED(nifmGetInternetConnectionStatus(&type, &strength, &st))) return 0; // (it fails when there is no connection at all)
    return st == NifmInternetConnectionStatus_Connected ? 1 : 0;
}

typedef struct {
    uint8_t *buf;
    size_t len, cap;
    int oom;
    const volatile int *cancel;
    void (*progress)(void *, uint64_t, uint64_t);
    void *ud;
} Get;

static size_t on_data(char *ptr, size_t size, size_t n, void *ud)
{
    Get *g = ud;
    size_t add = size * n;
    if (g->len + add > g->cap) {
        size_t cap = g->cap ? g->cap * 2 : 64 * 1024;
        while (cap < g->len + add) cap *= 2;
        uint8_t *nb = realloc(g->buf, cap);
        if (!nb) {
            g->oom = 1;
            return 0; // (curl stops)
        }
        g->buf = nb;
        g->cap = cap;
    }
    memcpy(g->buf + g->len, ptr, add);
    g->len += add;
    return add;
}

static int on_progress(void *ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t ultotal, curl_off_t ulnow)
{
    (void)ultotal;
    (void)ulnow;
    Get *g = ud;
    if (g->cancel && *g->cancel) return 1; // (stops the transfer)
    if (g->progress) g->progress(g->ud, (uint64_t)dlnow, (uint64_t)dltotal);
    return 0;
}

static int nx_http_get(Platform *p, const char *url, uint8_t **body, size_t *len, int *status, const volatile int *cancel,
                       void (*progress)(void *, uint64_t, uint64_t), void *ud)
{
    (void)p;
    *body = NULL;
    *len = 0;
    *status = 0;
    if (!net_up()) return -1;
    CURL *c = pool_take();
    if (!c) return -1;
    Get g = {NULL, 0, 0, 0, cancel, progress, ud};
    struct curl_slist *hdr = NULL;
    hdr = curl_slist_append(hdr, "User-Agent: Washi/" APP_VERSION " (Nintendo Switch homebrew)"); // (GitHub turns away requests without one)
    if (strstr(url, "api.github.com")) {
        hdr = curl_slist_append(hdr, "Accept: application/vnd.github+json");
        hdr = curl_slist_append(hdr, "X-GitHub-Api-Version: 2022-11-28");
    } else {
        hdr = curl_slist_append(hdr, "Accept: application/octet-stream");
    }
    curl_easy_setopt(c, CURLOPT_URL, url);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_MAXREDIRS, 8L);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 12L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 64L); // (a transfer that has crawled under 64 B/s for 30 s has died)
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 30L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, on_data);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &g);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, on_progress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &g);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    curl_slist_free_all(hdr);
    if (rc != CURLE_OK || g.oom) {
        curl_easy_cleanup(c);
        free(g.buf);
        return -1;
    }
    pool_give(c);
    *body = g.buf;
    *len = g.len;
    *status = (int)code;
    return 0;
}

// written next to the running app (<self>.new), read back for its size, then swapped in: the old one becomes <self>.bak (the one before that goes)
static int nx_install_update(Platform *p, const uint8_t *nro, size_t len)
{
    (void)p;
    char tmp[530], bak[530];
    snprintf(tmp, sizeof tmp, "%s.new", g_self);
    snprintf(bak, sizeof bak, "%s.bak", g_self);
    FILE *f = fopen(tmp, "wb");
    if (!f) return -1;
    size_t w = fwrite(nro, 1, len, f);
    int bad = fflush(f) != 0;
    bad |= fclose(f) != 0;
    struct stat stt;
    if (bad || w != len || stat(tmp, &stt) != 0 || (size_t)stt.st_size != len) {
        remove(tmp);
        return -1;
    }
    remove(bak);
    if (rename(g_self, bak) != 0) {
        remove(tmp);
        return -1;
    }
    if (rename(tmp, g_self) != 0) {
        rename(bak, g_self); // (put the old one back)
        remove(tmp);
        return -1;
    }
    trial_begin(g_self); // (on trial until it gets going: trial.h)
    return 0;
}

static int nx_restart(Platform *p)
{
    (void)p;
    if (!envHasNextLoad()) return 0;
    return R_SUCCEEDED(envSetNextLoad(g_self, g_self)) ? 1 : 0;
}

static int nx_previous(Platform *p, char *ver, size_t cap)
{
    (void)p;
    return trial_kept(g_self, ver, cap);
}

static int nx_roll_back(Platform *p)
{
    (void)p;
    return trial_swap(g_self);
}

static void nx_started_ok(Platform *p)
{
    (void)p;
    trial_ok(g_self);
}

static int nx_rolled_back(Platform *p, char *ver, size_t cap)
{
    (void)p;
    return trial_note(g_self, ver, cap);
}

static void set_self(const char *self_path)
{
    if (self_path && !strncmp(self_path, "sdmc:/", 6) && strlen(self_path) < sizeof g_self) snprintf(g_self, sizeof g_self, "%s", self_path);
}

void net_nx_attach(Platform *p, const char *self_path)
{
    mutexInit(&g_mx);
    set_self(self_path);
    p->online = nx_online;
    p->http_get = nx_http_get;
    p->http_parallel = PARALLEL;
    p->install_update = nx_install_update;
    p->restart = nx_restart;
    p->previous = nx_previous;
    p->roll_back = nx_roll_back;
    p->started_ok = nx_started_ok;
    p->rolled_back = nx_rolled_back;
}

int net_nx_boot_check(const char *self_path)
{
    set_self(self_path);
    if (!trial_start(g_self)) return 0;
    if (envHasNextLoad()) envSetNextLoad(g_self, g_self); // (else the person opens it again, and it is the kept one)
    return 1;
}


void net_nx_exit(void)
{
    if (g_curl == 1) {
        pool_drain();
        curl_global_cleanup();
    }
    if (g_sock == 1) socketExit();
    if (g_nifm == 1) nifmExit();
    g_curl = g_sock = g_nifm = 0;
}
