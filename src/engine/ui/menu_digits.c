#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_digits.h"
#include "engine/ui/menu_sprite.h"

#define DIGIT_SLOT_MASK (MH_MENU_DIGIT_SLOTS - 1)
#define DIGIT_COUNT_LIMIT 10
#define DIGIT_SPACING 24
#define STRIP_ROWS 21
#define NUMA_WIDTH 19
#define NUMA_HEIGHT 220
#define NUMA_X_OFFSET 6
#define NUMA_ROW_STEP 20
#define NUMA_ROW_PERIOD 200
#define NUMB_WIDTH 6
#define NUMB_HEIGHT 40
#define NUMB_ROW_STEP (-30)
#define NUMB_ROW_PERIOD 20
#define ORIGIN_LIMIT 65536
#define ORIGIN_X_MAX 65280
#define POSITION_LIMIT 1000000
#define ANIMATION_RATE_SCALE 0.20000000298023224f
#define ANIMATION_TIME_SCALE 0.02857142873108387f
#define SNAP_DISTANCE 0.1

static unsigned long
wrap_u32(long double value)
{
	/*
	 * Truncate to signed 64 bits, then consume the low 32 bits for the
	 * unsigned strip-row division.
	 */
	if (value < 0)
		return (0UL - (unsigned long)-value) & MH_U32_MAX;

	return ((unsigned long)value) & MH_U32_MAX;
}

static enum mh_error_code
draw_strip(const struct mh_sprite_indexed *image, long x, long y,
	unsigned int first, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct mh_menu_sprite_command command;
	struct mh_menu_sprite_row rows[STRIP_ROWS];
	unsigned int i;

	if (image == NULL || image->pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(&command, 0, sizeof(command));
	command.kind = MH_MENU_SPRITE_CENTER;
	command.x = x;
	command.y = y;

	for (i = 0; i < STRIP_ROWS; i++) {
		rows[i].offset = (long)i * canvas->pitch;
		rows[i].source = first + i;
	}

	return mh_menu_sprite_blit(
		image, &command, rows, STRIP_ROWS, canvas, error_out);
}

static enum mh_error_code
draw_digit(const struct mh_sprite_indexed *numa,
	const struct mh_sprite_indexed *numb, long double position, long x,
	long y, struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = draw_strip(numa, x + NUMA_X_OFFSET, y,
		(unsigned int)(wrap_u32(position * NUMA_ROW_STEP) %
			NUMA_ROW_PERIOD),
		canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return draw_strip(numb, x, y,
		(unsigned int)(wrap_u32(position * NUMB_ROW_STEP) %
			NUMB_ROW_PERIOD),
		canvas, error_out);
}

enum mh_error_code
mh_menu_digits_draw(const struct mh_sprite_indexed *numa,
	const struct mh_sprite_indexed *numb, unsigned long value,
	unsigned int count, long x, long y, unsigned long elapsed_ms,
	unsigned int snap_digits, struct mh_menu_digits *state,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_digits next;
	struct mh_menu_canvas work;
	enum mh_error_code status;
	float position, delta, rate;
	long double advanced;
	unsigned int i, digit;
	long digit_x;

	if (state == NULL || canvas == NULL || canvas->pixels == NULL ||
		state->cursor > MH_MENU_DIGIT_SLOTS || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		canvas->begin >= canvas->end || canvas->end > canvas->size ||
		canvas->pitch == 0 ||
		canvas->pitch > MH_MENU_CANVAS_PITCH_LIMIT ||
		(canvas->end - canvas->begin) % canvas->pitch != 0 ||
		count > DIGIT_COUNT_LIMIT || value > MH_U32_MAX ||
		elapsed_ms > MH_U32_MAX || x < -ORIGIN_LIMIT ||
		x > ORIGIN_X_MAX || y < -ORIGIN_LIMIT || y > ORIGIN_LIMIT ||
		(numa != NULL && numa->pixels != NULL &&
			(numa->width != NUMA_WIDTH ||
				numa->height != NUMA_HEIGHT)) ||
		(numb != NULL && numb->pixels != NULL &&
			(numb->width != NUMB_WIDTH ||
				numb->height != NUMB_HEIGHT)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid animated menu digits");

	for (i = 0; i < MH_MENU_DIGIT_SLOTS; i++)
		if (!(state->position[i] >= -POSITION_LIMIT &&
			state->position[i] <= POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu digit position");

	next = *state;
	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (work.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory drawing menu digits");

	memcpy(work.pixels, canvas->pixels, work.size);
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	rate = (float)((long double)elapsed_ms * ANIMATION_RATE_SCALE *
		ANIMATION_TIME_SCALE);

	for (i = 0; i < count && status == MH_ERROR_NONE; i++) {
		next.cursor &= DIGIT_SLOT_MASK;
		position = next.position[next.cursor];
		digit = (unsigned int)(value % 10);

		if (position >= 10)
			position = (float)((long double)position - 10);

		if (position < 0)
			position = (float)((long double)position + 10);

		if (snap_digits == 1)
			position = (float)digit;

		delta = (float)((long double)digit - position);

		if (delta > 5)
			delta = (float)((long double)delta - 10);

		if (delta < -5)
			delta = (float)((long double)delta + 10);

		advanced = (long double)delta * rate + position;

		if (!(advanced >= -POSITION_LIMIT &&
			advanced <= POSITION_LIMIT)) {
			status = mh_error_set(error_out, MH_ERROR_RANGE,
				"menu digit animation overflow");
			break;
		}

		next.position[next.cursor] = (float)advanced;
		digit_x = x + (long)(count - 1 - i) * DIGIT_SPACING;
		status = draw_digit(
			numa, numb, advanced, digit_x, y, &work, error_out);

		if (fabs(delta) < SNAP_DISTANCE)
			next.position[next.cursor] = (float)digit;

		next.cursor++;
		value /= 10;
	}

	if (status == MH_ERROR_NONE) {
		*state = next;
		memcpy(canvas->pixels, work.pixels, work.size);
	}

	free(work.pixels);

	return status;
}
