#include <stddef.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_lines.h"

#define CLIP_LIMIT 16384
#define COORDINATE_LIMIT 1000000000
#define LINE_COUNT_LIMIT 65536
#define FRACTION_UNIT 65536UL

static void
swap_coordinate(long *a, long *b)
{
	long value;

	value = *a;
	*a = *b;
	*b = value;
}

static int
clip_line(
	struct mh_menu_line *line, long left, long top, long right, long bottom)
{
	long *x1, *x2, *y1, *y2;

	x1 = &line->x1;
	y1 = &line->y1;
	x2 = &line->x2;
	y2 = &line->y2;

	if (*x1 > *x2) {
		swap_coordinate(x1, x2);
		swap_coordinate(y1, y2);
	}

	if (*x2 < left || *x1 > right)
		return 0;

	if (*x1 < left) {
		*y1 = *y2 -
			(long)((long double)(*x2 - left) * (*y2 - *y1) /
				(*x2 - *x1));
		*x1 = left;
	}

	if (*x2 > right) {
		*y2 -= (long)((long double)(*x2 - right) * (*y2 - *y1) /
			(*x2 - *x1));
		*x2 = right;
	}

	if (*y1 > *y2) {
		swap_coordinate(x1, x2);
		swap_coordinate(y1, y2);
	}

	if (*y2 < top || *y1 > bottom)
		return 0;

	if (*y1 < top) {
		*x1 = *x2 -
			(long)((long double)(*y2 - top) * (*x2 - *x1) /
				(*y2 - *y1));
		*y1 = top;
	}

	if (*y2 > bottom) {
		*x2 -= (long)((long double)(*y2 - bottom) * (*x2 - *x1) /
			(*y2 - *y1));
		*y2 = bottom;
	}

	return *x1 >= left && *x1 <= right && *x2 >= left && *x2 <= right &&
		*y1 >= top && *y1 <= bottom && *y2 >= top && *y2 <= bottom;
}

enum mh_error_code
mh_menu_lines(const struct mh_menu_line *lines, unsigned int count,
	unsigned int width, unsigned int height, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	if (width == 0 || height == 0 || width > CLIP_LIMIT ||
		height > CLIP_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu line canvas");

	return mh_menu_lines_rect(lines, count, 0, 0, (long)width - 1,
		(long)height - 1, canvas, error_out);
}

static int
pixel_address(
	const struct mh_menu_canvas *canvas, long x, long y, size_t *address)
{
	long distance;

	distance = y * (long)canvas->pitch + x;

	if (distance < 0) {
		if ((unsigned long)(-distance) > canvas->begin)
			return 0;

		*address = canvas->begin - (size_t)(-distance);
	} else {
		if ((unsigned long)distance >= canvas->size - canvas->begin)
			return 0;

		*address = canvas->begin + (size_t)distance;
	}

	return *address < canvas->size;
}

static enum mh_error_code
draw_clipped_line(const struct mh_menu_line *line,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	long dx, dy, x, y, sx, sy, major, minor, step;
	unsigned int value;
	unsigned long fraction, increment;
	size_t address;

	dx = line->x2 - line->x1;
	dy = line->y2 - line->y1;
	sx = dx < 0 ? -1 : 1;
	sy = dy < 0 ? -1 : 1;

	if (dx < 0)
		dx = -dx;

	if (dy < 0)
		dy = -dy;

	major = dx > dy ? dx : dy;
	minor = dx > dy ? dy : dx;
	increment = major > 0
		? (unsigned long)minor * FRACTION_UNIT / (unsigned long)major
		: 0;
	fraction = 0;
	x = line->x1;
	y = line->y1;

	for (step = 0; step < (major == 0 ? 1 : major); step++) {
		/*
		 * The rectangle corners were checked before any
		 * mutation.
		 */
		if (!pixel_address(canvas, x, y, &address))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid clipped menu line");

		value = canvas->pixels[address] + line->brightness;
		canvas->pixels[address] =
			(unsigned char)(value > 255 ? 255 : value);
		fraction += increment;

		if (dx >= dy) {
			x += sx;

			if (fraction >= FRACTION_UNIT)
				y += sy;
		} else {
			y += sy;

			if (fraction >= FRACTION_UNIT)
				x += sx;
		}

		fraction &= (unsigned long)MH_U16_MAX;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_lines_rect(const struct mh_menu_line *lines, unsigned int count,
	long left, long top, long right, long bottom,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_line line;
	enum mh_error_code status;
	unsigned int i;
	size_t address;

	if (lines == NULL || count > LINE_COUNT_LIMIT || canvas == NULL ||
		canvas->pixels == NULL || left < -CLIP_LIMIT ||
		right > CLIP_LIMIT || left > right || top < -CLIP_LIMIT ||
		bottom > CLIP_LIMIT || top > bottom ||
		canvas->pitch < (unsigned long)(right - left + 1) ||
		canvas->pitch > CLIP_LIMIT || canvas->begin > canvas->size ||
		!pixel_address(canvas, left, top, &address) ||
		!pixel_address(canvas, right, bottom, &address))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu line canvas");

	for (i = 0; i < count; i++) {
		line = lines[i];

		if (line.x1 < -COORDINATE_LIMIT || line.x1 > COORDINATE_LIMIT ||
			line.x2 < -COORDINATE_LIMIT ||
			line.x2 > COORDINATE_LIMIT ||
			line.y1 < -COORDINATE_LIMIT ||
			line.y1 > COORDINATE_LIMIT ||
			line.y2 < -COORDINATE_LIMIT ||
			line.y2 > COORDINATE_LIMIT || line.brightness > 255)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu line");
	}

	for (i = 0; i < count; i++) {
		line = lines[i];

		if (!clip_line(&line, left, top, right, bottom))
			continue;

		status = draw_clipped_line(&line, canvas, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
