#ifndef MH_STORAGE_USER_ROOT_H
#define MH_STORAGE_USER_ROOT_H

#include "engine/core/error.h"

struct mh_user_data;

/*
 * Compose an existing writable view with a different default directory.
 * Explicit keys/paths pass through unchanged; this is not a filesystem sandbox.
 * root is copied. source remains borrowed through the last directory/file user.
 * Open performs no I/O. Querying the default directory creates it through the
 * source provider and returns an owned copy, matching ordinary user-data roots.
 * Close after all users of this view; PREPARE/COMMIT stay with the same storage
 * owner. Open leaves output unchanged on failure.
 */
enum mh_error_code	mh_user_root_open(const struct mh_user_data *source,
	const char *root, const struct mh_user_data **output,
	struct mh_error *error_out);
void			mh_user_root_close(const struct mh_user_data *data);

#endif
