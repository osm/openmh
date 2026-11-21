#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_gauge.h"
#include "engine/ui/menu_sine.h"

#define COORDINATE_LIMIT 16384
#define VALUE_LIMIT 16
#define GAUGE_SWEEP 3740
#define GAUGE_ASPECT 0.8333333
#define SEGMENT_STEP 468
#define FIRST_PHASE 3250
#define SEGMENT_SWEEP 467.5
#define END_PHASE_STEP 4565

static unsigned int
phase_index(long double value)
{
	return (unsigned int)((unsigned long)(long)value &
		(unsigned long)MH_MENU_SINE_PHASE_MASK);
}

static void
rim_point(float x, float y, float radius, float vertical_radius,
	unsigned int phase, struct mh_menu_point *point)
{
	point->x = (float)((long double)mh_menu_sine(
				phase + MH_MENU_SINE_QUARTER_TURN) *
			radius +
		x);

	point->y =
		(float)((long double)mh_menu_sine(phase) * vertical_radius + y);
}

enum mh_error_code
mh_menu_gauge(float x, float y, float radius, float value, unsigned int color,
	struct mh_menu_gauge_draw *output, struct mh_error *error_out)
{
	struct mh_menu_gauge_draw result;
	struct mh_menu_point *point;
	float remaining, vertical_radius;
	unsigned int i, segment, start_phase, end_phase;
	int partial;

	if (output == NULL ||
		!(x >= -COORDINATE_LIMIT && x <= COORDINATE_LIMIT) ||
		!(y >= -COORDINATE_LIMIT && y <= COORDINATE_LIMIT) ||
		!(radius >= 0 && radius <= COORDINATE_LIMIT) ||
		!(value >= -VALUE_LIMIT && value <= VALUE_LIMIT) || color > 255)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid menu gauge");

	memset(&result, 0, sizeof(result));
	result.color = color;
	remaining = (float)((1 - (long double)value) * GAUGE_SWEEP);
	vertical_radius = (float)((long double)radius * GAUGE_ASPECT);

	for (i = 0; i < MH_MENU_GAUGE_TRIANGLES; i++) {
		segment = MH_MENU_GAUGE_TRIANGLES - 1 - i;
		start_phase = (segment * SEGMENT_STEP + FIRST_PHASE) &
			MH_MENU_SINE_PHASE_MASK;
		partial = remaining < SEGMENT_STEP;

		if (partial) {
			start_phase = phase_index((long double)start_phase +
				SEGMENT_SWEEP - remaining);
			end_phase = phase_index(
				(long double)start_phase + remaining);
		} else
			end_phase = (start_phase + END_PHASE_STEP) &
				MH_MENU_SINE_PHASE_MASK;

		point = result.points + result.count * 3;
		point[0].x = x;
		point[0].y = y;
		rim_point(
			x, y, radius, vertical_radius, start_phase, &point[1]);
		rim_point(x, y, radius, vertical_radius, end_phase, &point[2]);
		result.count++;

		if (partial)
			break;

		remaining = (float)((long double)remaining - SEGMENT_SWEEP);
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
