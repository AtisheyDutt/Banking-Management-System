#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"
#include "store.h"
#include "session.h"
#include "journal.h"

int main(int argc, char **argv) {
    int port = (argc > 1) ? atoi(argv[1]) : SERVER_PORT;

    /* Finished children are reaped automatically (no zombie processes). */
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_handler = SIG_IGN;
    sa.sa_flags = SA_NOCLDWAIT;
    sigaction(SIGCHLD, &sa, NULL);
    signal(SIGPIPE, SIG_IGN);           /* writing to a closed socket gives an error, not a crash */

    if (store_init() < 0) return 1;
    journal_recover();                  /* finish anything a crash left half-done */
    store_reset_sessions();

    int ls = socket(AF_INET, SOCK_STREAM, 0);
    if (ls < 0) { perror("socket"); return 1; }
    int yes = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);   /* allow quick restarts */

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port = htons((unsigned short)port);
    if (bind(ls, (struct sockaddr *)&addr, sizeof addr) < 0) { perror("bind"); return 1; }
    if (listen(ls, 16) < 0) { perror("listen"); return 1; }

    printf("Banking server listening on port %d\n", port);
    fflush(stdout);

    for (;;) {
        int cfd = accept(ls, NULL, NULL);
        if (cfd < 0) {
            if (errno != EINTR) perror("accept");
            continue;
        }
        pid_t pid = fork();
        if (pid == 0) {                 /* child: serve this one client */
            close(ls);
            handle_client(cfd);
            close(cfd);
            _exit(0);
        }
        if (pid < 0) perror("fork");
        close(cfd);                     /* parent: the child owns the connection now */
    }
}
