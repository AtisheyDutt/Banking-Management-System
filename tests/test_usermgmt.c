#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "store.h"

#define CHECK(label, cond) printf("%-56s %s\n", label, (cond) ? "PASS" : "FAIL")

int main(void) {
    User u, e1, e2, c[5], list[20];

    if (store_init() < 0) return 1;
    int ruid = customer_create("ravi", "pw1", "Ravi Kumar", 100000, NULL, NULL);
    customer_create("meera", "pw2", "Meera Rao", 0, NULL, NULL);
    user_create("emp1", "pw", "Employee One", ROLE_EMPLOYEE, &e1);
    user_create("emp2", "pw", "Employee Two", ROLE_EMPLOYEE, &e2);

    /* --- passwords --- */
    CHECK("wrong old password rejected",
          user_change_password(ruid, "bad", "new1") == STORE_BAD_PASSWORD);
    CHECK("empty new password rejected",
          user_change_password(ruid, "pw1", "") == STORE_INVALID);
    CHECK("password changed", user_change_password(ruid, "pw1", "new1") == STORE_OK);
    CHECK("old password no longer works",
          user_login("ravi", "pw1", NULL) == LOGIN_BAD_CREDENTIALS);
    CHECK("new password works", user_login("ravi", "new1", NULL) == LOGIN_OK);

    /* --- details (ravi is logged in during these) --- */
    CHECK("rename to an existing username rejected",
          user_update_details(ruid, "meera", NULL) == STORE_DUPLICATE);
    CHECK("update username and full name",
          user_update_details(ruid, "ravi.k", "Ravi K. Kumar") == STORE_OK &&
          user_find_by_username("ravi.k", &u) == ruid &&
          strcmp(u.full_name, "Ravi K. Kumar") == 0);
    CHECK("old username is gone", user_find_by_username("ravi", NULL) == -1);
    CHECK("update did not log the user out", u.logged_in == 1);
    user_logout(ruid);
    CHECK("empty full name rejected", user_update_details(ruid, NULL, "") == STORE_INVALID);
    CHECK("update of a missing user rejected", user_update_details(99, NULL, "x") == STORE_NOT_FOUND);

    /* --- roles --- */
    CHECK("promote employee to manager",
          user_set_role(e1.id, ROLE_MANAGER) == STORE_OK &&
          user_get(e1.id, &u) == 0 && u.role == ROLE_MANAGER);
    CHECK("demote manager to employee",
          user_set_role(e1.id, ROLE_EMPLOYEE) == STORE_OK &&
          user_get(e1.id, &u) == 0 && u.role == ROLE_EMPLOYEE);
    CHECK("a customer cannot become a manager", user_set_role(ruid, ROLE_MANAGER) == STORE_INVALID);
    CHECK("nobody can be made admin", user_set_role(e1.id, ROLE_ADMIN) == STORE_INVALID);

    /* --- activation --- */
    CHECK("deactivate a user", user_set_active(ruid, 0) == STORE_OK);
    CHECK("inactive user cannot log in",
          user_login("ravi.k", "new1", NULL) == LOGIN_INACTIVE);
    CHECK("reactivate: login works again",
          user_set_active(ruid, 1) == STORE_OK && user_login("ravi.k", "new1", NULL) == LOGIN_OK);
    user_logout(ruid);
    CHECK("the admin cannot be deactivated", user_set_active(1, 0) == STORE_INVALID);

    /* --- listing --- */
    CHECK("list: 2 employees, 2 customers, 1 admin",
          user_list_by_role(ROLE_EMPLOYEE, list, 20) == 2 &&
          user_list_by_role(ROLE_CUSTOMER, list, 20) == 2 &&
          user_list_by_role(ROLE_ADMIN, list, 20) == 1);

    /* --- 5 processes rename 5 different users to the same name at once --- */
    for (int i = 0; i < 5; i++) {
        char nm[16];
        snprintf(nm, sizeof nm, "c%d", i);
        user_create(nm, "pw", "Temp", ROLE_CUSTOMER, &c[i]);
    }
    fflush(stdout);
    int ok = 0, status;
    for (int i = 0; i < 5; i++) {
        if (fork() == 0) _exit(user_update_details(c[i].id, "taken", NULL) == STORE_OK ? 0 : 1);
    }
    for (int i = 0; i < 5; i++) {
        wait(&status);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) ok++;
    }
    printf("%d of 5 simultaneous renames to \"taken\" succeeded (expected exactly 1)\n", ok);
    CHECK("exactly one rename wins", ok == 1);

    store_close();
    return 0;
}
