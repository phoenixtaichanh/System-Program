/* ============================================================================
 * test_fs_metadata.c -- Tests for the metadata and permission modules.
 *
 * Covers fs_get_metadata, fs_get_permissions, fs_perm_describe,
 * fs_perm_effective_class and fs_perm_is_owner.
 * ============================================================================
 */

#define _XOPEN_SOURCE 700

#include "test_util.h"
#include "fs_metadata.h"
#include "fs_permissions.h"
#include "fs_types.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond) do { \
    if (cond) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: %s\n", __LINE__, #cond); \
    } \
} while (0)

#define CHECK_STR_EQ(actual, expected) do { \
    if (strcmp((actual), (expected)) == 0) { g_pass++; } \
    else { \
        g_fail++; \
        fprintf(stderr, "  FAIL line %d: \"%s\" != \"%s\"\n", \
                __LINE__, (actual), (expected)); \
    } \
} while (0)

#define SANDBOX t_sandbox_path()

static void
rm_sandbox(void)
{
    t_rmtree(SANDBOX);
}

static void
make_sandbox(void)
{
    rm_sandbox();
    t_mkdir(SANDBOX);
}

/* ------------------------------------------------------------------ *
 * test_metadata_regular_file
 * ------------------------------------------------------------------ */
static void
test_metadata_regular_file(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/hello.txt", SANDBOX);

    CHECK(t_write_file(path, "hello world\n") == 0);

    FileMetadata m;
    FSStatus s = fs_get_metadata(path, 1, &m);
    CHECK(s == FS_OK);
    CHECK_STR_EQ(m.name, "hello.txt");
    CHECK(m.type == FS_TYPE_REGULAR);
    CHECK(m.size == 12);
    CHECK(m.nlinks >= 1);
    /* Windows exposes the NTFS file index instead of a POSIX inode, so
     * the value is non-zero there too. */
    CHECK(m.inode > 0);
    CHECK(m.perm_symbolic[0] == '-');
    CHECK(m.owner[0] != '\0');
    CHECK(m.group[0] != '\0');
    CHECK(m.mtime > 0);

    /* The symbolic permissions are 10 characters: a type character plus
     * three rwx triplets. */
    CHECK(strlen(m.perm_symbolic) == 10);
    CHECK(m.perm_symbolic[0] == '-');

    /* The octal form must have at least 3 digits. */
    CHECK(strlen(m.perm_octal) >= 3);
}

/* ------------------------------------------------------------------ *
 * test_metadata_directory
 * ------------------------------------------------------------------ */
static void
test_metadata_directory(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/subdir", SANDBOX);
    t_mkdir(path);

    FileMetadata m;
    FSStatus s = fs_get_metadata(path, 1, &m);
    CHECK(s == FS_OK);
    CHECK(m.type == FS_TYPE_DIRECTORY);
    CHECK_STR_EQ(m.name, "subdir");
    CHECK(m.perm_symbolic[0] == 'd');
}

/* ------------------------------------------------------------------ *
 * test_metadata_missing
 * ------------------------------------------------------------------ */
static void
test_metadata_missing(void)
{
    FileMetadata m;
    FSStatus s = fs_get_metadata("/nonexistent/path/xyz", 1, &m);
    CHECK(s == FS_ERR_NOT_FOUND);
    CHECK(m.errno_code == ENOENT);
}

/* ------------------------------------------------------------------ *
 * test_metadata_null_args
 * ------------------------------------------------------------------ */
static void
test_metadata_null_args(void)
{
    FileMetadata m;
    CHECK(fs_get_metadata(NULL, 1, &m) == FS_ERR_INVALID_PATH);
    CHECK(fs_get_metadata("/tmp", 1, NULL) == FS_ERR_INVALID_PATH);
}

/* ------------------------------------------------------------------ *
 * test_permissions
 * ------------------------------------------------------------------ */
static void
test_permissions(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/perm.txt", SANDBOX);

    CHECK(t_write_file(path, "") == 0);

    /* chmod 0640: owner rw-, group r--, other --- */
    CHECK(chmod(path, 0640) == 0);

    FsPermInfo info;
    FSStatus s = fs_get_permissions(path, &info);
    CHECK(s == FS_OK);
    CHECK(info.owner.read == 1);
    CHECK(info.owner.write == 1);
    CHECK(info.owner.execute == 0);

#ifdef _WIN32
    /* Windows only knows the read-only flag: the group and other classes
     * are synthesized from it, so the exact bits differ from POSIX. */
    CHECK(info.group.read == 1);
    CHECK(info.other.read == 1);
    CHECK(info.group.execute == 0);
    CHECK(info.other.execute == 0);
#else
    CHECK(info.group.read == 1);
    CHECK(info.group.write == 0);
    CHECK(info.group.execute == 0);
    CHECK(info.other.read == 0);
    CHECK(info.other.write == 0);
    CHECK(info.other.execute == 0);
#endif

    /* The class accessor must agree with the struct fields. */
    FsPermBits bits;
    CHECK(fs_perm_class_bits(&info, FS_PERM_CLASS_OWNER, &bits) == 1);
    CHECK(bits.read == 1 && bits.write == 1 && bits.execute == 0);
    CHECK(fs_perm_class_bits(&info, FS_PERM_CLASS_GROUP, &bits) == 1);
    CHECK(bits.read == 1 && bits.execute == 0);
    CHECK(fs_perm_class_bits(&info, FS_PERM_CLASS_OTHER, &bits) == 1);
    CHECK(bits.execute == 0);

    /* The file belongs to the current user, so we are the owner. */
    CHECK(fs_perm_is_owner(&info) == 1);
    CHECK(fs_perm_effective_class(&info) == FS_PERM_CLASS_OWNER);

    /* The effective bits are the owner's bits. */
    FsPermBits eff = fs_perm_effective_bits(&info);
    CHECK(eff.read == 1 && eff.write == 1 && eff.execute == 0);

    /* The description must mention the three classes. */
    char desc[256];
    CHECK(fs_perm_describe(&info, desc, sizeof(desc)) == FS_OK);
    CHECK(strstr(desc, "owner") != NULL);
    CHECK(strstr(desc, "group") != NULL);
    CHECK(strstr(desc, "other") != NULL);
    CHECK(strstr(desc, "rwx") == NULL);
#ifdef _WIN32
    /* Windows synthesizes "rw-" for the group class of a writable file. */
    CHECK(strstr(desc, "rw-") != NULL);
#else
    CHECK(strstr(desc, "r--") != NULL);
#endif
}

/* ------------------------------------------------------------------ *
 * test_permissions_executable
 * ------------------------------------------------------------------ */
static void
test_permissions_executable(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/run.sh", SANDBOX);

    CHECK(t_write_file(path, "#!/bin/sh\n") == 0);

    CHECK(chmod(path, 0755) == 0);

    FsPermInfo info;
    CHECK(fs_get_permissions(path, &info) == FS_OK);
#ifndef _WIN32
    /* Windows has no execute bit at all. */
    CHECK(info.owner.execute == 1);
    CHECK(info.group.execute == 1);
    CHECK(info.other.execute == 1);
#endif
    CHECK(fs_perm_effective_class(&info) == FS_PERM_CLASS_OWNER);
}

/* ------------------------------------------------------------------ *
 * test_permissions_missing
 * ------------------------------------------------------------------ */
static void
test_permissions_missing(void)
{
    FsPermInfo info;
    CHECK(fs_get_permissions("/nonexistent/xyz", &info) == FS_ERR_NOT_FOUND);
}

/* ------------------------------------------------------------------ *
 * test_permissions_dir
 * ------------------------------------------------------------------ */
static void
test_permissions_dir(void)
{
    char path[256];
    snprintf(path, sizeof(path), "%s/perm_dir", SANDBOX);
    t_mkdir(path);

    FsPermInfo info;
    CHECK(fs_get_permissions(path, &info) == FS_OK);
    CHECK(info.owner.read == 1);
    CHECK(info.owner.write == 1);
    CHECK(info.owner.execute == 1);
}

/* ------------------------------------------------------------------ *
 * main
 * ------------------------------------------------------------------ */
int
main(void)
{
    printf("==== fs_metadata + fs_permissions test ====\n");
    t_sandbox_init("metadata");

    make_sandbox();

    test_metadata_regular_file();
    test_metadata_directory();
    test_metadata_missing();
    test_metadata_null_args();

    test_permissions();
    test_permissions_executable();
    test_permissions_missing();
    test_permissions_dir();

    rm_sandbox();

    printf("==== Result: %d passed, %d failed ====\n", g_pass, g_fail);
    return (g_fail == 0) ? 0 : 1;
}
