#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_gear.h"

#define TEETH_LIMIT 64
#define LINES_PER_TOOTH 6
#define GEAR_LINE_LIMIT (TEETH_LIMIT * LINES_PER_TOOTH)
#define ORIGIN_LIMIT 10000
#define RADIUS_LIMIT 4096
#define ANGLE_LIMIT 5000000
#define GEAR_ASPECT 0.833333333
#define QUARTER_TURN 1.5707963705062866f
#define TOOTH_INSET 0.10000000149011612f
#define TOOTH_DEPTH 0.2

static void
gear_endpoint(
	long x, long y, double angle, float radius, long *outx, long *outy)
{
	*outx = (long)((long double)cos(angle) * radius + x);

	*outy = (long)((long double)sin(angle) * radius * GEAR_ASPECT + y);
}

enum mh_error_code
mh_menu_gear(long x, long y, unsigned int teeth, float radius, float angle,
	unsigned int brightness, struct mh_menu_line *lines,
	unsigned int capacity, unsigned int *count, struct mh_error *error_out)
{
	struct mh_menu_line result[GEAR_LINE_LIMIT];
	float angle_step, angle_inset, tooth_depth, start_angle, end_angle,
		edge_radius;
	unsigned int i, line_count;
	struct mh_menu_line *line;
	double scaled_x, scaled_y;

	if (lines == NULL || count == NULL || teeth == 0 ||
		teeth > TEETH_LIMIT || capacity < teeth * LINES_PER_TOOTH ||
		brightness > 255 || x < -ORIGIN_LIMIT || x > ORIGIN_LIMIT ||
		y < -ORIGIN_LIMIT || y > ORIGIN_LIMIT ||
		!(radius >= -RADIUS_LIMIT && radius <= RADIUS_LIMIT) ||
		!(angle >= -ANGLE_LIMIT && angle <= ANGLE_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid menu gear");

	angle_step = (float)((long double)QUARTER_TURN / teeth);
	angle_inset = (float)((long double)angle_step * TOOTH_INSET);
	tooth_depth = (float)((long double)radius * TOOTH_DEPTH);
	line_count = 0;

	for (i = 0; i < teeth * 4; i++) {
		start_angle = (float)((long double)i * angle_step);
		end_angle = (float)((long double)(i + 1) * angle_step);

		if ((i & 3) == 0)
			start_angle =
				(float)((long double)start_angle + angle_inset);

		if ((i & 3) == 1)
			end_angle =
				(float)((long double)end_angle - angle_inset);

		start_angle = (float)((long double)start_angle + angle);
		end_angle = (float)((long double)end_angle + angle);
		edge_radius = (float)((long double)radius -
			((i / 2) & 1) * tooth_depth);
		line = &result[line_count++];
		line->brightness = brightness;

		/*
		 * The start point stores its products as binary64 before
		 * translation.
		 */
		scaled_x = (double)((long double)cos((double)start_angle) *
			edge_radius);
		scaled_y = (double)((long double)sin((double)start_angle) *
			edge_radius * GEAR_ASPECT);
		line->x1 = (long)((long double)scaled_x + x);
		line->y1 = (long)((long double)scaled_y + y);
		gear_endpoint(
			x, y, end_angle, edge_radius, &line->x2, &line->y2);

		if ((i & 3) == 2 || (i & 3) == 3) {
			line = &result[line_count++];
			line->brightness = brightness;

			if ((i & 3) == 2) {
				line->x1 = result[line_count - 2].x1;
				line->y1 = result[line_count - 2].y1;
				gear_endpoint(x, y,
					(double)start_angle - angle_inset,
					radius, &line->x2, &line->y2);
			} else {
				gear_endpoint(x, y, end_angle, edge_radius,
					&line->x1, &line->y1);
				gear_endpoint(x, y,
					(double)end_angle + angle_inset, radius,
					&line->x2, &line->y2);
			}
		}
	}

	memcpy(lines, result, line_count * sizeof(*lines));
	*count = line_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
