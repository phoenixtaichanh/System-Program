/* ============================================================================
 * fs_types.h -- Shared data types for the file system service layer.
 *
 * Defined in PHASE 0; implementation lands in PHASE 2.
 * ============================================================================
 */

#ifndef FS_TYPES_H
#define FS_TYPES_H

#include "fs_platform.h"

#include <sys/types.h>  /* off_t, ino_t, nlink_t, uid_t, gid_t, mode_t, time_t */

/* Hard limits chosen to be safely large for typical Linux paths. */
#define FS_PATH_MAX  4096
#define FS_NAME_MAX   256

/* Top-N largest files / sub-directories in DirStats. */
#define FS_TOP_FILES_MAX  10
#define FS_TOP_DIRS_MAX   32

/* Status codes returned by every service-layer function. */
typedef enum {
    FS_OK = 0,
    FS_ERR_NOT_FOUND,
    FS_ERR_PERMISSION,
    FS_ERR_NOT_DIR,
    FS_ERR_IO,
    FS_ERR_INVALID_PATH,
    FS_ERR_EXISTS,
    FS_ERR_BROKEN_LINK,
    FS_ERR_NOMEM,
    FS_ERR_UNKNOWN
} FSStatus;

/* Type tag for S_IS* results; decoupled from <sys/stat.h> for UI use. */
typedef enum {
    FS_TYPE_REGULAR,
    FS_TYPE_DIRECTORY,
    FS_TYPE_SYMLINK,
    FS_TYPE_CHAR_DEV,
    FS_TYPE_BLOCK_DEV,
    FS_TYPE_FIFO,
    FS_TYPE_SOCKET,
    FS_TYPE_UNKNOWN
} FSFileType;

/* Detailed metadata for a single file system entry. */
typedef struct {
    char        name[FS_NAME_MAX];
    char        path[FS_PATH_MAX];

    FSFileType  type;

    off_t       size;
    FsIno       inode;
    FsNlink     nlinks;
    FsBlkcnt    blocks;
    FsBlkcnt    block_size;

    FsUid       uid;
    FsGid       gid;
    char        owner[64];
    char        group[64];

    unsigned int mode;
    char        perm_symbolic[12];   /* e.g. "-rwxr-xr-x" */
    char        perm_octal[5];       /* e.g. "0754" */

    time_t      atime;
    time_t      mtime;
    time_t      ctime;

    /* Symlink-specific (only valid if type == FS_TYPE_SYMLINK). */
    char        link_target[FS_PATH_MAX];

    /* Whether the link target resolves:
     *   1  the target exists
     *   0  unknown (the link was not resolved)
     *  -1  the target does not exist (broken link) */
    int         link_target_exists;

    /* Per-row fetch status so a failed stat does not abort the whole listing. */
    FSStatus    status;
    int         errno_code;
} FileMetadata;

/* One row of a directory listing. */
typedef struct {
    FileMetadata meta;
} DirEntry;

/* Result of a recursive directory analysis. */
typedef struct {
    char                path[FS_PATH_MAX];

    unsigned long long  total_files;
    unsigned long long  total_dirs;
    unsigned long long  total_symlinks;
    unsigned long long  total_other;

    unsigned long long  total_bytes;

    unsigned long long  largest_file_size;
    char                largest_file_path[FS_PATH_MAX];

    unsigned long long  avg_file_size;   /* 0 if total_files == 0 */

    /* Top N largest files. */
    struct {
        char                path[FS_PATH_MAX];
        unsigned long long  size;
    } top_files[FS_TOP_FILES_MAX];
    int                 top_files_count;

    /* Subdirectory totals. */
    struct {
        char                name[FS_NAME_MAX];
        unsigned long long  size;
    } subdirs[FS_TOP_DIRS_MAX];
    int                 subdirs_count;
} DirStats;

#endif /* FS_TYPES_H */