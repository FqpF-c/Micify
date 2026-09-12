CC ?= gcc
CFLAGS ?= -O2 -Wall -Wextra -g
LDFLAGS ?=

PKG_CONFIG_LIBS := libpipewire-0.3 opus

CFLAGS += $(shell pkg-config --cflags $(PKG_CONFIG_LIBS))
LDLIBS := $(shell pkg-config --libs $(PKG_CONFIG_LIBS)) -lpthread -lm

# Daemon (headless engine): everything in src/ except the Windows backend
# (source-only here, no Windows toolchain on this machine) and the GUI's
# own main() (gui_main.c), which is built into a separate binary below.
DAEMON_SRC := $(wildcard src/*.c)
DAEMON_SRC := $(filter-out src/backend_windows_wasapi.c src/gui_main.c, $(DAEMON_SRC))
DAEMON_OBJ := $(DAEMON_SRC:.c=.o)
DAEMON_BIN := micify-daemon

# GUI front-end: a thin GTK3 wrapper that launches micify-daemon as a
# subprocess (see src/gui_main.c) - the app end users double-click.
GUI_CFLAGS := $(shell pkg-config --cflags gtk+-3.0)
GUI_LDLIBS := $(shell pkg-config --libs gtk+-3.0)
GUI_BIN := micify

.PHONY: all clean run gui

all: $(DAEMON_BIN) $(GUI_BIN)

$(DAEMON_BIN): $(DAEMON_OBJ)
	$(CC) $(DAEMON_OBJ) -o $@ $(LDLIBS)

src/gui_main.o: src/gui_main.c
	$(CC) $(CFLAGS) $(GUI_CFLAGS) -c $< -o $@

$(GUI_BIN): src/gui_main.o
	$(CC) src/gui_main.o -o $@ $(GUI_LDLIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: $(DAEMON_BIN)
	./$(DAEMON_BIN)

gui: $(GUI_BIN)
	./$(GUI_BIN)

clean:
	rm -f $(DAEMON_OBJ) $(DAEMON_BIN) src/gui_main.o $(GUI_BIN)
