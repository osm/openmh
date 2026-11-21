#ifndef MH_ASSET_PATH_H
#define MH_ASSET_PATH_H

#include <stddef.h>

#include "engine/core/error.h"

struct mh_resource_source;

/* Length of the directory prefix, including its last slash or backslash. */
size_t	mh_path_directory_length(const char *path);

/*
 * Resolve DOS separators and ASCII case in the supplied prepared namespace.
 * Reject absolute paths, parent components and ambiguous case matches.
 * This is name resolution, not a security boundary against filesystem links.
 */
enum mh_error_code	mh_asset_path_from(
	const struct mh_resource_source *source, const char *root,
	const char *relative, char *resolved, size_t capacity,
	struct mh_error *error_out);

/*
 * Optional asset lookup. Missing entries set found=0 without an error;
 * malformed paths, enumeration failures and ambiguous names still fail.
 */
enum mh_error_code	mh_asset_path_find_from(
	const struct mh_resource_source *source, const char *root,
	const char *relative, char *resolved, size_t capacity, int *found,
	struct mh_error *error_out);

/*
 * Replace only the basename's last extension, or append one if absent.
 * Extension is a decoder-validated suffix without the dot. Preserve both
 * slash spellings and leave output unchanged on error. Buffers must not
 * overlap. This only builds a name; callers retain required/optional lookup
 * policy, and asset-path resolution still validates namespace components.
 */
enum mh_error_code	mh_asset_reference_extension(const char *reference,
	const char *extension, char *output, size_t capacity,
	struct mh_error *error_out);

#endif
