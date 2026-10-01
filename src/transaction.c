/*
 * All replacements are staged before commit. Each original is copied to an
 * undo file before its journal entry is persisted. Two alternating journals
 * keep the previous complete state readable if writing the next one fails.
 * A restart rolls back an unfinished commit, including PLUGINS.TXT and the
 * database. Backups stay intact until recovery has completed and synced.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <cjson/cJSON.h>

#include "transaction.h"
#include "fs.h"
#include "util.h"

enum { TX_FILE, TX_DELETE, TX_DIR };
#define TX_MAX_ENTRIES 8192
#define TX_JOURNAL_LIMIT (4 * 1024 * 1024)

static int tx_path(const transaction *tx, int i, const char *suffix, char *out, int size)
{
    return snprintf(out, size, "%s%d.%s", tx->dir, i, suffix) < size ? 0 : -1;
}

static int allowed(const transaction *tx, const char *path)
{
    char checked[PM_PATH_MAX];
    /* Only the caller's fixed database path is exempt from package protection. */
    if (tx->ctx->db_file[0] && !strcmp(path, tx->ctx->db_file)) return 1;
    return installer_check_path(tx->ctx, path, checked, sizeof(checked)) == 0 &&
           !strcmp(checked, path);
}

static int allowed_dir(const transaction *tx, const char *path)
{
    /* PSP is the common parent of allowed package folders. Only directory
       creation may use it; package files cannot be written there. */
    return allowed(tx, path) || !strcmp(path, "ms0:/PSP") || !strcmp(path, "ef0:/PSP");
}

static int persist(transaction *tx)
{
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON_AddNumberToObject(root, "sequence", tx->sequence + 1);
    cJSON_AddBoolToObject(root, "done", tx->done);
    cJSON_AddBoolToObject(root, "applying", tx->applying);
    cJSON *entries = cJSON_AddArrayToObject(root, "entries");
    if (!entries) { cJSON_Delete(root); return -1; }
    for (int i = 0; i < tx->count; i++) {
        cJSON *e = cJSON_CreateObject();
        if (!e || !cJSON_AddStringToObject(e, "path", tx->entries[i].path) ||
            !cJSON_AddNumberToObject(e, "kind", tx->entries[i].kind) ||
            !cJSON_AddBoolToObject(e, "existed", tx->entries[i].existed)) {
            cJSON_Delete(e); cJSON_Delete(root); return -1;
        }
        cJSON_AddItemToArray(entries, e);
    }
    char *json = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!json) return -1;
    char file[PM_PATH_MAX];
    int ret = tx_path(tx, (tx->sequence + 1) % 2, "json", file, sizeof(file));
    if (ret == 0) ret = fs_write_all(file, json, (int)strlen(json));
    if (ret == 0) ret = fs_sync(file);
    if (ret == 0) ret = fs_sync(tx->dir);
    free(json);
    if (ret == 0) tx->sequence++;
    else fs_remove(file); /* Never select a journal whose sync failed. */
    return ret;
}

static int find(const transaction *tx, const char *path)
{
    for (int i = 0; i < tx->count; i++)
        if (!pm_strcasecmp(tx->entries[i].path, path)) return i;
    return -1;
}

static int add(transaction *tx, const char *path, int kind)
{
    if (!(kind == TX_DIR ? allowed_dir(tx, path) : allowed(tx, path)) || tx->count >= TX_MAX_ENTRIES) return -1;
    int i = find(tx, path);
    if (i >= 0) {
        if (tx->entries[i].kind == TX_DIR) return kind == TX_DIR ? i : -1;
        tx->entries[i].kind = kind;
        return i;
    }
    int existed = fs_exists(path);
    if (kind != TX_DIR && existed && fs_is_dir(path)) return -1;
    txn_entry *entries = realloc(tx->entries, (tx->count + 1) * sizeof(*entries));
    if (!entries) return -1;
    tx->entries = entries;
    i = tx->count;
    entries[i].path = pm_strdup(path);
    if (!entries[i].path) return -1;
    entries[i].kind = kind;
    entries[i].existed = existed;
    entries[i].size = 0;
    if (existed && kind != TX_DIR) {
        char backup[PM_PATH_MAX];
        if (tx_path(tx, i, "undo", backup, sizeof(backup)) < 0 ||
            fs_copy(path, backup) < 0 || fs_sync(backup) < 0) {
            free(entries[i].path);
            return -1;
        }
    }
    tx->count++;
    return i;
}

int txn_mkdirs(transaction *tx, const char *path)
{
    char dir[PM_PATH_MAX];
    if (pm_strlcpy(dir, path, sizeof(dir)) >= sizeof(dir)) return -1;
    size_t n = strlen(dir);
    while (n > 5 && dir[n - 1] == '/') dir[--n] = 0;
    if (fs_is_dir(dir)) return 0;
    if (n <= 5 || !allowed_dir(tx, dir)) return -1;
    if (fs_exists(dir)) return -1;
    int i = find(tx, dir);
    if (i >= 0) return tx->entries[i].kind == TX_DIR ? 0 : -1;
    char parent[PM_PATH_MAX];
    pm_dirname(dir, parent, sizeof(parent));
    if (!parent[0] || txn_mkdirs(tx, parent) < 0) return -1;
    return add(tx, dir, TX_DIR) >= 0 ? 0 : -1;
}

int txn_stage_sized(transaction *tx, const char *path, int64_t bytes, char *out, int size)
{
    if (!allowed(tx, path) || bytes < 0) return -1;
    int previous = find(tx, path);
    int64_t old_size = fs_size(path);
    if (old_size < 0) old_size = 0;
    int64_t backup = previous < 0 ? old_size : 0;
    int64_t pending_temp = 0, pending_target = 0;
    for (int i = 0; i < tx->count; i++) {
        txn_entry *entry = &tx->entries[i];
        if (entry->kind != TX_FILE || i == previous) continue;
        int64_t original = fs_size(entry->path);
        if (original < 0) original = 0;
        int64_t growth = entry->size > original ? entry->size - original : 0;
        if (!strncmp(entry->path, tx->dir, 4)) {
            if (growth > INT64_MAX - pending_temp) return -1;
            pending_temp += growth;
        }
        if (!strncmp(entry->path, path, 4)) {
            if (growth > INT64_MAX - pending_target) return -1;
            pending_target += growth;
        }
    }
    int64_t growth = bytes > old_size ? bytes - old_size : 0;
    if (growth > INT64_MAX - pending_temp || growth > INT64_MAX - pending_target) return -1;
    if (!strncmp(path, tx->dir, 4)) pending_temp += growth;
    pending_target += growth;
    int64_t temp_free = fs_free_bytes(tx->dir), target_free = fs_free_bytes(path);
    const int64_t margin = 256 * 1024;
    if (bytes > INT64_MAX - backup - margin || pending_temp > INT64_MAX - bytes - backup - margin ||
        pending_target > INT64_MAX - margin ||
        (temp_free >= 0 && temp_free < bytes + backup + pending_temp + margin) ||
        (target_free >= 0 && target_free < pending_target + margin)) return -1;
    char parent[PM_PATH_MAX];
    pm_dirname(path, parent, sizeof(parent));
    if (txn_mkdirs(tx, parent) < 0) return -1;
    int i = add(tx, path, TX_FILE);
    if (i >= 0) tx->entries[i].size = bytes;
    return i >= 0 ? tx_path(tx, i, "stage", out, size) : -1;
}

int txn_stage(transaction *tx, const char *path, char *out, int size)
{
    int64_t bytes = fs_size(path);
    return txn_stage_sized(tx, path, bytes > 0 ? bytes : 0, out, size);
}

int txn_delete(transaction *tx, const char *path)
{
    return add(tx, path, TX_DELETE) >= 0 ? 0 : -1;
}

const char *txn_read_path(const transaction *tx, const char *path, char *out, int size)
{
    int i = find(tx, path);
    if (i < 0) return path;
    if (tx->entries[i].kind == TX_DELETE) return NULL;
    if (tx->entries[i].kind == TX_DIR) return path;
    return tx_path(tx, i, "stage", out, size) == 0 ? out : NULL;
}

typedef struct { transaction *tx; int failed; } cleanup_state;

static int clean_entry(void *ud, const char *name, int is_dir)
{
    cleanup_state *state = ud;
    transaction *tx = state->tx;
    char path[PM_PATH_MAX];
    if (is_dir || snprintf(path, sizeof(path), "%s%s", tx->dir, name) >= (int)sizeof(path)) {
        state->failed = 1; return 0;
    }
    /* Journals are removed last, after both have been marked done. */
    if (strcmp(name, "0.json") && strcmp(name, "1.json") && fs_remove(path) < 0) state->failed = 1;
    return 0;
}

static int finish(transaction *tx)
{
    tx->done = 1;
    /* Both slots must say done before deleting any undo file. */
    if (persist(tx) < 0 || persist(tx) < 0) return -1;
    cleanup_state state = {tx, 0};
    if (fs_list(tx->dir, clean_entry, &state) < 0 || state.failed) return -1;
    char path[PM_PATH_MAX];
    for (int i = 0; i < 2; i++) {
        tx_path(tx, i, "json", path, sizeof(path));
        fs_remove(path);
    }
    return fs_rmdir(tx->dir);
}

int txn_rollback(transaction *tx, char *err, int errlen)
{
    int failed = 0;
    for (int i = tx->applying ? tx->count - 1 : -1; i >= 0; i--) {
        txn_entry *e = &tx->entries[i];
        if (!(e->kind == TX_DIR ? allowed_dir(tx, e->path) : allowed(tx, e->path))) { failed = 1; continue; }
        if (e->kind == TX_DIR) {
            if (!e->existed && fs_is_dir(e->path) && fs_dir_empty(e->path) && fs_rmdir(e->path) < 0)
                failed = 1;
        } else if (e->existed) {
            char backup[PM_PATH_MAX];
            if (tx_path(tx, i, "undo", backup, sizeof(backup)) < 0 ||
                fs_copy(backup, e->path) < 0 || fs_sync(e->path) < 0) failed = 1;
        } else if (fs_exists(e->path) && fs_remove(e->path) < 0) failed = 1;
    }
    if (failed) {
        snprintf(err, errlen, "Recovery could not finish. Keep data/tmp/transaction and reconnect the storage, then restart.");
        return -1;
    }
    /* A failed cleanup is recoverable; the complete backups remain if the
       done marker could not be saved. Replaying rollback is safe. */
    finish(tx);
    return 0;
}

int txn_commit(transaction *tx, char *err, int errlen)
{
    /* Staging never touches installed files. Persist the complete undo plan
       once, before the first live write, instead of rewriting it per file. */
    tx->applying = 1;
    if (persist(tx) < 0) { tx->applying = 0; goto fail; }
    for (int i = 0; i < tx->count; i++) {
        txn_entry *e = &tx->entries[i];
        char staged[PM_PATH_MAX];
        if ((tx->ctx->cancelled && tx->ctx->cancelled(tx->ctx)) ||
            !(e->kind == TX_DIR ? allowed_dir(tx, e->path) : allowed(tx, e->path)))
            goto fail;
        if (e->kind == TX_DIR) {
            if (fs_mkdir(e->path) < 0) goto fail;
        } else if (e->kind == TX_DELETE) {
            if (fs_exists(e->path) && fs_remove(e->path) < 0) goto fail;
        } else {
            if (tx_path(tx, i, "stage", staged, sizeof(staged)) < 0 ||
                fs_copy(staged, e->path) < 0 || fs_sync(e->path) < 0) goto fail;
        }
        char parent[PM_PATH_MAX];
        pm_dirname(e->path, parent, sizeof(parent));
        if (fs_sync(parent) < 0) goto fail;
    }
    tx->done = 1;
    if (persist(tx) < 0) { tx->done = 0; goto fail; }
    finish(tx); /* A committed journal can be cleaned on the next launch. */
    return 0;
fail:
    snprintf(err, errlen, "Install interrupted or storage write failed; restoring the previous installation.");
    return -1;
}

void txn_free(transaction *tx)
{
    for (int i = 0; i < tx->count; i++) free(tx->entries[i].path);
    free(tx->entries);
    memset(tx, 0, sizeof(*tx));
}

static int setup(transaction *tx, install_ctx *ctx)
{
    memset(tx, 0, sizeof(*tx));
    tx->ctx = ctx;
    return ctx->temp_dir[0] && snprintf(tx->dir, sizeof(tx->dir), "%stransaction/", ctx->temp_dir) < (int)sizeof(tx->dir) ? 0 : -1;
}

int txn_recover(install_ctx *ctx, char *err, int errlen)
{
    transaction tx;
    if (setup(&tx, ctx) < 0) return -1;
    if (!fs_exists(tx.dir)) return 0;
    cJSON *best = NULL;
    for (int i = 0; i < 2; i++) {
        char path[PM_PATH_MAX];
        tx_path(&tx, i, "json", path, sizeof(path));
        char *text = fs_read_all(path, NULL, TX_JOURNAL_LIMIT);
        cJSON *root = text ? cJSON_Parse(text) : NULL;
        free(text);
        const cJSON *seq = cJSON_GetObjectItemCaseSensitive(root, "sequence");
        const cJSON *version = cJSON_GetObjectItemCaseSensitive(root, "version");
        if (cJSON_IsObject(root) && cJSON_IsNumber(seq) && seq->valueint > tx.sequence &&
            cJSON_IsNumber(version) && version->valueint == 1 &&
            cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(root, "done")) &&
            cJSON_IsBool(cJSON_GetObjectItemCaseSensitive(root, "applying")) &&
            cJSON_IsArray(cJSON_GetObjectItemCaseSensitive(root, "entries"))) {
            cJSON_Delete(best); best = root; tx.sequence = seq->valueint;
        } else cJSON_Delete(root);
    }
    if (!best) {
        /* An empty directory is from a begin that never changed anything. */
        if (fs_dir_empty(tx.dir)) { fs_rmdir(tx.dir); return 0; }
        snprintf(err, errlen, "The recovery journal is unreadable. Keep data/tmp/transaction for recovery.");
        return -1;
    }
    tx.done = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(best, "done"));
    tx.applying = cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(best, "applying"));
    const cJSON *arr = cJSON_GetObjectItemCaseSensitive(best, "entries");
    int n = cJSON_GetArraySize(arr), ret = -1;
    if (n < 0 || n > TX_MAX_ENTRIES) goto bad;
    tx.entries = calloc(n ? n : 1, sizeof(*tx.entries));
    if (!tx.entries) goto bad;
    for (int i = 0; i < n; i++) {
        const cJSON *e = cJSON_GetArrayItem(arr, i);
        const cJSON *path = cJSON_GetObjectItemCaseSensitive(e, "path");
        const cJSON *kind = cJSON_GetObjectItemCaseSensitive(e, "kind");
        const cJSON *existed = cJSON_GetObjectItemCaseSensitive(e, "existed");
        if (!cJSON_IsString(path) || !cJSON_IsNumber(kind) ||
            kind->valueint < TX_FILE || kind->valueint > TX_DIR ||
            !(kind->valueint == TX_DIR ? allowed_dir(&tx, path->valuestring) : allowed(&tx, path->valuestring)) ||
            !cJSON_IsBool(existed) || find(&tx, path->valuestring) >= 0) goto bad;
        txn_entry *entry = &tx.entries[tx.count];
        entry->path = pm_strdup(path->valuestring);
        if (!entry->path) goto bad;
        entry->kind = kind->valueint;
        entry->existed = cJSON_IsTrue(existed);
        tx.count++;
    }
    ret = tx.done ? finish(&tx) : txn_rollback(&tx, err, errlen);
    if (ret < 0 && !err[0]) snprintf(err, errlen, "Recovery cleanup failed. Reconnect the storage and restart.");
    goto out;
bad:
    snprintf(err, errlen, "The recovery journal is invalid. Keep data/tmp/transaction for recovery.");
out:
    cJSON_Delete(best);
    txn_free(&tx);
    return ret;
}

int txn_begin(transaction *tx, install_ctx *ctx, char *err, int errlen)
{
    if (setup(tx, ctx) < 0) { snprintf(err, errlen, "Invalid transaction folder"); return -1; }
    if (txn_recover(ctx, err, errlen) < 0) return -1;
    if (fs_mkdirs(tx->dir, NULL, NULL) < 0 || persist(tx) < 0) {
        snprintf(err, errlen, "Can't create the recovery journal (storage full?)");
        return -1;
    }
    return 0;
}
