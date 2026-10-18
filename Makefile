CC = gcc
CFLAGS = -Wall -Wextra -Iinclude -g

all: server

server: src/server/main.c
	$(CC) $(CFLAGS) -o server src/server/main.c

clean:
	rm -f server
