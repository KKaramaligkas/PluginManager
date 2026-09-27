/*
    Developer tool: installs store entries into a memory stick folder on the
    PC with the same engine the PSP app uses (handy to prepare an emulator
    memory stick or to check a store entry).

    PM_FS_ROOT=<dir containing ms0/> ./hostinstall store.json <pkg-dir> id [id...]
    Packages are taken from <pkg-dir>/<last URL component>. An id written as
    id@version pretends that version is installed (to show updates).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/db.h"
#include "../src/fs.h"
#include "../src/installer.h"
#include "../src/store.h"
#include "../src/util.h"

static const char *pkg_dir;

static int local_download(install_ctx *ctx, const char *url, const char *dest, char *err, int errlen)
{
    char src[1024];
    snprintf(src, sizeof(src), "%s/%s", pkg_dir, pm_basename(url));
    char native[1024];
    FILE *in = fopen(src, "rb");
    FILE *out = in ? fopen(fs_native_path(dest, native, sizeof(native)), "wb") : NULL;
    if (!in || !out) {
        if (in) fclose(in);
        snprintf(err, errlen, "missing %s", src);
        return -1;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc < 4 || !getenv("PM_FS_ROOT")) {
        fprintf(stderr, "usage: PM_FS_ROOT=dir %s store.json pkg-dir id[@version]...\n", argv[0]);
        return 2;
    }
    pkg_dir = argv[2];

    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = calloc(1, n + 1);
    if (fread(json, 1, n, f) != (size_t)n) return 2;
    fclose(f);

    char err[256];
    store_t st;
    if (store_parse(&st, json, NULL, err, sizeof(err)) < 0) {
        fprintf(stderr, "store: %s\n", err);
        return 1;
    }

    const char *db_file = "ms0:/PSP/APPS/PluginManager/data/installed.json";
    const char *xmb_file = "ms0:/PSP/APPS/PluginManager/data/xmbnames.txt";
    db_t db;
    db_load(&db, db_file);

    install_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strcpy(ctx.root, "ms0:/");
    strcpy(ctx.ark_path, "ms0:/PSP/SAVEDATA/ARK_01234/");
    strcpy(ctx.temp_dir, "ms0:/PSP/APPS/PluginManager/data/tmp/");
    strcpy(ctx.protect_dir, "ms0:/PSP/APPS/PluginManager/data/");
    ctx.model = MODEL_3000;
    ctx.download = local_download;
    ctx.store_url = st.url;

    int ret = 0;
    for (int i = 3; i < argc; i++) {
        char id[64];
        pm_strlcpy(id, argv[i], sizeof(id));
        char *at = strchr(id, '@');
        if (at) *at++ = 0;
        const store_entry *e = store_find(&st, id);
        if (!e) {
            fprintf(stderr, "%s: not in the store\n", id);
            ret = 1;
            continue;
        }
        if (installer_install(&ctx, e, &db, err, sizeof(err)) < 0) {
            fprintf(stderr, "%s: %s\n", id, err);
            ret = 1;
            continue;
        }
        if (at) {
            db_package *p = db_find(&db, id);
            free(p->version);
            p->version = pm_strdup(at);
        }
        printf("installed %s\n", id);
    }
    db_save(&db, db_file);
    db_write_xmb_index(&db, NULL, 0, xmb_file);
    db_free(&db);
    store_free(&st);
    free(json);
    return ret;
}
