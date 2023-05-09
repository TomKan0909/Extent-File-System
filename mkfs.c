#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <time.h>
#include "a1fs.h"
#include "map.h"
#include <inttypes.h>


/** Command line options. */
typedef struct mkfs_opts {
    /** File system image file path. */
    const char *img_path;
    /** Number of inodes. */
    size_t n_inodes;

    /** Print help and exit. */
    bool help;
    /** Overwrite existing file system. */
    bool force;
    /** Zero out image contents. */
    bool zero;

} mkfs_opts;

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



static const char *help_str = "\
Usage: %s options image\n\
\n\
Format the image file into a1fs file system. The file must exist and\n\
its size must be a multiple of a1fs block size - %zu bytes.\n\
\n\
Options:\n\
    -i num  number of inodes; required argument\n\
    -h      print help and exit\n\
    -f      force format - overwrite existing a1fs file system\n\
    -z      zero out image contents\n\
";

static void print_help(FILE *f, const char *progname)
{
    fprintf(f, help_str, progname, A1FS_BLOCK_SIZE);
}


static bool parse_args(int argc, char *argv[], mkfs_opts *opts)
{
    char o;
    while ((o = getopt(argc, argv, "i:hfvz")) != -1) {
        switch (o) {
            case 'i': opts->n_inodes = strtoul(optarg, NULL, 10); break;

            case 'h': opts->help  = true; return true;// skip other arguments
            case 'f': opts->force = true; break;
            case 'z': opts->zero  = true; break;

            case '?': return false;
            default : assert(false);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Missing image path\n");
        return false;
    }
    opts->img_path = argv[optind];

    if (opts->n_inodes == 0) {
        fprintf(stderr, "Missing or invalid number of inodes\n");
        return false;
    }
    return true;
}


/** Determine if the image has already been formatted into a1fs. */
static bool a1fs_is_present(void *image)
{
    //TODO: check if the image already contains a valid a1fs superblock
    struct a1fs_superblock * superblock = (struct a1fs_superblock *)(image);
    if(superblock->magic & A1FS_MAGIC)
    {
        return true;
    }

    return false;
}


/**
 * Format the image into a1fs.
 *
 * NOTE: Must update mtime of the root directory.
 *
 * @param image  pointer to the start of the image.
 * @param size   image size in bytes.
 * @param opts   command line options.
 * @return       true on success;
 *               false on error, e.g. options are invalid for given image size.
 */
static bool mkfs(void *image, size_t size, mkfs_opts *opts)
{
    //TODO: initialize the superblock and create an empty root directory
    //NOTE: the mode of the root directory inode should be set to S_IFDIR | 0777

    // opts includes char * img_path, size_t n_inodes, other fields not necessary
    // relevant information is size_t n_inodes and size_t size,

    size_t n_inodes = opts->n_inodes;

    // Uncomment line below to check if superblock was succesfully written to image
    // struct a1fs_superblock cpy = *(image); /* Check if data is correct */

    // Calculate the number of blocks required to store the bitmaps, 1 byte = 8 bits
    int data_block_bitmap_blocks = my_ceil(size , A1FS_BLOCK_SIZE * A1FS_BLOCK_SIZE * 8);
    int inode_bitmap_blocks = my_ceil(n_inodes,A1FS_BLOCK_SIZE * 8);
    int inode_table_blocks = my_ceil(n_inodes * sizeof(a1fs_inode), A1FS_BLOCK_SIZE);

    // Calculate the number of data blocks used so far
    int total_dblocks = data_block_bitmap_blocks + inode_bitmap_blocks + inode_table_blocks + 1;
    // CREATE SUPERBLOCK
    struct a1fs_superblock superblock;
    superblock.size = size;
    superblock.magic = A1FS_MAGIC;
    superblock.inodes_count = n_inodes;
    superblock.blocks_count = my_ceil(size, A1FS_BLOCK_SIZE);
    superblock.free_blocks_count = superblock.blocks_count - total_dblocks;
    superblock.free_inodes_count = n_inodes - 1;
    superblock.inode_block_bitmap = 1;
    superblock.data_block_bitmap = superblock.inode_block_bitmap + inode_bitmap_blocks;
    superblock.inode_table = superblock.data_block_bitmap + data_block_bitmap_blocks;
    superblock.data_block = superblock.inode_table + inode_table_blocks;
    memcpy( image, &superblock, sizeof(a1fs_superblock));



    // Check to see if data was written to superblock
    struct a1fs_superblock * cpy = (struct a1fs_superblock *)(image);
    printf("--------------CHECKING SUPERBLOCK ALLOCATION START---------------\n");
    printf("size: %lu\n", cpy->size);
    printf("inodes_count: %lu\n", cpy->inodes_count);
    printf("blocks_count: %lu\n", cpy->blocks_count);
    printf("free_blocks_count: %lu\n", cpy->free_blocks_count);
    printf("free_inodes_count: %lu\n", cpy->free_inodes_count);
    printf("inode_block_bitmap: %u\n", cpy->inode_block_bitmap);
    printf("data_block_bitmap: %u\n", cpy->data_block_bitmap);
    printf("inode_table: %u\n", cpy->inode_table);
    printf("data_block: %u\n", cpy->data_block);
    printf("------------CHECKING SUPERBLOCK ALLOCATION COMPLETE---------------\n");



    // Set First Inode to in use in inode bitmap
    u_char val = 1;
    memcpy(image + (superblock.inode_block_bitmap * A1FS_BLOCK_SIZE), &val, sizeof(u_char));
    printf("---------------CHECKING INODE BITMAP START----------------------\n");
    int j = 0;
    int counter = 0;
    while((unsigned int )j < n_inodes)
    {
        int which_bit = j % 8;
        int offset = j/8;
        unsigned char * inode_block_bitmap_byte = (unsigned char *)(image +
                                                                    (superblock.inode_block_bitmap * A1FS_BLOCK_SIZE) + offset);
        if(*inode_block_bitmap_byte & (1<< which_bit))
        {
            printf("%d-th bit: 1\n", j);
            counter++;
        }
        else
        {
            printf("%d-th bit: 0\n", j);
        }
        j++;
    }
    printf("total bits that are used: %d\n", counter);
    printf("total bits that should be used: %d\n", 1);
    printf("------------------CHECKING INODE BITMAP COMPLETE-------------------------\n");

    // Set first total_dblocks to in use in data blockbitmap
    for(int i = 0; i < total_dblocks; i++)
    {
        int which_bit = i % 8;
        int offset = i/8;
        unsigned char * data_block_bitmap_byte = (unsigned char *)(image +
                                                                   (superblock.data_block_bitmap * A1FS_BLOCK_SIZE) + offset);
        *data_block_bitmap_byte |= (1 << which_bit);
//        printf("curr_val of data_block_bitmap_byte: %u, offset: %d\n", *data_block_bitmap_byte, offset);
        //set i-th bit in data block bitmap to 1

    }

    // Check for datablockbitmap was intialized correctly
    printf("----------------CHECKING DATABLOCK BITMAP START-------------------------\n");
    j = 0;
    counter = 0;
    while((unsigned int)j < superblock.blocks_count)
    {
        int which_bit = j % 8;
        int offset = j/8;
        unsigned char * data_block_bitmap_byte = (unsigned char *)(image +
                                                                   (superblock.data_block_bitmap * A1FS_BLOCK_SIZE) + offset);
        if(*data_block_bitmap_byte & (1<< which_bit))
        {
            printf("%d-th bit: 1\n", j);
            counter++;
        }
        else
        {
            printf("%d-th bit: 0\n", j);
        }
        j++;
    }
    printf("total bits that are used: %d\n", counter);
    printf("total bits that should be used: %d\n", total_dblocks);
    printf("----------------CHECKING DATABLOCK BITMAP END-------------------------\n");

//


    // Create Root Inode
    struct a1fs_inode root_inode;
    root_inode.mode = A1FS_S_IFDIR;
    root_inode.links = 2; // one from parent and from itself
    root_inode.size = 0;
    clock_gettime( CLOCK_REALTIME, &(root_inode.mtime));
    root_inode.used_extent_count = 0;
    root_inode.extent_table_start = -1;
    memcpy(image + (superblock.inode_table * A1FS_BLOCK_SIZE), &root_inode, sizeof(a1fs_inode));


    return true;

//    (void)image;
//	(void)size;
//	(void)opts;
//	return false;
}


int main(int argc, char *argv[])
{
    mkfs_opts opts = {0};// defaults are all 0
    if (!parse_args(argc, argv, &opts)) {
        // Invalid arguments, print help to stderr
        print_help(stderr, argv[0]);
        return 1;
    }
    if (opts.help) {
        // Help requested, print it to stdout
        print_help(stdout, argv[0]);
        return 0;
    }

    // Map image file into memory
    size_t size;
    void *image = map_file(opts.img_path, A1FS_BLOCK_SIZE, &size);
    if (image == NULL) return 1;

    // Check if overwriting existing file system
    int ret = 1;
    if (!opts.force && a1fs_is_present(image)) {
        fprintf(stderr, "Image already contains a1fs; use -f to overwrite\n");
        goto end;
    }

    if (opts.zero) memset(image, 0, size);
    if (!mkfs(image, size, &opts)) {
        fprintf(stderr, "Failed to format the image\n");
        goto end;
    }

    ret = 0;
    end:
    munmap(image, size);
    return ret;
}