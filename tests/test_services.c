#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "store.h"
#include "bank_ops.h"
#include "services.h"

#define CHECK(label, cond) printf("%-56s %s\n", label, (cond) ? "PASS" : "FAIL")

static long long bal(int acc) { long long b = -1; op_balance(acc, &b); return b; }

int main(void) {
    User u, e1, e2, mg;
    Account ra;
    Loan l, list[20];
    Transaction h[5];
    Feedback f[5];

    if (store_init() < 0) return 1;
    int ruid = customer_create("ravi", "pw1", "Ravi Kumar", 100000, &u, &ra);
    int R = ra.id;
    user_create("emp1", "pw", "Employee One", ROLE_EMPLOYEE, &e1);
    user_create("emp2", "pw", "Employee Two", ROLE_EMPLOYEE, &e2);
    user_create("mgr",  "pw", "Manager",      ROLE_MANAGER,  &mg);

    /* --- apply and assign --- */
    CHECK("loan applied: pending and unassigned",
          loan_apply(ruid, 500000, &l) > 0 && l.status == LOAN_PENDING && l.employee_id == 0);
    int L1 = l.id;
    CHECK("zero-amount loan rejected", loan_apply(ruid, 0, NULL) == SVC_INVALID);
    CHECK("loan for a non-customer rejected", loan_apply(e1.id, 1000, NULL) == SVC_NOT_FOUND);
    CHECK("manager sees 1 pending loan", loan_list(LOAN_PENDING, 0, list, 20) == 1);
    CHECK("assign to a non-employee rejected", loan_assign(L1, ruid) == SVC_INVALID);
    CHECK("assign to emp1 ok", loan_assign(L1, e1.id) == SVC_OK);
    CHECK("assigning twice rejected", loan_assign(L1, e2.id) == SVC_BAD_STATE);
    CHECK("pending list now empty", loan_list(LOAN_PENDING, 0, list, 20) == 0);
    CHECK("emp1 has 1 assigned loan, emp2 has none",
          loan_list(LOAN_ASSIGNED, e1.id, list, 20) == 1 &&
          loan_list(LOAN_ASSIGNED, e2.id, list, 20) == 0);

    /* --- decide --- */
    CHECK("emp2 cannot decide emp1's loan", loan_decide(L1, e2.id, 1) == SVC_NOT_ALLOWED);
    CHECK("emp1 approves: balance +500000",
          loan_decide(L1, e1.id, 1) == SVC_OK && bal(R) == 600000);
    CHECK("deciding twice rejected, no double credit",
          loan_decide(L1, e1.id, 1) == SVC_BAD_STATE && bal(R) == 600000);
    CHECK("passbook shows the loan credit",
          op_history(R, h, 5) >= 1 && h[0].type == TXN_LOAN_CREDIT && h[0].amount == 500000);

    loan_apply(ruid, 300000, &l);
    int L2 = l.id;
    loan_assign(L2, e1.id);
    CHECK("reject: status rejected, balance unchanged",
          loan_decide(L2, e1.id, 0) == SVC_OK && loan_get(L2, &l) == SVC_OK &&
          l.status == LOAN_REJECTED && bal(R) == 600000);

    /* --- deactivated account blocks approval --- */
    loan_apply(ruid, 1000, &l);
    int L3 = l.id;
    loan_assign(L3, e1.id);
    CHECK("manager deactivates the account", account_set_active(R, 0) == SVC_OK);
    CHECK("deposit to deactivated account rejected", op_deposit(R, 100, NULL) == OP_INACTIVE);
    CHECK("approval blocked, loan stays ASSIGNED",
          loan_decide(L3, e1.id, 1) == SVC_ACCOUNT_PROBLEM &&
          loan_get(L3, &l) == SVC_OK && l.status == LOAN_ASSIGNED);
    CHECK("reactivated: approval now succeeds",
          account_set_active(R, 1) == SVC_OK && loan_decide(L3, e1.id, 1) == SVC_OK &&
          bal(R) == 601000);

    /* --- 5 processes approve the same loan at the same instant --- */
    loan_apply(ruid, 2000, &l);
    int L4 = l.id;
    loan_assign(L4, e1.id);
    fflush(stdout);
    int n = 5, ok = 0, status;
    for (int i = 0; i < n; i++) {
        if (fork() == 0) _exit(loan_decide(L4, e1.id, 1) == SVC_OK ? 0 : 1);
    }
    for (int i = 0; i < n; i++) {
        wait(&status);
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) ok++;
    }
    printf("%d of %d simultaneous approvals succeeded (expected exactly 1)\n", ok, n);
    CHECK("loan credited exactly once", ok == 1 && bal(R) == 603000);

    /* --- feedback --- */
    CHECK("feedback saved", feedback_add(ruid, "Great service") > 0);
    CHECK("empty feedback rejected", feedback_add(ruid, "") == SVC_INVALID);
    CHECK("feedback from a non-customer rejected", feedback_add(e1.id, "hi") == SVC_INVALID);
    feedback_add(ruid, "Please add more branches");
    CHECK("manager lists feedback, newest first",
          feedback_list(f, 5) == 2 && strcmp(f[0].message, "Please add more branches") == 0);

    store_close();
    return 0;
}
