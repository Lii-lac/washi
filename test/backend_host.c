#define _GNU_SOURCE
#include "backend_host.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "../source/fsx.h"

static FakeBe *F(Backend *be)
{
    return (FakeBe *)be;
}

static void tid(uint64_t t, char out[17])
{
    snprintf(out, 17, "%016" PRIx64, t);
}

// ---- plain folder copy (the fake's "commit" and "mount")
static int copy_dir(const char *src, const char *dst)
{
    if (!fsx_is_dir(dst) && mkdir(dst, 0777) != 0) return -1;
    char **names;
    size_t n;
    if (fsx_list(src, &names, &n) != 0) return -1;
    int rc = 0;
    for (size_t i = 0; i < n && rc == 0; i++) {
        char s[FSX_PATH], d[FSX_PATH];
        fsx_join(s, sizeof s, src, names[i]);
        fsx_join(d, sizeof d, dst, names[i]);
        if (fsx_is_dir(s)) {
            rc = copy_dir(s, d);
        } else {
            char *data;
            size_t len;
            if (fsx_read_all(s, &data, &len) != 0) { rc = -1; break; }
            rc = fsx_write_file(d, data, len);
            free(data);
        }
    }
    fsx_list_free(names, n);
    return rc;
}

// ---- failure injection: every "counted" call goes through here
static int tick(FakeBe *f, const char *name)
{
    if (f->crashed) return 1;
    f->ops++;
    if (f->fail_at && f->ops == f->fail_at) {
        f->crashed = 1;
        snprintf(f->crash_name, sizeof f->crash_name, "%s", name);
        return 1;
    }
    return 0;
}

static int b_set_target(Backend *be, const char *uid)
{
    (void)be; (void)uid;
    return BE_OK;
}

static int b_list_saves(Backend *be, uint64_t *out, size_t max, size_t *count)
{
    FakeBe *f = F(be);
    if (tick(f, "list_saves")) return BE_ERR;
    char **names;
    size_t n;
    *count = 0;
    if (fsx_list(f->committed, &names, &n) != 0) return BE_ERR;
    for (size_t i = 0; i < n && *count < max; i++) {
        uint64_t t = 0;
        for (int k = 0; k < 16; k++) {
            char c = names[i][k];
            t = (t << 4) | (uint64_t)(c <= '9' ? c - '0' : c - 'a' + 10);
        }
        out[(*count)++] = t;
    }
    fsx_list_free(names, n);
    return BE_OK;
}

static int b_list_device(Backend *be, uint64_t *out, size_t max, size_t *count)
{
    FakeBe *f = F(be);
    *count = 0;
    for (int i = 0; i < f->ndevice && (size_t)i < max; i++) out[(*count)++] = f->device_saves[i];
    return BE_OK;
}

static int b_save_size(Backend *be, uint64_t t, uint64_t *bytes)
{
    FakeBe *f = F(be);
    if (tick(f, "save_size")) return BE_ERR;
    char p[FSX_PATH];
    fake_save_path(f, t, p, sizeof p);
    *bytes = fsx_tree_size(p);
    return BE_OK;
}

static int b_mount(Backend *be, uint64_t t)
{
    FakeBe *f = F(be);
    if (tick(f, "mount")) return BE_ERR;
    FakeTitle *ft = fake_title(f, t);
    if (ft && ft->running) return BE_ERR_OPEN;
    char src[FSX_PATH];
    fake_save_path(f, t, src, sizeof src);
    if (!fsx_is_dir(src) || f->is_mounted) return BE_ERR_OPEN;
    char tdir[17];
    tid(t, tdir);
    fsx_join(f->root, sizeof f->root, f->live, tdir);
    fsx_rmtree(f->root);
    if (copy_dir(src, f->root) != 0) return BE_ERR;
    f->is_mounted = 1;
    f->mounted = t;
    f->uncommitted = 0;
    be->save_root = f->root;
    return BE_OK;
}

static int b_create(Backend *be, uint64_t t)
{
    FakeBe *f = F(be);
    if (tick(f, "create")) return BE_ERR;
    FakeTitle *ft = fake_title(f, t);
    if (!ft || !ft->installed) return BE_ERR_NOT_INSTALLED;
    char p[FSX_PATH];
    fake_save_path(f, t, p, sizeof p);
    if (fsx_exists(p)) return BE_ERR;
    return fsx_mkdir_p(p) == 0 ? BE_OK : BE_ERR;
}

static int b_commit(Backend *be)
{
    FakeBe *f = F(be);
    if (tick(f, "commit")) return BE_ERR;
    if (!f->is_mounted) return BE_ERR;
    char dst[FSX_PATH];
    fake_save_path(f, f->mounted, dst, sizeof dst);
    fsx_rmtree(dst);
    if (copy_dir(f->root, dst) != 0) return BE_ERR;
    f->uncommitted = 0;
    return BE_OK;
}

static void b_unmount(Backend *be)
{
    FakeBe *f = F(be);
    if (f->crashed) return;
    f->is_mounted = 0; // uncommitted changes are simply dropped
}

static uint64_t b_journal(Backend *be, uint64_t t)
{
    FakeTitle *ft = fake_title(F(be), t);
    return ft ? ft->journal : 0x100000;
}

static int b_write(Backend *be, FILE *fp, const void *buf, size_t n)
{
    FakeBe *f = F(be);
    if (tick(f, "write")) return BE_ERR;
    if (!f->is_mounted) return BE_ERR;
    if (f->uncommitted + n > b_journal(be, f->mounted)) return BE_ERR_JOURNAL;
    size_t w = n;
    f->writes++;
    if (f->corrupt_next_write) {
        f->corrupt_next_write = 0;
        w = n / 2;
    }
    if (fwrite(buf, 1, w, fp) != w) return BE_ERR;
    f->uncommitted += n;
    return BE_OK;
}

static int b_title_name(Backend *be, uint64_t t, char *out, size_t n)
{
    FakeTitle *ft = fake_title(F(be), t);
    if (!ft) return BE_ERR;
    if (ft->name[0]) snprintf(out, n, "%s", ft->name);
    else snprintf(out, n, "Game %04" PRIx64, (t >> 16) & 0xFFFF);
    return BE_OK;
}

static int b_title_icon(Backend *be, uint64_t t, const uint8_t **jpeg, size_t *size)
{
    FakeBe *f = F(be);
    if (!f->icon_data || !fake_title(f, t)) return BE_ERR;
    *jpeg = f->icon_data;
    *size = f->icon_size;
    return BE_OK;
}

static int b_accounts(Backend *be, BeAccount *out, size_t max, size_t *count)
{
    FakeBe *f = F(be);
    *count = 0;
    for (int i = 0; i < f->naccounts && (size_t)i < max; i++) out[(*count)++] = f->accounts[i];
    return BE_OK;
}

static int b_battery(Backend *be, int *pct, int *charging)
{
    FakeBe *f = F(be);
    if (tick(f, "battery")) return BE_ERR;
    *pct = f->battery_pct;
    *charging = f->charging;
    return BE_OK;
}

static int b_free(Backend *be, uint64_t *bytes)
{
    FakeBe *f = F(be);
    if (tick(f, "free_space")) return BE_ERR;
    *bytes = f->free_bytes;
    return BE_OK;
}

static int b_total(Backend *be, uint64_t *bytes)
{
    FakeBe *f = F(be);
    if (tick(f, "total_space")) return BE_ERR;
    *bytes = f->total_bytes;
    return BE_OK;
}

static time_t b_now(Backend *be)
{
    FakeBe *f = F(be);
    time_t v = f->clock;
    f->clock += f->clock_step;
    return v;
}

static uint32_t b_random(Backend *be)
{
    FakeBe *f = F(be);
    f->rng ^= f->rng << 13;
    f->rng ^= f->rng >> 17;
    f->rng ^= f->rng << 5;
    return f->rng;
}

static int b_checkpoint(Backend *be, const char *name)
{
    return tick(F(be), name);
}

static int b_sys_read(Backend *be, char nick[40], uint8_t *user_data, uint8_t *img, size_t cap, size_t *img_size);
static int b_sys_store(Backend *be, const char *nick, const uint8_t *user_data, const uint8_t *jpeg, size_t n);

void fake_init(FakeBe *f)
{
    static int counter = 0;
    memset(f, 0, sizeof *f);
    snprintf(f->base, sizeof f->base, "/tmp/washi-test-%d-%d", (int)getpid(), counter++);
    fsx_rmtree(f->base);
    fsx_mkdir_p(f->base);
    fsx_join(f->sd, sizeof f->sd, f->base, "sd");
    fsx_join(f->committed, sizeof f->committed, f->base, "committed");
    fsx_join(f->live, sizeof f->live, f->base, "live");
    fsx_mkdir_p(f->sd);
    fsx_mkdir_p(f->committed);
    fsx_mkdir_p(f->live);
    f->battery_pct = 80;
    f->charging = 0;
    f->free_bytes = 1ull << 40;
    f->total_bytes = 2ull << 40;
    f->clock = 1790000000;
    f->clock_step = 1;
    f->rng = 0x2545F491u;
    f->sys_allowed = 1;
    snprintf(f->sys_nick, sizeof f->sys_nick, "Esports Switch");
    f->sys_img = malloc(24);
    memcpy(f->sys_img, "\xFF\xD8" "the original picture" "\xFF\xD9", 24);
    f->sys_img_size = 24;
    f->sys_ud[4] = 9;  // an original character icon (id 9) ...
    f->sys_ud[8] = 5;  // ... on background colour 5
    f->be.ctx = f;
    f->be.save_root = f->root;
    f->be.set_target = b_set_target;
    f->be.list_saves = b_list_saves;
    f->be.list_device_saves = b_list_device;
    f->be.save_size = b_save_size;
    f->be.mount = b_mount;
    f->be.create = b_create;
    f->be.commit = b_commit;
    f->be.unmount = b_unmount;
    f->be.journal_size = b_journal;
    f->be.write = b_write;
    f->be.title_name = b_title_name;
    f->be.title_icon = b_title_icon;
    f->be.sys_profile_read = b_sys_read;
    f->be.sys_profile_store = b_sys_store;
    f->be.list_accounts = b_accounts;
    f->be.battery = b_battery;
    f->be.free_space = b_free;
    f->be.total_space = b_total;
    f->be.now = b_now;
    f->be.random = b_random;
    f->be.checkpoint = b_checkpoint;
}

static int b_sys_read(Backend *be, char nick[40], uint8_t *user_data, uint8_t *img, size_t cap, size_t *img_size)
{
    FakeBe *f = be->ctx;
    if (!f->sys_allowed) return BE_ERR;
    memset(nick, 0, 40);
    snprintf(nick, 40, "%s", f->sys_nick);
    if (user_data) memcpy(user_data, f->sys_ud, sizeof f->sys_ud);
    if (img) {
        if (f->sys_img_size == 0 || f->sys_img_size > cap) return BE_ERR;
        memcpy(img, f->sys_img, f->sys_img_size);
        *img_size = f->sys_img_size;
    }
    return BE_OK;
}

static int b_sys_store(Backend *be, const char *nick, const uint8_t *user_data, const uint8_t *jpeg, size_t n)
{
    FakeBe *f = be->ctx;
    if (!f->sys_allowed) return BE_ERR;
    if (f->sys_fail_store) {
        if (f->sys_fail_store == 1) f->sys_fail_store = 0;
        return BE_ERR;
    }
    snprintf(f->sys_nick, sizeof f->sys_nick, "%.31s", nick);
    if (user_data) memcpy(f->sys_ud, user_data, sizeof f->sys_ud);
    if (jpeg && n) {
        free(f->sys_img);
        f->sys_img = malloc(n);
        memcpy(f->sys_img, jpeg, n);
        f->sys_img_size = n;
    }
    f->sys_stores++;
    return BE_OK;
}

void fake_destroy(FakeBe *f)
{
    free(f->sys_img);
    f->sys_img = NULL;
    fsx_rmtree(f->base);
}

void fake_add_title(FakeBe *f, uint64_t id, int installed, uint64_t journal)
{
    FakeTitle *t = &f->titles[f->ntitles++];
    t->id = id;
    t->installed = installed;
    t->journal = journal;
    t->running = 0;
}

void fake_name_title(FakeBe *f, uint64_t id, const char *name)
{
    FakeTitle *t = fake_title(f, id);
    if (t) snprintf(t->name, sizeof t->name, "%s", name);
}

FakeTitle *fake_title(FakeBe *f, uint64_t id)
{
    for (int i = 0; i < f->ntitles; i++)
        if (f->titles[i].id == id) return &f->titles[i];
    return NULL;
}

void fake_save_path(FakeBe *f, uint64_t title, char *out, size_t n)
{
    char t[17];
    tid(title, t);
    char rel[40];
    snprintf(rel, sizeof rel, "%s", t);
    fsx_join(out, n, f->committed, rel);
}

int fake_put(FakeBe *f, uint64_t title, const char *rel, const char *text)
{
    char dir[FSX_PATH], path[FSX_PATH], parent[FSX_PATH];
    fake_save_path(f, title, dir, sizeof dir);
    fsx_join(path, sizeof path, dir, rel);
    snprintf(parent, sizeof parent, "%s", path);
    char *slash = strrchr(parent, '/');
    if (slash) *slash = 0;
    if (fsx_mkdir_p(parent) != 0) return -1;
    return fsx_write_file(path, text, strlen(text));
}

int fake_put_n(FakeBe *f, uint64_t title, const char *rel, size_t bytes, const char *seed)
{
    char *buf = malloc(bytes + 1);
    size_t sl = strlen(seed);
    for (size_t i = 0; i < bytes; i++) buf[i] = (char)('a' + ((i * 7 + sl + seed[i % sl]) % 26));
    buf[bytes] = 0;
    int rc = fake_put(f, title, rel, buf);
    free(buf);
    return rc;
}

void fake_arm(FakeBe *f, long nth)
{
    f->ops = 0;
    f->fail_at = nth;
    f->crashed = 0;
}

void fake_reboot(FakeBe *f)
{
    f->crashed = 0;
    f->fail_at = 0;
    f->is_mounted = 0;
    f->uncommitted = 0;
    f->corrupt_next_write = 0;
}
