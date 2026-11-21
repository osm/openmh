#ifndef MH_INTERFACES_STORAGE_RESOURCE_H
#define MH_INTERFACES_STORAGE_RESOURCE_H

#include "engine/core/error.h"
#include "engine/core/file.h"
#include "interfaces/storage/directory.h"

#define MH_RESOURCE_READ_LIMIT (256UL * 1024UL * 1024UL)

struct mh_resource_source;
struct mh_data_directory;

/*
 * Read exactly size bytes at offset. The prepared resource must stay unchanged
 * across related size/range queries. No allocation or file handle crosses the
 * contract. On failure output may contain a partial read; never parse it.
 * Missing range support returns UNSUPPORTED, without a full-file fallback.
 */
enum mh_error_code	mh_resource_source_read_at(
	const struct mh_resource_source *source, const char *path,
	unsigned long offset, void *output, size_t size,
	struct mh_error *error_out);

/*
 * Bounded whole-file read; the limit is capped at MH_RESOURCE_READ_LIMIT.
 * Reject oversized data before allocating; failure preserves output.
 * Resources must remain unchanged while in use. Namespace selection and path
 * interpretation belong to the provider; this does not confine paths to a root.
 */
enum mh_error_code	mh_resource_source_read_bounded(
	const struct mh_resource_source *source, const char *path, size_t limit,
	struct mh_file *output, struct mh_error *error_out);
enum mh_error_code	mh_resource_source_read(
	const struct mh_resource_source *source, const char *path,
	struct mh_file *output, struct mh_error *error_out);
enum mh_error_code	mh_resource_source_size(
	const struct mh_resource_source *source, const char *path,
	unsigned long *output, struct mh_error *error_out);
enum mh_error_code	mh_resource_source_list(
	const struct mh_resource_source *source, const char *path,
	mh_directory_visit visit, void *context, struct mh_error *error_out);

/*
 * Open a held read-only directory in the prepared resource namespace. Close
 * its files and directory before releasing source. No mutations or directory
 * creation are allowed. Missing directories report NOT_FOUND; unavailable
 * support reports UNSUPPORTED. Failure preserves output. Empty path denotes
 * the logical root. The directory retains source identity for catalog use.
 */
enum mh_error_code	mh_resource_source_open_directory(
	const struct mh_resource_source *source, const char *path,
	struct mh_data_directory **output, struct mh_error *error_out);

#endif
