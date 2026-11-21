#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/name_label.h"

#define VALUE_LIMIT 1e9f
#define MINIMUM_SIZE .01
#define DISTANCE_HEIGHT_SCALE .025
#define BYTE_COLOR_SCALE .00392156862745098
/* Original fixed depth, bits 0x3f7fbe77. */
#define LABEL_DEPTH .999000012874603271484375f

enum {
	FADE_START = 20,
	FADE_END = 50,
	FADE_RANGE = 30,
	HALF_SIZE_SCALE = 192,
	BASE_BRIGHTNESS = 160,
	COLOR_BYTE_MAX = 255
};

static int
bounded(float value)
{
	return value >= -VALUE_LIMIT && value <= VALUE_LIMIT;
}

unsigned int
mh_name_label_mask(unsigned int count, unsigned int active_mask,
	unsigned int local, unsigned int viewed, int in_car, int chase)
{
	if (count < 1 || count > MH_NAME_LABELS_MAX || local >= count ||
		viewed >= count)
		return 0;

	active_mask &= (1U << count) - 1;

	if (in_car)
		active_mask &= ~(1U << viewed);
	else if (chase)
		active_mask &= ~(1U << local);

	return active_mask;
}

static float
fade_at_edge(long double distance, float half_size, int patched)
{
	/* Multiply by the extended reciprocal. */
	return (float)(patched ? distance * (1.0L / half_size)
			: distance / half_size);
}

static enum mh_error_code
project_label(const struct mh_name_label *label, const float world_position[3],
	const float camera_position[3], const float view[12],
	const struct mh_projection *projection, struct mh_name_quad *output,
	int patched, struct mh_error *error_out)
{
	struct mh_name_quad result;
	struct mh_effect_quad *quad;
	struct mh_projected_point point;
	long double distance_squared, distance, depth, fade, color_scale;
	float anchor[3], half_width, half_height, fade_ratio, brightness,
		edge_fade, edge_ratio;
	unsigned int axis, bound, channel, corner;
	enum mh_error_code status;

	if (label == NULL || world_position == NULL ||
		camera_position == NULL || view == NULL || projection == NULL ||
		output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid name projection");

	for (axis = 0; axis < 3; axis++)
		if (!bounded(world_position[axis]) ||
			!bounded(camera_position[axis]) ||
			!bounded(label->color[axis]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid name position or color");

	if (!bounded(label->height) || !bounded(label->size[0]) ||
		!bounded(label->size[1]))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid name dimensions");

	for (bound = 0; bound < 4; bound++)
		if (!(label->uv[bound] >= 0 && label->uv[bound] <= 1))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid name texture bounds");

	if (!(label->uv[2] >= label->uv[0] && label->uv[2] > 0 &&
		label->uv[3] > label->uv[1]))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"empty name texture bounds");

	memset(&result, 0, sizeof(result));
	memset(&point, 0, sizeof(point));
	quad = &result.quad;

	if (label->size[0] < MINIMUM_SIZE || label->size[1] < MINIMUM_SIZE) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	distance_squared =
		(long double)camera_position[0] * camera_position[0] +
		(long double)camera_position[2] * camera_position[2];

	/*
	 * Refine the distance in long double before scaling the label height.
	 */
	distance = sqrt((double)distance_squared);

	if (distance != 0)
		distance = (distance + distance_squared / distance) * .5L;

	anchor[0] = world_position[0];
	anchor[2] = world_position[2];
	anchor[1] = (float)((long double)world_position[1] + label->height +
		distance * (long double)DISTANCE_HEIGHT_SCALE);
	status = mh_project_point(projection, view, anchor, &point, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (point.outcode != 0) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	depth = (long double)point.value[2] * projection->far_distance;

	if (depth > FADE_END) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	half_width = (float)((long double)label->uv[2] * HALF_SIZE_SCALE);

	if (patched)
		half_width =
			(float)(((long double)label->uv[2] - label->uv[0]) *
				HALF_SIZE_SCALE);

	if (half_width == 0) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	half_height = (float)(((long double)label->uv[3] - label->uv[1]) *
		HALF_SIZE_SCALE);
	fade = depth - FADE_START;
	fade_ratio = (float)fade;
	fade_ratio =
		fade < 0 ? 0 : (float)((long double)fade_ratio / FADE_RANGE);
	brightness = (float)((1 - (long double)fade_ratio) * BASE_BRIGHTNESS);
	edge_fade = fade_at_edge(point.value[0], half_width, patched);
	edge_ratio = fade_at_edge(
		(long double)projection->clip.extent[0] - point.value[0],
		half_width, patched);

	if (edge_ratio < edge_fade)
		edge_fade = edge_ratio;

	edge_ratio = fade_at_edge(point.value[1], half_height, patched);

	if (edge_ratio < edge_fade)
		edge_fade = edge_ratio;

	edge_ratio = fade_at_edge(
		(long double)projection->clip.extent[1] - point.value[1],
		half_height, patched);

	if (edge_ratio < edge_fade)
		edge_fade = edge_ratio;

	if (edge_fade > 1)
		edge_fade = 1;

	/* Store brightness but retain its extended value for RGB. */
	color_scale = (long double)brightness * edge_fade *
		(long double)BYTE_COLOR_SCALE;

	for (channel = 0; channel < 3; channel++) {
		quad->color[channel] =
			(float)(color_scale * label->color[channel]);

		if (quad->color[channel] < 0)
			quad->color[channel] = 0;

		if (quad->color[channel] > COLOR_BYTE_MAX)
			quad->color[channel] = COLOR_BYTE_MAX;
	}

	for (corner = 0; corner < 4; corner++) {
		quad->xy[corner][0] = (float)((long double)point.value[0] +
			(corner == 0 || corner == 3 ? -half_width
						: half_width));
		quad->xy[corner][1] = (float)((long double)point.value[1] +
			(corner < 2 ? -half_height : half_height));
		quad->uv[corner][0] =
			label->uv[corner == 0 || corner == 3 ? 0 : 2];
		quad->uv[corner][1] = label->uv[corner < 2 ? 1 : 3];
	}

	quad->depth = LABEL_DEPTH;
	result.reciprocal_depth = point.value[3];
	result.visible = 1;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_name_label_project(const struct mh_name_label *label,
	const float world_position[3], const float camera_position[3],
	const float view[12], const struct mh_projection *projection,
	struct mh_name_quad *output, struct mh_error *error_out)
{
	return project_label(label, world_position, camera_position, view,
		projection, output, 0, error_out);
}

enum mh_error_code
mh_patched_name_label_project(const struct mh_name_label *label,
	const float world_position[3], const float camera_position[3],
	const float view[12], const struct mh_projection *projection,
	struct mh_name_quad *output, struct mh_error *error_out)
{
	return project_label(label, world_position, camera_position, view,
		projection, output, 1, error_out);
}
