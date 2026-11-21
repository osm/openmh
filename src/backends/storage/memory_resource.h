#ifndef MH_BACKENDS_STORAGE_MEMORY_RESOURCE_H
#define MH_BACKENDS_STORAGE_MEMORY_RESOURCE_H

#include "interfaces/storage/resource_backend.h"

struct mh_resource_entry {
	const char		*path;
	const unsigned char	*data;
	size_t			size;
};

/*
 * A read-only prepared tree. Keys are unique, case-sensitive, slash-separated
 * paths; directories are inferred from their children. The empty path lists
 * the root. The provider copies the entry table; names and bytes remain
 * borrowed and unchanged through close. Reads return copies that may outlive
 * the provider. This provider owns no files, threads or external callbacks.
 * Close it only after all users of the returned source finish. Output is
 * unchanged on error.
 */
enum mh_error_code	mh_memory_resources_open(
	const struct mh_resource_entry *entries, size_t count,
	const struct mh_resource_source **output, struct mh_error *error_out);
void			mh_memory_resources_close(
	const struct mh_resource_source *source);

#endif
