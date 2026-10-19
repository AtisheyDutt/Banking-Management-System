#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "db.h"
#include "store.h"
#include "bank_ops.h"

/* The transaction log is opened on first use, once per process. */
static int txns_fd = -1;
static int get_txns_fd(void) {
    if (txns_fd < 0) txns_fd = db_open(TRANSACTIONS_FILE);
    return txns_fd;
}

/* Append one passbook line. The caller still holds the account lock(s). */
static void log_txn(int account_id, int type, long long amount,
                    long long balance_after, int other_account_id) {
    Transaction t;
    memset(&t, 0, sizeof t);
    t.account_id       = account_id;
    t.type             = type;
    t.amount           = amount;
    t.balance_after    = balance_after;
    t.other_account_id = other_account_id;
    t.timestamp        = time(NULL);

    int fd = get_txns_fd();
    if (fd < 0 || db_append(fd, sizeof t, &t) < 0)
        fprintf(stderr, "warning: could not write transaction log\n");
}

int op_balance(int account_id, long long *balance) {
    Account a;
    if (account_get(account_id, &a) < 0) return OP_NO_ACCOUNT;   /* takes a read lock */
    if (balance) *balance = a.balance;
    return OP_OK;
}

/* Shared by deposit and withdraw: lock, read, check, write, log, unlock. */
static int apply(int account_id, int type, long long amount, long long *new_balance) {
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
        if (db_write(fd, account_id, sizeof a, &a) < 0) rc = OP_ERR;
        else {
            log_txn(account_id, type, amount, a.balance, 0);
            if (new_balance) *new_balance = a.balance;
            rc = OP_OK;
        }
    }

    db_unlock(fd, account_id, sizeof(Account));
    return rc;
}

int op_deposit(int account_id, long long amount, long long *new_balance) {
    return apply(account_id, TXN_DEPOSIT, amount, new_balance);
}

int op_withdraw(int account_id, long long amount, long long *new_balance) {
    return apply(account_id, TXN_WITHDRAW, amount, new_balance);
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
        if (db_write(fd, from_id, sizeof from, &from) < 0 ||
            db_write(fd, to_id,   sizeof to,   &to)   < 0) {
            rc = OP_ERR;
        } else {
            log_txn(from_id, TXN_TRANSFER_OUT, amount, from.balance, to_id);
            log_txn(to_id,   TXN_TRANSFER_IN,  amount, to.balance,   from_id);
            rc = OP_OK;
        }
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
    return apply(account_id, TXN_LOAN_CREDIT, amount, NULL);
}
