#ifndef MH_PALETTE_SOURCE_BACKEND_H
#define MH_PALETTE_SOURCE_BACKEND_H

#include "interfaces/image/palette.h"

/*
 * Context is borrowed. Keep ordered 24-bit colors; no alpha or pixel
 * allocations are returned. Publish complete validated output atomically.
 */
struct mh_palette_source {
	void			*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, unsigned long output[MH_PALETTE_COLORS],
		struct mh_error	*error_out);
};

#endif
