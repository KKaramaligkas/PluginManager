/*
    Plugin Manager for ARK-5
    fs.c: file system layer (sceIo on the PSP, POSIX on the host).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fs.h"
#include "util.h"

#ifdef __PSP__

#include <pspiofilemgr.h>
#include <pspkernel.h>

fs_file fs_open(const char *path, int mode)
{
    int flags = PSP_O_RDONLY;
    if (mode == FS_WRITE) flags = PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC;
    else if (mode == FS_APPEND) flags = PSP_O_WRONLY | PSP_O_CREAT | PSP_O_APPEND;
    return sceIoOpen(path, flags, 0777);
}

int fs_read(fs_file f, void *buf, int size) { return sceIoRead(f, buf, size); }
int fs_write(fs_file f, const void *buf, int size) { return sceIoWrite(f, buf, size); }
int64_t fs_seek(fs_file f, int64_t offset, int whence) { return sceIoLseek(f, offset, whence); }
void fs_close(fs_file f) { if (f >= 0) sceIoClose(f); }

static int stat_path(const char *path, SceIoStat *st)
{
    char tmp[PM_PATH_MAX];
    size_t n = pm_strlcpy(tmp, path, sizeof(tmp));
    /* sceIoGetstat fails on directories with a trailing slash */
    while (n > 5 && tmp[n - 1] == '/') tmp[--n] = 0;
    memset(st, 0, sizeof(*st));
    return sceIoGetstat(tmp, st);
}

int fs_exists(const char *path)
{
    SceIoStat st;
    if (stat_path(path, &st) >= 0) return 1;
    /* the root of a device ("ms0:/") can't be stat'ed on some firmwares */
    SceUID d = sceIoDopen(path);
    if (d >= 0) {
        sceIoDclose(d);
        return 1;
    }
    return 0;
}

int fs_is_dir(const char *path)
{
    SceIoStat st;
    if (stat_path(path, &st) >= 0) return FIO_S_ISDIR(st.st_mode) ? 1 : 0;
    SceUID d = sceIoDopen(path);
    if (d >= 0) {
        sceIoDclose(d);
        return 1;
    }
    return 0;
}

int64_t fs_size(const char *path)
{
    SceIoStat st;
    if (stat_path(path, &st) < 0) return -1;
    return st.st_size;
}

int fs_mkdir(const char *path)
{
    char tmp[PM_PATH_MAX];
    size_t n = pm_strlcpy(tmp, path, sizeof(tmp));
    while (n > 5 && tmp[n - 1] == '/') tmp[--n] = 0;
    int r = sceIoMkdir(tmp, 0777);
    if (r >= 0 || fs_is_dir(tmp)) return 0;
    return r;
}

int fs_remove(const char *path) { return sceIoRemove(path) >= 0 ? 0 : -1; }

int fs_rmdir(const char *path)
{
    char tmp[PM_PATH_MAX];
    size_t n = pm_strlcpy(tmp, path, sizeof(tmp));
    while (n > 5 && tmp[n - 1] == '/') tmp[--n] = 0;
    return sceIoRmdir(tmp) >= 0 ? 0 : -1;
}

int fs_rename(const char *from, const char *to)
{
    return sceIoRename(from, to) >= 0 ? 0 : -1;
}

const char *fs_native_path(const char *path, char *buf, int size)
{
    (void)buf;
    (void)size;
    return path;
}

int fs_list(const char *path, int (*cb)(void *ud, const char *name, int is_dir), void *ud)
{
    SceUID d = sceIoDopen(path);
    if (d < 0) return -1;
    SceIoDirent dit;
    memset(&dit, 0, sizeof(dit));
    while (sceIoDread(d, &dit) > 0) {
        if (strcmp(dit.d_name, ".") && strcmp(dit.d_name, "..")) {
            if (cb(ud, dit.d_name, FIO_S_ISDIR(dit.d_stat.st_mode) ? 1 : 0)) break;
        }
        memset(&dit, 0, sizeof(dit));
    }
    sceIoDclose(d);
    return 0;
}

#else /* host implementation used by the unit tests */

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

/* "ms0:/a/b" -> "$PM_FS_ROOT/ms0/a/b" */
static const char *map_path(const char *path, char *buf, size_t size)
{
    const char *colon = strchr(path, ':');
    const char *root = getenv("PM_FS_ROOT");
    if (!colon || !root || colon - path > 8) return path;
    const char *rest = colon + 1;
    while (*rest == '/') rest++;
    snprintf(buf, size, "%s/%.*s/%s", root, (int)(colon - path), path, rest);
    return buf;
}

const char *fs_native_path(const char *path, char *buf, int size)
{
    return map_path(path, buf, size);
}

fs_file fs_open(const char *path, int mode)
{
    char buf[1024];
    const char *p = map_path(path, buf, sizeof(buf));
    int flags = O_RDONLY;
    if (mode == FS_WRITE) flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (mode == FS_APPEND) flags = O_WRONLY | O_CREAT | O_APPEND;
    int fd = open(p, flags, 0644);
    return fd < 0 ? -1 : fd;
}

int fs_read(fs_file f, void *buf, int size) { return (int)read(f, buf, size); }
int fs_write(fs_file f, const void *buf, int size) { return (int)write(f, buf, size); }
int64_t fs_seek(fs_file f, int64_t offset, int whence) { return lseek(f, offset, whence); }
void fs_close(fs_file f) { if (f >= 0) close(f); }

int fs_exists(const char *path)
{
    char buf[1024];
    struct stat st;
    return stat(map_path(path, buf, sizeof(buf)), &st) == 0;
}

int fs_is_dir(const char *path)
{
    char buf[1024];
    struct stat st;
    return stat(map_path(path, buf, sizeof(buf)), &st) == 0 && S_ISDIR(st.st_mode);
}

int64_t fs_size(const char *path)
{
    char buf[1024];
    struct stat st;
    if (stat(map_path(path, buf, sizeof(buf)), &st) != 0) return -1;
    return st.st_size;
}

int fs_mkdir(const char *path)
{
    char buf[1024];
    const char *p = map_path(path, buf, sizeof(buf));
    if (mkdir(p, 0755) == 0 || errno == EEXIST) return 0;
    return -1;
}

int fs_remove(const char *path)
{
    char buf[1024];
    return unlink(map_path(path, buf, sizeof(buf)));
}

int fs_rmdir(const char *path)
{
    char buf[1024];
    return rmdir(map_path(path, buf, sizeof(buf)));
}

int fs_rename(const char *from, const char *to)
{
    char b1[1024], b2[1024];
    return rename(map_path(from, b1, sizeof(b1)), map_path(to, b2, sizeof(b2)));
}

int fs_list(const char *path, int (*cb)(void *ud, const char *name, int is_dir), void *ud)
{
    char buf[1024];
    const char *p = map_path(path, buf, sizeof(buf));
    DIR *d = opendir(p);
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d))) {
        if (!strcmp(de->d_name, ".") || !strcmp(de->d_name, "..")) continue;
        char full[1200];
        struct stat st;
        snprintf(full, sizeof(full), "%s/%s", p, de->d_name);
        int is_dir = stat(full, &st) == 0 && S_ISDIR(st.st_mode);
        if (cb(ud, de->d_name, is_dir)) break;
    }
    closedir(d);
    return 0;
}

#endif

int fs_mkdirs(const char *path, void (*created)(void *ud, const char *dir), void *ud)
{
    char tmp[PM_PATH_MAX];
    pm_strlcpy(tmp, path, sizeof(tmp));

    /* skip the device part ("ms0:/") */
    char *p = strchr(tmp, ':');
    p = p ? p + 1 : tmp;
    while (*p == '/') p++;

    for (;;) {
        char *slash = strchr(p, '/');
        char saved = 0;
        if (slash) {
            saved = slash[1];
            slash[1] = 0;
        }
        if (*p) {
            if (!fs_is_dir(tmp)) {
                if (fs_mkdir(tmp) < 0) return -1;
                if (created) created(ud, tmp);
            }
        }
        if (!slash) break;
        slash[1] = saved;
        p = slash + 1;
        if (!*p) break;
    }
    return 0;
}

static int count_cb(void *ud, const char *name, int is_dir)
{
    (void)name;
    (void)is_dir;
    (*(int *)ud)++;
    return 1;
}

int fs_dir_empty(const char *path)
{
    int n = 0;
    if (fs_list(path, count_cb, &n) < 0) return 1;
    return n == 0;
}

char *fs_read_all(const char *path, int *size_out, int max_size)
{
    fs_file f = fs_open(path, FS_READ);
    if (f < 0) return NULL;
    int64_t size = fs_seek(f, 0, 2);
    fs_seek(f, 0, 0);
    if (size < 0 || (max_size > 0 && size > max_size)) {
        fs_close(f);
        return NULL;
    }
    char *buf = malloc((size_t)size + 1);
    if (!buf) {
        fs_close(f);
        return NULL;
    }
    int got = 0;
    while (got < size) {
        int n = fs_read(f, buf + got, (int)size - got);
        if (n <= 0) break;
        got += n;
    }
    fs_close(f);
    if (got != size) {
        free(buf);
        return NULL;
    }
    buf[size] = 0;
    if (size_out) *size_out = (int)size;
    return buf;
}

int fs_write_all(const char *path, const void *data, int size)
{
    char tmp[PM_PATH_MAX];
    if (snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp)) return -1;
    fs_file f = fs_open(tmp, FS_WRITE);
    if (f < 0) return -1;
    int written = size ? fs_write(f, data, size) : 0;
    fs_close(f);
    if (written != size) {
        fs_remove(tmp);
        return -1;
    }
    fs_remove(path);
    if (fs_rename(tmp, path) < 0) {
        /* some drivers refuse renames: fall back to a direct write */
        fs_remove(tmp);
        f = fs_open(path, FS_WRITE);
        if (f < 0) return -1;
        written = size ? fs_write(f, data, size) : 0;
        fs_close(f);
        return written == size ? 0 : -1;
    }
    return 0;
}

int fs_copy(const char *from, const char *to)
{
    fs_file in = fs_open(from, FS_READ);
    if (in < 0) return -1;
    fs_file out = fs_open(to, FS_WRITE);
    if (out < 0) {
        fs_close(in);
        return -1;
    }
    char *buf = malloc(64 * 1024);
    int ret = buf ? 0 : -1;
    while (buf) {
        int n = fs_read(in, buf, 64 * 1024);
        if (n < 0) {
            ret = -1;
            break;
        }
        if (n == 0) break;
        if (fs_write(out, buf, n) != n) {
            ret = -1;
            break;
        }
    }
    free(buf);
    fs_close(in);
    fs_close(out);
    if (ret < 0) fs_remove(to);
    return ret;
}
