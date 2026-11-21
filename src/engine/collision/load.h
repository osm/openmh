#ifndef MH_COLLISION_LOAD_H
#define MH_COLLISION_LOAD_H

#include "engine/core/file.h"
#include "interfaces/collision/source.h"

struct mh_resource_source;

/* Normal case-insensitive file lookup, also used by provider composition. */
enum mh_error_code	mh_collision_file_read(
	const struct mh_collision_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *output,
	struct mh_error *error_out);

/* Resolve a logical reference using the selected source's optional suffix. */
enum mh_error_code	mh_collision_read_from(
	const struct mh_collision_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *output,
	struct mh_error *error_out);

/*
 * Owned decoded output; both encoded input and provider are released from use.
 */
enum mh_error_code	mh_collision_load_from(
	const struct mh_collision_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_collision *output,
	struct mh_error *error_out);

#endif
