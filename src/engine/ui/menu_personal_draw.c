#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_list.h"
#include "engine/ui/menu_personal_draw.h"
#include "engine/ui/menu_position.h"
#include "interfaces/sprite/collection.h"

#define PERSONAL_LAYOUT 17
#define LABEL_ORIGIN_SLOT 20
#define VALUE_ORIGIN_SLOT 21
#define LAST_ART_SLOT 3
#define ROW_CAPACITY 65536
#define RGB_MAX 0xffffffUL
#define CHOICE_COLOR_BASE 128
#define SWATCH_COLOR_BASE 160
#define CURSOR_WIDTH_LIMIT 16711425UL
#define CHOICE_OFFSET 44
#define CHOICE_STEP 12

void
mh_menu_personal_art_free(struct mh_menu_personal_art *art)
{
	unsigned int i;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_PERSONAL_ART_SLOTS; i++)
		free(art->images[i].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_personal_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_personal_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_PERSONAL_ART_SLOTS] = {
		NULL, "dots", "opt", "scrd"};

	struct mh_menu_personal_art parsed;
	struct mh_sprite_info info;
	enum mh_error_code status;
	struct mh_menu_art_reader reader = {0, "invalid Personal sprite",
		"Personal artwork exceeds capacity"};

	unsigned int i;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Personal artwork archive");

	memset(&parsed, 0, sizeof(parsed));

	/* The added Benchmark artwork identifies the patched menu bank. */
	status = mh_sprite_collection_info(collection, "tbr", &info, error_out);

	if (status != MH_ERROR_NONE && status != MH_ERROR_NOT_FOUND)
		return status;

	parsed.patched = status == MH_ERROR_NONE;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		status = mh_menu_art_read(&reader, collection, names[i],
			&parsed.images[i], 1, MH_MENU_ART_FIRST_FRAME, NULL,
			error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_personal_art_free(&parsed);

	return status;
}

void
mh_menu_personal_animation_init(struct mh_menu_personal_animation *state)
{
	if (state == NULL)
		return;

	memset(state, 0, sizeof(*state));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
}

static void
set_palette_color(
	unsigned char *palette, unsigned int index, unsigned long color)
{
	palette[index * 3] = (unsigned char)(color >> 16);

	palette[index * 3 + 1] = (unsigned char)(color >> 8);
	palette[index * 3 + 2] = (unsigned char)color;
}

enum mh_error_code
mh_menu_personal_render(const struct mh_menu_personal_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_personal_state *menu,
	const struct mh_settings *settings,
	const unsigned long colors[MH_MENU_PERSONAL_COLORS], unsigned int mix,
	struct mh_menu_personal_animation *state, unsigned long *random,
	unsigned long *noise, unsigned char palette[MH_MENU_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	static const char *const labels[MH_MENU_PERSONAL_ROWS] = {"Name:",
		"Team:", "Color:", "Key 0:", "Key 1:", "Key 2:", "Key 3:",
		"Key 4:", "Key 5:", "Key 6:", "Key 7:", "Key 8:", "Key 9:",
		"Car colors:", "Car color 1:", "Car color 2:", "Car color 3:"};

	struct mh_menu_personal_animation next;
	struct mh_menu_sprite_output commands;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_canvas work;
	enum mh_error_code status;
	const char *value;
	size_t size;
	unsigned int i, row_count, row;
	unsigned long seed, phase;
	unsigned char next_palette[MH_MENU_PALETTE_BYTES];
	long x, y, value_x, spacing;

	if (art == NULL || menu == NULL || settings == NULL || colors == NULL ||
		state == NULL || random == NULL || noise == NULL ||
		palette == NULL || canvas == NULL || canvas->pixels == NULL ||
		canvas->size == 0 || canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		menu->selected >
			mh_menu_personal_row(
				MH_MENU_PERSONAL_BODY_LAST, menu->patched) ||
		settings->av.system > MH_UNITS_IMPERIAL ||
		settings->custom_car_colors > 1 || *random > MH_U32_MAX ||
		*noise > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Personal frame");

	for (i = 0; i < MH_MENU_PERSONAL_COLORS; i++)
		if (colors[i] > RGB_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Personal palette choice");

	status = mh_menu_position(layout, PERSONAL_LAYOUT, LABEL_ORIGIN_SLOT,
		mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, PERSONAL_LAYOUT,
			VALUE_ORIGIN_SLOT, mix, &value_x, &spacing, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(font, (const unsigned char *)"", 1,
			x, y, 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);
	rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*rows));

	if (work.pixels == NULL || rows == NULL) {
		free(work.pixels);
		free(rows);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing Personal");
	}

	next = *state;
	seed = *random;
	phase = *noise;
	memcpy(work.pixels, canvas->pixels, work.size);
	memcpy(next_palette, palette, MH_MENU_PALETTE_BYTES);

	for (i = 0; i < MH_MENU_PERSONAL_COLORS; i++)
		set_palette_color(
			next_palette, CHOICE_COLOR_BASE + i, colors[i]);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		status = mh_menu_sprite_select(layout, PERSONAL_LAYOUT, i, mix,
			0, 0, 0, &next.sprites, &seed, &commands, error_out);

		if (status != MH_ERROR_NONE || art->images[i].pixels == NULL)
			continue;

		status = mh_menu_sprite_draw_commands(&art->images[i],
			&commands, rows, ROW_CAPACITY, &phase, &work, 0,
			error_out);
	}

	row_count = settings->custom_car_colors ? MH_MENU_PERSONAL_ROWS
						: MH_MENU_PERSONAL_BODY_FIRST;

	for (i = 0; i < row_count && status == MH_ERROR_NONE; i++) {
		row = mh_menu_personal_row(i, menu->patched);
		status = mh_menu_text_draw(font,
			(const unsigned char *)labels[i], strlen(labels[i]) + 1,
			x, y + (long)row * spacing,
			row == menu->selected ? 2 : 3, &work, error_out);
	}

	if (status == MH_ERROR_NONE && menu->patched) {
		static const char *const systems[] = {
			"Metric (KPH)", "Imperial (MPH)"};

		row = MH_MENU_PERSONAL_SYSTEM;
		status = mh_menu_text_draw(font,
			(const unsigned char *)"System:", sizeof("System:"), x,
			y + row * spacing, row == menu->selected ? 2 : 3, &work,
			error_out);

		if (status == MH_ERROR_NONE)
			status = mh_menu_choices_draw(font, systems, 2,
				settings->av.system, 15, row == menu->selected,
				value_x, y + row * spacing, &work, error_out);
	}

	for (i = 0;
		i <= MH_MENU_PERSONAL_CUSTOM_COLORS && status == MH_ERROR_NONE;
		i++) {
		if (i == MH_MENU_PERSONAL_PLAYER_COLOR)
			continue;

		if (i == MH_MENU_PERSONAL_NAME) {
			value = settings->player_name;
			size = sizeof(settings->player_name);
		} else if (i == MH_MENU_PERSONAL_TEAM) {
			value = settings->team_name;
			size = sizeof(settings->team_name);
		} else if (i == MH_MENU_PERSONAL_CUSTOM_COLORS) {
			value = settings->custom_car_colors ? "On" : "Off";
			size = strlen(value) + 1;
		} else {
			value = settings->short_keys[i -
				MH_MENU_PERSONAL_SHORTCUT_FIRST];
			size = sizeof(settings->short_keys[0]);
		}

		row = mh_menu_personal_row(i, menu->patched);
		status = mh_menu_text_draw(font, (const unsigned char *)value,
			size, value_x, y + (long)row * spacing,
			row == menu->selected ? 2 : 1, &work, error_out);
	}

	if (status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, work.pixels, work.size);
		memcpy(palette, next_palette, MH_MENU_PALETTE_BYTES);
		*state = next;
		*random = seed;
		*noise = phase;
	}

	free(work.pixels);
	free(rows);

	return status;
}

static void
fill_rectangle(struct mh_menu_canvas *canvas, long x, long y,
	unsigned int width, unsigned int height, unsigned int color)
{
	unsigned int row, col;
	double offset;

	/*
	 * Fill whole rows, including the padded canvas. Bound the allocation
	 * without imposing a logical screen clip.
	 */
	for (row = 0; row < height; row++)
		for (col = 0; col < width; col++) {
			offset = (double)canvas->begin +
				((double)y + row) * canvas->pitch + x + col;

			if (offset >= 0 && offset < (double)canvas->size)
				canvas->pixels[(size_t)offset] =
					(unsigned char)color;
		}
}

static void
draw_picker(const struct mh_menu_personal_swatch *swatch,
	unsigned int palette_index, long x, long y, unsigned char *palette,
	struct mh_menu_canvas *canvas)
{
	unsigned int j, border;
	long choice_x;

	border = swatch->active ? 2 : 1;
	set_palette_color(palette, palette_index, swatch->color);
	fill_rectangle(canvas, x, y - 1, 32, 10, border);
	fill_rectangle(canvas, x + 1, y, 30, 8, palette_index);

	for (j = 0; j < MH_MENU_PERSONAL_COLORS; j++) {
		choice_x = x + CHOICE_OFFSET + (long)j * CHOICE_STEP;

		if (j == swatch->nearest) {
			fill_rectangle(
				canvas, choice_x - 2, y - 2, 14, 12, border);
			fill_rectangle(canvas, choice_x - 1, y - 1, 12, 10, 0);
		}

		fill_rectangle(
			canvas, choice_x, y, 10, 8, CHOICE_COLOR_BASE + j);
	}
}

enum mh_error_code
mh_menu_personal_overlay(const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_personal_output *output, unsigned int mix,
	unsigned char palette[MH_MENU_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_canvas work;
	unsigned char next_palette[MH_MENU_PALETTE_BYTES];
	unsigned int i, row;
	long x, y, label_x, spacing, row_y;
	enum mh_error_code status;

	if (output == NULL || palette == NULL || canvas == NULL ||
		canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		(output->swatch_count != 1 &&
			output->swatch_count != MH_MENU_PERSONAL_SWATCHES) ||
		output->cursor > 1 ||
		output->cursor_width > CURSOR_WIDTH_LIMIT ||
		(output->cursor &&
			(output->cursor_row >
					mh_menu_personal_row(
						MH_MENU_PERSONAL_SHORTCUT_LAST,
						output->patched) ||
				output->cursor_row ==
					MH_MENU_PERSONAL_PLAYER_COLOR)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Personal overlay");

	for (i = 0; i < output->swatch_count; i++)
		if (output->swatches[i].color > RGB_MAX ||
			output->swatches[i].active > 1 ||
			output->swatches[i].nearest >= MH_MENU_PERSONAL_COLORS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid Personal color swatch");

	status = mh_menu_position(layout, PERSONAL_LAYOUT, LABEL_ORIGIN_SLOT,
		mix, &label_x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, PERSONAL_LAYOUT,
			VALUE_ORIGIN_SLOT, mix, &x, &spacing, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(font, (const unsigned char *)"", 1,
			x, y, 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (work.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing Personal overlay");

	memcpy(work.pixels, canvas->pixels, work.size);
	memcpy(next_palette, palette, MH_MENU_PALETTE_BYTES);

	for (i = 0; i < output->swatch_count; i++) {
		row = i == 0 ? MH_MENU_PERSONAL_PLAYER_COLOR
			: MH_MENU_PERSONAL_CUSTOM_COLORS + i;
		row = mh_menu_personal_row(row, output->patched);
		row_y = y + (long)row * spacing;
		draw_picker(&output->swatches[i], SWATCH_COLOR_BASE + i, x,
			row_y, next_palette, &work);
	}

	if (output->cursor)
		status = mh_menu_text_draw(font, (const unsigned char *)"|", 2,
			x + (long)output->cursor_width,
			y + (long)output->cursor_row * spacing, 2, &work,
			error_out);

	if (status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, work.pixels, work.size);
		memcpy(palette, next_palette, MH_MENU_PALETTE_BYTES);
	}

	free(work.pixels);

	return status;
}
