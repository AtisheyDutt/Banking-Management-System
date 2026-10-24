#ifndef BANK_OPS_H
#define BANK_OPS_H

#include "models.h"

/* Result codes (OP_OK is 0, everything else is a failure) */
#define OP_OK              0
#define OP_ERR            (-1)   /* I/O or locking failure */
#define OP_NO_ACCOUNT     (-2)
#define OP_INACTIVE       (-3)   /* account deactivated by the manager */
#define OP_INSUFFICIENT   (-4)
#define OP_INVALID_AMOUNT (-5)   /* zero, negative, or would overflow */
#define OP_SAME_ACCOUNT   (-6)

/* All amounts are in paise. new_balance may be NULL. */
int op_balance(int account_id, long long *balance);
int op_deposit(int account_id, long long amount, long long *new_balance);
int op_withdraw(int account_id, long long amount, long long *new_balance);
int op_transfer(int from_id, int to_id, long long amount);

/* Fills out[] with up to max transactions for the account, NEWEST FIRST.
 * Returns how many were stored, or OP_ERR. */
int op_history(int account_id, Transaction *out, int max);


/* Credit an approved loan to the account (logged as TXN_LOAN_CREDIT). */
int op_loan_credit(int account_id, long long amount);
int op_loan_approve_credit(int account_id, long long amount, int loan_id);

#endif
