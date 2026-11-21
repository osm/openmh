#include <float.h>
#include <stddef.h>
#include <string.h>

#include "backends/render/software_near_clip.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"

#define ATTRIBUTE_LIMIT 1e9f
#define FACTOR_LIMIT 1e6f
#define NEAR_MIN 1e-6f
#define VISIBLE_DEPTH_MIN 1e-9f
#define VIEWPORT_LIMIT 16384

static int
bounded(float value)
{
	return value >= -ATTRIBUTE_LIMIT && value <= ATTRIBUTE_LIMIT;
}

static int
store_float(double value, float *output)
{
	if (!(value >= -FLT_MAX && value <= FLT_MAX))
		return 0;

	*output = (float)value;

	return 1;
}

static void
copy_corner(const struct mh_near_corner *input,
	struct mh_software_clipped_corner *corner, unsigned int flags)
{
	corner->xy[0] = input->camera[0];

	corner->xy[1] = input->camera[1];

	/* Camera Z is retained until project_corner replaces it with 1/Z. */
	corner->weight = input->camera[2];

	if (flags & MH_NEAR_TEXTURED)
		memcpy(corner->uv, input->uv, sizeof(corner->uv));

	if (flags & MH_NEAR_COLOR_MASK)
		memcpy(corner->rgb, input->rgb, sizeof(corner->rgb));
}

/* Round each interpolation step to float to match the software projection. */
static int
interpolate(float a, float b, float t, float *output)
{
	float difference, product;

	return store_float((double)b - a, &difference) &&
		store_float((double)t * difference, &product) &&
		store_float((double)product + a, output);
}

static enum mh_error_code
intersect_edge(const struct mh_near_projection *projection,
	const struct mh_near_corner *a, const struct mh_near_corner *b,
	unsigned int flags, struct mh_software_clipped_corner *corner,
	struct mh_error *error_out)
{
	float dz, difference, t;
	unsigned int i;

	dz = (float)((double)b->camera[2] - a->camera[2]);

	if (!store_float((double)projection->near_distance - a->camera[2],
		&difference) ||
		!store_float((double)difference / dz, &t))
		goto range;

	for (i = 0; i < 2; i++)
		if (!interpolate(a->camera[i], b->camera[i], t, &corner->xy[i]))
			goto range;

	corner->weight = projection->near_distance;

	if (flags & MH_NEAR_TEXTURED)
		for (i = 0; i < 2; i++)
			if (!interpolate(a->uv[i], b->uv[i], t, &corner->uv[i]))
				goto range;

	if (flags & MH_NEAR_COLOR_MASK)
		for (i = 0; i < 3; i++)
			if (!interpolate(
				a->rgb[i], b->rgb[i], t, &corner->rgb[i]))
				goto range;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);

range:

	return mh_error_set(error_out, MH_ERROR_RANGE,
		"software near-clip interpolation exceeds native range");
}

static enum mh_error_code
project_corner(const struct mh_near_projection *projection, float horizontal,
	float vertical, struct mh_software_clipped_corner *corner,
	unsigned int *outcode, struct mh_error *error_out)
{
	unsigned int axis, channel;
	float x, y;

	if (!store_float(1.0 / corner->weight, &corner->weight) ||
		!store_float((double)corner->xy[0] * horizontal, &x) ||
		!store_float((double)x * corner->weight, &x) ||
		!store_float(
			(double)x + projection->center[0], &corner->xy[0]) ||
		!store_float((double)corner->xy[1] * vertical, &y) ||
		!store_float((double)y * corner->weight, &y) ||
		!store_float(
			(double)projection->center[1] - y, &corner->xy[1]) ||
		!bounded(corner->xy[0]) || !bounded(corner->xy[1]) ||
		!bounded(corner->weight))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software near-clip projection exceeds native range");

	for (axis = 0; axis < 2; axis++)
		if (!bounded(corner->uv[axis]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software near-clip UV exceeds native range");

	for (channel = 0; channel < 3; channel++)
		if (!bounded(corner->rgb[channel]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software near-clip color exceeds native range");

	*outcode = corner->xy[0] < 0			? MH_CLIP_LEFT
		: corner->xy[0] > projection->extent[0] ? MH_CLIP_RIGHT
							: 0;
	*outcode |= corner->xy[1] < 0			? MH_CLIP_TOP
		: corner->xy[1] > projection->extent[1] ? MH_CLIP_BOTTOM
							: 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_clip_near(const struct mh_near_projection *projection,
	unsigned int flags, unsigned int near_mask,
	const struct mh_near_corner corners[4],
	struct mh_software_clipped_face output[2], unsigned int *count,
	struct mh_error *error_out)
{
	static const unsigned int triangles[2][3] = {{0, 1, 2}, {0, 2, 3}};
	struct mh_software_clipped_face faces[2];
	struct mh_software_clipped_face *face;
	struct mh_software_clipped_corner *corner;
	unsigned int i, j, half, a, b;
	unsigned int outcode = 0, common;
	unsigned int corner_count, face_count, input_count;
	float horizontal, vertical;
	int front_a, front_b;
	enum mh_error_code status;

	if (projection == NULL || corners == NULL || output == NULL ||
		count == NULL || flags > MH_U16_MAX ||
		!(projection->near_distance >= NEAR_MIN &&
			projection->near_distance <= FACTOR_LIMIT) ||
		!(projection->focal > 0 && projection->focal <= FACTOR_LIMIT) ||
		!(projection->half_width > 0 &&
			projection->half_width <= VIEWPORT_LIMIT) ||
		!(projection->aspect > 0 && projection->aspect <= FACTOR_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software near-clip projection");

	for (i = 0; i < 2; i++)
		if (!bounded(projection->center[i]) ||
			!(projection->extent[i] >= 1 &&
				projection->extent[i] <= VIEWPORT_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software near-clip viewport");

	input_count = flags & MH_NEAR_QUAD ? 4 : 3;

	if (near_mask >= (1U << input_count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software near-clip mask");

	for (i = 0; i < input_count; i++) {
		for (j = 0; j < 3; j++)
			if (!bounded(corners[i].camera[j]) ||
				!bounded(corners[i].rgb[j]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software near-clip corner");

		for (j = 0; j < 2; j++)
			if (!bounded(corners[i].uv[j]))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid software near-clip UV");

		if (!(near_mask & (1U << i)) &&
			!(corners[i].camera[2] >= VISIBLE_DEPTH_MIN))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software near-clip visible depth");
	}

	memset(faces, 0, sizeof(faces));
	face_count = 0;
	horizontal =
		(float)((double)projection->focal * projection->half_width);
	vertical = (float)((double)horizontal * projection->aspect);

	for (half = 0; half < (input_count == 4 ? 2U : 1U); half++) {
		face = &faces[face_count];
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

		for (i = 0; i < 3; i++) {
			a = triangles[half][i];
			b = triangles[half][(i + 1) % 3];
			front_a = !(near_mask & (1U << a));
			front_b = !(near_mask & (1U << b));

			if (front_a)
				copy_corner(corners + a,
					&face->corners[corner_count++], flags);

			if (front_a != front_b) {
				/*
				 * Original aborts both halves on a parallel
				 * crossing.
				 */
				if (corners[a].camera[2] ==
					corners[b].camera[2]) {
					*count = 0;
					return mh_error_set(
						error_out, MH_ERROR_NONE, NULL);
				}

				status = intersect_edge(projection, corners + a,
					corners + b, flags,
					&face->corners[corner_count++],
					error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}
		}

		if (corner_count < 3)
			continue;

		face->count = corner_count;

		if (corner_count == 4)
			face->flags |= MH_NEAR_QUAD;

		common = MH_CLIP_VIEWPORT;

		for (i = 0; i < corner_count; i++) {
			corner = &face->corners[i];

			status = project_corner(projection, horizontal,
				vertical, corner, &outcode, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			common &= outcode;
		}

		if (!common)
			face_count++;
	}

	if (face_count != 0)
		memcpy(output, faces, face_count * sizeof(*output));

	*count = face_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
