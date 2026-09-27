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
        "  \"install\":[{\"type\":\"message\"}]}"
        "]}";
    CHECK_INT(store_parse(&st, json, "https://example.org/store/store.json?raw=1", err, sizeof(err)), 0);
    CHECK_INT(st.count, 2);
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
            CHECK_INT(st.count, 12);
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
    test_db();
    printf("test_core: %d checks, %d failures\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
