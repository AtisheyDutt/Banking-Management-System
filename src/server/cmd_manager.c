#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "net.h"
#include "store.h"
#include "services.h"
#include "session_internal.h"

static void do_set_account(Session *s, const char *acc_str, int active) {
    int id;
    if (parse_id(acc_str, &id) < 0) {
        reply1(s, "ERR", "Invalid account number.");
        return;
    }
    switch (account_set_active(id, active)) {
    case SVC_OK:        reply1(s, "OK", "Account %d %s.", id, active ? "activated" : "deactivated"); break;
    case SVC_NOT_FOUND: reply1(s, "ERR", "Account not found."); break;
    default:            reply1(s, "ERR", "Server error. Please try again.");
    }
}

static void do_assign(Session *s, char **a) {
    int loan_id, emp_id;
    if (parse_id(a[0], &loan_id) < 0 || parse_id(a[1], &emp_id) < 0) {
        reply1(s, "ERR", "Invalid loan id or employee user id.");
        return;
    }
    switch (loan_assign(loan_id, emp_id)) {
    case SVC_OK:        reply1(s, "OK", "Loan #%d assigned to employee %d.", loan_id, emp_id); break;
    case SVC_INVALID:   reply1(s, "ERR", "That user is not an active employee."); break;
    case SVC_NOT_FOUND: reply1(s, "ERR", "Loan not found."); break;
    case SVC_BAD_STATE: reply1(s, "ERR", "Loan is not pending."); break;
    default:            reply1(s, "ERR", "Server error. Please try again.");
    }
}

static void do_feedbacks(Session *s) {
    Feedback f[20];
    int n = feedback_list(f, 20);
    if (n < 0) {
        reply1(s, "ERR", "Server error. Please try again.");
        return;
    }
    net_printf(s->fd, "OK %d feedback item(s), newest first\n", n);
    for (int i = 0; i < n; i++)
        net_printf(s->fd, "#%d from user %d: %s\n", f[i].id, f[i].customer_id, f[i].message);
    net_printf(s->fd, ".\n");
}

int cmd_manager(Session *s, const char *cmd, char *rest) {
    char *a[2];
    if (!strcmp(cmd, "LISTCUSTOMERS")) {
        if (!no_args(rest)) reply1(s, "ERR", "Usage: LISTCUSTOMERS");
        else                send_customer_list(s);
    } else if (!strcmp(cmd, "ACTIVATE") || !strcmp(cmd, "DEACTIVATE")) {
        if (split_args(rest, a, 1) != 1) reply1(s, "ERR", "Usage: %s <account_no>", cmd);
        else do_set_account(s, a[0], !strcmp(cmd, "ACTIVATE"));
    } else if (!strcmp(cmd, "PENDINGLOANS")) {
        Loan loans[50];
        int n = loan_list(LOAN_PENDING, 0, loans, 50);
        if (n < 0) reply1(s, "ERR", "Server error. Please try again.");
        else       send_loan_list(s, loans, n);
    } else if (!strcmp(cmd, "EMPLOYEES")) {
        if (!no_args(rest)) reply1(s, "ERR", "Usage: EMPLOYEES");
        else                send_user_list(s, ROLE_EMPLOYEE);
    } else if (!strcmp(cmd, "ASSIGN")) {
        if (split_args(rest, a, 2) != 2) reply1(s, "ERR", "Usage: ASSIGN <loan_id> <employee_user_id>");
        else                             do_assign(s, a);
    } else if (!strcmp(cmd, "FEEDBACKS")) {
        if (!no_args(rest)) reply1(s, "ERR", "Usage: FEEDBACKS");
        else                do_feedbacks(s);
    } else {
        return 0;
    }
    return 1;
}
