/* Failed updates, storage errors, cancellation, and recovery after a crash. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "../src/transaction.h"
#include "../src/fs.h"
#include "../src/pluginstxt.h"
#include "../src/util.h"
#include "test.h"

int test_failures, test_checks;
static const char *file = "ms0:/SEPLUGINS/test.prx";
static const char *config = "ms0:/SEPLUGINS/PLUGINS.TXT";
static const char *new_file = "ef0:/PSP/APPS/Test/EBOOT.PBP";

static void put(const char *path, const char *text)
{
    char dir[PM_PATH_MAX];
    pm_dirname(path, dir, sizeof(dir));
    CHECK_INT(fs_mkdirs(dir, NULL, NULL), 0);
    CHECK_INT(fs_write_all(path, text, (int)strlen(text)), 0);
}

static void expect(const char *path, const char *text)
{
    char *actual = fs_read_all(path, NULL, 10000);
    CHECK_STR(actual, text);
    free(actual);
}

static install_ctx context(void)
{
    install_ctx ctx = {0};
    strcpy(ctx.root, "ms0:/");
    strcpy(ctx.ark_path, "ms0:/PSP/SAVEDATA/ARK_01234/");
    strcpy(ctx.temp_dir, "ms0:/PSP/APPS/PluginManager/data/tmp/");
    strcpy(ctx.protect_dir, "ms0:/PSP/APPS/PluginManager/data/");
    strcpy(ctx.db_file, "ms0:/PSP/APPS/PluginManager/data/installed.json");
    ctx.model = MODEL_3000;
    return ctx;
}

static void stage(transaction *tx, const char *path, const char *text)
{
    char out[PM_PATH_MAX];
    CHECK_INT(txn_stage(tx, path, out, sizeof(out)), 0);
    put(out, text);
}

static void test_commit_and_abort(void)
{
    install_ctx ctx = context();
    transaction tx;
    char err[256] = "";
    put(file, "old"); put(config, "old config"); put(ctx.db_file, "old db");
    CHECK_INT(txn_begin(&tx, &ctx, err, sizeof(err)), 0);
    stage(&tx, file, "new"); stage(&tx, config, "new config"); stage(&tx, ctx.db_file, "new db");
    stage(&tx, new_file, "new app");
    expect(file, "old"); expect(ctx.db_file, "old db"); CHECK(!fs_exists(new_file));
    CHECK_INT(txn_rollback(&tx, err, sizeof(err)), 0);
    txn_free(&tx);
    expect(file, "old"); expect(config, "old config"); CHECK(!fs_exists(new_file));

    CHECK_INT(txn_begin(&tx, &ctx, err, sizeof(err)), 0);
    stage(&tx, file, "new"); stage(&tx, config, "new config"); stage(&tx, ctx.db_file, "new db");
    stage(&tx, new_file, "new app");
    CHECK_INT(txn_commit(&tx, err, sizeof(err)), 0);
    txn_free(&tx);
    expect(file, "new"); expect(config, "new config"); expect(ctx.db_file, "new db"); expect(new_file, "new app");
    CHECK_INT(txn_recover(&ctx, err, sizeof(err)), 0);
    fs_remove(new_file);
}

static int cancel_calls;
static int cancel_second(install_ctx *ctx) { (void)ctx; return ++cancel_calls == 2; }
static int crash_third(install_ctx *ctx) { (void)ctx; if (++cancel_calls == 3) _exit(42); return 0; }

static void test_commit_failures(void)
{
    /* Fail writing the journal, then either of two live replacement files. */
    for (int n = 0; n < 3; n++) {
        install_ctx ctx = context();
        transaction tx;
        char err[256] = "";
        put(file, "original"); put(config, "original config");
        CHECK_INT(txn_begin(&tx, &ctx, err, sizeof(err)), 0);
        stage(&tx, file, "replacement"); stage(&tx, config, "replacement config");
        fs_test_fail_after_writes(n);
        CHECK_INT(txn_commit(&tx, err, sizeof(err)), -1);
        CHECK_INT(txn_rollback(&tx, err, sizeof(err)), 0);
        txn_free(&tx);
        expect(file, "original"); expect(config, "original config");
        CHECK_INT(txn_recover(&ctx, err, sizeof(err)), 0);
    }
    install_ctx ctx = context();
    transaction tx;
    char err[256] = "";
    CHECK_INT(txn_begin(&tx, &ctx, err, sizeof(err)), 0);
    stage(&tx, file, "replacement"); stage(&tx, config, "replacement config");
    cancel_calls = 0; ctx.cancelled = cancel_second;
    CHECK_INT(txn_commit(&tx, err, sizeof(err)), -1);
    CHECK_INT(txn_rollback(&tx, err, sizeof(err)), 0);
    txn_free(&tx);
    expect(file, "original"); expect(config, "original config");
}

static void test_restart(void)
{
    install_ctx ctx = context();
    transaction tx;
    char err[256] = "";
    put(file, "original"); put(config, "original config"); put(ctx.db_file, "original db");
    CHECK_INT(txn_begin(&tx, &ctx, err, sizeof(err)), 0);
    stage(&tx, file, "replacement");
    stage(&tx, config, "replacement config");
    stage(&tx, ctx.db_file, "replacement db");
    cancel_calls = 0; ctx.cancelled = crash_third;
    pid_t pid = fork();
    CHECK(pid >= 0);
    if (pid == 0) { txn_commit(&tx, err, sizeof(err)); _exit(43); }
    int status = 0;
    CHECK_INT(waitpid(pid, &status, 0), pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 42);
    txn_free(&tx);
    expect(file, "replacement"); expect(config, "replacement config"); expect(ctx.db_file, "original db");
    ctx.cancelled = NULL;
    CHECK_INT(txn_recover(&ctx, err, sizeof(err)), 0);
    expect(file, "original"); expect(config, "original config"); expect(ctx.db_file, "original db");
    CHECK_INT(txn_recover(&ctx, err, sizeof(err)), 0);
}

static int download(install_ctx *ctx, const char *url, const char *dest, char *err, int errlen)
{
    (void)ctx; (void)url; (void)err; (void)errlen;
    return fs_write_all(dest, "downloaded", 10);
}

static int install(install_ctx *ctx, db_t *db, const char *version, int fail, char *err)
{
    char json[2000];
    snprintf(json, sizeof(json),
        "{\"entries\":[{\"id\":\"test\",\"title\":\"Test\",\"version\":\"%s\",\"install\":["
        "{\"type\":\"download\",\"url\":\"https://example.org/test\",\"file\":\"test\"},"
        "{\"type\":\"copy\",\"to\":\"%%SEPLUGINS%%test.prx\"},"
        "{\"type\":\"plugin\",\"path\":\"%%SEPLUGINS%%test.prx\",\"runlevel\":\"vsh\"}%s]}]}",
        version, fail ? ",{\"type\":\"run\",\"path\":\"%GAME%Missing/EBOOT.PBP\"}" : "");
    store_t st;
    int ret = store_parse(&st, json, NULL, err, 256);
    if (ret == 0) ret = installer_install(ctx, &st.entries[0], db, err, 256);
    store_free(&st);
    return ret;
}

static void test_installer(void)
{
    install_ctx ctx = context();
    ctx.download = download;
    db_t db = {0}, saved = {0};
    char err[256] = "";
    put(config, "# preserve this\n");
    CHECK_INT(install(&ctx, &db, "1.0", 0, err), 0);
    put(file, "original plugin");
    CHECK_INT(install(&ctx, &db, "2.0", 1, err), -1);
    expect(file, "original plugin");
    CHECK_STR(db_find(&db, "test")->version, "1.0");
    CHECK_INT(db_load(&saved, ctx.db_file), 0);
    CHECK_STR(db_find(&saved, "test")->version, "1.0");
    db_free(&saved);
    CHECK_INT(install(&ctx, &db, "2.0", 0, err), 0);
    expect(file, "downloaded"); CHECK_STR(db_find(&db, "test")->version, "2.0");
    CHECK_INT(installer_uninstall(&ctx, &db, "test", err, sizeof(err)), 0);
    CHECK(!fs_exists(file)); CHECK(db_find(&db, "test") == NULL);
    expect(config, "# preserve this\n");
    CHECK_INT(db_load(&saved, ctx.db_file), 0); CHECK_INT(saved.count, 0);
    db_free(&saved); db_free(&db);
}

static void test_protected_paths(void)
{
    install_ctx ctx = context();
    transaction tx;
    char err[256] = "", out[PM_PATH_MAX];
    CHECK_INT(txn_begin(&tx, &ctx, err, sizeof(err)), 0);
    CHECK_INT(txn_stage(&tx, "flash0:/kd/evil.prx", out, sizeof(out)), -1);
    CHECK_INT(txn_stage(&tx, "ms0:/PSP/APPS/PluginManager/data/settings.json", out, sizeof(out)), -1);
    CHECK_INT(txn_rollback(&tx, err, sizeof(err)), 0);
    txn_free(&tx);
}

int main(void)
{
    if (!getenv("PM_FS_ROOT")) return 2;
    test_commit_and_abort(); test_commit_failures(); test_restart(); test_installer(); test_protected_paths();
    printf("test_transaction: %d checks, %d failures\n", test_checks, test_failures);
    return test_failures ? 1 : 0;
}
