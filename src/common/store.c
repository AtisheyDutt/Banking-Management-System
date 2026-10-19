#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include "db.h"
#include "store.h"

static int users_fd    = -1;
static int accounts_fd = -1;

int store_init(void) {
    users_fd    = db_open(USERS_FILE);
    accounts_fd = db_open(ACCOUNTS_FILE);
    if (users_fd < 0 || accounts_fd < 0) {
        perror("store_init: cannot open data files (run from the project root)");
        return -1;
    }
    if (db_count(users_fd, sizeof(User)) == 0) {
        User admin;
        if (user_create("admin", "admin123", "System Administrator", ROLE_ADMIN, &admin) < 0) {
            fprintf(stderr, "store_init: could not create default admin\n");
            return -1;
        }
        printf("Created default admin (username: admin, password: admin123)\n");
    }
    return 0;
}

void store_close(void) {
    if (users_fd >= 0)    close(users_fd);
    if (accounts_fd >= 0) close(accounts_fd);
    users_fd = accounts_fd = -1;
}

/* ---------- Users ---------- */

int user_get(int id, User *out) {
    if (db_lock(users_fd, id, sizeof(User), F_RDLCK) < 0) return -1;
    int rc = db_read(users_fd, id, sizeof(User), out);
    db_unlock(users_fd, id, sizeof(User));
    return rc;
}

int user_find_by_username(const char *username, User *out) {
    int count = db_count(users_fd, sizeof(User));
    for (int i = 1; i <= count; i++) {
        User u;
        if (user_get(i, &u) == 0 && strcmp(u.username, username) == 0) {
            if (out) *out = u;
            return i;
        }
    }
    return -1;
}

int user_create(const char *username, const char *password,
                const char *full_name, int role, User *out) {
    if (!username || !password || !full_name || username[0] == '\0' ||
        strlen(username)  >= MAX_NAME_LEN ||
        strlen(password)  >= MAX_PASS_LEN ||
        strlen(full_name) >= MAX_FULLNAME_LEN ||
        role < ROLE_CUSTOMER || role > ROLE_ADMIN)
        return STORE_INVALID;

    /* Hold the whole-file write lock across "check for duplicate" AND "append",
     * so two simultaneous requests cannot both create the same username. */
    if (db_lock_file(users_fd, F_WRLCK) < 0) return STORE_ERR;

    int rc = 0;
    User u;
    int count = db_count(users_fd, sizeof(User));
    for (int i = 1; i <= count; i++) {
        if (db_read(users_fd, i, sizeof(User), &u) == 0 &&
            strcmp(u.username, username) == 0) {
            rc = STORE_DUPLICATE;
            break;
        }
    }
    if (rc == 0) {
        memset(&u, 0, sizeof u);
        snprintf(u.username,  sizeof u.username,  "%s", username);
        snprintf(u.password,  sizeof u.password,  "%s", password);
        snprintf(u.full_name, sizeof u.full_name, "%s", full_name);
        u.role = role;
        u.active = 1;
        u.logged_in = 0;
        rc = db_append_nolock(users_fd, sizeof(User), &u);
        if (rc > 0 && out) *out = u;
        if (rc < 0) rc = STORE_ERR;
    }

    db_unlock_file(users_fd);
    return rc;
}

int user_login(const char *username, const char *password, User *out) {
    User u;
    int id = user_find_by_username(username, &u);
    if (id < 0) return LOGIN_BAD_CREDENTIALS;

    /* Check and update under ONE write lock: this is what enforces
     * "one session per user" even when two logins arrive at the same instant. */
    if (db_lock(users_fd, id, sizeof(User), F_WRLCK) < 0) return LOGIN_ERROR;

    int rc;
    if (db_read(users_fd, id, sizeof(User), &u) < 0)  rc = LOGIN_ERROR;
    else if (strcmp(u.password, password) != 0)       rc = LOGIN_BAD_CREDENTIALS;
    else if (!u.active)                               rc = LOGIN_INACTIVE;
    else if (u.logged_in)                             rc = LOGIN_ALREADY_ACTIVE;
    else {
        u.logged_in = 1;
        if (db_write(users_fd, id, sizeof(User), &u) < 0) rc = LOGIN_ERROR;
        else { rc = LOGIN_OK; if (out) *out = u; }
    }

    db_unlock(users_fd, id, sizeof(User));
    return rc;
}

int user_logout(int id) {
    if (db_lock(users_fd, id, sizeof(User), F_WRLCK) < 0) return -1;
    User u;
    int rc = db_read(users_fd, id, sizeof(User), &u);
    if (rc == 0) {
        u.logged_in = 0;
        rc = db_write(users_fd, id, sizeof(User), &u);
    }
    db_unlock(users_fd, id, sizeof(User));
    return rc;
}

/* ---------- Accounts ---------- */

int account_get(int id, Account *out) {
    if (db_lock(accounts_fd, id, sizeof(Account), F_RDLCK) < 0) return -1;
    int rc = db_read(accounts_fd, id, sizeof(Account), out);
    db_unlock(accounts_fd, id, sizeof(Account));
    return rc;
}

int account_find_by_user(int user_id, Account *out) {
    int count = db_count(accounts_fd, sizeof(Account));
    for (int i = 1; i <= count; i++) {
        Account a;
        if (account_get(i, &a) == 0 && a.user_id == user_id) {
            if (out) *out = a;
            return i;
        }
    }
    return -1;
}

int account_create(int user_id, long long opening_balance, Account *out) {
    /* Validate the owner BEFORE taking the accounts lock (never hold two locks needlessly). */
    User owner;
    if (opening_balance < 0 || user_get(user_id, &owner) < 0 || owner.role != ROLE_CUSTOMER)
        return STORE_INVALID;

    if (db_lock_file(accounts_fd, F_WRLCK) < 0) return STORE_ERR;

    int rc = 0;
    Account a;
    int count = db_count(accounts_fd, sizeof(Account));
    for (int i = 1; i <= count; i++) {
        if (db_read(accounts_fd, i, sizeof(Account), &a) == 0 && a.user_id == user_id) {
            rc = STORE_DUPLICATE;               /* one account per customer */
            break;
        }
    }
    if (rc == 0) {
        memset(&a, 0, sizeof a);
        a.user_id = user_id;
        a.balance = opening_balance;
        a.active  = 1;
        rc = db_append_nolock(accounts_fd, sizeof(Account), &a);
        if (rc > 0 && out) *out = a;
        if (rc < 0) rc = STORE_ERR;
    }

    db_unlock_file(accounts_fd);
    return rc;
}

int customer_create(const char *username, const char *password,
                    const char *full_name, long long opening_balance,
                    User *u_out, Account *a_out) {
    User u;
    Account a;
    int uid = user_create(username, password, full_name, ROLE_CUSTOMER, &u);
    if (uid < 0) return uid;
    int aid = account_create(uid, opening_balance, &a);
    if (aid < 0) return aid;
    if (u_out) *u_out = u;
    if (a_out) *a_out = a;
    return uid;
}
