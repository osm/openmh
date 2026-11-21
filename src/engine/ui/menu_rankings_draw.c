#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/lap_presets.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_rankings_draw.h"
#include "interfaces/sprite/collection.h"

#define RANKINGS_LAYOUT 11
#define FIRST_LABEL_SLOT 4
#define LAST_LABEL_SLOT 8
#define TRACK_LABEL_SLOT 5
#define FIRST_SCORE_SLOT 16
#define SCORE_SPACING_SLOT 17
#define MARKER_SLOT 20
#define FIRST_SELECTOR_SLOT 24
#define SELECTOR_COLUMNS 4
#define SCORE_COLUMNS 4
#define LAP_LABEL_BYTES (sizeof(" ") + sizeof(unsigned int) * CHAR_BIT)
#define ROW_CAPACITY 65536
#define MINUTES_RIGHT 80
#define SECONDS_RIGHT 104
#define HUNDREDTHS_RIGHT 124

void
mh_menu_rankings_art_free(struct mh_menu_rankings_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_RANKINGS_ART_SLOTS; i++)
		for (j = 0; j < MH_MENU_RANKINGS_ART_FRAMES; j++)
			free(art->images[i][j].pixels);

	for (j = 0; j < MH_MENU_RANKINGS_ART_FRAMES; j++)
		free(art->marker[j].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_rankings_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_rankings_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_RANKINGS_ART_SLOTS + 1] = {NULL,
		"dots", "rank", "scr6", "ranka", "rankb", "rankd", "ranke",
		NULL, "rankz", "rankx", "mupp"};

	struct mh_menu_rankings_art parsed;
	struct mh_sprite_indexed *images;
	enum mh_error_code status;
	unsigned int i;
	unsigned int *count;
	struct mh_menu_art_reader reader = {0, "invalid Rankings sprite",
		"Rankings artwork exceeds capacity"};

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Rankings artwork archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i <= MH_MENU_RANKINGS_ART_SLOTS && status == MH_ERROR_NONE;
		i++) {
		if (names[i] == NULL)
			continue;

		if (i == MH_MENU_RANKINGS_ART_SLOTS) {
			count = &parsed.marker_count;
			images = parsed.marker;
		} else {
			count = &parsed.count[i];
			images = parsed.images[i];
		}

		status = mh_menu_art_read(&reader, collection, names[i], images,
			MH_MENU_RANKINGS_ART_FRAMES, MH_MENU_ART_ALL_FRAMES,
			count, error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_rankings_art_free(&parsed);

	return status;
}

void
mh_menu_rankings_animation_init(struct mh_menu_rankings_animation *state)
{
	if (state == NULL)
		return;

	memset(state, 0, sizeof(*state));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
}

static enum mh_error_code
draw_right_text(const struct mh_font *font, const char *text, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	unsigned int i, width;

	width = 0;

	for (i = 0; text[i] != 0; i++)
		width += font->advance[(unsigned char)text[i]];

	return mh_menu_text_draw(font, (const unsigned char *)text, i + 1,
		x - (long)width, y, 1, canvas, error_out);
}

static enum mh_error_code
draw_time(const struct mh_font *font, unsigned long time_ms, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	char text[16];
	enum mh_error_code status;

	if (time_ms > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Rankings time");

	sprintf(text, "%8lu:", time_ms / 60000UL);
	status = draw_right_text(
		font, text, x + MINUTES_RIGHT, y, canvas, error_out);
	sprintf(text, "%02lu,", time_ms / 1000UL % 60);

	if (status == MH_ERROR_NONE)
		status = draw_right_text(
			font, text, x + SECONDS_RIGHT, y, canvas, error_out);

	sprintf(text, "%02lu", time_ms / 10UL % 100);

	if (status != MH_ERROR_NONE)
		return status;

	return draw_right_text(
		font, text, x + HUNDREDTHS_RIGHT, y, canvas, error_out);
}

static enum mh_error_code
draw_scores(const struct mh_font *font, const struct mh_menu_layout *layout,
	unsigned int category, const struct mh_highscore_table *table,
	unsigned int mix, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	unsigned char text[MH_HIGHSCORE_NAME_SIZE + 1];
	unsigned int i;
	long y, unused, spacing, columns[SCORE_COLUMNS];
	enum mh_error_code status;

	status = mh_menu_position(layout, RANKINGS_LAYOUT, FIRST_SCORE_SLOT,
		mix, &unused, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, RANKINGS_LAYOUT,
			SCORE_SPACING_SLOT, mix, &unused, &spacing, error_out);

	for (i = 0; i < SCORE_COLUMNS && status == MH_ERROR_NONE; i++)
		status = mh_menu_position(layout, RANKINGS_LAYOUT,
			FIRST_SCORE_SLOT + i, mix, &columns[i], &unused,
			error_out);

	for (i = 0; i < MH_HIGHSCORE_ROWS && status == MH_ERROR_NONE; i++) {
		memcpy(text, table->rows[i].name, MH_HIGHSCORE_NAME_SIZE);
		text[MH_HIGHSCORE_NAME_SIZE] = 0;
		status = mh_menu_text_draw(font, text,
			MH_HIGHSCORE_NAME_SIZE + 1, columns[0], y, 1, canvas,
			error_out);
		memcpy(text, table->rows[i].car, MH_HIGHSCORE_CAR_SIZE);
		text[MH_HIGHSCORE_CAR_SIZE] = 0;

		if (category == MH_HIGHSCORE_LEAGUE)
			memcpy(text, "----", 5);

		if (status == MH_ERROR_NONE)
			status = mh_menu_text_draw(font, text,
				MH_HIGHSCORE_CAR_SIZE + 1, columns[1], y, 1,
				canvas, error_out);

		if (status == MH_ERROR_NONE)
			status = draw_time(font, table->rows[i].lap_ms,
				columns[2], y, canvas, error_out);

		if (status == MH_ERROR_NONE)
			status = category == MH_HIGHSCORE_LAP
				? draw_right_text(font, "----",
					columns[3] + MINUTES_RIGHT, y, canvas,
					error_out)
				: draw_time(font, table->rows[i].race_ms,
					columns[3], y, canvas, error_out);

		y += spacing;
	}

	return status;
}

enum mh_error_code
mh_menu_rankings_render(const struct mh_menu_rankings_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_rankings_state *menu, const char *const *tracks,
	unsigned int track_count, const struct mh_highscore_table *table,
	unsigned int mix, struct mh_menu_rankings_animation *state,
	unsigned long *random, unsigned long *noise,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	static const char *const categories[MH_MENU_RANKINGS_CATEGORIES] = {
		"Best laptime", "Best racetime", "Single race"};

	static const char *const divisions[MH_MENU_RANKINGS_DIVISIONS] = {
		"Division 3", "Division 2&3", "Total"};

	static const char *const difficulty[MH_MENU_RANKINGS_DIFFICULTIES] = {
		"Easy", "Medium", "Hard"};

	static const char *const absent[1] = {"----"};
	struct mh_menu_rankings_animation next;
	struct mh_menu_sprite_output commands;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_canvas work;
	const struct mh_sprite_indexed *image;
	const char *const *names;
	const char *lap_names[MH_SINGLE_LAP_PRESET_COUNT];
	char lap_labels[MH_SINGLE_LAP_PRESET_COUNT][LAP_LABEL_BYTES];
	enum mh_error_code status;
	unsigned long seed, phase;
	unsigned int i, j, frame, count, selected, visible;
	long x, y, unused;

	if (art == NULL || menu == NULL || state == NULL || random == NULL ||
		noise == NULL || menu->focus > MH_MENU_RANKINGS_RECORD_CLASS ||
		menu->category > MH_HIGHSCORE_LEAGUE ||
		menu->division >= MH_MENU_RANKINGS_DIVISIONS ||
		menu->lap_choice >= MH_SINGLE_LAP_PRESET_COUNT ||
		menu->difficulty >= MH_MENU_RANKINGS_DIFFICULTIES ||
		menu->record_class > 1 || track_count == 0 ||
		track_count > MH_MENU_RANKINGS_TRACK_LIMIT ||
		menu->track >= track_count || *random > MH_U32_MAX ||
		*noise > MH_U32_MAX || canvas == NULL ||
		canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Rankings frame");

	status = mh_menu_text_draw(
		font, (const unsigned char *)"", 1, 0, 0, 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);
	rows = (struct mh_menu_sprite_row *)malloc(
		(size_t)ROW_CAPACITY * sizeof(*rows));

	if (work.pixels == NULL || rows == NULL) {
		free(work.pixels);
		free(rows);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing Rankings");
	}

	memcpy(work.pixels, canvas->pixels, work.size);
	next = *state;
	seed = *random;
	phase = *noise;
	memset(next.frames, 0, sizeof(next.frames));

	for (i = FIRST_LABEL_SLOT; i <= LAST_LABEL_SLOT; i++)
		next.frames[i] = 1;

	next.frames[FIRST_LABEL_SLOT + menu->focus] = 0;

	if (menu->category == MH_HIGHSCORE_LEAGUE)
		next.frames[TRACK_LABEL_SLOT] += 2;

	for (i = 1; i < MH_MENU_RANKINGS_ART_SLOTS && status == MH_ERROR_NONE;
		i++) {
		frame = next.frames[i];
		status = mh_menu_sprite_select(layout, RANKINGS_LAYOUT, i, mix,
			0, 0, frame, &next.sprites, &seed, &commands,
			error_out);

		if (status != MH_ERROR_NONE || art->count[i] == 0)
			continue;

		if (art->count[i] > MH_MENU_RANKINGS_ART_FRAMES ||
			frame >= art->count[i]) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"missing Rankings sprite frame");
			break;
		}

		image = &art->images[i][frame];

		status = mh_menu_sprite_draw_commands(image, &commands, rows,
			ROW_CAPACITY, &phase, &work, 0, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, RANKINGS_LAYOUT,
			FIRST_SELECTOR_SLOT, mix, &unused, &y, error_out);

	for (i = 0; i < SELECTOR_COLUMNS && status == MH_ERROR_NONE; i++) {
		next.frames[MARKER_SLOT] =
			next.frames[i + FIRST_LABEL_SLOT] & 1;
		status = mh_menu_position(layout, RANKINGS_LAYOUT,
			i + FIRST_SELECTOR_SLOT, mix, &x, &unused, error_out);
		image = NULL;

		if (i != MH_MENU_RANKINGS_CATEGORY && art->marker_count != 0) {
			if (art->marker_count > MH_MENU_RANKINGS_ART_FRAMES ||
				next.frames[MARKER_SLOT] >= art->marker_count)
				status = mh_error_set(error_out,
					MH_ERROR_INVALID,
					"missing Rankings marker frame");
			else
				image = &art->marker[next.frames[MARKER_SLOT]];
		}

		names = absent;
		count = 1;
		selected = 0;
		visible = 10;

		if (i == MH_MENU_RANKINGS_CATEGORY) {
			names = categories;
			count = MH_MENU_RANKINGS_CATEGORIES;
			selected = menu->category;
		}

		if (i == MH_MENU_RANKINGS_TRACK) {
			if (menu->category == MH_HIGHSCORE_LEAGUE) {
				names = divisions;
				count = MH_MENU_RANKINGS_DIVISIONS;
				selected = menu->division;
			} else {
				names = tracks;
				count = track_count;
				selected = menu->track;
				visible = 4;
			}
		}

		if (i == MH_MENU_RANKINGS_LAPS &&
			menu->category != MH_HIGHSCORE_LAP &&
			menu->category != MH_HIGHSCORE_LEAGUE) {
			count = MH_SINGLE_LAP_PRESET_COUNT;

			for (j = 0; j < count; j++) {
				sprintf(lap_labels[j], " %u",
					mh_single_lap_presets[j]);
				lap_names[j] = lap_labels[j];
			}

			names = lap_names;
			selected = menu->lap_choice;
			visible = 4;
		}

		if (i == MH_MENU_RANKINGS_DIFFICULTY &&
			menu->category >= MH_HIGHSCORE_SINGLE) {
			names = difficulty;
			count = MH_MENU_RANKINGS_DIFFICULTIES;
			selected = menu->difficulty;
		}

		if (status == MH_ERROR_NONE)
			status = mh_menu_list_selection_draw(font, names, count,
				selected, visible, menu->focus == i, image, x,
				y, &work, error_out);
	}

	if (table != NULL && status == MH_ERROR_NONE)
		status = draw_scores(font, layout, menu->category, table, mix,
			&work, error_out);

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
