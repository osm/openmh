#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_car_draw.h"
#include "engine/ui/menu_gauge.h"
#include "engine/ui/menu_palette.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_preview.h"
#include "engine/ui/menu_sine.h"
#include "engine/ui/menu_text.h"
#include "engine/ui/menu_triangles.h"
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
#define CAR_LAYOUT 3
#define HORN_LIMIT 128
#define STAGE_LIMIT 3
#define LEAGUE_DIVISIONS 4
#define DIVISION_BASE 100
#define STATISTIC_LIMIT 1000000
#define SPRITES_BEFORE_GAUGES 17
#define SPRITES_TOTAL 20
#define AUTOMATIC_SLOT 17
#define RECORD_SLOT 18
#define LABEL_SLOT 10
#define GAUGE_SLOT 6
#define GAUGE_COLOR 4
#define TITLE_SLOT 24
#define HORN_SLOT 19
#define OUTLINE_SLOT 29
#define THUMBNAIL_SLOT 30
#define OUTLINE_CLIP_RADIUS 200

void
mh_menu_car_art_free(struct mh_menu_car_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_LAYOUT_SLOTS; i++)
		for (j = 0; j < MH_MENU_CAR_ART_FRAMES; j++)
			free(art->images[i][j].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_car_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_car_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_LAYOUT_SLOTS] = {NULL, "dots",
		"car", "scr8", "carc", "cars", "carx", "cary", "carz", "carop",
		"tcar", "ttran", "thorn", "trr", "cas", "caa", "cag", "tman",
		"onoff", NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, "card",
		NULL, NULL, NULL, "caro2"};

	struct mh_menu_car_art parsed;
	enum mh_error_code status;
	struct mh_menu_art_reader reader = {0, "invalid car setup sprite",
		"car setup artwork exceeds capacity"};

	unsigned int i;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car setup archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < MH_MENU_LAYOUT_SLOTS && status == MH_ERROR_NONE; i++) {
		if (names[i] == NULL)
			continue;

		status = mh_menu_art_read(&reader, collection, names[i],
			parsed.images[i], MH_MENU_CAR_ART_FRAMES,
			MH_MENU_ART_ALL_FRAMES, &parsed.count[i], error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_palette_load(
			collection, parsed.palette, error_out);

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_car_art_free(&parsed);

	return status;
}

void
mh_menu_car_animation_init(struct mh_menu_car_animation *state)
{
	if (state == NULL)
		return;

	memset(&state->sprites, 0, sizeof(state->sprites));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
	state->fade = 0;
}

static enum mh_error_code
draw_label(const struct mh_font *font, const char *text, long x, long y,
	int centered, unsigned int *width, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	unsigned int i;

	if (font == NULL || text == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing car setup text");

	*width = 0;

	for (i = 0; i < NAME_BYTES && text[i] != 0; i++)
		*width += font->advance[(unsigned char)text[i]];

	if (i == NAME_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated car setup text");

	return mh_menu_text_draw(font, (const unsigned char *)text, i + 1,
		x - (centered ? (long)(*width / 2) : 0), y, 1, canvas,
		error_out);
}

static enum mh_error_code
draw_sprite(const struct mh_menu_car_art *art,
	const struct mh_menu_layout *layout, unsigned int slot,
	unsigned int mix, unsigned int frame,
	struct mh_menu_car_animation *state, unsigned long *seed,
	unsigned long *noise, struct mh_menu_sprite_row *rows,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_sprite_output commands;
	const struct mh_sprite_indexed *image;
	enum mh_error_code status;
	unsigned int j, count;

	status = mh_menu_sprite_select(layout, CAR_LAYOUT, slot, mix, 0, 0,
		frame, &state->sprites, seed, &commands, error_out);

	if (status != MH_ERROR_NONE || art->count[slot] == 0)
		return status;

	if (art->count[slot] > MH_MENU_CAR_ART_FRAMES ||
		frame >= art->count[slot])
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing car setup artwork frame");

	image = &art->images[slot][frame];

	for (j = 0; j < commands.count && status == MH_ERROR_NONE; j++) {
		status = mh_menu_sprite_rows(&commands.commands[j],
			image->height, canvas->pitch, noise, rows, ROW_CAPACITY,
			&count, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_menu_sprite_blit(image,
				&commands.commands[j], rows, count, canvas,
				error_out);
	}

	return status;
}

static enum mh_error_code
draw_statistics(const struct mh_menu_layout *layout, unsigned int mix,
	const float statistics[MH_MENU_CAR_STATISTICS], float rate,
	int no_subpixel, float displayed[MH_MENU_CAR_STATISTICS],
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	static const float minimum[MH_MENU_CAR_STATISTICS] = {250, 2.2f, 3.5f},
			maximum[MH_MENU_CAR_STATISTICS] = {320, 3.2f, 8.5f};

	static const float radius[MH_MENU_CAR_STATISTICS] = {
		41.5f, 35.5f, 25.5f};

	static const long dx[MH_MENU_CAR_STATISTICS] = {49, 42, 32},
			dy[MH_MENU_CAR_STATISTICS] = {44, 35, 30};

	struct mh_menu_gauge_draw gauge;
	unsigned int i;
	float difference, value;
	long double follower, adjusted;
	long x, y;
	enum mh_error_code status;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < MH_MENU_CAR_STATISTICS && status == MH_ERROR_NONE;
		i++) {
		follower = ((long double)statistics[i] - displayed[i]) * rate +
			displayed[i];
		displayed[i] = (float)follower;

		/*
		 * The original subtracts before reloading the stored follower.
		 */
		difference = (float)(follower - minimum[i]);

		if (difference < 0)
			difference = 0;

		adjusted = (long double)difference /
			((long double)maximum[i] - minimum[i]);
		value = (float)(i == MH_MENU_CAR_ACCELERATION ? 1 - adjusted
							: adjusted);
		status = mh_menu_position(layout, CAR_LAYOUT, GAUGE_SLOT + i,
			mix, &x, &y, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_menu_gauge((float)(x + dx[i]),
				(float)(y + dy[i]), radius[i], value,
				GAUGE_COLOR, &gauge, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_menu_triangles(gauge.points, gauge.count,
				gauge.color, no_subpixel, canvas, error_out);
	}

	return status;
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
	sine_phase =
		(unsigned int)(unsigned long)(turn + MH_MENU_SINE_QUARTER_TURN);
	angles[0] =
		(unsigned long)(long)((long double)mh_menu_sine(sine_phase) *
			250);
	angles[2] = 0;
	position[0] = 0;
	position[1] = 105;
	position[2] = (float)((1 - (long double)fade) * 2000 + 400);
}

enum mh_error_code
mh_menu_car_render(const struct mh_menu_car_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_car_setup_state *menu,
	const struct mh_menu_car_scene *scene, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms, int no_subpixel,
	struct mh_menu_car_animation *state, unsigned long *random,
	unsigned long *noise, unsigned char palette[MH_MENU_PALETTE_BYTES],
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	static const unsigned int slots[SPRITES_TOTAL] = {1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 17, 18, 27, 31, 14, 15, 16};

	struct mh_menu_car_animation next;
	struct mh_menu_canvas work;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_wire_point *points;
	struct mh_menu_line *lines;
	const struct mh_wireframe *model;
	const struct mh_menu_thumbnail *thumbnail;
	enum mh_error_code status;
	const char *message;
	unsigned char colors[MH_MENU_PALETTE_BYTES];
	char arrow[2];
	unsigned long seed, phase, angles[3];
	unsigned int i, slot, frame, animated, width, ignored, target,
		brightness;
	float position[3], dim, fade, rate;
	long x, y, thumb_x, thumb_y, center_x, center_y;

	if (art == NULL || font == NULL || menu == NULL || scene == NULL ||
		state == NULL || random == NULL || noise == NULL ||
		palette == NULL || canvas == NULL || canvas->pixels == NULL ||
		canvas->size == 0 || canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		menu->selected > MH_MENU_CAR_CHOICE_RECORD ||
		menu->automatic > 1 || menu->record > 1 ||
		scene->mode > MH_GAME_NETWORK || scene->stage > STAGE_LIMIT ||
		scene->catalog.count == 0 ||
		scene->catalog.count > CATALOG_LIMIT ||
		scene->catalog.visible == 0 ||
		scene->catalog.visible > scene->catalog.count ||
		scene->catalog.unlocked == 0 ||
		scene->catalog.unlocked > scene->catalog.count ||
		scene->catalog.divisions == NULL ||
		menu->car >= scene->catalog.count || scene->horn_count == 0 ||
		scene->horn_count > HORN_LIMIT ||
		menu->horn >= scene->horn_count || scene->names == NULL ||
		scene->horn_names == NULL || scene->statistics == NULL ||
		scene->models == NULL || scene->background_palette == NULL ||
		!(state->fade >= 0 && state->fade <= 1) ||
		state->automatic_frame > SWITCH_LAST_FRAME ||
		state->record_frame > SWITCH_LAST_FRAME ||
		(state->fade != 0 &&
			state->shown_car >= scene->catalog.count) ||
		clock_ms > MH_U32_MAX || elapsed_ms > MH_U32_MAX ||
		*random > MH_U32_MAX || *noise > MH_U32_MAX ||
		(no_subpixel != 0 && no_subpixel != 1))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid car setup frame");

	if (scene->catalog.divisions[menu->car] > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid car division");

	for (i = 0; i < MH_MENU_CAR_STATISTICS; i++)
		if (!(state->statistics[i] >= -STATISTIC_LIMIT &&
			state->statistics[i] <= STATISTIC_LIMIT) ||
			!(scene->statistics[menu->car][i] >= -STATISTIC_LIMIT &&
				scene->statistics[menu->car][i] <=
					STATISTIC_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid car setup statistic");

	next = *state;
	seed = *random;
	phase = *noise;
	x = y = 0;
	status = mh_menu_position(layout, CAR_LAYOUT, OUTLINE_SLOT, mix,
		&center_x, &center_y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, CAR_LAYOUT, THUMBNAIL_SLOT,
			mix, &thumb_x, &thumb_y, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (next.automatic_frame < menu->automatic * SWITCH_LAST_FRAME)
		next.automatic_frame++;

	if (next.automatic_frame > menu->automatic * SWITCH_LAST_FRAME)
		next.automatic_frame--;

	target = menu->record == 1 ? 0 : SWITCH_LAST_FRAME;

	if (next.record_frame < target)
		next.record_frame++;

	if (next.record_frame > target)
		next.record_frame--;

	outline_pose(clock_ms, next.fade, position, angles);
	brightness = (unsigned int)((long double)next.fade * 32.0f);
	model = &scene->models[menu->car];

	if (model->point_count == 0 || model->point_count > OUTLINE_LIMIT ||
		model->edge_count > OUTLINE_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car setup outline");

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);
	rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*rows));
	points = (struct mh_menu_wire_point *)malloc(
		(size_t)model->point_count * sizeof(*points));
	lines = (struct mh_menu_line *)malloc(
		(size_t)(model->edge_count == 0 ? 1 : model->edge_count) *
		sizeof(*lines));

	if (work.pixels == NULL || rows == NULL || points == NULL ||
		lines == NULL) {
		free(work.pixels);
		free(rows);
		free(points);
		free(lines);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing car setup");
	}

	memcpy(work.pixels, canvas->pixels, work.size);
	status = mh_menu_wire_project_at(model, position, angles,
		(float)center_x, (float)center_y, points, model->point_count,
		error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_wire_lines(model, points, brightness, lines,
			model->edge_count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_lines_rect(lines, model->edge_count,
			center_x - OUTLINE_CLIP_RADIUS,
			center_y - OUTLINE_CLIP_RADIUS,
			center_x + OUTLINE_CLIP_RADIUS,
			center_y + OUTLINE_CLIP_RADIUS, &work, error_out);

	free(points);
	free(lines);
	animated = mh_menu_sprite_label_frame(clock_ms);
	memset(next.frames, 0, sizeof(next.frames));
	next.frames[AUTOMATIC_SLOT] = next.automatic_frame;
	next.frames[RECORD_SLOT] = next.record_frame;
	next.frames[menu->selected + LABEL_SLOT] = animated;

	/* Test the OLD shown index, before zero fade changes it. */
	dim = next.shown_car < scene->catalog.unlocked ? 1 : 0.3f;

	next.fade = mh_menu_preview_advance(
		menu->car, elapsed_ms, &next.shown_car, next.fade);

	thumbnail = scene->thumbnails == NULL
		? NULL
		: &scene->thumbnails[next.shown_car];

	if (thumbnail != NULL && thumbnail->pixels == NULL)
		thumbnail = NULL;

	fade = (float)((long double)next.fade * dim);

	if (status == MH_ERROR_NONE)
		status = mh_menu_palette_frame(art->palette,
			scene->background_palette,
			thumbnail == NULL ? NULL : thumbnail->palette, fade,
			colors, error_out);

	memcpy(colors + MH_MENU_PALETTE_BYTES - 3,
		scene->background_palette + MH_MENU_PALETTE_SLICE_BYTES - 3, 3);

	if (status == MH_ERROR_NONE)
		status = mh_menu_thumbnail_draw(
			thumbnail, thumb_x, thumb_y, &work, error_out);

	for (i = 0; i < SPRITES_BEFORE_GAUGES && status == MH_ERROR_NONE; i++) {
		slot = slots[i];
		frame = next.frames[slot];
		status = draw_sprite(art, layout, slot, mix, frame, &next,
			&seed, &phase, rows, &work, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(
			layout, CAR_LAYOUT, TITLE_SLOT, mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = draw_label(font, scene->names[menu->car], x, y, 1,
			&width, &work, error_out);

	arrow[1] = 0;

	if (status == MH_ERROR_NONE && menu->car != 0 &&
		(clock_ms & ARROW_PHASE_MASK) < ARROW_HALF_PERIOD) {
		arrow[0] = (char)ARROW_LEFT;
		status = draw_label(font, arrow,
			x - (long)(width / 2) - ARROW_GAP, y, 1, &ignored,
			&work, error_out);
	}

	if (status == MH_ERROR_NONE &&
		menu->car != scene->catalog.visible - 1 &&
		(clock_ms & ARROW_PHASE_MASK) > ARROW_HALF_PERIOD) {
		arrow[0] = (char)ARROW_RIGHT;
		status = draw_label(font, arrow,
			x + (long)(width / 2) + ARROW_GAP, y, 1, &ignored,
			&work, error_out);
	}

	message = NULL;

	if ((clock_ms & MESSAGE_PHASE_MASK) < MESSAGE_HALF_PERIOD) {
		if (dim != 1)
			message = "Not yet available";
		else if (scene->mode == MH_GAME_LEAGUE &&
			scene->catalog.divisions[menu->car] / DIVISION_BASE >
				LEAGUE_DIVISIONS - scene->stage)
			message = "Not available in division";
	}

	if (status == MH_ERROR_NONE && message != NULL)
		status = draw_label(font, message, thumb_x,
			thumb_y - MESSAGE_RISE, 1, &ignored, &work, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(
			layout, CAR_LAYOUT, HORN_SLOT, mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = draw_label(font, scene->horn_names[menu->horn], x, y,
			0, &ignored, &work, error_out);

	rate = (float)((long double)elapsed_ms * 0.08f * 0.0625);

	if (status == MH_ERROR_NONE)
		status = draw_statistics(layout, mix,
			scene->statistics[menu->car], rate, no_subpixel,
			next.statistics, &work, error_out);

	for (i = SPRITES_BEFORE_GAUGES;
		i < SPRITES_TOTAL && status == MH_ERROR_NONE; i++)
		status = draw_sprite(art, layout, slots[i], mix, 0, &next,
			&seed, &phase, rows, &work, error_out);

	if (status == MH_ERROR_NONE) {
		*state = next;
		*random = seed;
		*noise = phase;
		memcpy(palette, colors, MH_MENU_PALETTE_BYTES);
		memcpy(canvas->pixels, work.pixels, work.size);
	}

	free(work.pixels);
	free(rows);

	return status;
}
