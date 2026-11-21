#include "backends/window/sdl_provider.h"
#include "client/diagnostics/render_graphics_catalog.h"
#include "engine/window/graphics_catalog.h"
#include "interfaces/window/backend.h"

enum mh_error_code
mh_graphics_catalog_open(unsigned long width, unsigned long height,
	const struct mh_graphics_renderer *renderers, unsigned int count,
	unsigned int device, struct mh_graphics_catalog **output,
	unsigned int *selected, struct mh_error *error_out)
{
	return mh_graphics_catalog_open_from(mh_sdl_window_system(), width,
		height, renderers, count, device, 1, 1, output, selected,
		error_out);
}
