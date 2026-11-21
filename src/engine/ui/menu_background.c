#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/ui/menu_background.h"
#include "engine/ui/menu_gear.h"
#include "interfaces/ui/layout.h"

#define FLASH_WIDTH 640
#define FLASH_HEIGHT 400
#define LIGHTNING_COORD_LIMIT 32768
#define LIGHTNING_DEPTH_LIMIT 32
#define LIGHTNING_JITTER 3.9999998989515007e-05f
#define POSITION_LIMIT 1000000
#define OBJECT_SPEED_LIMIT 64
#define RESPAWN_DEPTH 200
#define GEAR_ANGULAR_SPEED 0.0010000000474974513f

enum mh_error_code
mh_menu_background_reset(struct mh_menu_background *state, unsigned int count,
	struct mh_error *error_out)
{
	unsigned int i;

	if (state == NULL || count > MH_MENU_BACKGROUND_OBJECTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu background count");

	state->count = count;

	for (i = 0; i < count; i++) {
		state->objects[i].position[2] = 0;
		state->objects[i].timer = 0;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_background_flash(struct mh_menu_background *state,
	unsigned long *random, struct mh_error *error_out)
{
	unsigned long seed, frames;
	long points[4];

	if (state == NULL || random == NULL || *random > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu lightning seed");

	seed = *random;
	frames = (mh_random_next(&seed) & 7) + 3;

	if (mh_random_next(&seed) & 1) {
		points[0] = (long)(mh_random_next(&seed) % FLASH_WIDTH);
		points[2] = (long)(mh_random_next(&seed) % FLASH_WIDTH);
		points[1] = 0;
		points[3] = FLASH_HEIGHT;
	} else {
		points[0] = 0;
		points[2] = FLASH_WIDTH;
		points[1] = (long)(mh_random_next(&seed) % FLASH_HEIGHT);
		points[3] = (long)(mh_random_next(&seed) % FLASH_HEIGHT);
	}

	*random = seed;
	state->lightning_frames = frames;
	memcpy(state->lightning, points, sizeof(points));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_lightning(float x1, float y1, float x2, float y2, unsigned long *random,
	struct mh_menu_background_draw *draw, unsigned int depth,
	struct mh_error *error_out)
{
	float dx, dy, factor, scaled_x, scaled_y, mid_x, mid_y;
	long double wide_factor;
	struct mh_menu_line *line;
	enum mh_error_code status;

	if (depth > LIGHTNING_DEPTH_LIMIT ||
		draw->line_count >= MH_MENU_BACKGROUND_LINES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"menu lightning capacity exceeded");

	dx = (float)((long double)x2 - x1);
	dy = (float)((long double)y2 - y1);

	/* Signed maximum, deliberately not absolute length. */
	if ((long double)(dx > dy ? dx : dy) * 0.5 < 16) {
		if (!(x1 >= -LIGHTNING_COORD_LIMIT &&
			x1 <= LIGHTNING_COORD_LIMIT &&
			y1 >= -LIGHTNING_COORD_LIMIT &&
			y1 <= LIGHTNING_COORD_LIMIT &&
			x2 >= -LIGHTNING_COORD_LIMIT &&
			x2 <= LIGHTNING_COORD_LIMIT &&
			y2 >= -LIGHTNING_COORD_LIMIT &&
			y2 <= LIGHTNING_COORD_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu lightning capacity exceeded");

		line = &draw->lines[draw->line_count++];
		line->x1 = (long)x1;
		line->y1 = (long)y1;
		line->x2 = (long)x2;
		line->y2 = (long)y2;
		line->brightness = 16;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	wide_factor = ((long double)(mh_random_next(random) & 4095) - 2048.0f) *
		LIGHTNING_JITTER;
	factor = (float)wide_factor;
	scaled_x = (float)(wide_factor * dx);
	scaled_y = (float)((long double)factor * dy);
	mid_x = (float)(((long double)x1 + x2) * 0.5 + scaled_y);
	mid_y = (float)(((long double)y1 + y2) * 0.5 - scaled_x);

	status = append_lightning(
		x1, y1, mid_x, mid_y, random, draw, depth + 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return append_lightning(
		mid_x, mid_y, x2, y2, random, draw, depth + 1, error_out);
}

static void
respawn_object(struct mh_menu_background_object *object, unsigned long *random)
{
	float correction;
	unsigned int j;

	object->model = mh_random_next(random) % MH_MENU_BACKGROUND_MODELS;
	object->speed =
		(float)((long double)((mh_random_next(random) & 8191) + 512) *
			0.00048828125f);
	object->position[0] =
		(float)((long)(mh_random_next(random) & 2047) - 1024);
	object->position[1] =
		(float)((long)(mh_random_next(random) & 2047) - 1024);

	if (fabs(object->position[0]) < 260 &&
		fabs(object->position[1]) < 260) {
		correction = mh_random_next(random) & 1 ? 300.0f : -300.0f;
		j = (unsigned int)(mh_random_next(random) & 1);
		object->position[j] = correction;
	}

	object->position[2] = (float)((mh_random_next(random) & 2047) + 1024);

	for (j = 0; j < 3; j++)
		object->angle[j] = mh_random_next(random);

	object->timer = (mh_random_next(random) & 1023) + 200;
}

static enum mh_error_code
build_gears(unsigned long clock_ms, struct mh_menu_background_draw *draw,
	struct mh_error *error_out)
{
	float angle;
	unsigned int count;
	enum mh_error_code status;

	angle = (float)((long double)clock_ms * GEAR_ANGULAR_SPEED);
	status = mh_menu_gear(100, 150, 6, 200, angle, 16, draw->lines,
		MH_MENU_BACKGROUND_LINES, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	draw->line_count = count;
	status = mh_menu_gear(470, 150, 6, 200, -angle, 16, draw->lines + count,
		MH_MENU_BACKGROUND_LINES - count, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	draw->line_count += count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
advance_objects(struct mh_menu_background *state, unsigned long clock_ms,
	unsigned long elapsed_ms, unsigned long *random,
	struct mh_menu_background_draw *draw)
{
	struct mh_menu_background_object *object;
	struct mh_menu_background_model *model;
	float brightness;
	unsigned int i;

	for (i = 0; i < state->count; i++) {
		object = &state->objects[i];
		object->position[2] = (float)((long double)object->position[2] -
			(long double)elapsed_ms * object->speed);

		if (object->position[2] <= RESPAWN_DEPTH) {
			respawn_object(object, random);
		} else
			object->timer =
				(object->timer - elapsed_ms) & MH_U32_MAX;

		brightness = (float)(32.0 -
			(long double)object->position[2] * 0.015625f);

		if (brightness < 2)
			brightness = 2;

		model = &draw->models[draw->model_count++];
		model->brightness = (unsigned int)brightness;
		model->model = (unsigned int)object->model;
		memcpy(model->position, object->position,
			sizeof(model->position));
		model->angle[0] = (clock_ms + object->angle[0]) & MH_U32_MAX;
		model->angle[1] =
			(clock_ms / 7 + object->angle[1]) & MH_U32_MAX;
		model->angle[2] =
			(clock_ms / 3 + object->angle[2]) & MH_U32_MAX;
	}
}

enum mh_error_code
mh_menu_background_step(struct mh_menu_background *state, unsigned int layout,
	unsigned long clock_ms, unsigned long elapsed_ms, unsigned long *random,
	struct mh_menu_background_draw *output, struct mh_error *error_out)
{
	struct mh_menu_background next;
	struct mh_menu_background_draw result;
	struct mh_menu_background_object *object;
	unsigned long seed;
	unsigned int i, j;
	enum mh_error_code status;

	if (state == NULL || output == NULL || random == NULL ||
		*random > MH_U32_MAX ||
		state->count > MH_MENU_BACKGROUND_OBJECTS ||
		state->lightning_frames > MH_U32_MAX || clock_ms > MH_U32_MAX ||
		elapsed_ms > MH_U32_MAX || layout >= MH_MENU_LAYOUT_PAGES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu background request");

	memset(&result, 0, sizeof(result));

	if (layout == 2 || layout == 3) {
		*output = result;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < 4; i++)
		if (state->lightning[i] < -LIGHTNING_COORD_LIMIT ||
			state->lightning[i] > LIGHTNING_COORD_LIMIT)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu lightning endpoints");

	for (i = 0; i < state->count; i++) {
		object = &state->objects[i];

		if (!(object->speed >= 0 &&
			object->speed <= OBJECT_SPEED_LIMIT) ||
			object->model >= MH_MENU_BACKGROUND_MODELS ||
			object->timer > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu background object");

		for (j = 0; j < 3; j++)
			if (!(object->position[j] >= -POSITION_LIMIT &&
				object->position[j] <= POSITION_LIMIT) ||
				object->angle[j] > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid menu background pose");
	}

	next = *state;
	seed = *random;
	status = build_gears(clock_ms, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (next.lightning_frames != 0) {
		for (i = 0; i < 3; i++) {
			status = append_lightning((float)next.lightning[0],
				(float)next.lightning[1],
				(float)next.lightning[2],
				(float)next.lightning[3], &seed, &result, 0,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		next.lightning_frames--;
	}

	advance_objects(&next, clock_ms, elapsed_ms, &seed, &result);

	*state = next;
	*random = seed;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
