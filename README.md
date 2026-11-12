# File System Explorer & Metadata Analyzer

A lightweight file explorer with an integrated file system **metadata
analyzer**. Written in **C11** with **GTK 3** and the **POSIX APIs**, and
portable to **Windows (MSYS2 / MinGW-w64)** through a thin platform layer.

The application ships with **two front-ends** over one shared core library:

* a **GTK 3 graphical explorer** (tree pane, detailed file list, metadata
  panel, recursive analysis, wildcard search), and
* an **interactive command-line shell** with the same capabilities.

---

## Table of contents

1. [Features](#features)
2. [Quick start on Ubuntu (inside a VMware VM)](#quick-start-on-ubuntu-inside-a-vmware-vm)
3. [Full installation guide for a fresh Ubuntu VM](#full-installation-guide-for-a-fresh-ubuntu-vm)
4. [Automated host ↔ guest workflow](#automated-host--guest-workflow)
5. [Running the application](#running-the-application)
6. [CLI reference](#cli-reference)
7. [GUI reference](#gui-reference)
8. [Architecture](#architecture)
9. [Build targets](#build-targets)
10. [Project layout](#project-layout)
11. [Testing](#testing)
12. [Memory-safety tooling](#memory-safety-tooling)
13. [Building on Windows (MSYS2)](#building-on-windows-msys2)
14. [Design notes](#design-notes)

---

## Features

### Navigation

* Directory tree pane with **lazy loading** (children are only read when a
  node is expanded).
* Detailed file list showing name, type, size, modification time, permission
  string and owner.
* Back / Forward / Parent / Home / Refresh navigation with a **history stack**.
* Free-text path entry with a *Go* button, so absolute paths can be typed
  directly.

### Metadata analysis

* Full POSIX metadata for the selected entry: type, size, mode, owner and
  group, `atime` / `mtime` / `ctime`, inode, hard-link count, allocated
  blocks, symlink target and the **current user's effective access**
  (`rwx` for the owning / group / other class).
* Recursive **folder analysis**: file and directory counts, total and average
  size, the N largest files, and a per-sub-directory size breakdown.
* File system capacity information (`statvfs`): total / used / free / available
  space, block size and inode usage.
* Symbolic-link inspection: target resolution, broken-link detection.

### Safe operations

All mutating commands go through the same validated layer:

* `mkdir` (with optional `-p`), `touch`, `mv`, `rm`, `rmdir`, `ln -s`.
* Destructive operations refuse to act on non-empty directories and never
  follow symlinks out of the tree by accident.
* Every operation reports a typed status code (`FSStatus`) rather than a bare
  `errno`, so the GUI can show precise, human-readable error messages.

### Search

* Recursive name search with `*` and `?` wildcards, a literal-match mode, a
  maximum depth and a maximum result count.
* Symlinks are **not** followed during traversal, and visited inodes are
  tracked so directory loops cannot cause an infinite recursion.

### Interface

* Standardised GTK modal dialogs for errors, confirmations and information.
* An **activity log** pane that records every operation.
* A status bar with contextual hints.

---

## Quick start on Ubuntu (inside a VMware VM)

If the guest already has a compiler and GTK 3, this is all you need:

```bash
sudo apt update
sudo apt install -y build-essential pkg-config libgtk-3-dev

make                 # -> build/fs_explorer
make test            # run the unit tests
make run             # launch the GTK application
```

For a **brand-new virtual machine** follow the complete guide below.

---

## Full installation guide for a fresh Ubuntu VM

This chapter walks through everything needed on a machine that has never run
the project before. It covers the host side (Windows + VMware Workstation)
and the guest side (Ubuntu 24.04 / 26.04 LTS).

### Step 1 — Prepare the virtual machine on the host

1. Install **VMware Workstation Pro** (or VMware Player) on the host.
   *VMware Tools* are required, because the automation in
   [Step 6](#step-6--optional-automate-everything-with-vmrun) uses the
   guest-operations API of `vmrun`.

2. Create a new virtual machine:

   | Setting | Recommended value |
   | ------- | ----------------- |
   | Guest OS | Ubuntu 64-bit |
   | RAM | 4 GB |
   | Disk | 40 GB, "store as a single file" |
   | Network | NAT |
   | Firmware | UEFI (the default) |

3. Boot the Ubuntu ISO, finish the installer and reboot.

4. **Install VMware Tools.** This is the step most people miss; without it
   `vmrun copyFileFromHostToGuest` fails with an unhelpful error.

   ```bash
   sudo apt update
   sudo apt install -y open-vm-tools open-vm-tools-desktop
   sudo systemctl enable --now open-vm-tools
   # or, from the VM menu: Player → Install VMware Tools
   ```

   Verify from inside the guest:

   ```bash
   systemctl status open-vm-tools      # must show "active (running)"
   ```

5. Give the guest user a password — the automation logs in with it:

   ```bash
   sudo passwd <your-user>
   ```

### Step 2 — Install the build dependencies

Open a terminal in the guest (or use `Ctrl+Alt+T`) and run:

```bash
sudo apt update
sudo apt install -y \
    build-essential \
    pkg-config \
    libgtk-3-dev \
    make
```

What each package is used for:

| Package | Why |
| ------- | --- |
| `build-essential` | GCC, libc headers, `make` |
| `pkg-config` | locates the GTK 3 and GLib compile/link flags |
| `libgtk-3-dev` | the GTK 3 headers and the `gtk+-3.0.pc` file |
| `make` | the build system |

Optionally install the extra tools used by the verification passes:

```bash
sudo apt install -y valgrind          # memory-safety pass
sudo apt install -y xvfb x11-utils imagemagick xdotool   # headless GUI tests
```

> **Tip:** the whole installation is automated by `vm_setup.sh` — see
> [Step 6](#step-6--optional-automate-everything-with-vmrun).

### Step 3 — Get the source code into the guest

**Option A — copy the tree with the included `vmrun` helpers (recommended).**

From the host, in the project directory (MSYS2 / Git Bash):

```bash
./vmrun.sh check
```

The first run syncs the tree into the guest at `/home/<user>/fs_explorer`,
builds it and runs the tests. See
[Automated host ↔ guest workflow](#automated-host--guest-workflow) for the
details.

**Option B — a shared folder.**

1. In the guest: *Player → Manage → Virtual Machine Settings → Options →
   Shared Folders → Always Enabled*.
2. Add the folder that holds the project.
3. In the guest, the share appears under `/mnt/hgfs/`.

```bash
cd /mnt/hgfs/project
make
```

**Option C — a git clone or an archive.**

```bash
git clone <your-remote-url> file-system-explorer
cd file-system-explorer
make
```

### Step 4 — Build

```bash
cd ~/fs_explorer      # or wherever the sources live
make clean            # optional: start from scratch
make                  # produces build/fs_explorer
```

The build is **warning-free** with a strict flag set:

```
-std=c11 -Wall -Wextra -Wpedantic -Wshadow -Wstrict-prototypes
-Wmissing-prototypes -Wold-style-definition
```

Sanity check that the binary was produced:

```bash
ls -l build/fs_explorer
./build/fs_explorer --version
```

### Step 5 — Run and test

```bash
make test             # the whole unit-test suite
make test-gui         # the GTK widget test (uses Xvfb automatically)
make valgrind         # the memory-safety pass
make asan             # rebuild with ASan/UBSan and run the suite
```

Launch the graphical explorer:

```bash
make run              # equivalent to ./build/fs_explorer
```

Launch the interactive shell:

```bash
make cli
```

### Step 6 — Optional: automate everything with `vmrun`

The repository ships a set of helper scripts that drive the guest from the
host over `vmrun`, so no SSH server or shared folder is required.

| Script | Purpose |
| ------ | ------- |
| `vm_config.sh` | **the single place to edit** — paths and credentials |
| `vm_setup.sh` | installs every dependency inside the guest (run in the guest) |
| `vmsync.sh` | copies the project tree from the host into the guest |
| `vmrun.sh` | sync + build + test / asan / valgrind / clean / shell |
| `vmbash.sh` | runs an arbitrary shell script inside the guest |
| `vmexec.sh` | runs a single command inside the guest and prints the output |
| `vmfetch.sh` | copies a file out of the guest |
| `vm_ready.sh` | waits until the guest answers guest operations |
| `vm_gui_check.sh`, `vm_gui_interact.sh`, `vm_gui_shot.sh` | headless GUI verification |

#### 6.1 Configure them

Open `vm_config.sh` and adjust the values for **your** machine:

```bash
# Path to vmrun.exe
VMRUN="${VMRUN:-D:\\VMware\\VMware Workstation\\vmrun.exe}"

# Path to the .vmx file of your Ubuntu VM
VMX="${VMX:-D:\\Virtualmachine\\Ubuntu 64-bit.vmx}"

# Guest credentials
GUEST_USER="${GUEST_USER:-tuananh}"
GUEST_PASS="${GUEST_PASS:-1234}"

# Where the project lives inside the guest
GUEST_PROJECT="${GUEST_PROJECT:-$GUEST_HOME/fs_explorer}"

# Where the project lives on the host (MSYS2 / Git-Bash style)
HOST_PROJECT="${HOST_PROJECT:-/d/c++/LTHT/file-system-explorer}"
```

Every value can also be overridden from the command line without editing
the file:

```bash
VMX='D:\VMs\Ubuntu-24.vmx' GUEST_USER=alice GUEST_PASS=secret \
    ./vmrun.sh check
```

#### 6.2 Start the VM and wait for it

```powershell
# Windows PowerShell / cmd
& "D:\VMware\VMware Workstation\vmrun.exe" start "D:\Virtualmachine\Ubuntu 64-bit.vmx" nogui
```

```bash
# then, from MSYS2 / Git Bash
./vm_ready.sh          # polls until the guest answers guest operations
```

#### 6.3 Prepare the guest (one-off)

```bash
./vmbash.sh vm_setup.sh
```

This installs `build-essential`, `pkg-config`, `libgtk-3-dev`, `valgrind`,
`xvfb`, `x11-utils`, `imagemagick` and `xdotool` inside the guest and
prints a verification table.

#### 6.4 Build and test

```bash
./vmrun.sh build       # compile in the guest
./vmrun.sh test        # run the whole suite in the guest
./vmrun.sh check       # build + test (default)
./vmrun.sh asan        # ASan + UBSan pass
./vmrun.sh vg          # Valgrind pass
./vmrun.sh clean       # remove the build artefacts in the guest
./vmrun.sh shell       # an interactive shell inside the guest
```

### Step 7 — Troubleshooting

| Symptom | Cause | Fix |
| ------- | ----- | --- |
| `pkg-config: command not found` | `pkg-config` is missing | `sudo apt install -y pkg-config` |
| `Package gtk+-3.0 was not found` | `libgtk-3-dev` is missing | `sudo apt install -y libgtk-3-dev` |
| `No such file or directory` on `make` | `make` is missing | `sudo apt install -y build-essential` |
| `make test-gui` skips the GUI test | no display and no `Xvfb` | `sudo apt install -y xvfb` |
| `The virtual machine is not powered on` | the VM is off | start it with `vmrun start` |
| `vmrun` reports an authentication error | wrong credentials | check `GUEST_USER` / `GUEST_PASS` in `vm_config.sh` |
| `Error: The virtual machine is not found` | wrong `.vmx` path | check `VMX` in `vm_config.sh` |
| `Guest operations are disabled` | VMware Tools are missing | install `open-vm-tools` (Step 1.4) |
| `Permission denied` on `sudo` | the password is wrong | re-set it with `sudo passwd <user>` |

---

## Automated host ↔ guest workflow

`vmrun.sh` is the entry point. It always performs three steps:

1. **sync** — the host tree is packed into a tarball, copied into the guest
   with `vmrun copyFileFromHostToGuest` and extracted into
   `$GUEST_PROJECT`;
2. **run** — a small shell script is staged in the guest and executed with
   `vmrun runProgramInGuest`;
3. **fetch** — the output is read back into a file in the guest and copied
   to the host, because `runProgramInGuest` does not capture stdout.

```bash
./vmrun.sh check
```

```
### sync ok
===== BUILD =====
...
### MAKE EXIT: 0
===== TESTS =====
---- running build/test_utils ----
==== Utils smoke test ====
==== Result: 72 passed, 0 failed ====
...
### TEST EXIT: 0
```

---

## Running the application

```bash
make run                    # launch the GUI
make cli                    # launch the interactive shell
```

### Command-line options

```
fs_explorer [options]

  --cli            start the interactive command shell
  --exec "<cmd>"   run a single command and exit
  --path <dir>     set the starting directory
  --version        print the version and exit
  --help, -h       print this help and exit
```

Examples:

```bash
./build/fs_explorer --exec "ls /etc"           # one-shot command
./build/fs_explorer --cli --path /home/user   # shell starting elsewhere
./build/fs_explorer                          # the GTK application
```

---

## CLI reference

```
File System Explorer - command reference

Navigation
  ls [path]              list the contents of a directory
  cd <path>              change the working directory
  pwd                    print the working directory
  tree [path] [depth]    print a directory tree

Inspection
  stat <path>            show detailed metadata for a file
  info <path>            show the permission summary for a file
  du [path]              recursive size analysis of a directory
  find <pattern> [root]  recursive name search ('*' and '?' wildcards)
  df [path]              show file system capacity
  readlink <path>        print the target of a symbolic link

Mutation
  mkdir <path> [-p]      create a directory (-p creates parents)
  touch <path>           create an empty file
  mv <src> <dst>         rename or move a file
  rm <path>              remove a file or an EMPTY directory
  rmdir <path>           remove an EMPTY directory
  ln -s <target> <link>  create a symbolic link

Session
  log                    show the activity log
  clear                  clear the activity log
  help                   show this help
  exit / quit            leave the shell
```

### Extra syntax

* **Quoted arguments** — `"with space/file.txt"` is treated as one argument.
* **Chaining** — `&&` runs the next command only if the previous one
  succeeded; `;` always continues.

  ```
  ~ $ cd "/tmp/my dir" && ls && stat report.txt
  ```

* **Wildcards** in `find` — `*` matches any run of characters, `?` matches
  exactly one.

  ```
  ~ $ find *.log /var/log
  ```

---

## GUI reference

| Toolbar button | Action |
| -------------- | ------ |
| `<`            | Go back (history stack) |
| `>`            | Go forward |
| `^`            | Parent folder |
| `Home`         | Home folder |
| `Refresh`      | Re-read the current directory |
| `Analyze`      | Recursive size / count analysis of the current folder |
| `Disk`         | File system capacity (total / used / free) |
| `Find`         | Recursive name search |
| `New folder`   | Create a sub-directory |
| `Delete`       | Delete the selected entry (with confirmation) |
| `About`        | Version and licence information |

### Window layout

```
+---------------------------------------------------------------+
|  <  >  ^  Home  Refresh  Analyze  Disk  Find  New folder ...  |  toolbar
|  /home/user/project                              [ Go ]       |  path entry
+---------------------+-----------------------------------------+
|  Directory tree     |  File list                              |
|  (lazy loaded)      |  NAME  TYPE  SIZE  MODIFIED  PERMS     |
+---------------------+-----------------------------------------+
|  Metadata panel:  Name / Path / Type / Size / Mode / Owner /   |
|  Timestamps / Inode / Links / Blocks / Link target / Access    |
+---------------------------------------------------------------+
|  Activity log (expander)                                        |
+---------------------------------------------------------------+
|  Status bar                                                     |
+---------------------------------------------------------------+
```

The tree, the file list, the metadata panel and the activity log all refresh
automatically through the `AppState` GObject signals `fs-path-changed` and
`fs-selection-changed`.

---

## Architecture

```
                       +---------------------------+
                       |          main.c           |
                       |  (option parsing, mode    |
                       |   selection: GUI / CLI)   |
                       +-------------+-------------+
                                     |
              +----------------------+----------------------+
              |                                             |
   +---------v----------+                      +-----------v---------+
    |      GUI (GTK 3)   |                      |     CLI shell       |
    |  main_window       |                      |  shell.c            |
    |  directory_tree    |                      |  command.c          |
    |  file_view         |                      |  (REPL, chaining)   |
    |  metadata_panel    |                      +-----------+---------+
    |  dialogs           |                                  |
    +---------+----------+                                  |
              |                                             |
              +----------------------+----------------------+
                                     |
                       +-------------v--------------+
                       |        AppState           |
                       |  (GObject: current path, |
                       |   selection, history      |
                       |   stack, signals)         |
                       +-------------+--------------+
                                     |
     +-----------+-----------+-------+-------+-----------+
     |           |           |       |       |           |
  +--v--+    +---v----   +--v--+  +-v---+  +-v------+  +-v-------+
  |fs_  |    |fs_perm-  |fs_  |  |fs_  |  |fs_     |  |fs_      |
  |meta |    |issions   |analy|  |sear|  |listing |  |operatio |
  |data |    |          |sis  |  |ch  |  |        |  |ns       |
  +-----+    +---------+ +-----+  +----+  +--------+  +---------+
     |
  +--v-----------+   +-------------+
  | fs_platform  |   | fs_stat_   |
  | (POSIX/Win32)|   | util       |
  +--------------+   +-------------+
```

### Layers

| Layer | Location | Responsibility |
| ----- | -------- | -------------- |
| **Entry point** | `src/main.c` | Option parsing, mode selection, GTK bootstrap |
| **Presentation** | `src/gui/`, `src/cli/` | GTK widgets and the command interpreter |
| **State** | `src/app_state.c` | GObject holding the current path, selection and history |
| **Core** | `src/filesystem/` | Metadata, listing, analysis, search, permissions, symlinks, operations, filesystem info |
| **Utilities** | `src/utils/` | Path helpers, formatting, logging, the platform abstraction |

The core layer never includes GTK headers, which is why the core unit tests
link against it without pulling in a display server. Only the tests whose
name starts with `test_gui_` are compiled and linked with GTK.

---

## Build targets

| Target | Description |
| ------ | ----------- |
| `make` / `make all` | Build `build/fs_explorer` |
| `make run` | Build and launch the GUI |
| `make cli` | Build and launch the interactive shell |
| `make test` | Build and run the core unit tests |
| `make test-gui` | Run the GUI tests (starts `Xvfb` when needed) |
| `make asan` | Rebuild with ASan + UBSan and run the whole suite |
| `make valgrind` | Run the CLI under Valgrind with full leak checking |
| `make debug` | Clean rebuild with `-O0 -g3 -DDEBUG` |
| `make clean` | Remove the `build/` directory |
| `make help` | List every target |

---

## Project layout

```
file-system-explorer/
├── Makefile
├── README.md
├── include/                    # public headers (one per module)
│   ├── app_state.h
│   ├── cli_command.h
│   ├── cli_shell.h
│   ├── format_utils.h
│   ├── fs_analysis.h
│   ├── fs_filesystem_info.h
│   ├── fs_listing.h
│   ├── fs_metadata.h
│   ├── fs_navigation.h
│   ├── fs_operations.h
│   ├── fs_permissions.h
│   ├── fs_platform.h
│   ├── fs_search.h
│   ├── fs_stat_util.h
│   ├── fs_symlink.h
│   ├── fs_types.h
│   ├── gui_dialogs.h
│   ├── gui_directory_tree.h
│   ├── gui_file_view.h
│   ├── gui_main_window.h
│   ├── gui_metadata_panel.h
│   ├── logger.h
│   └── path_utils.h
├── src/
│   ├── main.c
│   ├── app_state.c
│   ├── cli/
│   │   ├── command.c            # command table, chaining, quoting
│   │   └── shell.c              # the REPL
│   ├── filesystem/
│   │   ├── analysis.c           # recursive statistics
│   │   ├── filesystem_info.c    # statvfs
│   │   ├── listing.c            # directory reading and sorting
│   │   ├── metadata.c           # stat / lstat metadata
│   │   ├── navigation.c         # path helpers around the filesystem
│   │   ├── operations.c         # mkdir / touch / rename / remove / copy
│   │   ├── permissions.c        # rwx class analysis
│   │   ├── search.c             # wildcard search with depth limits
│   │   ├── stat_util.c          # stat -> FileMetadata conversion
│   │   └── symlink.c            # link creation and resolution
│   ├── gui/
│   │   ├── dialogs.c
│   │   ├── directory_tree.c
│   │   ├── file_view.c
│   │   ├── main_window.c
│   │   └── metadata_panel.c
│   └── utils/
│       ├── format_utils.c       # human sizes, permission strings, dates
│       ├── fs_platform.c        # POSIX / Win32 abstraction
│       ├── logger.c             # activity log
│       └── path_utils.c         # join, dirname, basename, absolute
├── tests/
│   ├── test_app_state.c
│   ├── test_cli.c
│   ├── test_fs_analysis.c
│   ├── test_fs_listing.c
│   ├── test_fs_metadata.c
│   ├── test_fs_operations.c
│   ├── test_fs_search.c
│   ├── test_fs_symlink.c
│   ├── test_gui_metadata.c     # widget-level test (needs a display)
│   ├── test_util.h             # portable helpers for the test programs
│   └── test_utils.c
├── scripts/
│   └── make_test_tree.sh       # generates a sample directory tree
└── vm_*.sh                     # VMware guest automation (host side)
```

---

## Testing

```bash
make test        # the core test suite
make test-gui    # the GUI tests (starts Xvfb automatically when available)
```

The suite contains **451 assertions across 10 binaries**, all passing:

| Test binary | Assertions | Area |
| ----------- | ---------- | ---- |
| `test_utils`            | 72 | Path helpers, formatting, logging |
| `test_cli`              | 69 | Every command, quoting, `&&` chaining |
| `test_fs_metadata`      | 61 | `stat` / `lstat` metadata extraction |
| `test_app_state`        | 45 | History stack, back / forward / up |
| `test_fs_listing`       | 44 | Directory reading, sorting, truncation |
| `test_fs_operations`    | 42 | `mkdir`, `touch`, `mv`, `rm`, `copy` |
| `test_fs_analysis`      | 42 | Recursive counts, sizes, top-N files |
| `test_gui_metadata`     | 35 | The GTK metadata panel (widget level) |
| `test_fs_search`        | 22 | Wildcards, depth limits, result caps |
| `test_fs_symlink`       | 21 | Link creation, target resolution, broken links |

Each test is self-contained: it creates its own sandbox under the system
temporary directory, cleans it up afterwards, and prints a
`==== Result: N passed, M failed ====` summary.

The GUI test needs a display. When none is available it **skips itself**
instead of failing, so `make test` still passes on a headless build machine.
With `Xvfb` installed, `make test-gui` starts a virtual X server and the
widget is really exercised.

### Platform-specific expectations

The tests run on Linux and on Windows. A few behaviours are genuinely
different and are handled explicitly rather than being papered over:

* **Symbolic links** need Administrator rights or Developer Mode on
  Windows. `t_can_symlink()` probes the capability once and the affected
  test cases **skip** themselves with a message instead of failing.
* **Permission bits** only exist on POSIX. The Windows platform layer
  synthesises `rw-rw-rw-` / `r--r--r--` from the read-only attribute.
* **The inode number** is the NTFS file index on Windows.
* **Newline translation** — the tests write their files in binary mode, so
  a file is 12 bytes on both platforms.

---

## Memory-safety tooling

```bash
make asan       # rebuild with AddressSanitizer + UndefinedBehaviorSanitizer
make valgrind   # run the CLI under valgrind with full leak checking
```

Current status on Ubuntu 26.04 / GCC 15.2 / Valgrind 3.26:

* **ASan + UBSan** — all test binaries pass with zero reports.
* **Valgrind** — `0 bytes definitely lost`, `0 bytes indirectly lost`,
  `0 errors` across every command (`help`, `ls`, `stat`, `find`, `du`, `df`).

The same passes can be run inside the guest with `./vmrun.sh asan` and
`./vmrun.sh vg`.

---

## Building on Windows (MSYS2)

```bash
pacman -S --needed mingw-w64-ucrt-x86_64-gtk3 mingw-w64-ucrt-x86_64-pkgconf
make
make test
make run
```

`build_msys2.sh` performs the whole cycle in one command, and
`run_win_tests.sh` runs the compiled test binaries and prints a summary.

> **Note.** Creating symbolic links on Windows requires Administrator
> rights or Developer Mode, so the symlink test suites report *skipped*
> rather than *failed* on a standard account.

---

## Design notes

### A typed status code instead of `errno`

Every core function returns an `FSStatus`:

```c
typedef enum {
    FS_OK = 0,
    FS_ERR_INVALID_PATH,
    FS_ERR_NOT_FOUND,
    FS_ERR_NOT_DIR,
    FS_ERR_EXISTS,
    FS_ERR_PERMISSION,
    FS_ERR_IO,
    FS_ERR_NOMEM,
    FS_ERR_UNKNOWN
} FSStatus;
```

`fs_stat_util_status_from_errno()` maps a raw `errno` onto this enum, so the
GUI can render precise messages ("Permission denied", "No such file or
directory") while the CLI stays terse.

### `AppState` is a GObject

`AppState` carries the current path, the selected entry and the history
stack. Making it a `GObject` lets the widgets subscribe to
`fs-path-changed` and `fs-selection-changed` instead of being manually
re-wired by the main window. The history paths are stored as `gchar *`
pointers rather than a fixed 64 × 4096-byte array, which keeps the instance
size inside GLib's 16-bit instance-size limit.

### Safe directory traversal

`fs_search` and `fs_analyze_directory` never follow symlinks and keep a set
of visited inode numbers. A symlink loop therefore cannot cause an infinite
recursion, and the depth limit is enforced at every level.

### The platform layer

`src/utils/fs_platform.c` wraps the few genuinely platform-specific calls
(`lstat`, `statvfs`, the current directory, the home directory, user/group
lookups) behind a single header. The Windows build swaps in the
`CreateFileW` / `GetFileInformationByHandle` equivalents and the
`FILE_ATTRIBUTE_*` → POSIX mode conversion.

---

## Licence

Written as an academic System Programming project. Free to use and modify
for educational purposes.
