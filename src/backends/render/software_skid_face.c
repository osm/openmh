#include <string.h>

#include "backends/render/software_fog.h"
#include "backends/render/software_order.h"
#include "backends/render/software_skid_face.h"

#define NEAR_MARGIN 6
#define FAR_MARGIN 4

static enum mh_error_code
depth_bucket(const struct mh_projection *projection, float depth,
	unsigned int *bucket, struct mh_error *error_out)
{
	struct mh_software_order order;
	float offset, reserved, scale, position;
	enum mh_error_code status;

	status = mh_software_order_setup(projection->clip.near_distance,
		projection->far_distance, MH_SOFTWARE_ORDER_ROOT, &order,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* The skid band reserves near=6, far=4. */
	offset = (float)((double)FAR_MARGIN * order.inverse_range);
	reserved = (float)((double)NEAR_MARGIN * order.inverse_range);
	reserved = (float)((double)reserved + offset);
	scale = (float)(1.0 - reserved);
	position = (float)((double)depth - order.near_distance);
	position = (float)((double)position * order.inverse_range);
	position = (float)(1.0 - position);
	position = (float)((double)position * scale);
	position = (float)((double)position + offset);
	position = (float)((double)position * (order.buckets - 1));

	if (!(position > -1 && position < order.buckets))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software skid outside ordering buckets");

	*bucket = (unsigned int)(long)position;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_depth_cue(const struct mh_projection *projection,
	const struct mh_depth_cue *cue, struct mh_software_point points[4],
	float depth, struct mh_software_material *material,
	struct mh_error *error_out)
{
	struct mh_software_fog fog;
	float amount;
	unsigned int i;
	enum mh_error_code status;

	if (cue == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_software_fog_setup(projection->clip.near_distance,
		projection->far_distance, cue->start, &fog, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	amount = (float)((double)depth - fog.start_distance);

	if (amount <= 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	material->fog = (float)((double)amount * fog.inverse_range);
	memcpy(material->fog_color, cue->color, sizeof(material->fog_color));

	if (cue->gradient == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* Skids include cached camera Y for near corners in the row average. */
	for (i = 0; i < 4; i++)
		points[i].outcode &= ~(unsigned int)MH_CLIP_NEAR;

	return mh_software_fog_color(
		cue->gradient, points, 4, material->fog_color, error_out);
}

enum mh_error_code
mh_software_skid_face_prepare(const struct mh_projection *projection,
	const float camera[12], const float positions[12], const float color[3],
	const struct mh_depth_cue *cue, struct mh_software_skid_face *output,
	struct mh_error *error_out)
{
	struct mh_software_skid_face result;
	struct mh_software_clipped_face faces[2];
	struct mh_software_point points[4];
	struct mh_software_material material;
	struct mh_near_corner corners[4];
	float depth;
	unsigned int i, face_count, triangle_count, common_outcode,
		combined_outcode;
	enum mh_error_code status;

	if (positions == NULL || color == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing software skid face inputs");

	for (i = 0; i < 3; i++)
		if (!(color[i] >= -MH_SOFTWARE_LIGHT_MAX &&
			color[i] <= MH_SOFTWARE_LIGHT_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"invalid software skid face color");

	memset(&result, 0, sizeof(result));
	memset(&material, 0, sizeof(material));
	memset(corners, 0, sizeof(corners));
	material.mapping = MH_TEXTURE_AFFINE;
	material.perspective_shift = 2;

	for (i = 0; i < 4; i++) {
		memcpy(corners[i].camera, positions + i * 3,
			sizeof(corners[i].camera));
		memcpy(corners[i].rgb, color, sizeof(corners[i].rgb));
	}

	status = mh_software_project_face(projection, camera, MH_NEAR_QUAD,
		corners, faces, &face_count, points, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	common_outcode = MH_CLIP_VIEWPORT | MH_CLIP_NEAR;
	combined_outcode = 0;
	depth = 0;

	for (i = 0; i < 4; i++) {
		common_outcode &= points[i].outcode;
		combined_outcode |= points[i].outcode;
		depth = (float)((double)depth + points[i].value[2]);
	}

	if (common_outcode || (combined_outcode & MH_CLIP_FAR)) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	result.visible = 1;
	depth = (float)((double)depth * .25);
	status = depth_bucket(projection, depth, &result.bucket, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = apply_depth_cue(
		projection, cue, points, depth, &material, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < face_count; i++) {
		status = mh_software_face_prepare(faces + i, &material,
			result.triangles + result.count, &triangle_count,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		result.count += triangle_count;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
