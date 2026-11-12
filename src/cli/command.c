/* ============================================================================
 * cli/command.c -- Shared command interpreter.
 *
 * One command interpreter serves both the interactive CLI shell and any
 * future scripting front-end. Every command writes its output through a
 * caller-supplied writer callback, so the module never touches stdout
 * directly and stays testable.
 *
 * Commands
 *   help                       show the command list
 *   ls [path]                  list a directory
 *   cd <path>                  change the working directory
 *   pwd                        print the working directory
 *   stat <path>                show detailed metadata
 *   info <path>                show metadata + permission summary
 *   du [path]                  recursive analysis of a directory
 *   find <pattern> [root]      recursive name search
 *   df [path]                  file system capacity
 *   tree [path] [depth]        print a directory tree
 *   mkdir <path> [-p]          create a directory
 *   touch <path>               create an empty file
 *   mv <src> <dst>             rename / move
 *   rm <path>                  remove a file or empty directory
 *   ln -s <target> <link>      create a symbolic link
 *   readlink <path>            print a symbolic link's target
 *   log                        show the activity log
 *   clear                      clear the activity log
 *   exit / quit                leave the shell
 * ============================================================================
 */

#include "cli_command.h"

#include "app_state.h"
#include "format_utils.h"
#include "fs_analysis.h"
#include "fs_filesystem_info.h"
#include "fs_listing.h"
#include "fs_metadata.h"
#include "fs_navigation.h"
#include "fs_operations.h"
#include "fs_permissions.h"
#include "fs_search.h"
#include "fs_stat_util.h"
#include "fs_symlink.h"
#include "logger.h"
#include "path_utils.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Maximum number of entries a single "ls" prints before it truncates. */
#define CLI_MAX_LS_ENTRIES   4096
#define CLI_MAX_SEARCH_RESULTS 512
#define CLI_MAX_ANALYSIS_DEPTH 8

/* ------------------------------------------------------------------ *
 * Output helpers
 * ------------------------------------------------------------------ */
static void
emit(cli_writer_fn out, void *user_data, const char *fmt, ...)
{
    if (out == NULL) return;

    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);

    out(buf, user_data);
}

/* ------------------------------------------------------------------ *
 * Tokenizer: splits a command line into whitespace-separated arguments,
 * honouring double quotes so that paths with spaces can be used.
 * ------------------------------------------------------------------ */
#define CLI_MAX_ARGS 16

static int
tokenize(const char *line, char args[CLI_MAX_ARGS][FS_PATH_MAX])
{
    int argc = 0;
    const char *p = line;

    while (*p != '\0' && argc < CLI_MAX_ARGS) {
        /* Skip whitespace. */
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '\0') break;

        char *dst = args[argc];
        size_t n = 0;

        if (*p == '"') {
            p++;
            while (*p != '\0' && *p != '"') {
                if (n + 1 < FS_PATH_MAX) dst[n++] = *p;
                p++;
            }
            if (*p == '"') p++;
        } else {
            while (*p != '\0' && *p != ' ' && *p != '\t') {
                if (n + 1 < FS_PATH_MAX) dst[n++] = *p;
                p++;
            }
        }

        dst[n] = '\0';
        if (n > 0) argc++;
    }

    return argc;
}

/* ------------------------------------------------------------------ *
 * Resolve a user-supplied path against the current directory.
 * ------------------------------------------------------------------ */
static void
resolve_against(const char *cwd, const char *path, char *out, size_t out_size)
{
    if (path_is_absolute(path)) {
        snprintf(out, out_size, "%s", path);
        return;
    }
    path_join(cwd, path, out, out_size);
}

/* ------------------------------------------------------------------ *
 * cmd_ls
 * ------------------------------------------------------------------ */
static FSStatus
cmd_ls(const char *cwd, char args[][FS_PATH_MAX], int argc, cli_writer_fn out, void *ud)
{
    char target[FS_PATH_MAX];
    if (argc >= 2) {
        resolve_against(cwd, args[1], target, sizeof(target));
    } else {
        snprintf(target, sizeof(target), "%s", cwd);
    }

    DirEntry *entries = (DirEntry *)calloc(CLI_MAX_LS_ENTRIES,
                                           sizeof(DirEntry));
    if (entries == NULL) {
        emit(out, ud, "ls: out of memory\n");
        return FS_ERR_NOMEM;
    }

    int count = CLI_MAX_LS_ENTRIES;
    FSStatus s = fs_list_dir(target, entries, &count);
    if (s != FS_OK) {
        emit(out, ud, "ls: %s: %s\n", target,
             fmt_status_message(s, errno));
        free(entries);
        return s;
    }

    emit(out, ud, "%-28s %-10s %8s  %-19s %s\n",
         "NAME", "TYPE", "SIZE", "MODIFIED", "PERMS");
    emit(out, ud, "%-28s %-10s %8s  %-19s %s\n",
         "----------------------------", "----------", "--------",
         "-------------------", "----------");

    for (int i = 0; i < count; i++) {
        const FileMetadata *m = &entries[i].meta;

        char size[32];
        fmt_size_human((unsigned long long)m->size, size, sizeof(size));

        char mtime[32];
        fmt_time_local(m->mtime, mtime, sizeof(mtime));

        const char *type = fmt_type_label(m->type);

        if (m->status != FS_OK) {
            emit(out, ud, "%-28s %-10s %8s  %-19s %s  <%s>\n",
                 m->name, type, "-", "-", m->perm_symbolic,
                 fmt_status_message(m->status, m->errno_code));
            continue;
        }

        emit(out, ud, "%-28s %-10s %8s  %-19s %s\n",
             m->name, type, size, mtime, m->perm_symbolic);
    }

    if (count == CLI_MAX_LS_ENTRIES) {
        emit(out, ud, "... (truncated at %d entries)\n", CLI_MAX_LS_ENTRIES);
    }
    emit(out, ud, "\n%d item%s\n", count, (count == 1) ? "" : "s");

    free(entries);
    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * cmd_stat
 * ------------------------------------------------------------------ */
static FSStatus
cmd_stat(const char *cwd, char args[][FS_PATH_MAX], int argc, cli_writer_fn out, void *ud)
{
    if (argc < 2) {
        emit(out, ud, "stat: missing operand\n");
        return FS_ERR_INVALID_PATH;
    }

    char target[FS_PATH_MAX];
    resolve_against(cwd, args[1], target, sizeof(target));

    FileMetadata m;
    FSStatus s = fs_get_metadata(target, 1, &m);
    if (s != FS_OK) {
        emit(out, ud, "stat: %s: %s\n", target,
             fmt_status_message(s, m.errno_code));
        return s;
    }

    char size[32], atime[32], mtime[32], ctime[32];
    fmt_size_human((unsigned long long)m.size, size, sizeof(size));
    fmt_time_local(m.atime, atime, sizeof(atime));
    fmt_time_local(m.mtime, mtime, sizeof(mtime));
    fmt_time_local(m.ctime, ctime, sizeof(ctime));

    emit(out, ud, "  File: %s\n", m.path);
    emit(out, ud, "  Size: %lld bytes (%s)\n", (long long)m.size, size);
    emit(out, ud, "  Type: %s\n", fmt_type_label(m.type));
    emit(out, ud, "  Mode: %s (%s)\n", m.perm_symbolic, m.perm_octal);
    emit(out, ud, "  Owner: %s : %s\n", m.owner, m.group);
    emit(out, ud, "  Inode: %llu   Links: %llu\n",
         (unsigned long long)m.inode, (unsigned long long)m.nlinks);
    emit(out, ud, " Blocks: %llu x %llu bytes\n",
         (unsigned long long)m.blocks, (unsigned long long)m.block_size);
    emit(out, ud, "Access: %s\n", atime);
    emit(out, ud, "Modify: %s\n", mtime);
    emit(out, ud, "Change: %s\n", ctime);

    if (m.type == FS_TYPE_SYMLINK) {
        emit(out, ud, "  Link: %s -> %s", m.name, m.link_target);
        if (m.link_target_exists == 1) {
            emit(out, ud, " (target exists)\n");
        } else if (m.link_target_exists == -1) {
            emit(out, ud, " (BROKEN LINK)\n");
        } else {
            emit(out, ud, "\n");
        }
    }
    return FS_OK;
}


/* ------------------------------------------------------------------ *
 * cmd_info -- metadata plus the permission summary
 * ------------------------------------------------------------------ */
static FSStatus
cmd_info(const char *cwd, char args[][FS_PATH_MAX], int argc, cli_writer_fn out, void *ud)
{
    if (argc < 2) {
        emit(out, ud, "info: missing operand\n");
        return FS_ERR_INVALID_PATH;
    }

    char target[FS_PATH_MAX];
    resolve_against(cwd, args[1], target, sizeof(target));

    FsPermInfo perm;
    FSStatus s = fs_get_permissions(target, &perm);
    if (s != FS_OK) {
        emit(out, ud, "info: %s: %s\n", target, fmt_status_message(s, errno));
        return s;
    }

    char desc[256];
    fs_perm_describe(&perm, desc, sizeof(desc));

    const char *klass = "other";
    switch (fs_perm_effective_class(&perm)) {
    case FS_PERM_CLASS_OWNER: klass = "owner"; break;
    case FS_PERM_CLASS_GROUP: klass = "group"; break;
    case FS_PERM_CLASS_OTHER: klass = "other"; break;
    }

    emit(out, ud, "  Permissions: %s\n", desc);
    emit(out, ud, "  Your class : %s%s\n", klass,
         fs_perm_is_owner(&perm) ? " (owner)" : "");

    FsPermBits bits = fs_perm_effective_bits(&perm);
    char access[4];
    snprintf(access, sizeof(access), "%s%s%s",
             bits.read ? "r" : "-",
             bits.write ? "w" : "-",
             bits.execute ? "x" : "-");
    emit(out, ud, "  Your access: %s\n", access);
    return FS_OK;
}


/* ------------------------------------------------------------------ *
 * cmd_du
 * ------------------------------------------------------------------ */
static FSStatus
cmd_du(const char *cwd, char args[][FS_PATH_MAX], int argc, cli_writer_fn out, void *ud)
{
    char target[FS_PATH_MAX];
    if (argc >= 2) {
        resolve_against(cwd, args[1], target, sizeof(target));
    } else {
        snprintf(target, sizeof(target), "%s", cwd);
    }

    DirStats stats;
    FSStatus s = fs_analyze_directory(target, &stats);
    if (s != FS_OK) {
        emit(out, ud, "du: %s: %s\n", target, fmt_status_message(s, errno));
        return s;
    }

    char total[32], avg[32];
    fmt_size_human(stats.total_bytes, total, sizeof(total));
    fmt_size_human(stats.avg_file_size, avg, sizeof(avg));

    emit(out, ud, "Directory analysis of %s\n", stats.path);
    emit(out, ud, "  Files      : %llu\n", stats.total_files);
    emit(out, ud, "  Directories: %llu\n", stats.total_dirs);
    emit(out, ud, "  Symlinks   : %llu\n", stats.total_symlinks);
    emit(out, ud, "  Other      : %llu\n", stats.total_other);
    emit(out, ud, "  Total size : %s (%llu bytes)\n", total, stats.total_bytes);
    emit(out, ud, "  Avg file   : %s\n", avg);

    if (stats.largest_file_size > 0) {
        char lsize[32];
        fmt_size_human(stats.largest_file_size, lsize, sizeof(lsize));
        emit(out, ud, "  Largest    : %s (%s)\n", lsize, stats.largest_file_path);
    }

    if (stats.top_files_count > 0) {
        emit(out, ud, "\n  Top %d largest files:\n", stats.top_files_count);
        for (int i = 0; i < stats.top_files_count; i++) {
            char fsize[32];
            fmt_size_human(stats.top_files[i].size, fsize, sizeof(fsize));
            emit(out, ud, "    %-10s %s\n", fsize, stats.top_files[i].path);
        }
    }

    if (stats.subdirs_count > 0) {
        emit(out, ud, "\n  Sub-directories:\n");
        for (int i = 0; i < stats.subdirs_count; i++) {
            char dsize[32];
            fmt_size_human(stats.subdirs[i].size, dsize, sizeof(dsize));
            emit(out, ud, "    %-10s %s\n", dsize, stats.subdirs[i].name);
        }
    }
    return FS_OK;
}


/* ------------------------------------------------------------------ *
 * cmd_find
 * ------------------------------------------------------------------ */
static FSStatus
cmd_find(const char *cwd, char args[][FS_PATH_MAX], int argc, cli_writer_fn out, void *ud)
{
    if (argc < 2) {
        emit(out, ud, "find: missing pattern\n");
        return FS_ERR_INVALID_PATH;
    }

    char root[FS_PATH_MAX];
    if (argc >= 3) {
        resolve_against(cwd, args[2], root, sizeof(root));
    } else {
        snprintf(root, sizeof(root), "%s", cwd);
    }

    /* A pattern containing '*' or '?' is a wildcard; otherwise we do a
     * plain substring match. */
    int literal = (strchr(args[1], '*') == NULL &&
                   strchr(args[1], '?') == NULL);

    SearchResult *results = (SearchResult *)calloc(CLI_MAX_SEARCH_RESULTS,
                                                   sizeof(SearchResult));
    if (results == NULL) {
        emit(out, ud, "find: out of memory\n");
        return FS_ERR_NOMEM;
    }

    int count = CLI_MAX_SEARCH_RESULTS;
    FSStatus s = fs_search(root, args[1], literal, -1, CLI_MAX_SEARCH_RESULTS,
                           results, &count);
    if (s != FS_OK) {
        emit(out, ud, "find: %s: %s\n", root, fmt_status_message(s, errno));
        free(results);
        return s;
    }

    for (int i = 0; i < count; i++) {
        char size[32];
        fmt_size_human((unsigned long long)results[i].meta.size,
                       size, sizeof(size));
        emit(out, ud, "  %-10s %-14s %s\n", size,
             fmt_type_label(results[i].meta.type), results[i].meta.path);
    }

    emit(out, ud, "\n%d match%s\n", count, (count == 1) ? "" : "es");
    free(results);
    return FS_OK;
}


/* ------------------------------------------------------------------ *
 * cmd_df
 * ------------------------------------------------------------------ */
static FSStatus
cmd_df(const char *cwd, char args[][FS_PATH_MAX], int argc, cli_writer_fn out, void *ud)
{
    char target[FS_PATH_MAX];
    if (argc >= 2) {
        resolve_against(cwd, args[1], target, sizeof(target));
    } else {
        snprintf(target, sizeof(target), "%s", cwd);
    }

    FSInfo info;
    FSStatus s = fs_get_filesystem_info(target, &info);
    if (s != FS_OK) {
        emit(out, ud, "df: %s: %s\n", target, fmt_status_message(s, errno));
        return s;
    }

    char total[32], fre[32], avail[32];
    fmt_size_human(info.total_bytes, total, sizeof(total));
    fmt_size_human(info.free_bytes, fre, sizeof(fre));
    fmt_size_human(info.available_bytes, avail, sizeof(avail));

    emit(out, ud, "File system for %s\n", info.mount_path);
    emit(out, ud, "  Block size    : %llu bytes\n", info.block_size);
    emit(out, ud, "  Total         : %s (%llu blocks)\n",
         total, info.total_blocks);
    emit(out, ud, "  Free          : %s (%llu blocks)\n",
         fre, info.free_blocks);
    emit(out, ud, "  Available     : %s (%llu blocks)\n",
         avail, info.available_blocks);
    emit(out, ud, "  Used          : %.1f%%\n", info.usage_percent);

    if (info.total_inodes > 0) {
        emit(out, ud, "  Inodes        : %llu total, %llu free\n",
             info.total_inodes, info.free_inodes);
    }
    return FS_OK;
}


/* ------------------------------------------------------------------ *
 * cmd_tree
 * ------------------------------------------------------------------ */
static void
tree_walk(const char *dir, int depth, int max_depth,
          cli_writer_fn out, void *ud)
{
    if (depth > max_depth) return;

    DirEntry *entries = (DirEntry *)calloc(512, sizeof(DirEntry));
    if (entries == NULL) return;

    int count = 512;
    if (fs_list_dir(dir, entries, &count) != FS_OK) {
        free(entries);
        return;
    }

    for (int i = 0; i < count; i++) {
        for (int j = 0; j < depth; j++) {
            emit(out, ud, "    ");
        }
        emit(out, ud, "%s%s\n", entries[i].meta.name,
             (entries[i].meta.type == FS_TYPE_DIRECTORY) ? "/" : "");

        if (entries[i].meta.type == FS_TYPE_DIRECTORY) {
            tree_walk(entries[i].meta.path, depth + 1, max_depth, out, ud);
        }
    }
    free(entries);
}

static FSStatus
cmd_tree(const char *cwd, char args[][FS_PATH_MAX], int argc, cli_writer_fn out, void *ud)
{
    char target[FS_PATH_MAX];
    if (argc >= 2) {
        resolve_against(cwd, args[1], target, sizeof(target));
    } else {
        snprintf(target, sizeof(target), "%s", cwd);
    }

    int depth = CLI_MAX_ANALYSIS_DEPTH;
    if (argc >= 3) {
        depth = atoi(args[2]);
        if (depth < 0) depth = CLI_MAX_ANALYSIS_DEPTH;
    }

    emit(out, ud, "%s\n", target);
    tree_walk(target, 1, depth, out, ud);
    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * cmd_readlink
 * ------------------------------------------------------------------ */
static FSStatus
cmd_readlink(const char *cwd, char args[][FS_PATH_MAX], int argc, cli_writer_fn out, void *ud)
{
    if (argc < 2) {
        emit(out, ud, "readlink: missing operand\n");
        return FS_ERR_INVALID_PATH;
    }

    char target[FS_PATH_MAX];
    resolve_against(cwd, args[1], target, sizeof(target));

    char link[FS_PATH_MAX];
    FSStatus s = fs_read_link_target(target, link, sizeof(link));
    if (s != FS_OK) {
        emit(out, ud, "readlink: %s: %s\n", target,
             fmt_status_message(s, errno));
        return s;
    }

    emit(out, ud, "%s\n", link);
    return FS_OK;
}


/* ------------------------------------------------------------------ *
 * cli_print_help
 * ------------------------------------------------------------------ */
void
cli_print_help(cli_writer_fn out, void *user_data)
{
    emit(out, user_data,
"File System Explorer - command reference\n"
"\n"
"Navigation\n"
"  ls [path]              list the contents of a directory\n"
"  cd <path>              change the working directory\n"
"  pwd                    print the working directory\n"
"  tree [path] [depth]    print a directory tree\n"
"\n"
"Inspection\n"
"  stat <path>            show detailed metadata for a file\n"
"  info <path>            show the permission summary for a file\n"
"  du [path]              recursive size analysis of a directory\n"
"  find <pattern> [root]  recursive name search ('*' and '?' wildcards)\n"
"  df [path]              show file system capacity\n"
"  readlink <path>        print the target of a symbolic link\n"
"\n"
"Mutation\n"
"  mkdir <path> [-p]      create a directory (-p creates parents)\n"
"  touch <path>           create an empty file\n"
"  mv <src> <dst>         rename or move a file\n"
"  rm <path>              remove a file or an EMPTY directory\n"
"  rmdir <path>           remove an EMPTY directory\n"
"  ln -s <target> <link>  create a symbolic link\n"
"\n"
"Session\n"
"  log                    show the activity log\n"
"  clear                  clear the activity log\n"
"  help                   show this help\n"
"  exit / quit            leave the shell\n");
}

/* ------------------------------------------------------------------ *
 * cli_run_line_with_state
 *
 * Executes one command line. `current_path` is updated by "cd" and other
 * navigation commands.
 * ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ *
 * Run a single command (no chaining).
 * ------------------------------------------------------------------ */
static FSStatus
cli_run_single(const char *line,
               char *current_path,
               size_t current_path_size,
               cli_writer_fn out,
               void *user_data);

/* ------------------------------------------------------------------ *
 * cli_run_line_with_state
 *
 * Executes one command line. `current_path` is updated by "cd" and other
 * navigation commands.
 *
 * A line may contain several commands separated by "&&" (run the next
 * command only if the previous one succeeded) or ";" (always run the next
 * command).
 * ------------------------------------------------------------------ */
FSStatus
cli_run_line_with_state(const char *line,
                        char *current_path,
                        size_t current_path_size,
                        cli_writer_fn out,
                        void *user_data)
{
    if (line == NULL || current_path == NULL) {
        return FS_ERR_INVALID_PATH;
    }

    /* Split the line into segments at the top level. A "&&" makes the
     * following segment conditional on the previous one's success, a ";"
     * always continues. */
    char segment[4096];
    FSStatus overall = FS_OK;
    int run_next = 1;

    const char *p = line;
    while (*p != '\0') {
        /* Find the next separator. */
        const char *sep = p;
        int conditional = 0;

        while (*sep != '\0') {
            if (*sep == ';') {
                conditional = 0;
                break;
            }
            if (*sep == '&' && sep[1] == '&') {
                conditional = 1;
                break;
            }
            sep++;
        }

        size_t seg_len = (size_t)(sep - p);
        if (seg_len >= sizeof(segment)) seg_len = sizeof(segment) - 1;
        memcpy(segment, p, seg_len);
        segment[seg_len] = '\0';

        if (run_next) {
            FSStatus s = cli_run_single(segment, current_path,
                                        current_path_size, out, user_data);
            if (s != FS_OK) {
                overall = s;
                /* A failed command stops an "&&" chain but not a ";" one. */
                run_next = 0;
            }
        }

        if (*sep == '\0') break;

        /* Skip the separator itself. */
        p = sep + (conditional ? 2 : 1);

        /* After "&&" the next segment only runs if the previous one
         * succeeded; after ";" it always runs. */
        run_next = conditional ? (overall == FS_OK) : 1;
    }

    return overall;
}

/* ------------------------------------------------------------------ *
 * cli_run_single
 * ------------------------------------------------------------------ */
static FSStatus
cli_run_single(const char *line,
               char *current_path,
               size_t current_path_size,
               cli_writer_fn out,
               void *user_data)
{
    if (line == NULL || current_path == NULL) {
        return FS_ERR_INVALID_PATH;
    }

    /* Skip leading whitespace. */
    while (*line == ' ' || *line == '\t') line++;

    /* An empty line does nothing. */
    if (*line == '\0') return FS_OK;

    char args[CLI_MAX_ARGS][FS_PATH_MAX];
    int argc = tokenize(line, args);
    if (argc == 0) return FS_OK;

    const char *cmd = args[0];
    char cwd[FS_PATH_MAX];
    snprintf(cwd, sizeof(cwd), "%s", current_path);

    /* ---------------------------------------------------------------- *
     * Navigation
     * ---------------------------------------------------------------- */
    if (strcmp(cmd, "ls") == 0) {
        return cmd_ls(cwd, args, argc, out, user_data);

    } else if (strcmp(cmd, "cd") == 0) {
        if (argc < 2) {
            /* "cd" without an argument goes home. */
            snprintf(current_path, current_path_size, "%s", path_home_dir());
            emit(out, user_data, "%s\n", current_path);
        } else {
            char target[FS_PATH_MAX];
            resolve_against(cwd, args[1], target, sizeof(target));

            FsRawStat st;
            if (fs_plat_lstat(target, &st) != 0) {
                emit(out, user_data, "cd: %s: %s\n", target,
                     fmt_status_message(fs_stat_util_status_from_errno(errno),
                                        errno));
                return FS_ERR_NOT_FOUND;
            }
            if (!FS_S_ISDIR(st.mode)) {
                emit(out, user_data, "cd: %s: Not a directory\n", target);
                return FS_ERR_NOT_DIR;
            }

            /* Store the canonical path so that ".." behaves predictably. */
            char resolved[FS_PATH_MAX];
            if (fs_plat_realpath(target, resolved, sizeof(resolved))) {
                snprintf(current_path, current_path_size, "%s", resolved);
            } else {
                snprintf(current_path, current_path_size, "%s", target);
            }
            emit(out, user_data, "%s\n", current_path);
        }

    } else if (strcmp(cmd, "pwd") == 0) {
        emit(out, user_data, "%s\n", current_path);

    } else if (strcmp(cmd, "tree") == 0) {
        return cmd_tree(cwd, args, argc, out, user_data);

    /* ---------------------------------------------------------------- *
     * Inspection
     * ---------------------------------------------------------------- */
    } else if (strcmp(cmd, "stat") == 0) {
        return cmd_stat(cwd, args, argc, out, user_data);

    } else if (strcmp(cmd, "info") == 0) {
        return cmd_info(cwd, args, argc, out, user_data);

    } else if (strcmp(cmd, "du") == 0) {
        return cmd_du(cwd, args, argc, out, user_data);

    } else if (strcmp(cmd, "find") == 0) {
        return cmd_find(cwd, args, argc, out, user_data);

    } else if (strcmp(cmd, "df") == 0) {
        return cmd_df(cwd, args, argc, out, user_data);

    } else if (strcmp(cmd, "readlink") == 0) {
        return cmd_readlink(cwd, args, argc, out, user_data);

    /* ---------------------------------------------------------------- *
     * Mutation
     * ---------------------------------------------------------------- */
    } else if (strcmp(cmd, "mkdir") == 0) {
        if (argc < 2) {
            emit(out, user_data, "mkdir: missing operand\n");
            return FS_ERR_INVALID_PATH;
        }
        int recursive = 0;
        const char *target_arg = NULL;
        for (int i = 1; i < argc; i++) {
            if (strcmp(args[i], "-p") == 0) {
                recursive = 1;
            } else {
                target_arg = args[i];
            }
        }
        if (target_arg == NULL) {
            emit(out, user_data, "mkdir: missing operand\n");
            return FS_ERR_INVALID_PATH;
        }

        char target[FS_PATH_MAX];
        resolve_against(cwd, target_arg, target, sizeof(target));

        FSStatus s = fs_mkdir(target, recursive);
        if (s != FS_OK) {
            emit(out, user_data, "mkdir: %s: %s\n", target,
                 fmt_status_message(s, errno));
            return s;
        }
        logger_append("mkdir %s", target);
        emit(out, user_data, "created directory %s\n", target);

    } else if (strcmp(cmd, "touch") == 0) {
        if (argc < 2) {
            emit(out, user_data, "touch: missing operand\n");
            return FS_ERR_INVALID_PATH;
        }
        char target[FS_PATH_MAX];
        resolve_against(cwd, args[1], target, sizeof(target));

        FSStatus s = fs_touch(target);
        if (s != FS_OK) {
            emit(out, user_data, "touch: %s: %s\n", target,
                 fmt_status_message(s, errno));
            return s;
        }
        logger_append("touch %s", target);
        emit(out, user_data, "created file %s\n", target);

    } else if (strcmp(cmd, "mv") == 0) {
        if (argc < 3) {
            emit(out, user_data, "mv: usage: mv <src> <dst>\n");
            return FS_ERR_INVALID_PATH;
        }
        char src[FS_PATH_MAX], dst[FS_PATH_MAX];
        resolve_against(cwd, args[1], src, sizeof(src));
        resolve_against(cwd, args[2], dst, sizeof(dst));

        FSStatus s = fs_rename(src, dst);
        if (s != FS_OK) {
            emit(out, user_data, "mv: %s -> %s: %s\n", src, dst,
                 fmt_status_message(s, errno));
            return s;
        }
        logger_append("mv %s %s", src, dst);
        emit(out, user_data, "renamed %s -> %s\n", src, dst);

    } else if (strcmp(cmd, "rm") == 0) {
        if (argc < 2) {
            emit(out, user_data, "rm: missing operand\n");
            return FS_ERR_INVALID_PATH;
        }
        char target[FS_PATH_MAX];
        resolve_against(cwd, args[1], target, sizeof(target));

        FSStatus s = fs_remove(target);
        if (s != FS_OK) {
            emit(out, user_data, "rm: %s: %s\n", target,
                 fmt_status_message(s, errno));
            return s;
        }
        logger_append("rm %s", target);
        emit(out, user_data, "removed %s\n", target);

    } else if (strcmp(cmd, "rmdir") == 0) {
        if (argc < 2) {
            emit(out, user_data, "rmdir: missing operand\n");
            return FS_ERR_INVALID_PATH;
        }
        char target[FS_PATH_MAX];
        resolve_against(cwd, args[1], target, sizeof(target));

        /* rmdir must refuse to delete a non-empty directory. */
        FsRawStat st;
        if (fs_plat_lstat(target, &st) != 0) {
            emit(out, user_data, "rmdir: %s: %s\n", target,
                 fmt_status_message(fs_stat_util_status_from_errno(errno),
                                    errno));
            return FS_ERR_NOT_FOUND;
        }
        if (!FS_S_ISDIR(st.mode)) {
            emit(out, user_data, "rmdir: %s: Not a directory\n", target);
            return FS_ERR_NOT_DIR;
        }

        FSStatus s = fs_remove(target);
        if (s != FS_OK) {
            emit(out, user_data, "rmdir: %s: %s\n", target,
                 fmt_status_message(s, errno));
            return s;
        }
        logger_append("rmdir %s", target);
        emit(out, user_data, "removed directory %s\n", target);

    } else if (strcmp(cmd, "ln") == 0) {
        if (argc < 4 || strcmp(args[1], "-s") != 0) {
            emit(out, user_data, "ln: usage: ln -s <target> <link>\n");
            return FS_ERR_INVALID_PATH;
        }
        char link[FS_PATH_MAX];
        resolve_against(cwd, args[3], link, sizeof(link));

        FSStatus s = fs_create_symlink(args[2], link);
        if (s != FS_OK) {
            emit(out, user_data, "ln: %s: %s\n", link,
                 fmt_status_message(s, errno));
            return s;
        }
        logger_append("ln -s %s %s", args[2], link);
        emit(out, user_data, "created symbolic link %s -> %s\n",
             link, args[2]);

    /* ---------------------------------------------------------------- *
     * Session
     * ---------------------------------------------------------------- */
    } else if (strcmp(cmd, "log") == 0) {
        static char buf[LOG_MSG_MAX * 8];
        size_t n = logger_recent(buf, sizeof(buf), 40);
        if (n == 0) {
            emit(out, user_data, "(activity log is empty)\n");
        } else {
            emit(out, user_data, "%s", buf);
        }

    } else if (strcmp(cmd, "clear") == 0) {
        logger_clear();
        emit(out, user_data, "activity log cleared\n");

    } else if (strcmp(cmd, "help") == 0 || strcmp(cmd, "?") == 0) {
        cli_print_help(out, user_data);

    } else {
        emit(out, user_data, "%s: unknown command (try 'help')\n", cmd);
        return FS_ERR_UNKNOWN;
    }

    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * cli_run_line
 *
 * Executes one command line against the process working directory.
 * ------------------------------------------------------------------ */
FSStatus
cli_run_line(const char *line, cli_writer_fn out, void *user_data)
{
    char cwd[FS_PATH_MAX];
    char *p = fs_get_cwd();
    if (p != NULL) {
        snprintf(cwd, sizeof(cwd), "%s", p);
        free(p);
    } else {
        snprintf(cwd, sizeof(cwd), "%s", path_home_dir());
    }

    return cli_run_line_with_state(line, cwd, sizeof(cwd), out, user_data);
}
