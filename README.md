# Banking Management System

CS-513 System Software mini project. A client/server banking system written in C
(sockets, `fork`, `fcntl` record locking, signals) with four roles:
**customer, employee, manager, administrator**.

## Build

```bash
make            # builds bin/server, bin/client, bin/seed
make clean
```

## Run

Run the seed tool and the server from the directory where the `data/` folder
should live.

```bash
mkdir -p data
./bin/seed                       # creates the admin and five sample customers
./bin/server [port]              # terminal 1, default port 8080
./bin/client [host] [port]       # terminal 2 (menu mode), host defaults to 127.0.0.1
BANK_RAW=1 ./bin/client          # raw protocol mode (type commands directly)
```

When input is piped, the client automatically uses raw mode, which is how the
tests drive it.

### Sample logins

| Role     | Username / password |
|----------|---------------------|
| Admin    | `admin` / `admin123` |
| Employee | `emp1` / `emp123` |
| Manager  | `mgr1` / `mgr123` |
| Customer | `alice`/`alice123`, `bob`/`bob123`, `carol`/`carol123`, `dave`/`dave123`, `erin`/`erin123` |

More employees and managers can be created by the admin (`ADDEMPLOYEE`, then `PROMOTE`).

## Architecture

```
  client (menu / raw)                 server
  src/client/main.c                   src/server/main.c
        |                             parent: socket, bind, listen,
        |  TCP, line protocol         accept loop, fork() per client
        +--------------------------->        |
                                             | child process
                                             v
                                   session.c: login, one session per user,
                                   command parsing and dispatch
                                             |
        +------------------+-----------------+-----------------+
        v                  v                 v                 v
  cmd_customer.c    cmd_employee.c    cmd_manager.c      cmd_admin.c
        +------------------+-----------------+-----------------+
                                             v
                         bank_ops.c    services.c    store.c
                         (money)       (loans,       (users, accounts,
                                        feedback)     login state)
                                             v
                                   db.c: fixed-size record files
                                   with fcntl record locks
                                             v
                                        data/*.dat
```

| Layer | Files | Job |
|-------|-------|-----|
| Client | `src/client/main.c` | Menu per role, or raw command mode |
| Server main | `src/server/main.c` | Listening socket, `fork` per connection, signal setup |
| Session | `src/server/session.c`, `include/session_internal.h` | Login/logout, command parsing, role check, dispatch |
| Commands | `src/server/cmd_*.c` | One module per role |
| Operations | `src/common/bank_ops.c` | Deposit, withdraw, transfer, history |
| Services | `src/common/services.c` | Loans and feedback |
| Store | `src/common/store.c` | Users, accounts, login state |
| Record I/O | `src/common/db.c` | Fixed-size record files and locking |
| Protocol | `src/common/net.c` | Buffered line reader, `net_printf` |

## System-software concepts used

| Concept | Where |
|---------|-------|
| TCP sockets (`socket`, `bind`, `listen`, `accept`, `connect`) | `src/server/main.c`, `src/client/main.c` |
| Process per client (`fork`) | `src/server/main.c` |
| Signals: `SIGCHLD` reaped with `sigaction`, `SIGPIPE` ignored | `src/server/main.c` |
| `pread` / `pwrite` on fixed-size records (record N at offset `(N-1)*size`) | `src/common/db.c` |
| Record locking with `fcntl` (`F_SETLKW` blocking, `F_SETLK` non-blocking, `F_RDLCK`/`F_WRLCK`) | `db_lock`, `db_unlock` |
| Whole-file lock so check-then-append is atomic | `db_lock_file`, `db_append` |
| One session per user across processes; stale sessions cleared at start | `store.c` |
| Retrying locks after `EINTR` | `db.c` |

Design note: concurrency comes from separate processes sharing files, with
`fcntl` locks keeping the data consistent. The server does not use threads,
shared memory, semaphores or pipes.

## Commands

All roles: `LOGIN <user> <password>`, `LOGOUT`, `QUIT`.

| Role | Commands |
|------|----------|
| Customer | `BALANCE`, `HISTORY`, `DEPOSIT <amt>`, `WITHDRAW <amt>`, `TRANSFER <account_no> <amt>`, `APPLYLOAN <amt>`, `FEEDBACK <text>` |
| Employee | `ADDCUSTOMER <user> <pass> <opening_amt> <full name>`, `MODCUSTOMER <user_id> <username or -> <full name or ->`, `LISTCUSTOMERS`, `MYLOANS`, `APPROVE <loan_id>`, `REJECT <loan_id>`, `CUSTHISTORY <account_no>` |
| Manager | `LISTCUSTOMERS`, `ACTIVATE <account_no>`, `DEACTIVATE <account_no>`, `PENDINGLOANS`, `EMPLOYEES`, `ASSIGN <loan_id> <employee_user_id>`, `FEEDBACKS` |
| Admin | `ADDEMPLOYEE <user> <pass> <full name>`, `MODUSER <user_id> <username or -> <full name or ->`, `LISTUSERS <CUSTOMER, EMPLOYEE, MANAGER or ADMIN>`, `LISTCUSTOMERS`, `PROMOTE <user_id>`, `DEMOTE <user_id>`, `SETACTIVE <user_id> <0 or 1>` |
| Employee | `emp1` / `emp123` |
| Manager  | `mgr1` / `mgr123` |

Amounts are in rupees with up to two decimals and are stored as integer paise.

## Tests

```bash
make
bash tests/e2e.sh          # customer flows, one session per user, concurrent transfers
bash tests/e2e_roles.sh    # all four roles, loan workflow, account activation (38 checks)
bash tests/stress.sh       # mixed concurrent load, money conservation, login race
```


Each script starts its own server on a private port with a throwaway data
directory in /tmp.
