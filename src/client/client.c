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
        close(fd);
        return 1;
    }
    if (connect(fd, (struct sockaddr *)&sa, sizeof sa) < 0) {
        perror("connect");
        close(fd);
        return 1;
    }

    NetReader r;
    net_reader_init(&r, fd);

    int tty = isatty(STDIN_FILENO);
    int menus = tty && !getenv("BANK_RAW");   /* BANK_RAW=1 forces raw mode in a terminal */

    /* The server sends a greeting line first; consume it in both modes. */
    if (read_response(&r, NULL, 0, 0) < 0) {
        fprintf(stderr, "Server closed the connection.\n");
        close(fd);
        return 1;
    }

    if (menus) menu_mode(fd, &r);
    else       raw_mode(fd, &r, tty);

    close(fd);
    return 0;
}
