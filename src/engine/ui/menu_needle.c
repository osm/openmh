#include <math.h>
#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_needle.h"

#define ANGLE_LIMIT 1000000000.0f
#define RESPONSE_LIMIT 1024.0f
#define COORDINATE_LIMIT 32768
#define LENGTH_LIMIT 64
#define WIDTH_LIMIT 256
#define RESPONSE_SCALE 0.0003333333333333333
#define NEEDLE_PI 3.141592654
#define HALF_TURN_RECIPROCAL 0.00048828125
#define NEEDLE_ASPECT 0.8333333134651184f

static int
bounded(float value, float limit)
{
	return value >= -limit && value <= limit;
}

enum mh_error_code
mh_menu_needle_init(struct mh_menu_needle *needle, float angle, float response,
	struct mh_error *error_out)
{
	if (needle == NULL || !bounded(angle, ANGLE_LIMIT) ||
		!bounded(response, RESPONSE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu needle state");

	needle->angle = angle;
	needle->response = response;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_needle_step(struct mh_menu_needle *needle, float x, float y,
	float target, float length, float width, unsigned int color,
	unsigned long elapsed_ms, struct mh_menu_needle_draw *output,
	struct mh_error *error_out)
{
	static const float local_y[MH_MENU_NEEDLE_POINTS] = {
		-5, -40, -5, -5, -5, 10};

	struct mh_menu_needle_draw result;
	float difference, delta, angle, radians, sine, cosine, local_x;
	unsigned int i;

	if (needle == NULL || output == NULL ||
		!bounded(needle->angle, ANGLE_LIMIT) ||
		!bounded(needle->response, RESPONSE_LIMIT) ||
		!bounded(target, ANGLE_LIMIT) ||
		!bounded(x, COORDINATE_LIMIT) ||
		!bounded(y, COORDINATE_LIMIT) ||
		!bounded(length, LENGTH_LIMIT) ||
		!bounded(width, WIDTH_LIMIT) || elapsed_ms > MH_U32_MAX ||
		color > 255)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu needle request");

	difference = (float)((long double)target - needle->angle);
	delta = (float)((long double)elapsed_ms * difference *
		needle->response * RESPONSE_SCALE);
	angle = (float)((long double)needle->angle + delta);

	if (!bounded(angle, ANGLE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"menu needle angle overflow");

	radians =
		(float)((long double)angle * NEEDLE_PI * HALF_TURN_RECIPROCAL);
	sine = (float)sin((double)radians);
	cosine = (float)cos((double)radians);
	sine = (float)((long double)sine * length);
	cosine = (float)((long double)cosine * length);

	for (i = 0; i < MH_MENU_NEEDLE_POINTS; i++) {
		local_x = i == 1 || i == 5
			? 0
			: (i == 0 || i == 3 ? -width : width);
		result.points[i].x = (float)((long double)local_y[i] * cosine +
			(long double)local_x * sine + x);
		result.points[i].y =
			(float)(((long double)local_x * cosine -
					(long double)local_y[i] * sine) *
					NEEDLE_ASPECT +
				y);

		if (!bounded(result.points[i].x, COORDINATE_LIMIT) ||
			!bounded(result.points[i].y, COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu needle vertex overflow");
	}

	result.color = color;
	needle->angle = angle;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
