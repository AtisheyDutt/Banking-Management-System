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
#include "session_internal.h"

/* ================= helpers shared by all roles ================= */

/* A reply is one or more lines; the first starts with OK or ERR;
 * a line containing just "." ends the reply. */
void reply1(Session *s, const char *status, const char *fmt, ...) {
    char msg[400];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    net_printf(s->fd, "%s %s\n.\n", status, msg);
}

const char *role_name(int role) {
    switch (role) {
        case ROLE_CUSTOMER: return "CUSTOMER";
        case ROLE_EMPLOYEE: return "EMPLOYEE";
        case ROLE_MANAGER:  return "MANAGER";
        case ROLE_ADMIN:    return "ADMIN";
        default:            return "UNKNOWN";
    }
}

const char *txn_name(int type) {
    switch (type) {
        case TXN_DEPOSIT:      return "DEPOSIT";
        case TXN_WITHDRAW:     return "WITHDRAWAL";
        case TXN_TRANSFER_OUT: return "TRANSFER OUT";
        case TXN_TRANSFER_IN:  return "TRANSFER IN";
        case TXN_LOAN_CREDIT:  return "LOAN CREDIT";
        default:               return "UNKNOWN";
    }
}

static const char *loan_status_name(int status) {
    switch (status) {
        case LOAN_PENDING:  return "PENDING";
        case LOAN_ASSIGNED: return "ASSIGNED";
        case LOAN_APPROVED: return "APPROVED";
        case LOAN_REJECTED: return "REJECTED";
        default:            return "UNKNOWN";
    }
}

const char *money(long long paise, char *buf, size_t n) {
    snprintf(buf, n, "Rs %lld.%02lld", paise / 100, paise % 100);
    return buf;
}

/* "250", "250.5", "250.50" -> paise. Strict: digits, optional '.', 1-2 decimals. */
int parse_amount(const char *s, long long *paise) {
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

int parse_id(const char *s, int *out) {
    if (!s || !*s) return -1;
    char *end;
    long v = strtol(s, &end, 10);
    if (*end != '\0' || v < 1 || v > INT_MAX) return -1;
    *out = (int)v;
    return 0;
}

int no_args(const char *rest) {
    while (*rest == ' ' || *rest == '\t') rest++;
    return *rest == '\0';
}

/* Split `rest` into up to `want` arguments. The LAST argument keeps the remainder of
 * the line, so it may contain spaces (full names, feedback text). Returns how many
 * arguments were found (0..want). */
int split_args(char *rest, char **argv, int want) {
    int n = 0;
    while (n < want) {
        while (*rest == ' ' || *rest == '\t') rest++;
        if (*rest == '\0') break;
        argv[n++] = rest;
        if (n == want) {
            size_t l = strlen(rest);
            while (l > 0 && (rest[l - 1] == ' ' || rest[l - 1] == '\t')) rest[--l] = '\0';
            break;
        }
        while (*rest && *rest != ' ' && *rest != '\t') rest++;
        if (*rest) *rest++ = '\0';
    }
    return n;
}

void send_history(Session *s, int account_id, int max) {
    Transaction h[50];
    if (max > 50) max = 50;
    int n = op_history(account_id, h, max);
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

void send_loan_list(Session *s, const Loan *loans, int n) {
    net_printf(s->fd, "OK %d loan(s)\n", n);
    for (int i = 0; i < n; i++) {
        char m[40], who[32];
        if (loans[i].employee_id > 0) snprintf(who, sizeof who, "employee %d", loans[i].employee_id);
        else                          snprintf(who, sizeof who, "unassigned");
        net_printf(s->fd, "Loan #%d  customer user %d  %s  %s  %s\n", loans[i].id,
                   loans[i].customer_id, money(loans[i].amount, m, sizeof m),
                   loan_status_name(loans[i].status), who);
    }
    net_printf(s->fd, ".\n");
}

void send_customer_list(Session *s) {
    User list[100];
    int n = user_list_by_role(ROLE_CUSTOMER, list, 100);
    net_printf(s->fd, "OK %d customer(s)\n", n);
    for (int i = 0; i < n; i++) {
        Account a;
        int acct = account_find_by_user(list[i].id, &a);
        net_printf(s->fd, "user %-3d acct %-3d %-12s %-22s user:%s account:%s\n",
                   list[i].id, acct > 0 ? a.id : 0, list[i].username, list[i].full_name,
                   list[i].active ? "active" : "deactivated",
                   (acct > 0 && a.active) ? "active" : "deactivated");
    }
    net_printf(s->fd, ".\n");
}

/* Never sends passwords. */
void send_user_list(Session *s, int role) {
    User list[100];
    int n = user_list_by_role(role, list, 100);
    const char *label = (role == ROLE_CUSTOMER) ? "customer(s)" :
                        (role == ROLE_EMPLOYEE) ? "employee(s)" :
                        (role == ROLE_MANAGER)  ? "manager(s)"  : "admin(s)";
    net_printf(s->fd, "OK %d %s\n", n, label);
    for (int i = 0; i < n; i++)
        net_printf(s->fd, "user %-3d %-12s %-22s %s\n", list[i].id, list[i].username,
                   list[i].full_name, list[i].active ? "active" : "deactivated");
    net_printf(s->fd, ".\n");
}

/* a[0] = user id, a[1] = new username or "-", a[2] = new full name or "-" */
void modify_user(Session *s, char **a, int allow_customer, int allow_employee) {
    int uid;
    User u;
    if (parse_id(a[0], &uid) < 0) {
        reply1(s, "ERR", "Invalid user id.");
        return;
    }
    if (user_get(uid, &u) < 0 ||
        !((allow_customer && u.role == ROLE_CUSTOMER) || (allow_employee && u.role == ROLE_EMPLOYEE))) {
        reply1(s, "ERR", allow_employee ? "Customer or employee not found." : "Customer not found.");
        return;
    }
    const char *new_username  = strcmp(a[1], "-") ? a[1] : NULL;
    const char *new_full_name = strcmp(a[2], "-") ? a[2] : NULL;
    if (!new_username && !new_full_name) {
        reply1(s, "ERR", "Nothing to change.");
        return;
    }
    switch (user_update_details(uid, new_username, new_full_name)) {
    case STORE_OK:        reply1(s, "OK", "User %d updated.", uid); break;
    case STORE_DUPLICATE: reply1(s, "ERR", "That username is already taken."); break;
    case STORE_INVALID:   reply1(s, "ERR", "Invalid details (empty or too long)."); break;
    default:              reply1(s, "ERR", "Server error. Please try again.");
    }
}

/* ================= login and the command dispatcher ================= */

static const char *const HELP_CUSTOMER[] = {
    "BALANCE", "DEPOSIT <amount>", "WITHDRAW <amount>", "TRANSFER <account_no> <amount>",
    "HISTORY", "APPLYLOAN <amount>", "FEEDBACK <text>", NULL };
static const char *const HELP_EMPLOYEE[] = {
    "ADDCUSTOMER <username> <password> <opening_amount> <full name>",
    "MODCUSTOMER <user_id> <username or -> <full name or ->", "LISTCUSTOMERS", "MYLOANS",
    "APPROVE <loan_id>", "REJECT <loan_id>", "CUSTHISTORY <account_no>", NULL };
static const char *const HELP_MANAGER[] = {
    "LISTCUSTOMERS", "ACTIVATE <account_no>", "DEACTIVATE <account_no>", "PENDINGLOANS",
    "EMPLOYEES", "ASSIGN <loan_id> <employee_user_id>", "FEEDBACKS", NULL };
static const char *const HELP_ADMIN[] = {
    "ADDEMPLOYEE <username> <password> <full name>",
    "MODUSER <user_id> <username or -> <full name or ->", "LISTUSERS <CUSTOMER|EMPLOYEE|MANAGER|ADMIN>",
    "LISTCUSTOMERS", "PROMOTE <user_id>", "DEMOTE <user_id>", "SETACTIVE <user_id> <0|1>", NULL };
static const char *const HELP_COMMON[] = { "PASSWORD <old> <new>", "WHOAMI", "LOGOUT", "QUIT", NULL };

static void do_help(Session *s) {
    if (!s->user_id) {
        net_printf(s->fd, "OK Before login: LOGIN <username> <password>, HELP, QUIT\n.\n");
        return;
    }
    const char *const *list = (s->role == ROLE_CUSTOMER) ? HELP_CUSTOMER :
                              (s->role == ROLE_EMPLOYEE) ? HELP_EMPLOYEE :
                              (s->role == ROLE_MANAGER)  ? HELP_MANAGER  : HELP_ADMIN;
    net_printf(s->fd, "OK Commands for %s:\n", role_name(s->role));
    for (int i = 0; list[i]; i++) net_printf(s->fd, "  %s\n", list[i]);
    for (int i = 0; HELP_COMMON[i]; i++) net_printf(s->fd, "  %s\n", HELP_COMMON[i]);
    net_printf(s->fd, ".\n");
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

static void do_password(Session *s, char **a) {
    switch (user_change_password(s->user_id, a[0], a[1])) {
    case STORE_OK:           reply1(s, "OK", "Password changed."); break;
    case STORE_BAD_PASSWORD: reply1(s, "ERR", "Old password is incorrect."); break;
    case STORE_INVALID:      reply1(s, "ERR", "New password must be 1 to 31 characters."); break;
    default:                 reply1(s, "ERR", "Server error. Please try again.");
    }
}

/* Returns 1 if the client asked to quit. */
static int dispatch(Session *s, char *input) {
    char *save = NULL;
    char *cmd = strtok_r(input, " \t", &save);
    if (!cmd) {
        reply1(s, "ERR", "Empty command. Type HELP.");
        return 0;
    }
    char rest[512];                                     /* everything after the command word */
    snprintf(rest, sizeof rest, "%s", save ? save : "");
    for (char *p = cmd; *p; p++) *p = (char)toupper((unsigned char)*p);

    if (!strcmp(cmd, "QUIT")) {
        reply1(s, "OK", "Goodbye.");
        return 1;                                       /* handle_client logs the user out */
    }
    if (!strcmp(cmd, "HELP")) { do_help(s); return 0; }
    if (!strcmp(cmd, "LOGIN")) {
        char *a[2];
        if (split_args(rest, a, 2) != 2) reply1(s, "ERR", "Usage: LOGIN <username> <password>");
        else                             do_login(s, a[0], a[1]);
        return 0;
    }
    if (!s->user_id) {
        reply1(s, "ERR", "Please LOGIN first.");
        return 0;
    }

    /* commands every role has */
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
    if (!strcmp(cmd, "PASSWORD")) {
        char *a[2];
        if (split_args(rest, a, 2) != 2) reply1(s, "ERR", "Usage: PASSWORD <old> <new>");
        else                             do_password(s, a);
        return 0;
    }

    /* role-specific commands: the role comes from the server's own session, never from the client */
    int handled = 0;
    switch (s->role) {
    case ROLE_CUSTOMER: handled = cmd_customer(s, cmd, rest); break;
    case ROLE_EMPLOYEE: handled = cmd_employee(s, cmd, rest); break;
    case ROLE_MANAGER:  handled = cmd_manager(s, cmd, rest);  break;
    case ROLE_ADMIN:    handled = cmd_admin(s, cmd, rest);    break;
    }
    if (!handled) reply1(s, "ERR", "Unknown command, or not available for your role. Type HELP.");
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
