#ifndef MH_HIDDEN_WEATHER_H
#define MH_HIDDEN_WEATHER_H

#include "engine/core/error.h"

struct mh_weather_style {
	double		brightness[3], fog[3];
	unsigned long	top, bottom;

	/* Stored binary32 mean of world RGB. */
	float		particle_brightness;
};

/*
 * Change projected XY, after clipping and texture coordinate generation. The
 * caller owns the process-lifetime phase. Advance once per rendered world
 * frame, even if its lists are empty.
 */
struct mh_water_frame {
	float	width, height, phase, scale, frequency;
};

/*
 * Special immediate sky shared by Ramlosa and Lemmy. Focal is the
 * horizontal projection multiplier, not a field of view in degrees.
 * Underwater selects .001 position scrolling; the other path uses .005.
 * Colors are original packed ARGB. Pixel Y points down.
 */
struct mh_weather_camera {
	unsigned int	width, height;
	float		focal, forward[3], position[3];
	int		underwater;
	unsigned long	top, bottom;
};

struct mh_weather_vertex {
	float		xy[2], depth, reciprocal_w, uv[2];
	unsigned long	color, specular;
};

/* Weather constants; brightness blue is binary32 1.2. */
const struct mh_weather_style	*mh_water_style(void);

/*
 * Consume one original 15-bit rand from shared 32-bit state. Flash only for
 * 20000 < rand < 20400. No timer or interpolation. Output points to immutable
 * storage; invalid inputs change neither output.
 */
enum mh_error_code	mh_thunder_frame(unsigned long *random,
	const struct mh_weather_style **output, struct mh_error *error_out);

enum mh_error_code	mh_water_frame(float *phase, float width, float height,
	struct mh_water_frame *output, struct mh_error *error_out);
enum mh_error_code	mh_water_point(const struct mh_water_frame *frame,
	const float xy[2], float output[2], struct mh_error *error_out);

/*
 * Two quads: environment-textured upper 60%, then an untextured fill.
 * A focal outside the original (0,180] degree gate returns zero draws.
 * Outputs remain unchanged for malformed input.
 */
enum mh_error_code	mh_weather_sky(const struct mh_weather_camera *camera,
	struct mh_weather_vertex output[8], unsigned int *draws,
	struct mh_error *error_out);

#endif
