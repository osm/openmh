#ifndef MH_SOFTWARE_LIGHTING_H
#define MH_SOFTWARE_LIGHTING_H

#include "engine/render/lighting.h"

enum mh_software_shade {
	MH_SOFTWARE_SHADE_NORMAL,
	MH_SOFTWARE_SHADE_UNIFORM,
	MH_SOFTWARE_SHADE_CAR
};

/* Borrows prepared lights for the duration of each shade operation. */
struct mh_software_lighting {
	const struct mh_prepared_light	*lights;
	size_t				count;
	float	ambient[3], direction[3], directional_color[3];
	float				specular_scale;
};

/*
 * Prepare one selected world light in object space. The transform contains nine
 * column-major rotation floats followed by the world origin. Coincident
 * directions become zero, as in the portable light preparation policy; the
 * original helper retained its destination in that case. Errors preserve
 * prepared.
 */
enum mh_error_code	mh_software_light_prepare(
	const struct mh_point_light *light, const float transform[12],
	const float eye[3], float intensity, struct mh_prepared_light *prepared,
	struct mh_error *error_out);

/*
 * Shade under the frame's binary32 precision. Normal shading uses model-space
 * normals and prepared vectors. Car shading uses camera-space face normals.
 * Uniform shading ignores the normal. Only active point lights belong in the
 * supplied array. Shades clamp to [0,1] before material multiplication. Output
 * is unchanged on failure.
 */
enum mh_error_code	mh_software_light_shade(
	const struct mh_software_lighting *lighting, const float normal[3],
	enum mh_software_shade mode, float color[3],
	struct mh_error *error_out);

/*
 * Type 0..3 material RGB, before fog/raster saturation; flag 0x10 is emissive.
 */
enum mh_error_code	mh_software_light_color(unsigned int type,
	unsigned int flags, const unsigned char rgb[3],
	const float brightness[3], const float shade[3], float color[3],
	struct mh_error *error_out);

/* Type-8/9 RGB: authored RGB * brightness, then (optionally dimmed) shade. */
enum mh_error_code	mh_software_light_vertex_color(
	const unsigned char rgb[3], const float brightness[3],
	const float shade[3], int dim, float color[3],
	struct mh_error *error_out);

#endif
