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
# Each test_<name>.c produces its own binary build/test_<name>.
TEST_SRCS := $(shell find $(TEST_SRC_DIR) -type f -name '*.c')
TEST_OBJS := $(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/%,$(TEST_SRCS))

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

# Each test binary links the corresponding test object plus all object files
# from src/utils/. (Phase-3 tests also need src/filesystem/, added below.)
TEST_UTILS_OBJS := $(filter $(BUILD_DIR)/utils/%,$(OBJS))
TEST_FS_OBJS    := $(filter $(BUILD_DIR)/filesystem/%,$(OBJS))

# Default: tests that only need utils.
$(BUILD_DIR)/test_utils: $(BUILD_DIR)/tests/test_utils.o $(TEST_UTILS_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^

# Phase 3+ tests need the filesystem module.
$(BUILD_DIR)/test_fs_listing: $(BUILD_DIR)/tests/test_fs_listing.o $(TEST_UTILS_OBJS) $(TEST_FS_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^

# Generic rule for any future test: link with all non-GUI sources.
$(BUILD_DIR)/test_%: $(BUILD_DIR)/tests/test_%.o $(TEST_UTILS_OBJS) $(TEST_FS_OBJS)
	@mkdir -p $(dir $@)
	$(CC) -o $@ $^

test: $(TEST_BINS)
	@set -e; rc=0; for t in $(TEST_BINS); do \
	    echo "---- running $$t ----"; \
	    ./$$t || rc=$$?; \
	done; \
	exit $$rc

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
	@echo "  make test      - build & run all unit tests (one binary per tests/*.c)"
	@echo "  make clean     - remove build artifacts"

# ---- Auto-generated header dependencies -------------------------------------
-include $(DEPS)