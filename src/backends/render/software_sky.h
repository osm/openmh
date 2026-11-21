#ifndef MH_SOFTWARE_SKY_H
#define MH_SOFTWARE_SKY_H

#include "backends/render/software_raster.h"
#include "backends/render/software_sky_image.h"

/* Pixel phase and last fog-table top survive frames without an image. */
struct mh_software_sky_state {
	float	angle, phase, top;
};

struct mh_software_sky_camera {
	float	focal, forward[3];
	int	horizon_offset;
};

enum mh_software_sky_mode {
	MH_SOFTWARE_SKY_DIRECT,
	MH_SOFTWARE_SKY_HISTORY
};

/* Use viewport height, not the resized image height. */
enum mh_error_code	mh_software_sky_horizon(unsigned int height,
	float horizon, int *offset, struct mh_error *error_out);

/*
 * Placement, wrapped copying and solid edge fills. Image pixels and packed edge
 * colors must use the surface's 16-bit format. Initialize state to zero once
 * per session. NULL image fills black without advancing state in history mode;
 * direct mode uses clear instead of black. Image width must not exceed the
 * viewport (loader policy). History edge fills include even pitch padding;
 * direct mode and image copies leave it untouched. Validation failures preserve
 * the surface and state.
 */
enum mh_error_code	mh_software_sky_draw(
	const struct mh_software_surface *surface,
	const struct mh_software_sky_image *image,
	const struct mh_software_sky_camera *camera,
	struct mh_software_sky_state *state, enum mh_software_sky_mode mode,
	unsigned int above, unsigned int below, unsigned int clear,
	struct mh_error *error_out);

#endif
