#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_palette.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_preview.h"
#include "engine/ui/menu_race_draw.h"
#include "engine/ui/menu_sine.h"
#include "engine/ui/menu_text.h"
#include "engine/ui/menu_wire.h"
#include "interfaces/sprite/collection.h"

#define CATALOG_LIMIT 4096
#define NAME_BYTES 128
#define ROW_CAPACITY 65536
#define OUTLINE_LIMIT 65536
#define SWITCH_LAST_FRAME 7
#define ARROW_PHASE_MASK 255
#define ARROW_HALF_PERIOD 128
#define ARROW_GAP 8
#define ARROW_LEFT 0x87
#define ARROW_RIGHT 0x88
#define MESSAGE_PHASE_MASK 1023
#define MESSAGE_HALF_PERIOD 512
#define MESSAGE_RISE 5
#define RACE_LAYOUT 2
#define LAST_ART_SLOT 10
#define TIME_ATTACK_LAST_SLOT 7
#define LABEL_SLOT 6
#define CATCH_UP_SLOT 9
#define TITLE_SLOT 19
#define LAPS_SLOT 18
#define THUMBNAIL_SLOT 24
#define OUTLINE_SLOT 30
#define OUTLINE_WIDTH 640
#define OUTLINE_HEIGHT 400
#define OUTLINE_COLOR 255
#define LAP_DIGITS 2

void
mh_menu_race_art_free(struct mh_menu_race_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_RACE_ART_SLOTS; i++)
		for (j = 0; j < MH_MENU_RACE_ART_FRAMES; j++)
			free(art->images[i][j].pixels);

	free(art->numa.pixels);
	free(art->numb.pixels);
	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_race_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_race_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_RACE_ART_SLOTS] = {NULL, "dots",
		"track", "scra", "trsel", "carop", "ttrac", "tlaps", "tcat",
		"onoff", "caro2"};

	struct mh_menu_race_art parsed;
	unsigned int i;
	struct mh_menu_art_reader reader = {0, "invalid race setup sprite",
		"race setup artwork exceeds capacity"};

	enum mh_error_code status;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race setup archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++)
		status = mh_menu_art_read(&reader, collection, names[i],
			parsed.images[i], MH_MENU_RACE_ART_FRAMES,
			MH_MENU_ART_ALL_FRAMES, &parsed.count[i], error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_art_read(&reader, collection, "numa",
			&parsed.numa, 1, MH_MENU_ART_ALL_FRAMES, NULL,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_art_read(&reader, collection, "numb",
			&parsed.numb, 1, MH_MENU_ART_ALL_FRAMES, NULL,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_palette_load(
			collection, parsed.palette, error_out);

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_race_art_free(&parsed);

	return status;
}

void
mh_menu_race_animation_init(struct mh_menu_race_animation *state)
{
	if (state == NULL)
		return;

	memset(&state->sprites, 0, sizeof(state->sprites));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
	state->fade = 0;
}

static enum mh_error_code
draw_centered(const struct mh_font *font, const unsigned char *text, long x,
	long y, struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	unsigned int i, width;

	if (font == NULL || text == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing race setup text");

	width = 0;

	for (i = 0; i < NAME_BYTES && text[i] != 0; i++)
		width += font->advance[text[i]];

	if (i == NAME_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated race setup text");

	return mh_menu_text_draw(font, text, i + 1, x - (long)(width / 2), y, 1,
		canvas, error_out);
}

static void
outline_pose(unsigned long clock_ms, float fade, float position[3],
	unsigned long angles[3])
{
	float rotation;
	long double turn;
	unsigned int sine_phase;

	rotation = (float)((long double)clock_ms * 32.0f * 0.001953125);
	turn = (long double)rotation * 13.0f;
	angles[1] = ((unsigned long)turn) & MH_U32_MAX;

	sine_phase = (unsigned int)angles[1] + MH_MENU_SINE_QUARTER_TURN;
	angles[0] =
		(unsigned long)(long)((long double)mh_menu_sine(sine_phase) *
			250);
	angles[2] = 0;
	position[0] = 0;
	position[1] = 105;
	position[2] = (float)((1 - (long double)fade) * 3000 + 475);
}

enum mh_error_code
mh_menu_race_render(const struct mh_menu_race_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_race_setup_state *menu,
	const struct mh_menu_race_scene *scene, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms,
	unsigned int snap_digits, struct mh_menu_race_animation *state,
	struct mh_menu_digits *digits, unsigned long *random,
	unsigned long *noise, unsigned char palette[MH_MENU_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_race_animation next;
	struct mh_menu_digits numbers;
	struct mh_menu_sprite_output commands;
	struct mh_menu_canvas work;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_wire_point *points;
	struct mh_menu_line *lines;
	const struct mh_wireframe *model;
	const struct mh_menu_thumbnail *thumbnail;
	const struct mh_sprite_indexed *image;
	enum mh_error_code status;
	unsigned char colors[MH_MENU_PALETTE_BYTES], arrow[2];
	unsigned long seed, phase, angles[3];
	unsigned int i, animated, frame, last, width;
	float position[3], center_x, center_y, fade;
	long x, y, thumb_x, thumb_y;

	if (art == NULL || menu == NULL || scene == NULL || state == NULL ||
		digits == NULL || random == NULL || noise == NULL ||
		palette == NULL || canvas == NULL || canvas->pixels == NULL ||
		canvas->size == 0 || canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		menu->selected > MH_MENU_RACE_CHOICE_CATCH_UP ||
		menu->catch_up > 1 || menu->laps > MH_U32_MAX ||
		scene->mode > MH_GAME_NETWORK || scene->count == 0 ||
		scene->count > CATALOG_LIMIT || scene->unlocked == 0 ||
		scene->unlocked > scene->count || menu->track >= scene->count ||
		scene->names == NULL || scene->models == NULL ||
		scene->background_palette == NULL ||
		!(state->fade >= 0 && state->fade <= 1) ||
		state->catch_frame > SWITCH_LAST_FRAME ||
		(state->fade != 0 && state->shown_track >= scene->count) ||
		clock_ms > MH_U32_MAX || elapsed_ms > MH_U32_MAX ||
		*random > MH_U32_MAX || *noise > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race setup frame");

	next = *state;
	numbers = *digits;
	seed = *random;
	phase = *noise;
	x = y = 0;
	status = mh_menu_position(layout, RACE_LAYOUT, THUMBNAIL_SLOT, mix,
		&thumb_x, &thumb_y, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.fade = mh_menu_preview_advance(
		menu->track, elapsed_ms, &next.shown_track, next.fade);

	thumbnail = scene->thumbnails == NULL
		? NULL
		: &scene->thumbnails[next.shown_track];

	if (thumbnail != NULL && thumbnail->pixels == NULL)
		thumbnail = NULL;

	fade = (float)((long double)next.fade *
		(menu->track < scene->unlocked ? 1 : 0.5));
	status = mh_menu_palette_frame(art->palette, scene->background_palette,
		thumbnail == NULL ? NULL : thumbnail->palette, fade, colors,
		error_out);

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
			"out of memory composing race setup");
	}

	memcpy(work.pixels, canvas->pixels, work.size);
	status = mh_menu_thumbnail_draw(
		thumbnail, thumb_x, thumb_y, &work, error_out);

	if (next.catch_frame < menu->catch_up * SWITCH_LAST_FRAME)
		next.catch_frame++;

	if (next.catch_frame > menu->catch_up * SWITCH_LAST_FRAME)
		next.catch_frame--;

	animated = mh_menu_sprite_label_frame(clock_ms);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, RACE_LAYOUT, OUTLINE_SLOT,
			mix, &x, &y, error_out);

	center_x = (float)x;
	center_y = (float)y;
	outline_pose(clock_ms, next.fade, position, angles);
	model = &scene->models[menu->track];
	points = NULL;
	lines = NULL;

	if (status == MH_ERROR_NONE &&
		(model->point_count == 0 ||
			model->point_count > OUTLINE_LIMIT ||
			model->edge_count > OUTLINE_LIMIT))
		status = mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race setup outline");

	if (status == MH_ERROR_NONE) {
		points = (struct mh_menu_wire_point *)malloc(
			(size_t)model->point_count * sizeof(*points));
		lines = (struct mh_menu_line *)malloc(
			(size_t)(model->edge_count == 0 ? 1
							: model->edge_count) *
			sizeof(*lines));

		if (points == NULL || lines == NULL)
			status = mh_error_set(error_out, MH_ERROR_MEMORY,
				"out of memory projecting race setup outline");
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_wire_project_at(model, position, angles,
			center_x, center_y, points, model->point_count,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_wire_lines(model, points, OUTLINE_COLOR, lines,
			model->edge_count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_lines(lines, model->edge_count, OUTLINE_WIDTH,
			OUTLINE_HEIGHT, &work, error_out);

	free(points);
	free(lines);
	last = scene->mode == MH_GAME_TIME_ATTACK ? TIME_ATTACK_LAST_SLOT
						: LAST_ART_SLOT;

	for (i = 1; i <= last && status == MH_ERROR_NONE; i++) {
		if (i == menu->selected + LABEL_SLOT)
			frame = animated;
		else if (i == CATCH_UP_SLOT)
			frame = SWITCH_LAST_FRAME - next.catch_frame;
		else
			frame = 0;

		status = mh_menu_sprite_select(layout, RACE_LAYOUT, i, mix, 0,
			0, frame, &next.sprites, &seed, &commands, error_out);

		if (status != MH_ERROR_NONE || art->count[i] == 0)
			continue;

		if (art->count[i] > MH_MENU_RACE_ART_FRAMES ||
			frame >= art->count[i]) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"missing race setup artwork frame");
			break;
		}

		image = &art->images[i][frame];

		status = mh_menu_sprite_draw_commands(image, &commands, rows,
			ROW_CAPACITY, &phase, &work, 0, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, RACE_LAYOUT, TITLE_SLOT, mix,
			&x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = draw_centered(font,
			(const unsigned char *)scene->names[menu->track], x, y,
			&work, error_out);

	width = 0;

	if (status == MH_ERROR_NONE)
		for (i = 0; scene->names[menu->track][i] != 0; i++)
			width += font->advance[(
				unsigned char)scene->names[menu->track][i]];

	arrow[1] = 0;

	if (status == MH_ERROR_NONE && menu->track != 0 &&
		(clock_ms & ARROW_PHASE_MASK) < ARROW_HALF_PERIOD) {
		arrow[0] = ARROW_LEFT;
		status = draw_centered(font, arrow,
			x - (long)(width / 2) - ARROW_GAP, y, &work, error_out);
	}

	if (status == MH_ERROR_NONE && menu->track != scene->count - 1 &&
		(clock_ms & ARROW_PHASE_MASK) > ARROW_HALF_PERIOD) {
		arrow[0] = ARROW_RIGHT;
		status = draw_centered(font, arrow,
			x + (long)(width / 2) + ARROW_GAP, y, &work, error_out);
	}

	if (status == MH_ERROR_NONE && menu->track >= scene->unlocked &&
		(clock_ms & MESSAGE_PHASE_MASK) < MESSAGE_HALF_PERIOD)
		status = draw_centered(font,
			(const unsigned char *)"Not yet available", thumb_x,
			thumb_y - MESSAGE_RISE, &work, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(
			layout, RACE_LAYOUT, LAPS_SLOT, mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_digits_draw(&art->numa, &art->numb, menu->laps,
			LAP_DIGITS, x, y, elapsed_ms, snap_digits, &numbers,
			&work, error_out);

	if (status == MH_ERROR_NONE) {
		*state = next;
		*digits = numbers;
		*random = seed;
		*noise = phase;
		memcpy(palette, colors, MH_MENU_PALETTE_BYTES);
		memcpy(canvas->pixels, work.pixels, work.size);
	}

	free(work.pixels);
	free(rows);

	return status;
}
