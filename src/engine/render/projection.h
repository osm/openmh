#ifndef MH_PROJECTION_H
#define MH_PROJECTION_H

#include "engine/core/error.h"
#include "engine/render/near_clip.h"

/*
 * Original viewport/lens caches. The inverse aspect is computed before
 * rounding the aspect to binary32, and is not simply 1/clip.aspect.
 */
struct mh_projection {
	struct mh_near_projection	clip;
	float far_distance, inverse_far, inverse_focal, inverse_half_width,
		inverse_aspect;

	/* Cached horizontal and negative vertical factors. */
	float				scale[2];
};

struct mh_projected_point {
	/* Screen XY, Z/far, 1/Z; camera XYZ when outcode=MH_CLIP_NEAR. */
	float		value[4];

	/* Combination of mh_clip_code bits. */
	unsigned int	outcode;
};

/*
 * Lens values have already passed the camera's clamp policy. aspect is the
 * viewport aspect, normally width/height.
 */
enum mh_error_code	mh_projection_setup(unsigned int width,
	unsigned int height, float aspect, float focal, float near_distance,
	float far_distance, struct mh_projection *output,
	struct mh_error *error_out);

/*
 * Affine camera transform is 12 column-major floats, with positive forward Z.
 * Near equality is behind; far equality is culled. Near points preserve
 * value[3], and far points preserve all four values. Errors preserve the entire
 * output; inputs may alias it.
 */
enum mh_error_code	mh_project_point(const struct mh_projection *projection,
	const float transform[12], const float point[3],
	struct mh_projected_point *output, struct mh_error *error_out);

/*
 * Restore camera coordinates for the near splitter, retaining the original
 * projection/reconstruction float stores. Far points are invalid here. Errors
 * preserve output.
 */
enum mh_error_code	mh_projection_restore(
	const struct mh_projection *projection,
	const struct mh_projected_point *point, float camera[3],
	struct mh_error *error_out);

#endif
