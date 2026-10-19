#define _POSIX_C_SOURCE 200809L
#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>
#include <unistd.h>
#include "common.h"
#include "net.h"

/* Print reply lines until the "." terminator. -1 if the server went away. */
static int read_response(NetReader *r) {
    char line[1024];
    for (;;) {
        if (net_readline(r, line, sizeof line) < 0) return -1;
        if (strcmp(line, ".") == 0) return 0;
        printf("%s\n", line);
    }
}

int main(int argc, char **argv) {
    const char *host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : SERVER_PORT;

    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return 1; }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof sa);
    sa.sin_family = AF_INET;
    sa.sin_port = htons((unsigned short)port);
    if (inet_pton(AF_INET, host, &sa.sin_addr) != 1) {
        fprintf(stderr, "Bad IPv4 address: %s\n", host);
        return 1;
    }
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) { perror("connect"); return 1; }

    NetReader r;
    net_reader_init(&r, fd);
    if (read_response(&r) < 0) {
        fprintf(stderr, "Server closed the connection.\n");
        return 1;
    }

    int tty = isatty(STDIN_FILENO);
    char line[512];
    for (;;) {
        if (tty) { printf("> "); fflush(stdout); }
        if (!fgets(line, sizeof line, stdin)) break;        /* end of input */
        line[strcspn(line, "\r\n")] = '\0';
        if (net_printf(fd, "%s\n", line) < 0) break;
        if (read_response(&r) < 0) {
            fprintf(stderr, "Server closed the connection.\n");
            break;
        }
        if (strcasecmp(line, "QUIT") == 0) break;
    }
    close(fd);
    return 0;
}
