CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra
LDFLAGS ?=

PREFIX  ?= /usr/local
BINDIR  ?= $(PREFIX)/bin

BIN := wlproxy

.PHONY: all static install clean

all: $(BIN)

$(BIN): src/wlproxy.c
	$(CC) $(CFLAGS) -pthread -o $@ $< $(LDFLAGS)

# Fully static build: safe to run inside the Flatpak sandbox.
static:
	$(CC) $(CFLAGS) -pthread -static -o $(BIN) src/wlproxy.c $(LDFLAGS)

install: $(BIN)
	install -d "$(DESTDIR)$(BINDIR)"
	install -m 0755 $(BIN) "$(DESTDIR)$(BINDIR)/$(BIN)"

clean:
	rm -f $(BIN)
