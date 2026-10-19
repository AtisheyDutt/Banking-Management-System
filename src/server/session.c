#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <limits.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include "net.h"
#include "store.h"
#include "bank_ops.h"
#include "services.h"
#include "session.h"

/* Everything the server remembers about one connected client. */
typedef struct {
    int fd;
    int user_id;      /* 0 = nobody logged in yet */
    int role;         /* ROLE_* */
    int account_id;   /* customers only, else 0 */
} Session;

/* ---------- reply helpers ----------
 * A reply is one or more lines; the first starts with OK or ERR;
 * a line containing just "." ends the reply. */

static void reply1(Session *s, const char *status, const char *fmt, ...) {
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    net_printf(s->fd, "%s %s\n.\n", status, msg);
}

static const char *role_name(int role) {
    switch (role) {
        case ROLE_CUSTOMER: return "CUSTOMER";
        case ROLE_EMPLOYEE: return "EMPLOYEE";
        case ROLE_MANAGER:  return "MANAGER";
        case ROLE_ADMIN:    return "ADMIN";
        default:            return "UNKNOWN";
    }
}

static const char *txn_name(int type) {
    switch (type) {
        case TXN_DEPOSIT:      return "DEPOSIT";
        case TXN_WITHDRAW:     return "WITHDRAWAL";
        case TXN_TRANSFER_OUT: return "TRANSFER OUT";
        case TXN_TRANSFER_IN:  return "TRANSFER IN";
        case TXN_LOAN_CREDIT:  return "LOAN CREDIT";
        default:               return "UNKNOWN";
    }
}

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

/* 105050 -> "Rs 1050.50" */
static const char *money(long long paise, char *buf, size_t n) {
    snprintf(buf, n, "Rs %lld.%02lld", paise / 100, paise % 100);
    return buf;
}

/* "250", "250.5", "250.50" -> paise. Strict: digits, optional '.', 1-2 decimals. */
static int parse_amount(const char *s, long long *paise) {
    if (!s || !*s) return -1;
    long long whole = 0, frac = 0;
    int digits = 0;
    const char *p = s;
    while (isdigit((unsigned char)*p)) {
        if (++digits > 12) return -1;                   /* keeps the arithmetic far from overflow */
        whole = whole * 10 + (*p - '0');
        p++;
    }
    if (digits == 0) return -1;
    if (*p == '.') {
        p++;
        int d = 0;
        while (isdigit((unsigned char)*p) && d < 2) {
            frac = frac * 10 + (*p - '0');
            p++;
            d++;
        }
        if (d == 0) return -1;
        if (d == 1) frac *= 10;
    }
    if (*p != '\0') return -1;                          /* junk, or a third decimal digit */
    *paise = whole * 100 + frac;
    return 0;
}

#define BAD_AMOUNT "Invalid amount. Use a number like 100 or 100.50"

/* ---------- commands ---------- */

static void do_help(Session *s) {
    if (!s->user_id) {
        net_printf(s->fd, "OK Commands: LOGIN <username> <password>, HELP, QUIT\n.\n");
    } else if (s->role == ROLE_CUSTOMER) {
        net_printf(s->fd,
            "OK Customer commands: BALANCE, DEPOSIT <amount>, WITHDRAW <amount>, "
            "TRANSFER <account_no> <amount>, HISTORY, WHOAMI, LOGOUT, QUIT\n.\n");
    } else {
        net_printf(s->fd, "OK Commands for the %s role are coming soon. "
                          "Available now: WHOAMI, LOGOUT, QUIT\n.\n", role_name(s->role));
    }
}

static void do_login(Session *s, const char *username, const char *password) {
    if (s->user_id) {
        reply1(s, "ERR", "Already logged in. LOGOUT first.");
        return;
    }
    User u;
    switch (user_login(username, password, &u)) {
    case LOGIN_OK: {
        s->user_id = u.id;
        s->role = u.role;
        s->account_id = 0;
        if (u.role == ROLE_CUSTOMER) {
            Account a;
            if (account_find_by_user(u.id, &a) > 0) s->account_id = a.id;
        }
        reply1(s, "OK", "Welcome %s. Role: %s.", u.full_name, role_name(u.role));
        break;
    }
    case LOGIN_BAD_CREDENTIALS:
        reply1(s, "ERR", "Invalid username or password.");
        break;
    case LOGIN_INACTIVE:
        reply1(s, "ERR", "This user has been deactivated. Please contact the bank.");
        break;
    case LOGIN_ALREADY_ACTIVE:
        reply1(s, "ERR", "This user is already logged in elsewhere.");
        break;
    default:
        reply1(s, "ERR", "Server error. Please try again.");
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
    reply1(s, "OK", "%s %s. New balance: %s",
           type == TXN_DEPOSIT ? "Deposited" : "Withdrew",
           money(amount, m1, sizeof m1), money(nb, m2, sizeof m2));
}

static void do_transfer(Session *s, const char *to_str, const char *amount_str) {
    long long amount;
    char m[40];
    char *end;
    long to = strtol(to_str, &end, 10);
    if (*end != '\0' || to < 1 || to > INT_MAX) {
        reply1(s, "ERR", "Invalid account number.");
        return;
    }
    if (parse_amount(amount_str, &amount) < 0) {
        reply1(s, "ERR", BAD_AMOUNT);
        return;
    }
    int rc = op_transfer(s->account_id, (int)to, amount);
    if (rc != OP_OK) reply1(s, "ERR", "%s", op_msg(rc));
    else reply1(s, "OK", "Transferred %s to account %ld.", money(amount, m, sizeof m), to);
}

static void do_history(Session *s) {
    Transaction h[10];
    int n = op_history(s->account_id, h, 10);
    if (n < 0) {
        reply1(s, "ERR", "Server error. Please try again.");
        return;
    }
    net_printf(s->fd, "OK %d transaction(s), newest first\n", n);
    for (int i = 0; i < n; i++) {
        char ts[32], a[40], b[40];
        struct tm tmv;
        localtime_r(&h[i].timestamp, &tmv);
        strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tmv);
        net_printf(s->fd, "%s  %-12s %12s  balance %s\n", ts, txn_name(h[i].type),
                   money(h[i].amount, a, sizeof a), money(h[i].balance_after, b, sizeof b));
    }
    net_printf(s->fd, ".\n");
}

/* Returns 1 if the client asked to quit. */
static int dispatch(Session *s, char *input) {
    char *save = NULL;
    char *cmd = strtok_r(input, " \t", &save);
    if (!cmd) {
        reply1(s, "ERR", "Empty command. Type HELP.");
        return 0;
    }
    char *a1 = strtok_r(NULL, " \t", &save);
    char *a2 = strtok_r(NULL, " \t", &save);
    char *a3 = strtok_r(NULL, " \t", &save);
    int n = (a1 != NULL) + (a2 != NULL) + (a3 != NULL);
    for (char *p = cmd; *p; p++) *p = (char)toupper((unsigned char)*p);

    if (!strcmp(cmd, "QUIT")) {
        reply1(s, "OK", "Goodbye.");
        return 1;                       /* handle_client logs the user out */
    }
    if (!strcmp(cmd, "HELP")) { do_help(s); return 0; }
    if (!strcmp(cmd, "LOGIN")) {
        if (n != 2) reply1(s, "ERR", "Usage: LOGIN <username> <password>");
        else        do_login(s, a1, a2);
        return 0;
    }
    if (!s->user_id) {
        reply1(s, "ERR", "Please LOGIN first.");
        return 0;
    }
    if (!strcmp(cmd, "LOGOUT")) {
        user_logout(s->user_id);
        s->user_id = s->role = s->account_id = 0;
        reply1(s, "OK", "Logged out.");
        return 0;
    }
    if (!strcmp(cmd, "WHOAMI")) {
        reply1(s, "OK", "User id %d, role %s.", s->user_id, role_name(s->role));
        return 0;
    }

    int is_customer_cmd = !strcmp(cmd, "BALANCE") || !strcmp(cmd, "DEPOSIT") ||
                          !strcmp(cmd, "WITHDRAW") || !strcmp(cmd, "TRANSFER") ||
                          !strcmp(cmd, "HISTORY");
    if (is_customer_cmd) {
        if (s->role != ROLE_CUSTOMER || s->account_id == 0) {
            reply1(s, "ERR", "That command is not available for your role.");
        } else if (!strcmp(cmd, "BALANCE")) {
            if (n != 0) reply1(s, "ERR", "Usage: BALANCE");
            else        do_balance(s);
        } else if (!strcmp(cmd, "HISTORY")) {
            if (n != 0) reply1(s, "ERR", "Usage: HISTORY");
            else        do_history(s);
        } else if (!strcmp(cmd, "TRANSFER")) {
            if (n != 2) reply1(s, "ERR", "Usage: TRANSFER <account_no> <amount>");
            else        do_transfer(s, a1, a2);
        } else {
            if (n != 1) reply1(s, "ERR", "Usage: %s <amount>", cmd);
            else        do_deposit_withdraw(s, !strcmp(cmd, "DEPOSIT") ? TXN_DEPOSIT : TXN_WITHDRAW, a1);
        }
        return 0;
    }

    reply1(s, "ERR", "Unknown command. Type HELP.");
    return 0;
}

void handle_client(int fd) {
    Session s;
    memset(&s, 0, sizeof s);
    s.fd = fd;

    NetReader r;
    net_reader_init(&r, fd);
    char buf[512];

    reply1(&s, "OK", "Welcome to the Banking System. Type HELP for commands. "
                     "Start with: LOGIN <username> <password>");
    while (net_readline(&r, buf, sizeof buf) >= 0) {
        if (dispatch(&s, buf)) break;
    }
    /* QUIT or dropped connection: free this user's session so they can log in again. */
    if (s.user_id) user_logout(s.user_id);
}
