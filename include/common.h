#ifndef COMMON_H
#define COMMON_H

#define SERVER_PORT 8080

#define MAX_NAME_LEN     32
#define MAX_PASS_LEN     32
#define MAX_FULLNAME_LEN 64
#define MAX_MSG_LEN      256

/* Data files (all live in the data/ folder) */
#define USERS_FILE        "data/users.dat"
#define ACCOUNTS_FILE     "data/accounts.dat"
#define LOANS_FILE        "data/loans.dat"
#define TRANSACTIONS_FILE "data/transactions.dat"
#define FEEDBACK_FILE     "data/feedback.dat"

/* User roles */
#define ROLE_CUSTOMER 1
#define ROLE_EMPLOYEE 2
#define ROLE_MANAGER  3
#define ROLE_ADMIN    4

/* Loan statuses */
#define LOAN_PENDING  0   /* applied, not yet assigned */
#define LOAN_ASSIGNED 1   /* manager gave it to an employee */
#define LOAN_APPROVED 2
#define LOAN_REJECTED 3

/* Transaction types */
#define TXN_DEPOSIT      1
#define TXN_WITHDRAW     2
#define TXN_TRANSFER_OUT 3
#define TXN_TRANSFER_IN  4
#define TXN_LOAN_CREDIT  5

#endif
