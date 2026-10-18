#ifndef MODELS_H
#define MODELS_H

#include <time.h>
#include "common.h"

/* Every person who can log in: customer, employee, manager, admin */
typedef struct {
    int  id;                              /* position in users.dat, starts at 1 */
    char username[MAX_NAME_LEN];
    char password[MAX_PASS_LEN];
    char full_name[MAX_FULLNAME_LEN];
    int  role;                            /* ROLE_* */
    int  active;                          /* 1 = can log in, 0 = deactivated */
    int  logged_in;                       /* 1 = has a session right now */
} User;

/* One bank account per customer */
typedef struct {
    int       id;
    int       user_id;                    /* owner (a User with ROLE_CUSTOMER) */
    long long balance;                    /* in paise: 1050 means Rs 10.50 */
    int       active;                     /* manager can deactivate */
} Account;

typedef struct {
    int       id;
    int       customer_id;                /* who applied */
    long long amount;                     /* in paise */
    int       status;                     /* LOAN_* */
    int       employee_id;                /* assigned employee, 0 if none */
} Loan;

/* One line of the passbook */
typedef struct {
    int       id;
    int       account_id;
    int       type;                       /* TXN_* */
    long long amount;                     /* in paise */
    long long balance_after;              /* balance after this transaction */
    int       other_account_id;           /* for transfers, else 0 */
    time_t    timestamp;
} Transaction;

typedef struct {
    int  id;
    int  customer_id;
    char message[MAX_MSG_LEN];
} Feedback;

#endif
