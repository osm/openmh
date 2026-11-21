#ifndef MH_RETRO_SOURCE_H
#define MH_RETRO_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/retro/data.h"

struct mh_retro_source;
struct mh_resource_source;

/* Load one independent part. Missing data is MH_ERROR_NOT_FOUND. */
enum mh_error_code	mh_retro_load_part_from(
	const struct mh_retro_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, enum mh_retro_part part,
	union mh_retro_part_data *output, struct mh_error *error_out);

/*
 * Load validated decoded effect data by logical reference. Sources/resources
 * are borrowed for the call. Output owns inline values, survives source
 * teardown and is unchanged on failure. Preserve mesh/face order, motion units,
 * palette roles and original single-byte story text.
 */
enum mh_error_code	mh_retro_load_from(const struct mh_retro_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_retro_assets *output,
	struct mh_error *error_out);

#endif
