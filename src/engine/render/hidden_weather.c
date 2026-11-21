#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/render/hidden_weather.h"

#define FLASH_START 20000
#define FULL_TURN_F32 6.2831854820251465f
#define FULL_TURN 6.28318530718
#define VIEWPORT_LIMIT 20000
#define FLASH_END 20400
#define PROJECTION_LIMIT 1e6f
#define POSITION_LIMIT 1e7f
#define WATER_PHASE_STEP .05f
#define WATER_SCALE_SWING .05
#define WATER_SCALE_BASE 1.2
#define WATER_FREQUENCY_SCALE 25
#define WATER_WAVE_AMPLITUDE 10.0L
#define ZERO_FOCAL_TANGENT 1e300
#define FOV_DEGREES_SCALE 114.5915590261571
#define HALF_TURN_F32 3.1415927410125732f
#define DEGREES_TO_TURNS .0027777778450399637f

static int
bounded(float value, float limit)
{
	return value >= -limit && value <= limit;
}

const struct mh_weather_style *
mh_water_style(void)
{
	static const struct mh_weather_style style = {
		{.5, 1, 1.2000000476837158}, {0, 100, 64}, 0x7f60a0ffUL,

		0x7f00a080UL, .9000000357627869f};

	return &style;
}

enum mh_error_code
mh_thunder_frame(unsigned long *random, const struct mh_weather_style **output,
	struct mh_error *error_out)
{
	static const struct mh_weather_style dark = {
		{.80000001192092896, .40000000596046448, .40000000596046448},
		{0, 0, 0}, 0x7fff0000UL, 0x7f3f006fUL, .53333336114883423f};

	static const struct mh_weather_style flash = {
		{3, 3, 3}, {255, 255, 255}, 0x7f7f60ffUL, 0x7fffffffUL, 3};

	unsigned long next, value;

	if (random == NULL || output == NULL || *random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid thunder random state");

	next = *random;
	value = mh_random_next(&next);
	*output = value > FLASH_START && value < FLASH_END ? &flash : &dark;
	*random = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_water_frame(float *phase, float width, float height,
	struct mh_water_frame *output, struct mh_error *error_out)
{
	struct mh_water_frame next;
	long double raw;

	if (phase == NULL || output == NULL ||
		!(*phase >= 0 && *phase <= FULL_TURN_F32) ||
		!(width >= 1 && width <= VIEWPORT_LIMIT) ||
		!(height >= 1 && height <= VIEWPORT_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid underwater frame");

	raw = (long double)*phase + WATER_PHASE_STEP;
	next.phase = raw > (double)FULL_TURN ? 0 : (float)raw;
	next.width = width;
	next.height = height;
	next.scale = (float)((long double)cos(next.phase) *
			(double)WATER_SCALE_SWING +
		(double)WATER_SCALE_BASE);
	next.frequency = (float)(WATER_FREQUENCY_SCALE /
		(long double)(float)(height * .5f));
	*phase = next.phase;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_water_point(const struct mh_water_frame *frame, const float xy[2],
	float output[2], struct mh_error *error_out)
{
	long double x, y, wave;
	float result[2];

	if (frame == NULL || xy == NULL || output == NULL ||
		!(frame->width >= 1 && frame->width <= VIEWPORT_LIMIT) ||
		!(frame->height >= 1 && frame->height <= VIEWPORT_LIMIT) ||
		!(frame->phase >= 0 && frame->phase <= FULL_TURN_F32) ||
		!(frame->scale >= 1 && frame->scale <= 2) ||
		!(frame->frequency > 0 &&
			frame->frequency <= PROJECTION_LIMIT) ||
		!bounded(xy[0], POSITION_LIMIT) ||
		!bounded(xy[1], POSITION_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid underwater point");

	x = (long double)xy[0] - frame->width;
	y = (long double)xy[1] - frame->height;
	wave = sin((double)(y * frame->frequency + frame->phase)) *
		WATER_WAVE_AMPLITUDE;
	result[0] = (float)(x * frame->scale + frame->width + wave);
	result[1] = (float)(y * frame->scale + frame->height);
	memcpy(output, result, sizeof(result));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_weather_sky(const struct mh_weather_camera *camera,
	struct mh_weather_vertex output[8], unsigned int *draws,
	struct mh_error *error_out)
{
	struct mh_weather_vertex vertices[8];
	float angle, x, z, left_x, left_z, right_x, right_z, u, rate, split;
	long double degrees, sine, cosine_extended, inverse_length, offset_z;
	double length, cosine;
	unsigned int axis, corner;

	if (camera == NULL || output == NULL || draws == NULL ||
		camera->width < 1 || camera->width > VIEWPORT_LIMIT ||
		camera->height < 1 || camera->height > VIEWPORT_LIMIT ||
		!bounded(camera->focal, PROJECTION_LIMIT) ||
		camera->top > MH_U32_MAX || camera->bottom > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid special sky camera");

	for (axis = 0; axis < 3; axis++)
		if (!bounded(camera->forward[axis], 1) ||
			!bounded(camera->position[axis], POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid special sky pose");

	memset(vertices, 0, sizeof(vertices));
	degrees = atan(camera->focal == 0 ? ZERO_FOCAL_TANGENT
					: 1 / (double)camera->focal) *
		(long double)FOV_DEGREES_SCALE;

	if (!(degrees > 0 && degrees <= 180)) {
		memcpy(output, vertices, sizeof(vertices));
		*draws = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/* Store float FOV, then float radians. */
	angle = (float)((long double)HALF_TURN_F32 * 2 * DEGREES_TO_TURNS *
		(float)degrees);
	x = camera->forward[0];
	z = camera->forward[2];
	length = (double)((long double)x * x + (long double)z * z);

	if (length != 0) {
		inverse_length = 1 / (long double)sqrt(length);
		x = (float)(x * inverse_length);
		z = (float)(z * inverse_length);
	}

	sine = sin(-angle * .5);
	cosine = cos(-angle * .5);
	cosine_extended = cosine;
	left_x = (float)(sine * z + cosine_extended * x);
	left_z = (float)(cosine_extended * z - x * sine);
	sine = sin(angle * .5);
	cosine = cos(angle * .5);
	cosine_extended = cosine;
	right_x = (float)(sine * z + cosine_extended * x);
	right_z = (float)(cosine_extended * z - x * sine);
	left_x = (float)((long double)left_x * 1.5);
	left_z = (float)((long double)left_z * 1.5);
	right_x = (float)((long double)right_x * 1.5);
	right_z = (float)((long double)right_z * 1.5);
	rate = camera->underwater ? .001f : .005f;
	u = (float)fmod((double)camera->position[0] * rate, 1);
	offset_z = fmod((double)camera->position[2] * rate, 1);
	vertices[0].uv[0] = (float)((long double)left_x * .1f + u);
	vertices[1].uv[0] = (float)((long double)right_x * .1f + u);
	vertices[2].uv[0] = (float)((long double)right_x * .9f + u);
	vertices[3].uv[0] = (float)((long double)left_x * .9f + u);
	vertices[0].uv[1] = vertices[1].uv[1] =
		(float)((long double)left_z * .1f + offset_z);
	vertices[2].uv[1] = vertices[3].uv[1] =
		(float)((long double)right_z * .9f + offset_z);
	split = (float)((long double)camera->height * (double).6);

	for (corner = 0; corner < 8; corner++) {
		vertices[corner].xy[0] = corner % 4 == 1 || corner % 4 == 2
			? (float)camera->width
			: 0;
		vertices[corner].xy[1] = corner < 2 ? 0
			: corner < 6		    ? split
						: (float)camera->height;
		vertices[corner].depth = .9998999834060669f;
		vertices[corner].reciprocal_w = corner < 2 ? 10
			: corner < 4			   ? 1.1111111640930176f
				: .00009999999747378752f;
		vertices[corner].color =
			corner < 2 ? camera->top : camera->bottom;
		vertices[corner].specular = 0xff000000UL;
	}

	memcpy(output, vertices, sizeof(vertices));
	*draws = 2;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
