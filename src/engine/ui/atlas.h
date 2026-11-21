#ifndef MH_UI_ATLAS_H
#define MH_UI_ATLAS_H

#include "engine/assets/image.h"
#include "engine/core/error.h"

struct mh_ui_region {
	unsigned int	x, y, width, height;
};

struct mh_ui_atlas {
	struct mh_image	image;
	unsigned int	x, y, row_height;
};

/*
 * Portable atlas packing. Add images only to an initialized atlas.
 * Atlas owns its RGBA allocation; release it with free(atlas.image.rgba).
 * Initialization/add failures preserve their destinations.
 * Each image has a one-pixel border and two-pixel spacing between entries.
 */
enum mh_error_code	mh_ui_atlas_init(unsigned int width,
	unsigned int height, struct mh_ui_atlas *atlas,
	struct mh_error *error_out);
enum mh_error_code	mh_ui_atlas_add(struct mh_ui_atlas *atlas,
	const struct mh_image *image, struct mh_ui_region *region,
	struct mh_error *error_out);

#endif
