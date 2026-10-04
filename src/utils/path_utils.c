/* ============================================================================
 * path_utils.c -- Path normalization & composition helpers.
 *
 * Implementation of:
 *   - path_is_absolute
 *   - path_home_dir
 *   - path_join
 *   - path_basename
 *   - path_dirname
 *   - path_normalize   (no symlink resolution)
 *   - path_resolve     (realpath; follows symlinks)
 *
 * Designed for Linux/POSIX only. Buffers are caller-provided so we can keep
 * allocation predictable.
 * ============================================================================
 */

#include "path_utils.h"

#include <pwd.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <errno.h>
#include <libgen.h>     /* POSIX basename()/dirname() - may modify input */
#include <sys/stat.h>

/* ------------------------------------------------------------------ *
 * path_is_absolute
 *   Returns true if `path` starts with '/'. Empty path is NOT absolute.
 * ------------------------------------------------------------------ */
bool
path_is_absolute(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        return false;
    }
    return path[0] == '/';
}

/* ------------------------------------------------------------------ *
 * path_home_dir
 *   Returns a stable pointer to the user's home directory. We prefer
 *   getenv("HOME") if it is set, otherwise fall back to getpwuid(getuid()).
 * ------------------------------------------------------------------ */
const char *
path_home_dir(void)
{
    const char *env = getenv("HOME");
    if (env != NULL && env[0] != '\0') {
        return env;
    }

    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL && pw->pw_dir[0] != '\0') {
        /* Cache the value inside the passwd struct; it persists for the
         * lifetime of the process in the glibc implementation, but we copy
         * into a static buffer to be safe against getpwuid() reuse. */
        static char buf[FS_PATH_MAX];
        snprintf(buf, sizeof(buf), "%s", pw->pw_dir);
        return buf;
    }

    return "/";
}

/* ------------------------------------------------------------------ *
 * path_join
 *   Joins two components with a single '/' between them.
 *   - If `base` is empty or NULL, returns a copy of `leaf`.
 *   - If `base` is absolute AND `leaf` is absolute, the result is `leaf`
 *     (i.e. the second absolute path wins, like POSIX file APIs).
 *   - Otherwise concatenates them, collapsing any duplicate slashes.
 * ------------------------------------------------------------------ */
bool
path_join(const char *base, const char *leaf, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        return false;
    }

    if (base == NULL) base = "";
    if (leaf == NULL) leaf = "";

    /* If leaf is absolute, just copy it. */
    if (path_is_absolute(leaf)) {
        int n = snprintf(out, out_size, "%s", leaf);
        return n > 0 && (size_t)n < out_size;
    }

    /* If base is empty, copy leaf alone. */
    if (base[0] == '\0') {
        int n = snprintf(out, out_size, "%s", leaf);
        return n > 0 && (size_t)n < out_size;
    }

    /* Strip trailing '/' from base so "/a/" + "b" becomes "/a/b". */
    size_t base_len = strlen(base);
    while (base_len > 1 && base[base_len - 1] == '/') {
        base_len--;
    }

    /* Empty leaf: return stripped base (no extra separator). */
    if (leaf[0] == '\0') {
        if (base_len + 1 > out_size) return false;
        memcpy(out, base, base_len);
        out[base_len] = '\0';
        return true;
    }

    int need_sep = (base_len > 0 && base[base_len - 1] != '/') ? 1 : 0;

    int n = snprintf(out, out_size, "%.*s%s%s",
                     (int)base_len, base,
                     need_sep ? "/" : "",
                     leaf);
    return n > 0 && (size_t)n < out_size;
}

/* ------------------------------------------------------------------ *
 * path_basename
 *   Returns the trailing name component. Uses a thread-local static
 *   buffer because POSIX basename() may modify its input and the
 *   glibc version of POSIX basename() returns a pointer into the input
 *   itself, which makes it dangerous to call repeatedly.
 * ------------------------------------------------------------------ */
const char *
path_basename(const char *path)
{
    static char buf[FS_PATH_MAX];

    if (path == NULL) {
        buf[0] = '\0';
        return buf;
    }

    /* Find the last '/' and copy everything after it.
     * Strip trailing slashes (overwrite with '\0' so the source string
     * also doesn't leak them into snprintf) so "/a/b/" returns "b" (not
     * "b/"). Then find the LAST '/' within the stripped prefix, so
     * "/a/b/c" -> "c" (not "a/b/c").
     * NOTE: we must take a writable copy because the input `path` is
     * const; the static `buf` doubles as scratch space. */
    char *scratch = buf;
    size_t len = strlen(path);
    if (len >= sizeof(buf)) len = sizeof(buf) - 1;
    memcpy(scratch, path, len);
    scratch[len] = '\0';
    while (len > 0 && scratch[len - 1] == '/') {
        scratch[--len] = '\0';
    }
    if (len == 0) {
        buf[0] = '\0';
        return buf;
    }
    const char *p = NULL;
    for (size_t i = len; i > 0; i--) {
        if (scratch[i - 1] == '/') { p = scratch + (i - 1); break; }
    }
    const char *name = (p != NULL) ? p + 1 : scratch;
    size_t name_len = (p != NULL) ? (size_t)(scratch + len - name)
                                  : len;
    memcpy(buf, name, name_len);
    buf[name_len] = '\0';
    return buf;
}

/* ------------------------------------------------------------------ *
 * path_dirname
 *   Writes everything before the trailing '/' into `out`.
 *   Special cases:
 *     - ""           -> "."
 *     - "/"          -> "/"
 *     - "name"       -> "."
 *     - "/name"      -> "/"
 *     - "/a/b/c"     -> "/a/b"
 * ------------------------------------------------------------------ */
bool
path_dirname(const char *path, char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0) {
        return false;
    }

    if (path[0] == '\0') {
        return snprintf(out, out_size, ".") > 0;
    }

    /* Find the last '/'. */
    const char *slash = strrchr(path, '/');

    if (slash == NULL) {
        /* No slash at all -> dirname is "." */
        return snprintf(out, out_size, ".") > 0;
    }

    if (slash == path) {
        /* Slash is the first character -> dirname is "/" */
        return snprintf(out, out_size, "/") > 0;
    }

    /* Otherwise, write up to (not including) the last slash. */
    size_t len = (size_t)(slash - path);
    if (len >= out_size) {
        return false;       /* buffer too small */
    }
    memcpy(out, path, len);
    out[len] = '\0';
    return true;
}

/* ------------------------------------------------------------------ *
 * path_normalize
 *   In-place normalization: collapse "//", resolve "." and ".." segments.
 *   Does NOT follow symlinks. Algorithm:
 *     1. Tokenize by '/'.
 *     2. Maintain a stack of components.
 *     3. For "..", pop if stack non-empty (and top is not "..").
 *     4. For ".", skip.
 *     5. Otherwise push.
 * ------------------------------------------------------------------ */
bool
path_normalize(char *path, size_t path_size)
{
    if (path == NULL || path_size == 0) {
        return false;
    }

    size_t in_len = strlen(path);
    if (in_len >= path_size) {
        return false;       /* not NUL-terminated within buffer */
    }

    /* Empty path -> "." */
    if (in_len == 0) {
        if (path_size < 2) return false;
        path[0] = '.';
        path[1] = '\0';
        return true;
    }

    /* Copy input into a scratch buffer so we can tokenize it. */
    char *scratch = malloc(in_len + 1);
    if (scratch == NULL) {
        return false;
    }
    memcpy(scratch, path, in_len + 1);

    /* Stack of component start positions in the scratch buffer. */
    char *stack[FS_PATH_MAX / 2 + 1];
    int  sp = 0;
    bool is_absolute = (path[0] == '/');

    char *saveptr = NULL;
    char *tok = strtok_r(scratch, "/", &saveptr);

    while (tok != NULL) {
        if (strcmp(tok, ".") == 0) {
            /* skip */
        } else if (strcmp(tok, "..") == 0) {
            if (sp > 0 && strcmp(stack[sp - 1], "..") != 0) {
                sp--;                /* pop last component */
            } else if (!is_absolute) {
                stack[sp++] = tok;   /* keep leading ".." for relative paths */
            }
            /* if absolute and stack is empty, just drop the ".." */
        } else {
            if (sp >= (int)(sizeof(stack) / sizeof(stack[0]))) {
                free(scratch);
                return false;
            }
            stack[sp++] = tok;
        }
        tok = strtok_r(NULL, "/", &saveptr);
    }

    /* Rebuild the normalized path. */
    char out[FS_PATH_MAX];
    size_t pos = 0;
    if (is_absolute) {
        out[pos++] = '/';
    }

    for (int i = 0; i < sp; i++) {
        size_t len = strlen(stack[i]);
        /* Need room for component + optional '/' + '\0' */
        if (pos + len + 2 > sizeof(out)) {
            free(scratch);
            return false;
        }
        if (i > 0 || (is_absolute && pos == 1 && sp == 0)) {
            /* nothing - keep current state */
        }
        if (i > 0) {
            out[pos++] = '/';
        }
        memcpy(out + pos, stack[i], len);
        pos += len;
    }

    if (pos == 0) {
        /* No components at all */
        out[pos++] = '.';
    }
    out[pos] = '\0';

    /* Copy back to the caller's buffer. */
    if (pos + 1 > path_size) {
        free(scratch);
        return false;
    }
    memcpy(path, out, pos + 1);

    free(scratch);
    return true;
}

/* ------------------------------------------------------------------ *
 * path_resolve
 *   Resolves `path` against `base` (if path is relative) and then calls
 *   realpath() to canonicalize (following all symlinks). The canonical
 *   absolute path is written into `out`. If path is absolute, base is
 *   ignored.
 * ------------------------------------------------------------------ */
bool
path_resolve(const char *base, const char *path,
             char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0) {
        return false;
    }

    char combined[FS_PATH_MAX];
    const char *use = path;

    if (!path_is_absolute(path)) {
        if (!path_join(base ? base : ".", path,
                       combined, sizeof(combined))) {
            return false;
        }
        use = combined;
    }

    char *resolved = realpath(use, NULL);
    if (resolved == NULL) {
        /* realpath failed (e.g. EACCES, ENOENT). The caller can check
         * errno. We still write a best-effort absolute path. */
        snprintf(out, out_size, "%s", use);
        return false;
    }

    bool ok = snprintf(out, out_size, "%s", resolved) > 0
              && strlen(resolved) < out_size;
    free(resolved);
    return ok;
}