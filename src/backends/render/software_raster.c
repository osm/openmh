#include <math.h>
#include <string.h>

#include "backends/render/software_fixed.h"
#include "backends/render/software_palette.h"
#include "backends/render/software_perspective.h"
#include "backends/render/software_raster.h"
#include "engine/core/limits.h"
#include "engine/render/viewport.h"

#define TEXTURE_LIMIT 256U
#define COLOR_BYTE_MAX 255
#define PERSPECTIVE_SHIFT_MIN 2
#define PERSPECTIVE_SHIFT_MAX 5
#define FIXED_SCALE 65536L
#define COLOR_SCALE 65535.0f
#define GRADIENT_BIAS 0.999f
#define MIN_DETERMINANT 0.1f
#define TEXTURE_VERTICAL_GRADIENT_LIMIT 5.0
#define COORDINATE_LIMIT 30000.0f
#define MIN_PERSPECTIVE_WEIGHT 1e-6f
#define MAX_PERSPECTIVE_WEIGHT 1e6f
#define RGB555_ADD_MASK 0x7bdfUL
#define RGB555_CARRY_MASK 0x8420UL
#define RGB565_ADD_MASK 0xf7deUL
#define RGB565_LOW_MASK 0x0821UL
#define RGB565_SHIFTED_CARRY_MASK 0x0841UL
#define CARRY_SPREAD 27UL

struct edge {
	long	x, step;
};

struct interpolation {
	unsigned int				count;
	int	empty, textured, corrected, fallback;
	struct mh_software_texture_state	*texture_state;
	struct mh_software_perspective		perspective;
	float					dx[3], dy[3];
	long	value[3], pixel_step[3], row_step[3];
};

/* Integer conversion rounds to nearest, ties to even. */
static long
round_integer(double value)
{
	double whole, fraction;

	if (!(value >= (double)MH_I32_MIN && value < (double)MH_U32_SIGN_BIT))
		return MH_I32_MIN;

	whole = floor((double)value);
	fraction = (double)value - whole;

	if (fraction > 0.5 || (fraction == 0.5 && fmod(whole, 2.0) != 0))
		whole += 1;

	if (whole > MH_I32_MAX)
		return MH_I32_MIN;

	return (long)whole;
}

static long
add_word(long left, unsigned long right)
{
	unsigned long word;

	word = ((unsigned long)left + (unsigned long)right) & MH_U32_MAX;

	if (word & MH_U32_SIGN_BIT)
		return -1L - (long)(MH_U32_MAX - word);

	return (long)word;
}

static long
ceil_fixed(long value)
{
	unsigned long magnitude;

	if (value >= 0)
		return (long)(((unsigned long)value + FIXED_SCALE - 1) /
			FIXED_SCALE);

	magnitude = (unsigned long)(-(value + 1)) + 1;

	return -(long)(magnitude / FIXED_SCALE);
}

/* Compute edges in double and round offsets to float. */
static void
start_edge(struct edge *edge, const float from[2], const float to[2], long y)
{
	double dx, dy, slope, x, scaled;
	float offset;

	dx = (double)to[0] - from[0];
	dy = (double)to[1] - from[1];
	slope = dx / dy;
	offset = (float)y - from[1];
	x = slope * offset;
	x = x + from[0];
	scaled = x * (float)FIXED_SCALE;
	edge->x = round_integer(scaled);
	scaled = slope * (float)FIXED_SCALE;
	edge->step = round_integer(scaled);
}

/* Biased reciprocal and binary32 color gradients. */
static void
start_interpolation(struct interpolation *values,
	const struct mh_software_triangle *triangle,
	struct mh_software_texture_state *state)
{
	float dx0, dx1, dy0, dy1, determinant, reciprocal;
	double d0, d1, first, second, value;
	unsigned int channel;
	float limit;

	values->texture_state = state;
	values->fallback = 0;
	values->textured = triangle->texture != NULL;
	values->corrected = values->textured && !triangle->additive &&
		triangle->perspective_shift != 0 &&
		!triangle->texture->zero_transparent;

	if (values->corrected) {
		mh_software_perspective_init(&values->perspective, triangle);
		values->empty = values->perspective.empty;
		return;
	}

	values->count = values->textured ? 2 : 3;
	dx0 = triangle->xy[0][0] - triangle->xy[2][0];
	dx1 = triangle->xy[1][0] - triangle->xy[2][0];
	dy0 = triangle->xy[0][1] - triangle->xy[2][1];
	dy1 = triangle->xy[1][1] - triangle->xy[2][1];
	first = (double)dx1 * dy0;
	second = (double)dx0 * dy1;
	determinant = (float)(first - second);
	reciprocal = 0;

	if (fabs((double)determinant) > MIN_DETERMINANT)
		reciprocal = (float)((double)GRADIENT_BIAS / determinant);

	values->empty = reciprocal == 0;

	for (channel = 0; channel < values->count; channel++) {
		if (triangle->texture != NULL) {
			d0 = (double)triangle->uv[0][channel] -
				triangle->uv[2][channel];
			d1 = (double)triangle->uv[1][channel] -
				triangle->uv[2][channel];
		} else {
			d0 = (double)triangle->color[0][channel] -
				triangle->color[2][channel];
			d1 = (double)triangle->color[1][channel] -
				triangle->color[2][channel];
		}

		first = d1 * dy0;
		second = d0 * dy1;
		value = first - second;
		value = value * reciprocal;
		values->dx[channel] = (float)value;
		values->pixel_step[channel] =
			round_integer(value * COLOR_SCALE);
		first = d0 * dx1;
		second = d1 * dx0;
		value = first - second;
		values->dy[channel] = (float)(value * reciprocal);

		if (triangle->texture == NULL)
			continue;

		limit = (float)(channel == 0 ? triangle->texture->width
					: triangle->texture->height) -
			0.5f;

		/* Rejection leaves cached pixel/span steps intact. */
		if (fabs(values->dx[channel]) > limit ||
			fabs(values->dy[channel]) >
				(float)(limit *
					TEXTURE_VERTICAL_GRADIENT_LIMIT))
			values->fallback = 1;

		values->pixel_step[channel] =
			round_integer((double)values->dx[channel] *
				MH_SOFTWARE_TEXTURE_SCALE);
	}

	if (values->textured && !values->empty) {
		if (values->fallback) {
			values->empty = triangle->additive ||
				triangle->texture->zero_transparent;

			for (channel = 0; channel < 2; channel++)
				values->pixel_step[channel] =
					state->pixel_step[channel];
		} else {
			for (channel = 0; channel < 2; channel++)
				state->pixel_step[channel] =
					state->span_step[channel] =
						values->pixel_step[channel];

			state->width = triangle->texture->width;
		}
	}
}

/* Restart color only when the left edge changes. */
static void
start_edge_values(struct interpolation *values, const struct edge *edge,
	const struct mh_software_triangle *triangle, unsigned int corner,
	long y)
{
	double dx, first, second, value;
	const float *origin, *attributes;
	float dy;
	long step;
	unsigned int channel;

	step = (long)floor((double)edge->step / FIXED_SCALE);

	if (values->corrected) {
		mh_software_perspective_edge(&values->perspective, triangle,
			corner, ceil_fixed(edge->x), step, y);
		return;
	}

	origin = triangle->xy[corner];
	attributes = triangle->texture != NULL ? triangle->uv[corner]
					: triangle->color[corner];
	dx = (double)ceil_fixed(edge->x) - origin[0];
	dy = (float)y - origin[1];

	for (channel = 0; channel < values->count; channel++) {
		value = (double)step * values->dx[channel];
		value = value + values->dy[channel];
		values->row_step[channel] = values->textured
			? mh_software_texture_fixed(value)
			: round_integer(value * COLOR_SCALE);

		first = (double)dy * values->dy[channel];
		second = dx * values->dx[channel];
		value = first + second;
		value = value + attributes[channel];
		values->value[channel] = values->textured
			? mh_software_texture_fixed(value)
			: round_integer(value * COLOR_SCALE);
	}
}

static unsigned int
pack_color(const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle)
{
	float keep;
	double background, foreground, value;
	unsigned int channel, packed;
	long component;

	keep = 1.0f - triangle->fog;
	packed = 0;

	for (channel = 0; channel < 3; channel++) {
		background =
			(double)triangle->fog_color[channel] * triangle->fog;
		foreground = (double)triangle->color[0][channel] * keep;
		value = background + foreground;
		component = round_integer(value);

		if ((unsigned long)component > COLOR_BYTE_MAX)
			component = COLOR_BYTE_MAX;

		packed |= ((unsigned int)component >>
				(8 - surface->bits[channel]))
			<< surface->shift[channel];
	}

	return packed;
}

static void
write_pixel(const struct mh_software_surface *surface, long x, long y,
	unsigned int color)
{
	unsigned char *output;

	output = surface->pixels + (size_t)y * surface->pitch + (size_t)x * 2;
	output[0] = (unsigned char)color;
	output[1] = (unsigned char)(color >> 8);
}

/*
 * Leave the destination's low component bits in place, add the remaining bits
 * and spread each carry into its saturated channel. RGB555 preserves bit 15
 * too. Multiply carries by 27, then OR that with its double to fill the
 * overflowing component's bits.
 */
static unsigned int
add_color(const struct mh_software_surface *surface, long x, long y,
	unsigned int source)
{
	const unsigned char *output;
	unsigned long destination, sum, fill, retained;

	output = surface->pixels + (size_t)y * surface->pitch + (size_t)x * 2;
	destination = output[0] | ((unsigned long)output[1] << 8);

	if (surface->bits[1] == 6) {
		retained = destination & RGB565_LOW_MASK;
		sum = (source & RGB565_ADD_MASK) +
			(destination & RGB565_ADD_MASK);
		fill = ((sum >> 5) & RGB565_SHIFTED_CARRY_MASK) * CARRY_SPREAD;
	} else {
		retained = destination & RGB555_CARRY_MASK;
		sum = (source & RGB555_ADD_MASK) +
			(destination & RGB555_ADD_MASK);
		fill = ((sum & RGB555_CARRY_MASK) >> 5) * CARRY_SPREAD;
	}

	return (unsigned int)((sum | fill | (fill << 1) | retained) &
		MH_U16_MAX);
}

/* Extract the high channel bits, without saturation. */
static unsigned int
pack_shading(const struct mh_software_surface *surface, const long color[3])
{
	unsigned long mask, value;
	unsigned int channel, packed;

	packed = 0;

	for (channel = 0; channel < 3; channel++) {
		mask = ((1UL << surface->bits[channel]) - 1)
			<< (24 - surface->bits[channel]);

		value = (unsigned long)color[channel] & mask;
		packed |= (unsigned int)(value >>
			(24 - surface->bits[channel] -
				surface->shift[channel]));
	}

	return packed;
}

/* Clipping the left span does not advance its starting color. */
static void
smooth_rows(const struct mh_software_surface *surface, struct edge *left,
	struct edge *right, long first, long end, struct interpolation *values)
{
	long x, y, low, high, color[3];
	unsigned long fraction, step;
	unsigned int channel;
	int carry;

	for (y = first; y < end; y++) {
		low = ceil_fixed(left->x);
		high = ceil_fixed(right->x);

		if (low < 0)
			low = 0;

		if (high > (long)surface->width)
			high = surface->width;

		for (channel = 0; channel < 3; channel++)
			color[channel] = values->value[channel];

		for (x = low; x < high; x++) {
			write_pixel(
				surface, x, y, pack_shading(surface, color));

			for (channel = 0; channel < 3; channel++)
				color[channel] = add_word(color[channel],
					values->pixel_step[channel]);
		}

		fraction = ((unsigned long)left->x - 1) & MH_U16_MAX;
		step = (unsigned long)left->step & MH_U16_MAX;
		carry = fraction + step > MH_U16_MAX;

		for (channel = 0; channel < 3; channel++) {
			values->value[channel] =
				add_word(values->value[channel],
					values->row_step[channel]);

			if (carry)
				values->value[channel] =
					add_word(values->value[channel],
						values->pixel_step[channel]);
		}

		left->x = add_word(left->x, left->step);
		right->x = add_word(right->x, right->step);
	}
}

static enum mh_error_code
perspective_rows(const struct mh_software_surface *surface, struct edge *left,
	struct edge *right, long first, long end, struct interpolation *values,
	const struct mh_software_texture *texture, int write_pixels,
	struct mh_error *error_out)
{
	long y, high;
	unsigned long fraction, step;
	enum mh_error_code status;

	for (y = first; y < end; y++) {
		high = ceil_fixed(right->x);

		if (high > (long)surface->width)
			high = surface->width;

		status = mh_software_perspective_span(&values->perspective,
			surface, texture, ceil_fixed(left->x), high, y,
			write_pixels, values->texture_state, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		fraction = ((unsigned long)left->x - 1) & MH_U16_MAX;
		step = (unsigned long)left->step & MH_U16_MAX;
		mh_software_perspective_advance(
			&values->perspective, fraction + step > MH_U16_MAX);
		left->x = add_word(left->x, left->step);
		right->x = add_word(right->x, right->step);
	}

	return MH_ERROR_NONE;
}

/*
 * Linear-address bounds exclude texel zero. Invalid addresses repeat the last
 * index (initially zero). Pixel and span steps may have different owners.
 */
static enum mh_error_code
fallback_span(const struct mh_software_surface *surface,
	const struct mh_software_texture *texture,
	const struct mh_software_texture_state *state, long uv[2], long low,
	long high, long y, int write_pixels, struct mh_error *error_out)
{
	long offset, step, x;
	unsigned long fraction[2], increment[2], sum;
	unsigned int axis, palette_index;

	offset = (long)floor((double)uv[1] / MH_SOFTWARE_TEXTURE_SCALE) *
			(long)texture->width +
		(long)floor((double)uv[0] / MH_SOFTWARE_TEXTURE_SCALE);
	step = (long)floor((double)state->span_step[1] /
		MH_SOFTWARE_TEXTURE_SCALE) *
			(long)state->width +
		(long)floor((double)state->span_step[0] /
			MH_SOFTWARE_TEXTURE_SCALE);

	for (axis = 0; axis < 2; axis++) {
		fraction[axis] = (unsigned long)uv[axis] &
			(MH_SOFTWARE_TEXTURE_SCALE - 1);
		increment[axis] = (unsigned long)state->span_step[axis] &
			(MH_SOFTWARE_TEXTURE_SCALE - 1);
	}

	palette_index = 0;

	for (x = low; x < high; x++) {
		if (offset > 0 &&
			(unsigned long)offset <
				(size_t)texture->width * texture->height)
			palette_index = texture->indices[offset];

		if (palette_index >= texture->palette->count)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software fallback index outside palette");

		if (write_pixels)
			write_pixel(surface, x, y,
				texture->palette->color[palette_index]);

		offset = add_word(offset, (unsigned long)step);

		for (axis = 0; axis < 2; axis++) {
			sum = fraction[axis] + increment[axis];

			if (sum >= MH_SOFTWARE_TEXTURE_SCALE)
				offset = add_word(
					offset, axis == 0 ? 1 : state->width);

			fraction[axis] = sum & (MH_SOFTWARE_TEXTURE_SCALE - 1);
		}
	}

	return MH_ERROR_NONE;
}

/*
 * Affine samples and UV advance at a clipped left edge. The cutout span skips
 * index zero before looking up the shaded color.
 */
static enum mh_error_code
texture_rows(const struct mh_software_surface *surface, struct edge *left,
	struct edge *right, long first, long end, struct interpolation *values,
	const struct mh_software_texture *texture, int additive,
	int write_pixels, struct mh_error *error_out)
{
	long x, y, low, high, uv[2], skip;
	unsigned long fraction, step;
	unsigned int axis, palette_index, packed_color;
	size_t offset;
	int carry;

	if (values->corrected)
		return perspective_rows(surface, left, right, first, end,
			values, texture, write_pixels, error_out);

	for (y = first; y < end; y++) {
		low = ceil_fixed(left->x);
		high = ceil_fixed(right->x);
		skip = low < 0 ? -low : 0;

		if (low < 0)
			low = 0;

		if (high > (long)surface->width)
			high = surface->width;

		for (axis = 0; axis < 2; axis++)
			uv[axis] = add_word(values->value[axis],
				(unsigned long)skip *
					(unsigned long)
						values->pixel_step[axis]);

		if (values->fallback) {
			enum mh_error_code status;

			status = fallback_span(surface, texture,
				values->texture_state, uv, low, high, y,
				write_pixels, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		for (x = low; !values->fallback && x < high; x++) {
			if (uv[0] < 0 || uv[1] < 0 ||
				uv[0] >= (long)texture->width *
						MH_SOFTWARE_TEXTURE_SCALE ||
				uv[1] >= (long)texture->height *
						MH_SOFTWARE_TEXTURE_SCALE)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"software texture sample outside image");

			offset = (size_t)(uv[1] / MH_SOFTWARE_TEXTURE_SCALE) *
					texture->width +
				(size_t)(uv[0] / MH_SOFTWARE_TEXTURE_SCALE);
			palette_index = texture->indices[offset];

			if (palette_index >= texture->palette->count)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"software texture index outside palette");

			if (write_pixels &&
				(additive || !texture->zero_transparent ||
					palette_index != 0)) {
				packed_color =
					texture->palette->color[palette_index];

				if (additive)
					packed_color = add_color(
						surface, x, y, packed_color);

				write_pixel(surface, x, y, packed_color);
			}

			for (axis = 0; axis < 2; axis++)
				uv[axis] = add_word(
					uv[axis], values->pixel_step[axis]);
		}

		fraction = ((unsigned long)left->x - 1) & MH_U16_MAX;
		step = (unsigned long)left->step & MH_U16_MAX;
		carry = fraction + step > MH_U16_MAX;

		for (axis = 0; axis < 2; axis++) {
			values->value[axis] = add_word(
				values->value[axis], values->row_step[axis]);

			if (carry)
				values->value[axis] =
					add_word(values->value[axis],
						values->pixel_step[axis]);
		}

		left->x = add_word(left->x, left->step);
		right->x = add_word(right->x, right->step);
	}

	return MH_ERROR_NONE;
}

static enum mh_error_code
texture_check(const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle, struct mh_error *error_out)
{
	const struct mh_software_texture *texture;
	unsigned int i;

	texture = triangle->texture;

	if (texture->indices == NULL || texture->palette == NULL ||
		texture->width == 0 || texture->height == 0 ||
		texture->width > TEXTURE_LIMIT ||
		texture->height > TEXTURE_LIMIT ||
		texture->size < (size_t)texture->width * texture->height ||
		texture->palette->count == 0 ||
		texture->palette->count > MH_SOFTWARE_PALETTE_COLORS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software indexed texture");

	if (surface->width > MH_SOFTWARE_VIEW_MAX_WIDTH || triangle->smooth ||
		triangle->outline || surface->bits[0] != 5 ||
		surface->bits[2] != 5 || surface->shift[2] != 0 ||
		surface->shift[1] != 5 ||
		surface->bits[1] != texture->palette->green_bits ||
		(surface->bits[1] != 5 && surface->bits[1] != 6) ||
		surface->shift[0] != surface->bits[1] + 5)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported software textured triangle format");

	if (!triangle->additive && !texture->zero_transparent &&
		triangle->perspective_shift != 0) {
		if (triangle->perspective_shift < PERSPECTIVE_SHIFT_MIN ||
			triangle->perspective_shift > PERSPECTIVE_SHIFT_MAX)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported software perspective block size");

		for (i = 0; i < 3; i++)
			if (!(triangle->perspective_weight[i] >=
					MIN_PERSPECTIVE_WEIGHT &&
				triangle->perspective_weight[i] <=
					MAX_PERSPECTIVE_WEIGHT))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"invalid software perspective weight");
	}

	for (i = 0; i < 3; i++) {
		if (!(triangle->uv[i][0] >= 0 &&
			triangle->uv[i][0] <=
				(float)texture->width - 0.5f &&
			triangle->uv[i][1] >= 0 &&
			triangle->uv[i][1] <=
				(float)texture->height - 0.5f))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"software texture coordinates outside image");
	}

	return MH_ERROR_NONE;
}

/* Right edge is exclusive for fills, included in outlines. */
static void
flat_rows(const struct mh_software_surface *surface, struct edge *left,
	struct edge *right, long first, long end, unsigned int color,
	int outline)
{
	long x, low, high, y;

	for (y = first; y < end; y++) {
		low = ceil_fixed(left->x);
		high = ceil_fixed(right->x);

		if (outline) {
			if (high >= 0 && high < (long)surface->width)
				write_pixel(surface, high, y, color);

			if (low >= 0 && low < (long)surface->width)
				write_pixel(surface, low, y, color);
		} else {
			if (low < 0)
				low = 0;

			if (high > (long)surface->width)
				high = surface->width;

			for (x = low; x < high; x++)
				write_pixel(surface, x, y, color);
		}

		left->x = add_word(left->x, left->step);
		right->x = add_word(right->x, right->step);
	}
}

static enum mh_error_code	raster_triangle(
	const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle,
	struct mh_software_texture_state *state, int write_pixels,
	struct mh_error *error_out);

static enum mh_error_code
validate_triangle(const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle,
	struct mh_software_texture_state *state, struct mh_error *error_out)
{
	unsigned int i, channel, mask, used;
	float light_limit, light_minimum;
	enum mh_error_code status;

	if (surface == NULL || triangle == NULL || surface->pixels == NULL ||
		surface->width == 0 || surface->height == 0 ||
		surface->width > MH_VIEW_MAX_WIDTH ||
		surface->height > MH_VIEW_MAX_HEIGHT ||
		surface->pitch < (size_t)surface->width * 2 ||
		surface->height > surface->size / surface->pitch)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software surface");

	used = 0;

	for (i = 0; i < 3; i++) {
		if (surface->bits[i] == 0 || surface->bits[i] > 8 ||
			surface->shift[i] > 16 - surface->bits[i])
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software pixel format");

		mask = ((1U << surface->bits[i]) - 1) << surface->shift[i];

		if (used & mask)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"overlapping software color channels");

		used |= mask;

		if (!(triangle->fog_color[i] >= 0 &&
			triangle->fog_color[i] <= COLOR_BYTE_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"invalid software triangle color");
	}

	light_limit = triangle->smooth ? COLOR_BYTE_MAX : MH_SOFTWARE_LIGHT_MAX;
	light_minimum = !triangle->smooth ? -light_limit : 0;

	for (i = 0; i < (triangle->smooth ? 3U : 1U); i++) {
		for (channel = 0; channel < 3; channel++) {
			if (!(triangle->color[i][channel] >= light_minimum &&
				triangle->color[i][channel] <= light_limit))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"invalid software triangle color");
		}
	}

	if (!(triangle->fog >= 0 && triangle->fog <= 1))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid software triangle fog");

	if (triangle->additive && triangle->texture == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"software additive drawing needs a texture");

	if (triangle->texture != NULL) {
		status = texture_check(surface, triangle, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * Check every sample before a frame can replace its previous
		 * image.
		 */

		return raster_triangle(surface, triangle, state, 0, error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/*
 * Compare signed binary32 words, not floating-point values. Negative Y values
 * consequently sort in reverse numeric order. Keep that behavior (including
 * signed zero) before clamping the row starts.
 */
static int
y_before(float left, float right)
{
	static const float negative_zero = -0.0f;
	int left_negative, right_negative;

	left_negative = left < 0 ||
		(left == 0 && memcmp(&left, &negative_zero, sizeof(left)) == 0);
	right_negative = right < 0 ||
		(right == 0 &&
			memcmp(&right, &negative_zero, sizeof(right)) == 0);

	if (left_negative != right_negative)
		return left_negative;

	return left_negative ? left >= right : left <= right;
}

static enum mh_error_code
raster_triangle(const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle,
	struct mh_software_texture_state *state, int write_pixels,
	struct mh_error *error_out)
{
	const float *top, *middle, *bottom;
	struct edge long_edge, short_edge;
	struct interpolation values;
	unsigned int top_corner, middle_corner, bottom_corner;
	unsigned int color, i;
	long first, split, end;
	float scaled;
	int long_left, interpolated;
	enum mh_error_code status;

	/* Reject the primitive, not the frame, before edge setup. */
	for (i = 0; i < 3; i++)
		if (!(triangle->xy[i][0] >= -COORDINATE_LIMIT &&
			triangle->xy[i][0] <= COORDINATE_LIMIT &&
			triangle->xy[i][1] >= -COORDINATE_LIMIT &&
			triangle->xy[i][1] <= COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* Retain the original tie ordering and winding flag. */
	if (y_before(triangle->xy[0][1], triangle->xy[1][1])) {
		if (y_before(triangle->xy[1][1], triangle->xy[2][1])) {
			top_corner = 0;
			middle_corner = 1;
			bottom_corner = 2;
			long_left = 1;
		} else if (y_before(triangle->xy[0][1], triangle->xy[2][1])) {
			top_corner = 0;
			middle_corner = 2;
			bottom_corner = 1;
			long_left = 0;
		} else {
			top_corner = 2;
			middle_corner = 0;
			bottom_corner = 1;
			long_left = 1;
		}
	} else if (y_before(triangle->xy[0][1], triangle->xy[2][1])) {
		top_corner = 1;
		middle_corner = 0;
		bottom_corner = 2;
		long_left = 0;
	} else if (y_before(triangle->xy[1][1], triangle->xy[2][1])) {
		top_corner = 1;
		middle_corner = 2;
		bottom_corner = 0;
		long_left = 1;
	} else {
		top_corner = 2;
		middle_corner = 1;
		bottom_corner = 0;
		long_left = 0;
	}

	top = triangle->xy[top_corner];
	middle = triangle->xy[middle_corner];
	bottom = triangle->xy[bottom_corner];
	scaled = top[1] * (float)FIXED_SCALE;
	first = ceil_fixed(round_integer(scaled));
	scaled = middle[1] * (float)FIXED_SCALE;
	split = ceil_fixed(round_integer(scaled));
	scaled = bottom[1] * (float)FIXED_SCALE;
	end = ceil_fixed(round_integer(scaled));

	if (first < 0)
		first = 0;

	if (split < 0)
		split = 0;

	if (split > (long)surface->height)
		split = surface->height;

	if (end > (long)surface->height)
		end = surface->height;

	if (first >= end)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	color = 0;

	interpolated = triangle->smooth || triangle->texture != NULL;

	if (interpolated) {
		start_interpolation(&values, triangle, state);

		if (triangle->texture != NULL && values.empty)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	} else
		color = pack_color(surface, triangle);

	start_edge(&long_edge, top, bottom, first);

	if (interpolated && long_left)
		start_edge_values(
			&values, &long_edge, triangle, top_corner, first);

	if (first < split) {
		start_edge(&short_edge, top, middle, first);

		if (interpolated) {
			if (!long_left)
				start_edge_values(&values, &short_edge,
					triangle, top_corner, first);

			if (triangle->texture != NULL) {
				status = texture_rows(surface,
					long_left ? &long_edge : &short_edge,
					long_left ? &short_edge : &long_edge,
					first, split, &values,
					triangle->texture, triangle->additive,
					write_pixels, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			} else
				smooth_rows(surface,
					long_left ? &long_edge : &short_edge,
					long_left ? &short_edge : &long_edge,
					first, split, &values);
		} else
			flat_rows(surface, long_left ? &long_edge : &short_edge,
				long_left ? &short_edge : &long_edge, first,
				split, color, triangle->outline);
	}

	if (split < end) {
		start_edge(&short_edge, middle, bottom, split);

		if (interpolated) {
			if (!long_left)
				start_edge_values(&values, &short_edge,
					triangle, middle_corner, split);

			if (triangle->texture != NULL) {
				status = texture_rows(surface,
					long_left ? &long_edge : &short_edge,
					long_left ? &short_edge : &long_edge,
					split, end, &values, triangle->texture,
					triangle->additive, write_pixels,
					error_out);

				if (status != MH_ERROR_NONE)
					return status;
			} else
				smooth_rows(surface,
					long_left ? &long_edge : &short_edge,
					long_left ? &short_edge : &long_edge,
					split, end, &values);
		} else
			flat_rows(surface, long_left ? &long_edge : &short_edge,
				long_left ? &short_edge : &long_edge, split,
				end, color, triangle->outline);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_triangle_check(const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle,
	struct mh_software_texture_state *state, struct mh_error *error_out)
{
	struct mh_software_texture_state next;
	enum mh_error_code status;

	memset(&next, 0, sizeof(next));

	if (state != NULL)
		next = *state;

	status = validate_triangle(surface, triangle, &next, error_out);

	if (status == MH_ERROR_NONE && state != NULL)
		*state = next;

	return status;
}

enum mh_error_code
mh_software_triangle(const struct mh_software_surface *surface,
	const struct mh_software_triangle *triangle,
	struct mh_software_texture_state *state, struct mh_error *error_out)
{
	struct mh_software_texture_state next, checked;
	enum mh_error_code status;

	memset(&next, 0, sizeof(next));

	if (state != NULL)
		next = *state;

	checked = next;
	status = validate_triangle(surface, triangle, &checked, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = raster_triangle(surface, triangle, &next, 1, error_out);

	if (status == MH_ERROR_NONE && state != NULL)
		*state = next;

	return status;
}
