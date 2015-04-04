# Install
PREFIX=/usr/local
#BIN=netbwmon

# Compiler
CC = gcc
DCC = clang

# Flags

DFLAGS = -g -Wall -Wextra -Werror -Wformat -Wunreachable-code
DFLAGS += -fstack-protector-strong -Winline -Wshadow -Wwrite-strings -fstrict-aliasing
DFLAGS += -Wstrict-prototypes -Wold-style-definition -Wconversion
DFLAGS += -Wredundant-decls -Wnested-externs -Wmissing-include-dirs
DFLAGS += -Wshadow -Wpointer-arith -Wcast-qual -Wcast-align -Wmissing-prototypes -Wconversion
DFLAGS += -Wswitch-default -Wundef -Wno-unused -Wstrict-overflow=5 -Wsign-conversion
DFLAGS += -Winit-self -Wstrict-aliasing -fsanitize=address -fno-omit-frame-pointer
CFLAGS = -O3

# Modes
.PHONY: release
release: netbwmon

.PHONY: debug
debug: CFLAGS = $(DFLAGS)
debug: CC = $(DCC)
debug: netbwmon

# Objects
SRC = netbwmon.c
OBJ = $(SRC:.c=.o)

netbwmon: $(SRC)
	$(CC) $^ $(CFLAGS) -o netbwmon

install: netbwmon
	install netbwmon $(PREFIX)/bin

uninstall:
	rm $(PREFIX)/bin/netbwmon

clean:
	rm -f netbwmon $(OBJ)

all:
	release

.PHONY: clean install uninstall all
