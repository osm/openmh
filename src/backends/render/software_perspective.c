#include <math.h>

#include "backends/render/software_fixed.h"
#include "backends/render/software_palette.h"
#include "backends/render/software_perspective.h"

#define GRADIENT_BIAS 0.999f
#define MIN_DETERMINANT 0.1f
#define UV_BIAS 768.0
#define HALF_TEXEL_LIMIT (MH_SOFTWARE_TEXTURE_SCALE / 2 - 1)
#define PROJECTED_VALUE_LIMIT 1e20
#define MIN_PERSPECTIVE_SPREAD 0.006f

/* Entries after 16 are rounded decimal approximations of 1/n. */
static const float inverse_lengths[32] = {0.0f, 1.0f, 0.5f, 0.333333343f, 0.25f,
	0.200000003f, 0.166666672f, 0.142857149f, 0.125f, 0.111111112f,
	0.100000001f, 0.0909090936f, 0.0833333358f, 0.0769230798f,
	0.0714285746f, 0.0666666701f, 0.0625f, 0.058823999f, 0.0555559993f,
	0.0526320003f, 0.0500000007f, 0.0476190001f, 0.0454550013f,
	0.0434780009f, 0.0416669995f, 0.0399999991f, 0.0384620018f,
	0.0370370001f, 0.0357140005f, 0.0344830006f, 0.0333329998f,
	0.0322580002f};

/* Threshold is binary32 0x3bc49ba6. */
int
mh_software_perspective_needed(
	enum mh_texture_mapping mapping, int flagged, const float weights[3])
{
	float low, high, spread;
	unsigned int i;

	if (mapping == MH_TEXTURE_PERSPECTIVE)
		return 1;

	if (mapping == MH_TEXTURE_AFFINE || !flagged)
		return 0;

	low = high = weights[0];

	for (i = 1; i < 3; i++) {
		if (weights[i] < low)
			low = weights[i];

		if (weights[i] > high)
			high = weights[i];
	}

	spread = (float)((double)high - low);

	return spread >= MIN_PERSPECTIVE_SPREAD;
}

/* Store weighted UV before computing biased gradients. */
void
mh_software_perspective_init(struct mh_software_perspective *state,
	const struct mh_software_triangle *triangle)
{
	float dx0, dx1, dy0, dy1, determinant, reciprocal;
	double d0, d1, first, second;
	unsigned int corner, axis;

	state->block = 1U << triangle->perspective_shift;
	dx0 = triangle->xy[0][0] - triangle->xy[2][0];
	dx1 = triangle->xy[1][0] - triangle->xy[2][0];
	dy0 = triangle->xy[0][1] - triangle->xy[2][1];
	dy1 = triangle->xy[1][1] - triangle->xy[2][1];
	first = (double)dx1 * dy0;
	second = (double)dx0 * dy1;
	determinant = (float)(first - second);
	reciprocal = 0;

	if (fabs(determinant) > MIN_DETERMINANT)
		reciprocal = (float)((double)GRADIENT_BIAS / determinant);

	state->empty = reciprocal == 0;

	for (corner = 0; corner < 3; corner++) {
		for (axis = 0; axis < 2; axis++)
			state->source[corner][axis] =
				(float)((double)triangle->uv[corner][axis] *
					triangle->perspective_weight[corner]);

		state->source[corner][2] = triangle->perspective_weight[corner];
	}

	for (axis = 0; axis < 3; axis++) {
		d0 = (double)state->source[0][axis] - state->source[2][axis];
		d1 = (double)state->source[1][axis] - state->source[2][axis];
		first = d1 * dy0;
		second = d0 * dy1;
		state->dx[axis] = (float)((first - second) * reciprocal);
		first = d0 * dx1;
		second = d1 * dx0;
		state->dy[axis] = (float)((first - second) * reciprocal);
		state->block_step[axis] =
			(float)((double)state->dx[axis] * state->block);
	}
}

/* Row increments are binary32, running row values stay binary64. */
void
mh_software_perspective_edge(struct mh_software_perspective *state,
	const struct mh_software_triangle *triangle, unsigned int corner,
	long x, long step, long y)
{
	double dx, first, second;
	float dy;
	unsigned int axis;

	dx = (double)x - triangle->xy[corner][0];
	dy = (float)y - triangle->xy[corner][1];

	for (axis = 0; axis < 3; axis++) {
		first = (double)step * state->dx[axis];
		state->row_step[axis] = (float)(first + state->dy[axis]);
		first = (double)dy * state->dy[axis];
		second = dx * state->dx[axis];
		state->value[axis] = first + second;
		state->value[axis] += state->source[corner][axis];
	}
}

void
mh_software_perspective_advance(
	struct mh_software_perspective *state, int carry)
{
	unsigned int axis;

	for (axis = 0; axis < 3; axis++) {
		state->value[axis] += state->row_step[axis];

		if (carry)
			state->value[axis] += state->dx[axis];
	}
}

static long
clamp_fixed(long value, unsigned int dimension)
{
	long limit;

	limit = (long)dimension * MH_SOFTWARE_TEXTURE_SCALE - HALF_TEXEL_LIMIT;

	if (value < 0)
		return 0;

	return value > limit ? limit : value;
}

/* Multiply by one shared reciprocal, rather than dividing each UV. */
static enum mh_error_code
project_uv(const double value[3], double uv[2], struct mh_error *error_out)
{
	double reciprocal;
	unsigned int axis;

	/* Thin valid faces can extrapolate past zero at a span endpoint. */
	if (!(value[2] >= -PROJECTED_VALUE_LIMIT &&
		value[2] <= PROJECTED_VALUE_LIMIT) ||
		value[2] == 0)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"software perspective span crosses invalid weight");

	reciprocal = 1.0 / value[2];

	for (axis = 0; axis < 2; axis++) {
		uv[axis] = value[axis] * reciprocal;

		if (!(uv[axis] >= -PROJECTED_VALUE_LIMIT &&
			uv[axis] <= PROJECTED_VALUE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software perspective coordinate outside range");
	}

	return MH_ERROR_NONE;
}

/* Complete sample validation is also run before any framebuffer writes. */
static enum mh_error_code
sample_span(const struct mh_software_surface *surface,
	const struct mh_software_texture *texture, double uv[2],
	const long step[2], long x, long y, unsigned int count,
	int write_pixels, struct mh_error *error_out)
{
	unsigned int i, palette_index, packed_color;
	size_t offset;
	unsigned char *pixel;

	for (i = 0; i < count; i++) {
		if (!(uv[0] >= 0 && uv[1] >= 0 &&
			uv[0] < texture->width *
					(double)MH_SOFTWARE_TEXTURE_SCALE &&
			uv[1] < texture->height *
					(double)MH_SOFTWARE_TEXTURE_SCALE))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software perspective sample outside image");

		offset = (size_t)(uv[1] / MH_SOFTWARE_TEXTURE_SCALE) *
				texture->width +
			(size_t)(uv[0] / MH_SOFTWARE_TEXTURE_SCALE);
		palette_index = texture->indices[offset];

		if (palette_index >= texture->palette->count)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software perspective index outside palette");

		if (write_pixels) {
			packed_color = texture->palette->color[palette_index];
			pixel = surface->pixels + (size_t)y * surface->pitch +
				(size_t)(x + i) * 2;
			pixel[0] = (unsigned char)packed_color;
			pixel[1] = (unsigned char)(packed_color >> 8);
		}

		uv[0] += step[0];
		uv[1] += step[1];
	}

	return MH_ERROR_NONE;
}

/*
 * Full blocks use integer endpoint differences. The remainder uses a separate
 * binary32 reciprocal table and bias conversion, not that division.
 */
enum mh_error_code
mh_software_perspective_span(const struct mh_software_perspective *state,
	const struct mh_software_surface *surface,
	const struct mh_software_texture *texture, long low, long high, long y,
	int write_pixels, struct mh_software_texture_state *texture_state,
	struct mh_error *error_out)
{
	double current[3], projected[2], uv[2], delta;
	float encoded[2], end, inverse;
	long raw[2], step[2];
	unsigned int dimension[2], axis, remaining, block_pixels;
	enum mh_error_code status;

	if (high <= (low < 0 ? 0 : low))
		return MH_ERROR_NONE;

	for (axis = 0; axis < 3; axis++) {
		current[axis] = state->value[axis];

		if (low < 0)
			current[axis] -= (double)low * state->dx[axis];
	}

	if (low < 0)
		low = 0;

	dimension[0] = texture->width;
	dimension[1] = texture->height;
	status = project_uv(current, projected, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (axis = 0; axis < 2; axis++) {
		encoded[axis] = (float)(projected[axis] + UV_BIAS);
		raw[axis] = mh_software_texture_fixed(projected[axis]);
		uv[axis] = clamp_fixed(raw[axis], dimension[axis]);
	}

	while (low < high) {
		remaining = (unsigned int)(high - low);
		block_pixels =
			remaining >= state->block ? state->block : remaining;

		for (axis = 0; axis < 3; axis++)
			current[axis] += block_pixels == state->block
				? state->block_step[axis]
				: (double)block_pixels * state->dx[axis];

		status = project_uv(current, projected, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (axis = 0; axis < 2; axis++) {
			if (block_pixels == state->block) {
				delta = (double)clamp_fixed(
						mh_software_texture_fixed(
							projected[axis]),
						dimension[axis]) -
					raw[axis];
				step[axis] = (long)floor(delta / block_pixels);
			} else {
				end = (float)projected[axis];

				if (end < 0)
					end = 0;
				else if (end > (float)dimension[axis] - 0.5f)
					end = (float)dimension[axis] - 0.5f;

				inverse = inverse_lengths[block_pixels];
				delta = (double)end -
					((double)encoded[axis] - UV_BIAS);
				step[axis] = mh_software_texture_fixed(
					delta * inverse);
			}

			encoded[axis] = (float)(projected[axis] + UV_BIAS);
			raw[axis] = mh_software_texture_fixed(projected[axis]);
		}

		for (axis = 0; axis < 2; axis++)
			texture_state->span_step[axis] = step[axis];

		texture_state->width = texture->width;

		status = sample_span(surface, texture, uv, step, low, y,
			block_pixels, write_pixels, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		low += block_pixels;
	}

	return MH_ERROR_NONE;
}
