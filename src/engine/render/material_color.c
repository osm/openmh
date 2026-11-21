#include <stddef.h>

#include "engine/core/limits.h"
#include "engine/render/material_color.h"

#define INPUT_LIMIT 1000000.0f
#define PACK_LIMIT 1000000000.0f
#define OBJECT_TEXTURE_BASE 200.0f
#define CAR_TEXTURE_BASE 120.0f
#define CAR_LIGHT_SCALE 50.0f
#define SOLID_LIGHT_SCALE 1.0f
#define COLOR_MAX 255.0f

enum mh_error_code
mh_material_color_pack(
	const float color[3], unsigned long *packed, struct mh_error *error_out)
{
	unsigned long channels[3];
	unsigned int channel;

	if (color == NULL || packed == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid material color packing");

	for (channel = 0; channel < 3; channel++) {
		if (!(color[channel] >= -PACK_LIMIT &&
			color[channel] <= PACK_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"material color outside packing range");

		channels[channel] = (unsigned long)(long)color[channel];
	}

	*packed = ((channels[0] << 16) | (channels[1] << 8) | channels[2]) &
		MH_U32_MAX;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
scale_color(const unsigned char rgb[3], const float brightness[3], int clamp,
	float color[3], struct mh_error *error_out)
{
	float result[3];
	unsigned int channel;

	if (rgb == NULL || brightness == NULL || color == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid material color scale");

	for (channel = 0; channel < 3; channel++) {
		if (!(brightness[channel] >= -INPUT_LIMIT &&
			brightness[channel] <= INPUT_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid material color scale");

		result[channel] = (float)((long double)rgb[channel] *
			brightness[channel]);

		if (clamp)
			result[channel] = result[channel] < 0 ? 0
				: result[channel] > COLOR_MAX ? COLOR_MAX
							: result[channel];
	}

	for (channel = 0; channel < 3; channel++)
		color[channel] = result[channel];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_prelit_color(const unsigned char rgb[3], const float brightness[3],
	float color[3], struct mh_error *error_out)
{
	return scale_color(rgb, brightness, 1, color, error_out);
}

enum mh_error_code
mh_emissive_color(const unsigned char rgb[3], const float brightness[3],
	float color[3], struct mh_error *error_out)
{
	return scale_color(rgb, brightness, 0, color, error_out);
}

enum mh_error_code
mh_material_color(enum mh_material_kind material, unsigned int mode,
	const unsigned char rgb[3], const float brightness[3],
	const float light[3], float color[3], struct mh_error *error_out)
{
	float result[3], base, scale;
	long double value;
	unsigned int channel;

	if (rgb == NULL || brightness == NULL || light == NULL ||
		color == NULL ||
		(material != MH_MATERIAL_SOLID &&
			material != MH_MATERIAL_TEXTURED &&
			material != MH_MATERIAL_ENVIRONMENT) ||
		(mode != MH_LIGHT_OBJECT && mode != MH_LIGHT_CAR))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid material color");

	for (channel = 0; channel < 3; channel++)
		if (!(brightness[channel] >= -INPUT_LIMIT &&
			brightness[channel] <= INPUT_LIMIT &&
			light[channel] >= -INPUT_LIMIT &&
			light[channel] <= INPUT_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid material color");

	scale = material == MH_MATERIAL_SOLID ? SOLID_LIGHT_SCALE
					: CAR_LIGHT_SCALE;

	for (channel = 0; channel < 3; channel++) {
		base = material == MH_MATERIAL_TEXTURED
			? (mode == MH_LIGHT_CAR ? CAR_TEXTURE_BASE
						: OBJECT_TEXTURE_BASE)
			: rgb[channel];

		base = (float)((long double)brightness[channel] * base);
		value = base;

		if (mode == MH_LIGHT_CAR) {
			value += (long double)scale * light[channel];

			/* Only red keeps the light sum in long double. */
			if (channel != 0)
				value = (float)value;
		}

		base = (float)(value * light[channel]);
		result[channel] = base < 0 ? 0
			: base > COLOR_MAX ? COLOR_MAX
					: base;
	}

	for (channel = 0; channel < 3; channel++)
		color[channel] = result[channel];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
