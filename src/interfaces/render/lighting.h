#ifndef MH_INTERFACES_RENDER_LIGHTING_H
#define MH_INTERFACES_RENDER_LIGHTING_H

#include "engine/render/lighting.h"

/* World-space selected lights; ambient/directional RGB includes object tint. */
struct mh_model_lighting {
	const struct mh_point_light	*lights;
	size_t				light_count;
	float	eye[3], ambient[3], direction[3], directional_color[3];
	float	brightness[3], intensity, specular_scale;
	enum mh_light_mode		mode;

	/*
	 * Original per-object car flag: textured faces use face-plane shading.
	 */
	int				flat_car;

	/* Original reduced-detail flag: type-8/9 faces use uniform lighting. */
	int				flat_detail;
};

#endif
