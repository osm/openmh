#ifndef MH_GEOMETRY_WIREFRAME_SOURCE_BACKEND_H
#define MH_GEOMETRY_WIREFRAME_SOURCE_BACKEND_H

#include "interfaces/geometry/wireframe_source.h"

/*
 * Validate counts, finite coordinates and edge indices before returning owned
 * points and edges. Preserve their order and coordinate units.
 * Free partial output on failure. Descriptor/context are borrowed during load.
 */
struct mh_wireframe_source {
	void			*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_wireframe *output,
		struct mh_error	*error_out);
};

#endif
