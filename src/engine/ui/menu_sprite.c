#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/random.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_sine.h"
#include "engine/ui/menu_sprite.h"

#define PARAMETER_LIMIT 1048576
#define IMAGE_SIZE_LIMIT 4096
#define ROW_COUNT_LIMIT 65536
#define TRANSLATION_LIMIT 32768
#define FRAME_INDEX_MAX 255
#define SPRITE_ORIGIN_LIMIT 65536

#define ROW_SCALE_ONE 1024
#define LAYOUT_SCALE_PER_MIX_UNIT (1.0 / (1000 * MH_MENU_POSITION_MIX_MAX))
#define IDLE_EFFECT_THRESHOLD 10
#define IDLE_JITTER_TICKS 20
#define IDLE_JITTER_DRAW_MASK 2047U
#define IDLE_JITTER_AMPLITUDE_MASK 63U
#define WOBBLE_TIMER_SLOT 2
#define WOBBLE_PHASE_STEP 256
#define JITTER_PHASE_CENTER 4096
#define JITTER_PHASE_MASK (2UL * JITTER_PHASE_CENTER - 1)
#define JITTER_UNITS_PER_PIXEL 65536L
#define JITTER_FREQUENCY_STEP 1737

enum {
	LAYOUT_JITTER_FIRST = 1,
	LAYOUT_JITTER_REVERSE = 2,
	LAYOUT_JITTER_LAST = 3,
	LAYOUT_PHASE = 4,
	LAYOUT_DOUBLE_PHASE = 5,
	LAYOUT_EXPAND = 6,
	LAYOUT_SPREAD = 7,
	LAYOUT_CENTER = 8
};

unsigned int
mh_menu_sprite_label_frame(unsigned long clock_ms)
{
	unsigned long phase;

	/* Wrap the clock product to 32 bits before extracting the phase. */
	phase = (clock_ms * 32) & MH_U32_MAX;

	return (unsigned int)((long double)mh_menu_sine(
				(unsigned int)(phase >> 2)) *
			3.5 +
		3.5);
}

static long
floor_divide(long value, long divisor)
{
	return value >= 0 ? value / divisor : -1 - (-(value + 1) / divisor);
}

static long
wrap_i32(unsigned long value)
{
	value &= MH_U32_MAX;

	return value <= (unsigned long)MH_I32_MAX
		? (long)value
		: -1L - (long)(MH_U32_MAX - value);
}

enum mh_error_code
mh_menu_sprite_select(const struct mh_menu_layout *layout, unsigned int page,
	unsigned int slot, unsigned int mix, long dx, long dy,
	unsigned int frame, struct mh_menu_sprite_state *state,
	unsigned long *random, struct mh_menu_sprite_output *output,
	struct mh_error *error_out)
{
	struct mh_menu_sprite_output result;
	struct mh_menu_sprite_command *command;
	const long *record;
	enum mh_error_code status;
	unsigned long seed;
	long timer, amplitude, effect;
	unsigned int i;
	float factor;

	if (state == NULL || random == NULL || output == NULL ||
		*random > MH_U32_MAX || dx < -TRANSLATION_LIMIT ||
		dx > TRANSLATION_LIMIT || dy < -TRANSLATION_LIMIT ||
		dy > TRANSLATION_LIMIT || frame > FRAME_INDEX_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu sprite request");

	for (i = 0; i < MH_MENU_LAYOUT_SLOTS; i++)
		if (state->timer[i] < MH_I32_MIN ||
			state->timer[i] > MH_I32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu sprite timer");

	memset(&result, 0, sizeof(result));
	command = &result.commands[0];
	status = mh_menu_position(
		layout, page, slot, mix, &command->x, &command->y, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	record = layout->words[page][slot];
	amplitude = record[MH_MENU_LAYOUT_PARAMETER];

	if (amplitude < -PARAMETER_LIMIT || amplitude > PARAMETER_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu effect parameter");

	command->x += dx;
	command->y += dy;
	command->slot = slot;
	command->frame = frame;
	timer = state->timer[slot];
	seed = *random;
	result.count = 1;
	effect = record[MH_MENU_LAYOUT_EFFECT];

	if (mix == 0) {
		if (mh_random_next(&seed) < IDLE_EFFECT_THRESHOLD ||
			timer > IDLE_JITTER_TICKS) {
			if (state->wobble[slot]) {
				command->kind = MH_MENU_SPRITE_PHASE;
				command->parameter = wrap_i32(
					(unsigned long)state
						->timer[WOBBLE_TIMER_SLOT] +
					MH_MENU_SINE_QUARTER_TURN);
				timer = timer > 0 ? timer - WOBBLE_PHASE_STEP
						: MH_MENU_SINE_FULL_TURN;
			}
		} else if ((mh_random_next(&seed) & IDLE_JITTER_DRAW_MASK) <
				IDLE_EFFECT_THRESHOLD ||
			timer > 0) {
			command->kind = MH_MENU_SPRITE_JITTER;
			command->parameter =
				(mh_random_next(&seed) &
					IDLE_JITTER_AMPLITUDE_MASK) +
				1;
			command->offset = JITTER_PHASE_CENTER;
			timer = timer > 0 ? timer - 1
					: (long)(mh_random_next(&seed) %
						IDLE_JITTER_TICKS);
		}
	} else if (effect >= LAYOUT_JITTER_FIRST &&
		effect <= LAYOUT_JITTER_LAST) {
		command->kind = MH_MENU_SPRITE_JITTER;
		amplitude = floor_divide(
			amplitude * (long)mix, MH_MENU_POSITION_MIX_MAX);

		if (amplitude < 1)
			amplitude = 1;

		command->parameter = (long)mh_random_next(&seed) % amplitude;
		command->offset = effect == LAYOUT_JITTER_REVERSE
			? -JITTER_PHASE_CENTER
			: JITTER_PHASE_CENTER;
	} else if (effect == LAYOUT_PHASE || effect == LAYOUT_DOUBLE_PHASE) {
		command->kind = MH_MENU_SPRITE_PHASE;
		command->parameter = MH_MENU_SINE_QUARTER_TURN +
			(long)mix * (effect == LAYOUT_PHASE ? 4 : 12);

		if (effect == LAYOUT_DOUBLE_PHASE) {
			result.commands[1] = *command;
			result.count = 2;
		}
	} else if (effect >= LAYOUT_EXPAND && effect <= LAYOUT_CENTER) {
		factor = (float)((double)mix * (float)amplitude *
			(double)LAYOUT_SCALE_PER_MIX_UNIT);
		command->factor = (float)(1.0 + (double)factor);

		if (effect == LAYOUT_EXPAND)
			command->kind = MH_MENU_SPRITE_EXPAND;
		else if (effect == LAYOUT_SPREAD)
			command->kind = MH_MENU_SPRITE_SPREAD;
		else
			command->kind = MH_MENU_SPRITE_CENTER;
	} else if (effect != 0)
		result.count = 0;

	state->timer[slot] = timer;
	*random = seed;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_sprite_rows(const struct mh_menu_sprite_command *command,
	unsigned int height, unsigned int pitch, unsigned long *noise,
	struct mh_menu_sprite_row *rows, unsigned int capacity,
	unsigned int *count, struct mh_error *error_out)
{
	unsigned int i, total;
	unsigned long phase, frequency;
	long extent, factor, offset, row;
	float scale;

	if (command == NULL || noise == NULL || rows == NULL || count == NULL ||
		height < 2 || height > IMAGE_SIZE_LIMIT || pitch == 0 ||
		pitch > MH_MENU_CANVAS_PITCH_LIMIT || *noise > MH_U32_MAX ||
		command->kind > MH_MENU_SPRITE_CENTER ||
		command->slot >= MH_MENU_LAYOUT_SLOTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu sprite row request");

	scale = command->factor;
	total = height;
	extent = 0;

	if (command->kind == MH_MENU_SPRITE_PHASE)
		scale = mh_menu_sine((unsigned int)command->parameter);

	if (command->kind >= MH_MENU_SPRITE_EXPAND ||
		command->kind == MH_MENU_SPRITE_PHASE) {
		if (!(scale >= -64 && scale <= 64))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu sprite scale out of range");

		if (command->kind == MH_MENU_SPRITE_SPREAD && scale < 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"negative unsigned sprite spread");

		/* Shrinking an EXPAND sprite would require a second draw. */
		if (command->kind == MH_MENU_SPRITE_EXPAND && scale < 1)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"unsupported menu sprite expansion");

		factor = (long)((double)scale * ROW_SCALE_ONE);

		if (command->kind == MH_MENU_SPRITE_CENTER ||
			command->kind == MH_MENU_SPRITE_PHASE)
			factor = -factor;

		extent = floor_divide(
			factor * (long)(height - 1), ROW_SCALE_ONE);

		if ((extent >= 0 ? extent : -extent) + (long)height >
			MH_I32_MAX / (long)pitch)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"menu sprite offset out of range");

		if (command->kind == MH_MENU_SPRITE_EXPAND) {
			if (extent <= 0 || extent > ROW_COUNT_LIMIT)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"menu sprite row count out of range");

			total = (unsigned int)extent;
		}
	}

	if (total > capacity)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"short menu sprite row output");

	if (command->kind == MH_MENU_SPRITE_JITTER &&
		(command->parameter < -PARAMETER_LIMIT ||
			command->parameter > PARAMETER_LIMIT ||
			command->offset < -PARAMETER_LIMIT ||
			command->offset > PARAMETER_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"menu sprite jitter out of range");

	phase = *noise;
	frequency = command->slot;

	for (i = 0; i < total; i++) {
		row = (long)i;
		offset = (long)i * (long)pitch;

		if (command->kind == MH_MENU_SPRITE_JITTER) {
			phase = ((phase + i) * frequency) & MH_U32_MAX;

			/*
			 * Wrap the displacement to signed 32 bits before
			 * scaling.
			 */
			offset += floor_divide(
				wrap_i32(((phase & JITTER_PHASE_MASK) -
						(unsigned long)
							command->offset) *
					(unsigned long)command->parameter),
				JITTER_UNITS_PER_PIXEL);
			frequency += JITTER_FREQUENCY_STEP;
		} else if (command->kind == MH_MENU_SPRITE_SPREAD)
			offset = ((extent * (long)i) / (long)(height - 1)) *
				(long)pitch;
		else if (command->kind == MH_MENU_SPRITE_PHASE ||
			command->kind == MH_MENU_SPRITE_CENTER) {
			row = (long)height / 2 - (long)i;

			/*
			 * C89 leaves negative division rounding
			 * implementation-defined.
			 */
			factor = row * extent;
			row = factor >= 0 ? factor / (long)(height - 1)
					: -((-factor) / (long)(height - 1));

			offset = (row + (long)height / 2) * (long)pitch;
			row = (long)i;
		} else if (command->kind == MH_MENU_SPRITE_EXPAND)
			row = (long)(((unsigned long)i * height) /
				(unsigned long)total);

		rows[i].offset = offset;
		rows[i].source = (unsigned int)row;
	}

	*noise = phase;
	*count = total;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
copy_row(const struct mh_sprite_indexed *image, unsigned int source,
	double origin, struct mh_menu_canvas *canvas, int color)
{
	unsigned int x;
	const unsigned char *pixels;
	double address;

	pixels = image->pixels + (size_t)source * image->width * 2;

	for (x = 0; x < image->width; x++)
		if (pixels[x * 2 + 1] != 0) {
			if (color >= 0 && ((x + source) & 1) != 0)
				continue;

			address = origin + x;

			if (address >= 0 && address < (double)canvas->size)
				canvas->pixels[(size_t)address] = color < 0
					? pixels[x * 2]
					: (unsigned char)color;
		}
}

static void
blit_plain(const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_command *command,
	struct mh_menu_canvas *canvas, int color)
{
	unsigned int i;
	long start, amount, limit, height;
	double base;

	start = command->y < 0 ? -command->y : 0;
	amount = (long)image->height - start;

	if (amount <= 0)
		return;

	height = (long)((canvas->end - canvas->begin) / canvas->pitch);
	limit = height - amount - command->y;

	if (limit < 0)
		amount += limit;

	if (amount <= 0)
		return;

	base = (double)canvas->begin + command->x +
		(double)(command->y < 0 ? 0 : command->y) * canvas->pitch;

	for (i = 0; i < (unsigned long)amount; i++)
		copy_row(image, (unsigned int)start + i,
			base + (double)i * canvas->pitch, canvas, color);
}

static void
blit_transformed(const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_command *command,
	const struct mh_menu_sprite_row *rows, unsigned int count,
	struct mh_menu_canvas *canvas, int color)
{
	unsigned int i;
	double base, origin;

	base = (double)canvas->begin + command->x +
		(double)command->y * canvas->pitch;

	for (i = 0; i < count; i++) {
		origin = base + rows[i].offset;

		if (origin >= (double)canvas->end)
			break;

		if (origin >= (double)canvas->begin)
			copy_row(image, rows[i].source, origin, canvas, color);
	}
}

static enum mh_error_code
blit(const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_command *command,
	const struct mh_menu_sprite_row *rows, unsigned int count,
	struct mh_menu_canvas *canvas, int color, struct mh_error *error_out)
{
	unsigned int i;

	if (image == NULL || image->pixels == NULL || command == NULL ||
		rows == NULL || canvas == NULL || canvas->pixels == NULL ||
		image->width == 0 || image->width > IMAGE_SIZE_LIMIT ||
		image->height < 2 || image->height > IMAGE_SIZE_LIMIT ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		canvas->begin >= canvas->end || canvas->end > canvas->size ||
		canvas->pitch == 0 ||
		canvas->pitch > MH_MENU_CANVAS_PITCH_LIMIT ||
		(canvas->end - canvas->begin) % canvas->pitch != 0 ||
		count == 0 || count > ROW_COUNT_LIMIT ||
		command->kind > MH_MENU_SPRITE_CENTER ||
		command->x < -SPRITE_ORIGIN_LIMIT ||
		command->x > SPRITE_ORIGIN_LIMIT ||
		command->y < -SPRITE_ORIGIN_LIMIT ||
		command->y > SPRITE_ORIGIN_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu sprite canvas");

	for (i = 0; i < count; i++)
		if (rows[i].source >= image->height ||
			rows[i].offset < MH_I32_MIN ||
			rows[i].offset > MH_I32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid menu sprite row");

	if (command->kind == MH_MENU_SPRITE_PLAIN)
		blit_plain(image, command, canvas, color);
	else
		blit_transformed(image, command, rows, count, canvas, color);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_sprite_blit(const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_command *command,
	const struct mh_menu_sprite_row *rows, unsigned int count,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	return blit(image, command, rows, count, canvas, -1, error_out);
}

enum mh_error_code
mh_menu_sprite_blit_disabled(const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_command *command,
	const struct mh_menu_sprite_row *rows, unsigned int count,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	/* Host-disabled labels use muted blue stippling, not orange index 1. */
	return blit(image, command, rows, count, canvas, 3, error_out);
}

enum mh_error_code
mh_menu_sprite_draw_commands(const struct mh_sprite_indexed *image,
	const struct mh_menu_sprite_output *commands,
	struct mh_menu_sprite_row *rows, unsigned int capacity,
	unsigned long *noise, struct mh_menu_canvas *canvas, int disabled,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i, count = 0;

	if (image == NULL || commands == NULL || canvas == NULL ||
		commands->count > sizeof(commands->commands) /
				sizeof(commands->commands[0]))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu sprite commands");

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < commands->count && status == MH_ERROR_NONE; i++) {
		status = mh_menu_sprite_rows(&commands->commands[i],
			image->height, canvas->pitch, noise, rows, capacity,
			&count, error_out);

		if (status == MH_ERROR_NONE && disabled)
			status = mh_menu_sprite_blit_disabled(image,
				&commands->commands[i], rows, count, canvas,
				error_out);
		else if (status == MH_ERROR_NONE)
			status = mh_menu_sprite_blit(image,
				&commands->commands[i], rows, count, canvas,
				error_out);
	}

	return status;
}
