#ifndef MH_WINDOW_GRAPHICS_CATALOG_H
#define MH_WINDOW_GRAPHICS_CATALOG_H

#include "engine/core/error.h"
#include "engine/ui/menu_graphics.h"

struct mh_graphics_catalog;
struct mh_window_system;

struct mh_graphics_renderer {
	const char	*name;

	/* Zero retains an unavailable profile slot without display modes. */
	unsigned int	max_width;
	int		glenz;
	int		texture_filter;
	int		texture_precision;
	int		frame_queue;
	int		blur_history;
	int		motion_blur;
};

/*
 * Window modes for the application's supplied renderer names, not original
 * hardware enumeration. Requires an initialized window system on its main
 * thread. Common window sizes plus bounded primary-display modes, sorted by
 * width then height without refresh-rate duplicates. Include an exact valid
 * saved size; otherwise select 960x640 or the first supported mode. Enumeration
 * failure retains the common sizes. Each renderer exposes only widths it
 * supports; selected indexes the supplied device's mode list.
 *
 * Names are borrowed until close; the catalog owns its devices and modes. On
 * error, output and selected remain unchanged. The selected window system is
 * required; no native/default display lookup is performed. Presentation selects
 * whether VSync and FPS Limit appear in the menu.
 */
enum mh_error_code			 mh_graphics_catalog_open_from(
	const struct mh_window_system *system, unsigned long width,
	unsigned long height, const struct mh_graphics_renderer *renderers,
	unsigned int count, unsigned int device, int presentation,
	int screenshots, struct mh_graphics_catalog **output,
	unsigned int *selected, struct mh_error *error_out);
const struct mh_menu_graphics_catalog	*mh_graphics_catalog_choices(
	const struct mh_graphics_catalog *catalog);
void					 mh_graphics_catalog_close(
	struct mh_graphics_catalog *catalog);

#endif
