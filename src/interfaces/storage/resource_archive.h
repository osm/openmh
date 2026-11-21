#ifndef MH_RESOURCE_ARCHIVE_H
#define MH_RESOURCE_ARCHIVE_H

#include "interfaces/storage/resource.h"

/*
 * Mount an immutable archive as read-only resources rooted at its own path.
 * The returned view owns its index and borrows source through close. Source
 * must support range reads and remain unchanged. Close all readers first.
 */
struct mh_resource_archive_provider {
	enum mh_error_code (*open)(const struct mh_resource_source *source,
		const char *path, const struct mh_resource_source **output,
		struct mh_error	*error_out);
	void	(*close)(const struct mh_resource_source *resources);
};

#endif
