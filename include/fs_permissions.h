/* ============================================================================
 * fs_permissions.h -- Permission inspection helpers.
 *
 * These helpers answer "who may do what with this file?" without
 * requiring the caller to know the platform-specific uid/gid model.
 * ============================================================================
 */

#ifndef FS_PERMISSIONS_H
#define FS_PERMISSIONS_H

#include "fs_types.h"

#include <stdbool.h>

/* The three POSIX permission classes. */
typedef enum {
    FS_PERM_CLASS_OWNER = 0,
    FS_PERM_CLASS_GROUP = 1,
    FS_PERM_CLASS_OTHER = 2
} FsPermClass;

/* The three permission bits of one class. */
typedef enum {
    FS_PERM_READ  = 4,
    FS_PERM_WRITE = 2,
    FS_PERM_EXEC  = 1
} FsPermBit;

/* The read/write/execute bits of a single permission class. */
typedef struct {
    bool read;
    bool write;
    bool execute;
} FsPermBits;

/* The permission information of one file. */
typedef struct {
    unsigned int mode;      /* full mode bits of the file */
    FsUid        uid;       /* owner id                     */
    FsGid        gid;       /* group id                     */
    FsPermBits   owner;     /* owner class bits             */
    FsPermBits   group;     /* group class bits             */
    FsPermBits   other;     /* other class bits             */
} FsPermInfo;

/* Fill `out` with the permission information of `path`.
 * Uses lstat() semantics so symlinks are described, not their targets.
 * Returns FS_OK on success, an error status otherwise. */
FSStatus fs_get_permissions(const char *path, FsPermInfo *out);

/* Return the bits of one permission class.
 * `klass` selects owner, group or other. Returns false for an invalid
 * class. */
bool fs_perm_class_bits(const FsPermInfo *info,
                        FsPermClass klass,
                        FsPermBits *out);

/* Return true if the *current* process is the owner of the file described
 * by `info`. On Windows this reports true only for the "SYSTEM" pseudo
 * owner. */
bool fs_perm_is_owner(const FsPermInfo *info);

/* Compute the effective permission class of the current process with
 * respect to the file described by `info`:
 *   FS_PERM_CLASS_OWNER - the caller owns the file
 *   FS_PERM_CLASS_GROUP - the caller belongs to the file's group
 *   FS_PERM_CLASS_OTHER - everyone else
 * On platforms without POSIX groups the result is OWNER for the owning
 * user and OTHER otherwise. */
FsPermClass fs_perm_effective_class(const FsPermInfo *info);

/* The bits that actually apply to the current process, given the
 * effective class. */
FsPermBits fs_perm_effective_bits(const FsPermInfo *info);

/* Human-readable one-line summary of the permissions, e.g.
 * "rwxr-x---  (owner: rwx, group: r-x, other: ---)".
 * Writes into `out` (size `out_size`). */
FSStatus fs_perm_describe(const FsPermInfo *info,
                          char *out, size_t out_size);

#endif /* FS_PERMISSIONS_H */
