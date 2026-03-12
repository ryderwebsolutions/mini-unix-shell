# =============================================================================
# Makefile — mini-unix-shell
#
# Targets:
#   all       (default) build the release binary
#   run       build and launch an interactive session
#   debug     build with AddressSanitizer + UBSan for development
#   valgrind  run under valgrind --leak-check=full
#   clean     remove object files and binaries
#   rebuild   clean + all
#   install   copy binary to $(PREFIX)/bin  (default: /usr/local/bin)
#   uninstall remove installed binary
# =============================================================================

CC      := gcc
CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wpedantic -g

TARGET  := mysh
PREFIX  := /usr/local

SRCS    := shell.c parser.c executor.c builtins.c utils.c
OBJS    := $(SRCS:.c=.o)
HEADERS := parser.h executor.h builtins.h utils.h

# ── Default: release build ───────────────────────────────────────────────────
.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# Every .o depends on all headers — a header change triggers a full rebuild.
%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Debug build (AddressSanitizer + UndefinedBehaviourSanitizer) ─────────────
.PHONY: debug
debug: CFLAGS += -O0 -fsanitize=address,undefined -fno-omit-frame-pointer
debug: clean $(TARGET)

# ── Convenience ──────────────────────────────────────────────────────────────
.PHONY: run
run: all
	./$(TARGET)

.PHONY: valgrind
valgrind: all
	valgrind --leak-check=full --track-origins=yes --error-exitcode=1 ./$(TARGET)

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET) $(TARGET)-debug

.PHONY: rebuild
rebuild: clean all

# ── Install / uninstall ──────────────────────────────────────────────────────
.PHONY: install
install: all
	install -m 0755 $(TARGET) $(PREFIX)/bin/$(TARGET)
	@echo "Installed to $(PREFIX)/bin/$(TARGET)"

.PHONY: uninstall
uninstall:
	rm -f $(PREFIX)/bin/$(TARGET)
	@echo "Removed $(PREFIX)/bin/$(TARGET)"
