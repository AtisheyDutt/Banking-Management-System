#ifndef JOURNAL_H
#define JOURNAL_H

#include "models.h"

#define JOURNAL_FILE "data/journal.dat"

/* One journal record: the FINAL state an operation will write.
 * "id" must stay the first member (db_append sets it). */
typedef struct {
    int          id;
    int          status;          /* 1 = pending, 2 = done */
    unsigned int checksum;        /* detects a half-written entry */
    int          n_acc;
    int          acc_id[2];
    long long    acc_balance[2];  /* absolute new balances, so replay is idempotent */
    int          n_txn;
    int          loan_id;         /* loan to mark APPROVED in the same step, 0 = none */
    Transaction  txn[2];          /* passbook lines the operation will append */
} JEntry;

/* Write and fsync an entry. Returns the entry id, or -1. */
int  journal_begin(int n_acc, const int *acc_id, const long long *acc_balance,
                   int n_txn, const Transaction *txns, int loan_id);
/* Mark the entry done and fsync. */
int  journal_commit(int entry_id);
/* Call once at server start, before accepting clients. Returns number replayed. */
int  journal_recover(void);
/* Testing hook: exits abruptly at the Nth call if BANK_CRASH_AT=N is set. */
void crash_point(void);

#endif
