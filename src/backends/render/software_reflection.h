#ifndef MH_SOFTWARE_REFLECTION_H
#define MH_SOFTWARE_REFLECTION_H

#include "backends/render/software_projection.h"

/*
 * Original type-8/9 reflection coordinates before near clipping. Point cache
 * entries use screen XY, or camera XY when near. The camera transform rotates
 * model normals; translation is ignored. Texture coordinates clamp to [0,255]
 * regardless of the supplied image dimensions. Output is unchanged on failure.
 */
enum mh_error_code	mh_software_reflection_uv(
	const struct mh_projection *projection, const float camera[12],
	const float normal[3], const struct mh_software_point *point,
	unsigned int width, unsigned int height, float uv[2],
	struct mh_error *error_out);

#endif
