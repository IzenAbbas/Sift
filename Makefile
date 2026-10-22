# Makefile for the Sift compiler.
#
#   make          build ./sift
#   make test     build, then run every test in tests/
#   make clean    remove everything the build produced

CC       = gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -Isrc -Ibuild -pthread
FLEX    ?= flex
BISON   ?= bison

# LLVM tools used by `./sift compile` (and so by `make test`). Override
# them like this:  make test LLC=llc-18 CLANG=clang-18 OPT=opt-18
LLC     ?= llc
CLANG   ?= clang
OPT     ?= opt
export LLC CLANG OPT

BUILD = build

# Hand-written C files (in src/) and the two generated ones (in build/).
SRCS = main.c util.c ast.c sema.c interp.c codegen.c
OBJS = $(addprefix $(BUILD)/,$(SRCS:.c=.o)) $(BUILD)/parser.o $(BUILD)/lexer.o

.PHONY: all test clean

# sift_runtime.o is not part of ./sift itself: `./sift compile` links it
# into every compiled Sift program.
all: sift $(BUILD)/sift_runtime.o

# -pthread: the interpreter runs on a thread with a big stack (interp.c)
sift: $(OBJS)
	$(CC) $(CFLAGS) -pthread -o $@ $(OBJS)

$(BUILD):
	mkdir -p $(BUILD)

# Bison: -d writes parser.h too. Any shift/reduce or reduce/reduce
# conflict is turned into an error so the build fails.
$(BUILD)/parser.c $(BUILD)/parser.h: src/parser.y | $(BUILD)
	$(BISON) -Wall -Werror=conflicts-sr -Werror=conflicts-rr -d -o $(BUILD)/parser.c src/parser.y

$(BUILD)/lexer.c: src/lexer.l $(BUILD)/parser.h | $(BUILD)
	$(FLEX) -o $@ src/lexer.l

$(BUILD)/sift_runtime.o: src/runtime.c | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: $(BUILD)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.c $(BUILD)/parser.h $(wildcard src/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: all
	./run_tests.sh

clean:
	rm -rf $(BUILD) sift
