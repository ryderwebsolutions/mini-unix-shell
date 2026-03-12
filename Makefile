# Compiler and flags
CC      := gcc
CFLAGS  := -std=c11 -Wall -Wextra -Wpedantic -g

# Target executable
TARGET  := mysh

# Source files (all .c in this directory)
SRCS    := shell.c parser.c executor.c builtins.c utils.c

# Derive object files from sources
OBJS    := $(SRCS:.c=.o)

# ── Default target ──────────────────────────────────────────────────────────
.PHONY: all
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^

# ── Compile each .c to .o  ──────────────────────────────────────────────────
# Make every .o depend on all headers so a header change triggers a rebuild.
HEADERS := parser.h executor.h builtins.h utils.h

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ── Convenience targets ─────────────────────────────────────────────────────
.PHONY: run
run: all
	./$(TARGET)

.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: rebuild
rebuild: clean all
