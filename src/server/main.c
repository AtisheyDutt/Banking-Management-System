#include <unistd.h>

int main(void) {
    const char msg[] = "Banking server: setup OK\n";
    write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    return 0;
}
