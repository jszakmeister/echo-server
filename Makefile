# Use make's built-in rules
# CFLAGS=-O2 -g -Wall -Wextra
# echo-server: echo-server.c

ifeq ($(ENABLE_ALARM),1)
    OTHER_FLAGS := $(OTHER_FLAGS) -DENABLE_ALARM
endif

ifeq ($(ENABLE_FORKING),1)
    OTHER_FLAGS := $(OTHER_FLAGS) -DENABLE_FORKING
endif

ifeq ($(ENABLE_THREADING),1)
    OTHER_FLAGS := $(OTHER_FLAGS) -DENABLE_THREADING
endif

ifeq ($(SHOW_BUG),1)
    OTHER_FLAGS := $(OTHER_FLAGS) -DSHOW_BUG
endif

ifeq ($(ENABLE_DAEMON),1)
    OTHER_FLAGS := $(OTHER_FLAGS) -DENABLE_DAEMON
endif

ifeq ($(ENABLE_PRIV),1)
    OTHER_FLAGS := $(OTHER_FLAGS) -DENABLE_PRIV
endif


UNAME=$(shell uname)
ifeq ($(UNAME),Linux)
    LIBS := -lpthread
endif

.PHONY: all

all: echo-server

echo-server: echo-server.c
	gcc -O2 -g -Wall -Wextra $(OTHER_FLAGS) -o $@ $^ $(LIBS)

.PHONY: clean

clean:
	rm -f echo-server
