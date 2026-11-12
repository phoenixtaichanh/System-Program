# ============================================================================
# File System Explorer & Metadata Analyzer
#
# Primary target platform : Ubuntu Linux + GTK 3
# Secondary target platform: Windows (MSYS2 / MinGW-w64) + GTK 3
#
# Quick start (Ubuntu):
#     sudo apt install -y build-essential pkg-config libgtk-3-dev
#     make
#     make run
#
# Quick start (MSYS2 UCRT64):
#     pacman -S --needed mingw-w64-ucrt-x86_64-gtk3 mingw-w64-ucrt-x86_64-pkgconf
#     make
#     make run
# ============================================================================

# ---- Toolchain --------------------------------------------------------------
CC        ?= gcc
PKG_CONFIG ?= pkg-config

# ---- GTK detection ---------------------------------------------------------
GTK_PACKAGE := gtk+-3.0
GTK_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(GTK_PACKAGE))
GTK_LIBS    := $(shell $(PKG_CONFIG) --libs   $(GTK_PACKAGE))

# ---- GLib detection --------------------------------------------------------
# AppState is a GObject, so both the core library and the tests need GLib.
GLIB_PACKAGE := glib-2.0 gobject-2.0
GLIB_CFLAGS  := $(shell $(PKG_CONFIG) --cflags $(GLIB_PACKAGE))
GLIB_LIBS    := $(shell $(PKG_CONFIG) --libs   $(GLIB_PACKAGE))

# ---- Platform detection ----------------------------------------------------
# FS_PLATFORM_WINDOWS is defined by fs_platform.h automatically; here we only
# need it to pick the right set of feature-test macros for the compiler.
ifeq ($(OS),Windows_NT)
  PLATFORM_CPPFLAGS := -D_WIN32_WINNT=0x0601
  # The MinGW CRT provides its own POSIX-ish shims; we must not ask glibc for
  # _POSIX_C_SOURCE-only symbols that do not exist there.
  PLATFORM_CPPFLAGS += -D__USE_MINGW_ANSI_STDIO=1
else
  PLATFORM_CPPFLAGS :=
endif

# ---- Compile / link flags --------------------------------------------------
CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
           -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes \
           -Wmissing-prototypes -Wold-style-definition \
           -Iinclude \
           $(GTK_CFLAGS) \
           -O2 -g
CPPFLAGS := $(PLATFORM_CPPFLAGS)
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

# The GUI and the application entry point are not part of the core library:
# they pull in GTK and define main(), so tests must not link them.
APP_OBJS  := $(BUILD_DIR)/main.o
GUI_OBJS  := $(filter $(BUILD_DIR)/gui/%,$(OBJS))
CORE_OBJS := $(filter-out $(APP_OBJS) $(GUI_OBJS),$(OBJS))

# ---- Tests -----------------------------------------------------------------
# Each tests/test_<name>.c produces its own binary build/test_<name>.
#
# A test whose name starts with "test_gui_" needs GTK: it exercises the
# widgets directly. Every other test only touches the platform-independent
# core, so it is compiled WITHOUT the GTK flags and links without GTK.
TEST_SRC_DIR := tests
TEST_SRCS := $(shell find $(TEST_SRC_DIR) -type f -name '*.c')
TEST_OBJS := $(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/%,$(TEST_SRCS))

# The GUI tests.
GUI_TEST_SRCS := $(filter $(TEST_SRC_DIR)/test_gui_%.c,$(TEST_SRCS))
GUI_TEST_BINS := $(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/%,$(GUI_TEST_SRCS))
# The core tests.
CORE_TEST_SRCS := $(filter-out $(GUI_TEST_SRCS),$(TEST_SRCS))
CORE_TEST_BINS := $(patsubst $(TEST_SRC_DIR)/%.c,$(BUILD_DIR)/%,$(CORE_TEST_SRCS))

# Core tests are compiled WITHOUT the GTK flags: they only exercise the
# platform-independent core. GLib is still needed because AppState is a
# GObject.
TEST_CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L -D_XOPEN_SOURCE=700 \
                -Wall -Wextra -Wpedantic \
                -Iinclude \
                $(GLIB_CFLAGS) \
                -O0 -g

# The GUI tests need the full GTK include and link flags.
GUI_TEST_CFLAGS := $(TEST_CFLAGS) $(GTK_CFLAGS)
GUI_TEST_LDLIBS := $(GLIB_LIBS) $(GTK_LIBS)

# The core objects a test binary needs: everything except the GUI and main().
CORE_OBJS := $(filter-out $(APP_OBJS) $(GUI_OBJS),$(OBJS))

# The tests link against GLib because AppState is a GObject.
TEST_LDLIBS  := $(GLIB_LIBS)

# ---- Targets ---------------------------------------------------------------
.PHONY: all run clean debug valgrind help test test-gui test_utils cli asan

all: $(BIN)

# Link step
$(BIN): $(OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# Compile step with header dependency tracking.
# The .o files are marked "precious" so that make does not delete them as
# intermediate files (they are shared between the app and the test links).
.PRECIOUS: $(OBJS) $(TEST_OBJS)

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(CFLAGS) -MMD -MP -c $< -o $@

# ---- Test compilation ------------------------------------------------------
$(BUILD_DIR)/tests/%.o: $(TEST_SRC_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(TEST_CFLAGS) -c $< -o $@

# The GUI tests are compiled with the GTK flags.
$(BUILD_DIR)/tests/test_gui_%.o: $(TEST_SRC_DIR)/test_gui_%.c
	@mkdir -p $(dir $@)
	$(CC) $(CPPFLAGS) $(GUI_TEST_CFLAGS) -c $< -o $@

# Generic rule: every core test binary links the whole non-GUI core.
$(BUILD_DIR)/test_%: $(BUILD_DIR)/tests/test_%.o $(CORE_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(TEST_LDLIBS)

# The GUI test binaries additionally link the GUI objects and GTK.
$(BUILD_DIR)/test_gui_%: $(BUILD_DIR)/tests/test_gui_%.o $(CORE_OBJS) $(GUI_OBJS)
	@mkdir -p $(dir $@)
	$(CC) $(LDFLAGS) -o $@ $^ $(GUI_TEST_LDLIBS)

test: $(TEST_BINS)
	@set -e; rc=0; for t in $(TEST_BINS); do \
	    echo "---- running $$t ----"; \
	    ./$$t || rc=$$?; \
	done; \
	exit $$rc

# The GUI tests need a display. When none is available they skip
# themselves, so `make test` works on a headless machine too. When Xvfb is
# installed we start a virtual server so the GUI tests really run.
test-gui: $(GUI_TEST_BINS)
	@rc=0; \
	if [ -z "$$DISPLAY" ] && command -v Xvfb >/dev/null 2>&1; then \
	    echo "---- starting Xvfb for the GUI tests ----"; \
	    Xvfb :99 -screen 0 1024x768x24 >/dev/null 2>&1 & \
	    XVFB_PID=$$!; \
	    sleep 2; \
	    DISPLAY=:99; export DISPLAY; \
	fi; \
	for t in $(GUI_TEST_BINS); do \
	    echo "---- running $$t ----"; \
	    ./$$t || rc=$$?; \
	done; \
	[ -n "$$XVFB_PID" ] && kill $$XVFB_PID 2>/dev/null; \
	exit $$rc

# ---- Convenience -----------------------------------------------------------
cli: $(BIN)
	./$(BIN) --cli

run: $(BIN)
	./$(BIN)

debug: CFLAGS += -O0 -g3 -DDEBUG
debug: clean all

# ---- AddressSanitizer / UndefinedBehaviorSanitizer build --------------------
# Rebuilds everything with the sanitizers enabled and runs the whole test
# suite. Requires the ASan/UBSan runtime (part of gcc on Ubuntu).
SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer \
             -fno-sanitize-recover=all

asan: clean
	@$(MAKE) --no-print-directory CFLAGS="$(CFLAGS) -O1 -g $(SAN_FLAGS)" \
	         LDFLAGS="$(SAN_FLAGS)" all
	@$(MAKE) --no-print-directory CFLAGS="$(TEST_CFLAGS) -O1 -g $(SAN_FLAGS)" \
	         LDFLAGS="$(SAN_FLAGS)" test
	@echo "### ASAN/UBSAN: all tests passed"

valgrind: $(BIN)
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --error-exitcode=1 \
	         ./$(BIN) --exec "help" || true
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --error-exitcode=1 \
	         ./$(BIN) --exec "ls /tmp" || true
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --error-exitcode=1 \
	         ./$(BIN) --exec "stat /tmp" || true
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --error-exitcode=1 \
	         ./$(BIN) --exec "find /tmp *.txt" || true
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --error-exitcode=1 \
	         ./$(BIN) --exec "du /tmp" || true
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --error-exitcode=1 \
	         ./$(BIN) --exec "df" || true

clean:
	rm -rf $(BUILD_DIR)

help:
	@echo "Targets:"
	@echo "  make / all     - build the application"
	@echo "  make run       - build and launch the GUI"
	@echo "  make cli       - build and run the CLI shell"
	@echo "  make test      - build & run all unit tests"
	@echo "  make test-gui  - run the GUI tests (uses Xvfb when available)"
	@echo "  make debug     - clean rebuild with -O0 -g3 -DDEBUG"
	@echo "  make asan      - rebuild with ASan/UBSan and run the tests"
	@echo "  make valgrind  - run the CLI under valgrind"
	@echo "  make clean     - remove build artifacts"

# ---- Auto-generated header dependencies -------------------------------------
-include $(DEPS)
