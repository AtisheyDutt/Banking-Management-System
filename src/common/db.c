#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include "db.h"

static off_t offset_of(int id, size_t rec_size) {
    return (off_t)(id - 1) * (off_t)rec_size;
}

int db_open(const char *path) {
    return open(path, O_RDWR | O_CREAT, 0644);
}

int db_count(int fd, size_t rec_size) {
    struct stat st;
    if (fstat(fd, &st) < 0) return -1;
    return (int)(st.st_size / (off_t)rec_size);
}

int db_read(int fd, int id, size_t rec_size, void *buf) {
    if (id < 1) return -1;
    ssize_t n = pread(fd, buf, rec_size, offset_of(id, rec_size));
    return (n == (ssize_t)rec_size) ? 0 : -1;   /* short read = no such record */
}

int db_write(int fd, int id, size_t rec_size, const void *buf) {
    if (id < 1) return -1;
    ssize_t n = pwrite(fd, buf, rec_size, offset_of(id, rec_size));
    return (n == (ssize_t)rec_size) ? 0 : -1;
}

int db_lock(int fd, int id, size_t rec_size, int type) {
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_type   = type;
    fl.l_whence = SEEK_SET;
    fl.l_start  = offset_of(id, rec_size);
    fl.l_len    = (off_t)rec_size;
    while (fcntl(fd, F_SETLKW, &fl) == -1) {
        if (errno != EINTR) return -1;          /* retry if a signal interrupted us */
    }
    return 0;
}

int db_unlock(int fd, int id, size_t rec_size) {
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = offset_of(id, rec_size);
    fl.l_len    = (off_t)rec_size;
    return fcntl(fd, F_SETLK, &fl);
}

int db_append(int fd, size_t rec_size, void *rec) {
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_type   = F_WRLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;                            /* 0 = whole file, including future growth */
    while (fcntl(fd, F_SETLKW, &fl) == -1) {
        if (errno != EINTR) return -1;
    }

    int count = db_count(fd, rec_size);
    int id = (count < 0) ? -1 : count + 1;
    if (id > 0) {
        *(int *)rec = id;                       /* id is always the first member */
        if (db_write(fd, id, rec_size, rec) < 0) id = -1;
    }

    fl.l_type = F_UNLCK;
    fcntl(fd, F_SETLK, &fl);
    return id;
}

int db_lock_file(int fd, int type) {
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_type   = type;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;                            /* 0 = whole file, including future growth */
    while (fcntl(fd, F_SETLKW, &fl) == -1) {
        if (errno != EINTR) return -1;
    }
    return 0;
}

int db_unlock_file(int fd) {
    struct flock fl;
    memset(&fl, 0, sizeof fl);
    fl.l_type   = F_UNLCK;
    fl.l_whence = SEEK_SET;
    fl.l_start  = 0;
    fl.l_len    = 0;
    return fcntl(fd, F_SETLK, &fl);
}

int db_append_nolock(int fd, size_t rec_size, void *rec) {
    int count = db_count(fd, rec_size);
    if (count < 0) return -1;
    int id = count + 1;
    *(int *)rec = id;                           /* id is always the first member */
    if (db_write(fd, id, rec_size, rec) < 0) return -1;
    return id;
}
