#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "bank_ops.h"
#include "store.h"
#include "services.h"
#include "session_internal.h"

/* a[0]=username a[1]=password a[2]=opening amount a[3]=full name */
static void do_add_customer(Session *s, char **a) {
    long long opening;
    User u;
    Account acc;
    if (parse_amount(a[2], &opening) < 0) {
        reply1(s, "ERR", BAD_AMOUNT);
        return;
    }
    int rc = customer_create(a[0], a[1], a[3], opening, &u, &acc);
    if (rc > 0)                       reply1(s, "OK", "Customer created: user id %d, account number %d.", u.id, acc.id);
    else if (rc == STORE_DUPLICATE)   reply1(s, "ERR", "That username is already taken.");
    else if (rc == STORE_INVALID)     reply1(s, "ERR", "Invalid details (empty or too long).");
    else                              reply1(s, "ERR", "Server error. Please try again.");
}

static void do_decide(Session *s, const char *id_str, int approve) {
    int id;
    if (parse_id(id_str, &id) < 0) {
        reply1(s, "ERR", "Invalid loan id.");
        return;
    }
    switch (loan_decide(id, s->user_id, approve)) {
    case SVC_OK:
        if (approve) reply1(s, "OK", "Loan #%d approved and credited.", id);
        else         reply1(s, "OK", "Loan #%d rejected.", id);
        break;
    case SVC_NOT_FOUND:       reply1(s, "ERR", "Loan not found."); break;
    case SVC_NOT_ALLOWED:     reply1(s, "ERR", "That loan is not assigned to you."); break;
    case SVC_BAD_STATE:       reply1(s, "ERR", "That loan is not awaiting a decision."); break;
    case SVC_ACCOUNT_PROBLEM: reply1(s, "ERR", "Customer account is missing or deactivated. Loan left pending."); break;
    default:                  reply1(s, "ERR", "Server error. Please try again.");
    }
}

static void do_cust_history(Session *s, const char *acc_str) {
    int id;
    Account a;
    if (parse_id(acc_str, &id) < 0) {
        reply1(s, "ERR", "Invalid account number.");
        return;
    }
    if (account_get(id, &a) < 0) {
        reply1(s, "ERR", "Account not found.");
        return;
    }
    send_history(s, id, 20);
}

int cmd_employee(Session *s, const char *cmd, char *rest) {
    char *a[4];
    if (!strcmp(cmd, "ADDCUSTOMER")) {
        if (split_args(rest, a, 4) != 4)
            reply1(s, "ERR", "Usage: ADDCUSTOMER <username> <password> <opening_amount> <full name>");
        else do_add_customer(s, a);
    } else if (!strcmp(cmd, "MODCUSTOMER")) {
        if (split_args(rest, a, 3) != 3)
            reply1(s, "ERR", "Usage: MODCUSTOMER <user_id> <username or -> <full name or ->");
        else modify_user(s, a, 1, 0);
    } else if (!strcmp(cmd, "LISTCUSTOMERS")) {
        if (!no_args(rest)) reply1(s, "ERR", "Usage: LISTCUSTOMERS");
        else                send_customer_list(s);
    } else if (!strcmp(cmd, "MYLOANS")) {
        Loan loans[50];
        int n = loan_list(-1, s->user_id, loans, 50);
        if (n < 0) reply1(s, "ERR", "Server error. Please try again.");
        else       send_loan_list(s, loans, n);
    } else if (!strcmp(cmd, "APPROVE") || !strcmp(cmd, "REJECT")) {
        if (split_args(rest, a, 1) != 1) reply1(s, "ERR", "Usage: %s <loan_id>", cmd);
        else do_decide(s, a[0], !strcmp(cmd, "APPROVE"));
    } else if (!strcmp(cmd, "CUSTHISTORY")) {
        if (split_args(rest, a, 1) != 1) reply1(s, "ERR", "Usage: CUSTHISTORY <account_no>");
        else                             do_cust_history(s, a[0]);
    } else {
        return 0;
    }
    return 1;
}
