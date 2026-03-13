# ──────────────────────────────────────────────────────────────
# Q+ Compiler (qpc) — Makefile
# ──────────────────────────────────────────────────────────────

CC       = gcc
CFLAGS   = -Wall -Wextra -Wno-unused-parameter -std=c11 -Iinclude
LDFLAGS  =

# Debug/Release
ifdef RELEASE
  CFLAGS += -O2 -DNDEBUG
else
  CFLAGS += -g -O0
endif

# Directories
SRC_DIR   = src
INC_DIR   = include
TEST_DIR  = tests
BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj

# Source files (compiler)
SRCS = $(SRC_DIR)/main.c \
       $(SRC_DIR)/lexer.c \
       $(SRC_DIR)/source.c \
       $(SRC_DIR)/diagnostic.c

# Object files
OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Test source files
TEST_SRCS = $(TEST_DIR)/test_lexer.c \
            $(SRC_DIR)/lexer.c \
            $(SRC_DIR)/source.c \
            $(SRC_DIR)/diagnostic.c

TEST_OBJS = $(OBJ_DIR)/test_lexer.o \
            $(OBJ_DIR)/lexer.o \
            $(OBJ_DIR)/source.o \
            $(OBJ_DIR)/diagnostic.o

# Output
TARGET      = $(BUILD_DIR)/qpc.exe
TEST_TARGET = $(BUILD_DIR)/test_lexer.exe

# ── Rules ────────────────────────────────────────────────────

.PHONY: all clean test run_test

all: $(TARGET)
	@echo "Build complete: $(TARGET)"

$(TARGET): $(OBJS)
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@if not exist "$(OBJ_DIR)" mkdir "$(OBJ_DIR)"
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/test_lexer.o: $(TEST_DIR)/test_lexer.c
	@if not exist "$(OBJ_DIR)" mkdir "$(OBJ_DIR)"
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_TARGET)
	@echo.
	@echo Running lexer tests...
	@echo.
	@$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS)
	@if not exist "$(BUILD_DIR)" mkdir "$(BUILD_DIR)"
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	@if exist "$(BUILD_DIR)" rmdir /s /q "$(BUILD_DIR)"
	@echo Clean complete.

# ── Convenience targets ─────────────────────────────────────

# Lex a sample file
lex_hello: $(TARGET)
	$(TARGET) lex tests/samples/hello.qp

lex_driver: $(TARGET)
	$(TARGET) lex tests/samples/driver.qp
