#ifndef DB_H
#define DB_H

#include <stddef.h>

/* Generic fixed-size record file helpers.
 * Record ids start at 1. Record id N lives at byte offset (N-1) * rec_size.
 * Every model struct must have "int id" as its FIRST member. */

int db_open(const char *path);                                  /* fd, or -1 */
int db_count(int fd, size_t rec_size);                          /* number of records, or -1 */
int db_read(int fd, int id, size_t rec_size, void *buf);        /* 0 ok, -1 fail / not found */
int db_write(int fd, int id, size_t rec_size, const void *buf); /* 0 ok, -1 fail */
int db_append(int fd, size_t rec_size, void *rec);              /* locks file itself; sets rec->id, returns new id or -1 */

/* Record locks. type is F_RDLCK or F_WRLCK. db_lock blocks until granted. */
int db_lock(int fd, int id, size_t rec_size, int type);
int db_unlock(int fd, int id, size_t rec_size);

/* Whole-file lock, for "check then append" operations that must be atomic. */
int db_lock_file(int fd, int type);
int db_unlock_file(int fd);
/* Like db_append but the CALLER must already hold db_lock_file(fd, F_WRLCK). */
int db_append_nolock(int fd, size_t rec_size, void *rec);

#endif
