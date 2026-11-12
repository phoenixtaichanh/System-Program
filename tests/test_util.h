/* ============================================================================
 * test_util.h -- Small portable helpers shared by the test programs.
 *
 * The tests must build on both POSIX (Linux) and Windows (MSYS2), where
 * `mkdir()` has a different signature, `lstat()` does not exist and the
 * `S_IS*` macros are absent. These wrappers hide the difference so the test
 * bodies stay readable.
 * ============================================================================
 */

#ifndef TEST_UTIL_H
#define TEST_UTIL_H

#include "fs_platform.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef _WIN32
#  include <direct.h>
#  include <io.h>
#  define T_MKDIR_RAW(p) _mkdir(p)
#  define T_RMDIR_RAW(p) _rmdir(p)
#  define T_UNLINK_RAW(p) _unlink(p)
#else
#  include <unistd.h>
#  define T_MKDIR_RAW(p) mkdir((p), 0755)
#  define T_RMDIR_RAW(p) rmdir(p)
#  define T_UNLINK_RAW(p) unlink(p)
#endif

/* ------------------------------------------------------------------ *
 * Sandbox location
 *
 * The sandboxes live in the system temporary directory. On POSIX that is
 * "/tmp"; on Windows it is the value of %TEMP%. The path is built at run
 * time because the environment is not known at compile time.
 * ------------------------------------------------------------------ */

/* The root directory for the test sandboxes. */
static inline const char *
t_tmp_root(void)
{
#ifdef _WIN32
    static char buf[512];
    const char *e = getenv("TEMP");
    if (e == NULL || e[0] == '\0') e = getenv("TMP");
    if (e == NULL || e[0] == '\0') e = ".";

    snprintf(buf, sizeof(buf), "%s", e);
    size_t n = strlen(buf);
    while (n > 1 && (buf[n - 1] == '\\' || buf[n - 1] == '/')) {
        buf[--n] = '\0';
    }
    return buf;
#else
    return "/tmp";
#endif
}

/* The absolute path of the sandbox of the running test program.
 *
 * Each test program calls t_sandbox_init("name") once at start-up. The
 * buffer is a plain variable (not a function) so that it can be used
 * wherever a string is expected, for example:
 *
 *     snprintf(cmd, sizeof(cmd), "cd \"%s\" && ls", t_sandbox_path());
 */
static char t_sandbox_buf[512] = "";

/* The sandbox path as a string. */
static inline const char *
t_sandbox_path(void)
{
    return t_sandbox_buf;
}

/* Point the sandbox at <tmp root>/fse_test_<name>.
 *
 * The native directory separator is used so that the path can be compared
 * with the result of getcwd(), which also uses the native separator. */
static inline void
t_sandbox_init(const char *name)
{
#ifdef _WIN32
    snprintf(t_sandbox_buf, sizeof(t_sandbox_buf), "%s\\fse_test_%s",
             t_tmp_root(), name);
#else
    snprintf(t_sandbox_buf, sizeof(t_sandbox_buf), "%s/fse_test_%s",
             t_tmp_root(), name);
#endif
}

/* A path inside the sandbox, for example t_sandbox_sub("sub/file.txt").
 * The returned pointer stays valid until the next call. */
static inline const char *
t_sandbox_sub(const char *rel)
{
    static char buf[1024];
#ifdef _WIN32
    snprintf(buf, sizeof(buf), "%s\\%s", t_sandbox_path(), rel);
#else
    snprintf(buf, sizeof(buf), "%s/%s", t_sandbox_path(), rel);
#endif
    return buf;
}

/* ------------------------------------------------------------------ *
 * Directory creation
 * ------------------------------------------------------------------ */

/* Create a directory (the result is ignored). */
static inline void
t_mkdir(const char *path)
{
    (void)fs_plat_mkdir(path, 0755);
}

/* ------------------------------------------------------------------ *
 * Removal
 * ------------------------------------------------------------------ */

/* Remove a file or a symbolic link. */
static inline void
t_unlink(const char *path)
{
    (void)fs_plat_unlink(path);
}

/* Remove an empty directory. */
static inline void
t_rmdir(const char *path)
{
    (void)fs_plat_rmdir(path);
}

/* Remove a directory and everything below it.
 * The result is ignored. */
static inline void
t_rmtree(const char *path)
{
    DIR *d = opendir(path);
    if (d == NULL) {
        (void)T_UNLINK_RAW(path);
        return;
    }

    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0) {
            continue;
        }

        char child[2048];
#ifdef _WIN32
        snprintf(child, sizeof(child), "%s\\%s", path, de->d_name);
#else
        snprintf(child, sizeof(child), "%s/%s", path, de->d_name);
#endif

        FsRawStat st;
        if (fs_plat_lstat(child, &st) == 0 && FS_S_ISDIR(st.mode)) {
            t_rmtree(child);
        } else {
            (void)T_UNLINK_RAW(child);
        }
    }
    closedir(d);

    (void)T_RMDIR_RAW(path);
}

/* ------------------------------------------------------------------ *
 * Inspection
 * ------------------------------------------------------------------ */

/* lstat() into a platform-neutral structure.
 * Returns 0 on success, -1 on failure. */
static inline int
t_lstat(const char *path, FsRawStat *out)
{
    return fs_plat_lstat(path, out);
}

/* True when `path` exists. */
static inline int
t_exists(const char *path)
{
    FsRawStat st;
    return fs_plat_lstat(path, &st) == 0;
}

/* True when `path` is a symbolic link. */
static inline int
t_is_symlink(const char *path)
{
    FsRawStat st;
    if (fs_plat_lstat(path, &st) != 0) return 0;
    return (st.is_symlink || FS_S_ISLNK(st.mode)) ? 1 : 0;
}

/* True when `path` is a directory. */
static inline int
t_is_dir(const char *path)
{
    FsRawStat st;
    if (fs_plat_lstat(path, &st) != 0) return 0;
    return FS_S_ISDIR(st.mode) ? 1 : 0;
}

/* True when `path` is a regular file. */
static inline int
t_is_reg(const char *path)
{
    FsRawStat st;
    if (fs_plat_lstat(path, &st) != 0) return 0;
    return FS_S_ISREG(st.mode) ? 1 : 0;
}

/* ------------------------------------------------------------------ *
 * The effective user id
 * ------------------------------------------------------------------ */

/* The effective user id, or 0 when the platform cannot tell. */
static inline unsigned int
t_geteuid(void)
{
#ifdef _WIN32
    /* Windows has no uid concept; report "not root" so the permission
     * test is not skipped for the wrong reason. */
    return 1000;
#else
    return (unsigned int)geteuid();
#endif
}

/* ------------------------------------------------------------------ *
 * Symbolic links
 * ------------------------------------------------------------------ */

/* Create a symbolic link; returns 0 on success, -1 on failure. */
static inline int
t_symlink(const char *target, const char *link_path)
{
    return fs_plat_symlink(target, link_path);
}

/* True when the current user is allowed to create symbolic links.
 *
 * On Windows this requires either Administrator rights or the
 * "SeCreateSymbolicLinkPrivilege" (Developer Mode). The probe creates a
 * link in the current directory and removes it again. */
static inline int
t_can_symlink(void)
{
    static int cached = -1;
    if (cached >= 0) return cached;

    const char *probe = "_fse_symlink_probe";
    t_unlink(probe);

    if (t_symlink("probe_target", probe) == 0) {
        t_unlink(probe);
        cached = 1;
    } else {
        cached = 0;
    }
    return cached;
}

/* ------------------------------------------------------------------ *
 * Files
 * ------------------------------------------------------------------ */

/* Write `text` to `path` in binary mode.
 *
 * Binary mode matters on Windows: in text mode the C runtime turns every
 * "\n" into "\r\n", which changes the file size and would make the size
 * assertions fail. */
static inline int
t_write_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb");
    if (f == NULL) return -1;
    if (text != NULL) fputs(text, f);
    fclose(f);
    return 0;
}

#endif /* TEST_UTIL_H */
