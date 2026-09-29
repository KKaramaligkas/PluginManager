/*
    Unit tests for the portable Plugin Manager core (run on the host).
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/db.h"
#include "../src/fs.h"
#include "../src/installer.h"
#include "../src/pluginstxt.h"
#include "../src/store.h"
#include "../src/tlsdiag.h"
#include "../src/util.h"
#include "test.h"

int test_failures = 0;
int test_checks = 0;

static void test_strings(void)
{
    CHECK(pm_glob_match("*.prx", "xmbih.prx"));
    CHECK(pm_glob_match("*.PRX", "xmbih.prx"));
    CHECK(!pm_glob_match("*.prx", "xmbih.ini"));
    CHECK(pm_glob_match("kd/*", "kd/memab.prx"));
    CHECK(pm_glob_match("seplugins/*.txt", "seplugins/hotspot.txt"));
    CHECK(pm_glob_match("a?c", "abc"));
    CHECK(!pm_glob_match("a?c", "ac"));
    CHECK(pm_glob_match("*", ""));

    CHECK(pm_version_compare("1.0h3", "1.0h2") > 0);
    CHECK(pm_version_compare("1.8", "1.83") < 0);
    CHECK(pm_version_compare("v1.8", "1.8") == 0);
    CHECK(pm_version_compare("1.10", "1.9") > 0);
    CHECK(pm_version_compare("2026-09-12", "2026-09-08") > 0);
    CHECK(pm_version_compare("1.0.0", "1.0") > 0);
    CHECK(pm_version_compare("3.2.1", "3.2.1") == 0);
    CHECK(pm_version_compare("1.0", "1.3fix") < 0);   /* letter suffixes are hotfixes */
    CHECK(pm_version_compare("1.0.1", "1.0beta") > 0);
    CHECK(pm_version_compare(NULL, "1") < 0);

    char out[256];
    CHECK_INT(pm_sanitize_relpath("SEPLUGINS/xmbih.prx", out, sizeof(out)), 0);
    CHECK_STR(out, "SEPLUGINS/xmbih.prx");
    CHECK_INT(pm_sanitize_relpath("\\a\\\\b\\.\\c.txt", out, sizeof(out)), 0);
    CHECK_STR(out, "a/b/c.txt");
    CHECK_INT(pm_sanitize_relpath("dir/", out, sizeof(out)), 0);
    CHECK_STR(out, "dir/");
    CHECK_INT(pm_sanitize_relpath("../evil.prx", out, sizeof(out)), -1);
    CHECK_INT(pm_sanitize_relpath("a/../../evil", out, sizeof(out)), -1);
    CHECK_INT(pm_sanitize_relpath("flash0:/kd/x.prx", out, sizeof(out)), -1);
    CHECK_INT(pm_sanitize_relpath("", out, sizeof(out)), -1);
    CHECK_INT(pm_sanitize_relpath("./", out, sizeof(out)), -1);

    CHECK(pm_valid_id("xmbih"));
    CHECK(pm_valid_id("cheat-device_2.0"));
    CHECK(!pm_valid_id("../x"));
    CHECK(!pm_valid_id(""));
    CHECK(!pm_valid_id("a b"));

    CHECK_STR(pm_basename("ms0:/SEPLUGINS/a.prx"), "a.prx");
    CHECK_STR(pm_basename("a.prx"), "a.prx");
    pm_dirname("ms0:/SEPLUGINS/a.prx", out, sizeof(out));
    CHECK_STR(out, "ms0:/SEPLUGINS/");

    pm_format_size(512, out, sizeof(out));
    CHECK_STR(out, "512 B");
    pm_format_size(54591, out, sizeof(out));
    CHECK_STR(out, "54 KB");
    pm_format_size(35895692, out, sizeof(out));
    CHECK_STR(out, "34.2 MB");

    char hex[65];
    pm_sha256_buf("abc", 3, hex);
    CHECK_STR(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
}

static void test_pluginstxt(void)
{
    const char *file = "ms0:/SEPLUGINS/PLUGINS.TXT";
    const char *initial =
        "# my plugins\r\n"
        "vsh, ms0:/SEPLUGINS/cxmb/cxmb.prx, on\r\n"
        "game, ms0:/seplugins/cwcheat/cwcheat.prx, off // cheats\r\n"
        "this line is weird\r\n"
        "\r\n";
    fs_mkdirs("ms0:/SEPLUGINS/", NULL, NULL);
    CHECK_INT(fs_write_all(file, initial, (int)strlen(initial)), 0);

    ptxt_t pt;
    CHECK_INT(ptxt_load(&pt, file), 0);
    CHECK_INT(pt.count, 4);
    CHECK(ptxt_find(&pt, "ms0:/SEPLUGINS/cxmb/cxmb.prx", NULL) == 1);
    CHECK(ptxt_find(&pt, "MS0:SEPLUGINS/CWCHEAT/CWCHEAT.PRX", "game") == 2);
    CHECK(ptxt_find(&pt, "ms0:/SEPLUGINS/cwcheat/cwcheat.prx", "vsh") == -1);
    CHECK(ptxt_is_enabled(&pt, "ms0:/SEPLUGINS/cxmb/cxmb.prx"));
    CHECK(!ptxt_is_enabled(&pt, "ms0:/SEPLUGINS/cwcheat/cwcheat.prx"));

    CHECK_INT(ptxt_add(&pt, "vsh", "ms0:/SEPLUGINS/xmbih.prx", 1, 0), 1);
    CHECK_INT(ptxt_add(&pt, "vsh", "ms0:/SEPLUGINS/xmbih.prx", 1, 0), 0);   /* no duplicate */
    CHECK_INT(ptxt_add(&pt, "vsh", "ms0:/SEPLUGINS/category_lite.prx", 1, 1), 1);
    CHECK_INT(ptxt_set_enabled(&pt, "ms0:/SEPLUGINS/cwcheat/cwcheat.prx", 1), 1);
    CHECK_INT(ptxt_save(&pt, file), 0);
    ptxt_free(&pt);

    char *text = fs_read_all(file, NULL, 0);
    CHECK_STR(text,
        "# my plugins\n"
        "vsh, ms0:/SEPLUGINS/category_lite.prx, on\n"
        "vsh, ms0:/SEPLUGINS/cxmb/cxmb.prx, on\n"
        "game, ms0:/seplugins/cwcheat/cwcheat.prx, on\n"
        "this line is weird\n"
        "vsh, ms0:/SEPLUGINS/xmbih.prx, on\n");
    free(text);

    CHECK_INT(ptxt_load(&pt, file), 0);
    CHECK_INT(ptxt_remove(&pt, "ms0:/SEPLUGINS/cxmb/cxmb.prx", NULL), 1);
    CHECK_INT(ptxt_remove(&pt, "ms0:/SEPLUGINS/nothere.prx", NULL), 0);
    CHECK_INT(ptxt_save(&pt, file), 0);
    ptxt_free(&pt);
    text = fs_read_all(file, NULL, 0);
    CHECK(text && !strstr(text, "cxmb"));
    CHECK(text && strstr(text, "# my plugins\n"));
    free(text);
}

static void test_store(void)
{
    char err[256];
    store_t st;

    CHECK_INT(store_parse(&st, "not json", NULL, err, sizeof(err)), -1);
    CHECK_INT(store_parse(&st, "{\"entries\": []}", NULL, err, sizeof(err)), -1);
    CHECK_INT(store_parse(&st, "{\"storeInfo\":{\"version\":99},\"entries\":[]}", NULL, err, sizeof(err)), -1);
    CHECK(strstr(err, "newer") != NULL);

    const char *json =
        "{\"storeInfo\":{\"title\":\"T\",\"revision\":3},"
        "\"entries\":["
        " {\"id\":\"ok\",\"title\":\"Good\",\"category\":\"plugin\",\"icon\":\"icons/ok.png\",\"size\":10,"
        "  \"install\":[{\"type\":\"message\",\"text\":\"hi\"}]},"
        " {\"id\":\"ok\",\"title\":\"Duplicate\",\"install\":[{\"type\":\"message\"}]},"
        " {\"id\":\"bad id\",\"title\":\"Bad\",\"install\":[{\"type\":\"message\"}]},"
        " {\"id\":\"nosteps\",\"title\":\"No steps\",\"install\":[]},"
        " {\"id\":\"badstep\",\"title\":\"Bad step\",\"install\":[{\"type\":\"format_flash0\"}]},"
        " {\"id\":\"abs\",\"title\":\"Abs\",\"category\":\"emulators\",\"icon\":\"https://x.org/a.png\","
        "  \"install\":[{\"type\":\"message\"}]},"
        " {\"id\":\"sys\",\"title\":\"System\",\"versionFile\":\"%ARK%VERSION.TXT\","
        "  \"install\":[{\"type\":\"message\"},{\"type\":\"run\",\"path\":\"%GAME%UPDATE/EBOOT.PBP\"}]}"
        "]}";
    CHECK_INT(store_parse(&st, json, "https://example.org/store/store.json?raw=1", err, sizeof(err)), 0);
    CHECK_INT(st.count, 3);
    CHECK_INT(st.revision, 3);
    CHECK_STR(st.entries[0].id, "ok");
    CHECK_STR(st.entries[0].title, "Good");
    CHECK_STR(st.entries[0].icon, "https://example.org/store/icons/ok.png");
    CHECK_STR(st.entries[0].version, "?");
    CHECK_INT(st.entries[0].size, 10);
    CHECK_INT(st.entries[0].category, CAT_PLUGIN);
    CHECK_STR(st.entries[1].icon, "https://x.org/a.png");
    CHECK_INT(st.entries[1].category, CAT_EMULATOR);
    CHECK(store_find(&st, "abs") == &st.entries[1]);
    CHECK(st.entries[0].version_file == NULL);
    CHECK_INT(st.entries[0].runs, 0);
    CHECK_STR(st.entries[2].version_file, "%ARK%VERSION.TXT");
    CHECK_INT(st.entries[2].runs, 1);
    CHECK(store_find(&st, "nosteps") == NULL);
    store_free(&st);

    /* the real seed store must parse completely */
    const char *seed = getenv("PM_SEED_STORE");
    if (seed) {
        FILE *f = fopen(seed, "rb");
        CHECK(f != NULL);
        if (f) {
            fseek(f, 0, SEEK_END);
            long n = ftell(f);
            fseek(f, 0, SEEK_SET);
            char *buf = calloc(1, n + 1);
            CHECK((long)fread(buf, 1, n, f) == n);
            fclose(f);
            CHECK_INT(store_parse(&st, buf, NULL, err, sizeof(err)), 0);
            CHECK_INT(st.count, 19);
            for (int i = 0; i < st.count; i++) {
                CHECK(st.entries[i].icon != NULL);
                CHECK(st.entries[i].icon && pm_starts_with(st.entries[i].icon,
                    "https://raw.githubusercontent.com/kkaramaligkas/fasterark_powerup/main/PluginManager/store/icons/"));
            }
            store_free(&st);
            free(buf);
        }
    }
}

static void test_paths(void)
{
    install_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strcpy(ctx.root, "ms0:/");
    strcpy(ctx.ark_path, "ms0:/PSP/SAVEDATA/ARK_01234/");
    strcpy(ctx.temp_dir, "ms0:/PSP/APPS/PluginManager/data/tmp/");
    strcpy(ctx.protect_dir, "ms0:/PSP/APPS/PluginManager/data/");
    ctx.model = MODEL_3000;

    char out[256];
    CHECK_INT(installer_resolve_path(&ctx, "%SEPLUGINS%xmbih.prx", out, sizeof(out), 1), 0);
    CHECK_STR(out, "ms0:/SEPLUGINS/xmbih.prx");
    CHECK_INT(installer_resolve_path(&ctx, "%GAME%DaedalusX64/", out, sizeof(out), 1), 0);
    CHECK_STR(out, "ms0:/PSP/GAME/DaedalusX64/");
    CHECK_INT(installer_resolve_path(&ctx, "%PSPPLUGINS%cdr/x.prx", out, sizeof(out), 1), 0);
    CHECK_STR(out, "ms0:/PSP/PLUGINS/cdr/x.prx");
    CHECK_INT(installer_resolve_path(&ctx, "%ROOT%kd/memab.prx", out, sizeof(out), 1), 0);
    CHECK_INT(installer_resolve_path(&ctx, "ef0:/SEPLUGINS/a.prx", out, sizeof(out), 1), 0);
    CHECK_INT(installer_resolve_path(&ctx, "%APPS%PluginManager/EBOOT.PBP", out, sizeof(out), 1), 0);
    CHECK_INT(installer_resolve_path(&ctx, "%ARK%THEME.ARK", out, sizeof(out), 1), 0);

    /* refused */
    CHECK_INT(installer_resolve_path(&ctx, "flash0:/kd/systemctrl.prx", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%ARK%FLASH0.ARK", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%ARK%SETTINGS.TXT", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%SEPLUGINS%../PSP/SAVEDATA/x", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%ROOT%PSP/SAVEDATA/ULUS10041/DATA.BIN", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%ROOT%", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%NOPE%x", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%APPS%PluginManager/data/installed.json", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%APPS%PluginManager/data", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "%GAME150X%a", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "ms0:/SEPLUGINSX/a.prx", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "ms0:/PSP/GAME150X/a", out, sizeof(out), 1), -1);
    CHECK_INT(installer_resolve_path(&ctx, "SEPLUGINS/a.prx", out, sizeof(out), 1), -1);

    /* already expanded paths (archives, database): '%' is just a character */
    CHECK_INT(installer_check_path(&ctx, "ms0:/PSP/GAME/nzp/models/zbc%.mdl", out, sizeof(out)), 0);
    CHECK_STR(out, "ms0:/PSP/GAME/nzp/models/zbc%.mdl");
    CHECK_INT(installer_resolve_path(&ctx, "ms0:/PSP/GAME/nzp/models/zbc%.mdl", out, sizeof(out), 1), -1);
    CHECK_INT(installer_check_path(&ctx, "ef0:/SEPLUGINS/%ARK%x.prx", out, sizeof(out)), 0);
    CHECK_STR(out, "ef0:/SEPLUGINS/%ARK%x.prx");
    CHECK_INT(installer_check_path(&ctx, "ms0:/PSP/SAVEDATA/ARK_01234/FLASH0.ARK", out, sizeof(out)), -1);
    CHECK_INT(installer_check_path(&ctx, "ms0:/SEPLUGINS/../PSP/SAVEDATA/x", out, sizeof(out)), -1);
    CHECK_INT(installer_check_path(&ctx, "ms0:/PSP/APPS/PluginManager/data/installed.json", out, sizeof(out)), -1);
    CHECK_INT(installer_check_path(&ctx, "flash0:/kd/x.prx", out, sizeof(out)), -1);
    CHECK_INT(installer_check_path(&ctx, "%GAME%x", out, sizeof(out)), -1);

    /* conditions */
    cJSON *step = cJSON_Parse("{\"if\":{\"model\":\"1000\"}}");
    CHECK(!installer_condition_ok(&ctx, step));
    ctx.model = MODEL_1000;
    CHECK(installer_condition_ok(&ctx, step));
    cJSON_Delete(step);
    step = cJSON_Parse("{\"if\":{\"notModel\":[\"go\",\"vita\"]}}");
    CHECK(installer_condition_ok(&ctx, step));
    ctx.model = MODEL_GO;
    CHECK(!installer_condition_ok(&ctx, step));
    cJSON_Delete(step);
    step = cJSON_Parse("{\"if\":{\"platform\":\"vita\"}}");
    CHECK(!installer_condition_ok(&ctx, step));
    cJSON_Delete(step);
    step = cJSON_Parse("{\"if\":{\"future\":1}}");
    CHECK(!installer_condition_ok(&ctx, step));
    cJSON_Delete(step);
    step = cJSON_Parse("{}");
    CHECK(installer_condition_ok(&ctx, step));
    cJSON_Delete(step);
}

static void put_text(const char *path, const char *text)
{
    fs_write_all(path, text, (int)strlen(text));
}

static int run_only(install_ctx *ctx, db_t *db, const char *path, char *err)
{
    char json[512];
    snprintf(json, sizeof(json),
             "{\"entries\":[{\"id\":\"r\",\"title\":\"R\",\"install\":[{\"type\":\"run\",\"path\":\"%s\"}]}]}", path);
    store_t st;
    int r = store_parse(&st, json, NULL, err, 256);
    if (r == 0) r = installer_install(ctx, &st.entries[0], db, err, 256);
    store_free(&st);
    return r;
}

static void test_run_and_version(void)
{
    install_ctx ctx;
    memset(&ctx, 0, sizeof(ctx));
    strcpy(ctx.root, "ms0:/");
    strcpy(ctx.ark_path, "ms0:/PSP/SAVEDATA/ARK_01234/");
    strcpy(ctx.temp_dir, "ms0:/PSP/APPS/PluginManager/data/tmp/");
    strcpy(ctx.protect_dir, "ms0:/PSP/APPS/PluginManager/data/");
    ctx.model = MODEL_3000;
    db_t db;
    memset(&db, 0, sizeof(db));
    char err[256];

    /* "run" only records the program; the app offers to start it */
    fs_mkdirs("ms0:/PSP/GAME/UPDATE/", NULL, NULL);
    fs_write_all("ms0:/PSP/GAME/UPDATE/EBOOT.PBP", "PBP", 3);
    CHECK_INT(run_only(&ctx, &db, "%GAME%UPDATE/EBOOT.PBP", err), 0);
    CHECK_STR(ctx.run_path, "ms0:/PSP/GAME/UPDATE/EBOOT.PBP");
    fs_mkdirs("ms0:/PSP/APPS/Tool/", NULL, NULL);
    fs_write_all("ms0:/PSP/APPS/Tool/EBOOT.PBP", "PBP", 3);
    CHECK_INT(run_only(&ctx, &db, "%APPS%Tool/EBOOT.PBP", err), 0);
    CHECK_STR(ctx.run_path, "ms0:/PSP/APPS/Tool/EBOOT.PBP");

    /* refused: not an EBOOT.PBP, outside PSP/GAME and PSP/APPS, missing */
    fs_mkdirs("ms0:/SEPLUGINS/x/", NULL, NULL);
    fs_write_all("ms0:/SEPLUGINS/x/EBOOT.PBP", "PBP", 3);
    CHECK_INT(run_only(&ctx, &db, "%SEPLUGINS%x/EBOOT.PBP", err), -1);
    CHECK_STR(ctx.run_path, "");
    fs_write_all("ms0:/PSP/GAME/UPDATE/PARAM.SFO", "SFO", 3);
    CHECK_INT(run_only(&ctx, &db, "%GAME%UPDATE/PARAM.SFO", err), -1);
    CHECK_INT(run_only(&ctx, &db, "%GAME%NOPE/EBOOT.PBP", err), -1);
    CHECK(strstr(err, "missing") != NULL);
    CHECK_INT(run_only(&ctx, &db, "flash0:/vsh/module/EBOOT.PBP", err), -1);
    CHECK_INT(run_only(&ctx, &db, "%ARK%EBOOT.PBP", err), -1);
    CHECK_STR(ctx.run_path, "");
    db_free(&db);

    /* version files: the first line, if it looks like a version */
    char v[32];
    fs_mkdirs("ms0:/PSP/SAVEDATA/ARK_01234/", NULL, NULL);
    put_text("ms0:/PSP/SAVEDATA/ARK_01234/VERSION.TXT", "5.1.6\r\n");
    CHECK_INT(installer_read_version_file(&ctx, "%ARK%VERSION.TXT", v, sizeof(v)), 0);
    CHECK_STR(v, "5.1.6");
    put_text("ms0:/PSP/SAVEDATA/ARK_01234/VERSION.TXT", " 2026-09-12\nsecond line\n");
    CHECK_INT(installer_read_version_file(&ctx, "%ARK%VERSION.TXT", v, sizeof(v)), 0);
    CHECK_STR(v, "2026-09-12");
    put_text("ms0:/PSP/SAVEDATA/ARK_01234/VERSION.TXT", "not a version");
    CHECK_INT(installer_read_version_file(&ctx, "%ARK%VERSION.TXT", v, sizeof(v)), -1);
    CHECK_STR(v, "");
    put_text("ms0:/PSP/SAVEDATA/ARK_01234/VERSION.TXT", "");
    CHECK_INT(installer_read_version_file(&ctx, "%ARK%VERSION.TXT", v, sizeof(v)), -1);
    put_text("ms0:/PSP/SAVEDATA/ARK_01234/VERSION.TXT", "1.2.3.4.5.6.7.8.9.10.11.12.13.14.15");
    CHECK_INT(installer_read_version_file(&ctx, "%ARK%VERSION.TXT", v, sizeof(v)), -1);
    CHECK_INT(installer_read_version_file(&ctx, "%ARK%MISSING.TXT", v, sizeof(v)), -1);
    CHECK_INT(installer_read_version_file(&ctx, "flash0:/vsh/etc/version.txt", v, sizeof(v)), -1);
    CHECK_INT(installer_read_version_file(&ctx, "%NOPE%x", v, sizeof(v)), -1);
}

static void test_db(void)
{
    db_t db;
    CHECK_INT(db_load(&db, "ms0:/nothing.json"), 0);
    CHECK_INT(db.count, 0);

    db_package *p = calloc(1, sizeof(db_package));
    p->id = pm_strdup("x");
    p->title = pm_strdup("Plugin X");
    p->version = pm_strdup("1.0");
    p->root = pm_strdup("ms0:/");
    db_list_add(&p->files, &p->n_files, "ms0:/SEPLUGINS/x.prx");
    db_list_add(&p->files, &p->n_files, "ms0:/SEPLUGINS/x.prx");
    p->plugins = calloc(2, sizeof(db_plugin));
    p->plugins[0].runlevel = pm_strdup("vsh");
    p->plugins[0].path = pm_strdup("ms0:/SEPLUGINS/x.prx");
    p->plugins[1].runlevel = pm_strdup("game");
    p->plugins[1].path = pm_strdup("ms0:/SEPLUGINS/x.prx");
    p->n_plugins = 2;
    db_put(&db, p);
    CHECK_INT(db.count, 1);
    CHECK_INT(db.pkgs[0].n_files, 1);

    CHECK_INT(db_save(&db, "ms0:/PSP/APPS/PluginManager/data/installed.json"), 0);
    const char *local[] = { "ef0:/SEPLUGINS/my.plugin.prx", "ms0:/SEPLUGINS/noext" };
    CHECK_INT(db_write_xmb_index(&db, local, 2, "ms0:/PSP/APPS/PluginManager/data/xmbnames.txt"), 0);
    db_free(&db);

    /* one line per package (its first plugin), then local plugins named after the file */
    char *idx = fs_read_all("ms0:/PSP/APPS/PluginManager/data/xmbnames.txt", NULL, 0);
    CHECK_STR(idx, "ms0:/SEPLUGINS/x.prx\tPlugin X\n"
                   "ef0:/SEPLUGINS/my.plugin.prx\tmy.plugin\n"
                   "ms0:/SEPLUGINS/noext\tnoext\n");
    free(idx);

    CHECK_INT(db_load(&db, "ms0:/PSP/APPS/PluginManager/data/installed.json"), 0);
    CHECK_INT(db.count, 1);
    CHECK_STR(db.pkgs[0].title, "Plugin X");
    CHECK_INT(db.pkgs[0].n_plugins, 2);
    CHECK_STR(db.pkgs[0].plugins[1].runlevel, "game");
    db_remove(&db, "x");
    CHECK_INT(db.count, 0);
    db_free(&db);
}

static tlsdiag_time tt(int y, int mo, int d, int h, int mi, int s)
{
    tlsdiag_time t = { y, mo, d, h, mi, s };
    return t;
}

static void set_cert(tlsdiag *d, int depth, uint32_t flags, const char *subject, const char *issuer,
                     tlsdiag_time from, tlsdiag_time to)
{
    tlsdiag_cert *c = &d->certs[depth];
    c->flags = flags;
    pm_strlcpy(c->subject, subject, sizeof(c->subject));
    pm_strlcpy(c->issuer, issuer, sizeof(c->issuer));
    c->valid_from = from;
    c->valid_to = to;
    if (d->count < depth + 1) d->count = depth + 1;
    d->flags |= flags;
}

static void test_tlsdiag(void)
{
    tlsdiag d;
    char buf[600], small[40], name[16];

    /* server names */
    memset(&d, 0, sizeof(d));
    tlsdiag_set_host(&d, "https://raw.githubusercontent.com/kkaramaligkas/x/main/store.json");
    CHECK_STR(d.host, "raw.githubusercontent.com");
    tlsdiag_set_host(&d, "https://user:pw@example.org:8443/path?q=1");
    CHECK_STR(d.host, "example.org");
    tlsdiag_set_host(&d, "https://[::1]:8443/");
    CHECK_STR(d.host, "[::1]");
    tlsdiag_set_host(&d, "example.org/path");
    CHECK_STR(d.host, "example.org");
    tlsdiag_set_host(&d, NULL);
    CHECK_STR(d.host, "");
    char longurl[400] = "https://";
    memset(longurl + 8, 'a', 300);
    longurl[308] = 0;
    tlsdiag_set_host(&d, longurl);
    CHECK_INT(strlen(d.host), sizeof(d.host) - 1);

    /* names in certificates */
    tlsdiag_common_name("C=US, O=Let's Encrypt, CN=YR1", buf, sizeof(buf));
    CHECK_STR(buf, "YR1");
    tlsdiag_common_name("C=GB, O=Sectigo Limited", buf, sizeof(buf));
    CHECK_STR(buf, "Sectigo Limited");
    tlsdiag_common_name("OU=Unit", buf, sizeof(buf));
    CHECK_STR(buf, "OU=Unit");
    tlsdiag_common_name("CN=*.github.io", buf, sizeof(buf));
    CHECK_STR(buf, "*.github.io");
    tlsdiag_common_name("C=GB, O=Sectigo Limited", name, 5);
    CHECK_STR(name, "Sect");

    /* the PSP's clock */
    tlsdiag_format_time((time_t)1790618391, 1, buf, sizeof(buf));
    CHECK_STR(buf, "28 Sep 2026, 17:59");
    tlsdiag_format_time((time_t)1790618391, 0, buf, sizeof(buf));
    CHECK_STR(buf, "28 Sep 2026");
    tlsdiag_format_time((time_t)-1, 1, buf, sizeof(buf));
    CHECK_STR(buf, "unreadable");

    /* clock behind: the server's certificate isn't valid yet */
    memset(&d, 0, sizeof(d));
    tlsdiag_set_host(&d, "https://raw.githubusercontent.com/x");
    set_cert(&d, 2, 0, "C=US, O=ISRG, CN=Root YR", "C=US, O=Internet Security Research Group, CN=ISRG Root X1",
             tt(2026, 5, 13, 0, 0, 0), tt(2032, 9, 2, 23, 59, 59));
    set_cert(&d, 1, TLSDIAG_FUTURE, "C=US, O=Let's Encrypt, CN=YR1", "C=US, O=ISRG, CN=Root YR",
             tt(2025, 9, 3, 0, 0, 0), tt(2028, 9, 2, 23, 59, 59));
    set_cert(&d, 0, TLSDIAG_FUTURE, "CN=*.github.io", "C=US, O=Let's Encrypt, CN=YR1",
             tt(2026, 8, 2, 23, 38, 2), tt(2026, 10, 31, 23, 38, 1));
    tlsdiag_message(&d, "28 Sep 2016, 20:59", buf, sizeof(buf));
    CHECK_STR(buf, "The certificate of raw.githubusercontent.com is only valid from 2 Aug 2026, but your PSP's "
                   "clock says 28 Sep 2016, 20:59. Set the date and time in Settings > Date & Time Settings.");
    tlsdiag_report(&d, buf, sizeof(buf));
    CHECK(strstr(buf, "Server: raw.githubusercontent.com\nProblems: 00000200 (not valid yet)\n") == buf);
    CHECK(strstr(buf, "\nCertificate 0 (the server's): problems 00000200 (not valid yet)\n"
                      "  Subject: CN=*.github.io\n  Issuer:  C=US, O=Let's Encrypt, CN=YR1\n"
                      "  Valid:   2026-08-02 23:38:02 to 2026-10-31 23:38:01 UTC\n") != NULL);
    CHECK(strstr(buf, "\nCertificate 2: problems 00000000\n") != NULL);

    /* a time problem wins over the others */
    d.certs[2].flags |= TLSDIAG_NOT_TRUSTED;
    d.flags |= TLSDIAG_NOT_TRUSTED;
    tlsdiag_message(&d, "28 Sep 2016, 20:59", buf, sizeof(buf));
    CHECK(strstr(buf, "is only valid from 2 Aug 2026") != NULL);
    tlsdiag_report(&d, buf, sizeof(buf));
    CHECK(strstr(buf, "Problems: 00000208 (not valid yet, not trusted)\n") != NULL);

    /* clock ahead */
    memset(&d, 0, sizeof(d));
    tlsdiag_set_host(&d, "https://github.com/a/b");
    set_cert(&d, 0, TLSDIAG_EXPIRED, "CN=github.com", "C=GB, O=Sectigo Limited, CN=Sectigo Public Server Authentication CA DV E36",
             tt(2026, 9, 1, 0, 0, 0), tt(2026, 11, 29, 23, 59, 59));
    tlsdiag_message(&d, "5 Jan 2031, 10:00", buf, sizeof(buf));
    CHECK_STR(buf, "The certificate of github.com expired on 29 Nov 2026, and your PSP's clock says 5 Jan 2031, 10:00. "
                   "If that isn't today, set the date in Settings > Date & Time Settings.");

    /* a login page answering for the server */
    memset(&d, 0, sizeof(d));
    tlsdiag_set_host(&d, "https://github.com/a/b");
    set_cert(&d, 0, TLSDIAG_NAME | TLSDIAG_NOT_TRUSTED, "CN=login.hotspot.example", "CN=login.hotspot.example",
             tt(2024, 1, 1, 0, 0, 0), tt(2034, 1, 1, 0, 0, 0));
    tlsdiag_message(&d, "28 Sep 2026, 20:59", buf, sizeof(buf));
    CHECK_STR(buf, "github.com answered with a certificate for \"login.hotspot.example\". The network may be "
                   "redirecting secure connections (a hotspot's login page or a filter). PSP clock: 28 Sep 2026, 20:59.");

    /* an authority that isn't trusted: the top of the chain names it */
    memset(&d, 0, sizeof(d));
    tlsdiag_set_host(&d, "https://github.com/a/b");
    set_cert(&d, 1, TLSDIAG_NOT_TRUSTED, "C=US, O=Filter, CN=Filter Intermediate", "C=US, O=Filter Inc, CN=Filter Root CA",
             tt(2024, 1, 1, 0, 0, 0), tt(2034, 1, 1, 0, 0, 0));
    set_cert(&d, 0, 0, "CN=github.com", "C=US, O=Filter, CN=Filter Intermediate", tt(2026, 9, 1, 0, 0, 0),
             tt(2026, 11, 29, 0, 0, 0));
    tlsdiag_message(&d, "28 Sep 2026, 20:59", buf, sizeof(buf));
    CHECK_STR(buf, "The certificate of github.com, issued by \"Filter Root CA\", isn't from a trusted authority. "
                   "The network may be intercepting secure connections (a hotspot's login page or a filter). "
                   "PSP clock: 28 Sep 2026, 20:59.");

    /* nothing recorded (a problem found outside the chain check) */
    memset(&d, 0, sizeof(d));
    d.flags = 0x4000;
    tlsdiag_message(&d, "unreadable", buf, sizeof(buf));
    CHECK_STR(buf, "The certificate of the server was rejected (problems 00004000, details in data/tls_error.txt). "
                   "PSP clock: unreadable.");
    CHECK(tlsdiag_report(&d, buf, sizeof(buf)) == (int)strlen(buf));
    CHECK_STR(buf, "Server: ?\nProblems: 00004000\n");

    /* short buffers are cut, never overrun */
    memset(&d, 0, sizeof(d));
    tlsdiag_set_host(&d, "https://github.com/a/b");
    set_cert(&d, 0, TLSDIAG_EXPIRED, "CN=github.com", "CN=x", tt(2020, 1, 1, 0, 0, 0), tt(2021, 1, 1, 0, 0, 0));
    tlsdiag_message(&d, "28 Sep 2026, 20:59", small, sizeof(small));
    CHECK_INT(strlen(small), sizeof(small) - 1);
    CHECK(tlsdiag_report(&d, small, sizeof(small)) == (int)strlen(small));
    CHECK_INT(strlen(small), sizeof(small) - 1);
}

int main(void)
{
    if (!getenv("PM_FS_ROOT")) {
        fprintf(stderr, "PM_FS_ROOT must point to an empty scratch folder\n");
        return 2;
    }
    test_strings();
    test_pluginstxt();
    test_store();
    test_paths();
    test_run_and_version();
    test_db();
    test_tlsdiag();
    printf("test_core: %d checks, %d failures\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
