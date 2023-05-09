#pragma once

#include <stdio.h>
#include <assert.h>
#include <stdint.h>
#include <limits.h>
#include <sys/stat.h>


/**
 * a1fs block size in bytes. You are not allowed to change this value.
 *
 * The block size is the unit of space allocation. Each file (and directory)
 * must occupy an integral number of blocks. Each of the file systems metadata
 * partitions, e.g. superblock, inode/block bitmaps, inode table (but not an
 * individual inode) must also occupy an integral number of blocks.
 */
#define A1FS_BLOCK_SIZE 4096

/** Block number (block pointer) type. */
typedef uint32_t a1fs_blk_t;

/** Inode number type. */
typedef int32_t a1fs_ino_t;


/** Magic value that can be used to identify an a1fs image. */
#define A1FS_MAGIC 0xC5C369A1C5C369A1ul
#define    A1FS_S_IFREG  0x8000    /* regular file */
#define    A1FS_S_IFDIR  0x4000    /* directory */


/** a1fs superblock. */
typedef struct a1fs_superblock {
    /** Must match A1FS_MAGIC. */
    uint64_t magic;
    /** File system size in bytes. */
    uint64_t size;
    //TODO: add necessary fields
    uint64_t inodes_count; /* Inodes count */
    uint64_t blocks_count; /* Blocks count */
    uint64_t free_blocks_count; /* Free blocks count */
    uint64_t free_inodes_count; /* Free inodes count */
    uint32_t inode_block_bitmap; /* Contains offset from first data block to find inode bitmap*/
    uint32_t data_block_bitmap; /* Contains offset from first data block to find db bitmap, similar to tut1 */
    uint32_t inode_table; /* Contains offset from first data blocks to find inode table*/
    uint32_t data_block; /* Contains offset from first data block to find data blocks*/
} a1fs_superblock;

// Superblock must fit into a single block
static_assert(sizeof(a1fs_superblock) <= A1FS_BLOCK_SIZE,
              "superblock is too large");



///* a1fs block group descriptor*/
//typedef struct a1fs_group_descriptor{
//    uint32_t data_block_bitmap; /* Contains offset from first data block to find db bitmap, similar to tut1 */
//    uint32_t data_block_bitmap_block_count; /* Number of data blocks used for data block bitmap */
//    uint32_t inode_block_bitmap; /* Contains offset from first data block to find inode bitmap*/
//    uint32_t inode_block_bitmap_block_count; /* Number of data blocks used for inode block bitmap */
//    uint32_t inode_table; /* Contains offset from first data blocks to find inode table*/
//    uint32_t inode_table_block_count; /* Number of data blocks used for inode table*/
//    uint32_t extent_table; /* Contains offset from first data block to find extent table*/
//    uint32_t extent_table_block_count; /* Number of data blocks used for extent table */
//    uint32_t data_block; /* Contains offset from first data block to find data blocks*/
//    uint64_t free_data_blocks_count; /* Free data blocks count */
//    uint64_t free_inodes_count; /* Free inodes count */
//} a1fs_group_descriptor;

//
//static_assert(sizeof(a1fs_group_descriptor) <= A1FS_BLOCK_SIZE,
//              "block group descriptor is too large" );

/** Extent - a contiguous range of blocks. 8 bytes in size */
typedef struct a1fs_extent {
    /** Starting block of the extent. */
    a1fs_blk_t start;
    /** Number of blocks in the extent. */
    a1fs_blk_t count;

} a1fs_extent;


/** a1fs inode. */
typedef struct a1fs_inode {
    /** File mode. */
    mode_t mode;

    /**
     * Reference count (number of hard links).
     *
     * Each file is referenced by its parent directory. Each directory is
     * referenced by its parent directory, itself (via "."), and each
     * subdirectory (via ".."). The "parent directory" of the root directory is
     * the root directory itself.
     */
    uint32_t links;

    /** File size in bytes. */
    uint64_t size;
    /**
     * Last modification timestamp.
     *
     * Must be updated when the file (or directory) is created, written to, or
     * its size changes. Use the clock_gettime() function from time.h with the
     * CLOCK_REALTIME clock; see "man 3 clock_gettime" for details.
     */
    struct timespec mtime;

    //TODO: add necessary fields

    uint16_t used_extent_count; /* Number of extents used by inode (Max extents is 512)*/

    /* Pointer to beginning of extent "array";holds the offset from the beginning of data blocks
     *
     * */
    int32_t extent_table_start;

    char padding[18];
    //NOTE: You might have to add padding (e.g. a dummy char array field) at the
    // end of the struct in order to satisfy the assertion below. Try to keep
    // the size of this struct minimal, but don't worry about the "wasted space"
    // introduced by the required padding.

} a1fs_inode;

// A single block must fit an integral number of inodes
static_assert(A1FS_BLOCK_SIZE % sizeof(a1fs_inode) == 0, "invalid inode size");


/** Maximum file name (path component) length. Includes the null terminator. */
#define A1FS_NAME_MAX 252

/** Maximum file path length. Includes the null terminator. */
#define A1FS_PATH_MAX PATH_MAX

/** Fixed size directory entry structure. */
typedef struct a1fs_dentry {
    /** Inode number. */
    a1fs_ino_t ino;
    /** File name. A null-terminated string. */
    char name[A1FS_NAME_MAX];

} a1fs_dentry;

static_assert(sizeof(a1fs_dentry) == 256, "invalid dentry size");
