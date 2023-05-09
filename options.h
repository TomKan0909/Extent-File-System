#pragma once

#include <stdbool.h>

#include <fuse_opt.h>


/** a1fs command line options. */
typedef struct a1fs_opts {
	/** a1fs image file path. */
	const char *img_path;
	/** Print help and exit. FUSE option. */
	int help;

} a1fs_opts;

/**
 * Parse a1fs command line options.
 *
 * @param args  pointer to 'struct fuse_args' with the program arguments.
 * @param args  pointer to the options struct that receives the result.
 * @return      true on success; false on failure.
 */
bool a1fs_opt_parse(struct fuse_args *args, a1fs_opts *opts);
