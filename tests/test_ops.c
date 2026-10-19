#include <stdio.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <unistd.h>
#include "store.h"
#include "bank_ops.h"

#define CHECK(label, cond) printf("%-56s %s\n", label, (cond) ? "PASS" : "FAIL")

static long long bal(int acc) { long long b = -1; op_balance(acc, &b); return b; }

int main(void) {
    User u;
    Account ra, ma;
    if (store_init() < 0) return 1;
    customer_create("ravi",  "pw1", "Ravi Kumar", 100000, &u, &ra);
    customer_create("meera", "pw2", "Meera Rao",   50000, &u, &ma);
    int R = ra.id, M = ma.id;
    long long b;

    CHECK("deposit 25000 -> balance 125000", op_deposit(R, 25000, &b) == OP_OK && b == 125000);
    CHECK("withdraw 5000 -> balance 120000", op_withdraw(R, 5000, &b) == OP_OK && b == 120000);
    CHECK("overdraw rejected, balance unchanged",
          op_withdraw(R, 999999, NULL) == OP_INSUFFICIENT && bal(R) == 120000);
    CHECK("zero deposit rejected", op_deposit(R, 0, NULL) == OP_INVALID_AMOUNT);
    CHECK("negative withdraw rejected", op_withdraw(R, -5, NULL) == OP_INVALID_AMOUNT);
    CHECK("transfer 20000 ravi -> meera",
          op_transfer(R, M, 20000) == OP_OK && bal(R) == 100000 && bal(M) == 70000);
    CHECK("transfer to same account rejected", op_transfer(R, R, 100) == OP_SAME_ACCOUNT);
    CHECK("transfer to missing account rejected, nothing lost",
          op_transfer(R, 99, 100) == OP_NO_ACCOUNT && bal(R) == 100000);
    CHECK("transfer above balance rejected", op_transfer(M, R, 999999) == OP_INSUFFICIENT);

    Transaction h[10];
    int n = op_history(R, h, 10);
    CHECK("ravi history: 3 entries, newest first",
          n == 3 && h[0].type == TXN_TRANSFER_OUT && h[1].type == TXN_WITHDRAW &&
          h[2].type == TXN_DEPOSIT);
    n = op_history(M, h, 10);
    CHECK("meera history: incoming transfer from ravi",
          n == 1 && h[0].type == TXN_TRANSFER_IN && h[0].other_account_id == R &&
          h[0].balance_after == 70000);

    /* Stress 1: opposite transfers at the same time. Without ordered locking
     * this is the classic deadlock. Each side moves 200 x 100 paise. */
    fflush(stdout);
    for (int i = 0; i < 2; i++) {
        if (fork() == 0) {
            for (int k = 0; k < 200; k++) {
                if (i == 0) op_transfer(R, M, 100);
                else        op_transfer(M, R, 100);
            }
            _exit(0);
        }
    }
    for (int i = 0; i < 2; i++) wait(NULL);
    CHECK("opposite transfers finished, balances back to start",
          bal(R) == 100000 && bal(M) == 70000);

    /* Stress 2: lost-update test. 4 processes x 100 deposits of 100 = +40000. */
    for (int i = 0; i < 4; i++) {
        if (fork() == 0) {
            for (int k = 0; k < 100; k++) op_deposit(R, 100, NULL);
            _exit(0);
        }
    }
    for (int i = 0; i < 4; i++) wait(NULL);
    CHECK("400 concurrent deposits all counted (140000)", bal(R) == 140000);

    static Transaction big[1000];
    CHECK("every operation logged exactly once (803 entries)",
          op_history(R, big, 1000) == 803);

    store_close();
    return 0;
}
