#include <float.h>
#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/near_clip.h"

#define ATTRIBUTE_LIMIT 1e9f
#define FACTOR_LIMIT 1e6f
#define NEAR_MIN 1e-6f
#define VISIBLE_DEPTH_MIN 1e-9f
#define VIEWPORT_LIMIT 16384
#define DEPTH_SCALE .99999

static int
bounded(float value)
{
	return value >= -ATTRIBUTE_LIMIT && value <= ATTRIBUTE_LIMIT;
}

static int
store_float(long double value, float *output)
{
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		return 0;

	*output = (float)value;

	return 1;
}

static void
copy_corner(const struct mh_near_corner *input,
	struct mh_projected_corner *corner, unsigned int flags)
{
	corner->xy[0] = input->camera[0];

	corner->xy[1] = input->camera[1];
	corner->reciprocal_w = (float)(1 / (long double)input->camera[2]);

	if (flags & MH_NEAR_TEXTURED)
		memcpy(corner->uv, input->uv, sizeof(corner->uv));

	if (flags & MH_NEAR_COLOR_MASK)
		memcpy(corner->rgb, input->rgb, sizeof(corner->rgb));
}

static enum mh_error_code
intersect_edge(const struct mh_near_projection *projection,
	const struct mh_near_corner *a, const struct mh_near_corner *b,
	unsigned int flags, struct mh_projected_corner *corner, int *crossing)
{
	float dx, dy, dz, t, difference;
	long double ratio;
	unsigned int component;

	*crossing = 0;

	dx = (float)((long double)b->camera[0] - a->camera[0]);
	dy = (float)((long double)b->camera[1] - a->camera[1]);
	dz = (float)((long double)b->camera[2] - a->camera[2]);

	if (dz == 0)
		return MH_ERROR_NONE;

	ratio = ((long double)projection->near_distance - a->camera[2]) / dz;

	if (!store_float(ratio, &t))
		return MH_ERROR_RANGE;

	/*
	 * The first X consumes the extended quotient left after the store;
	 * subsequent attributes reload the binary32 parameter.
	 */
	if (!store_float(ratio * dx + a->camera[0], corner->xy) ||
		!store_float(
			(long double)t * dy + a->camera[1], corner->xy + 1))
		return MH_ERROR_RANGE;

	corner->reciprocal_w = projection->inverse_near;

	if (flags & MH_NEAR_TEXTURED)
		for (component = 0; component < 2; component++) {
			difference = (float)((long double)b->uv[component] -
				a->uv[component]);

			if (!store_float((long double)t * difference +
					a->uv[component],
				corner->uv + component))
				return MH_ERROR_RANGE;
		}

	if (flags & MH_NEAR_COLOR_MASK)
		for (component = 0; component < 3; component++) {
			difference = (float)((long double)b->rgb[component] -
				a->rgb[component]);

			if (!store_float((long double)t * difference +
					a->rgb[component],
				corner->rgb + component))
				return MH_ERROR_RANGE;
		}

	*crossing = 1;

	return MH_ERROR_NONE;
}

static enum mh_error_code
project_corner(const struct mh_near_projection *projection, float horizontal,
	float vertical, struct mh_projected_corner *corner,
	unsigned int *outcode, struct mh_error *error_out)
{
	unsigned int component;

	if (!store_float((long double)corner->xy[0] * horizontal *
				corner->reciprocal_w +
			projection->center[0],
		corner->xy) ||
		!store_float(projection->center[1] -
				(long double)corner->xy[1] * vertical *
					corner->reciprocal_w,
			corner->xy + 1))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"near-clip projection exceeds native range");

	corner->depth = (float)((long double)projection->near_distance *
		corner->reciprocal_w * (double)DEPTH_SCALE);

	if (!bounded(corner->xy[0]) || !bounded(corner->xy[1]) ||
		!bounded(corner->depth) || !bounded(corner->reciprocal_w))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"near-clip projection exceeds native range");

	for (component = 0; component < 2; component++)
		if (!bounded(corner->uv[component]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"near-clip UV exceeds native range");

	for (component = 0; component < 3; component++)
		if (!bounded(corner->rgb[component]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"near-clip color exceeds native range");

	*outcode = corner->xy[0] < 0			? MH_CLIP_LEFT
		: corner->xy[0] > projection->extent[0] ? MH_CLIP_RIGHT
							: 0;
	*outcode |= corner->xy[1] < 0			? MH_CLIP_TOP
		: corner->xy[1] > projection->extent[1] ? MH_CLIP_BOTTOM
							: 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_clip_near(const struct mh_near_projection *projection, unsigned int flags,
	unsigned int near_mask, const struct mh_near_corner corners[4],
	struct mh_projected_face output[2], unsigned int *count,
	struct mh_error *error_out)
{
	static const unsigned int triangles[2][3] = {{0, 1, 2}, {0, 2, 3}};
	struct mh_projected_face faces[2];
	struct mh_projected_face *face;
	struct mh_projected_corner *corner;
	unsigned int axis, vertex, component, triangle, edge, a, b,
		outcode = 0, common_outcode, corner_count, faces_used,
		input_count;
	float horizontal, vertical;
	int front_a, front_b, crossing;
	enum mh_error_code status;

	if (projection == NULL || corners == NULL || output == NULL ||
		count == NULL || flags > MH_U16_MAX ||
		!(projection->near_distance >= NEAR_MIN &&
			projection->near_distance <= FACTOR_LIMIT) ||
		!(projection->inverse_near > 0 &&
			projection->inverse_near <= FACTOR_LIMIT) ||
		!(projection->focal > 0 && projection->focal <= FACTOR_LIMIT) ||
		!(projection->half_width > 0 &&
			projection->half_width <= VIEWPORT_LIMIT) ||
		!(projection->aspect > 0 && projection->aspect <= FACTOR_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid near-clip projection");

	for (axis = 0; axis < 2; axis++)
		if (!bounded(projection->center[axis]) ||
			!(projection->extent[axis] >= 1 &&
				projection->extent[axis] <= VIEWPORT_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid near-clip viewport");

	input_count = flags & MH_NEAR_QUAD ? 4 : 3;

	if (near_mask >= (1U << input_count))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid near-clip mask");

	for (vertex = 0; vertex < input_count; vertex++) {
		for (component = 0; component < 3; component++)
			if (!bounded(corners[vertex].camera[component]) ||
				!bounded(corners[vertex].rgb[component]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid near-clip corner");

		for (component = 0; component < 2; component++)
			if (!bounded(corners[vertex].uv[component]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid near-clip UV");

		if (!(near_mask & (1U << vertex)) &&
			!(corners[vertex].camera[2] >= VISIBLE_DEPTH_MIN))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid near-clip visible depth");
	}

	memset(faces, 0, sizeof(faces));
	faces_used = 0;
	horizontal = (float)((long double)projection->focal *
		projection->half_width);
	vertical = (float)((long double)projection->focal *
		projection->half_width * projection->aspect);

	for (triangle = 0; triangle < (input_count == 4 ? 2U : 1U);
		triangle++) {
		face = &faces[faces_used];
		memset(face, 0, sizeof(*face));
		face->flags = flags & ~(unsigned int)MH_NEAR_QUAD;

		/*
		 * The original copies flat RGB once, independently of whether
		 * the source corner survives. Per-corner modes overwrite this
		 * later.
		 */
		memcpy(face->corners[0].rgb, corners[0].rgb,
			sizeof(corners[0].rgb));
		corner_count = 0;

		for (edge = 0; edge < 3; edge++) {
			a = triangles[triangle][edge];
			b = triangles[triangle][(edge + 1) % 3];
			front_a = !(near_mask & (1U << a));
			front_b = !(near_mask & (1U << b));

			if (front_a)
				copy_corner(corners + a,
					&face->corners[corner_count++], flags);

			if (front_a != front_b) {
				status = intersect_edge(projection, corners + a,
					corners + b, flags,
					&face->corners[corner_count++],
					&crossing);

				if (status != MH_ERROR_NONE)
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"near-clip interpolation exceeds native range");

				if (!crossing) {
					/* Original aborts both halves. */
					*count = 0;
					return mh_error_set(
						error_out, MH_ERROR_NONE, NULL);
				}
			}
		}

		if (corner_count < 3)
			continue;

		face->count = corner_count;

		if (corner_count == 4)
			face->flags |= MH_NEAR_QUAD;

		common_outcode = MH_CLIP_VIEWPORT;

		for (vertex = 0; vertex < corner_count; vertex++) {
			corner = &face->corners[vertex];

			status = project_corner(projection, horizontal,
				vertical, corner, &outcode, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			common_outcode &= outcode;
		}

		if (!common_outcode)
			faces_used++;
	}

	if (faces_used != 0)
		memcpy(output, faces, faces_used * sizeof(*output));

	*count = faces_used;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
