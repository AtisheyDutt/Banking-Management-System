#ifndef STORE_H
#define STORE_H

#include "models.h"

/* Error codes returned by create functions (a positive return is the new id) */
#define STORE_ERR       (-1)   /* I/O or locking failure */
#define STORE_DUPLICATE (-2)   /* username already taken / account already exists */
#define STORE_INVALID   (-3)   /* bad input (empty or too long, wrong role) */

/* Results of user_login */
#define LOGIN_OK              0
#define LOGIN_BAD_CREDENTIALS 1   /* unknown user OR wrong password (we do not say which) */
#define LOGIN_INACTIVE        2
#define LOGIN_ALREADY_ACTIVE  3   /* one session per user */
#define LOGIN_ERROR          (-1)

/* Open the data files; create the default admin if users.dat is empty.
 * Paths are relative ("data/..."), so run from the project root. */
int  store_init(void);
void store_close(void);

/* Users. "out" may be NULL on every function that has it. */
int user_get(int id, User *out);                                    /* 0 ok, -1 not found */
int user_find_by_username(const char *username, User *out);         /* id, or -1 */
int user_create(const char *username, const char *password,
                const char *full_name, int role, User *out);        /* id or STORE_* */
int user_login(const char *username, const char *password, User *out); /* LOGIN_* */
int user_logout(int id);                                            /* 0 ok, -1 fail */

/* Accounts */
int account_get(int id, Account *out);                              /* 0 ok, -1 not found */
int account_find_by_user(int user_id, Account *out);                /* id, or -1 */
int account_create(int user_id, long long opening_balance, Account *out); /* id or STORE_* */

/* Convenience: user (ROLE_CUSTOMER) + account in one call. Returns user id or STORE_*. */
int customer_create(const char *username, const char *password,
                    const char *full_name, long long opening_balance,
                    User *u_out, Account *a_out);


/* Raw descriptor of accounts.dat, used by the money operations in bank_ops.c */
int store_accounts_fd(void);


/* ----- User management (Step 7) ----- */
#define STORE_OK            0
#define STORE_BAD_PASSWORD (-4)   /* old password did not match */
#define STORE_NOT_FOUND    (-5)

/* Change own password: needs the old one. */
int user_change_password(int user_id, const char *old_pw, const char *new_pw);

/* Change username and/or full name. Pass NULL to leave a field unchanged. */
int user_update_details(int user_id, const char *new_username, const char *new_full_name);

/* Admin: switch between ROLE_EMPLOYEE and ROLE_MANAGER only. */
int user_set_role(int user_id, int new_role);

/* Activate / deactivate a user. The admin account cannot be deactivated. */
int user_set_active(int user_id, int active);

/* Fill out[] with users of this role (up to max). Returns the count.
 * NOTE: the structs include passwords, so never send them to a client. */
int user_list_by_role(int role, User *out, int max);


/* Server start-up: mark every user as logged out. */
void store_reset_sessions(void);


#endif
