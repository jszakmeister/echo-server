# Use make's built-in rules
# CFLAGS=-O2 -g -Wall -Wextra
# echo-server: echo-server.c

.PHONY: all

all: echo-server

echo-server: echo-server.c
	gcc -O2 -g -Wall -Wextra $(OTHER_FLAGS) -o $@ $^

.PHONY: clean

clean:
	rm -f echo-server
