#ifndef SESSION_INTERNAL_H
#define SESSION_INTERNAL_H

#include <stddef.h>
#include "models.h"

/* Everything the server remembers about one connected client. */
typedef struct {
    int fd;
    int user_id;      /* 0 = nobody logged in yet */
    int role;         /* ROLE_* */
    int account_id;   /* customers only, else 0 */
} Session;

#define BAD_AMOUNT "Invalid amount. Use a number like 100 or 100.50"

/* ----- shared helpers (session.c) ----- */
void reply1(Session *s, const char *status, const char *fmt, ...);   /* one-line reply */
const char *money(long long paise, char *buf, size_t n);             /* 105050 -> "Rs 1050.50" */
int  parse_amount(const char *s, long long *paise);                  /* 0 ok, -1 bad */
int  parse_id(const char *s, int *out);                              /* positive int: 0 ok, -1 bad */
int  no_args(const char *rest);                                      /* 1 if nothing follows the command */
int  split_args(char *rest, char **argv, int want);                  /* last arg keeps the rest of the line */
const char *role_name(int role);
const char *txn_name(int type);

void send_history(Session *s, int account_id, int max);
void send_loan_list(Session *s, const Loan *loans, int n);
void send_customer_list(Session *s);
void send_user_list(Session *s, int role);
void modify_user(Session *s, char **a, int allow_customer, int allow_employee);

/* ----- one handler per role: return 1 if the command was theirs, else 0 ----- */
int cmd_customer(Session *s, const char *cmd, char *rest);
int cmd_employee(Session *s, const char *cmd, char *rest);
int cmd_manager(Session *s, const char *cmd, char *rest);
int cmd_admin(Session *s, const char *cmd, char *rest);

#endif
