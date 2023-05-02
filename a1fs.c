/*
 * This code is provided solely for the personal and private use of students
 * taking the CSC369H course at the University of Toronto. Copying for purposes
 * other than this use is expressly prohibited. All forms of distribution of
 * this code, including but not limited to public repositories on GitHub,
 * GitLab, Bitbucket, or any other online platform, whether as given or with
 * any changes, are expressly prohibited.
 *
 * Authors: Alexey Khrabrov, Karen Reid
 *
 * All of the files in this directory and all subdirectories are:
 * Copyright (c) 2020 Karen Reid
 */

/**
 * CSC369 Assignment 1 - a1fs driver implementation.
 */

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>

// Using 2.9.x FUSE API
#define FUSE_USE_VERSION 29
#include <fuse.h>
#include "a1fs.h"
#include "fs_ctx.h"
#include "options.h"
#include "map.h"
#include <libgen.h>
#include <stdlib.h>

//NOTE: All path arguments are absolute paths within the a1fs file system and
// start with a '/' that corresponds to the a1fs root directory.
//
// For example, if a1fs is mounted at "~/my_csc369_repo/a1b/mnt/", the path to a
// file at "~/my_csc369_repo/a1b/mnt/dir/file" (as seen by the OS) will be
// passed to FUSE callbacks as "/dir/file".
//
// Paths to directories (except for the root directory - "/") do not end in a
// trailing '/'. For example, "~/my_csc369_repo/a1b/mnt/dir/" will be passed to
// FUSE callbacks as "/dir".


/**
 * Initialize the file system.
 *
 * Called when the file system is mounted. NOTE: we are not using the FUSE
 * init() callback since it doesn't support returning errors. This function must
 * be called explicitly before fuse_main().
 *
 * @param fs    file system context to initialize.
 * @param opts  command line options.
 * @return      true on success; false on failure.
 */
static bool a1fs_init(fs_ctx *fs, a1fs_opts *opts)
{
    // Nothing to initialize if only printing help
    if (opts->help) return true;

    size_t size;
    void *image = map_file(opts->img_path, A1FS_BLOCK_SIZE, &size);
    if (!image) return false;

    return fs_ctx_init(fs, image, size);
}

/**
 * Cleanup the file system.
 *
 * Called when the file system is unmounted. Must cleanup all the resources
 * created in a1fs_init().
 */
static void a1fs_destroy(void *ctx)
{
    fs_ctx *fs = (fs_ctx*)ctx;
    if (fs->image) {
        munmap(fs->image, fs->size);
        fs_ctx_destroy(fs);
    }
}

/** Get file system context. */
static fs_ctx *get_fs(void)
{
    return (fs_ctx*)fuse_get_context()->private_data;
}


/**
 * Get file system statistics.
 *
 * Implements the statvfs() system call. See "man 2 statvfs" for details.
 * The f_bfree and f_bavail fields should be set to the same value.
 * The f_ffree and f_favail fields should be set to the same value.
 * The following fields can be ignored: f_fsid, f_flag.
 * All remaining fields are required.
 *
 * Errors: none
 *
 * @param path  path to any file in the file system. Can be ignored.
 * @param st    pointer to the struct statvfs that receives the result.
 * @return      0 on success; -errno on error.
 */
static int a1fs_statfs(const char *path, struct statvfs *st)
{
    (void)path;// unused
    fs_ctx *fs = get_fs();

    memset(st, 0, sizeof(*st));
    st->f_bsize   = A1FS_BLOCK_SIZE;
    st->f_frsize  = A1FS_BLOCK_SIZE;
    //TODO: fill in the rest of required fields based on the information stored
    // in the superblock
    const struct a1fs_superblock * superblock = (const struct  a1fs_superblock *)(fs->image);
    st->f_blocks = superblock->blocks_count;
    st->f_bfree = superblock->free_blocks_count;
    st->f_bavail = superblock->free_blocks_count;
    st->f_files = superblock->inodes_count;
    st->f_ffree = superblock->free_inodes_count;
    st->f_favail = superblock->free_inodes_count;
    st->f_namemax = A1FS_NAME_MAX;
    return 0;

//	return -ENOSYS;
}

/*##################################################################################################
 * #################################################################################################
 * #################################################################################################
 * #################################################################################################
 * HELPER FUNCTIONS START */


/* ASSUMPTIONS MADE: Path is a valid path in the file system
 * NOTE: If need inode id, use inode->extent_table_start as id
 */
uint16_t path_to_inode_id(char * path, void* image)
{
    // Get Group Descriptor, Root Inode, and create a copy of Path
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);
    struct a1fs_inode *root_inode = (struct a1fs_inode *)(image + (superblock->inode_table * A1FS_BLOCK_SIZE));
    char * path_copy = malloc(strlen(path) + 1);
    strcpy(path_copy, path);

    // Set curr_inode to be root inode
    uint16_t curr_inode_id = 0;
    struct a1fs_inode * curr_file_inode = root_inode;
    char * pch = strtok(path_copy, "/");

    /* Init variables that we need to traverse directory to get to indoe.
     * This includes: # of extents of curr_inode, # of directory entries of curr_inode (should be a directory),
     *                # of dir_entry explored so far,
     * */
    int used_extent_count = curr_file_inode->used_extent_count;
    int number_of_directory_entries = curr_file_inode->size/sizeof(a1fs_dentry);
    int explored_dir_entry = 0;
    int nod_in_db = A1FS_BLOCK_SIZE/ sizeof(a1fs_dentry); /* number of directory entries in one data block*/
    struct a1fs_extent * first_extent = (struct a1fs_extent *)(image +
                                                               (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                               (curr_file_inode->extent_table_start * A1FS_BLOCK_SIZE));
    bool found = false;

    while(pch != NULL)
    {
        // Traverse curr_inodes data blocks by iterating through it's extents
        for(int i = 0; i < used_extent_count; ++i)
        {

            // Get current extent
            struct a1fs_extent extent = *(first_extent + i);

            // Extract extent start and the end of extent
            int start = extent.start;
            int end = start + extent.count;

            // Iterate through data blocks in extent
            for(int j = start; j < end; ++j)
            {

                // Tracks which directory entry in a given data block, i.e (first/second/third ..
                // directory entry in datablock)
                int counter = 0;

                // Each data block can hold 8 dir_entries and make sure we dont go over the lim
                while (explored_dir_entry < number_of_directory_entries && counter < nod_in_db)
                {
                    const struct a1fs_dentry * dentry = (const struct a1fs_dentry *)(image +
                                                                                     (superblock->data_block * A1FS_BLOCK_SIZE) + (j * A1FS_BLOCK_SIZE) +
                                                                                     (counter * sizeof(a1fs_dentry)));
                    // Check current dir_entry is a valid directory entry, in other words it was not deleted
                    if (dentry->ino != -1)
                    {
                        /* if current directory entry == path set curr_inodes and other
                        relevant info to new dir inode info*/
                        if(strcmp(dentry->name, pch) == 0)
                        {
                            curr_inode_id = dentry->ino;
                            curr_file_inode = (struct a1fs_inode *)(image +
                                                                    (superblock->inode_table * A1FS_BLOCK_SIZE) + curr_inode_id * sizeof(struct a1fs_inode));
                            first_extent = (struct a1fs_extent *)(image +
                                                                  (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                                  (curr_file_inode->extent_table_start * A1FS_BLOCK_SIZE));
                            number_of_directory_entries = curr_file_inode->size/sizeof(a1fs_dentry);
                            explored_dir_entry = 0;
                            used_extent_count = curr_file_inode->used_extent_count;
                            pch = strtok(NULL, "/");
                            found = true;
                            break;

                        }

                    }
                    explored_dir_entry++;
                    counter ++;
                }
                // Break out of current extent
                if(found)
                {
                    break;
                }

            }
            // Break out of all extents
            if(found){
                found = false;
                break;
            }
        }
    }

    /*When code gets here, curr_file_inode is the inode we want*/
    free(path_copy);
    return curr_inode_id;
}

a1fs_inode *get_inode_with_id(uint16_t inode_id, void* image)
{
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);
    struct a1fs_inode *inode = (struct a1fs_inode *)(image + (superblock->inode_table * A1FS_BLOCK_SIZE) +
                                                     (inode_id * sizeof(a1fs_inode)));
    return inode;
}

/* Finds first available inode in inode bitmap, if no available inode return an error*/
int64_t available_inode(void* image)
{
    struct a1fs_superblock * superblock = (struct a1fs_superblock *)(image);
    uint16_t n_inodes = superblock->inodes_count;
    int j = 0;
    while(j < n_inodes)
    {
        int which_bit = j%8;
        int offset = j/8;
        unsigned char * inode_block_bitmap_byte = (unsigned char *)(image +
                                                                    (superblock->inode_block_bitmap * A1FS_BLOCK_SIZE) + offset);
        if(!(*inode_block_bitmap_byte & (1<< which_bit)))
        {
            return j;
        }
        j++;

    }
    return -ENOSPC;
}

/* Finds first available data block in data block bitmap, if no available data block return an error*/
int64_t available_data_block(void * image)
{
    struct a1fs_superblock * superblock = (struct a1fs_superblock *)(image);
    uint16_t blocks_count = superblock->blocks_count;
    int j = 0;
    while(j < blocks_count)
    {
        int which_bit = j % 8;
        int offset = j / 8;
        unsigned char * data_block_bitmap_byte = (unsigned char *)(image +
                                                                   (superblock->data_block_bitmap * A1FS_BLOCK_SIZE) + offset);
        if(!(*data_block_bitmap_byte & (1<< which_bit)))
        {
            return j - superblock->data_block;
        }
        j++;

    }
    return -ENOSPC;
}

/* Checks data block at position pos, returns 1 if taken 0 if not taken*/
int check_data_block(void * image, uint16_t pos)
{
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);
    int which_bit = pos % 8;
    int offset = pos / 8;
    unsigned char * data_block_bitmap_byte = (unsigned char *)(image +
                                                               (superblock->data_block_bitmap * A1FS_BLOCK_SIZE) + offset);
    if(*data_block_bitmap_byte & (1 << which_bit))
    {
        return 1;
    }
    else
    {
        return 0;
    }
}

/* Sets inode at position pos to 1 indicating taken, or 0 indicating free */
void set_inode_bitmap(void* image, uint16_t pos, int flag)
{
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);
    int which_bit = pos % 8;
    int offset = pos / 8;
    unsigned char * inode_block_bitmap_byte = (unsigned char *)(image +
                                                                (superblock->inode_block_bitmap * A1FS_BLOCK_SIZE) + offset);
    if(flag == 1)
    {
        *inode_block_bitmap_byte |= (1 << which_bit);
    }
    else
    {
        *inode_block_bitmap_byte &= ~(1 << which_bit);
    }

}

/* set data block at position pos to 1 indicating taken, or 0 indicating free */
void set_data_block_bitmap(void* image, uint16_t pos, int flag)
{
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);
    pos = pos + superblock->data_block;
    int which_bit = pos % 8;
    int offset = pos / 8;
    unsigned char * data_block_bitmap_byte = (unsigned char *)(image +
                                                               (superblock->data_block_bitmap * A1FS_BLOCK_SIZE) + offset);
    if(flag == 1)
    {
        *data_block_bitmap_byte |= (1 << which_bit);
    }
    else
    {
        *data_block_bitmap_byte &= ~(1 << which_bit);
    }}

/* Write new dir entry to parent inode data blocks and update parent inode metadata
 * and update datablock bitmaps if necessary. Lastly update the group descriptor and superblock(update
 * free_blocks count if necessary and update free inodes_count)
 * */
int write_dir_entry_to_parent(struct a1fs_inode * p_inode, struct a1fs_dentry * new_dentry, void* image){

    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);

    // First traverse p_inode data blocks to check if any dir_entries with - 1 as inode number
    int used_extent_count = p_inode->used_extent_count;
    int number_of_directory_entries = p_inode->size/sizeof(a1fs_dentry);
    int explored_dir_entry = 0;
    int nod_in_db = A1FS_BLOCK_SIZE/ sizeof(a1fs_dentry); /* number of directory entries in one data block*/
    // Check if p_inode extent_table_start == -1, if so allocate an extent to
    if(p_inode->extent_table_start == -1)
    {
        // find available data block and allocate block for extent table
        int64_t extent_table_start = available_data_block(image);
        if (extent_table_start < 0)
        {
            return -ENOSPC;
        }
        p_inode->extent_table_start = extent_table_start;
        set_data_block_bitmap(image, extent_table_start, 1);


        // find available data block and write dentry
        int64_t data_block = available_data_block(image);
        if (data_block < 0)
        {
            p_inode->extent_table_start = -1;
            set_data_block_bitmap(image, extent_table_start, 0);
            return -ENOSPC;
        }
        set_data_block_bitmap(image, data_block, 1);
        memcpy(image + (superblock->data_block * A1FS_BLOCK_SIZE ) + (data_block * A1FS_BLOCK_SIZE),
               new_dentry, sizeof(a1fs_dentry));

        // write extent information to extent table and update p_inode metadata
        p_inode->used_extent_count++;
        p_inode->links++;
        p_inode->size += sizeof(a1fs_dentry);
        clock_gettime(CLOCK_REALTIME, &(p_inode->mtime));
        struct a1fs_extent extent;
        extent.start = data_block;
        extent.count = 1;
        memcpy(image + (superblock->data_block * A1FS_BLOCK_SIZE) + (extent_table_start * A1FS_BLOCK_SIZE),
               &extent, sizeof(a1fs_extent));

        // update superblock free blocks count
        superblock->free_blocks_count-=2;
        return 0;
    }



    struct a1fs_extent * p_first_extent = (struct a1fs_extent *)(image +
                                                                 (superblock->data_block * A1FS_BLOCK_SIZE) + (p_inode->extent_table_start * A1FS_BLOCK_SIZE));
    struct a1fs_extent * curr_extent;

    int last_data_block = -1;
    // Traverse p_inode data blocks by iterating through it's extents
    for(int i = 0; i < used_extent_count; ++i)
    {
        // Get current extent
        curr_extent = (p_first_extent + i);

        // Extract curr_extent start and end
        int start = curr_extent->start;
        int end = start + curr_extent->count;
        last_data_block = end - 1;
        // Iterate through data blocks in curr_extent
        for(int j = start; j < end; ++j)
        {
            // Tracks which directory entry in a given data block, i.e (first/second/third ..
            // directory entry in datablock)
            int counter = 0;

            // Each data black can hold 8_dir_entries and make sure we don't go over the number_of_directory_entries
            while (explored_dir_entry < number_of_directory_entries && counter < nod_in_db)
            {
                const struct a1fs_dentry *dentry = (const struct a1fs_dentry *) (image +
                                                                                 (superblock->data_block *A1FS_BLOCK_SIZE) + (j * A1FS_BLOCK_SIZE) +
                                                                                 (counter * sizeof(a1fs_dentry)));

                // Current dentry was previous deleted, therefore we can overwrite
                if(dentry->ino == -1)
                {
                    // Create new directory entry
                    memcpy(image +
                           (superblock->data_block *A1FS_BLOCK_SIZE) + (j * A1FS_BLOCK_SIZE) +
                           (counter * sizeof(a1fs_dentry)), new_dentry, sizeof(a1fs_dentry));
                    return 0;
                }

                explored_dir_entry++;
                counter++;
            }
        }
    }

    // If code gets here, that tells us that we have to allocate space to write directory_entry
    int x = number_of_directory_entries % nod_in_db;
    if(x != 0) // Last data block has available space
    {
        memcpy(image + (superblock->data_block * A1FS_BLOCK_SIZE) + (last_data_block * A1FS_BLOCK_SIZE) +
               (x * sizeof(a1fs_dentry)), new_dentry, sizeof(a1fs_dentry));
        // update parent inode size, links, mtime, used extent count
        p_inode->links++;
        p_inode->size += sizeof(a1fs_dentry);
        clock_gettime(CLOCK_REALTIME, &(p_inode->mtime));
        return 0;
        //
    }
    else // x == 0 which tells us last data blk has no space
    {
        int result = check_data_block(image, last_data_block + 1);
        if (result == 0)
        {
            curr_extent->count++;
            // write directory entry to last_data_block + 1
            memcpy(image + (superblock->data_block * A1FS_BLOCK_SIZE) +
                   ((last_data_block + 1) * A1FS_BLOCK_SIZE), new_dentry, sizeof(a1fs_dentry));
            //update p_inode metadata
            p_inode->links++;
            p_inode->size += sizeof(a1fs_dentry);
            clock_gettime(CLOCK_REALTIME, &(p_inode->mtime));
            set_data_block_bitmap(image, last_data_block + 1, 1);
            superblock->free_blocks_count--;
            return 0;
        }
        else
        {
            // Find available data block
            int64_t data_block = available_data_block(image);
            if( data_block < 0 )
            {
                return -ENOSPC;
            }
            // write dentry to data block
            set_data_block_bitmap(image, data_block, 1);
            memcpy(image + (superblock->data_block * A1FS_BLOCK_SIZE ) + (data_block * A1FS_BLOCK_SIZE),
                   new_dentry, sizeof(a1fs_dentry));
            // Write new extent to p_inode extent table
            curr_extent = (curr_extent + 1);
            curr_extent->start = data_block;
            curr_extent->count = 1;
            // Update p_inode meta data
            p_inode->used_extent_count++;
            p_inode->links++;
            p_inode->size+= sizeof(a1fs_dentry);
            clock_gettime(CLOCK_REALTIME, &(p_inode->mtime));
            superblock->free_blocks_count--;
            return 0;

        }

        // Check if last data blk + 1 is free
        // if free modify last extent to start, end + 1
        // go to data block end + 1 and write directory entry
        // udpate superblock free block ++
        // if last data blk + 1 not free
        // allocate new data blk
        // write next extent to extent table for p_inode
        // got to data block of new extent
    }
    return 0;

}

/* Remove an inode; i.e setting inode bitmap at c_inode_id to 0 and updating superblock*/
void remove_inode(uint16_t c_inode_id, struct a1fs_inode * c_inode, void * image)
{
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);
    if(c_inode->size == 0)
    {
        // update Inode bitmap and superblock
        set_inode_bitmap(image, c_inode_id, 0);
        superblock->free_inodes_count++;
    }
    else
    {
        uint16_t used_extent_count = c_inode->used_extent_count;
        // Iterate through all extents and for each extent iterate through its data blocks
        struct a1fs_extent * c_first_extent = (struct a1fs_extent *)(image +
                                                                     (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                                     (c_inode->extent_table_start * A1FS_BLOCK_SIZE));

        for(int i = 0; i < used_extent_count; ++i) {
            // Get current extent
            struct a1fs_extent extent = *(c_first_extent + i);
            int start = extent.start;
            int end = extent.start + extent.count;
            for (int j = start; j < end; ++j) {
                // update data block bitmap and superblock
                set_data_block_bitmap(image, j, 0);
                superblock->free_blocks_count++;
            }
        }
        // update Inode bitmap and superblock
        set_inode_bitmap(image, c_inode_id, 0);
        superblock->free_inodes_count++;
    }
}

/* Removing a directory entry in parent inode; setting c_inode_id to -1 in directory entries,
 * update parent inode metadata and update superblock metadata*/
void remove_dentry(struct a1fs_inode * p_inode, uint16_t c_inode_id, void * image)
{
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);
    uint16_t used_extent_count = p_inode->used_extent_count;
    struct a1fs_extent * p_first_extent = (struct a1fs_extent *)(image +
                                                                 (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                                 (p_inode->extent_table_start * A1FS_BLOCK_SIZE));
    int p_number_of_directory_entries = p_inode->size/sizeof(a1fs_dentry);
    int p_explored_dir_entry = 0;
    int nod_in_db = A1FS_BLOCK_SIZE/ sizeof(a1fs_dentry);
    for(int i = 0; i < used_extent_count; ++i)
    {
        // Get current extent
        struct a1fs_extent extent = *(p_first_extent + i);
        int start = extent.start;
        int end = extent.start + extent.count;
        for (int j = start; j < end; ++j)
        {
            int counter = 0;
            while (p_explored_dir_entry < p_number_of_directory_entries && counter < nod_in_db)
            {
                struct a1fs_dentry * dentry = (struct a1fs_dentry *)(image +
                                                                     (superblock->data_block * A1FS_BLOCK_SIZE) + (j * A1FS_BLOCK_SIZE)
                                                                     + (counter * sizeof(a1fs_dentry)));
                if (dentry ->ino == c_inode_id)
                {
                    dentry->ino = -1;
                    p_inode->links--;
                    clock_gettime(CLOCK_REALTIME, &(p_inode->mtime));
                    return;
                }
                p_explored_dir_entry++;
                counter++;
            }

        }

    }

}

void zero_out_data_block(void * image, int64_t pos)
{
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(image);
    unsigned char zero = 0;
    for(int i = 0; i < A1FS_BLOCK_SIZE; ++i)
    {
        memcpy(image + (superblock->data_block * A1FS_BLOCK_SIZE) +
               (pos * A1FS_BLOCK_SIZE) + i, &zero, sizeof(unsigned char));
    }

}

int compare (const void * a, const void * b)
{
    return ( *(int64_t *)a - *(int64_t *)b );
}

uint16_t my_ceil(uint32_t a, uint32_t b)
{
    if (a % b == 0)
    {
        return a / b;
    }
    else
    {
        return (a / b) + 1;
    }

}



/* HELPER FUNCTIONS END
 * ##################################################################################################
 * #################################################################################################
 * #################################################################################################
 * #################################################################################################*/





/**
 * Get file or directory attributes.
 *
 * Implements the lstat() system call. See "man 2 lstat" for details.
 * The following fields can be ignored: st_dev, st_ino, st_uid, st_gid, st_rdev,
 *                                      st_blksize, st_atim, st_ctim.
 * All remaining fields are required.
 *
 * NOTE: the st_blocks field is measured in 512-byte units (disk sectors).
 *
 * Errors:
 *   ENAMETOOLONG  the path or one of its components is too long.
 *   ENOENT        a component of the path does not exist.
 *   ENOTDIR       a component of the path prefix is not a directory.
 *
 * @param path  path to a file or directory.
 * @param st    pointer to the struct stat that receives the result.
 * @return      0 on success; -errno on error;
 */
static int a1fs_getattr(const char *path, struct stat *st)
{
    if (strlen(path) >= A1FS_PATH_MAX) return -ENAMETOOLONG;
    fs_ctx *fs = get_fs();

    memset(st, 0, sizeof(*st));

    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(fs->image);
    struct a1fs_inode *root_inode = (struct a1fs_inode *)(fs->image + (superblock->inode_table * A1FS_BLOCK_SIZE));
    char * path_copy = malloc(strlen(path) + 1);
    strcpy(path_copy, path);

    int curr_inode_id = 0;
    struct a1fs_inode * curr_file_inode = root_inode;
    char * pch = strtok(path_copy, "/");

    int used_extent_count = curr_file_inode->used_extent_count;
    int number_of_directory_entries = curr_file_inode->size/sizeof(a1fs_dentry);
    int explored_dir_entry = 0;
    int nod_in_db = A1FS_BLOCK_SIZE/ sizeof(a1fs_dentry); /*number of directory entries in one data block*/

    // Check if extent exists for root and whether path is "/"
    if(curr_file_inode->extent_table_start == -1 && strcmp(path_copy, "/") != 0)
    {
        return -ENOENT;
    }

    struct a1fs_extent * first_extent = (struct a1fs_extent *)(fs->image +
                                                               (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                               (curr_file_inode->extent_table_start * A1FS_BLOCK_SIZE));
    bool prev_file = false;
    bool found = false;


    while(pch != NULL)
    {
        // go to curr_inodes data blocks
        if(prev_file)
        {
            return -ENOTDIR;
        }

        for(int i = 0; i < used_extent_count; ++i)
        {
            // Get current extent
            struct a1fs_extent extent = *(first_extent + i);

            // Extract extent start and the end of extent
            int start = extent.start;
            int end = start + extent.count;

            // Iterate through data blocks in extent
            for(int j =start; j < end; ++j)
            {
                int counter = 0;

                // Each data block can hold 8 dir_entries and make sure we dont go over the lim
                while (explored_dir_entry < number_of_directory_entries && counter < nod_in_db)
                {
                    // Get current directory entry
                    const struct a1fs_dentry * dentry = (const struct a1fs_dentry *)(fs->image +
                                                                                     (superblock->data_block * A1FS_BLOCK_SIZE)+ (j * A1FS_BLOCK_SIZE) +
                                                                                     (counter * sizeof(a1fs_dentry)));

                    // Check if current directory is valid;i.e directory entry has not been deleted
                    if(dentry->ino != -1)
                    {
                        /* if current directory entry == pch set curr_inodes and other
                        relevant info to new dir inode info*/
                        if(strcmp(dentry->name, pch) == 0)
                        {
                            curr_inode_id = dentry->ino;
                            curr_file_inode = (struct a1fs_inode *)(fs->image +
                                                                    (superblock->inode_table * A1FS_BLOCK_SIZE) + curr_inode_id * sizeof(a1fs_inode));
                            first_extent = (struct a1fs_extent *)(fs->image +
                                                                  (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                                  (curr_file_inode->extent_table_start * A1FS_BLOCK_SIZE));
                            number_of_directory_entries = curr_file_inode->size/sizeof(a1fs_dentry);
                            explored_dir_entry = 0;
                            used_extent_count = curr_file_inode->used_extent_count;
                            pch = strtok(NULL, "/");
                            if(curr_file_inode->mode & A1FS_S_IFREG)
                            {
                                prev_file = true;
                            }
                            found = true;
                            break;

                        }

                    }
                    explored_dir_entry++;
                    counter++;

                }
                // Break out of current extent
                if (found)
                {
                    break;
                }

            }
            // Break out of all extents / all datablocks for inode
            if (found)
            {
                break;
            }
        }
        /* After iterating through all datablocks in extent, make sure we actually found the next file/dir
         * If so set found to false, else return an error
        */
        if (found)
        {
            found = false;
        }
        else
        {
            return -ENOENT;
        }
    }


    /* If we reach here, that means no errors encountered in while loop and curr_file_inode is inode that
     * represents the path
    */

    st->st_mode = curr_file_inode->mode;
    st->st_nlink = curr_file_inode->links;
    st->st_size = curr_file_inode->size;
    st->st_blocks = my_ceil(curr_file_inode->size , A1FS_BLOCK_SIZE) * 8; // 8 comes from 4096/512 i.e how many 512 byte block in 4096 data block
    st->st_mtim = curr_file_inode->mtime;
    return 0;

}

/**
 * Read a directory.
 *
 * Implements the readdir() system call. Should call filler(buf, name, NULL, 0)
 * for each directory entry. See fuse.h in libfuse source code for details.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a filler() call failed).
 *
 * @param path    path to the directory.
 * @param buf     buffer that receives the result.
 * @param filler  function that needs to be called for each directory entry.
 *                Pass 0 as offset (4th argument). 3rd argument can be NULL.
 * @param offset  unused.
 * @param fi      unused.
 * @return        0 on success; -errno on error.
 */
static int a1fs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{
    (void)offset;// unused
    (void)fi;// unused
    fs_ctx *fs = get_fs();


    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(fs->image);
    uint16_t inode_id = path_to_inode_id((char* )path, fs->image);
    struct a1fs_inode * inode = get_inode_with_id(inode_id, fs->image);
    uint16_t used_extent_count = inode->used_extent_count;

    // first_extent is gonna be garbage value if extent_table_start == -1
    struct a1fs_extent * first_extent = (struct a1fs_extent *)(fs->image +
                                                               (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                               (inode->extent_table_start * A1FS_BLOCK_SIZE));
    int number_of_directory_entries = inode->size/sizeof(a1fs_dentry);
    int explored_dir_entry = 0;
    int nod_in_db = A1FS_BLOCK_SIZE/ sizeof(a1fs_dentry); /*number of directory entries in one data block*/


    // once finish traverse while loop, curr_dir will be the inode that inode that represents the path
    filler(buf, "." , NULL, 0);
    filler(buf, "..", NULL, 0);
    for(int i = 0; i < used_extent_count; ++i){
        // Get current extent
        struct a1fs_extent extent = *(first_extent + i);
        int start = extent.start;
        int end = extent.start + extent.count;
        for(int j =start; j < end; ++j)
        {
            int counter = 0;
            while (explored_dir_entry < number_of_directory_entries && counter < nod_in_db)
            {
                const struct a1fs_dentry * dentry = (const struct a1fs_dentry *)(fs->image +
                                                                                 (superblock->data_block * A1FS_BLOCK_SIZE) + (j * A1FS_BLOCK_SIZE)
                                                                                 + (counter * sizeof(a1fs_dentry)));
                if(dentry->ino != -1)
                {
                    filler(buf, dentry->name, NULL, 0);
                }
                explored_dir_entry++;
                counter ++;
            }
        }
    }

    return 0;


//	return -ENOSYS;
}


/**
 * Create a directory.
 *
 * Implements the mkdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the directory to create.
 * @param mode  file mode bits.
 * @return      0 on success; -errno on error.
 */
static int a1fs_mkdir(const char *path, mode_t mode)
{
    mode = mode | S_IFDIR;
    fs_ctx *fs = get_fs();
    //TODO: create a directory at given path with given mode

    /* Step 1: Extract parent absolute path and new directory name*/
    char *pc_1 = malloc(strlen(path) + 1);
    char *pc_2 = malloc(strlen(path) + 1);
    strcpy(pc_1, path);
    strcpy(pc_2, path);
    char * parent_directory = dirname(pc_1);
    char * directory = basename(pc_2);

    /* Step 2: Traverse parent directory absolute path to get parent directory inode*/
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(fs->image);
    uint16_t p_inode_id = path_to_inode_id(parent_directory, fs->image);
    struct a1fs_inode * p_inode = get_inode_with_id(p_inode_id, fs->image);


    /* Step 3: Find first available inode and create new directory inode. Update inode bitmap
     * and update parent inode metadata. Also perform necessary writes to datablocks
     * and update datablock bitmap accordingly
     * */

    if(superblock->free_inodes_count == 0){ // No more available inodes
        return -ENOMEM; // I think this error is fine?
    }

    // Step 3a. Find available inode_id
    uint16_t new_dir_inode_id = available_inode(fs->image);

    // Step 3b. Create a new_dir inode
    struct a1fs_inode new_dir;
    new_dir.size = 0;
    new_dir.mode = A1FS_S_IFDIR;
    new_dir.used_extent_count = 0;
    new_dir.extent_table_start = -1;
    new_dir.links = 2;
    clock_gettime( CLOCK_REALTIME, &(new_dir.mtime));

    // Step 3c. Write new_dir inode to inode table
    memcpy(fs->image + (superblock->inode_table * A1FS_BLOCK_SIZE) +
           new_dir_inode_id * sizeof(a1fs_inode), &new_dir, sizeof(a1fs_inode));

    // Step 3d. Create new directory entry
    struct a1fs_dentry new_dentry;
    strcpy(new_dentry.name, directory);
    new_dentry.name[strlen(directory)+1] = '\0';
    new_dentry.ino = new_dir_inode_id;

    // Step 3e. Write directory entry to parent inode
    int res = write_dir_entry_to_parent(p_inode, &new_dentry, fs->image);
    if(res < 0)
    {
        return -ENOSPC;
    }
    set_inode_bitmap(fs->image, new_dir_inode_id, 1);
    superblock->free_inodes_count--;


    // new_dir_inode_id = available_inode(fs->image) // Step 3a. Find available inode_id
    // create new inode call new_dir // Step 3b. Create a new_dir inode
    // write new_dir inode to inode table // Step 3c. Write new_dir inode to inode table
    // create new directory entry for p_inode // Step 3d. Create new directory entry
    // Step 3f. Write directory entry, in this subroutine update superblock.free_blocks_count if necessary
    // write_dir_entry(p_inode, new directory entry, fs->image) [returns 0 on success, -ENOMEM on error]
    // Step 3g. Update inode bitmap for newly allocated inode
    // set available_inode_id bit to 1 in inode bitmap and update superblock free_inode_count


    free(pc_1);
    free(pc_2);
    (void)path;
    (void)mode;
    (void)fs;
    return 0;
}

/**
 * Remove a directory.
 *
 * Implements the rmdir() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a directory.
 *
 * Errors:
 *   ENOTEMPTY  the directory is not empty.
 *
 * @param path  path to the directory to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_rmdir(const char *path)
{
    fs_ctx *fs = get_fs();
    // Get parent path and parent inode
    // Get directory and directory inode
    char *pc_1 = malloc(strlen(path) + 1);
    strcpy(pc_1, path);
    char * parent_directory = dirname(pc_1);
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(fs->image);

    uint16_t p_inode_id = path_to_inode_id(parent_directory, fs->image);
    struct a1fs_inode * p_inode = get_inode_with_id(p_inode_id, fs->image);

    uint16_t c_inode_id = path_to_inode_id((char *)path, fs->image);
    struct a1fs_inode * c_inode = get_inode_with_id(c_inode_id, fs->image);
    uint16_t used_extent_count = c_inode->used_extent_count;

    // If size is 0 then its empty,
    if(c_inode->size == 0)
    {
        remove_inode(c_inode_id, c_inode, fs->image);
        remove_dentry(p_inode, c_inode_id, fs->image);
        return 0;
    }

    // Iterate through directory entries making sure that each directory entry_ino is -1
    struct a1fs_extent * c_first_extent = (struct a1fs_extent *)(fs->image +
                                                                 (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                                 (c_inode->extent_table_start * A1FS_BLOCK_SIZE));
    int c_number_of_directory_entries = c_inode->size/sizeof(a1fs_dentry);
    int c_explored_dir_entry = 0;
    int nod_in_db = A1FS_BLOCK_SIZE/ sizeof(a1fs_dentry);
    bool found_valid_dentry = false;
    for(int i = 0; i < used_extent_count; ++i)
    {
        // Get current extent
        struct a1fs_extent extent = *(c_first_extent + i);
        int start = extent.start;
        int end = extent.start + extent.count;
        for (int j = start; j < end; ++j)
        {
            int counter = 0;
            while (c_explored_dir_entry < c_number_of_directory_entries && counter < nod_in_db)
            {
                const struct a1fs_dentry * dentry = (const struct a1fs_dentry *)(fs->image +
                                                                                 (superblock->data_block * A1FS_BLOCK_SIZE) + (j * A1FS_BLOCK_SIZE)
                                                                                 + (counter * sizeof(a1fs_dentry)));
                if(dentry->ino != -1) // Theres still a valid directory entry
                {
                    found_valid_dentry = true;
                    break;
                }
                c_explored_dir_entry++;
                counter++;
            }
            if(found_valid_dentry)
            {
                break;
            }
        }
        if(found_valid_dentry)
        {
            break;
        }
    }


    if(found_valid_dentry)
    {
        return -ENOTEMPTY;
    }
    else
    {
        remove_inode(c_inode_id, c_inode, fs->image);
        remove_dentry(p_inode, c_inode_id, fs->image);
        return 0;
    }



}

/**
 * Create a file.
 *
 * Implements the open()/creat() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" doesn't exist.
 *   The parent directory of "path" exists and is a directory.
 *   "path" and its components are not too long.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to create.
 * @param mode  file mode bits.
 * @param fi    unused.
 * @return      0 on success; -errno on error.
 */
static int a1fs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    (void)fi;// unused
    assert(S_ISREG(mode));
    fs_ctx *fs = get_fs();

    /* Step 1: Extract parent absolute path and new file name*/
    char *pc_1 = malloc(strlen(path) + 1);
    char *pc_2 = malloc(strlen(path) + 1);
    strcpy(pc_1, path);
    strcpy(pc_2, path);
    char * parent_directory = dirname(pc_1);
    char * file = basename(pc_2);

    /* Step 2: Traverse parent directory absolute path to get parent directory inode*/
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(fs->image);
    uint16_t p_inode_id = path_to_inode_id(parent_directory, fs->image);
    struct a1fs_inode * p_inode = get_inode_with_id(p_inode_id, fs->image);

    /* Step 3: Find first available inode and create new file inode. Update inode bitmap
     * and update parent inode metadata. Also perform necessary writes to datablocks
     * and update datablock bitmap accordingly
     * */
    if(superblock->free_inodes_count == 0){ // No more available inodes
        return -ENOMEM; // I think this error is fine?
    }

    // Step 3a. Find available inode_id
    uint16_t new_file_inode_id = available_inode(fs->image);

    // Step 3b. Create a new_file inode
    struct a1fs_inode new_file;
    new_file.size = 0;
    new_file.mode = mode;
    new_file.used_extent_count = 0;
    new_file.extent_table_start = -1;
    new_file.links = 1;
    clock_gettime(CLOCK_REALTIME, &(new_file.mtime));

    // Step 3c. Write new_file inode to inode table
    memcpy(fs->image + (superblock->inode_table * A1FS_BLOCK_SIZE) +
           new_file_inode_id * sizeof(struct a1fs_inode), &new_file, sizeof(a1fs_inode));

    // Step 3d. Create new directory entry
    struct a1fs_dentry new_dentry;
    strcpy(new_dentry.name, file);
    new_dentry.name[strlen(file)+1] = '\0';
    new_dentry.ino = new_file_inode_id;

    int res = write_dir_entry_to_parent(p_inode, &new_dentry, fs->image);
    if(res < 0 )
    {
        return -ENOSPC;
    }
    set_inode_bitmap(fs->image, new_file_inode_id, 1);
    superblock->free_inodes_count--;

    (void)path;
    (void)mode;
    (void)fs;
    return 0;
}

/**
 * Remove a file.
 *
 * Implements the unlink() system call.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path  path to the file to remove.
 * @return      0 on success; -errno on error.
 */
static int a1fs_unlink(const char *path)
{
    fs_ctx *fs = get_fs();

    // Get parent path and parent inode
    // Get directory and directory inode
    char *pc_1 = malloc(strlen(path) + 1);
    strcpy(pc_1, path);
    char * parent_directory = dirname(pc_1);



    uint16_t p_inode_id = path_to_inode_id(parent_directory, fs->image);
    struct a1fs_inode * p_inode = get_inode_with_id(p_inode_id, fs->image);

    uint16_t c_inode_id = path_to_inode_id((char *)path, fs->image);
    struct a1fs_inode * c_inode = get_inode_with_id(c_inode_id, fs->image);


    remove_inode(c_inode_id, c_inode, fs->image);
    remove_dentry(p_inode, c_inode_id, fs->image);
    free(pc_1);
    (void)path;
    (void)fs;
    return 0;

}


/**
 * Change the modification time of a file or directory.
 *
 * Implements the utimensat() system call. See "man 2 utimensat" for details.
 *
 * NOTE: You only need to implement the setting of modification time (mtime).
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists.
 *
 * Errors: none
 *
 * @param path   path to the file or directory.
 * @param times  timestamps array. See "man 2 utimensat" for details.
 * @return       0 on success; -errno on failure.
 */
static int a1fs_utimens(const char *path, const struct timespec times[2])
{
    fs_ctx *fs = get_fs();

    uint16_t  inode_id = path_to_inode_id((char *) path, fs->image);
    struct a1fs_inode * inode = get_inode_with_id(inode_id, fs->image);
    inode->mtime = times[1];
    (void)path;
    (void)times;
    (void)fs;
    return 0;

}

/**
 * Change the size of a file.
 *
 * Implements the truncate() system call. Supports both extending and shrinking.
 * If the file is extended, the new uninitialized range at the end must be
 * filled with zeros.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path  path to the file to set the size.
 * @param size  new file size in bytes.
 * @return      0 on success; -errno on error.
 */
static int a1fs_truncate(const char *path, off_t size)
{
    fs_ctx *fs = get_fs();
    struct a1fs_superblock *superblock = (struct a1fs_superblock *)(fs->image);
    uint16_t inode_id = path_to_inode_id((char *)path, fs->image);
    struct a1fs_inode * inode = get_inode_with_id(inode_id, fs->image);

    // truncate to smaller size
    if ((uint32_t)size < inode->size)
    {
        uint16_t counter = 0;
        bool truncated = false;
        uint16_t extents_visited = 0;
        struct a1fs_extent * first_extent = (struct a1fs_extent *)(fs->image +
                                                                   (superblock->data_block * A1FS_BLOCK_SIZE) + (inode->extent_table_start * A1FS_BLOCK_SIZE));
        struct a1fs_extent * curr_extent;

        // Iterate through extents of the file
        for (int i = 0; i < inode->used_extent_count; ++i)
        {
            curr_extent = (first_extent + i);
            extents_visited++;
            uint16_t start = curr_extent->start;
            uint16_t end = start + curr_extent->count;
            for(int j = start; j < end; ++j)
            {
                if(!truncated)
                {
                    counter += A1FS_BLOCK_SIZE;
                    if(size == 0)
                    {
                        set_data_block_bitmap(fs->image, j, 0);
                        superblock->free_blocks_count++;

                        inode->extent_table_start = -1;
                        inode->used_extent_count = 0;
                        inode->size = 0;
                        truncated = true;
                    }
                    else if (counter >= size) // Found Last Data Block in Extent
                    {
                        curr_extent->count = j + 1 - start;
                        inode->used_extent_count = extents_visited;
                        inode->size = size;
                        truncated = true;
                    }

                }
                else // has already been truncated, free remaining data blocks in rest of the extents
                {
                    set_data_block_bitmap(fs->image, j , 0);
                    superblock->free_blocks_count++;
                }
            }

        }
        return 0;


    }
    else if((uint32_t)size > inode->size) // Truncate to a bigger size
    {
        // Calculating how many data blocks are require to truncate it to bigger size
        uint16_t blocks_required;
        uint8_t excess = A1FS_BLOCK_SIZE - (inode->size % A1FS_BLOCK_SIZE);

        if (inode->size % A1FS_BLOCK_SIZE == 0)
        {
            blocks_required = my_ceil((size - inode->size),  A1FS_BLOCK_SIZE);
        }
        else
        {
            if (size - inode->size <= excess)
            {
                blocks_required = 0;
            }
            else
            {
                blocks_required = my_ceil((size - excess - inode->size), A1FS_BLOCK_SIZE);
            }

        }

        // Make sure that we have enough data blocks in our file system to perform truncate call
        if(inode->extent_table_start == -1) // This is the case when file does not have an extent_table allocated
        {
            if((uint32_t)(blocks_required + 1) > superblock->free_blocks_count)
            {
                return -ENOSPC;
            }
        }
        else if (blocks_required > superblock->free_blocks_count)
        {
            return -ENOSPC;
        }

        // allocated the data blocks required for truncate
        int64_t datablocks[blocks_required];
        for(int i = 0; i < blocks_required; ++i)
        {
            int64_t pos = available_data_block(fs->image);
            set_data_block_bitmap(fs->image, pos, 1);
            superblock->free_blocks_count--;
            datablocks[i] = pos;
            zero_out_data_block(fs->image, pos);
        }

        // Convert datablocks[blocks_required] into extents
        // Step 1. Sort the array
        qsort(datablocks, blocks_required, sizeof(int64_t), compare);

        // Step 2. Do 2 passes of datablocks[blocks_required]
        // Step 2a. First pass check how many extents we need and at the same time zero
        // out the dat in these data blocks
        int extents_required = 0;
        if(blocks_required == 0)
        {
            // Zero out last Datab Bock if applicable
            if(inode->used_extent_count != 0)
            {
                struct a1fs_extent * last_extent = (struct a1fs_extent *)(fs->image +
                                                                          (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                                          (inode->extent_table_start * A1FS_BLOCK_SIZE) +
                                                                          ((inode->used_extent_count - 1) * sizeof(struct a1fs_extent)));

                // Zero out excess bytes of last data block
                uint16_t  last_datablock = last_extent->start + last_extent->count - 1;
                for(int i = A1FS_BLOCK_SIZE - excess; i < A1FS_BLOCK_SIZE; ++i)
                {
                    unsigned char zero = 0;
                    memcpy(fs->image + (superblock->data_block * A1FS_BLOCK_SIZE) +
                           (last_datablock * A1FS_BLOCK_SIZE) + i, &zero, sizeof(unsigned char ));
                }
            }
            return 0;
        }
        else
        {
            //Step 2a. First pass check how many extents we need
            for(int i = 1; i < blocks_required + 1; ++i)
            {
                if (i == blocks_required || datablocks[i] - datablocks[i-1] != 1)
                {
                    extents_required++;
                }
            }
            if(extents_required + inode->used_extent_count > 512)
            {
                for(int i = 0; i < blocks_required; ++i)
                {
                    set_data_block_bitmap(fs->image, datablocks[i] , 0);
                    superblock->free_blocks_count++;
                }
                return -ENOSPC;
            }

            // Step 2b. Write extents to extent array
            struct a1fs_extent extent_arr[extents_required];
            int idx = 0;
            int length = 1;
            for(int i = 1; i < blocks_required + 1; ++i)
            {
                if (i == blocks_required || datablocks[i] - datablocks[i-1] != 1)
                {
                    struct a1fs_extent ext;
                    ext.start = datablocks[i-length];
                    ext.count = length;
                    extent_arr[idx] = ext;
                    length = 1;
                    idx++;
                }
                else
                {
                    length++;
                }
            }
            // write 0 to excess bytes to do that get last data block for file
            if(inode->extent_table_start != - 1)
            {
                struct a1fs_extent * last_extent = (struct a1fs_extent *)(fs->image +
                                                                          (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                                          (inode->extent_table_start * A1FS_BLOCK_SIZE) +
                                                                          ((inode->used_extent_count - 1) * sizeof(struct a1fs_extent)));

                // Zero out excess bytes of last data block
                uint16_t  last_datablock = last_extent->start + last_extent->count - 1;
                for(int i = A1FS_BLOCK_SIZE - excess; i < A1FS_BLOCK_SIZE; ++i)
                {
                    unsigned char zero = 0;
                    memcpy(fs->image + (superblock->data_block * A1FS_BLOCK_SIZE) +
                           (last_datablock * A1FS_BLOCK_SIZE) + i, &zero, sizeof(unsigned char ));
                }
                // Go through extents see if any extents.start matches last_extent.start + last_extent.count, i.e
                // increase length of last extent
                bool extend_last_extent = false;
                // Add extents to extent table
                for(int i = 0; i < extents_required; ++i)
                {
                    if(extent_arr[i].start == last_extent->start + last_extent->count)
                    {
                        last_extent->count += extent_arr[i].count;
                        extend_last_extent = true;
                    }
                    else
                    {
                        if(extend_last_extent)
                        {
                            memcpy(fs->image +
                                   (superblock->data_block * A1FS_BLOCK_SIZE) +
                                   (inode->extent_table_start * A1FS_BLOCK_SIZE) +
                                   ((inode->used_extent_count + i -1) * sizeof(struct a1fs_extent)),
                                   &extent_arr[i], sizeof(a1fs_extent));
                        }else
                        {
                            memcpy(fs->image +
                                   (superblock->data_block * A1FS_BLOCK_SIZE) +
                                   (inode->extent_table_start * A1FS_BLOCK_SIZE) +
                                   ((inode->used_extent_count + i) * sizeof(struct a1fs_extent)),
                                   &extent_arr[i], sizeof(a1fs_extent));
                        }

                    }
                }
                // Update inode data
                if (extend_last_extent)
                {
                    inode->used_extent_count += extents_required -1;

                }else
                {
                    inode->used_extent_count += extents_required;
                }
                inode->size = size;
                return 0;

            }
            else
            {
                // Allocate data block for extent table
                int64_t pos = available_data_block(fs->image);
                set_data_block_bitmap(fs->image, pos, 1);
                superblock->free_blocks_count--;
                inode->extent_table_start = pos;

                // Add extents to extent table
                for(int i = 0; i < extents_required; ++i)
                {
                    memcpy(fs->image +
                           (superblock->data_block * A1FS_BLOCK_SIZE) +
                           (inode->extent_table_start * A1FS_BLOCK_SIZE) +
                           ((inode->used_extent_count + i) * sizeof(struct a1fs_extent)),
                           &extent_arr[i], sizeof(a1fs_extent));
                }
                // Update inode data
                inode->used_extent_count += extents_required;
                inode->size = size;
                return 0;
            }

        }


    }
    else
    {
        return 0;
    }

}


/**
 * Read data from a file.
 *
 * Implements the pread() system call. Must return exactly the number of bytes
 * requested except on EOF (end of file). Reads from file ranges that have not
 * been written to must return ranges filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors: none
 *
 * @param path    path to the file to read from.
 * @param buf     pointer to the buffer that receives the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to read from.
 * @param fi      unused.
 * @return        number of bytes read on success; 0 if offset is beyond EOF;
 *                -errno on error.
 */
static int a1fs_read(const char *path, char *buf, size_t size, off_t offset,
                     struct fuse_file_info *fi)
{
    (void)fi;// unused
    fs_ctx *fs = get_fs();
    struct a1fs_superblock * superblock = (struct a1fs_superblock *)(fs->image);
    uint16_t inode_id = path_to_inode_id((char *)path, fs->image);
    struct a1fs_inode * inode =  get_inode_with_id(inode_id, fs->image);
    // EOF case
    if((uint32_t)offset> inode->size)
    {
        return 0;
    }


    uint16_t which_datablk = offset / A1FS_BLOCK_SIZE;
    uint16_t which_byte = offset % A1FS_BLOCK_SIZE;

    uint16_t datablk_traversed = 0;
    int16_t datablock_pos = -1;
    struct a1fs_extent * first_extent = (struct a1fs_extent *)(fs->image +
                                                               (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                               (inode->extent_table_start * A1FS_BLOCK_SIZE));
    uint16_t bytes_visited = 0;
    for(int i = 0; i < inode->used_extent_count; ++i)
    {
        struct a1fs_extent * extent = (first_extent + i);
        int start = extent->start;
        int end = extent->start + extent->count;
        for(int j = start; j < end; ++j)
        {
            if(datablk_traversed == which_datablk)
            {
                datablock_pos = j;
                break;
            }
            datablk_traversed++;
            bytes_visited += A1FS_BLOCK_SIZE;
        }
        if(datablock_pos != -1)
        {
            break;
        }
    }

    char * inode_offset_byte = (char *)(fs->image +
                                        (superblock->data_block * A1FS_BLOCK_SIZE) + (datablock_pos * A1FS_BLOCK_SIZE) + which_byte);
    bytes_visited += which_byte;

    uint16_t read = 0;
    for (int i = 0; i < (int)size; ++i)
    {
        if(inode->size - bytes_visited == 0)
        {
            return read;
        }
        buf[i] = inode_offset_byte[i];
        read++;
        bytes_visited++;
    }
    return size;

}

/**
 * Write data to a file.
 *
 * Implements the pwrite() system call. Must return exactly the number of bytes
 * requested except on error. If the offset is beyond EOF (end of file), the
 * file must be extended. If the write creates a "hole" of uninitialized data,
 * the new uninitialized range must filled with zeros. You can assume that the
 * byte range from offset to offset + size is contained within a single block.
 *
 * Assumptions (already verified by FUSE using getattr() calls):
 *   "path" exists and is a file.
 *
 * Errors:
 *   ENOMEM  not enough memory (e.g. a malloc() call failed).
 *   ENOSPC  not enough free space in the file system.
 *
 * @param path    path to the file to write to.
 * @param buf     pointer to the buffer containing the data.
 * @param size    buffer size (number of bytes requested).
 * @param offset  offset from the beginning of the file to write to.
 * @param fi      unused.
 * @return        number of bytes written on success; -errno on error.
 */
static int a1fs_write(const char *path, const char *buf, size_t size,
                      off_t offset, struct fuse_file_info *fi)
{
    (void)fi;// unused
    fs_ctx *fs = get_fs();

    struct a1fs_superblock * superblock = (struct a1fs_superblock *)(fs->image);
    uint16_t inode_id = path_to_inode_id((char *)path, fs->image);
    struct a1fs_inode * inode =  get_inode_with_id(inode_id, fs->image);

    uint16_t which_datablk = offset / A1FS_BLOCK_SIZE;
    uint16_t which_byte = offset % A1FS_BLOCK_SIZE;
    if(offset + size > inode->size)
    {
        int res = a1fs_truncate(path, (offset + size));
        if(res != 0)
        {
            return -ENOSPC;
        }
    }

    uint16_t datablk_traversed = 0;
    int16_t datablock_pos = -1;
    struct a1fs_extent * first_extent = (struct a1fs_extent *)(fs->image +
                                                               (superblock->data_block * A1FS_BLOCK_SIZE) +
                                                               (inode->extent_table_start * A1FS_BLOCK_SIZE));
    for(int i = 0; i < inode->used_extent_count; ++i)
    {
        struct a1fs_extent * extent = (first_extent + i);
        int start = extent->start;
        int end = extent->start + extent->count;
        for(int j = start; j < end; ++j)
        {
            if(datablk_traversed == which_datablk)
            {
                datablock_pos = j;
                break;
            }
            datablk_traversed++;
        }
        if(datablock_pos != -1)
        {
            break;
        }
    }

    unsigned char * inode_offset_byte = (unsigned char *)(fs->image +
                                                          (superblock->data_block * A1FS_BLOCK_SIZE) + (datablock_pos * A1FS_BLOCK_SIZE) + which_byte);

    for (int i = 0; i < (int)size; ++i)
    {
        inode_offset_byte[i] = buf[i];
    }
    return size;

}


static struct fuse_operations a1fs_ops = {
        .destroy  = a1fs_destroy,
        .statfs   = a1fs_statfs,
        .getattr  = a1fs_getattr,
        .readdir  = a1fs_readdir,
        .mkdir    = a1fs_mkdir,
        .rmdir    = a1fs_rmdir,
        .create   = a1fs_create,
        .unlink   = a1fs_unlink,
        .utimens  = a1fs_utimens,
        .truncate = a1fs_truncate,
        .read     = a1fs_read,
        .write    = a1fs_write,
};

int main(int argc, char *argv[])
{
    a1fs_opts opts = {0};// defaults are all 0
    struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
    if (!a1fs_opt_parse(&args, &opts)) return 1;

    fs_ctx fs = {0};
    if (!a1fs_init(&fs, &opts)) {
        fprintf(stderr, "Failed to mount the file system\n");
        return 1;
    }

    return fuse_main(args.argc, args.argv, &a1fs_ops, &fs);
}
