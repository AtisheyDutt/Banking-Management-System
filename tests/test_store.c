#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "store.h"

#define CHECK(label, cond) printf("%-50s %s\n", label, (cond) ? "PASS" : "FAIL")

int main(void) {
    User u;
    Account a;

    if (store_init() < 0) return 1;
    CHECK("default admin exists with id 1",
          user_find_by_username("admin", &u) == 1 && u.role == ROLE_ADMIN);

    store_close();                               /* simulate a server restart */
    if (store_init() < 0) return 1;

    int rid = customer_create("ravi", "pw1", "Ravi Kumar", 100000, &u, &a);
    CHECK("re-init made no 2nd admin (ravi is user id 2)", rid == 2 && a.user_id == 2);
    CHECK("opening balance stored in paise", a.balance == 100000);
    CHECK("duplicate username rejected",
          user_create("ravi", "x", "Other", ROLE_CUSTOMER, NULL) == STORE_DUPLICATE);
    CHECK("account found by user id", account_find_by_user(2, &a) > 0 && a.balance == 100000);
    CHECK("second account for same user rejected", account_create(2, 0, NULL) == STORE_DUPLICATE);
    CHECK("account for a non-customer (admin) rejected", account_create(1, 0, NULL) == STORE_INVALID);

    CHECK("wrong password rejected", user_login("ravi", "bad", NULL) == LOGIN_BAD_CREDENTIALS);
    CHECK("unknown user rejected",   user_login("nobody", "x", NULL) == LOGIN_BAD_CREDENTIALS);
    CHECK("correct login ok", user_login("ravi", "pw1", &u) == LOGIN_OK && u.logged_in == 1);
    CHECK("second login blocked (one session per user)",
          user_login("ravi", "pw1", NULL) == LOGIN_ALREADY_ACTIVE);
    CHECK("logout ok", user_logout(2) == 0);
    CHECK("login after logout ok", user_login("ravi", "pw1", NULL) == LOGIN_OK);
    user_logout(2);

    /* 5 processes try to log in as ravi at the same moment: exactly one may win */
    int n = 5, ok = 0, status;
    fflush(stdout);
    for (int i = 0; i < n; i++) {
        if (fork() == 0) {
            int rc = user_login("ravi", "pw1", NULL);
            _exit(rc == LOGIN_OK ? 0 : 1);
        }
    }
    for (int i = 0; i < n; i++) {
        wait(&status);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) ok++;
    }
    printf("%d of %d simultaneous logins succeeded (expected exactly 1)\n", ok, n);
    CHECK("exactly one simultaneous login wins", ok == 1);

    store_close();
    return 0;
}
