#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "db.h"
#include "models.h"

#define TEST_FILE "data/test_users.dat"

static void make_user(User *u, const char *name, int role) {
    memset(u, 0, sizeof *u);
    snprintf(u->username,  sizeof u->username,  "%s", name);
    snprintf(u->password,  sizeof u->password,  "%s", "pass123");
    snprintf(u->full_name, sizeof u->full_name, "%s Test", name);
    u->role = role;
    u->active = 1;
}

int main(void) {
    unlink(TEST_FILE);
    int fd = db_open(TEST_FILE);
    if (fd < 0) { perror("db_open"); return 1; }

    User a, b, r;
    make_user(&a, "alice", ROLE_CUSTOMER);
    make_user(&b, "bob", ROLE_EMPLOYEE);
    printf("appended alice -> id %d\n", db_append(fd, sizeof(User), &a));
    printf("appended bob   -> id %d\n", db_append(fd, sizeof(User), &b));
    printf("record count   = %d\n", db_count(fd, sizeof(User)));

    if (db_read(fd, 2, sizeof(User), &r) == 0)
        printf("read id 2      : %s, role %d\n", r.username, r.role);
    if (db_read(fd, 99, sizeof(User), &r) < 0)
        printf("read id 99     : not found (expected)\n");

    /* Locking demo: parent holds a write lock, child must wait for it */
    pid_t pid = fork();
    if (pid == 0) {
        int cfd = db_open(TEST_FILE);           /* child opens its own fd */
        sleep(1);                               /* let the parent grab the lock first */
        printf("[child]  asking for write lock on record 1...\n");
        db_lock(cfd, 1, sizeof(User), F_WRLCK);
        User c;
        db_read(cfd, 1, sizeof(User), &c);
        printf("[child]  got the lock, full_name = %s\n", c.full_name);
        db_unlock(cfd, 1, sizeof(User));
        close(cfd);
        return 0;
    }

    db_lock(fd, 1, sizeof(User), F_WRLCK);
    printf("[parent] holding write lock on record 1 for 3 seconds\n");
    db_read(fd, 1, sizeof(User), &r);
    snprintf(r.full_name, sizeof r.full_name, "Alice Updated");
    sleep(3);
    db_write(fd, 1, sizeof(User), &r);
    printf("[parent] updated record 1, releasing lock\n");
    db_unlock(fd, 1, sizeof(User));

    waitpid(pid, NULL, 0);
    close(fd);
    unlink(TEST_FILE);
    return 0;
}
