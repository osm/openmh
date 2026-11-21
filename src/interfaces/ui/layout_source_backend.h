#ifndef MH_LAYOUT_SOURCE_BACKEND_H
#define MH_LAYOUT_SOURCE_BACKEND_H

#include "interfaces/ui/layout_source.h"

/*
 * Context is borrowed. Keep signed 32-bit fields, coordinate bounds, effect
 * IDs/parameters and opaque fields 4/6. Preserve fixed page/slot order and
 * numeric units. Publish complete validated output atomically.
 */
struct mh_menu_layout_source {
	void			*context;
	enum mh_error_code (*load)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_menu_layout *output,
		struct mh_error	*error_out);
};

#endif
