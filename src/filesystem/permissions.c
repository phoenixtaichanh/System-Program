/* ============================================================================
 * permissions.c -- Permission inspection helpers.
 *
 * On POSIX this resolves the owning user and group of a file and reports
 * the three permission classes (owner / group / other) with their
 * read-write-execute bits.
 *
 * On Windows the permission model is different: the read-only attribute
 * is the only per-file flag. The helpers still report a coherent answer
 * so that the UI behaves consistently across platforms.
 * ============================================================================
 */

/* Feature-test macros must come before any system header. */
#if !defined(_WIN32) && !defined(__MINGW32__) && !defined(__MINGW64__)
#  ifndef _DEFAULT_SOURCE
#    define _DEFAULT_SOURCE
#  endif
#  ifndef _XOPEN_SOURCE
#    define _XOPEN_SOURCE 700
#  endif
#endif

#include "fs_permissions.h"
#include "fs_stat_util.h"
#include "format_utils.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if FS_PLATFORM_POSIX
#  include <grp.h>
#  include <pwd.h>
#  include <unistd.h>
#endif

/* ------------------------------------------------------------------ *
 * Extract the rwx bits of one permission class.
 * ------------------------------------------------------------------ */
static FsPermBits
class_bits(unsigned int mode, FsPermClass klass)
{
    unsigned int shift;
    switch (klass) {
    case FS_PERM_CLASS_OWNER: shift = 6; break;
    case FS_PERM_CLASS_GROUP: shift = 3; break;
    case FS_PERM_CLASS_OTHER:
    default:                  shift = 0; break;
    }

    unsigned int bits = (mode >> shift) & 7u;

    FsPermBits out;
    out.read    = (bits & FS_PERM_READ)  != 0;
    out.write   = (bits & FS_PERM_WRITE) != 0;
    out.execute = (bits & FS_PERM_EXEC)  != 0;
    return out;
}

/* ------------------------------------------------------------------ *
 * fs_get_permissions
 * ------------------------------------------------------------------ */
FSStatus
fs_get_permissions(const char *path, FsPermInfo *out)
{
    if (path == NULL || out == NULL) {
        return FS_ERR_INVALID_PATH;
    }

    memset(out, 0, sizeof(*out));

    FsRawStat st;
    if (fs_plat_lstat(path, &st) != 0) {
        return fs_stat_util_status_from_errno(errno);
    }

    out->mode  = st.mode;
    out->uid   = st.uid;
    out->gid   = st.gid;
    out->owner = class_bits(st.mode, FS_PERM_CLASS_OWNER);
    out->group = class_bits(st.mode, FS_PERM_CLASS_GROUP);
    out->other = class_bits(st.mode, FS_PERM_CLASS_OTHER);

    return FS_OK;
}

/* ------------------------------------------------------------------ *
 * fs_perm_class_bits
 * ------------------------------------------------------------------ */
bool
fs_perm_class_bits(const FsPermInfo *info, FsPermClass klass,
                   FsPermBits *out)
{
    if (info == NULL || out == NULL) return false;

    switch (klass) {
    case FS_PERM_CLASS_OWNER: *out = info->owner; return true;
    case FS_PERM_CLASS_GROUP: *out = info->group; return true;
    case FS_PERM_CLASS_OTHER: *out = info->other; return true;
    default: return false;
    }
}

/* ------------------------------------------------------------------ *
 * fs_perm_is_owner
 * ------------------------------------------------------------------ */
bool
fs_perm_is_owner(const FsPermInfo *info)
{
    if (info == NULL) return false;

#if FS_PLATFORM_POSIX
    return info->uid == (FsUid)getuid();
#else
    /* Windows has no uid concept; the platform layer maps uid 0 to the
     * pseudo-owner "SYSTEM", which we treat as the owner. */
    return info->uid == 0;
#endif
}

/* ------------------------------------------------------------------ *
 * fs_perm_effective_class
 * ------------------------------------------------------------------ */
FsPermClass
fs_perm_effective_class(const FsPermInfo *info)
{
    if (info == NULL) return FS_PERM_CLASS_OTHER;

#if FS_PLATFORM_POSIX
    if (info->uid == (FsUid)getuid()) {
        return FS_PERM_CLASS_OWNER;
    }

    /* Check whether the process belongs to the file's group. We look at
     * the process's supplementary group list as well as its primary gid. */
    if (info->gid == (FsGid)getgid()) {
        return FS_PERM_CLASS_GROUP;
    }

    int ngroups = getgroups(0, NULL);
    if (ngroups > 0) {
        gid_t *groups = (gid_t *)calloc((size_t)ngroups, sizeof(gid_t));
        if (groups != NULL) {
            int got = getgroups(ngroups, groups);
            for (int i = 0; i < got; i++) {
                if ((FsGid)groups[i] == info->gid) {
                    free(groups);
                    return FS_PERM_CLASS_GROUP;
                }
            }
            free(groups);
        }
    }
    return FS_PERM_CLASS_OTHER;
#else
    return (info->uid == 0) ? FS_PERM_CLASS_OWNER : FS_PERM_CLASS_OTHER;
#endif
}

/* ------------------------------------------------------------------ *
 * fs_perm_effective_bits
 * ------------------------------------------------------------------ */
FsPermBits
fs_perm_effective_bits(const FsPermInfo *info)
{
    FsPermBits bits;
    memset(&bits, 0, sizeof(bits));

    if (info == NULL) return bits;

    FsPermClass klass = fs_perm_effective_class(info);
    fs_perm_class_bits(info, klass, &bits);
    return bits;
}

/* ------------------------------------------------------------------ *
 * fs_perm_describe
 * ------------------------------------------------------------------ */
FSStatus
fs_perm_describe(const FsPermInfo *info, char *out, size_t out_size)
{
    if (info == NULL || out == NULL || out_size == 0) {
        return FS_ERR_INVALID_PATH;
    }

    char sym[16];
    if (!fmt_perm_symbolic(info->mode, sym, sizeof(sym))) {
        return FS_ERR_IO;
    }

    int n = snprintf(out, out_size,
                     "%s  (owner: %s%s%s, group: %s%s%s, other: %s%s%s)",
                     sym,
                     info->owner.read    ? "r" : "-",
                     info->owner.write   ? "w" : "-",
                     info->owner.execute ? "x" : "-",
                     info->group.read    ? "r" : "-",
                     info->group.write   ? "w" : "-",
                     info->group.execute ? "x" : "-",
                     info->other.read    ? "r" : "-",
                     info->other.write   ? "w" : "-",
                     info->other.execute ? "x" : "-");
    if (n < 0 || (size_t)n >= out_size) {
        return FS_ERR_IO;
    }
    return FS_OK;
}
