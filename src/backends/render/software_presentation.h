#ifndef MH_SOFTWARE_PRESENTATION_H
#define MH_SOFTWARE_PRESENTATION_H

#include "backends/render/software_raster.h"
#include "interfaces/window/window.h"

struct mh_software_presentation;
struct mh_software_output;

/*
 * NULL window selects offscreen operation and returns a NULL presentation.
 * Otherwise services and window remain borrowed through close. Failed open
 * leaves output unchanged; close accepts NULL.
 */
enum mh_error_code	mh_software_presentation_open(
	const struct mh_software_output *services, struct mh_window *window,
	unsigned int width, unsigned int height,
	struct mh_software_presentation **output, struct mh_error *error_out);
void			mh_software_presentation_close(
	struct mh_software_presentation *presentation);

/* Completed RGB565 surface at the open dimensions; NULL is offscreen. */
int	mh_software_present(struct mh_software_presentation *presentation,
	const struct mh_software_surface *surface);

/* Redraw the retained frame before reading the window-sized image. */
int	mh_software_capture(struct mh_software_presentation *presentation,
	const struct mh_software_surface *surface, const char *path,
	struct mh_error *error_out);

enum mh_error_code	mh_software_presentation_set_vsync(
	struct mh_software_presentation *presentation, int enabled,
	struct mh_error *error_out);

#endif
