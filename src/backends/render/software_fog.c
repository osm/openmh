#include <string.h>

#include "backends/render/software_fog.h"
#include "engine/core/binary.h"

#define DEPTH_LIMIT 1e9f
#define FACTOR_LIMIT 1e6f
#define TRIANGLE_AVERAGE 0.33333334f
#define IMAGE_WIDTH_LIMIT 4096
#define SCREEN_LIMIT 1e6f
#define COLOR_BYTE_MAX 255

enum mh_error_code
mh_software_fog_setup(float near_distance, float far_distance, float start,
	struct mh_software_fog *output, struct mh_error *error_out)
{
	struct mh_software_fog fog;
	float range;

	if (output == NULL || !(start >= 0 && start < 1) ||
		!(near_distance > 0 && near_distance <= DEPTH_LIMIT) ||
		!(far_distance > near_distance && far_distance <= DEPTH_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software fog lens or start");

	fog.near_distance = near_distance;
	fog.start_distance = (float)((double)start * far_distance);
	range = (float)((double)far_distance - fog.start_distance);

	if (!(range >= 1.0f / FACTOR_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software fog range too small");

	fog.inverse_range = (float)(1.0 / range);
	*output = fog;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_fog_rows(const unsigned char *pixels, size_t size,
	unsigned int width, unsigned int height, unsigned int green_bits,
	struct mh_depth_cue_gradient *gradient, struct mh_error *error_out)
{
	unsigned int row, column, channel, value[3];
	unsigned long pixel;
	float sum[3];
	double inverse_width;

	if (pixels == NULL || gradient == NULL || width == 0 ||
		width > IMAGE_WIDTH_LIMIT || height == 0 ||
		height > MH_DEPTH_CUE_ROWS ||
		(green_bits != 5 && green_bits != 6) ||
		(size_t)width > (size_t)-1 / 2 / height ||
		size < (size_t)width * height * 2)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software fog panorama");

	/* Unpack without low-bit replication, then average. */
	inverse_width = 1.0 / width;

	for (row = 0; row < height; row++) {
		memset(sum, 0, sizeof(sum));

		for (column = 0; column < width; column++) {
			pixel = mh_read_u16le(pixels);
			pixels += 2;
			value[0] =
				(unsigned int)((pixel >> (green_bits + 5)) & 31)
				<< 3;

			value[1] = (unsigned int)((pixel >> 5) &
					((1U << green_bits) - 1))
				<< (8 - green_bits);

			value[2] = (unsigned int)(pixel & 31) << 3;

			for (channel = 0; channel < 3; channel++)
				sum[channel] = (float)((double)sum[channel] +
					value[channel]);
		}

		for (channel = 0; channel < 3; channel++)
			gradient->rows[row][channel] =
				(float)(sum[channel] * inverse_width);
	}

	gradient->height = height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_fog_color(const struct mh_depth_cue_gradient *gradient,
	const struct mh_software_point points[4], unsigned int count,
	float color[3], struct mh_error *error_out)
{
	float sum, mean, end, offset;
	const float *selected;
	unsigned int corner, channel, row;

	if (gradient == NULL || points == NULL || color == NULL ||
		(count != 3 && count != 4) || gradient->height == 0 ||
		gradient->height > MH_DEPTH_CUE_ROWS ||
		!(gradient->top >= -SCREEN_LIMIT &&
			gradient->top <= SCREEN_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software fog gradient");

	sum = 0;

	for (corner = 0; corner < count; corner++) {
		if (points[corner].outcode &
			~(unsigned int)(MH_CLIP_VIEWPORT | MH_CLIP_NEAR))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"software fog color requires non-far points");

		if (points[corner].outcode & MH_CLIP_NEAR)
			continue;

		if (!(points[corner].value[1] >= -SCREEN_LIMIT &&
			points[corner].value[1] <= SCREEN_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software fog screen coordinate");

		sum = (float)((double)sum + points[corner].value[1]);
	}

	mean = (float)((double)sum * (count == 3 ? TRIANGLE_AVERAGE : .25f));
	end = (float)((double)gradient->top + gradient->height);

	if (mean < gradient->top)
		selected = gradient->above;
	else if (mean > end)
		selected = gradient->below;
	else {
		offset = (float)((double)mean - gradient->top);

		if (!(offset >= 0 && offset <= MH_DEPTH_CUE_ROWS))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software fog row outside original table");

		row = (unsigned int)offset;

		/*
		 * Exact bottom equality may select a retained, unused table
		 * row.
		 */
		selected = row == MH_DEPTH_CUE_ROWS ? gradient->below
						: gradient->rows[row];
	}

	for (channel = 0; channel < 3; channel++)
		if (!(selected[channel] >= 0 &&
			selected[channel] <= COLOR_BYTE_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software fog gradient color");

	memcpy(color, selected, 3 * sizeof(*color));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_fog_amount(const struct mh_software_fog *fog,
	const struct mh_software_point points[4], unsigned int count,
	float *output, struct mh_error *error_out)
{
	float sum, depth, amount;
	unsigned int corner;

	if (fog == NULL || points == NULL || output == NULL ||
		(count != 3 && count != 4) ||
		!(fog->near_distance > 0 &&
			fog->near_distance <= DEPTH_LIMIT) ||
		!(fog->start_distance >= 0 &&
			fog->start_distance <= DEPTH_LIMIT) ||
		!(fog->inverse_range > 0 && fog->inverse_range <= FACTOR_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software fog inputs");

	sum = 0;

	for (corner = 0; corner < count; corner++) {
		if (points[corner].outcode &
			~(unsigned int)(MH_CLIP_VIEWPORT | MH_CLIP_NEAR))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"software fog requires non-far points");

		depth = points[corner].outcode & MH_CLIP_NEAR
			? fog->near_distance
			: points[corner].value[2];

		if (!(depth >= fog->near_distance && depth <= DEPTH_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software fog depth");

		sum = (float)((double)sum + depth);
	}

	depth = (float)((double)sum * (count == 3 ? TRIANGLE_AVERAGE : .25f));
	amount = (float)((double)depth - fog->start_distance);
	*output = amount > 0 ? (float)((double)amount * fog->inverse_range) : 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
