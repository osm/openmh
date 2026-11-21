#include <math.h>
#include <string.h>

#include "backends/render/software_name_geometry.h"

#define VALUE_LIMIT 1e9f
#define MINIMUM_SIZE .01
#define DISTANCE_HEIGHT_SCALE .025

enum {
	FADE_START = 20,
	FADE_END = 50,
	BASE_BRIGHTNESS = 80,
	IMAGE_LIMIT = 256
};

static int
bounded(float value)
{
	return value >= -VALUE_LIMIT && value <= VALUE_LIMIT;
}

static float
anchor_height(const struct mh_software_name_source *source)
{
	float x, z, distance, height;

	/* Round each step to float so the label follows the projected car. */
	x = (float)((double)source->camera[0] * source->camera[0]);
	z = (float)((double)source->camera[2] * source->camera[2]);
	distance = (float)((double)x + z);
	distance = (float)sqrt(distance);
	distance = (float)((long double)distance * DISTANCE_HEIGHT_SCALE);
	height = (float)((double)source->world[1] + source->height);

	return (float)((double)height + distance);
}

static float
brightness(const struct mh_projection *projection,
	const struct mh_software_point *point, const float half[2])
{
	float fade, light, inverse, edge, ratio, remaining;
	unsigned int axis;

	fade = (float)((double)point->value[2] - FADE_START);
	fade = fade < 0 ? 0 : (float)((double)fade / (FADE_END - FADE_START));
	light = (float)(1.0 - fade);
	light = (float)((double)light * BASE_BRIGHTNESS);
	edge = 1;

	for (axis = 0; axis < 2; axis++) {
		inverse = (float)(1.0 / half[axis]);
		ratio = (float)((double)point->value[axis] * inverse);

		if (ratio < edge)
			edge = ratio;

		remaining = (float)((double)projection->clip.extent[axis] -
			point->value[axis]);
		ratio = (float)((double)remaining * inverse);

		if (ratio < edge)
			edge = ratio;
	}

	return (float)((double)light * edge);
}

static enum mh_error_code
depth_bucket(const struct mh_projection *projection,
	const struct mh_software_depth_band *band, float depth,
	unsigned int *bucket, struct mh_error *error_out)
{
	float inverse, position;

	inverse = (float)(1.0L /
		((long double)projection->far_distance -
			projection->clip.near_distance));
	position = (float)((double)depth - projection->clip.near_distance);
	position = (float)((double)position * inverse);
	position = (float)(1.0 - position);
	position = (float)((double)position * band->scale);
	position = (float)((double)position + band->offset);
	position = (float)((double)position * (MH_SOFTWARE_BUCKETS - 1));

	if (!(position >= 0 && position < MH_SOFTWARE_BUCKETS))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software name outside ordering buckets");

	*bucket = (unsigned int)position;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_name_project(const struct mh_software_name_source *source,
	const struct mh_projection *projection, const float view[12],
	const struct mh_software_depth_band *band,
	struct mh_software_name_geometry *output, struct mh_error *error_out)
{
	struct mh_software_name_geometry next;
	struct mh_software_point point;
	struct mh_software_clipped_corner *vertex;
	float anchor[3], half[2], light;
	unsigned int axis, corner, channel;
	enum mh_error_code status;

	if (source == NULL || band == NULL || output == NULL ||
		source->image == NULL || source->image->width == 0 ||
		source->image->width > IMAGE_LIMIT ||
		source->image->height == 0 ||
		source->image->height > IMAGE_LIMIT ||
		!bounded(source->height) || !bounded(source->size[0]) ||
		!bounded(source->size[1]) ||
		!(band->offset >= 0 && band->scale >= 0 &&
			band->offset + band->scale <= 1.000001f))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software name geometry inputs");

	for (axis = 0; axis < 3; axis++)
		if (!bounded(source->world[axis]) ||
			!bounded(source->camera[axis]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software name position");

	status = mh_software_projection_check(projection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&next, 0, sizeof(next));
	memset(&point, 0, sizeof(point));

	if (source->size[0] < MINIMUM_SIZE || source->size[1] < MINIMUM_SIZE) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	anchor[0] = source->world[0];
	anchor[1] = anchor_height(source);
	anchor[2] = source->world[2];
	status = mh_software_project_point(
		projection, view, anchor, &point, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (point.outcode != 0 || point.value[2] > FADE_END) {
		*output = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	half[0] = source->image->width * .5f;
	half[1] = source->image->height * .5f;
	light = source->transparent ? brightness(projection, &point, half)
				: BASE_BRIGHTNESS;

	next.face.count = 4;
	next.face.flags = MH_NEAR_QUAD | MH_NEAR_TEXTURED |
		(source->transparent ? MH_SOFTWARE_FACE_ADDITIVE
				: MH_SOFTWARE_FACE_CUTOUT);

	for (corner = 0; corner < 4; corner++) {
		vertex = &next.face.corners[corner];
		vertex->xy[0] = (float)((double)point.value[0] +
			(corner == 0 || corner == 3 ? -half[0] : half[0]));
		vertex->xy[1] = (float)((double)point.value[1] +
			(corner < 2 ? -half[1] : half[1]));
		vertex->uv[0] = corner == 0 || corner == 3
			? .5f
			: source->image->width - .5f;
		vertex->uv[1] = corner < 2 ? .5f : source->image->height - .5f;

		for (channel = 0; channel < 3; channel++)
			vertex->rgb[channel] = light;
	}

	status = depth_bucket(
		projection, band, point.value[2], &next.bucket, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
