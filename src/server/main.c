#include <stdio.h>
#include <unistd.h>
#include "models.h"

static void show(const char *name, size_t size) {
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "%-12s %zu bytes per record\n", name, size);
    write(STDOUT_FILENO, buf, n);
}

int main(void) {
    show("User", sizeof(User));
    show("Account", sizeof(Account));
    show("Loan", sizeof(Loan));
    show("Transaction", sizeof(Transaction));
    show("Feedback", sizeof(Feedback));
    return 0;
}
