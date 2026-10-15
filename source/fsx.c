#include "fsx.h"

#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "hash.h"

void (*fsx_pump_hook)(void) = NULL;

int fsx_join(char *out, size_t n, const char *a, const char *b)
{
    size_t la = strlen(a);
    int slash = la > 0 && a[la - 1] != '/';
    int r = snprintf(out, n, "%s%s%s", a, slash ? "/" : "", b);
    return (r < 0 || (size_t)r >= n) ? -1 : 0;
}

int fsx_exists(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0;
}

int fsx_is_dir(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

int64_t fsx_mtime(const char *path)
{
    struct stat st;
    return stat(path, &st) == 0 ? (int64_t)st.st_mtime : 0;
}

int fsx_mkdir_p(const char *path)
{
    char tmp[FSX_PATH];
    if (strlen(path) >= sizeof tmp) return -1;
    strcpy(tmp, path);
    size_t len = strlen(tmp);
    while (len > 1 && tmp[len - 1] == '/') tmp[--len] = 0;
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        if (p[-1] == ':') continue; // "sdmc:/" is a device, not a folder
        *p = 0;
        mkdir(tmp, 0777); // existing folders fail harmlessly; the final is_dir check decides
        *p = '/';
    }
    mkdir(tmp, 0777);
    return fsx_is_dir(tmp) ? 0 : -1;
}

static int cmp_names(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

int fsx_list(const char *dir, char ***names, size_t *n)
{
    fsx_pump();
    DIR *d = opendir(dir);
    if (!d) return -1;
    size_t cap = 16, cnt = 0;
    char **v = malloc(cap * sizeof *v);
    if (!v) { closedir(d); return -1; }
    struct dirent *e;
    while ((e = readdir(d))) {
        if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
        if (cnt == cap) {
            cap *= 2;
            char **nv = realloc(v, cap * sizeof *v);
            if (!nv) goto fail;
            v = nv;
        }
        v[cnt] = strdup(e->d_name);
        if (!v[cnt]) goto fail;
        cnt++;
    }
    closedir(d);
    qsort(v, cnt, sizeof *v, cmp_names);
    *names = v;
    *n = cnt;
    return 0;
fail:
    closedir(d);
    fsx_list_free(v, cnt);
    return -1;
}

void fsx_list_free(char **names, size_t n)
{
    for (size_t i = 0; i < n; i++) free(names[i]);
    free(names);
}

int fsx_rmtree_ex(const char *path, int remove_self, FsxTick tick, void *user)
{
    // !remove_self means "empty this directory": it may be the root of a mounted save (save:/), so don't stat it
    if (remove_self) {
        if (!fsx_exists(path)) return 0;
        if (!fsx_is_dir(path)) {
            if (unlink(path) != 0) return -1;
            return tick ? tick(user) : 0;
        }
    }
    char **names;
    size_t n;
    if (fsx_list(path, &names, &n) != 0) return -1;
    int rc = 0;
    for (size_t i = 0; i < n && rc == 0; i++) {
        char child[FSX_PATH];
        if (fsx_join(child, sizeof child, path, names[i]) != 0) { rc = -1; break; }
        rc = fsx_rmtree_ex(child, 1, tick, user);
    }
    fsx_list_free(names, n);
    if (rc != 0) return rc;
    if (remove_self) {
        if (rmdir(path) != 0) return -1;
        return tick ? tick(user) : 0;
    }
    return 0;
}

int fsx_rmtree(const char *path)
{
    return fsx_rmtree_ex(path, 1, NULL, NULL);
}

int fsx_read_all(const char *path, char **out, size_t *len)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t cap = 4096, n = 0;
    char *buf = malloc(cap + 1);
    if (!buf) { fclose(f); return -1; }
    for (;;) {
        size_t r = fread(buf + n, 1, cap - n, f);
        n += r;
        if (n < cap) break;
        cap *= 2;
        char *nb = realloc(buf, cap + 1);
        if (!nb) { free(buf); fclose(f); return -1; }
        buf = nb;
    }
    fsx_pump();
    int bad = ferror(f);
    fclose(f);
    if (bad) { free(buf); return -1; }
    buf[n] = 0;
    *out = buf;
    if (len) *len = n;
    return 0;
}

int fsx_write_file(const char *path, const void *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    int ok = (len == 0 || fwrite(data, 1, len, f) == len);
    ok = (fflush(f) == 0) && ok;
    ok = (fclose(f) == 0) && ok;
    return ok ? 0 : -1;
}

// The SD card's FAT driver can't rename over an existing file, so: write <path>.tmp completely, remove <path>,
// rename. A reader that finds <path> missing but <path>.tmp present must use the .tmp (it is always complete).
int fsx_write_atomic(const char *path, const void *data, size_t len, int (*between)(void *), void *user)
{
    char tmp[FSX_PATH];
    if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return -1;
    if (fsx_write_file(tmp, data, len) != 0) return -1;
    if (between && between(user) != 0) return -1;
    if (fsx_exists(path) && unlink(path) != 0) return -1;
    if (between && between(user) != 0) return -1;
    return rename(tmp, path) == 0 ? 0 : -1;
}

int fsx_file_crc(const char *path, uint32_t *crc, uint64_t *size)
{
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    enum { CHUNK = 64 * 1024 };
    uint8_t *buf = malloc(CHUNK);
    if (!buf) { fclose(f); return -1; }
    uint32_t c = 0;
    uint64_t total = 0;
    size_t r;
    while ((r = fread(buf, 1, CHUNK, f)) > 0) {
        fsx_pump();
        c = crc32_update(c, buf, r);
        total += r;
    }
    int bad = ferror(f);
    free(buf);
    fclose(f);
    if (bad) return -1;
    *crc = c;
    *size = total;
    return 0;
}

uint64_t fsx_tree_size(const char *path)
{
    char **names;
    size_t n;
    if (fsx_list(path, &names, &n) != 0) { // not a directory (or can't be listed): a plain file
        struct stat st;
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) return 0;
        return (uint64_t)st.st_size;
    }
    uint64_t total = 0;
    for (size_t i = 0; i < n; i++) {
        char child[FSX_PATH];
        fsx_pump();
        if (fsx_join(child, sizeof child, path, names[i]) == 0) total += fsx_tree_size(child);
    }
    fsx_list_free(names, n);
    return total;
}

static int equal_rec(const char *a, const char *b)
{
    char **na, **nb;
    size_t ca, cb;
    if (fsx_list(a, &na, &ca) != 0) return -1;
    if (fsx_list(b, &nb, &cb) != 0) { fsx_list_free(na, ca); return -1; }
    int result = 1;
    if (ca != cb) result = 0;
    for (size_t i = 0; i < ca && result == 1; i++)
        if (strcmp(na[i], nb[i]) != 0) result = 0;
    // pass 1: kinds and sizes only (cheap), so a mismatch is found before any file is read
    for (size_t i = 0; i < ca && result == 1; i++) {
        char pa[FSX_PATH], pb[FSX_PATH];
        struct stat sa, sb;
        if (fsx_join(pa, sizeof pa, a, na[i]) || fsx_join(pb, sizeof pb, b, nb[i])) { result = -1; break; }
        if (stat(pa, &sa) != 0 || stat(pb, &sb) != 0) { result = -1; break; }
        if (S_ISDIR(sa.st_mode) != S_ISDIR(sb.st_mode)) result = 0;
        else if (!S_ISDIR(sa.st_mode) && sa.st_size != sb.st_size) result = 0;
    }
    // pass 2: contents
    for (size_t i = 0; i < ca && result == 1; i++) {
        char pa[FSX_PATH], pb[FSX_PATH];
        fsx_join(pa, sizeof pa, a, na[i]);
        fsx_join(pb, sizeof pb, b, nb[i]);
        if (fsx_is_dir(pa)) {
            result = equal_rec(pa, pb);
        } else {
            uint32_t c1, c2;
            uint64_t s1, s2;
            if (fsx_file_crc(pa, &c1, &s1) != 0 || fsx_file_crc(pb, &c2, &s2) != 0) result = -1;
            else if (c1 != c2 || s1 != s2) result = 0;
        }
    }
    fsx_list_free(na, ca);
    fsx_list_free(nb, cb);
    return result;
}

int fsx_tree_equal(const char *a, const char *b)
{
    return equal_rec(a, b);
}
