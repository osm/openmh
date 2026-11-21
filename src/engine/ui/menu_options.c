#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_options.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_triangles.h"
#include "interfaces/sprite/collection.h"

#define OPTIONS_LAYOUT 7
#define DIFFICULTY_CHOICE 4
#define DIFFICULTY_COUNT 3
#define LAST_DIFFICULTY 2
#define LAST_ART_SLOT (MH_MENU_OPTIONS_SLOTS - 1)
#define ROW_CAPACITY 65536
#define LABEL_SLOT 5
#define DIFFICULTY_ART_SLOT 11
#define DIFFICULTY_FRAME_STEP 8
#define LAST_DIFFICULTY_FRAME 16
#define NEEDLE_TARGET_SLOT 24
#define NEEDLE_ORIGIN_SLOT 30
#define NEEDLE_RESPONSE 32
#define NEEDLE_LENGTH 2.5999999046325684f

static void
append_event(
	struct mh_menu_output *result, unsigned int kind, unsigned int value)
{
	result->events[result->count].kind = kind;

	result->events[result->count++].value = value;
}

enum mh_error_code
mh_menu_options_update(struct mh_menu_options_state *state,
	const struct mh_menu_input *input, struct mh_menu_output *output,
	const struct mh_menu_choices *choices, struct mh_error *error_out)
{
	static const unsigned int destinations[4] = {MH_MENU_PERSONAL,
		MH_MENU_GRAPHICS, MH_MENU_CONTROLS, MH_MENU_SOUND_OPTIONS};

	struct mh_menu_options_state next;
	struct mh_menu_output result;

	if (state == NULL || input == NULL || output == NULL ||
		state->selected > DIFFICULTY_CHOICE ||
		state->difficulty > LAST_DIFFICULTY)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Options input");

	next = *state;
	memset(&result, 0, sizeof(result));

	next.selected = mh_menu_choice_first(choices, next.selected);

	if (input->left == 1)
		append_event(&result, MH_MENU_SOUND,
			mh_menu_choice_navigate(choices, &next.selected,
				DIFFICULTY_CHOICE + 1, 0));

	if (input->right == 1)
		append_event(&result, MH_MENU_SOUND,
			mh_menu_choice_navigate(choices, &next.selected,
				DIFFICULTY_CHOICE + 1, 1));

	if (input->enter == 1 &&
		mh_menu_choice_state(choices, next.selected) != MH_MENU_ENABLED)
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_LIMIT);

	if (input->enter == 1 &&
		mh_menu_choice_state(choices, next.selected) ==
			MH_MENU_ENABLED) {
		if (next.selected == DIFFICULTY_CHOICE) {
			append_event(
				&result, MH_MENU_SOUND, MH_MENU_SOUND_CHANGE);
			next.difficulty =
				(next.difficulty + 1) % DIFFICULTY_COUNT;
		} else {
			append_event(
				&result, MH_MENU_SOUND, MH_MENU_SOUND_CONFIRM);
			append_event(&result, MH_MENU_PAGE,
				destinations[next.selected]);
		}
	}

	if (input->escape == 1) {
		append_event(&result, MH_MENU_SOUND, MH_MENU_SOUND_CANCEL);
		append_event(&result, MH_MENU_PAGE, MH_MENU_MAIN);
	}

	*state = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_menu_options_art_free(struct mh_menu_options_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_OPTIONS_SLOTS; i++)
		for (j = 0; j < MH_MENU_OPTIONS_FRAMES; j++)
			free(art->images[i][j].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_options_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_options_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_OPTIONS_SLOTS] = {NULL, "dots",
		"opt", "scr1", "msel", "tpers", "tgro", "tcon", "tso", "tdif",
		"difin", "emh"};

	struct mh_menu_options_art parsed;
	enum mh_error_code status;
	struct mh_menu_art_reader reader = {0, "invalid Options sprite",
		"Options artwork exceeds capacity"};

	unsigned int i;

	if (collection == NULL || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Options archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		status = mh_menu_art_read(&reader, collection, names[i],
			parsed.images[i], MH_MENU_OPTIONS_FRAMES,
			MH_MENU_ART_ALL_FRAMES, &parsed.count[i], error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_options_art_free(&parsed);

	return status;
}

static enum mh_error_code
needle_target(const struct mh_menu_layout *layout, unsigned int selected,
	unsigned int mix, float *angle, struct mh_error *error_out)
{
	long x, y;
	enum mh_error_code status;

	if (selected > DIFFICULTY_CHOICE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Options selection");

	status = mh_menu_position(layout, OPTIONS_LAYOUT,
		selected + NEEDLE_TARGET_SLOT, mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		*angle = (float)(8 * x + y);

	return status;
}

enum mh_error_code
mh_menu_options_animation_init(const struct mh_menu_layout *layout,
	unsigned int selected, unsigned int mix,
	struct mh_menu_options_animation *state, struct mh_error *error_out)
{
	struct mh_menu_options_animation next;
	float angle = 0;
	enum mh_error_code status;

	if (state == NULL || state->difficulty_frame > LAST_DIFFICULTY_FRAME)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Options animation");

	status = needle_target(layout, selected, mix, &angle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *state;
	memset(&next.sprites, 0, sizeof(next.sprites));
	next.sprites.wobble[2] = next.sprites.wobble[3] = 1;
	status = mh_menu_needle_init(
		&next.needle, angle, NEEDLE_RESPONSE, error_out);

	if (status == MH_ERROR_NONE)
		*state = next;

	return status;
}

enum mh_error_code
mh_menu_options_render(const struct mh_menu_options_art *art,
	const struct mh_menu_layout *layout,
	const struct mh_menu_options_state *menu, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms, int no_subpixel,
	struct mh_menu_options_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	const struct mh_menu_choices *choices, struct mh_error *error_out)
{
	struct mh_menu_options_animation next;
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

	if (art == NULL || menu == NULL || state == NULL || random == NULL ||
		noise == NULL || canvas == NULL || canvas->pixels == NULL ||
		canvas->size == 0 || canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		menu->selected > DIFFICULTY_CHOICE ||
		menu->difficulty > LAST_DIFFICULTY ||
		state->difficulty_frame > LAST_DIFFICULTY_FRAME ||
		*random > MH_U32_MAX || *noise > MH_U32_MAX ||
		clock_ms > MH_U32_MAX || elapsed_ms > MH_U32_MAX ||
		(no_subpixel != 0 && no_subpixel != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Options frame");

	status = needle_target(layout, menu->selected, mix, &angle, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, OPTIONS_LAYOUT,
			NEEDLE_ORIGIN_SLOT, mix, &x, &y, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *state;
	seed = *random;
	phase = *noise;

	if (next.difficulty_frame > menu->difficulty * DIFFICULTY_FRAME_STEP)
		next.difficulty_frame--;

	if (next.difficulty_frame < menu->difficulty * DIFFICULTY_FRAME_STEP)
		next.difficulty_frame++;

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
			"out of memory composing Options");
	}

	memcpy(work.pixels, canvas->pixels, work.size);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		if (i == DIFFICULTY_ART_SLOT)
			frame = next.difficulty_frame;
		else if (i == menu->selected + LABEL_SLOT &&
			mh_menu_choice_state(choices, menu->selected) ==
				MH_MENU_ENABLED)
			frame = animated;
		else
			frame = 0;

		status = mh_menu_sprite_select(layout, OPTIONS_LAYOUT, i, mix,
			0, 0, frame, &next.sprites, &seed, &commands,
			error_out);

		if (i >= LABEL_SLOT && i <= LABEL_SLOT + DIFFICULTY_CHOICE &&
			mh_menu_choice_state(choices, i - LABEL_SLOT) ==
				MH_MENU_HIDDEN)
			continue;

		if (i >= DIFFICULTY_ART_SLOT - 1 &&
			mh_menu_choice_state(choices, DIFFICULTY_CHOICE) ==
				MH_MENU_HIDDEN)
			continue;

		if (status != MH_ERROR_NONE || art->count[i] == 0)
			continue;

		if (art->count[i] > MH_MENU_OPTIONS_FRAMES ||
			frame >= art->count[i]) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"missing Options frame");
			break;
		}

		image = &art->images[i][frame];

		status = mh_menu_sprite_draw_commands(image, &commands, rows,
			ROW_CAPACITY, &phase, &work,
			i >= LABEL_SLOT &&
				i <= LABEL_SLOT + DIFFICULTY_CHOICE &&
				mh_menu_choice_state(choices, i - LABEL_SLOT) ==
					MH_MENU_DISABLED,
			error_out);
	}

	if (status == MH_ERROR_NONE &&
		mh_menu_choice_state(choices, menu->selected) ==
			MH_MENU_ENABLED)
		status = mh_menu_triangles(needle.points, 2, needle.color,
			no_subpixel, &work, error_out);

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
