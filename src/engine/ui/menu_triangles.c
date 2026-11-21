#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_triangles.h"

#define EDGE_LIMIT 1073741824.0f
#define TRIANGLE_LIMIT 32
#define COORDINATE_LIMIT 32768

static int
bounded(float value, float limit)
{
	return value >= -limit && value <= limit;
}

static void
fill_span(struct mh_menu_canvas *canvas, long y, long left, long right,
	unsigned int color)
{
	double start, end;

	if (right < left)
		return;

	/*
	 * Double represents these bounded integer addresses exactly on ILP32
	 * too. Clip before conversion to size_t; negative origins never form
	 * pointers.
	 */
	start = (double)canvas->begin + (double)y * canvas->pitch + left;
	end = (double)canvas->begin + (double)y * canvas->pitch + right + 1;

	if (start < 0)
		start = 0;

	if (end > (double)canvas->size)
		end = (double)canvas->size;

	if (start < end)
		memset(canvas->pixels + (size_t)start, (int)color,
			(size_t)(end - start));
}

static int
walk_triangle(const struct mh_menu_point *points, unsigned int color,
	int no_subpixel, struct mh_menu_canvas *canvas, int write_pixels)
{
	const struct mh_menu_point *a, *b, *c, *swap;
	float dy_ac, dy_ab, dy_bc, dx_ab, dx_ac, dx_bc, slope_ab, slope_ac,
		slope_bc, subpixel_a, subpixel_b;
	float adjust_ab, adjust_ac, adjust_bc, short_edge, long_edge;
	long first, middle, last, row, left, right;
	int short_left;

	a = points;
	b = points + 1;
	c = points + 2;

	if (c->y < b->y) {
		swap = b;
		b = c;
		c = swap;
	}

	if (b->y < a->y) {
		swap = a;
		a = b;
		b = swap;
	}

	if (c->y < b->y) {
		swap = b;
		b = c;
		c = swap;
	}

	if (a->y == b->y && a->x > b->x) {
		swap = a;
		a = b;
		b = swap;
	}

	dy_ac = (float)((long double)c->y - a->y);

	if (dy_ac == 0)
		return 1;

	dy_ab = (float)((long double)b->y - a->y);
	dy_bc = (float)((long double)c->y - b->y);
	first = (long)a->y + 1;
	middle = (long)b->y + 1;
	last = (long)c->y + 1;
	dx_ab = (float)((long double)b->x - a->x);
	dx_ac = (float)((long double)c->x - a->x);
	dx_bc = (float)((long double)c->x - b->x);
	slope_ab = dy_ab > 0 ? (float)((long double)dx_ab / dy_ab) : 0;
	slope_ac = (float)((long double)dx_ac / dy_ac);
	slope_bc = dy_bc > 0 ? (float)((long double)dx_bc / dy_bc) : 0;
	subpixel_a = no_subpixel ? 0 : (float)((long double)a->y - first);
	subpixel_b = no_subpixel ? 0 : (float)((long double)b->y - middle);
	short_left = ((long double)b->y - a->y) * slope_ac + a->x > b->x;
	adjust_ab = (float)((long double)subpixel_a * slope_ab);
	adjust_ac = (float)((long double)subpixel_a * slope_ac);

	/*
	 * The original flat-top branch uses the BC slope with A's subpixel
	 * correction, even though this initializes the long AC edge.
	 */
	adjust_bc = (float)((long double)subpixel_a * slope_bc);
	short_edge = (float)((long double)a->x - adjust_ab);
	long_edge = (float)((long double)a->x -
		(middle > first ? adjust_ac : adjust_bc));

	for (row = first; row < last; row++) {
		if (row == middle)
			short_edge = (float)((long double)b->x -
				(long double)subpixel_b * slope_bc);

		if (!bounded(short_edge, EDGE_LIMIT) ||
			!bounded(long_edge, EDGE_LIMIT))
			return 0;

		left = (long)(short_left ? short_edge : long_edge);
		right = (long)(short_left ? long_edge : short_edge);

		if (write_pixels)
			fill_span(canvas, row, left, right, color);

		short_edge = (float)((long double)short_edge +
			(row < middle ? slope_ab : slope_bc));
		long_edge = (float)((long double)long_edge + slope_ac);
	}

	return 1;
}

enum mh_error_code
mh_menu_triangles(const struct mh_menu_point *points, unsigned int count,
	unsigned int color, int no_subpixel, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	unsigned int i;

	if (points == NULL || count > TRIANGLE_LIMIT || color > 255 ||
		canvas == NULL || canvas->pixels == NULL ||
		canvas->size > (unsigned long)MH_I32_MAX ||
		canvas->begin > canvas->end || canvas->end > canvas->size ||
		canvas->pitch == 0 ||
		canvas->pitch > MH_MENU_CANVAS_PITCH_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu triangle canvas");

	for (i = 0; i < count * 3; i++)
		if (!bounded(points[i].x, COORDINATE_LIMIT) ||
			!bounded(points[i].y, COORDINATE_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu triangle vertex");

	/*
	 * Validate the complete edge walks before publishing the first
	 * triangle. Tiny positive dy can otherwise create
	 * nonfinite/out-of-range edges.
	 */
	for (i = 0; i < count; i++)
		if (!walk_triangle(
			points + i * 3, color, no_subpixel, canvas, 0))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu triangle edge overflow");

	for (i = 0; i < count; i++)
		walk_triangle(points + i * 3, color, no_subpixel, canvas, 1);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
