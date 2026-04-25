CC ?= cc
BUILD_DIR := build
BIN := $(BUILD_DIR)/rcc

LLVM_CFLAGS := $(shell llvm-config --cflags 2>/dev/null)
LLVM_LDFLAGS := $(shell llvm-config --ldflags --system-libs --libs core analysis 2>/dev/null)

CFLAGS ?= -std=c11 -Wall -Wextra -Werror -pedantic -g
CPPFLAGS := -Isrc

SRC := \
	src/main.c \
	src/lexer.c \
	src/parser.c \
	src/ast.c \
	src/semantic.c \
	src/codegen.c

OBJ := $(patsubst src/%.c,$(BUILD_DIR)/%.o,$(SRC))

.PHONY: all clean test

all: $(BIN)

$(BIN): $(OBJ) | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(OBJ) -o $@ $(LLVM_LDFLAGS)

$(BUILD_DIR)/%.o: src/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(LLVM_CFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

test: $(BIN)
	tests/run-tests.sh

clean:
	rm -rf $(BUILD_DIR)
