/* Staged writes and a durable undo journal for package changes. */
#ifndef PM_TRANSACTION_H
#define PM_TRANSACTION_H

#include "installer.h"

typedef struct {
    char *path;
    int kind;                  /* replacement, deletion, or new directory */
    int existed;
    int64_t size;              /* planned replacement size, used for space checks */
} txn_entry;

typedef struct {
    install_ctx *ctx;
    char dir[256];
    txn_entry *entries;
    int count;
    int sequence;
    int done;
    int applying;
} transaction;

int txn_begin(transaction *tx, install_ctx *ctx, char *err, int errlen);
int txn_stage(transaction *tx, const char *path, char *out, int size);
int txn_stage_sized(transaction *tx, const char *path, int64_t bytes, char *out, int size);
int txn_delete(transaction *tx, const char *path);
int txn_mkdirs(transaction *tx, const char *path);
const char *txn_read_path(const transaction *tx, const char *path, char *out, int size);
int txn_commit(transaction *tx, char *err, int errlen);
int txn_rollback(transaction *tx, char *err, int errlen);
void txn_free(transaction *tx);
/* Recover before loading the package database or changing installed files. */
int txn_recover(install_ctx *ctx, char *err, int errlen);

#endif
