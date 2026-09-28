/*
    Integration test: installs, updates and uninstalls every entry of the seed
    store into a fake memory stick (PM_FS_ROOT).

    Downloads are served from PM_PKG_DIR (files named like the URL's last
    component). With PM_LIVE=1 the real URLs are fetched with curl instead,
    which also validates the URLs and checksums of the store.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../src/db.h"
#include "../src/fs.h"
#include "../src/installer.h"
#include "../src/pluginstxt.h"
#include "../src/store.h"
#include "../src/util.h"
#include "test.h"

int test_failures = 0;
int test_checks = 0;

static int live = 0;
static const char *pkg_dir;
static int downloads = 0;

static int fake_download(install_ctx *ctx, const char *url, const char *dest, char *err, int errlen)
{
    (void)ctx;
    char native[1024];
    downloads++;
    if (live) {
        char cmd[2048];
        snprintf(cmd, sizeof(cmd), "curl -sSfL --max-time 600 -o '%s' '%s'", fs_native_path(dest, native, sizeof(native)), url);
        if (system(cmd) != 0) {
            snprintf(err, errlen, "curl failed for %s", url);
            return -1;
        }
        return 0;
    }
    char src[1024];
    snprintf(src, sizeof(src), "%s/%s", pkg_dir, pm_basename(url));
    FILE *in = fopen(src, "rb");
    if (!in) {
        snprintf(err, errlen, "SKIP: no local copy of %s", pm_basename(url));
        return -1;
    }
    FILE *out = fopen(fs_native_path(dest, native, sizeof(native)), "wb");
    if (!out) {
        fclose(in);
        snprintf(err, errlen, "can't write %s", dest);
        return -1;
    }
    char buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 0;
}

static void init_ctx(install_ctx *ctx, const char *root, int model)
{
    memset(ctx, 0, sizeof(*ctx));
    snprintf(ctx->root, sizeof(ctx->root), "%s", root);
    strcpy(ctx->ark_path, "ms0:/PSP/SAVEDATA/ARK_01234/");
    snprintf(ctx->temp_dir, sizeof(ctx->temp_dir), "%sPSP/APPS/PluginManager/data/tmp/", root);
    snprintf(ctx->protect_dir, sizeof(ctx->protect_dir), "%sPSP/APPS/PluginManager/data/", root);
    ctx->model = model;
    ctx->download = fake_download;
    ctx->store_url = "https://example.org/store.json";
}

static char *plugins_txt(const char *root)
{
    char file[256];
    installer_plugins_txt(root, file, sizeof(file));
    char *t = fs_read_all(file, NULL, 0);
    return t ? t : pm_strdup("");
}

/* counts regular files below a device root, ignoring PLUGINS.TXT and our data */
static int count_files(const char *dir)
{
    int n = 0;
    struct stat st;
    char native[1024];
    const char *p = fs_native_path(dir, native, sizeof(native));
    if (stat(p, &st) != 0) return 0;
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "find '%s' -type f ! -iname PLUGINS.TXT ! -path '*/PluginManager/data/*' | wc -l", p);
    FILE *f = popen(cmd, "r");
    if (f) {
        if (fscanf(f, "%d", &n) != 1) n = -1;
        pclose(f);
    }
    return n;
}

static int count_dirs(const char *dir)
{
    int n = 0;
    char native[1024];
    char cmd[1200];
    snprintf(cmd, sizeof(cmd), "find '%s' -mindepth 1 -type d ! -path '*/PluginManager*' ! -iname SEPLUGINS | wc -l",
             fs_native_path(dir, native, sizeof(native)));
    FILE *f = popen(cmd, "r");
    if (f) {
        if (fscanf(f, "%d", &n) != 1) n = -1;
        pclose(f);
    }
    return n;
}

static int install(install_ctx *ctx, const store_t *st, db_t *db, const char *id, char *err)
{
    const store_entry *e = store_find(st, id);
    CHECK(e != NULL);
    if (!e) return -1;
    int r = installer_install(ctx, e, db, err, 256);
    if (r < 0 && strncmp(err, "SKIP", 4) != 0) fprintf(stderr, "install %s: %s\n", id, err);
    return r;
}

int main(void)
{
    const char *store_file = getenv("PM_SEED_STORE");
    pkg_dir = getenv("PM_PKG_DIR");
    live = getenv("PM_LIVE") && atoi(getenv("PM_LIVE"));
    if (!getenv("PM_FS_ROOT") || !store_file || (!pkg_dir && !live)) {
        fprintf(stderr, "needs PM_FS_ROOT, PM_SEED_STORE and PM_PKG_DIR (or PM_LIVE=1)\n");
        return 2;
    }

    int size;
    char native[1024];
    FILE *f = fopen(store_file, "rb");
    if (!f) return 2;
    fseek(f, 0, SEEK_END);
    size = (int)ftell(f);
    fseek(f, 0, SEEK_SET);
    char *json = calloc(1, size + 1);
    if ((int)fread(json, 1, size, f) != size) return 2;
    fclose(f);

    store_t st;
    char err[256];
    CHECK_INT(store_parse(&st, json, NULL, err, sizeof(err)), 0);

    fs_mkdirs("ms0:/SEPLUGINS/", NULL, NULL);
    fs_mkdirs("ef0:/SEPLUGINS/", NULL, NULL);
    const char *user_line = "vsh, ms0:/SEPLUGINS/mine.prx, on\n";
    fs_write_all("ms0:/SEPLUGINS/PLUGINS.TXT", user_line, (int)strlen(user_line));

    install_ctx ctx;
    db_t db;
    memset(&db, 0, sizeof(db));
    init_ctx(&ctx, "ms0:/", MODEL_3000);

    /* ---- install everything on a PSP-3000 ---- */
    int installed = 0, skipped = 0;
    for (int i = 0; i < st.count; i++) {
        const store_entry *e = &st.entries[i];
        int r = installer_install(&ctx, e, &db, err, sizeof(err));
        if (r < 0 && strncmp(err, "SKIP", 4) == 0) {
            printf("  skip %-14s (%s)\n", e->id, err + 6);
            skipped++;
            continue;
        }
        if (r < 0) fprintf(stderr, "install %s failed: %s\n", e->id, err);
        CHECK_INT(r, 0);
        if (r == 0) {
            installed++;
            db_package *p = db_find(&db, e->id);
            printf("  ok   %-14s %3d files, %d plugin lines%s%s\n", e->id, p->n_files, p->n_plugins,
                   ctx.messages[0] ? " | " : "", ctx.messages);
        }
    }
    CHECK(installed >= 11);

    char *txt = plugins_txt("ms0:/");
    printf("---- PLUGINS.TXT ----\n%s---------------------\n", txt);
    CHECK(strstr(txt, "vsh, ms0:/SEPLUGINS/mine.prx, on\n") != NULL);
    CHECK(strstr(txt, "vsh, ms0:/SEPLUGINS/xmbih.prx, on\n") != NULL);
    CHECK(strncmp(txt, "vsh, ms0:/SEPLUGINS/category_lite.prx, on\n", 42) == 0);   /* position: first */
    CHECK(strstr(txt, "game, ms0:/SEPLUGINS/TempAR/tempar.prx, on\n") != NULL);
    CHECK(strstr(txt, "pops, ms0:/SEPLUGINS/TempAR/tempar_lite.prx, on\n") != NULL);
    CHECK(strstr(txt, "pops, ms0:/SEPLUGINS/cdda_enabler.prx, on\n") != NULL);
    CHECK(strstr(txt, "vsh, ms0:/SEPLUGINS/zerovsh_patcher.prx, on\n") != NULL);
    CHECK(strstr(txt, "game, ms0:/SEPLUGINS/RemoteJoyLite.prx, on\n") != NULL);
    CHECK(strstr(txt, "pops, ms0:/SEPLUGINS/RemoteJoyLite.prx, on\n") != NULL);
    CHECK(strstr(txt, "game, ms0:/SEPLUGINS/atpro.prx, on\n") != NULL);
    CHECK(strstr(txt, "ULUS10041 ULES00182 ULES00151 ULUS10160 ULES00502 ULES00503 ULUS01826 ULUS11826 ULUX80146 ULET00417, "
                      "ms0:/PSP/PLUGINS/cheatdevice_remastered/cheatdevice_remastered.prx, on\n") != NULL);
    CHECK(strstr(txt, "_lite.prx, on\n") == NULL || strstr(txt, "cheatdevice_remastered_lite") == NULL);
    free(txt);

    CHECK(fs_exists("ms0:/SEPLUGINS/xmbih.prx"));
    CHECK(fs_exists("ms0:/SEPLUGINS/xmbih.ini"));
    CHECK(!fs_exists("ms0:/SEPLUGINS/ReadMe.txt"));
    CHECK(!fs_exists("ms0:/SEPLUGINS/category_lite.prx.tmp"));
    CHECK(fs_exists("ms0:/SEPLUGINS/category_lite.prx"));
    CHECK(fs_exists("ms0:/PSP/PLUGINS/cheatdevice_remastered/SCRIPTS/LCS/Snow.txt"));
    CHECK(!fs_exists("ms0:/PSP/PLUGINS/cheatdevice_remastered/cdr_compat_loader.prx"));
    CHECK(fs_exists("ms0:/SEPLUGINS/TempAR/cheat.bin"));
    CHECK(fs_exists("ms0:/SEPLUGINS/TempAR/languages/ja.bin"));
    CHECK(!fs_exists("ms0:/SEPLUGINS/TempAR/docs"));
    CHECK(fs_exists("ms0:/SEPLUGINS/cdda_enabler.prx"));
    CHECK(!fs_exists("ms0:/SEPLUGINS/readme.txt"));
    CHECK(fs_exists("ms0:/SEPLUGINS/zerovsh.ini"));
    CHECK(fs_is_dir("ms0:/PSP/VSH/"));
    CHECK(fs_exists("ms0:/SEPLUGINS/RemoteJoyLite.prx"));
    CHECK(!fs_exists("ms0:/SEPLUGINS/RemoteJoyLiteDebug.prx"));
    CHECK(fs_exists("ms0:/kd/pspnet_adhocctl.prx"));
    /* aemu ships "seplugins/" in lower case: the same folder on the PSP's FAT
       file system, a different one on the (case sensitive) test host */
    CHECK(fs_exists("ms0:/seplugins/hotspot.txt"));
    CHECK(!fs_exists("ms0:/server"));
    CHECK(fs_exists("ms0:/PSP/GAME/CMFileManager/EBOOT.PBP"));
    CHECK(!fs_exists("ms0:/PSP/GAME/CMFileManager/APP.PBP"));
    CHECK(fs_exists("ms0:/PSP/GAME/DaedalusX64/EBOOT.PBP"));
    CHECK(fs_is_dir("ms0:/PSP/GAME/DaedalusX64/Roms/"));
    CHECK(fs_exists("ms0:/PSP/GAME/psplink/EBOOT.PBP"));
    if (db_find(&db, "picodrive")) {
        CHECK(fs_exists("ms0:/PSP/GAME/PicoDrive/EBOOT.PBP"));
        CHECK(fs_exists("ms0:/PSP/GAME/PicoDrive/skin/background.png"));
        CHECK(!fs_exists("ms0:/PSP/GAME/bin_to_cso_mp3") && !fs_exists("ms0:/bin_to_cso_mp3"));
    }
    if (db_find(&db, "snes9xtyl")) {
        CHECK(fs_exists("ms0:/PSP/GAME/Snes9xTYL/EBOOT.PBP"));
        CHECK(fs_exists("ms0:/PSP/GAME/Snes9xTYL/mediaengine.prx"));   /* the PSP (Media Engine) build */
        CHECK(fs_exists("ms0:/PSP/GAME/Snes9xTYL/DATA/snesadvance.dat"));
    }
    if (db_find(&db, "nzportable")) {
        CHECK(fs_exists("ms0:/PSP/GAME/nzportable/EBOOT.PBP"));
        CHECK(fs_exists("ms0:/PSP/GAME/nzportable/nzp/config.cfg"));
    }
    if (db_find(&db, "crosscraft")) {
        CHECK(fs_exists("ms0:/PSP/GAME/CrossCraft/EBOOT.PBP"));
        CHECK(fs_exists("ms0:/PSP/GAME/CrossCraft/texturepacks/default.zip"));
        CHECK(!fs_exists("ms0:/PSP/GAME/CrossCraft/default.zip"));
        CHECK(!fs_exists("ms0:/PSP/GAME/CrossCraft/CrossCraft-Classic.prx"));
    }
    if (db_find(&db, "tuxracer")) {
        CHECK(fs_exists("ms0:/PSP/GAME/ExtremeTuxRacer/EBOOT.PBP"));
        CHECK(fs_exists("ms0:/PSP/GAME/ExtremeTuxRacer/config/options.txt"));
        CHECK(!fs_exists("ms0:/PSP/SYSTEM") && !fs_exists("ms0:/LICENSES"));
    }

    /* ---- ARK itself: the updater goes to PSP/GAME/UPDATE, and the app offers to start it ---- */
    if (install(&ctx, &st, &db, "ark", err) == 0) {
        CHECK_STR(ctx.run_path, "ms0:/PSP/GAME/UPDATE/EBOOT.PBP");
        CHECK_STR(ctx.run_title, "ARK Updater");
        CHECK(fs_exists("ms0:/PSP/GAME/UPDATE/EBOOT.PBP"));
    }
    CHECK(store_find(&st, "ark") && store_find(&st, "ark")->runs);
    CHECK(fs_dir_empty("ms0:/PSP/APPS/PluginManager/data/tmp/"));

    /* ---- the XMB index lists every package with plugins once ---- */
    CHECK_INT(db_write_xmb_index(&db, NULL, 0, "ms0:/PSP/APPS/PluginManager/data/xmbnames.txt"), 0);
    char *idx = fs_read_all("ms0:/PSP/APPS/PluginManager/data/xmbnames.txt", NULL, 0);
    CHECK(idx && strstr(idx, "ms0:/SEPLUGINS/RemoteJoyLite.prx\tRemoteJoyLite\n"));
    CHECK(idx && strstr(idx, "ms0:/SEPLUGINS/xmbih.prx\tXMB Item Hider\n"));
    const char *tempar = idx ? strstr(idx, "\tTempAR\n") : NULL;
    CHECK(tempar && !strstr(tempar + 1, "\tTempAR\n"));     /* two plugin files, one entry */
    free(idx);

    /* ---- update keeps user configuration and the disabled state ---- */
    fs_write_all("ms0:/SEPLUGINS/xmbih.ini", "USER EDIT", 9);
    ptxt_t pt;
    ptxt_load(&pt, "ms0:/SEPLUGINS/PLUGINS.TXT");
    ptxt_set_enabled(&pt, "ms0:/SEPLUGINS/xmbih.prx", 0);
    ptxt_save(&pt, "ms0:/SEPLUGINS/PLUGINS.TXT");
    ptxt_free(&pt);
    if (install(&ctx, &st, &db, "xmbih", err) == 0) {
        char *ini = fs_read_all("ms0:/SEPLUGINS/xmbih.ini", NULL, 0);
        CHECK_STR(ini, "USER EDIT");
        free(ini);
        txt = plugins_txt("ms0:/");
        CHECK(strstr(txt, "vsh, ms0:/SEPLUGINS/xmbih.prx, off\n") != NULL);
        free(txt);
        CHECK(db_list_has(db_find(&db, "xmbih")->files, db_find(&db, "xmbih")->n_files, "ms0:/SEPLUGINS/xmbih.ini"));
    }

    if (db_find(&db, "crosscraft")) {
        fs_write_all("ms0:/PSP/GAME/CrossCraft/config.cfg", "username:Me\n", 12);
        CHECK_INT(install(&ctx, &st, &db, "crosscraft", err), 0);
        char *cfg = fs_read_all("ms0:/PSP/GAME/CrossCraft/config.cfg", NULL, 0);
        CHECK_STR(cfg, "username:Me\n");
        free(cfg);
    }

    /* ---- a checksum mismatch aborts and leaves the old install intact ---- */
    {
        const store_entry *e = store_find(&st, "cddaenabler");
        cJSON *bad = cJSON_Duplicate(e->install, 1);
        cJSON *dl = cJSON_GetArrayItem(bad, 0);
        cJSON_ReplaceItemInObject(dl, "sha256", cJSON_CreateString("00"));
        store_entry copy = *e;
        copy.install = bad;
        CHECK_INT(installer_install(&ctx, &copy, &db, err, sizeof(err)), -1);
        CHECK(strstr(err, "Checksum") != NULL);
        CHECK(fs_exists("ms0:/SEPLUGINS/cdda_enabler.prx"));
        CHECK(db_find(&db, "cddaenabler") != NULL);
        txt = plugins_txt("ms0:/");
        CHECK(strstr(txt, "pops, ms0:/SEPLUGINS/cdda_enabler.prx, on\n") != NULL);
        free(txt);
        cJSON_Delete(bad);
    }

    /* ---- plain http downloads need a checksum ---- */
    {
        cJSON *http = cJSON_Parse("[{\"type\":\"download\",\"url\":\"http://x/cdda_enabler.rar\"}]");
        store_entry e;
        memset(&e, 0, sizeof(e));
        e.id = "http";
        e.title = "Http";
        e.version = "1";
        e.install = http;
        int before = downloads;
        CHECK_INT(installer_install(&ctx, &e, &db, err, sizeof(err)), -1);
        CHECK(strstr(err, "sha256") != NULL);
        CHECK_INT(downloads, before);
        CHECK(db_find(&db, "http") == NULL);
        cJSON_Delete(http);
    }

    /* ---- a malicious entry can't escape the allowed folders ---- */
    {
        cJSON *evil = cJSON_Parse(
            "[{\"type\":\"download\",\"url\":\"https://x/cdda_enabler.rar\",\"file\":\"e.rar\"},"
            " {\"type\":\"extract\",\"file\":\"e.rar\",\"output\":\"%ARK%\"}]");
        store_entry e;
        memset(&e, 0, sizeof(e));
        e.id = "evil";
        e.title = "Evil";
        e.version = "1";
        e.install = evil;
        CHECK_INT(installer_install(&ctx, &e, &db, err, sizeof(err)), -1);
        CHECK(db_find(&db, "evil") == NULL);
        CHECK(!fs_exists("ms0:/PSP/SAVEDATA/ARK_01234/cdda_enabler/cdda_enabler.prx"));
        cJSON_Delete(evil);
    }

    /* ---- uninstall everything ---- */
    for (int i = db.count - 1; i >= 0; i--) {
        char id[64];
        snprintf(id, sizeof(id), "%s", db.pkgs[i].id);
        CHECK_INT(installer_uninstall(&ctx, &db, id, err, sizeof(err)), 0);
    }
    CHECK_INT(db.count, 0);
    txt = plugins_txt("ms0:/");
    CHECK_STR(txt, user_line);
    free(txt);
    CHECK_INT(count_files("ms0:/"), 0);
    CHECK_INT(count_dirs("ms0:/"), 0);
    if (count_files("ms0:/") || count_dirs("ms0:/")) {
        snprintf(err, sizeof(err), "find '%s' ! -path '*/PluginManager/data*'", fs_native_path("ms0:/", native, sizeof(native)));
        if (system(err) != 0) fprintf(stderr, "find failed\n");
    }

    /* ---- model specific variants ---- */
    init_ctx(&ctx, "ms0:/", MODEL_1000);
    if (install(&ctx, &st, &db, "cheatdevice", err) == 0) {
        txt = plugins_txt("ms0:/");
        CHECK(strstr(txt, "cheatdevice_remastered_lite.prx, on\n") != NULL);
        CHECK(strstr(txt, "cheatdevice_remastered.prx, on\n") == NULL);
        free(txt);
        CHECK_INT(installer_uninstall(&ctx, &db, "cheatdevice", err, sizeof(err)), 0);
    }

    init_ctx(&ctx, "ms0:/", MODEL_VITA);
    if (install(&ctx, &st, &db, "snes9xtyl", err) == 0) {
        CHECK(fs_exists("ms0:/PSP/GAME/Snes9xTYL/EBOOT.PBP"));
        CHECK(!fs_exists("ms0:/PSP/GAME/Snes9xTYL/mediaengine.prx"));
        CHECK_INT(installer_uninstall(&ctx, &db, "snes9xtyl", err, sizeof(err)), 0);
        CHECK(!fs_exists("ms0:/PSP/GAME/Snes9xTYL"));
    }

    init_ctx(&ctx, "ef0:/", MODEL_GO);
    if (install(&ctx, &st, &db, "cmfilemanager", err) == 0) {
        CHECK(fs_exists("ef0:/PSP/GAME/CMFileManager/APP.PBP"));
        CHECK(!fs_exists("ms0:/PSP/GAME/CMFileManager/EBOOT.PBP"));
        CHECK_INT(installer_uninstall(&ctx, &db, "cmfilemanager", err, sizeof(err)), 0);
        CHECK(!fs_exists("ef0:/PSP/GAME/CMFileManager"));
    }
    if (install(&ctx, &st, &db, "gclite", err) == 0) {
        txt = plugins_txt("ef0:/");
        CHECK_STR(txt, "vsh, ef0:/SEPLUGINS/category_lite.prx, on\n");
        free(txt);
        CHECK_INT(installer_uninstall(&ctx, &db, "gclite", err, sizeof(err)), 0);
    }

    db_free(&db);
    store_free(&st);
    free(json);

    printf("test_install: %d installed, %d skipped, %d downloads, %d checks, %d failures\n",
           installed, skipped, downloads, test_checks, test_failures);
    return test_failures ? 1 : 0;
}
