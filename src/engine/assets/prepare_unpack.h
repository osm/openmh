#ifndef MH_ASSETS_PREPARE_UNPACK_H
#define MH_ASSETS_PREPARE_UNPACK_H

#include "interfaces/archive/archive.h"

struct mh_user_data;

/* Return failure to cancel between members; callbacks run synchronously. */
typedef enum mh_error_code (*mh_unpack_progress)(void *context,
	const char *path, size_t completed, size_t total,
	struct mh_error *error_out);

/*
 * Decode into a private, initially empty staging root owned by the caller.
 * On failure partial staged files remain for that owner to discard; this
 * function never publishes an installation or writes to source resources.
 */
enum mh_error_code	mh_archive_unpack(struct mh_archive *archive,
	const struct mh_user_data *data, const char *root,
	mh_unpack_progress progress, void *context, struct mh_error *error_out);

#endif
