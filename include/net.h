#ifndef NET_H
#define NET_H

#include <stddef.h>

/* Buffered line reader for a socket. Protocol: one command per line. */
typedef struct {
    int    fd;
    size_t start, end;
    char   buf[2048];
} NetReader;

void net_reader_init(NetReader *r, int fd);

/* Reads one line into out (newline and CR removed; extra characters beyond
 * max-1 are dropped). Returns the length, or -1 on disconnect / error. */
int net_readline(NetReader *r, char *out, size_t max);

int net_write_all(int fd, const char *buf, size_t len);   /* 0 ok, -1 error */
int net_printf(int fd, const char *fmt, ...);             /* 0 ok, -1 error */

#endif
