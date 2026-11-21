#ifndef MH_BRIGHTNESS_H
#define MH_BRIGHTNESS_H

#include "engine/core/error.h"
#include "engine/render/hidden_weather.h"

struct mh_brightness {
	/* Original stored binary32 values. */
	double	world[3], object[3];
	float	particle;
};

/*
 * Race/menu setters followed by the renderer's weather override and world
 * RGB mean. NULL user means 1/1/1. Negative object G/B retain the original
 * setter's wrong-destination clamp; output stays unchanged on failure.
 */
enum mh_error_code	mh_brightness_prepare(const double world[3],
	const double object[3], const float user[3], int dimmed,
	const struct mh_weather_style *weather, struct mh_brightness *output,
	struct mh_error *error_out);

#endif
