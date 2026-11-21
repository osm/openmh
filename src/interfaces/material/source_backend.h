#ifndef MH_MATERIAL_SOURCE_BACKEND_H
#define MH_MATERIAL_SOURCE_BACKEND_H

#include "interfaces/material/source.h"

/*
 * Keep collision surface order, names, defaults, binary32 precision and
 * sound/effect IDs. Counts satisfy surface_count <= count <= MH_MATERIAL_LIMIT.
 * Names and sample references are terminated within their inline buffers.
 * Keep finite tuning values, PCM loop units and collision-index correspondence.
 * Context is borrowed. Publish only complete, validated values.
 */
struct mh_material_source {
	void			*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_materials *output,
		struct mh_error	*error_out);
};

#endif
