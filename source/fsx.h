// Small filesystem helpers over stdio/dirent. Work the same on the Switch (sdmc:/, save:/) and on the PC.
#ifndef FSX_H
#define FSX_H

#include <stddef.h>
#include <stdint.h>

#define FSX_PATH 768

// Called now and then from the loops that walk folders and read files (the SD card is slow): the interface sets it while the app is busy between two screens,
// to keep a mark of "still working" moving. NULL: nothing.
extern void (*fsx_pump_hook)(void);
static inline void fsx_pump(void) { if (fsx_pump_hook) fsx_pump_hook(); }

typedef int (*FsxTick)(void *user); // called after each removed entry; non-zero aborts

int fsx_join(char *out, size_t n, const char *a, const char *b); // a + "/" + b (no doubled slash); 0 ok
int fsx_exists(const char *path);
int fsx_is_dir(const char *path);
int64_t fsx_mtime(const char *path); // when it was last changed (seconds since 1970), 0 if it is not there
int fsx_mkdir_p(const char *path); // 0 ok
int fsx_rmtree(const char *path);  // removes path and everything below it
int fsx_rmtree_ex(const char *path, int remove_self, FsxTick tick, void *user);
int fsx_list(const char *dir, char ***names, size_t *n); // sorted, without . and ..; 0 ok
void fsx_list_free(char **names, size_t n);
int fsx_read_all(const char *path, char **out, size_t *len); // NUL-terminated; free() it
int fsx_write_file(const char *path, const void *data, size_t len);
// Write <path>.tmp, then replace <path>. `between` (optional) runs at the two points where a crash would matter.
int fsx_write_atomic(const char *path, const void *data, size_t len, int (*between)(void *), void *user);
int fsx_file_crc(const char *path, uint32_t *crc, uint64_t *size);
uint64_t fsx_tree_size(const char *path);
// 1 = identical (names, kinds, sizes, CRC32 of every file), 0 = different, -1 = I/O error
int fsx_tree_equal(const char *a, const char *b);

#endif
