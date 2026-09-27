/*
    Plugin Manager for ARK-5
    archive.c: extraction of zip, rar, 7z, tar and tar.gz archives (unarr + zlib).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unarr.h>
#include <zlib.h>

#include "archive.h"
#include "fs.h"
#include "util.h"

#define CHUNK (64 * 1024)

const char *archive_detect(const char *path)
{
    unsigned char m[265];
    memset(m, 0, sizeof(m));
    fs_file f = fs_open(path, FS_READ);
    if (f < 0) return NULL;
    int n = fs_read(f, m, sizeof(m));
    fs_close(f);
    if (n < 4) return NULL;

    if (m[0] == 'P' && m[1] == 'K' && (m[2] == 3 || m[2] == 5 || m[2] == 7)) return "zip";
    if (memcmp(m, "Rar!\x1a\x07", 6) == 0) return "rar";
    if (memcmp(m, "7z\xbc\xaf\x27\x1c", 6) == 0) return "7z";
    if (m[0] == 0x1f && m[1] == 0x8b) return "gz";
    if (n >= 262 && memcmp(m + 257, "ustar", 5) == 0) return "tar";
    return NULL;
}

/* Decompresses a .gz file into `out` using the fs layer. */
static int gunzip(const char *in, const char *out, char *err, int errlen)
{
    fs_file fi = fs_open(in, FS_READ);
    if (fi < 0) {
        snprintf(err, errlen, "Can't open %s", pm_basename(in));
        return -1;
    }
    fs_file fo = fs_open(out, FS_WRITE);
    if (fo < 0) {
        fs_close(fi);
        snprintf(err, errlen, "Can't create %s", pm_basename(out));
        return -1;
    }

    unsigned char *ibuf = malloc(CHUNK), *obuf = malloc(CHUNK);
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    int ret = -1, zr = Z_OK;

    if (!ibuf || !obuf || inflateInit2(&zs, 16 + MAX_WBITS) != Z_OK) {
        snprintf(err, errlen, "Out of memory");
        goto done;
    }

    for (;;) {
        int n = fs_read(fi, ibuf, CHUNK);
        if (n < 0) {
            snprintf(err, errlen, "Read error");
            goto end;
        }
        if (n == 0) break;
        zs.next_in = ibuf;
        zs.avail_in = n;
        do {
            zs.next_out = obuf;
            zs.avail_out = CHUNK;
            zr = inflate(&zs, Z_NO_FLUSH);
            if (zr != Z_OK && zr != Z_STREAM_END && zr != Z_BUF_ERROR) {
                snprintf(err, errlen, "Corrupted gzip data");
                goto end;
            }
            int produced = CHUNK - zs.avail_out;
            if (produced && fs_write(fo, obuf, produced) != produced) {
                snprintf(err, errlen, "Write error (storage full?)");
                goto end;
            }
            if (zr == Z_STREAM_END) {
                /* concatenated gzip members */
                if (zs.avail_in) inflateReset(&zs);
                else break;
            }
        } while (zs.avail_in > 0 || zs.avail_out == 0);
    }
    if (zr != Z_STREAM_END) {
        snprintf(err, errlen, "Truncated gzip data");
        goto end;
    }
    ret = 0;

end:
    inflateEnd(&zs);
done:
    free(ibuf);
    free(obuf);
    fs_close(fi);
    fs_close(fo);
    if (ret < 0) fs_remove(out);
    return ret;
}

static ar_archive *open_archive(ar_stream *s, const char *type)
{
    if (strcmp(type, "zip") == 0) return ar_open_zip_archive(s, false);
    if (strcmp(type, "rar") == 0) return ar_open_rar_archive(s);
    if (strcmp(type, "7z") == 0) return ar_open_7z_archive(s);
    if (strcmp(type, "tar") == 0) return ar_open_tar_archive(s);
    return NULL;
}

static int extract_file(ar_archive *ar, size_t size, const char *dest, const archive_opts *opts,
                        int64_t *done, int64_t total, unsigned char *buf, char *err, int errlen)
{
    char dir[PM_PATH_MAX];
    pm_dirname(dest, dir, sizeof(dir));
    if (dir[0] && fs_mkdirs(dir, NULL, NULL) < 0) {
        snprintf(err, errlen, "Can't create folder %s", dir);
        return -1;
    }

    fs_file f = fs_open(dest, FS_WRITE);
    if (f < 0) {
        snprintf(err, errlen, "Can't write %s", dest);
        return -1;
    }

    size_t left = size;
    while (left > 0) {
        size_t n = left > CHUNK ? CHUNK : left;
        if (!ar_entry_uncompress(ar, buf, n)) {
            fs_close(f);
            fs_remove(dest);
            snprintf(err, errlen, "Decompression failed for %s", pm_basename(dest));
            return -1;
        }
        if (fs_write(f, buf, (int)n) != (int)n) {
            fs_close(f);
            fs_remove(dest);
            snprintf(err, errlen, "Write error (storage full?)");
            return -1;
        }
        left -= n;
        *done += n;
        if (opts->progress && opts->progress(opts->ud, *done, total)) {
            fs_close(f);
            fs_remove(dest);
            snprintf(err, errlen, "Cancelled");
            return -1;
        }
    }
    fs_close(f);
    if (opts->written) opts->written(opts->ud, dest);
    return 0;
}

static int extract_with_unarr(const char *path, const char *type, const archive_opts *opts,
                              char *err, int errlen)
{
    char nbuf[1024];
    ar_stream *s = ar_open_file(fs_native_path(path, nbuf, sizeof(nbuf)));
    if (!s) {
        snprintf(err, errlen, "Can't open %s", pm_basename(path));
        return -1;
    }
    ar_archive *ar = open_archive(s, type);
    if (!ar) {
        ar_close(s);
        snprintf(err, errlen, "Unsupported or damaged %s archive", type);
        return -1;
    }

    /* first pass: total size for the progress bar */
    int64_t total = 0, done = 0;
    while (ar_parse_entry(ar)) total += ar_entry_get_size(ar);
    ar_close_archive(ar);
    ar_seek(s, 0, SEEK_SET);
    ar = open_archive(s, type);
    if (!ar) {
        ar_close(s);
        snprintf(err, errlen, "Unsupported or damaged %s archive", type);
        return -1;
    }

    unsigned char *buf = malloc(CHUNK);
    if (!buf) {
        ar_close_archive(ar);
        ar_close(s);
        snprintf(err, errlen, "Out of memory");
        return -1;
    }

    int count = 0, ret = 0;
    while (ar_parse_entry(ar)) {
        const char *name = ar_entry_get_name(ar);
        size_t size = ar_entry_get_size(ar);
        if (!name) continue;

        size_t nl = strlen(name);
        if (nl && (name[nl - 1] == '/' || name[nl - 1] == '\\')) {
            done += size;
            continue;   /* directory entry, folders are created on demand */
        }

        char rel[PM_PATH_MAX];
        if (pm_sanitize_relpath(name, rel, sizeof(rel)) < 0) {
            done += size;
            continue;   /* never extract "../" or absolute paths */
        }

        char dest[PM_PATH_MAX];
        dest[0] = 0;
        int sel = opts->select(opts->ud, rel, (int64_t)size, dest, sizeof(dest));
        if (sel < 0) {
            if (!err[0]) snprintf(err, errlen, "Refused archive entry %s", rel);
            ret = -1;
            break;
        }
        if (sel == 0 || !dest[0]) {
            done += size;
            continue;
        }

        if (extract_file(ar, size, dest, opts, &done, total, buf, err, errlen) < 0) {
            ret = -1;
            break;
        }
        count++;
    }

    if (ret == 0 && !ar_at_eof(ar)) {
        snprintf(err, errlen, "The archive is damaged or uses an unsupported compression");
        ret = -1;
    }

    free(buf);
    ar_close_archive(ar);
    ar_close(s);
    return ret < 0 ? -1 : count;
}

int archive_extract(const char *path, const archive_opts *opts, char *err, int errlen)
{
    err[0] = 0;
    const char *type = archive_detect(path);
    if (!type) {
        snprintf(err, errlen, "%s is not a supported archive", pm_basename(path));
        return -1;
    }

    if (strcmp(type, "gz") == 0) {
        char tar[PM_PATH_MAX];
        if (pm_path_join(tar, sizeof(tar), opts->temp_dir ? opts->temp_dir : "", "gunzip.tar") < 0) {
            snprintf(err, errlen, "Path too long");
            return -1;
        }
        if (gunzip(path, tar, err, errlen) < 0) return -1;
        const char *inner = archive_detect(tar);
        int ret = -1;
        if (inner && strcmp(inner, "tar") == 0)
            ret = extract_with_unarr(tar, "tar", opts, err, errlen);
        else
            snprintf(err, errlen, "Only .tar.gz archives are supported");
        fs_remove(tar);
        return ret;
    }

    return extract_with_unarr(path, type, opts, err, errlen);
}
