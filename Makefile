CC = gcc
CFLAGS = -Wall -Wextra -std=c99 -pthread
LDFLAGS = -pthread

TARGETS = server client operations

all: $(TARGETS)

server: server.c common.h
	$(CC) $(CFLAGS) -o $@ server.c

client: client.c common.h
	$(CC) $(CFLAGS) -o $@ client.c

operations: operations.c common.h
	$(CC) $(CFLAGS) -o $@ operations.c

clean:
	rm -f $(TARGETS) *.o

.PHONY: all clean


