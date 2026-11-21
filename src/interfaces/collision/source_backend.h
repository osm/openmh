#ifndef MH_COLLISION_SOURCE_BACKEND_H
#define MH_COLLISION_SOURCE_BACKEND_H

#include "interfaces/collision/source.h"

/* Read/decode share borrowed context. NULL read selects normal file lookup. */
struct mh_collision_source {
	void			*context;

	/*
	 * Optional file extension; NULL retains the logical reference suffix.
	 */
	const char		*extension;
	enum mh_error_code (*read)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_file *output,
		struct mh_error	*error_out);
	enum mh_error_code (*decode)(void *context, const unsigned char *data,
		size_t size, struct mh_collision *output,
		struct mh_error	*error_out);
};

#endif
