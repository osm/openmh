#ifndef MH_IMAGE_ATLAS_SOURCE_H
#define MH_IMAGE_ATLAS_SOURCE_H

#include "engine/core/error.h"
#include "interfaces/image/atlas_map.h"

struct mh_atlas_source;
struct mh_resource_source;

/*
 * Load a logical atlas reference into independently owned decoded entries.
 * The source resolves physical names and encodings. Input resources and source
 * state are borrowed during loading; output retains neither. Failure preserves
 * output. Release successful output with mh_atlas_map_free before replacing it.
 * Diagnostics survive until the next source operation.
 */
enum mh_error_code	mh_atlas_map_load_from(
	const struct mh_atlas_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_atlas_map *output,
	struct mh_error *error_out);

#endif
