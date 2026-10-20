#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include "bank_ops.h"
#include "services.h"
#include "session_internal.h"

static const char *op_msg(int rc) {
    switch (rc) {
        case OP_NO_ACCOUNT:     return "Account not found.";
        case OP_INACTIVE:       return "Account is deactivated.";
        case OP_INSUFFICIENT:   return "Insufficient funds.";
        case OP_INVALID_AMOUNT: return "Invalid amount.";
        case OP_SAME_ACCOUNT:   return "Cannot transfer to the same account.";
        default:                return "Server error. Please try again.";
    }
}

static void do_balance(Session *s) {
    long long b;
    char m[40];
    int rc = op_balance(s->account_id, &b);
    if (rc != OP_OK) reply1(s, "ERR", "%s", op_msg(rc));
    else             reply1(s, "OK", "Balance: %s", money(b, m, sizeof m));
}

static void do_deposit_withdraw(Session *s, int type, const char *amount_str) {
    long long amount, nb;
    char m1[40], m2[40];
    if (parse_amount(amount_str, &amount) < 0) {
        reply1(s, "ERR", BAD_AMOUNT);
        return;
    }
    int rc = (type == TXN_DEPOSIT) ? op_deposit(s->account_id, amount, &nb)
                                   : op_withdraw(s->account_id, amount, &nb);
    if (rc != OP_OK) {
        reply1(s, "ERR", "%s", op_msg(rc));
        return;
    }
    reply1(s, "OK", "%s %s. New balance: %s", type == TXN_DEPOSIT ? "Deposited" : "Withdrew",
           money(amount, m1, sizeof m1), money(nb, m2, sizeof m2));
}

static void do_transfer(Session *s, const char *to_str, const char *amount_str) {
    long long amount;
    char m[40];
    int to;
    if (parse_id(to_str, &to) < 0) {
        reply1(s, "ERR", "Invalid account number.");
        return;
    }
    if (parse_amount(amount_str, &amount) < 0) {
        reply1(s, "ERR", BAD_AMOUNT);
        return;
    }
    int rc = op_transfer(s->account_id, to, amount);
    if (rc != OP_OK) reply1(s, "ERR", "%s", op_msg(rc));
    else reply1(s, "OK", "Transferred %s to account %d.", money(amount, m, sizeof m), to);
}

static void do_apply_loan(Session *s, const char *amount_str) {
    long long amount;
    char m[40];
    Loan l;
    if (parse_amount(amount_str, &amount) < 0) {
        reply1(s, "ERR", BAD_AMOUNT);
        return;
    }
    int rc = loan_apply(s->user_id, amount, &l);
    if (rc > 0)                    reply1(s, "OK", "Loan application #%d for %s submitted. Status: PENDING.", l.id, money(amount, m, sizeof m));
    else if (rc == SVC_INVALID)    reply1(s, "ERR", "Invalid amount.");
    else                           reply1(s, "ERR", "Could not submit the loan application.");
}

static void do_feedback(Session *s, const char *text) {
    int rc = feedback_add(s->user_id, text);
    if (rc > 0)                 reply1(s, "OK", "Thank you. Feedback #%d recorded.", rc);
    else if (rc == SVC_INVALID) reply1(s, "ERR", "Feedback must be 1 to 255 characters.");
    else                        reply1(s, "ERR", "Server error. Please try again.");
}

int cmd_customer(Session *s, const char *cmd, char *rest) {
    char *a[2];
    if (!strcmp(cmd, "BALANCE")) {
        if (!no_args(rest)) reply1(s, "ERR", "Usage: BALANCE");
        else                do_balance(s);
    } else if (!strcmp(cmd, "HISTORY")) {
        if (!no_args(rest)) reply1(s, "ERR", "Usage: HISTORY");
        else                send_history(s, s->account_id, 10);
    } else if (!strcmp(cmd, "DEPOSIT") || !strcmp(cmd, "WITHDRAW")) {
        if (split_args(rest, a, 1) != 1) reply1(s, "ERR", "Usage: %s <amount>", cmd);
        else do_deposit_withdraw(s, !strcmp(cmd, "DEPOSIT") ? TXN_DEPOSIT : TXN_WITHDRAW, a[0]);
    } else if (!strcmp(cmd, "TRANSFER")) {
        if (split_args(rest, a, 2) != 2) reply1(s, "ERR", "Usage: TRANSFER <account_no> <amount>");
        else                             do_transfer(s, a[0], a[1]);
    } else if (!strcmp(cmd, "APPLYLOAN")) {
        if (split_args(rest, a, 1) != 1) reply1(s, "ERR", "Usage: APPLYLOAN <amount>");
        else                             do_apply_loan(s, a[0]);
    } else if (!strcmp(cmd, "FEEDBACK")) {
        if (split_args(rest, a, 1) != 1) reply1(s, "ERR", "Usage: FEEDBACK <text>");
        else                             do_feedback(s, a[0]);
    } else {
        return 0;
    }
    return 1;
}
