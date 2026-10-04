# ============================================================================
# File System Explorer & Metadata Analyzer
# Makefile for Ubuntu Linux + GTK 3
# ============================================================================

# ---- Toolchain --------------------------------------------------------------
CC        ?= gcc
PKG_CONFIG ?= pkg-config

# ---- GTK detection ---------------------------------------------------------
GTK_PACKAGE := gtk+-3.0
GTK_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(GTK_PACKAGE))
GTK_LIBS    := $(shell $(PKG_CONFIG) --libs   $(GTK_PACKAGE))

# ---- Compile / link flags --------------------------------------------------
CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L \
           -Wall -Wextra -Wpedantic \
           -Iinclude \
           $(GTK_CFLAGS) \
           -O2 -g
LDFLAGS :=
LDLIBS  := $(GTK_LIBS)

# ---- Directories -----------------------------------------------------------
SRC_DIR    := src
BUILD_DIR  := build
BIN        := $(BUILD_DIR)/fs_explorer
TARGET     := fs_explorer

# ---- Tests -----------------------------------------------------------------
TEST_SRC_DIR := tests
TEST_BIN     := $(BUILD_DIR)/test_utils

# ---- Sources ----------------------------------------------------------------
# Auto-discover every .c file under src/
SRCS := $(shell find $(SRC_DIR) -type f -name '*.c')
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# Test sources (compiled WITHOUT gtk flags)
TEST_SRCS := $(shell find $(TEST_SRC_DIR) -type f -name '*.c')
TEST_OBJS := $(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))

# ---- Targets ---------------------------------------------------------------
.PHONY: all run clean debug valgrind help test test_utils

all: $(BIN)

# Link step
$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Compile step with header dependency tracking
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# ---- Test compilation ------------------------------------------------------
# Tests use the same strict flags but do NOT link against GTK. They include
# only the public headers in include/.
TEST_CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
                -Wall -Wextra -Wpedantic \
                -Iinclude \
                -O0 -g
$(BUILD_DIR)/tests/%.o: $(TEST_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(TEST_CFLAGS) -c $< -o $@

$(TEST_BIN): $(TEST_OBJS) $(filter $(BUILD_DIR)/utils/%,$(OBJS))
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^

test_utils: $(TEST_BIN)
	./$(TEST_BIN)

test: test_utils

run: $(BIN)
	./$(BIN)

debug: CFLAGS += -O0 -g3 -DDEBUG
debug: clean all

valgrind: $(BIN)
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --error-exitcode=1 \
	         ./$(BIN) --cli

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make / all     - build the application"
	@echo "  make run       - build and launch the GUI"
	@echo "  make debug     - clean rebuild with -O0 -g3 -DDEBUG"
	@echo "  make valgrind  - run engine under valgrind (requires --cli mode in code)"
	@echo "  make test      - build & run unit tests for the utils layer"
	@echo "  make test_utils- same as 'make test'"
	@echo "  make clean     - remove build artifacts"

# ---- Auto-generated header dependencies -------------------------------------
-include $(DEPS)