#ifndef MH_FILESYSTEM_TREE_H
#define MH_FILESYSTEM_TREE_H

#include "engine/core/error.h"

typedef enum mh_error_code (*mh_filesystem_copy_progress)(
	void *context, const char *name, struct mh_error *error_out);

/* Copy regular files/directories into private staging. Reject links/devices. */
enum mh_error_code	mh_filesystem_copy_tree(const char *source,
	const char *destination, mh_filesystem_copy_progress progress,
	void *context, struct mh_error *error_out);

#endif
