#ifndef MH_CLIENT_DIAGNOSTICS_RENDER_GRAPHICS_CATALOG_H
#define MH_CLIENT_DIAGNOSTICS_RENDER_GRAPHICS_CATALOG_H

#include "engine/window/graphics_catalog.h"

/*
 * Native convenience; selects the initialized SDL window system.
 * Catalog ownership and mode selection follow the portable _from operation.
 */
enum mh_error_code	mh_graphics_catalog_open(unsigned long width,
	unsigned long height, const struct mh_graphics_renderer *renderers,
	unsigned int count, unsigned int device,
	struct mh_graphics_catalog **output, unsigned int *selected,
	struct mh_error *error_out);

#endif
