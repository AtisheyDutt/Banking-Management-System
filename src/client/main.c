#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"
#include "net.h"

/* One menu entry: a label, the protocol command it sends, and the questions to ask.
 * Each answer becomes one argument of the command. */
typedef struct {
    const char *label;
    const char *command;
    const char *prompts[4];
} MenuItem;

static const MenuItem CUSTOMER_MENU[] = {
    { "Check balance",       "BALANCE",   { NULL } },
    { "Deposit money",       "DEPOSIT",   { "Amount (Rs): " } },
    { "Withdraw money",      "WITHDRAW",  { "Amount (Rs): " } },
    { "Transfer money",      "TRANSFER",  { "To account number: ", "Amount (Rs): " } },
    { "Transaction history", "HISTORY",   { NULL } },
    { "Apply for a loan",    "APPLYLOAN", { "Loan amount (Rs): " } },
    { "Submit feedback",     "FEEDBACK",  { "Your feedback: " } },
    { "Change password",     "PASSWORD",  { "Old password: ", "New password: " } },
    { "Logout",              "LOGOUT",    { NULL } },
    { NULL, NULL, { NULL } }
};

static const MenuItem EMPLOYEE_MENU[] = {
    { "Add customer",                  "ADDCUSTOMER",  { "Username: ", "Password: ", "Opening balance (Rs): ", "Full name: " } },
    { "Modify customer details",       "MODCUSTOMER",  { "Customer user id: ", "New username (- to keep): ", "New full name (- to keep): " } },
    { "List customers",                "LISTCUSTOMERS", { NULL } },
    { "View my assigned loans",        "MYLOANS",      { NULL } },
    { "Approve a loan",                "APPROVE",      { "Loan id: " } },
    { "Reject a loan",                 "REJECT",       { "Loan id: " } },
    { "View a customer's transactions", "CUSTHISTORY", { "Account number: " } },
    { "Change password",               "PASSWORD",     { "Old password: ", "New password: " } },
    { "Logout",                        "LOGOUT",       { NULL } },
    { NULL, NULL, { NULL } }
};

static const MenuItem MANAGER_MENU[] = {
    { "List customers",                   "LISTCUSTOMERS", { NULL } },
    { "Activate a customer account",      "ACTIVATE",      { "Account number: " } },
    { "Deactivate a customer account",    "DEACTIVATE",    { "Account number: " } },
    { "List pending loan applications",   "PENDINGLOANS",  { NULL } },
    { "List employees",                   "EMPLOYEES",     { NULL } },
    { "Assign a loan to an employee",     "ASSIGN",        { "Loan id: ", "Employee user id: " } },
    { "Review customer feedback",         "FEEDBACKS",     { NULL } },
    { "Change password",                  "PASSWORD",      { "Old password: ", "New password: " } },
    { "Logout",                           "LOGOUT",        { NULL } },
    { NULL, NULL, { NULL } }
};

static const MenuItem ADMIN_MENU[] = {
    { "Add employee",                       "ADDEMPLOYEE", { "Username: ", "Password: ", "Full name: " } },
    { "Modify customer/employee details",   "MODUSER",     { "User id: ", "New username (- to keep): ", "New full name (- to keep): " } },
    { "List users by role",                 "LISTUSERS",   { "Role (CUSTOMER/EMPLOYEE/MANAGER/ADMIN): " } },
    { "List customers",                     "LISTCUSTOMERS", { NULL } },
    { "Promote employee to manager",        "PROMOTE",     { "User id: " } },
    { "Demote manager to employee",         "DEMOTE",      { "User id: " } },
    { "Activate / deactivate a user",       "SETACTIVE",   { "User id: ", "1 = active, 0 = deactivated: " } },
    { "Change password",                    "PASSWORD",    { "Old password: ", "New password: " } },
    { "Logout",                             "LOGOUT",      { NULL } },
    { NULL, NULL, { NULL } }
};

/* Print reply lines until the "." terminator. Copies the first line into `first`.
 * pretty=1 turns "OK ..." / "ERR ..." into friendlier text for menu users.
 * Returns -1 if the server went away. */
static int read_response(NetReader *r, char *first, size_t first_n, int pretty) {
    char line[1024];
    int is_first = 1;
    for (;;) {
        if (net_readline(r, line, sizeof line) < 0) return -1;
        if (strcmp(line, ".") == 0) return 0;
        if (is_first && first) snprintf(first, first_n, "%s", line);
        if (pretty && is_first && strncmp(line, "OK ", 3) == 0)        printf("%s\n", line + 3);
        else if (pretty && is_first && strncmp(line, "ERR ", 4) == 0)  printf("Error: %s\n", line + 4);
        else                                                           printf("%s\n", line);
        is_first = 0;
    }
}

static int ask(const char *prompt, char *buf, size_t n) {
    printf("%s", prompt);
    fflush(stdout);
    if (!fgets(buf, (int)n, stdin)) return -1;
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static const MenuItem *menu_for(const char *login_reply) {
    if (strstr(login_reply, "Role: CUSTOMER")) return CUSTOMER_MENU;
    if (strstr(login_reply, "Role: EMPLOYEE")) return EMPLOYEE_MENU;
    if (strstr(login_reply, "Role: MANAGER"))  return MANAGER_MENU;
    if (strstr(login_reply, "Role: ADMIN"))    return ADMIN_MENU;
    return NULL;
}

static void say_goodbye(int fd, NetReader *r) {
    net_printf(fd, "QUIT\n");
    read_response(r, NULL, 0, 1);
}

/* Interactive mode: menus per role. */
static void menu_mode(int fd, NetReader *r) {
    const MenuItem *menu = NULL;
    char first[1024], in[256], cmd[1024];

    for (;;) {
        if (!menu) {                                            /* not logged in */
            printf("\n=== Banking System ===\n 1. Login\n 0. Exit\n");
            if (ask("Choice: ", in, sizeof in) < 0) return;
            int choice = atoi(in);
            if (choice == 0) { say_goodbye(fd, r); return; }
            if (choice != 1) continue;

            char user[64], pass[64];
            if (ask("Username: ", user, sizeof user) < 0 || ask("Password: ", pass, sizeof pass) < 0) return;
            net_printf(fd, "LOGIN %s %s\n", user, pass);
            first[0] = '\0';
            if (read_response(r, first, sizeof first, 1) < 0) { fprintf(stderr, "Server closed the connection.\n"); return; }
            if (strncmp(first, "OK", 2) == 0) menu = menu_for(first);
            continue;
        }

        printf("\n");
        int count = 0;
        for (; menu[count].label; count++) printf("%2d. %s\n", count + 1, menu[count].label);
        printf(" 0. Exit\n");
        if (ask("Choice: ", in, sizeof in) < 0) return;
        int choice = atoi(in);
        if (choice == 0) { say_goodbye(fd, r); return; }
        if (choice < 1 || choice > count) { printf("Invalid choice.\n"); continue; }

        const MenuItem *it = &menu[choice - 1];
        size_t len = (size_t)snprintf(cmd, sizeof cmd, "%s", it->command);
        for (int i = 0; i < 4 && it->prompts[i]; i++) {
            char answer[256];
            if (ask(it->prompts[i], answer, sizeof answer) < 0) return;
            if (len >= sizeof cmd) break;
            len += (size_t)snprintf(cmd + len, sizeof cmd - len, " %s", answer);
        }
        net_printf(fd, "%s\n", cmd);
        if (read_response(r, first, sizeof first, 1) < 0) { fprintf(stderr, "Server closed the connection.\n"); return; }
        if (!strcmp(it->command, "LOGOUT") && strncmp(first, "OK", 2) == 0) menu = NULL;
    }
}

/* Raw mode: type protocol commands directly (also used when input is piped). */
static void raw_mode(int fd, NetReader *r, int tty) {
    char line[512];
    for (;;) {
        if (tty) { printf("> "); fflush(stdout); }
        if (!fgets(line, sizeof line, stdin)) break;
        line[strcspn(line, "\r\n")] = '\0';
        if (net_printf(fd, "%s\n", line) < 0) break;
        if (read_response(r, NULL, 0, 0) < 0) {
            fprintf(stderr, "Server closed the connection.\n");
            break;
        }
        if (strcasecmp(line, "QUIT") == 0) break;
    }
}

int main(int argc, char **argv) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : SERVER_PORT;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        fprintf(stderr, "Bad IPv4 address: %s\n", host);
        close(fd);
        return 1;
    }
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    NetReader r;
    net_reader_init(&r, fd);

    int tty = isatty(STDIN_FILENO);
    int menus = tty && !getenv("BANK_RAW");   /* BANK_RAW=1 forces raw mode in a terminal */

    /* The server sends a greeting line first; consume it in both modes. */
    if (read_response(&r, NULL, 0, 0) < 0) {
        fprintf(stderr, "Server closed the connection.\n");
        close(fd);
        return 1;
    }

    if (menus) menu_mode(fd, &r);
    else       raw_mode(fd, &r, tty);

    close(fd);
    return 0;
}
