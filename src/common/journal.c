#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "common.h"
#include "db.h"
#include "store.h"
#include "journal.h"

#define J_PENDING 1
#define J_DONE    2

static int jfd = -1;
static int get_jfd(void) {
    if (jfd < 0) jfd = db_open(JOURNAL_FILE);
    return jfd;
}

/* FNV-1a over the entry, ignoring id and status (they change after the checksum is made). */
static unsigned int checksum_of(const JEntry *e) {
    JEntry t;
    memcpy(&t, e, sizeof t);
    t.id = 0; t.status = 0; t.checksum = 0;
    const unsigned char *p = (const unsigned char *)&t;
    unsigned int h = 2166136261u;
    for (size_t i = 0; i < sizeof t; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

void crash_point(void) {
    static int n = 0, target = -1;
    if (target < 0) { const char *s = getenv("BANK_CRASH_AT"); target = s ? atoi(s) : 0; }
    n++;
    if (target > 0 && n == target) _exit(137);      /* behaves like kill -9 */
}

int journal_begin(int n_acc, const int *acc_id, const long long *acc_balance,
                  int n_txn, const Transaction *txns, int loan_id) {
    if (n_acc < 0 || n_acc > 2 || n_txn < 0 || n_txn > 2) return -1;
    int fd = get_jfd();
    if (fd < 0) return -1;

    JEntry e;
    memset(&e, 0, sizeof e);
    e.status = J_PENDING;
    e.n_acc = n_acc;
    e.n_txn = n_txn;
    e.loan_id = loan_id;
    for (int i = 0; i < n_acc; i++) { e.acc_id[i] = acc_id[i]; e.acc_balance[i] = acc_balance[i]; }
    for (int i = 0; i < n_txn; i++) e.txn[i] = txns[i];
    e.checksum = checksum_of(&e);

    int id = db_append(fd, sizeof e, &e);
    if (id < 0) return -1;
    if (fsync(fd) < 0) return -1;
    return id;
}

int journal_commit(int id) {
    int fd = get_jfd();
    if (fd < 0 || id < 1) return -1;
    int st = J_DONE;
    off_t off = (off_t)(id - 1) * (off_t)sizeof(JEntry) + (off_t)offsetof(JEntry, status);
    if (pwrite(fd, &st, sizeof st, off) != (ssize_t)sizeof st) return -1;
    return fsync(fd);
}

static int txn_exists(int fd, const Transaction *t) {
    int n = db_count(fd, sizeof *t);
    for (int i = n; i >= 1; i--) {
        Transaction x;
        if (db_read(fd, i, sizeof x, &x) < 0) continue;
        if (x.account_id == t->account_id && x.type == t->type && x.amount == t->amount &&
            x.balance_after == t->balance_after && x.other_account_id == t->other_account_id &&
            x.timestamp == t->timestamp)
            return 1;
    }
    return 0;
}

int journal_recover(void) {
    int jf = db_open(JOURNAL_FILE);
    if (jf < 0) return -1;
    int tf = db_open(TRANSACTIONS_FILE);
    if (tf < 0) { close(jf); return -1; }
    int af = store_accounts_fd();

    int n = db_count(jf, sizeof(JEntry)), replayed = 0;
    for (int i = 1; i <= n; i++) {
        JEntry e;
        if (db_read(jf, i, sizeof e, &e) < 0) continue;
        if (e.status != J_PENDING) continue;
        if (e.n_acc < 0 || e.n_acc > 2 || e.n_txn < 0 || e.n_txn > 2) continue;
        if (e.checksum != checksum_of(&e)) continue;      /* torn write: nothing had been applied */

        for (int k = 0; k < e.n_acc; k++) {
            Account a;
            if (db_read(af, e.acc_id[k], sizeof a, &a) == 0) {
                a.balance = e.acc_balance[k];
                db_write(af, e.acc_id[k], sizeof a, &a);
            }
        }
        for (int k = 0; k < e.n_txn; k++) {
            if (!txn_exists(tf, &e.txn[k])) {
                Transaction t = e.txn[k];
                db_append(tf, sizeof t, &t);
            }
        }
        if (e.loan_id > 0) {
            int lf = db_open(LOANS_FILE);
            Loan ln;
            if (lf >= 0) {
                if (db_read(lf, e.loan_id, sizeof ln, &ln) == 0) {
                    ln.status = LOAN_APPROVED;
                    db_write(lf, e.loan_id, sizeof ln, &ln);
                }
                fsync(lf);
                close(lf);
            }
        }
        replayed++;
    }
    fsync(af);
    fsync(tf);
    if (ftruncate(jf, 0) == 0) fsync(jf);
    close(tf);
    close(jf);
    if (replayed) fprintf(stderr, "journal: replayed %d unfinished operation(s)\n", replayed);
    return replayed;
}
