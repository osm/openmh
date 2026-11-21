#include <float.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/brightness.h"

#define TRACK_BRIGHTNESS_LIMIT 100
#define USER_BRIGHTNESS_LIMIT 2
#define WEATHER_BRIGHTNESS_LIMIT 200
#define SHADE_COLOR_SCALE 256

enum mh_error_code
mh_brightness_prepare(const double world[3], const double object[3],
	const float user[3], int dimmed, const struct mh_weather_style *weather,
	struct mh_brightness *output, struct mh_error *error_out)
{
	struct mh_brightness result;
	const double *source;
	long double value;
	float scale, stored;
	unsigned int channel, object_pass;

	if (world == NULL || object == NULL || output == NULL ||
		(dimmed != 0 && dimmed != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid brightness input");

	memset(&result, 0, sizeof(result));

	for (channel = 0; channel < 3; channel++) {
		if (!(world[channel] >= 0 &&
			world[channel] <= TRACK_BRIGHTNESS_LIMIT) ||
			!(object[channel] >= 0 &&
				object[channel] <= TRACK_BRIGHTNESS_LIMIT) ||
			(user != NULL &&
				!(user[channel] >= -FLT_MAX &&
					user[channel] <=
						USER_BRIGHTNESS_LIMIT)) ||
			(weather != NULL &&
				!(weather->brightness[channel] >= 0 &&
					weather->brightness[channel] <=
						WEATHER_BRIGHTNESS_LIMIT)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid brightness scale");

		scale = user == NULL ? 1 : user[channel];

		for (object_pass = 0; object_pass < 2; object_pass++) {
			source = object_pass ? object : world;
			value = (long double)(float)source[channel] * scale;

			if (!(value >= -FLT_MAX && value <= FLT_MAX))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"brightness product overflow");

			/*
			 * Leave blue extended across the half multiply;
			 * red/green reload their rounded stores before
			 * multiplying.
			 */
			stored = (float)value;

			if (dimmed) {
				if (channel == 2)
					stored = (float)(value * .5);
				else
					stored = (float)((long double)stored *
						.5);
			}

			if (object_pass)
				result.object[channel] = stored;
			else
				result.world[channel] = stored < 0 ? 0 : stored;
		}
	}

	if (result.object[0] < 0)
		result.object[0] = 0;

	/* Write WORLD green/blue, not object RGB. */
	for (channel = 1; channel < 3; channel++)
		if (result.object[channel] < 0)
			result.world[channel] = 0;

	if (weather != NULL)
		for (channel = 0; channel < 3; channel++)
			result.world[channel] = result.object[channel] =
				(float)weather->brightness[channel];

	for (channel = 0; channel < 3; channel++)
		if (result.object[channel] <
			-(double)FLT_MAX / SHADE_COLOR_SCALE)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"object brightness outside shading range");

	result.particle = (float)(((long double)result.world[0] +
					result.world[1] + result.world[2]) *
		(double)(1.0 / 3.0));
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
