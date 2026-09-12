CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -g
LDFLAGS ?=

PKG_CONFIG_LIBS := libpipewire-0.3 opus

CFLAGS += $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))
LDLIBS := $(shell pkg-config --libs $(PKG_CONFIG_LIBS)) -lpthread -lm

SRC := $(wildcard src/*.c)
# Windows backend is source-only here (not built on Linux): excluded from
# this Makefile's object list until compiled on a Windows toolchain.
SRC := $(filter-out src/backend_windows_wasapi.c, $(SRC))
OBJ := $(SRC:.c=.o)
BIN := micify-daemon

.PHONY: all clean run

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(BIN)
	./$(BIN)

clean:
	rm -f $(OBJ) $(BIN)
