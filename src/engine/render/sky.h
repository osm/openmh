#ifndef MH_SKY_H
#define MH_SKY_H

#include "engine/core/error.h"
#include "engine/render/effect_quad.h"

enum mh_sky_quad {
	MH_SKY_PANORAMA,
	MH_SKY_TOP_FILL,
	MH_SKY_BOTTOM_FILL,
	MH_SKY_QUADS
};

struct mh_sky_state {
	float	angle, phase;
};

struct mh_sky_camera {
	float	width, height, focal, forward[3], roll_sine;
	float	horizon, image_height, image_scale;
};

/*
 * Three quads: panorama, top fill, bottom fill. Pixel Y points down.
 * image_scale changes panorama geometry without changing its texel interval.
 * image_height=0 selects fresh-session SkyImage Off: zero panorama area,
 * no authored horizon offset, and finite unused texture coordinates.
 */
enum mh_error_code	mh_sky_quads(struct mh_sky_state *state,
	const struct mh_sky_camera *camera,
	struct mh_effect_quad output[MH_SKY_QUADS], struct mh_error *error_out);

#endif
