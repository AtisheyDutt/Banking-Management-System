#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "db.h"
#include "store.h"
#include "journal.h"
#include "bank_ops.h"

/* The transaction log is opened on first use, once per process. */
static int txns_fd = -1;
static int get_txns_fd(void) {
    if (txns_fd < 0) txns_fd = db_open(TRANSACTIONS_FILE);
    return txns_fd;
}

/* Separate fd for the loans file. NEVER close it: closing any fd of a file drops this
 * process's fcntl locks on it, and loan_decide still holds the loan record lock. */
static int loans_fd2 = -1;
static int get_loans_fd2(void) {
    if (loans_fd2 < 0) loans_fd2 = db_open(LOANS_FILE);
    return loans_fd2;
}

static void make_txn(Transaction *t, int account_id, int type, long long amount,
                     long long balance_after, int other_account_id) {
    memset(t, 0, sizeof *t);
    t->account_id       = account_id;
    t->type             = type;
    t->amount           = amount;
    t->balance_after    = balance_after;
    t->other_account_id = other_account_id;
    t->timestamp        = time(NULL);
}

/* Crash-safe update. The caller holds the account lock(s) and has already put the
 * final balances in accs[]. Steps: journal (fsync) -> accounts (fsync) ->
 * passbook lines (fsync) -> mark journal entry done. If the process dies at any
 * point, journal_recover() at the next start finishes the job. */
static int durable_update(int n_acc, const int *acc_id, const Account *accs,
                          int n_txn, const Transaction *txns, int loan_id) {
    int af = store_accounts_fd();
    long long bals[2] = { 0, 0 };
    for (int i = 0; i < n_acc; i++) bals[i] = accs[i].balance;

    crash_point();                                          /* 1: nothing journaled */
    int jid = journal_begin(n_acc, acc_id, bals, n_txn, txns, loan_id);
    if (jid < 0) return -1;
    crash_point();                                          /* 2: journaled, nothing applied */

    for (int i = 0; i < n_acc; i++)
        if (db_write(af, acc_id[i], sizeof(Account), &accs[i]) < 0) return -1;
    fsync(af);
    crash_point();                                          /* 3: balances written */

    int tf = get_txns_fd();
    for (int i = 0; i < n_txn; i++) {
        Transaction t = txns[i];
        if (tf < 0 || db_append(tf, sizeof t, &t) < 0) return -1;
    }
    if (tf >= 0) fsync(tf);
    crash_point();                                          /* 4: passbook lines written */
    if (loan_id > 0) {
        int lf = get_loans_fd2();
        Loan ln;
        if (lf < 0 || db_read(lf, loan_id, sizeof ln, &ln) < 0) return -1;
        ln.status = LOAN_APPROVED;
        if (db_write(lf, loan_id, sizeof ln, &ln) < 0) return -1;
        fsync(lf);
    }

    journal_commit(jid);
    crash_point();                                          /* 5: fully committed */
    return 0;
}

int op_balance(int account_id, long long *balance) {
    Account a;
    if (account_get(account_id, &a) < 0) return OP_NO_ACCOUNT;   /* takes a read lock */
    if (balance) *balance = a.balance;
    return OP_OK;
}

/* Shared by deposit, withdraw and loan credit: lock, read, check, journal+write, unlock. */
static int apply(int account_id, int type, long long amount, long long *new_balance, int loan_id) {
    if (amount <= 0) return OP_INVALID_AMOUNT;
    if (account_id < 1) return OP_NO_ACCOUNT;

    int fd = store_accounts_fd();
    if (db_lock(fd, account_id, sizeof(Account), F_WRLCK) < 0) return OP_ERR;

    Account a;
    int rc;
    if (db_read(fd, account_id, sizeof a, &a) < 0)                         rc = OP_NO_ACCOUNT;
    else if (!a.active)                                                    rc = OP_INACTIVE;
    else if (type == TXN_WITHDRAW && a.balance < amount)                   rc = OP_INSUFFICIENT;
    else if ((type == TXN_DEPOSIT || type == TXN_LOAN_CREDIT) && amount > LLONG_MAX - a.balance)        rc = OP_INVALID_AMOUNT;
    else {
        a.balance += ((type == TXN_DEPOSIT || type == TXN_LOAN_CREDIT)) ? amount : -amount;
        Transaction t;
        make_txn(&t, account_id, type, amount, a.balance, 0);
        int ids[1] = { account_id };
        if (durable_update(1, ids, &a, 1, &t, loan_id) < 0) rc = OP_ERR;
        else {
            if (new_balance) *new_balance = a.balance;
            rc = OP_OK;
        }
    }

    db_unlock(fd, account_id, sizeof(Account));
    return rc;
}

int op_deposit(int account_id, long long amount, long long *new_balance) {
    return apply(account_id, TXN_DEPOSIT, amount, new_balance, 0);
}

int op_withdraw(int account_id, long long amount, long long *new_balance) {
    return apply(account_id, TXN_WITHDRAW, amount, new_balance, 0);
}

int op_transfer(int from_id, int to_id, long long amount) {
    if (amount <= 0) return OP_INVALID_AMOUNT;
    if (from_id == to_id) return OP_SAME_ACCOUNT;
    if (from_id < 1 || to_id < 1) return OP_NO_ACCOUNT;

    int fd = store_accounts_fd();

    /* DEADLOCK AVOIDANCE: always lock the lower id first, whatever the direction. */
    int first  = (from_id < to_id) ? from_id : to_id;
    int second = (from_id < to_id) ? to_id : from_id;

    if (db_lock(fd, first, sizeof(Account), F_WRLCK) < 0) return OP_ERR;
    if (db_lock(fd, second, sizeof(Account), F_WRLCK) < 0) {
        db_unlock(fd, first, sizeof(Account));
        return OP_ERR;
    }

    Account from, to;
    int rc;
    if (db_read(fd, from_id, sizeof from, &from) < 0 ||
        db_read(fd, to_id,   sizeof to,   &to)   < 0)        rc = OP_NO_ACCOUNT;
    else if (!from.active || !to.active)                     rc = OP_INACTIVE;
    else if (from.balance < amount)                          rc = OP_INSUFFICIENT;
    else if (amount > LLONG_MAX - to.balance)                rc = OP_INVALID_AMOUNT;
    else {
        from.balance -= amount;
        to.balance   += amount;
        Transaction t[2];
        make_txn(&t[0], from_id, TXN_TRANSFER_OUT, amount, from.balance, to_id);
        make_txn(&t[1], to_id,   TXN_TRANSFER_IN,  amount, to.balance,   from_id);
        int ids[2] = { from_id, to_id };
        Account accs[2] = { from, to };
        rc = (durable_update(2, ids, accs, 2, t, 0) < 0) ? OP_ERR : OP_OK;
    }

    db_unlock(fd, second, sizeof(Account));
    db_unlock(fd, first,  sizeof(Account));
    return rc;
}

int op_history(int account_id, Transaction *out, int max) {
    int fd = get_txns_fd();
    if (fd < 0 || max <= 0) return OP_ERR;

    if (db_lock_file(fd, F_RDLCK) < 0) return OP_ERR;   /* readers share; appends wait */
    int count = db_count(fd, sizeof(Transaction));
    int n = 0;
    for (int i = count; i >= 1 && n < max; i--) {       /* newest record is last in file */
        Transaction t;
        if (db_read(fd, i, sizeof t, &t) == 0 && t.account_id == account_id)
            out[n++] = t;
    }
    db_unlock_file(fd);
    return n;
}

int op_loan_credit(int account_id, long long amount) {
    return apply(account_id, TXN_LOAN_CREDIT, amount, NULL, 0);
}

/* Credit the account AND mark the loan APPROVED as one crash-safe step.
 * The caller (loan_decide) holds the loan record lock. */
int op_loan_approve_credit(int account_id, long long amount, int loan_id) {
    return apply(account_id, TXN_LOAN_CREDIT, amount, NULL, loan_id);
}
