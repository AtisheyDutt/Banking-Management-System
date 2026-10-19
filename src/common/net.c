#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include "net.h"

void net_reader_init(NetReader *r, int fd) {
    r->fd = fd;
    r->start = r->end = 0;
}

int net_readline(NetReader *r, char *out, size_t max) {
    size_t len = 0;
    for (;;) {
        if (r->start == r->end) {                       /* buffer empty: read more */
            ssize_t n;
            do {
                n = read(r->fd, r->buf, sizeof r->buf);
            } while (n < 0 && errno == EINTR);
            if (n <= 0) return -1;                      /* 0 = peer closed, <0 = error */
            r->start = 0;
            r->end = (size_t)n;
        }
        char c = r->buf[r->start++];
        if (c == '\n') {
            out[len] = '\0';
            return (int)len;
        }
        if (c == '\r') continue;
        if (len + 1 < max) out[len++] = c;
    }
}

int net_write_all(int fd, const char *buf, size_t len) {
    while (len > 0) {
        ssize_t n = write(fd, buf, len);
        if (n < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        buf += n;
        len -= (size_t)n;
    }
    return 0;
}

int net_printf(int fd, const char *fmt, ...) {
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return -1;
    if ((size_t)n >= sizeof buf) n = (int)sizeof buf - 1;
    return net_write_all(fd, buf, (size_t)n);
}
