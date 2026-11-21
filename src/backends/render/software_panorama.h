#ifndef MH_SOFTWARE_PANORAMA_H
#define MH_SOFTWARE_PANORAMA_H

#include "backends/render/software_sky.h"
#include "interfaces/render/renderer.h"

/* Zero once; image pixels are owned, fog row history survives reload/Off. */
struct mh_software_panorama {
	struct mh_software_sky_image	image;
	struct mh_depth_cue_gradient	gradient;
	struct mh_software_sky_state	state;
	int				horizon, enabled, height_cue;
	unsigned int			above, below, clear;
};

/* Inputs have passed renderer contract validation. Replacement is atomic. */
enum mh_error_code	mh_software_panorama_set(
	struct mh_software_panorama *sky, const struct mh_panorama *settings,
	unsigned int width, unsigned int height, unsigned int green_bits,
	struct mh_error *error_out);

#endif
