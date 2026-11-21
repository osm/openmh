#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/fog.h"
#include "engine/render/material_color.h"
#include "engine/render/scene_face_internal.h"

#define ATTRIBUTE_LIMIT 1e9f
#define NEAR_LIMIT 1e6f
#define COLOR_BYTE_MAX 255

static enum mh_error_code
clip_face(const struct mh_projection *projection, enum mh_scene_pass material,
	unsigned int count, int smooth, const struct mh_scene_corner input[4],
	unsigned int near_mask, struct mh_scene_face faces[2],
	unsigned int *output_count, struct mh_error *error_out)
{
	struct mh_near_corner corners[4];
	struct mh_projected_face clipped[2];
	struct mh_scene_vertex *corner;
	const struct mh_projected_face *split;
	const struct mh_projected_corner *source;
	unsigned int index, face, flags, used;
	enum mh_error_code status;

	memset(corners, 0, sizeof(corners));

	for (index = 0; index < count; index++) {
		status = mh_projection_restore(projection, &input[index].point,
			corners[index].camera, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		memcpy(corners[index].uv, input[index].uv,
			sizeof(corners[index].uv));
		memcpy(corners[index].rgb, input[index].rgb,
			sizeof(corners[index].rgb));
	}

	flags = (count == 4 ? MH_NEAR_QUAD : 0U) |
		(smooth ? MH_NEAR_VERTEX_COLOR : 0U);

	if (material != MH_SCENE_SOLID && material != MH_SCENE_UNAVAILABLE)
		flags |= MH_NEAR_TEXTURED;

	status = mh_clip_near(&projection->clip, flags, near_mask, corners,
		clipped, &used, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (face = 0; face < used; face++) {
		split = &clipped[face];
		faces[face].count = split->count;

		for (index = 0; index < split->count; index++) {
			corner = &faces[face].corners[index];
			source = &split->corners[index];
			memcpy(corner->xy, source->xy, sizeof(corner->xy));
			memcpy(corner->uv, source->uv, sizeof(corner->uv));
			memcpy(corner->rgb,
				split->corners[smooth ? index : 0].rgb,
				sizeof(corner->rgb));
			corner->depth = source->depth;
			corner->reciprocal_w = source->reciprocal_w;
			corner->visibility = COLOR_BYTE_MAX;
		}
	}

	*output_count = used;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_scene_face_prepare(const struct mh_projection *projection,
	enum mh_scene_pass material, unsigned int count, int smooth,
	const struct mh_scene_corner input[4], struct mh_scene_face output[2],
	unsigned int *output_count, const struct mh_scene_face_policy *policy,
	int fog, struct mh_error *error_out)
{
	struct mh_scene_face faces[2];
	struct mh_scene_vertex *corner;
	const struct mh_scene_corner *source;
	float depths[4], color_min, color_max;
	unsigned int index, axis, channel, face, common, near_mask, used = 0;
	int distant, bucket, far;
	unsigned long packed;
	enum mh_error_code status;

	if (projection == NULL || input == NULL || output == NULL ||
		output_count == NULL || (count != 3 && count != 4) ||
		(smooth != 0 && smooth != 1) || (fog != 0 && fog != 1) ||
		material < MH_SCENE_SOLID || material >= MH_SCENE_PASSES ||
		!(projection->clip.near_distance > 0 &&
			projection->clip.near_distance <= NEAR_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid scene polygon");

	common = MH_CLIP_ALL;
	near_mask = 0;
	far = 0;
	color_min = (policy->flags & MH_FACE_RAW_COLOR) ? -ATTRIBUTE_LIMIT : 0;
	color_max = (policy->flags & MH_FACE_RAW_COLOR) ? ATTRIBUTE_LIMIT
							: COLOR_BYTE_MAX;

	for (index = 0; index < count; index++) {
		source = &input[index];

		if (source->point.outcode > MH_CLIP_ALL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scene corner outcode");

		common &= source->point.outcode;

		if (source->point.outcode & MH_CLIP_NEAR)
			near_mask |= 1U << index;

		if (source->point.outcode & MH_CLIP_FAR)
			far = 1;

		for (channel = 0; channel < 3; channel++)
			if (!(source->rgb[channel] >= color_min &&
				source->rgb[channel] <= color_max))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid scene corner color");

		for (axis = 0; axis < 2; axis++)
			if (!(source->uv[axis] >= -ATTRIBUTE_LIMIT &&
				source->uv[axis] <= ATTRIBUTE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid scene corner UV");

		for (axis = 0; axis < 4; axis++)
			if (!(source->point.value[axis] >= -ATTRIBUTE_LIMIT &&
				source->point.value[axis] <=
					ATTRIBUTE_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid scene projected corner");

		depths[index] = source->point.value[2];
	}

	if (far || common) {
		*output_count = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	distant = fog && material != MH_SCENE_UNAVAILABLE &&
		policy->distant(depths, count);
	bucket = material == MH_SCENE_ALPHA && !distant
		? mh_fog_alpha_bucket(depths, count, near_mask)
		: 0;

	if (bucket < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid scene alpha bucket");

	memset(faces, 0, sizeof(faces));
	faces[0].pass = material;

	if (distant)
		faces[0].pass = ((policy->flags & MH_FACE_DISTANT_TEXTURE)
						? material != MH_SCENE_SOLID
						: material == MH_SCENE_ALPHA)
			? MH_SCENE_DISTANT_ALPHA
			: MH_SCENE_DISTANT_SOLID;

	faces[0].smooth = smooth;
	faces[0].bucket = (unsigned int)bucket;
	faces[1] = faces[0];

	if (near_mask != 0 && !distant) {
		status = clip_face(projection, material, count, smooth, input,
			near_mask, faces, &used, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else {
		used = 1;
		faces[0].count = count;

		for (index = 0; index < count; index++) {
			corner = &faces[0].corners[index];
			source = &input[index];
			memcpy(corner->xy, source->point.value,
				sizeof(corner->xy));
			memcpy(corner->uv, source->uv, sizeof(corner->uv));

			/*
			 * Flat-alpha TRON reads slots its original producer may
			 * leave untouched. Use stable flat RGB instead of
			 * allocation history.
			 */
			memcpy(corner->rgb, input[smooth ? index : 0].rgb,
				sizeof(corner->rgb));
			corner->depth =
				(float)((long double)
						projection->clip.near_distance *
					source->point.value[3]);
			corner->reciprocal_w = source->point.value[3];
			corner->visibility = !fog ||
					(policy->flags & MH_FACE_CONTEXT_FOG) ||
					material == MH_SCENE_UNAVAILABLE
				? (unsigned int)COLOR_BYTE_MAX
				: (unsigned int)mh_fog_visibility(
					depths[index]);

			if (distant)
				corner->background =
					(unsigned int)policy->background(
						depths[index]);
		}
	}

	for (face = 0; face < used; face++)
		for (index = 0; index < faces[face].count; index++) {
			corner = &faces[face].corners[index];

			if (!(corner->depth >= 0 && corner->depth <= 1) ||
				!(corner->reciprocal_w > 0))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"scene polygon exceeds native depth range");

			if ((policy->flags & MH_FACE_RAW_COLOR) &&
				((policy->flags & MH_FACE_PACK_DISTANT) ||
					!distant)) {
				status = mh_material_color_pack(
					corner->rgb, &packed, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				corner->rgb[0] = (float)((packed >> 16) &
					COLOR_BYTE_MAX);
				corner->rgb[1] =
					(float)((packed >> 8) & COLOR_BYTE_MAX);
				corner->rgb[2] =
					(float)(packed & COLOR_BYTE_MAX);
			}
		}

	if (used != 0)
		memcpy(output, faces, used * sizeof(*output));

	*output_count = used;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
