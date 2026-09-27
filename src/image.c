/*
    Plugin Manager for ARK-5
    image.c: PNG loading into textures (libpng), including ICON0 from PBPs.
*/

#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include <png.h>

#include "fs.h"
#include "image.h"

#define MAX_PNG_FILE (1024 * 1024)

typedef struct {
    fs_file f;
    int64_t left;       /* bytes still readable (PBP sections are bounded) */
} png_src;

static void read_fn(png_structp png, png_bytep data, png_size_t len)
{
    png_src *src = png_get_io_ptr(png);
    if ((int64_t)len > src->left || fs_read(src->f, data, (int)len) != (int)len)
        png_error(png, "read error");
    src->left -= len;
}

static texture *decode(fs_file f, int64_t size)
{
    png_structp png = NULL;
    png_infop info = NULL;
    texture *volatile tex = NULL;
    png_bytep *volatile rows = NULL;
    png_src src = { f, size };

    unsigned char sig[8];
    if (size < 8 || fs_read(f, sig, 8) != 8 || png_sig_cmp(sig, 0, 8) != 0) return NULL;
    src.left -= 8;

    png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    if (!png) return NULL;
    info = png_create_info_struct(png);
    if (!info) {
        png_destroy_read_struct(&png, NULL, NULL);
        return NULL;
    }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        free(rows);
        tex_free(tex);
        return NULL;
    }

    png_set_read_fn(png, &src, read_fn);
    png_set_sig_bytes(png, 8);
    png_read_info(png, info);

    png_uint_32 w, h;
    int depth, type;
    png_get_IHDR(png, info, &w, &h, &depth, &type, NULL, NULL, NULL);
    if (w == 0 || h == 0 || w > 512 || h > 512) png_error(png, "size");

    if (type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (type == PNG_COLOR_TYPE_GRAY && depth < 8) png_set_expand_gray_1_2_4_to_8(png);
    if (png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
    if (depth == 16) png_set_strip_16(png);
    if (type == PNG_COLOR_TYPE_GRAY || type == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
    png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    png_set_interlace_handling(png);
    png_read_update_info(png, info);
    if (png_get_rowbytes(png, info) != w * 4) png_error(png, "format");

    tex = tex_create((int)w, (int)h);
    rows = malloc(sizeof(png_bytep) * h);
    if (!tex || !rows) png_error(png, "memory");
    for (png_uint_32 y = 0; y < h; y++) rows[y] = (png_bytep)(tex->pixels + y * tex->tw);
    png_read_image(png, rows);
    png_read_end(png, NULL);

    png_destroy_read_struct(&png, &info, NULL);
    free(rows);
    tex_flush(tex);
    return tex;
}

texture *image_load_png(const char *path)
{
    int64_t size = fs_size(path);
    if (size <= 0 || size > MAX_PNG_FILE) return NULL;
    fs_file f = fs_open(path, FS_READ);
    if (f < 0) return NULL;
    texture *t = decode(f, size);
    fs_close(f);
    return t;
}

texture *image_load_pbp_icon(const char *pbp_path)
{
    u32 hdr[10];
    fs_file f = fs_open(pbp_path, FS_READ);
    if (f < 0) return NULL;
    texture *t = NULL;
    if (fs_read(f, hdr, sizeof(hdr)) == sizeof(hdr) && hdr[0] == 0x50425000) {
        u32 start = hdr[3], end = hdr[4];      /* ICON0 .. ICON1 offsets */
        if (end > start && end - start <= MAX_PNG_FILE && fs_seek(f, start, 0) == start)
            t = decode(f, end - start);
    }
    fs_close(f);
    return t;
}

int image_png_valid(const char *path)
{
    int64_t size = fs_size(path);
    if (size < 57 || size > MAX_PNG_FILE) return 0;
    fs_file f = fs_open(path, FS_READ);
    if (f < 0) return 0;

    static const unsigned char magic[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
    unsigned char buf[8];
    int ok = fs_read(f, buf, 8) == 8 && memcmp(buf, magic, 8) == 0;
    int64_t pos = 8;
    int first = 1, seen_end = 0;

    /* walk the chunk list: IHDR first, IEND within the file */
    while (ok && pos + 12 <= size) {
        if (fs_read(f, buf, 8) != 8) {
            ok = 0;
            break;
        }
        u32 len = (u32)buf[0] << 24 | (u32)buf[1] << 16 | (u32)buf[2] << 8 | buf[3];
        if (first) {
            unsigned char dim[8];
            if (memcmp(buf + 4, "IHDR", 4) != 0 || len != 13 || fs_read(f, dim, 8) != 8) {
                ok = 0;
                break;
            }
            u32 w = (u32)dim[0] << 24 | (u32)dim[1] << 16 | (u32)dim[2] << 8 | dim[3];
            u32 h = (u32)dim[4] << 24 | (u32)dim[5] << 16 | (u32)dim[6] << 8 | dim[7];
            if (w == 0 || h == 0 || w > 512 || h > 512) ok = 0;
            fs_seek(f, pos + 8, 0);
            first = 0;
        }
        if (memcmp(buf + 4, "IEND", 4) == 0) {
            seen_end = 1;
            break;
        }
        if ((int64_t)len > size) {
            ok = 0;
            break;
        }
        pos += 12 + (int64_t)len;
        if (fs_seek(f, pos, 0) != pos) ok = 0;
    }
    fs_close(f);
    return ok && seen_end;
}
