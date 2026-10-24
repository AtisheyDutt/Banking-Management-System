#define _POSIX_C_SOURCE 200809L
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "db.h"
#include "store.h"
#include "bank_ops.h"
#include "services.h"

/* These files are opened on first use, once per process. */
static int loans_fd = -1;
static int feedback_fd = -1;

static int get_loans_fd(void) {
    if (loans_fd < 0) loans_fd = db_open(LOANS_FILE);
    return loans_fd;
}
static int get_feedback_fd(void) {
    if (feedback_fd < 0) feedback_fd = db_open(FEEDBACK_FILE);
    return feedback_fd;
}

/* ---------- Loans ---------- */

int loan_apply(int customer_id, long long amount, Loan *out) {
    if (amount <= 0) return SVC_INVALID;

    User u;
    Account a;
    if (user_get(customer_id, &u) < 0 || u.role != ROLE_CUSTOMER) return SVC_NOT_FOUND;
    if (account_find_by_user(customer_id, &a) < 0) return SVC_NOT_FOUND;  /* needs an account to be paid into */

    Loan l;
    memset(&l, 0, sizeof l);
    l.customer_id = customer_id;
    l.amount      = amount;
    l.status      = LOAN_PENDING;
    l.employee_id = 0;

    int fd = get_loans_fd();
    if (fd < 0) return SVC_ERR;
    int id = db_append(fd, sizeof l, &l);       /* sets l.id */
    if (id < 0) return SVC_ERR;
    if (out) *out = l;
    return id;
}

int loan_get(int loan_id, Loan *out) {
    if (loan_id < 1) return SVC_NOT_FOUND;
    int fd = get_loans_fd();
    if (fd < 0) return SVC_ERR;
    if (db_lock(fd, loan_id, sizeof(Loan), F_RDLCK) < 0) return SVC_ERR;
    int rc = db_read(fd, loan_id, sizeof(Loan), out);
    db_unlock(fd, loan_id, sizeof(Loan));
    return (rc == 0) ? SVC_OK : SVC_NOT_FOUND;
}

int loan_assign(int loan_id, int employee_id) {
    User e;
    if (user_get(employee_id, &e) < 0 || e.role != ROLE_EMPLOYEE || !e.active) return SVC_INVALID;
    if (loan_id < 1) return SVC_NOT_FOUND;

    int fd = get_loans_fd();
    if (fd < 0) return SVC_ERR;
    if (db_lock(fd, loan_id, sizeof(Loan), F_WRLCK) < 0) return SVC_ERR;

    Loan l;
    int rc;
    if (db_read(fd, loan_id, sizeof l, &l) < 0)  rc = SVC_NOT_FOUND;
    else if (l.status != LOAN_PENDING)           rc = SVC_BAD_STATE;
    else {
        l.status = LOAN_ASSIGNED;
        l.employee_id = employee_id;
        rc = (db_write(fd, loan_id, sizeof l, &l) == 0) ? SVC_OK : SVC_ERR;
    }

    db_unlock(fd, loan_id, sizeof(Loan));
    return rc;
}

int loan_decide(int loan_id, int employee_id, int approve) {
    if (loan_id < 1) return SVC_NOT_FOUND;

    int fd = get_loans_fd();
    if (fd < 0) return SVC_ERR;

    /* Loan record stays locked for the whole decision, INCLUDING the credit.
     * Lock order is always: loan record, then account record. */
    if (db_lock(fd, loan_id, sizeof(Loan), F_WRLCK) < 0) return SVC_ERR;

    Loan l;
    int rc;
    if (db_read(fd, loan_id, sizeof l, &l) < 0)       rc = SVC_NOT_FOUND;
    else if (l.employee_id != employee_id)            rc = SVC_NOT_ALLOWED;
    else if (l.status != LOAN_ASSIGNED)               rc = SVC_BAD_STATE;
    else {
        rc = SVC_OK;
        if (approve) {
            Account a;
            if (account_find_by_user(l.customer_id, &a) < 0 ||
                op_loan_approve_credit(a.id, l.amount, loan_id) != OP_OK)
                rc = SVC_ACCOUNT_PROBLEM;           /* loan stays ASSIGNED, can be retried */
            else
                l.status = LOAN_APPROVED;
        } else {
            l.status = LOAN_REJECTED;
        }
        if (rc == SVC_OK && db_write(fd, loan_id, sizeof l, &l) < 0) rc = SVC_ERR;
    }

    db_unlock(fd, loan_id, sizeof(Loan));
    return rc;
}

int loan_list(int status, int employee_id, Loan *out, int max) {
    int fd = get_loans_fd();
    if (fd < 0 || max <= 0) return SVC_ERR;

    if (db_lock_file(fd, F_RDLCK) < 0) return SVC_ERR;
    int count = db_count(fd, sizeof(Loan));
    int n = 0;
    for (int i = 1; i <= count && n < max; i++) {
        Loan l;
        if (db_read(fd, i, sizeof l, &l) != 0) continue;
        if (status >= 0 && l.status != status) continue;
        if (employee_id > 0 && l.employee_id != employee_id) continue;
        out[n++] = l;
    }
    db_unlock_file(fd);
    return n;
}

/* ---------- Feedback ---------- */

int feedback_add(int customer_id, const char *message) {
    User u;
    if (!message || message[0] == '\0' || strlen(message) >= MAX_MSG_LEN) return SVC_INVALID;
    if (user_get(customer_id, &u) < 0 || u.role != ROLE_CUSTOMER) return SVC_INVALID;

    Feedback f;
    memset(&f, 0, sizeof f);
    f.customer_id = customer_id;
    snprintf(f.message, sizeof f.message, "%s", message);

    int fd = get_feedback_fd();
    if (fd < 0) return SVC_ERR;
    int id = db_append(fd, sizeof f, &f);
    return (id < 0) ? SVC_ERR : id;
}

int feedback_list(Feedback *out, int max) {
    int fd = get_feedback_fd();
    if (fd < 0 || max <= 0) return SVC_ERR;

    if (db_lock_file(fd, F_RDLCK) < 0) return SVC_ERR;
    int count = db_count(fd, sizeof(Feedback));
    int n = 0;
    for (int i = count; i >= 1 && n < max; i--) {
        Feedback f;
        if (db_read(fd, i, sizeof f, &f) == 0) out[n++] = f;
    }
    db_unlock_file(fd);
    return n;
}

/* ---------- Manager: account activation ---------- */

int account_set_active(int account_id, int active) {
    if (account_id < 1) return SVC_NOT_FOUND;
    int fd = store_accounts_fd();
    if (db_lock(fd, account_id, sizeof(Account), F_WRLCK) < 0) return SVC_ERR;

    Account a;
    int rc;
    if (db_read(fd, account_id, sizeof a, &a) < 0) rc = SVC_NOT_FOUND;
    else {
        a.active = active ? 1 : 0;
        rc = (db_write(fd, account_id, sizeof a, &a) == 0) ? SVC_OK : SVC_ERR;
    }

    db_unlock(fd, account_id, sizeof(Account));
    return rc;
}
