.RECIPEPREFIX = >
CC     = gcc
CFLAGS = -Wall -Wextra -Iinclude -g
COMMON = src/common/db.c src/common/store.c src/common/bank_ops.c src/common/services.c src/common/net.c src/common/journal.c

all: bin/server bin/client bin/seed

bin/server: src/server/main.c src/server/session.c $(wildcard src/server/cmd_*.c) $(COMMON)
> mkdir -p bin
> $(CC) $(CFLAGS) $^ -o $@

bin/client: src/client/main.c src/common/net.c
> mkdir -p bin
> $(CC) $(CFLAGS) $^ -o $@

bin/seed: src/server/seed.c $(COMMON)
> mkdir -p bin
> $(CC) $(CFLAGS) $^ -o $@

clean:
> rm -rf bin

.PHONY: all clean
