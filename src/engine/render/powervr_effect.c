#include <stddef.h>

#include "engine/render/powervr_effect.h"

#define COLOR_MAX 255UL
#define PARTICLE_INTENSITY_SCALE 8.0f
#define PARTICLE_AVERAGE (1.0f / 3.0f)
#define SHADOW_DEPTH_OFFSET (-.1)
#define SHADOW_TRANSMISSION_SCALE (1.0f / 16.0f)
#define SHADOW_ATLAS_U 129.0
#define SHADOW_ATLAS_V .25390625
#define SHADOW_ATLAS_SCALE (1.0 / 256.0)

static int
valid_color(const float color[3])
{
	unsigned int channel;

	if (color == NULL)
		return 0;

	for (channel = 0; channel < 3; channel++)
		if (!(color[channel] >= 0 && color[channel] <= COLOR_MAX))
			return 0;

	return 1;
}

/* Normalize RGB hue, retain peak intensity as opacity. */
enum mh_error_code
mh_powervr_halo_color(
	const float color[3], unsigned long *packed, struct mh_error *error_out)
{
	unsigned long rgb[3], peak;
	unsigned int channel;

	if (!valid_color(color) || packed == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR halo color");

	peak = 0;

	for (channel = 0; channel < 3; channel++) {
		rgb[channel] = (unsigned long)color[channel];

		if (rgb[channel] > peak)
			peak = rgb[channel];
	}

	if (peak != 0)
		for (channel = 0; channel < 3; channel++)
			rgb[channel] = rgb[channel] * COLOR_MAX / peak;

	*packed = (peak << 24) | (rgb[0] << 16) | (rgb[1] << 8) | rgb[2];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* White particle, opacity from amplified average RGB. */
enum mh_error_code
mh_powervr_particle_color(
	const float color[3], unsigned long *packed, struct mh_error *error_out)
{
	long double intensity;
	unsigned long opacity;

	if (!valid_color(color) || packed == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR particle color");

	intensity = (long double)color[0] + color[1];
	intensity += color[2];
	intensity *= PARTICLE_INTENSITY_SCALE;
	intensity *= PARTICLE_AVERAGE;
	opacity = intensity > COLOR_MAX ? COLOR_MAX : (unsigned long)intensity;
	*packed = (opacity << 24) | 0x00ffffffUL;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Keep intermediates in long double between float conversions. */
enum mh_error_code
mh_powervr_shadow_corner(float reciprocal_w, unsigned char transmission,
	struct mh_powervr_shadow_corner *output, struct mh_error *error_out)
{
	struct mh_powervr_shadow_corner result;
	long double inverse, u;

	if (output == NULL ||
		!(reciprocal_w > 0 && reciprocal_w < -1 / SHADOW_DEPTH_OFFSET))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR shadow depth");

	inverse = 1.0L / reciprocal_w;
	inverse += SHADOW_DEPTH_OFFSET;
	inverse = 1.0L / inverse;
	u = (COLOR_MAX - transmission) * SHADOW_TRANSMISSION_SCALE;
	u += SHADOW_ATLAS_U;
	u *= SHADOW_ATLAS_SCALE;
	result.reciprocal_w = (float)inverse;
	result.uv[0] = (float)(u * inverse);
	result.uv[1] =
		(float)((long double)result.reciprocal_w * SHADOW_ATLAS_V);
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
