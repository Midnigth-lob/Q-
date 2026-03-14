# ──────────────────────────────────────────────────────────────
# Q+ Compiler (qpc) — Makefile (Linux/Kali)
# ──────────────────────────────────────────────────────────────

CC       = gcc
CFLAGS   = -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -std=c11 -Iinclude
LDFLAGS  =

ifdef RELEASE
  CFLAGS += -O2 -DNDEBUG
else
  CFLAGS += -g -O0
endif

SRC_DIR   = src
INC_DIR   = include
TEST_DIR  = tests
BUILD_DIR = build
OBJ_DIR   = $(BUILD_DIR)/obj

# Source files
SRCS = $(SRC_DIR)/main.c       \
       $(SRC_DIR)/lexer.c      \
       $(SRC_DIR)/source.c     \
       $(SRC_DIR)/diagnostic.c \
       $(SRC_DIR)/parser.c     \
       $(SRC_DIR)/sema.c       \
       $(SRC_DIR)/codegen.c    \
       $(SRC_DIR)/security.c

OBJS = $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SRCS))

# Test files
TEST_OBJS = $(OBJ_DIR)/test_lexer.o \
            $(OBJ_DIR)/lexer.o      \
            $(OBJ_DIR)/source.o     \
            $(OBJ_DIR)/diagnostic.o

# Linux binaries (no .exe)
TARGET      = $(BUILD_DIR)/qpc
TEST_TARGET = $(BUILD_DIR)/test_lexer

.PHONY: all clean test lex_hello lex_driver parse_hello parse_driver build_hello build_driver

all: $(TARGET)
	@echo "Build complete: $(TARGET)"

$(TARGET): $(OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

$(OBJ_DIR)/test_lexer.o: $(TEST_DIR)/test_lexer.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c -o $@ $<

test: $(TEST_TARGET)
	@echo ""
	@echo "Running lexer tests..."
	@echo ""
	@$(TEST_TARGET)

$(TEST_TARGET): $(TEST_OBJS)
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
	rm -rf $(BUILD_DIR)
	@echo "Clean complete."

# ── Convenience targets ────────────────────────────────────────
lex_hello: $(TARGET)
	$(TARGET) lex tests/samples/hello.qp

lex_driver: $(TARGET)
	$(TARGET) lex tests/samples/driver.qp

parse_hello: $(TARGET)
	$(TARGET) parse tests/samples/hello.qp

parse_driver: $(TARGET)
	$(TARGET) parse tests/samples/driver.qp

build_hello: $(TARGET)
	$(TARGET) build tests/samples/hello.qp

build_driver: $(TARGET)
	$(TARGET) build tests/samples/driver.qp

build_syscall: $(TARGET)
	$(TARGET) build tests/samples/syscall.qp
