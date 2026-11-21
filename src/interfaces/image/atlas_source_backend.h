#ifndef MH_IMAGE_ATLAS_SOURCE_BACKEND_H
#define MH_IMAGE_ATLAS_SOURCE_BACKEND_H

#include "interfaces/image/atlas_source.h"

/*
 * Validate dimensions, scales, page/rectangle bounds and names before returning
 * an owned map. Preserve entry order, transpose and texture registration names.
 * Free partial output on failure. Descriptor/context are borrowed during load.
 */
struct mh_atlas_source {
	void			*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_atlas_map *output,
		struct mh_error	*error_out);
};

#endif
