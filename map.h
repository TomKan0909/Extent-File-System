#pragma once

#include <stddef.h>


/**
 * Map the whole file into memory for reading and writing.
 *
 * File size must be a non-zero multiple of the block_size.
 *
 * @param path        image file path.
 * @param block_size  file system block size.
 * @param size        pointer to the variable that will be set to file size.
 * @return            pointer to the file mapping in memory on success;
 *                    NULL on failure.
 */
void *map_file(const char *path, size_t block_size, size_t *size);
