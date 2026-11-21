#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/keyboard.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_controls_draw.h"
#include "engine/ui/menu_list.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_slider.h"
#include "interfaces/sprite/collection.h"

#define CONTROLS_LAYOUT 8
#define ROW_CAPACITY 65536
#define POSITION_COUNT 7
#define KEY_NAME_BYTES 32
#define KEY_CODE_MASK 255
#define KEY_TEXT_X 44
#define KEY_TEXT_Y 4
#define PATCHED_KEY_TEXT_Y 5
#define SENSITIVITY_PERCENT 100.0
#define SENSITIVITY_SLIDER_WIDTH 150
#define RELEASE_KEY_X 112
#define LIST_HALF_DIVISOR 8
#define SELECTOR_SLOT 4
#define FIRST_ROW_LABEL_SLOT 5
#define LIST_ORIGIN_SLOT 21
#define LIST_SPACING_SLOT 22
#define ROW_BACKGROUND_SLOT 24
#define KEY_BACKGROUND_SLOT 25
#define LABEL_OFFSET_SLOT 29
#define ROW_SPACING_SLOT 30

void
mh_menu_controls_art_free(struct mh_menu_controls_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_CONTROLS_ART_SLOTS; i++)
		for (j = 0; j < MH_MENU_CONTROLS_ART_FRAMES; j++)
			free(art->images[i][j].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_controls_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_controls_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_CONTROLS_ART_SLOTS] = {NULL,
		"dots", "opt", "scr3", "selco", "tlef", "trig", "tacc", "tbre",
		"tgup", "tgdo", "thorn", "thbr", "tsht", "tmenu", "tpaus", NULL,
		NULL, NULL, NULL, NULL, NULL, NULL, NULL, "ctrla", "ctrlb"};

	static const char *const patched_names[MH_MENU_CONTROLS_ART_SLOTS] = {
		NULL, "dots", "opt", "scr3", "selco", "tforf", "tmspx", "tmspy",
		"tmspz", "tlef", "trig", "tacc", "tbre", "tgup", "tgdo",
		"thorn", "thbr", "tsht", "tmenu", "tpaus", NULL, NULL, NULL,
		NULL, "ctrla", "ctrlb"};

	struct mh_menu_controls_art parsed;
	struct mh_sprite_info info;
	const char *const *labels;
	enum mh_error_code status;
	struct mh_menu_art_reader reader = {0, "invalid Controls sprite",
		"Controls artwork exceeds capacity"};

	unsigned int i;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Controls artwork archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_sprite_collection_info(
		collection, "tmspx", &info, error_out);

	if (status != MH_ERROR_NONE && status != MH_ERROR_NOT_FOUND)
		return status;

	parsed.preferences = status == MH_ERROR_NONE;
	labels = parsed.preferences ? patched_names : names;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i < MH_MENU_CONTROLS_ART_SLOTS && status == MH_ERROR_NONE;
		i++) {
		if (labels[i] == NULL)
			continue;

		status = mh_menu_art_read(&reader, collection, labels[i],
			parsed.images[i], MH_MENU_CONTROLS_ART_FRAMES,
			MH_MENU_ART_ALL_FRAMES, &parsed.count[i], error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_controls_art_free(&parsed);

	return status;
}

void
mh_menu_controls_animation_init(struct mh_menu_controls_animation *state)
{
	if (state == NULL)
		return;

	memset(state, 0, sizeof(*state));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
}

static enum mh_error_code
draw_sprite(const struct mh_menu_controls_art *art,
	const struct mh_menu_layout *layout, unsigned int slot,
	unsigned int mix, long dx, long dy, unsigned int frame,
	struct mh_menu_controls_animation *state, unsigned long *seed,
	unsigned long *noise, struct mh_menu_sprite_row *rows,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_sprite_output commands;
	const struct mh_sprite_indexed *image;
	enum mh_error_code status;

	status = mh_menu_sprite_select(layout, CONTROLS_LAYOUT, slot, mix, dx,
		dy, frame, &state->sprites, seed, &commands, error_out);

	if (status != MH_ERROR_NONE || art->count[slot] == 0)
		return status;

	if (art->count[slot] > MH_MENU_CONTROLS_ART_FRAMES ||
		frame >= art->count[slot])
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing Controls sprite frame");

	image = &art->images[slot][frame];

	status = mh_menu_sprite_draw_commands(image, &commands, rows,
		ROW_CAPACITY, noise, canvas, 0, error_out);

	return status;
}

static enum mh_error_code
draw_binding(const struct mh_font *font,
	const struct mh_menu_controls_state *menu, unsigned int row,
	unsigned int code, const char *const *key_names, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	const char *name;
	enum mh_error_code status;
	unsigned int color;

	color = menu->phase != MH_MENU_CONTROLS_IDLE && menu->row == row ? 2
									: 1;

	if (menu->row == row && menu->phase == MH_MENU_CONTROLS_CAPTURE)
		return mh_menu_text_draw(font,
			(const unsigned char *)"Assign new control",
			sizeof("Assign new control"), x + KEY_TEXT_X, y, color,
			canvas, error_out);

	if (menu->row == row && menu->phase == MH_MENU_CONTROLS_RELEASE) {
		status = mh_menu_text_draw(font,
			(const unsigned char *)"Release:", sizeof("Release:"),
			x + KEY_TEXT_X, y, color, canvas, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		code = menu->held_key;
		x += RELEASE_KEY_X;
	} else
		x += KEY_TEXT_X;

	name = key_names == NULL ? mh_control_key_name(code) : key_names[code];

	return mh_menu_text_draw(font, (const unsigned char *)name,
		KEY_NAME_BYTES, x, y, color, canvas, error_out);
}

static enum mh_error_code
draw_preference(const struct mh_font *font,
	const struct mh_menu_controls_state *menu,
	const struct mh_control_layout *profile, unsigned int row, long x,
	long y, struct mh_menu_canvas *canvas,
	const struct mh_menu_choices *choices, struct mh_error *error_out)
{
	static const char *const feedback[] = {"Off", "On"};
	float percent;
	unsigned int axis;
	int active;

	x += KEY_TEXT_X;
	active = menu->phase != MH_MENU_CONTROLS_IDLE && menu->row == row;

	if ((menu->unavailable & (1U << row)) ||
		mh_menu_choice_state(choices, row) != MH_MENU_ENABLED)
		return mh_menu_text_draw(font,
			(const unsigned char *)"Unavailable",
			sizeof("Unavailable"), x, y, 1, canvas, error_out);

	if (row == MH_MENU_CONTROLS_FEEDBACK)
		return mh_menu_choices_draw(font, feedback, 2,
			profile->force_feedback & 1, 10, active, x, y, canvas,
			error_out);

	axis = row - MH_MENU_CONTROLS_MOUSE_X;
	percent = (float)((long double)profile->mouse_speed[axis] *
		(long double)SENSITIVITY_PERCENT);

	return mh_menu_slider_draw(font, percent, 1, 1000, "%",
		SENSITIVITY_SLIDER_WIDTH, active, x, y, canvas, error_out);
}

static enum mh_error_code
draw_profiles(const struct mh_font *font,
	const struct mh_control_profile *profiles, unsigned int count,
	unsigned int selected, long x, long y, long half, long spacing,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int visible, distance, index;

	status = mh_menu_text_draw(font,
		(const unsigned char *)profiles[selected].layout.name,
		sizeof(profiles[0].layout.name), x, y, 2, canvas, error_out);

	visible = half > 0 ? (unsigned int)half : 0;

	if (visible > selected)
		visible = selected;

	for (distance = 1; distance <= visible && status == MH_ERROR_NONE;
		distance++) {
		index = selected - distance;
		status = mh_menu_text_draw(font,
			(const unsigned char *)profiles[index].layout.name,
			sizeof(profiles[0].layout.name), x,
			y - (long)distance * spacing, 1, canvas, error_out);
	}

	visible = half > 0 ? (unsigned int)half : 0;

	if (visible > count - 1 - selected)
		visible = count - 1 - selected;

	for (distance = 1; distance <= visible && status == MH_ERROR_NONE;
		distance++) {
		index = selected + distance;
		status = mh_menu_text_draw(font,
			(const unsigned char *)profiles[index].layout.name,
			sizeof(profiles[0].layout.name), x,
			y + (long)distance * spacing, 1, canvas, error_out);
	}

	return status;
}

enum mh_error_code
mh_menu_controls_render(const struct mh_menu_controls_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_controls_state *menu,
	const struct mh_control_profile *profiles, unsigned int count,
	const char *const *key_names, unsigned int mix, unsigned long clock_ms,
	struct mh_menu_controls_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	const struct mh_menu_choices *choices, struct mh_error *error_out)
{
	const struct mh_control_layout *selected_profile;
	struct mh_menu_controls_animation next;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_canvas work;
	enum mh_error_code status;
	unsigned long seed, phase;
	unsigned int i, frame, animated, code, first_action, row_count;
	long x[MH_MENU_LAYOUT_SLOTS], y[MH_MENU_LAYOUT_SLOTS], half, list_x,
		list_y;
	static const unsigned int positions[POSITION_COUNT] = {SELECTOR_SLOT,
		LIST_ORIGIN_SLOT, LIST_SPACING_SLOT, ROW_BACKGROUND_SLOT,
		KEY_BACKGROUND_SLOT, LABEL_OFFSET_SLOT, ROW_SPACING_SLOT};

	if (art == NULL || menu == NULL || profiles == NULL || count == 0 ||
		count > MH_MENU_CONTROL_CAPACITY || state == NULL ||
		random == NULL || noise == NULL || canvas == NULL ||
		canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		menu->column > MH_MENU_CONTROLS_BINDINGS ||
		menu->preferences > 1 ||
		menu->preferences != art->preferences ||
		menu->unavailable >= (1U << MH_MENU_CONTROLS_FIRST_ACTION) ||
		menu->row >= (menu->preferences ? MH_MENU_CONTROLS_ROWS
						: MH_MENU_CONTROL_ACTIONS) ||
		menu->selected >= count ||
		(menu->phase > MH_MENU_CONTROLS_FINISH &&
			menu->phase != MH_MENU_CONTROLS_RELEASE &&
			menu->phase != MH_MENU_CONTROLS_RESTORE) ||
		menu->held_key >= MH_KEY_COUNT || *random > MH_U32_MAX ||
		*noise > MH_U32_MAX || clock_ms > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Controls frame");

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < POSITION_COUNT && status == MH_ERROR_NONE; i++)
		status = mh_menu_position(layout, CONTROLS_LAYOUT, positions[i],
			mix, &x[positions[i]], &y[positions[i]], error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(font, (const unsigned char *)"", 1,
			0, 0, 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	selected_profile = &profiles[menu->selected].layout;
	first_action = menu->preferences ? MH_MENU_CONTROLS_FIRST_ACTION : 0;
	row_count = first_action + MH_MENU_CONTROL_ACTIONS;
	next = *state;
	seed = *random;
	phase = *noise;
	animated = mh_menu_sprite_label_frame(clock_ms);
	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);
	rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*rows));

	if (work.pixels == NULL || rows == NULL) {
		free(work.pixels);
		free(rows);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing Controls");
	}

	memcpy(work.pixels, canvas->pixels, work.size);

	for (i = 1; i <= SELECTOR_SLOT && status == MH_ERROR_NONE; i++)
		status = draw_sprite(art, layout, i, mix, 0, 0,
			i == SELECTOR_SLOT ? menu->column : 0, &next, &seed,
			&phase, rows, &work, error_out);

	for (i = 0; i < row_count && status == MH_ERROR_NONE; i++) {
		if (mh_menu_choice_state(choices, i) == MH_MENU_HIDDEN)
			continue;

		status = draw_sprite(art, layout, ROW_BACKGROUND_SLOT, mix, 0,
			(long)i * y[ROW_SPACING_SLOT], 0, &next, &seed, &phase,
			rows, &work, error_out);

		if (status == MH_ERROR_NONE)
			status = draw_sprite(art, layout, KEY_BACKGROUND_SLOT,
				mix, 0, (long)i * y[ROW_SPACING_SLOT], 0, &next,
				&seed, &phase, rows, &work, error_out);

		frame = menu->column == MH_MENU_CONTROLS_BINDINGS &&
				menu->row == i
			? animated
			: 0;

		if (status == MH_ERROR_NONE)
			status = draw_sprite(art, layout,
				FIRST_ROW_LABEL_SLOT + i, mix,
				x[ROW_BACKGROUND_SLOT] + x[LABEL_OFFSET_SLOT],
				y[ROW_BACKGROUND_SLOT] + y[LABEL_OFFSET_SLOT] +
					(long)i * y[ROW_SPACING_SLOT],
				frame, &next, &seed, &phase, rows, &work,
				error_out);

		if (status != MH_ERROR_NONE)
			break;

		if (i < first_action)
			status = draw_preference(font, menu, selected_profile,
				i, x[KEY_BACKGROUND_SLOT],
				y[KEY_BACKGROUND_SLOT] + PATCHED_KEY_TEXT_Y +
					(long)i * y[ROW_SPACING_SLOT],
				&work, choices, error_out);
		else {
			code = (unsigned int)(selected_profile->bindings[i -
						first_action] &
				KEY_CODE_MASK);
			status = draw_binding(font, menu, i, code, key_names,
				x[KEY_BACKGROUND_SLOT],
				y[KEY_BACKGROUND_SLOT] +
					(menu->preferences ? PATCHED_KEY_TEXT_Y
							: KEY_TEXT_Y) +
					(long)i * y[ROW_SPACING_SLOT],
				&work, error_out);
		}
	}

	list_x = x[SELECTOR_SLOT] + x[LIST_ORIGIN_SLOT];
	list_y = y[SELECTOR_SLOT] + y[LIST_ORIGIN_SLOT];
	half = x[LIST_SPACING_SLOT] >= 0
		? x[LIST_SPACING_SLOT] / LIST_HALF_DIVISOR
		: -((-x[LIST_SPACING_SLOT]) / LIST_HALF_DIVISOR);

	if (status == MH_ERROR_NONE)
		status = draw_profiles(font, profiles, count, menu->selected,
			list_x, list_y, half, y[LIST_SPACING_SLOT], &work,
			error_out);

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
