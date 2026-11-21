#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_main.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_triangles.h"
#include "interfaces/sprite/collection.h"

#define LAST_ART_SLOT (MH_MENU_MAIN_SLOTS - 1)
#define ROW_CAPACITY 65536
#define LABEL_SLOT 6
#define NEEDLE_TARGET_SLOT 24
#define NEEDLE_ORIGIN_SLOT 30
#define NEEDLE_RESPONSE 32
#define NEEDLE_LENGTH 2.5999999046325684f

static void
append_event(
	struct mh_menu_output *output, unsigned int kind, unsigned int value)
{
	output->events[output->count].kind = kind;

	output->events[output->count++].value = value;
}

enum mh_error_code
mh_menu_main_update(struct mh_menu_main_state *state,
	const struct mh_menu_input *input, struct mh_menu_output *output,
	const struct mh_menu_choices *choices, struct mh_error *error_out)
{
	struct mh_menu_output result;

	if (state == NULL || input == NULL || output == NULL ||
		state->selected > MH_MENU_MAIN_LAST_CHOICE ||
		state->exit_answer > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid main menu input");

	memset(&result, 0, sizeof(result));

	state->selected = mh_menu_choice_first(choices, state->selected);

	if (input->left == 1)
		append_event(&result, MH_MENU_SOUND,
			mh_menu_choice_navigate(choices, &state->selected,
				MH_MENU_MAIN_LAST_CHOICE + 1, 0));

	if (input->right == 1)
		append_event(&result, MH_MENU_SOUND,
			mh_menu_choice_navigate(choices, &state->selected,
				MH_MENU_MAIN_LAST_CHOICE + 1, 1));

	if (input->enter == 1 &&
		mh_menu_choice_state(choices, state->selected) !=
			MH_MENU_ENABLED)
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_LIMIT);

	if (input->enter == 1 &&
		mh_menu_choice_state(choices, state->selected) ==
			MH_MENU_ENABLED) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);
		append_event(&result, MH_MENU_PAGE,
			MH_MENU_SINGLE + state->selected);
	}

	if (input->escape == 1) {
		if (input->confirmation == 0)
			state->exit_answer = 1;
		else if (input->confirmation == 1) {
			append_event(
				&result, MH_MENU_DIALOG, MH_MENU_CONFIRM_EXIT);
			state->exit_answer = 0;
		} else
			append_event(
				&result, MH_MENU_DIALOG, MH_MENU_BLOCK_EXIT);
	}

	if (state->exit_answer == 1) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		append_event(&result, MH_MENU_PAGE, MH_MENU_EXIT);
		state->exit_answer = 0;
	}

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_menu_main_art_free(struct mh_menu_main_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_MAIN_SLOTS; i++)
		for (j = 0; j < MH_MENU_MAIN_FRAMES; j++)
			free(art->images[i][j].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_main_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_main_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_MAIN_SLOTS] = {NULL, "dots",
		"scr0", "msel", NULL, "main", "tone", "tmul", "tran", "topt",
		"tcre"};

	struct mh_menu_main_art parsed;
	enum mh_error_code status;
	struct mh_menu_art_reader reader = {0, "invalid main menu sprite",
		"main menu artwork exceeds capacity"};

	unsigned int i;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid main menu archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		if (names[i] == NULL)
			continue;

		status = mh_menu_art_read(&reader, collection, names[i],
			parsed.images[i], MH_MENU_MAIN_FRAMES,
			MH_MENU_ART_ALL_FRAMES, &parsed.count[i], error_out);
	}

	if (status != MH_ERROR_NONE)
		mh_menu_main_art_free(&parsed);
	else
		*output = parsed;

	return status;
}

static enum mh_error_code
needle_target(const struct mh_menu_layout *layout, unsigned int selected,
	unsigned int mix, float *angle, struct mh_error *error_out)
{
	enum mh_error_code status;
	long x, y;

	if (selected > MH_MENU_MAIN_LAST_CHOICE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid main menu selection");

	status = mh_menu_position(layout, 0, selected + NEEDLE_TARGET_SLOT, mix,
		&x, &y, error_out);

	if (status == MH_ERROR_NONE)
		*angle = (float)(8 * x + y);

	return status;
}

enum mh_error_code
mh_menu_main_animation_init(const struct mh_menu_layout *layout,
	unsigned int selected, unsigned int mix,
	struct mh_menu_main_animation *output, struct mh_error *error_out)
{
	struct mh_menu_main_animation parsed;
	enum mh_error_code status;
	float angle = 0;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing main menu animation");

	status = needle_target(layout, selected, mix, &angle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&parsed, 0, sizeof(parsed));
	parsed.sprites.wobble[2] = 1;
	parsed.sprites.wobble[5] = 1;
	status = mh_menu_needle_init(
		&parsed.needle, angle, NEEDLE_RESPONSE, error_out);

	if (status == MH_ERROR_NONE)
		*output = parsed;

	return status;
}

enum mh_error_code
mh_menu_main_render(const struct mh_menu_main_art *art,
	const struct mh_menu_layout *layout, unsigned int selected,
	unsigned int mix, unsigned long clock_ms, unsigned long elapsed_ms,
	struct mh_menu_main_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	const struct mh_menu_choices *choices, struct mh_error *error_out)
{
	struct mh_menu_main_animation next;
	struct mh_menu_sprite_output commands;
	struct mh_menu_needle_draw needle;
	struct mh_menu_canvas work;
	struct mh_menu_sprite_row *rows;
	const struct mh_sprite_indexed *image;
	enum mh_error_code status;
	unsigned long seed, phase;
	unsigned int i, frame, animated;
	long x, y;
	float angle = 0;

	if (art == NULL || state == NULL || random == NULL || noise == NULL ||
		canvas == NULL || canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		*random > MH_U32_MAX || *noise > MH_U32_MAX ||
		clock_ms > MH_U32_MAX || elapsed_ms > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid main menu frame");

	status = needle_target(layout, selected, mix, &angle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_menu_position(
		layout, 0, NEEDLE_ORIGIN_SLOT, mix, &x, &y, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *state;
	seed = *random;
	phase = *noise;
	status = mh_menu_needle_step(&next.needle, (float)x, (float)y, angle,
		NEEDLE_LENGTH, 3, 1, elapsed_ms, &needle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	animated = mh_menu_sprite_label_frame(clock_ms);
	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);
	rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*rows));

	if (work.pixels == NULL || rows == NULL) {
		free(work.pixels);
		free(rows);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing main menu");
	}

	memcpy(work.pixels, canvas->pixels, work.size);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		frame = i == selected + LABEL_SLOT &&
				mh_menu_choice_state(choices, selected) ==
					MH_MENU_ENABLED
			? animated
			: 0;
		status = mh_menu_sprite_select(layout, 0, i, mix, 0, 0, frame,
			&next.sprites, &seed, &commands, error_out);

		if (i >= LABEL_SLOT &&
			i <= LABEL_SLOT + MH_MENU_MAIN_LAST_CHOICE &&
			mh_menu_choice_state(choices, i - LABEL_SLOT) ==
				MH_MENU_HIDDEN)
			continue;

		if (status != MH_ERROR_NONE || art->count[i] == 0)
			continue;

		if (art->count[i] > MH_MENU_MAIN_FRAMES ||
			frame >= art->count[i]) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"missing main menu frame");
			break;
		}

		image = &art->images[i][frame];

		status = mh_menu_sprite_draw_commands(image, &commands, rows,
			ROW_CAPACITY, &phase, &work,
			i >= LABEL_SLOT &&
				i <= LABEL_SLOT + MH_MENU_MAIN_LAST_CHOICE &&
				mh_menu_choice_state(choices, i - LABEL_SLOT) ==
					MH_MENU_DISABLED,
			error_out);
	}

	if (status == MH_ERROR_NONE &&
		mh_menu_choice_state(choices, selected) == MH_MENU_ENABLED)
		status = mh_menu_triangles(
			needle.points, 2, needle.color, 0, &work, error_out);

	if (status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, work.pixels, work.size);
		*state = next;
		*random = seed;
		*noise = phase;
	}

	free(work.pixels);
	free(rows);

	return status;
}
