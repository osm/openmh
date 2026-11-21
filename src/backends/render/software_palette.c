#include <string.h>

#include "backends/render/software_palette.h"

/* Binary32 value intentionally slightly greater than 1/128. */
#define PALETTE_LIGHT_SCALE 0.007813f
#define INTEGER_BIAS 4503599627370496.0
#define COLOR_BYTE_MAX 255

/* Select RGB565 or RGB555. */
enum mh_error_code
mh_software_palette_build(struct mh_software_palette *output,
	const unsigned char *rgb, size_t size, unsigned int count,
	const struct mh_software_triangle *triangle, unsigned int green_bits,
	struct mh_error *error_out)
{
	float factor[3];
	double background[3], light_scale, value;
	unsigned int entry, channel, color[3];

	if (output == NULL || rgb == NULL || triangle == NULL || count == 0 ||
		count > MH_SOFTWARE_PALETTE_COLORS ||
		size < (size_t)count * 3 ||
		(green_bits != 5 && green_bits != 6))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software palette");

	if (!(triangle->fog >= 0 && triangle->fog <= 1))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"invalid software palette fog");

	for (channel = 0; channel < 3; channel++) {
		if (!(triangle->color[0][channel] >= -MH_SOFTWARE_LIGHT_MAX &&
			triangle->color[0][channel] <=
				MH_SOFTWARE_LIGHT_MAX &&
			triangle->fog_color[channel] >= 0 &&
			triangle->fog_color[channel] <= COLOR_BYTE_MAX))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"invalid software palette color");
	}

	light_scale = (1.0 - triangle->fog) * PALETTE_LIGHT_SCALE;

	for (channel = 0; channel < 3; channel++) {
		factor[channel] =
			(float)(triangle->color[0][channel] * light_scale);
		value = (double)triangle->fog_color[channel] * triangle->fog;
		background[channel] = value + INTEGER_BIAS;
	}

	/* The two bias additions round separately, including the fog term. */
	for (entry = 0; entry < count; entry++) {
		for (channel = 0; channel < 3; channel++) {
			value = (double)rgb[entry * 3 + channel] *
				factor[channel];
			value = value + background[channel];
			value = value - INTEGER_BIAS;
			color[channel] = value < 0 || value > COLOR_BYTE_MAX
				? COLOR_BYTE_MAX
				: (unsigned int)value;
		}

		output->color[entry] = ((color[0] >> 3) << (green_bits + 5)) |
			((color[1] >> (8 - green_bits)) << 5) | (color[2] >> 3);
	}

	/*
	 * Readers use count; clear unused slots so output has no stale colors.
	 */
	memset(output->color + count, 0,
		(MH_SOFTWARE_PALETTE_COLORS - count) *
			sizeof(output->color[0]));
	output->count = count;
	output->green_bits = green_bits;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
