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

# ---- Sources ----------------------------------------------------------------
# Auto-discover every .c file under src/
SRCS := $(shell find $(SRC_DIR) -type f -name '*.c')
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))
DEPS := $(OBJS:.o=.d)

# ---- Targets ---------------------------------------------------------------
.PHONY: all run clean debug valgrind help

all: $(BIN)

# Link step
$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Compile step with header dependency tracking
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

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
	@echo "  make clean     - remove build artifacts"

# ---- Auto-generated header dependencies -------------------------------------
-include $(DEPS)