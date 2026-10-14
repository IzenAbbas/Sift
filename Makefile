# Makefile for the Sift compiler.
#
#   make          build ./sift
#   make test     build, then run every test in tests/
#   make clean    remove everything the build produced

CC       = gcc
CFLAGS  ?= -std=c11 -Wall -Wextra -O2 -g
CPPFLAGS = -D_POSIX_C_SOURCE=200809L -Isrc -Ibuild
FLEX    ?= flex
BISON   ?= bison

BUILD = build

# Hand-written C files (in src/) and the two generated ones (in build/).
SRCS = main.c util.c ast.c sema.c interp.c
OBJS = $(addprefix $(BUILD)/,$(SRCS:.c=.o)) $(BUILD)/parser.o $(BUILD)/lexer.o

.PHONY: all test clean

all: sift

sift: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BUILD):
	mkdir -p $(BUILD)

# Bison: -d writes parser.h too. Any shift/reduce or reduce/reduce
# conflict is turned into an error so the build fails.
$(BUILD)/parser.c $(BUILD)/parser.h: src/parser.y | $(BUILD)
	$(BISON) -Wall -Werror=conflicts-sr -Werror=conflicts-rr -d -o $(BUILD)/parser.c src/parser.y

$(BUILD)/lexer.c: src/lexer.l $(BUILD)/parser.h | $(BUILD)
	$(FLEX) -o $@ src/lexer.l

$(BUILD)/%.o: $(BUILD)/%.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

$(BUILD)/%.o: src/%.c $(BUILD)/parser.h $(wildcard src/*.h) | $(BUILD)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c -o $@ $<

test: all
	./run_tests.sh

clean:
	rm -rf $(BUILD) sift
