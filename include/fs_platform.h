/* ============================================================================
 * fs_platform.h -- Platform abstraction layer.
 *
 * The project targets POSIX (Linux) as its primary platform, but it must
 * also build on Windows (MSYS2 / MinGW-w64). This header isolates every
 * platform-dependent primitive behind a small, stable API so that the rest
 * of the codebase stays plain portable C11.
 *
 * Responsibilities:
 *   - Normalize the POSIX `struct stat` into `FsRawStat` so callers never
 *     touch `struct stat` directly.
 *   - Provide lstat / readlink / symlink / statvfs / fnmatch equivalents.
 *   - Provide owner / group name lookups.
 *   - Provide a portable home-directory lookup.
 *
 * Mode bits are always stored in the classic POSIX layout (same numeric
 * values as Linux), so `fmt_perm_symbolic()` needs no platform branches.
 * ============================================================================
 */

#ifndef FS_PLATFORM_H
#define FS_PLATFORM_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* ------------------------------------------------------------------ *
 * Platform detection
 * ------------------------------------------------------------------ */
#if defined(_WIN32) || defined(__MINGW32__) || defined(__MINGW64__)
#  define FS_PLATFORM_WINDOWS 1
#  define FS_PLATFORM_POSIX   0
#else
#  define FS_PLATFORM_POSIX   1
#  define FS_PLATFORM_WINDOWS 0
#endif

/* ------------------------------------------------------------------ *
 * Portable scalar types
 *
 * These replace uid_t / gid_t / nlink_t / blkcnt_t / ino_t, which do not
 * exist in the Windows C runtime.
 * ------------------------------------------------------------------ */
typedef unsigned int       FsUid;
typedef unsigned int       FsGid;
typedef unsigned long long FsNlink;
typedef unsigned long long FsBlkcnt;
typedef unsigned long long FsIno;

/* ------------------------------------------------------------------ *
 * Portable mode bits (identical numeric values to Linux)
 * ------------------------------------------------------------------ */
#define FS_S_IFMT   0170000u
#define FS_S_IFREG  0100000u
#define FS_S_IFDIR  0040000u
#define FS_S_IFLNK  0120000u
#define FS_S_IFCHR  0020000u
#define FS_S_IFBLK  0060000u
#define FS_S_IFIFO  0010000u
#define FS_S_IFSOCK 0140000u

#define FS_S_ISUID  0004000u
#define FS_S_ISGID  0002000u
#define FS_S_ISVTX  0001000u

#define FS_S_ISREG(m)  (((m) & FS_S_IFMT) == FS_S_IFREG)
#define FS_S_ISDIR(m)  (((m) & FS_S_IFMT) == FS_S_IFDIR)
#define FS_S_ISLNK(m)  (((m) & FS_S_IFMT) == FS_S_IFLNK)
#define FS_S_ISCHR(m)  (((m) & FS_S_IFMT) == FS_S_IFCHR)
#define FS_S_ISBLK(m)  (((m) & FS_S_IFMT) == FS_S_IFBLK)
#define FS_S_ISFIFO(m) (((m) & FS_S_IFMT) == FS_S_IFIFO)
#define FS_S_ISSOCK(m) (((m) & FS_S_IFMT) == FS_S_IFSOCK)

/* Permission bits (owner / group / other). */
#define FS_S_IRWXU  0700u
#define FS_S_IRWXG  0070u
#define FS_S_IRWXO  0007u

/* ------------------------------------------------------------------ *
 * FsRawStat -- platform-neutral stat result.
 *
 * `mode` carries the POSIX-style type + permission bits described above.
 * `is_symlink` is redundant with the S_ISLNK(mode) test but is kept so
 * that Windows junctions (which are not reported as S_IFLNK by every
 * tool) can be flagged explicitly.
 * ------------------------------------------------------------------ */
typedef struct {
    FsNlink  nlinks;
    FsIno    inode;
    FsUid    uid;
    FsGid    gid;
    FsBlkcnt blocks;      /* 512-byte blocks actually allocated */
    FsBlkcnt block_size;  /* preferred I/O block size */
    unsigned long long size;
    long long atime;
    long long mtime;
    long long ctime;
    unsigned int mode;
    int       is_symlink;
} FsRawStat;

/* ------------------------------------------------------------------ *
 * FsRawVfs -- platform-neutral statvfs result.
 * ------------------------------------------------------------------ */
typedef struct {
    unsigned long long block_size;
    unsigned long long total_blocks;
    unsigned long long free_blocks;       /* blocks available to superuser */
    unsigned long long available_blocks;  /* blocks available to this user */
    unsigned long long total_inodes;
    unsigned long long free_inodes;
} FsRawVfs;

/* ------------------------------------------------------------------ *
 * API
 * ------------------------------------------------------------------ */

/* stat() that does NOT follow symlinks.
 * Returns 0 on success, -1 on failure with errno set. */
int fs_plat_lstat(const char *path, FsRawStat *out);

/* stat() that DOES follow symlinks.
 * Returns 0 on success, -1 on failure with errno set. */
int fs_plat_stat(const char *path, FsRawStat *out);

/* Read the target of a symbolic link into `out` (NUL-terminated).
 * `out_size` includes the NUL.
 * Returns  0  on success
 *         -1  on failure (errno set; EINVAL if not a symlink,
 *              ENOENT if the link itself is missing)
 *         -2  if the platform cannot read link targets at all. */
int fs_plat_readlink(const char *path, char *out, size_t out_size);

/* Create a symbolic link at `link_path` pointing at `target`.
 * Returns 0 on success, -1 on failure (errno set). */
int fs_plat_symlink(const char *target, const char *link_path);

/* Retrieve file-system statistics for the file system containing `path`.
 * Returns 0 on success, -1 on failure (errno set). */
int fs_plat_statvfs(const char *path, FsRawVfs *out);

/* Canonicalize `path` into `out` (size `out_size`), resolving symbolic
 * links and collapsing "." / ".." components.
 * Returns true on success, false on failure (errno set). */
bool fs_plat_realpath(const char *path, char *out, size_t out_size);

/* Shell-style wildcard match.
 * Supports '*', '?' and '[...]' character classes.
 * Returns 1 when `string` matches `pattern`, 0 otherwise. */
int fs_plat_fnmatch(const char *pattern, const char *string);

/* Look up the human-readable user name for a numeric uid.
 * Writes into `out` (size `out_size`). Returns 0 on success, -1 if the
 * uid cannot be resolved (the caller should then print the numeric id). */
int fs_plat_user_name(FsUid uid, char *out, size_t out_size);

/* Look up the human-readable group name for a numeric gid. */
int fs_plat_group_name(FsGid gid, char *out, size_t out_size);

/* Copy the current user's home directory into `out` (size `out_size`).
 * Returns 0 on success, -1 on failure. */
int fs_plat_home_dir(char *out, size_t out_size);

/* Create a directory at `path` with the given POSIX permission bits.
 * Returns 0 on success, -1 on failure (errno set). */
int fs_plat_mkdir(const char *path, unsigned int mode);

/* Remove an empty directory at `path`.
 * Returns 0 on success, -1 on failure (errno set). */
int fs_plat_rmdir(const char *path);

/* Remove a file (or symbolic link) at `path`.
 * Returns 0 on success, -1 on failure (errno set). */
int fs_plat_unlink(const char *path);

/* Change the permission bits of `path`.
 * Returns 0 on success, -1 on failure (errno set). */
int fs_plat_chmod(const char *path, unsigned int mode);

/* Map an errno value onto an FSStatus-compatible category.
 * (Kept here so both platforms agree on the mapping.) */
#define FS_PLAT_ERR_NONE     0
#define FS_PLAT_ERR_NOTFOUND 1
#define FS_PLAT_ERR_PERM     2
#define FS_PLAT_ERR_NOTDIR   3
#define FS_PLAT_ERR_EXISTS   4
#define FS_PLAT_ERR_IO       5

#endif /* FS_PLATFORM_H */
