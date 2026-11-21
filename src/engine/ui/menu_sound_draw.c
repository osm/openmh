#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_needle.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_sound_draw.h"
#include "engine/ui/menu_text.h"
#include "engine/ui/menu_triangles.h"
#include "interfaces/sprite/collection.h"

#define SOUND_LAYOUT 6
#define COURSE_ORIGIN_SLOT 11
#define LOOP_SLOT 10
#define LABEL_SLOT 15
#define EFFECTS_NEEDLE_SLOT 24
#define MUSIC_NEEDLE_SLOT 25
#define NEEDLE_LEFT_SLOT 26
#define NEEDLE_RIGHT_SLOT 27
#define NEEDLE_ANGLE_SCALE 8
#define VOLUME_TO_FRACTION ((double)(1.0 / MH_MENU_SOUND_VOLUME_MAX))
#define ROW_CAPACITY 65536UL
#define COURSE_NAME_BYTES 256
#define PHYSICAL_TRACK_COUNT 256
#define LOOP_LAST_FRAME 7

void
mh_menu_sound_art_free(struct mh_menu_sound_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_SOUND_ART_SLOTS; i++)
		for (j = 0; j < MH_MENU_SOUND_ART_FRAMES; j++)
			free(art->images[i][j].pixels);

	free(art->numa.pixels);
	free(art->numb.pixels);
	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_sound_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_sound_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_SOUND_ART_SLOTS] = {NULL, "dots",
		"opt", "scr2", "snd1", "snd4", "snd3", NULL, "sndm", "sndm",
		"onoff", NULL, "sndt", "snda", "sndc", "tsfx", "tcdm", "tcdl",
		"sndb", "sndak", "sndck", "sndtk"};

	struct mh_menu_sound_art parsed;
	unsigned int i;
	struct mh_menu_art_reader reader = {0, "invalid sound options sprite",
		"sound options artwork exceeds capacity"};

	enum mh_error_code status;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound options archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i < MH_MENU_SOUND_ART_SLOTS && status == MH_ERROR_NONE; i++)
		if (names[i] != NULL)
			status = mh_menu_art_read(&reader, collection, names[i],
				parsed.images[i], MH_MENU_SOUND_ART_FRAMES,
				MH_MENU_ART_ALL_FRAMES, &parsed.count[i],
				error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_art_read(&reader, collection, "numa",
			&parsed.numa, 1, MH_MENU_ART_ALL_FRAMES, NULL,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_art_read(&reader, collection, "numb",
			&parsed.numb, 1, MH_MENU_ART_ALL_FRAMES, NULL,
			error_out);

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_sound_art_free(&parsed);

	return status;
}

void
mh_menu_sound_animation_init(struct mh_menu_sound_animation *state)
{
	if (state == NULL)
		return;

	memset(&state->sprites, 0, sizeof(state->sprites));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
}

static enum mh_error_code
draw_volume_slider(const struct mh_menu_layout *layout, unsigned int slot,
	unsigned int mix, unsigned int volume, unsigned int color,
	int no_subpixel, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct mh_menu_needle needle;
	struct mh_menu_needle_draw draw;
	long left, right, unused, x, y;
	volatile float angle;
	enum mh_error_code status;

	status = mh_menu_position(layout, SOUND_LAYOUT, NEEDLE_LEFT_SLOT, mix,
		&left, &unused, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, SOUND_LAYOUT,
			NEEDLE_RIGHT_SLOT, mix, &right, &unused, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(
			layout, SOUND_LAYOUT, slot, mix, &x, &y, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	angle = (float)((long double)(right - left) * NEEDLE_ANGLE_SCALE *
		(float)volume * VOLUME_TO_FRACTION);
	angle = (float)((long double)angle + left * NEEDLE_ANGLE_SCALE);

	/*
	 * Start at the target angle so the needle immediately shows the volume.
	 */
	status = mh_menu_needle_init(&needle, angle, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_needle_step(&needle, (float)x, (float)y, angle,
			1, 3, color, 0, &draw, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menu_triangles(
		draw.points, 2, draw.color, no_subpixel, canvas, error_out);
}

static enum mh_error_code
draw_course(const struct mh_menu_sound_art *art, const struct mh_font *font,
	const struct mh_menu_sound_scene *scene, unsigned int index, long x,
	long y, unsigned int color, unsigned long elapsed_ms, unsigned int snap,
	struct mh_menu_digits *digits, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i;
	unsigned long seconds, track;

	if (scene->names[index] == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing sound options course name");

	for (i = 0; i < COURSE_NAME_BYTES && scene->names[index][i] != 0; i++) {
	}

	if (i == COURSE_NAME_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated sound options course name");

	status = mh_menu_text_draw(font,
		(const unsigned char *)scene->names[index], i + 1, x + 20,
		y + 2, color, canvas, error_out);
	track = scene->tracks[index];
	seconds = track < scene->seconds_count ? scene->seconds[track] : 0;

	if (status == MH_ERROR_NONE)
		status = mh_menu_digits_draw(&art->numa, &art->numb, track, 2,
			x + 183, y - 2, elapsed_ms, snap, digits, canvas,
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_digits_draw(&art->numa, &art->numb,
			(seconds / 60) % 100, 2, x + 304, y - 2, elapsed_ms,
			snap, digits, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menu_digits_draw(&art->numa, &art->numb, seconds % 60, 2,
		x + 360, y - 2, elapsed_ms, snap, digits, canvas, error_out);
}

enum mh_error_code
mh_menu_sound_render(const struct mh_menu_sound_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	struct mh_menu_sound_state *menu,
	const struct mh_menu_sound_values *values,
	const struct mh_menu_sound_scene *scene, unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms, int no_subpixel,
	struct mh_menu_sound_animation *state, struct mh_menu_digits *digits,
	unsigned long *random, unsigned long *noise,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_sound_animation next;
	struct mh_menu_digits numbers;
	struct mh_menu_sprite_output commands;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_canvas work;
	const struct mh_sprite_indexed *image;
	enum mh_error_code status;
	unsigned long seed, phase;
	unsigned int frames[MH_MENU_SOUND_ART_SLOTS], i, frame, target;
	long x, y;

	if (art == NULL || font == NULL || menu == NULL || values == NULL ||
		scene == NULL || state == NULL || digits == NULL ||
		random == NULL || noise == NULL || canvas == NULL ||
		canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		menu->selected > MH_MENU_SOUND_MODE || menu->list_changed > 1 ||
		values->effects > MH_MENU_SOUND_VOLUME_MAX ||
		values->music > MH_MENU_SOUND_VOLUME_MAX || values->loop > 1 ||
		state->loop_frame > LOOP_LAST_FRAME || scene->count == 0 ||
		scene->count > MH_MENU_SOUND_COURSE_LIMIT ||
		menu->course >= scene->count || scene->names == NULL ||
		scene->tracks == NULL ||
		scene->seconds_count > PHYSICAL_TRACK_COUNT ||
		(scene->seconds_count != 0 && scene->seconds == NULL) ||
		clock_ms > MH_U32_MAX || elapsed_ms > MH_U32_MAX ||
		*random > MH_U32_MAX || *noise > MH_U32_MAX ||
		(no_subpixel != 0 && no_subpixel != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sound options frame");

	for (i = 0; i < scene->count; i++)
		if (scene->tracks[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid sound options track word");

	for (i = 0; i < scene->seconds_count; i++)
		if (scene->seconds[i] > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid sound options track duration");

	status = mh_menu_position(layout, SOUND_LAYOUT, COURSE_ORIGIN_SLOT, mix,
		&x, &y, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *state;
	numbers = *digits;
	seed = *random;
	phase = *noise;
	memset(frames, 0, sizeof(frames));

	if (menu->selected == MH_MENU_SOUND_COURSE)
		frames[19] = frames[20] = frames[21] = 1;

	target = (1 - values->loop) * LOOP_LAST_FRAME;

	if (next.loop_frame < target)
		next.loop_frame++;

	if (next.loop_frame > target)
		next.loop_frame--;

	frames[LOOP_SLOT] = next.loop_frame;

	if (menu->selected != MH_MENU_SOUND_MODE)
		frames[LABEL_SLOT + menu->selected] =
			mh_menu_sprite_label_frame(clock_ms);

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);
	rows = (struct mh_menu_sprite_row *)malloc(
		ROW_CAPACITY * sizeof(*rows));

	if (work.pixels == NULL || rows == NULL) {
		free(work.pixels);

		free(rows);

		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing sound options");
	}

	memcpy(work.pixels, canvas->pixels, work.size);

	for (i = 1; i < MH_MENU_SOUND_ART_SLOTS && status == MH_ERROR_NONE;
		i++) {
		frame = frames[i];
		status = mh_menu_sprite_select(layout, SOUND_LAYOUT, i, mix, 0,
			0, frame, &next.sprites, &seed, &commands, error_out);

		if (status != MH_ERROR_NONE || art->count[i] == 0)
			continue;

		if (art->count[i] > MH_MENU_SOUND_ART_FRAMES ||
			frame >= art->count[i]) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"missing sound options frame");
			break;
		}

		image = &art->images[i][frame];

		status = mh_menu_sprite_draw_commands(image, &commands, rows,
			ROW_CAPACITY, &phase, &work, 0, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = draw_volume_slider(layout, EFFECTS_NEEDLE_SLOT, mix,
			values->effects,
			menu->selected == MH_MENU_SOUND_EFFECTS ? 2 : 1,
			no_subpixel, &work, error_out);

	if (status == MH_ERROR_NONE)
		status = draw_volume_slider(layout, MUSIC_NEEDLE_SLOT, mix,
			values->music,
			menu->selected == MH_MENU_SOUND_MUSIC ? 2 : 1,
			no_subpixel, &work, error_out);

	x += 53;
	y += 93;

	if (status == MH_ERROR_NONE)
		status = draw_course(art, font, scene, menu->course, x, y,
			menu->selected == MH_MENU_SOUND_COURSE ? 2 : 1,
			elapsed_ms, menu->list_changed, &numbers, &work,
			error_out);

	if (status == MH_ERROR_NONE && menu->course != 0)
		status = draw_course(art, font, scene, menu->course - 1, x,
			y - 30, 1, elapsed_ms, menu->list_changed, &numbers,
			&work, error_out);

	if (status == MH_ERROR_NONE && menu->course + 1 < scene->count)
		status = draw_course(art, font, scene, menu->course + 1, x,
			y + 30, 1, elapsed_ms, menu->list_changed, &numbers,
			&work, error_out);

	if (status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, work.pixels, work.size);
		*state = next;
		*digits = numbers;
		*random = seed;
		*noise = phase;
		menu->list_changed = 0;
	}

	free(work.pixels);
	free(rows);

	return status;
}
