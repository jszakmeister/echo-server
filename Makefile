# Use make's built-in rules
# CFLAGS=-O2 -g -Wall -Wextra
# echo-server: echo-server.c

ifeq ($(ENABLE_ALARM),1)
    OTHER_FLAGS := $(OTHER_FLAGS) -DENABLE_ALARM
endif

.PHONY: all

all: echo-server

echo-server: echo-server.c
	gcc -O2 -g -Wall -Wextra $(OTHER_FLAGS) -o $@ $^

.PHONY: clean

clean:
	rm -f echo-server
