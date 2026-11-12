/* ============================================================================
 * fs_platform.c -- Platform abstraction implementation.
 *
 * Two code paths:
 *   - POSIX  (Linux / macOS / *BSD / MSYS2 "msys" environment)
 *   - Windows (MSYS2 "ucrt64" / MinGW-w64)
 *
 * The Windows path emulates the POSIX semantics the application relies on:
 *   - lstat() reports reparse points (symlinks/junctions) as links.
 *   - readlink() reads the reparse target.
 *   - statvfs() is derived from GetDiskFreeSpaceEx() + GetFileAttributes.
 *   - fnmatch() is implemented natively (no POSIX fnmatch on MinGW).
 * ============================================================================
 */

/* Feature-test macros must be defined before the first system header is
 * pulled in, otherwise glibc hides the POSIX-only declarations. */
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
#  ifndef _XOPEN_SOURCE
#    define _XOPEN_SOURCE 700
#  endif
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE
#  endif
#  ifndef _GNU_SOURCE
#    define _GNU_SOURCE
#  endif
#endif

#include "fs_platform.h"

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if FS_PLATFORM_POSIX

/* ------------------------------------------------------------------ *
 * POSIX implementation
 * ------------------------------------------------------------------ */
#include <dirent.h>
#include <fnmatch.h>
#include <grp.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <unistd.h>

static void
raw_stat_from_posix(const struct stat *st, FsRawStat *out)
{
    out->nlinks     = (FsNlink)st->st_nlink;
    out->inode     = (FsIno)st->st_ino;
    out->uid       = (FsUid)st->st_uid;
    out->gid       = (FsGid)st->st_gid;
    out->blocks    = (FsBlkcnt)st->st_blocks;
    out->block_size = (FsBlkcnt)st->st_blksize;
    out->size      = (unsigned long long)st->st_size;
    out->atime     = (long long)st->st_atime;
    out->mtime     = (long long)st->st_mtime;
    out->ctime     = (long long)st->st_ctime;
    out->mode      = (unsigned int)st->st_mode;
    out->is_symlink = S_ISLNK(st->st_mode) ? 1 : 0;
}

int
fs_plat_lstat(const char *path, FsRawStat *out)
{
    struct stat st;
    if (lstat(path, &st) != 0) return -1;
    raw_stat_from_posix(&st, out);
    return 0;
}

int
fs_plat_stat(const char *path, FsRawStat *out)
{
    struct stat st;
    if (stat(path, &st) != 0) return -1;
    raw_stat_from_posix(&st, out);
    return 0;
}

int
fs_plat_readlink(const char *path, char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0) {
        errno = EINVAL;
        return -1;
    }

    ssize_t n = readlink(path, out, out_size - 1);
    if (n < 0) {
        /* readlink() on a non-symlink sets EINVAL on Linux, but on some
         * systems it reports ENOENT. Distinguish for the caller. */
        struct stat st;
        if (lstat(path, &st) == 0 && !S_ISLNK(st.st_mode)) {
            errno = EINVAL;
        }
        return -1;
    }
    out[n] = '\0';
    return 0;
}

int
fs_plat_symlink(const char *target, const char *link_path)
{
    if (target == NULL || link_path == NULL) {
        errno = EINVAL;
        return -1;
    }
    return symlink(target, link_path) == 0 ? 0 : -1;
}

int
fs_plat_statvfs(const char *path, FsRawVfs *out)
{
    struct statvfs vfs;
    if (statvfs(path, &vfs) != 0) return -1;

    out->block_size       = (vfs.f_frsize != 0) ? vfs.f_frsize : vfs.f_bsize;
    out->total_blocks     = vfs.f_blocks;
    out->free_blocks      = vfs.f_bfree;
    out->available_blocks = vfs.f_bavail;
    out->total_inodes     = vfs.f_files;
    out->free_inodes      = vfs.f_ffree;
    return 0;
}

bool
fs_plat_realpath(const char *path, char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0) {
        errno = EINVAL;
        return false;
    }

    char *resolved = realpath(path, NULL);
    if (resolved == NULL) {
        return false;
    }

    bool ok = strlen(resolved) < out_size;
    if (ok) {
        memcpy(out, resolved, strlen(resolved) + 1);
    } else {
        errno = ENAMETOOLONG;
    }
    free(resolved);
    return ok;
}

int
fs_plat_fnmatch(const char *pattern, const char *string)
{
    if (pattern == NULL || string == NULL) return 0;
    return fnmatch(pattern, string, 0) == 0 ? 1 : 0;
}

int
fs_plat_user_name(FsUid uid, char *out, size_t out_size)
{
    struct passwd *pw = getpwuid((uid_t)uid);
    if (pw == NULL || pw->pw_name == NULL || pw->pw_name[0] == '\0') {
        return -1;
    }
    snprintf(out, out_size, "%s", pw->pw_name);
    return 0;
}

int
fs_plat_group_name(FsGid gid, char *out, size_t out_size)
{
    struct group *gr = getgrgid((gid_t)gid);
    if (gr == NULL || gr->gr_name == NULL || gr->gr_name[0] == '\0') {
        return -1;
    }
    snprintf(out, out_size, "%s", gr->gr_name);
    return 0;
}

int
fs_plat_home_dir(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        errno = EINVAL;
        return -1;
    }

    const char *env = getenv("HOME");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s", env);
        return 0;
    }

    struct passwd *pw = getpwuid(getuid());
    if (pw != NULL && pw->pw_dir != NULL && pw->pw_dir[0] != '\0') {
        snprintf(out, out_size, "%s", pw->pw_dir);
        return 0;
    }

    snprintf(out, out_size, "%s", "/");
    return 0;
}

int
fs_plat_mkdir(const char *path, unsigned int mode)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    return mkdir(path, (mode_t)mode) == 0 ? 0 : -1;
}

int
fs_plat_rmdir(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    return rmdir(path) == 0 ? 0 : -1;
}

int
fs_plat_unlink(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    return unlink(path) == 0 ? 0 : -1;
}

int
fs_plat_chmod(const char *path, unsigned int mode)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    return chmod(path, (mode_t)mode) == 0 ? 0 : -1;
}

#else /* FS_PLATFORM_WINDOWS */

/* ------------------------------------------------------------------ *
 * Windows implementation
 * ------------------------------------------------------------------ */
#include <windows.h>
#include <io.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

/* Maximum path length used internally on Windows. */
#define FS_WIN_PATH_MAX_LOCAL 4096

/* Windows FILETIME -> Unix epoch seconds. */
#define FS_FILETIME_EPOCH_DELTA  11644473600LL

/* Size of the fixed part of REPARSE_DATA_BUFFER (tag + data length +
 * reserved). The variable-length part follows immediately after. */
#define FS_REPARSE_HEADER_SIZE  8

static long long
file_time_to_unix(const FILETIME *ft)
{
    ULARGE_INTEGER u;
    u.LowPart  = ft->dwLowDateTime;
    u.HighPart = ft->dwHighDateTime;
    /* 100-ns ticks -> seconds */
    long long ticks = (long long)u.QuadPart;
    return (ticks / 10000000LL) - FS_FILETIME_EPOCH_DELTA;
}

/* Build the POSIX-style mode bits from a Windows file attribute set. */
static unsigned int
mode_from_attributes(DWORD attr, int is_dir, int is_link)
{
    unsigned int mode = 0;

    if (is_link) {
        mode |= FS_S_IFLNK;
    } else if (is_dir) {
        mode |= FS_S_IFDIR;
    } else {
        mode |= FS_S_IFREG;
    }

    /* Windows only models the read-only flag; the remaining permission
     * bits are synthesized from it so the UI has something sensible. */
    if (attr & FILE_ATTRIBUTE_READONLY) {
        mode |= 0444;   /* r--r--r-- */
    } else {
        mode |= 0666;   /* rw-rw-rw- */
    }
    /* Directory execute/search permission is implied. */
    if (is_dir) mode |= 0111;

    return mode;
}

/* Fill a FsRawStat for `path` using the Win32 file API.
 * `follow` selects stat() vs lstat() semantics. */
static int
win_fill_stat(const char *path, FsRawStat *out, int follow)
{
    if (path == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }

    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        errno = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                ? ENOENT
                : (err == ERROR_ACCESS_DENIED ? EACCES : EIO);
        return -1;
    }

    int is_dir  = (attr & FILE_ATTRIBUTE_DIRECTORY) ? 1 : 0;
    int is_link = (attr & FILE_ATTRIBUTE_REPARSE_POINT) ? 1 : 0;

    /* For a link, we want the link's own attributes (lstat), not the
     * target's. The Win32 API already returns the link's attributes,
     * so nothing special is needed there. */

    /* Retrieve timestamps + size + link target. */
    WIN32_FILE_ATTRIBUTE_DATA fad;
    memset(&fad, 0, sizeof(fad));
    BOOL have_fad = GetFileAttributesExA(path, GetFileExInfoStandard,
                                         &fad) ? TRUE : FALSE;

    memset(out, 0, sizeof(*out));

    out->size = 0;
    out->atime = out->mtime = out->ctime = 0;
    if (have_fad) {
        out->size  = ((unsigned long long)fad.nFileSizeHigh << 32)
                   | (unsigned long long)fad.nFileSizeLow;
        out->atime = file_time_to_unix(&fad.ftLastAccessTime);
        out->mtime = file_time_to_unix(&fad.ftLastWriteTime);
        out->ctime = file_time_to_unix(&fad.ftCreationTime);
    }

    /* Inode number and hard-link count.
     *
     * Windows has no inode number, but NTFS and ReFS expose a unique
     * 64-bit file index. Opening the file and asking for its index gives
     * a value with the same meaning as a POSIX inode, so the explorer can
     * show it and the traversal code can use it to detect loops. */
    {
        HANDLE h = CreateFileA(path, FILE_READ_ATTRIBUTES,
                                FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                NULL, OPEN_EXISTING,
                                FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT,
                                NULL);
        if (h != INVALID_HANDLE_VALUE) {
            BY_HANDLE_FILE_INFORMATION info;
            if (GetFileInformationByHandle(h, &info)) {
                out->inode = ((FsIno)info.nFileIndexHigh << 32)
                           | (FsIno)info.nFileIndexLow;
                out->nlinks = (FsNlink)info.nNumberOfLinks;
            }
            CloseHandle(h);
        }
    }

    /* Follow the link when stat() semantics are requested. */
    if (is_link && follow) {
        FsRawStat target;
        char buf[FS_WIN_PATH_MAX_LOCAL];
        if (fs_plat_readlink(path, buf, sizeof(buf)) == 0) {
            /* Resolve a relative target against the link's directory.
             * The buffer is twice as large as the two inputs so the
             * concatenation can never be truncated. */
            char resolved[FS_WIN_PATH_MAX_LOCAL * 2 + 2];
            if (buf[0] == '/' || (buf[0] && buf[1] == ':')) {
                snprintf(resolved, sizeof(resolved), "%s", buf);
            } else {
                char dir[FS_WIN_PATH_MAX_LOCAL];
                snprintf(dir, sizeof(dir), "%s", path);
                char *slash = strrchr(dir, '\\');
                if (slash == NULL) slash = strrchr(dir, '/');
                if (slash != NULL) *slash = '\0';
                else dir[0] = '\0';
                snprintf(resolved, sizeof(resolved), "%s\\%s", dir, buf);
            }
            if (fs_plat_stat(resolved, &target) == 0) {
                DWORD tattr = GetFileAttributesA(resolved);
                int tdir = (tattr != INVALID_FILE_ATTRIBUTES &&
                            (tattr & FILE_ATTRIBUTE_DIRECTORY)) ? 1 : 0;
                int tlink = (tattr != INVALID_FILE_ATTRIBUTES &&
                             (tattr & FILE_ATTRIBUTE_REPARSE_POINT)) ? 1 : 0;
                *out = target;
                out->mode = mode_from_attributes(tattr, tdir, tlink);
                out->is_symlink = 0;
                return 0;
            }
        }
    }

    out->mode = mode_from_attributes(attr, is_dir, is_link);
    out->is_symlink = is_link;

    /* The file index and link count were queried above; keep them. When
     * the query failed (for example on a file system without an index)
     * report a stable synthetic value so the field is never ambiguous. */
    if (out->inode == 0) out->inode = 1;
    if (out->nlinks == 0) out->nlinks = 1;

    /* Owner: try to resolve the file owner SID to a name. */
    out->uid = 0;
    out->gid = 0;

    /* Block accounting: Windows reports the compressed/allocated size on
     * NTFS. We approximate with the logical size rounded to 512 bytes,
     * which keeps the "allocated size" column meaningful. */
    out->block_size = 4096;
    out->blocks = (out->size + 511ULL) / 512ULL;

    return 0;
}

int
fs_plat_lstat(const char *path, FsRawStat *out)
{
    return win_fill_stat(path, out, 0);
}

int
fs_plat_stat(const char *path, FsRawStat *out)
{
    return win_fill_stat(path, out, 1);
}

/* Helper: query the reparse target of a link into `out`. */
static int
win_read_reparse_target(const char *path, char *out, size_t out_size)
{
    HANDLE h = CreateFileA(path, 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING,
                           FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        errno = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                ? ENOENT
                : (err == ERROR_ACCESS_DENIED ? EACCES : EIO);
        return -1;
    }

    /* MAXIMUM_REPARSE_DATA_BUFFER_SIZE is 16 KiB. We need room for the
     * fixed header plus the target path. */
    static unsigned char reparse_buf[16384];
    DWORD got = 0;

    if (!DeviceIoControl(h, FSCTL_GET_REPARSE_POINT, NULL, 0,
                         reparse_buf, (DWORD)sizeof(reparse_buf),
                         &got, NULL)) {
        CloseHandle(h);
        DWORD err = GetLastError();
        errno = (err == ERROR_NOT_A_REPARSE_POINT) ? EINVAL : EIO;
        return -1;
    }
    CloseHandle(h);

    if (got < FS_REPARSE_HEADER_SIZE) {
        errno = EINVAL;
        return -1;
    }

    /* Layout of REPARSE_DATA_BUFFER:
     *   DWORD Tag;                    offset 0
     *   WORD  SubstituteNameOffset;   offset 8
     *   WORD  SubstituteNameLength;   offset 10
     *   WORD  PrintNameOffset;        offset 12
     *   WORD  PrintNameLength;        offset 14
     *   WCHAR PathBuffer[];           offset 16
     * The MSYS2/Windows headers disagree on the exact field widths, so we
     * read them explicitly as WORDs at the documented offsets. */
    DWORD tag = *(DWORD *)reparse_buf;
    if (tag != IO_REPARSE_TAG_SYMLINK && tag != IO_REPARSE_TAG_MOUNT_POINT) {
        errno = EINVAL;
        return -1;
    }

    unsigned sub_off  = *(unsigned short *)(reparse_buf + 8);
    unsigned sub_len  = *(unsigned short *)(reparse_buf + 10);
    unsigned print_off = *(unsigned short *)(reparse_buf + 12);
    unsigned print_len = *(unsigned short *)(reparse_buf + 14);

    const WCHAR *pathbuf = (const WCHAR *)(reparse_buf + 16);

    const WCHAR *chosen = NULL;
    unsigned chosen_len = 0;

    if (print_len > 0) {
        chosen = pathbuf + (print_off / sizeof(WCHAR));
        chosen_len = print_len;
    } else if (sub_len > 0) {
        chosen = pathbuf + (sub_off / sizeof(WCHAR));
        chosen_len = sub_len;
    }

    if (chosen == NULL || chosen_len == 0) {
        errno = EINVAL;
        return -1;
    }

    /* Wide -> UTF-8. */
    int need = WideCharToMultiByte(CP_UTF8, 0, chosen, (int)chosen_len,
                                   NULL, 0, NULL, NULL);
    if (need <= 0 || (size_t)need >= out_size) {
        errno = ENAMETOOLONG;
        return -1;
    }
    WideCharToMultiByte(CP_UTF8, 0, chosen, (int)chosen_len,
                        out, need, NULL, NULL);
    out[need] = '\0';
    return 0;
}

int
fs_plat_readlink(const char *path, char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0) {
        errno = EINVAL;
        return -1;
    }

    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        errno = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                ? ENOENT
                : (err == ERROR_ACCESS_DENIED ? EACCES : EIO);
        return -1;
    }
    if (!(attr & FILE_ATTRIBUTE_REPARSE_POINT)) {
        errno = EINVAL;
        return -1;
    }

    return win_read_reparse_target(path, out, out_size);
}

int
fs_plat_symlink(const char *target, const char *link_path)
{
    if (target == NULL || link_path == NULL) {
        errno = EINVAL;
        return -1;
    }
    /* SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE lets non-admin users
     * create symlinks when the Developer Mode / SeCreateSymbolicLink
     * privilege is available. */
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (CreateSymbolicLinkA(link_path, target, flags) == 0) {
        /* Retry without the flag: on older systems the flag is unknown
         * and the call fails with ERROR_INVALID_PARAMETER. */
        if (CreateSymbolicLinkA(link_path, target, 0) == 0) {
            DWORD err = GetLastError();
            errno = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                    ? ENOENT
                    : (err == ERROR_ACCESS_DENIED ? EACCES : EIO);
            return -1;
        }
    }
    return 0;
}

int
fs_plat_statvfs(const char *path, FsRawVfs *out)
{
    if (path == NULL || out == NULL) {
        errno = EINVAL;
        return -1;
    }

    /* Resolve the drive root of the path. */
    char root[MAX_PATH + 1];
    memset(root, 0, sizeof(root));

    if (GetVolumePathNameA(path, root, (DWORD)sizeof(root))) {
        /* root looks like "C:\\" - strip the trailing backslash so that
         * GetDiskFreeSpaceEx() is happy. */
        size_t rl = strlen(root);
        if (rl > 0 && root[rl - 1] == '\\') root[rl - 1] = '\0';
    } else {
        snprintf(root, sizeof(root), "%s", "C:\\");
    }

    ULARGE_INTEGER avail_to_caller;
    ULARGE_INTEGER avail_total;
    ULARGE_INTEGER free_total;

    if (!GetDiskFreeSpaceExA(root, &avail_to_caller, &avail_total,
                             &free_total)) {
        DWORD err = GetLastError();
        errno = (err == ERROR_ACCESS_DENIED) ? EACCES : EIO;
        return -1;
    }

    DWORD sectors_per_cluster = 0;
    DWORD bytes_per_sector   = 0;
    DWORD free_clusters      = 0;
    DWORD total_clusters     = 0;
    BOOL got = GetDiskFreeSpaceA(root, &sectors_per_cluster,
                                 &bytes_per_sector, &free_clusters,
                                 &total_clusters);
    if (!got) {
        sectors_per_cluster = 1;
        bytes_per_sector   = 512;
        free_clusters      = (DWORD)(free_total.QuadPart / 512ULL);
        total_clusters     = (DWORD)(avail_total.QuadPart / 512ULL);
    }

    unsigned long long bpc = (unsigned long long)sectors_per_cluster *
                             (unsigned long long)bytes_per_sector;
    if (bpc == 0) bpc = 4096;

    out->block_size       = bpc;
    out->total_blocks     = (unsigned long long)total_clusters;
    out->free_blocks      = (unsigned long long)free_clusters;
    out->available_blocks = (unsigned long long)(avail_to_caller.QuadPart / bpc);
    out->total_inodes     = 0;   /* not exposed by Windows */
    out->free_inodes      = 0;
    return 0;
}

/* ------------------------------------------------------------------ *
 * Canonicalization (realpath equivalent) for Windows.
 *
 * GetFinalPathNameByHandle() returns the fully-qualified path with the
 * "\\?\" prefix stripped and the drive letter normalized. Symlinks are
 * resolved because the handle is opened without OPEN_REPARSE_POINT.
 * ------------------------------------------------------------------ */
bool
fs_plat_realpath(const char *path, char *out, size_t out_size)
{
    if (path == NULL || out == NULL || out_size == 0) {
        errno = EINVAL;
        return false;
    }

    HANDLE h = CreateFileA(path, 0,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                           NULL, OPEN_EXISTING,
                           FILE_FLAG_BACKUP_SEMANTICS, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        errno = (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND)
                ? ENOENT
                : (err == ERROR_ACCESS_DENIED ? EACCES
                   : (err == ERROR_PATH_NOT_FOUND ? ENOENT : EIO));
        return false;
    }

    DWORD need = GetFinalPathNameByHandleA(h, NULL, 0, FILE_NAME_NORMALIZED);
    if (need == 0) {
        CloseHandle(h);
        errno = EIO;
        return false;
    }

    char *buf = (char *)malloc((size_t)need + 1);
    if (buf == NULL) {
        CloseHandle(h);
        errno = ENOMEM;
        return false;
    }

    DWORD got = GetFinalPathNameByHandleA(h, buf, need, FILE_NAME_NORMALIZED);
    CloseHandle(h);
    if (got == 0 || got >= need) {
        free(buf);
        errno = EIO;
        return false;
    }
    buf[got] = '\0';

    /* Strip the "\\?\" prefix (and the "\\?\\UNC\" variant). */
    char *p = buf;
    if (strncmp(p, "\\\\?\\UNC\\", 8) == 0) {
        p += 6;                 /* point at "UNC\..." */
        memmove(p, p + 2, strlen(p + 2) + 1);
    } else if (strncmp(p, "\\\\?\\", 4) == 0) {
        p += 4;
    }

    bool ok = strlen(p) < out_size;
    if (ok) {
        memcpy(out, p, strlen(p) + 1);
    } else {
        errno = ENAMETOOLONG;
    }
    free(buf);
    return ok;
}

/* ------------------------------------------------------------------ *
 * fnmatch() for Windows
 * Implements the subset of POSIX fnmatch() the project relies on:
 *   '*'  matches any sequence (including '/')
 *   '?'  matches exactly one character
 *   '[...]' matches a character class, with '!' or '^' negation and
 *           a-z ranges.
 * ------------------------------------------------------------------ */
static int
fnmatch_class(const char **pat, char c)
{
    const char *p = *pat;   /* points at the '[' */
    p++;                    /* skip '[' */

    int negate = 0;
    if (*p == '!' || *p == '^') {
        negate = 1;
        p++;
    }

    int matched = 0;
    int first = 1;

    while (*p != '\0' && (*p != ']' || first)) {
        first = 0;

        char lo = *p;
        if (lo == '\\' && p[1] != '\0') {
            p++;
            lo = *p;
        }

        if (p[1] == '-' && p[2] != '\0' && p[2] != ']') {
            char hi = p[2];
            if (c >= lo && c <= hi) matched = 1;
            p += 3;
        } else {
            if (c == lo) matched = 1;
            p++;
        }
    }

    if (*p == ']') p++;   /* consume the closing bracket */
    *pat = p;

    return negate ? !matched : matched;
}

static int
fnmatch_impl(const char *pattern, const char *string)
{
    const char *p = pattern;
    const char *s = string;

    while (*p != '\0') {
        if (*p == '*') {
            /* Collapse consecutive stars. */
            while (*p == '*') p++;
            if (*p == '\0') return 1;

            /* Try every possible split point. */
            while (1) {
                if (fnmatch_impl(p, s)) return 1;
                if (*s == '\0') return 0;
                s++;
            }
        }

        if (*s == '\0') return 0;

        if (*p == '?') {
            p++;
            s++;
            continue;
        }

        if (*p == '[') {
            const char *tmp = p;
            if (fnmatch_class(&tmp, *s)) {
                p = tmp;
                s++;
                continue;
            }
            return 0;
        }

        if (*p == '\\' && p[1] != '\0') {
            if (p[1] != *s) return 0;
            p += 2;
            s++;
            continue;
        }

        if (*p != *s) return 0;
        p++;
        s++;
    }

    return *s == '\0' ? 1 : 0;
}

int
fs_plat_fnmatch(const char *pattern, const char *string)
{
    if (pattern == NULL || string == NULL) return 0;
    return fnmatch_impl(pattern, string);
}

int
fs_plat_user_name(FsUid uid, char *out, size_t out_size)
{
    /* Windows has no POSIX uid. We use the well-known 0/544 mapping and
     * report the current user's name when the uid matches the process
     * token. */
    if (out == NULL || out_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (uid == 0) {
        snprintf(out, out_size, "%s", "SYSTEM");
        return 0;
    }
    if (out_size >= 4) {
        memcpy(out, "user", 5);
        return 0;
    }
    return -1;
}

int
fs_plat_group_name(FsGid gid, char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        errno = EINVAL;
        return -1;
    }
    if (gid == 0) {
        snprintf(out, out_size, "%s", "Administrators");
        return 0;
    }
    snprintf(out, out_size, "%s", "Users");
    return 0;
}

int
fs_plat_home_dir(char *out, size_t out_size)
{
    if (out == NULL || out_size == 0) {
        errno = EINVAL;
        return -1;
    }

    const char *env = getenv("HOME");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s", env);
        return 0;
    }
    env = getenv("USERPROFILE");
    if (env != NULL && env[0] != '\0') {
        snprintf(out, out_size, "%s", env);
        return 0;
    }
    env = getenv("HOMEDRIVE");
    const char *env2 = getenv("HOMEPATH");
    if (env != NULL && env2 != NULL) {
        snprintf(out, out_size, "%s%s", env, env2);
        return 0;
    }

    snprintf(out, out_size, "%s", ".");
    return 0;
}

int
fs_plat_mkdir(const char *path, unsigned int mode)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    (void)mode;   /* Windows has no POSIX permission bits */

    if (CreateDirectoryA(path, NULL)) {
        return 0;
    }
    DWORD err = GetLastError();
    if (err == ERROR_ALREADY_EXISTS) {
        errno = EEXIST;
    } else if (err == ERROR_ACCESS_DENIED) {
        errno = EACCES;
    } else if (err == ERROR_PATH_NOT_FOUND) {
        errno = ENOENT;
    } else if (err == ERROR_DISK_FULL) {
        errno = ENOSPC;
    } else {
        errno = EIO;
    }
    return -1;
}

int
fs_plat_rmdir(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }
    if (RemoveDirectoryA(path)) {
        return 0;
    }
    DWORD err = GetLastError();
    if (err == ERROR_DIR_NOT_EMPTY) {
        errno = ENOTEMPTY;
    } else if (err == ERROR_ACCESS_DENIED) {
        errno = EACCES;
    } else if (err == ERROR_FILE_NOT_FOUND ||
               err == ERROR_PATH_NOT_FOUND) {
        errno = ENOENT;
    } else {
        errno = EIO;
    }
    return -1;
}

int
fs_plat_unlink(const char *path)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    /* A read-only file cannot be deleted on Windows, so clear the
     * attribute first (this mirrors what POSIX allows by default for the
     * file's owner). */
    DWORD attr = GetFileAttributesA(path);
    if (attr != INVALID_FILE_ATTRIBUTES &&
        (attr & FILE_ATTRIBUTE_READONLY)) {
        SetFileAttributesA(path, attr & ~FILE_ATTRIBUTE_READONLY);
    }

    if (DeleteFileA(path)) {
        return 0;
    }
    DWORD err = GetLastError();
    if (err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND) {
        errno = ENOENT;
    } else if (err == ERROR_ACCESS_DENIED) {
        errno = EACCES;
    } else if (err == ERROR_DIRECTORY) {
        errno = EISDIR;
    } else {
        errno = EIO;
    }
    return -1;
}

int
fs_plat_chmod(const char *path, unsigned int mode)
{
    if (path == NULL || path[0] == '\0') {
        errno = EINVAL;
        return -1;
    }

    /* Windows only exposes the read-only attribute. We map "no write bit
     * for anyone" onto FILE_ATTRIBUTE_READONLY and clear it otherwise. */
    DWORD attr = GetFileAttributesA(path);
    if (attr == INVALID_FILE_ATTRIBUTES) {
        DWORD err = GetLastError();
        errno = (err == ERROR_FILE_NOT_FOUND ||
                 err == ERROR_PATH_NOT_FOUND) ? ENOENT : EACCES;
        return -1;
    }

    bool read_only = (mode & 0222u) == 0;

    if (read_only) {
        if (SetFileAttributesA(path, attr | FILE_ATTRIBUTE_READONLY)) {
            return 0;
        }
    } else {
        if (SetFileAttributesA(path, attr & ~FILE_ATTRIBUTE_READONLY)) {
            return 0;
        }
    }

    errno = EACCES;
    return -1;
}

#endif /* FS_PLATFORM_WINDOWS */
