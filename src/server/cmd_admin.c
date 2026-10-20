#define _POSIX_C_SOURCE 200809L
#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include "store.h"
#include "session_internal.h"

/* a[0]=username a[1]=password a[2]=full name */
static void do_add_employee(Session *s, char **a) {
    User u;
    int rc = user_create(a[0], a[1], a[2], ROLE_EMPLOYEE, &u);
    if (rc > 0)                     reply1(s, "OK", "Employee created: user id %d.", u.id);
    else if (rc == STORE_DUPLICATE) reply1(s, "ERR", "That username is already taken.");
    else if (rc == STORE_INVALID)   reply1(s, "ERR", "Invalid details (empty or too long).");
    else                            reply1(s, "ERR", "Server error. Please try again.");
}

static void do_list_users(Session *s, char *role_str) {
    for (char *p = role_str; *p; p++) *p = (char)toupper((unsigned char)*p);
    if      (!strcmp(role_str, "CUSTOMER")) send_user_list(s, ROLE_CUSTOMER);
    else if (!strcmp(role_str, "EMPLOYEE")) send_user_list(s, ROLE_EMPLOYEE);
    else if (!strcmp(role_str, "MANAGER"))  send_user_list(s, ROLE_MANAGER);
    else if (!strcmp(role_str, "ADMIN"))    send_user_list(s, ROLE_ADMIN);
    else reply1(s, "ERR", "Role must be CUSTOMER, EMPLOYEE, MANAGER or ADMIN.");
}

static void do_set_role(Session *s, const char *id_str, int from_role, int to_role) {
    int id;
    User u;
    if (parse_id(id_str, &id) < 0) {
        reply1(s, "ERR", "Invalid user id.");
        return;
    }
    if (user_get(id, &u) < 0) {
        reply1(s, "ERR", "User not found.");
        return;
    }
    if (u.role != from_role) {
        reply1(s, "ERR", "That user is not %s.", from_role == ROLE_EMPLOYEE ? "an employee" : "a manager");
        return;
    }
    if (user_set_role(id, to_role) == STORE_OK)
        reply1(s, "OK", "User %d is now %s.", id, to_role == ROLE_MANAGER ? "a manager" : "an employee");
    else
        reply1(s, "ERR", "Server error. Please try again.");
}

static void do_set_active(Session *s, char **a) {
    int id;
    if (parse_id(a[0], &id) < 0 || (strcmp(a[1], "0") && strcmp(a[1], "1"))) {
        reply1(s, "ERR", "Usage: SETACTIVE <user_id> <0|1>");
        return;
    }
    int active = (a[1][0] == '1');
    switch (user_set_active(id, active)) {
    case STORE_OK:        reply1(s, "OK", "User %d is now %s.", id, active ? "active" : "deactivated"); break;
    case STORE_NOT_FOUND: reply1(s, "ERR", "User not found."); break;
    case STORE_INVALID:   reply1(s, "ERR", "The administrator account cannot be deactivated."); break;
    default:              reply1(s, "ERR", "Server error. Please try again.");
    }
}

int cmd_admin(Session *s, const char *cmd, char *rest) {
    char *a[3];
    if (!strcmp(cmd, "ADDEMPLOYEE")) {
        if (split_args(rest, a, 3) != 3) reply1(s, "ERR", "Usage: ADDEMPLOYEE <username> <password> <full name>");
        else                             do_add_employee(s, a);
    } else if (!strcmp(cmd, "MODUSER")) {
        if (split_args(rest, a, 3) != 3) reply1(s, "ERR", "Usage: MODUSER <user_id> <username or -> <full name or ->");
        else                             modify_user(s, a, 1, 1);
    } else if (!strcmp(cmd, "LISTUSERS")) {
        if (split_args(rest, a, 1) != 1) reply1(s, "ERR", "Usage: LISTUSERS <CUSTOMER|EMPLOYEE|MANAGER|ADMIN>");
        else                             do_list_users(s, a[0]);
    } else if (!strcmp(cmd, "LISTCUSTOMERS")) {
        if (!no_args(rest)) reply1(s, "ERR", "Usage: LISTCUSTOMERS");
        else                send_customer_list(s);
    } else if (!strcmp(cmd, "PROMOTE") || !strcmp(cmd, "DEMOTE")) {
        if (split_args(rest, a, 1) != 1) reply1(s, "ERR", "Usage: %s <user_id>", cmd);
        else if (!strcmp(cmd, "PROMOTE")) do_set_role(s, a[0], ROLE_EMPLOYEE, ROLE_MANAGER);
        else                              do_set_role(s, a[0], ROLE_MANAGER, ROLE_EMPLOYEE);
    } else if (!strcmp(cmd, "SETACTIVE")) {
        if (split_args(rest, a, 2) != 2) reply1(s, "ERR", "Usage: SETACTIVE <user_id> <0|1>");
        else                             do_set_active(s, a);
    } else {
        return 0;
    }
    return 1;
}
