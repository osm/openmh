#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/mode.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_ghost_draw.h"
#include "engine/ui/menu_position.h"
#include "interfaces/sprite/collection.h"

#define GHOST_LAYOUT 19
#define ART_NAME_COUNT 8
#define MARKER_ENTRY 7
#define LIST_ENTRY 6
#define LAST_ART_SLOT 6
#define ROW_CAPACITY 65536
#define RACE_LABEL_SLOT 4
#define DEMO_LABEL_SLOT 5
#define LIST_FRAME_SLOT 6
#define BENCHMARK_LABEL_SLOT 6
#define PATCHED_LIST_FRAME_SLOT 10
#define BENCHMARK_HINT_SLOT 12
#define BENCHMARK_HINT_HEIGHT 12
#define MARKER_FRAME_SLOT 20
#define MODE_SELECTED_FRAME 4
#define LIST_ORIGIN_SLOT 24
#define HINT_ORIGIN_SLOT 21
#define VISIBLE_ROWS 16

void
mh_menu_ghost_art_free(struct mh_menu_ghost_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_GHOST_ART_SLOTS; i++)
		for (j = 0; j < MH_MENU_GHOST_ART_FRAMES; j++)
			free(art->images[i][j].pixels);

	for (j = 0; j < MH_MENU_GHOST_ART_FRAMES; j++)
		free(art->marker[j].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_ghost_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_ghost_art *output, struct mh_error *error_out)
{
	static const char *const names[ART_NAME_COUNT] = {
		"dots", "scre", "ghost", "tgr", "trpr", "tbr", "fghos", "mupp"};

	struct mh_menu_ghost_art parsed;
	struct mh_sprite_info info;
	struct mh_sprite_indexed *images;
	enum mh_error_code status;
	unsigned int i, slot;
	unsigned int *count;
	struct mh_menu_art_reader reader = {0, "invalid Ghost menu sprite",
		"Ghost menu artwork exceeds capacity"};

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Ghost menu archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_sprite_collection_info(collection, "tbr", &info, error_out);

	if (status != MH_ERROR_NONE && status != MH_ERROR_NOT_FOUND)
		return status;

	parsed.patched = status == MH_ERROR_NONE;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < ART_NAME_COUNT && status == MH_ERROR_NONE; i++) {
		if (i == MARKER_ENTRY) {
			count = &parsed.marker_count;
			images = parsed.marker;
		} else {
			slot = i + 1;

			if (i == LIST_ENTRY)
				slot = parsed.patched ? PATCHED_LIST_FRAME_SLOT
						: LIST_FRAME_SLOT;

			count = &parsed.count[slot];
			images = parsed.images[slot];
		}

		status = mh_menu_art_read(&reader, collection, names[i], images,
			MH_MENU_GHOST_ART_FRAMES, MH_MENU_ART_ALL_FRAMES, count,
			error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_ghost_art_free(&parsed);

	return status;
}

void
mh_menu_ghost_animation_init(struct mh_menu_ghost_animation *state)
{
	if (state != NULL)
		memset(&state->sprites, 0, sizeof(state->sprites));
}

static void
apply_focus(struct mh_menu_ghost_state *menu,
	struct mh_menu_ghost_animation *state, unsigned long clock_ms)
{
	unsigned int list_slot;

	list_slot = menu->patched ? PATCHED_LIST_FRAME_SLOT : LIST_FRAME_SLOT;

	if (menu->focus == MH_MENU_GHOST_FOCUS_RACE) {
		menu->mode = MH_GAME_GHOST;
		menu->benchmark = 0;
	}

	if (menu->focus == MH_MENU_GHOST_FOCUS_DEMO) {
		menu->mode = MH_GAME_DEMO;
		menu->benchmark = 0;
	}

	if (menu->patched && menu->focus == MH_MENU_GHOST_FOCUS_BENCHMARK) {
		menu->mode = MH_GAME_DEMO;
		menu->benchmark = 1;
	}

	if (menu->mode == MH_GAME_GHOST || menu->mode == MH_GAME_DEMO) {
		state->frames[RACE_LABEL_SLOT] =
			menu->mode == MH_GAME_GHOST ? MODE_SELECTED_FRAME : 0;
		state->frames[DEMO_LABEL_SLOT] =
			menu->mode == MH_GAME_DEMO && !menu->benchmark
			? MODE_SELECTED_FRAME
			: 0;

		if (menu->patched)
			state->frames[BENCHMARK_LABEL_SLOT] =
				menu->mode == MH_GAME_DEMO && menu->benchmark
				? MODE_SELECTED_FRAME
				: 0;
	}

	state->frames[menu->focus + RACE_LABEL_SLOT] =
		mh_menu_sprite_label_frame(clock_ms);
	state->frames[list_slot] = state->frames[MARKER_FRAME_SLOT] =
		menu->focus == mh_menu_ghost_list_focus(menu) ? 0 : 1;
}

static enum mh_error_code
draw_art(const struct mh_menu_ghost_art *art,
	const struct mh_menu_layout *layout, unsigned int mix,
	struct mh_menu_ghost_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_sprite_row *rows,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_sprite_output commands;
	const struct mh_sprite_indexed *image;
	unsigned int i, j, frame, count, last;
	enum mh_error_code status;

	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);
	last = art->patched ? PATCHED_LIST_FRAME_SLOT : LAST_ART_SLOT;

	for (i = 1; i <= last && status == MH_ERROR_NONE; i++) {
		if (i > LAST_ART_SLOT && i != PATCHED_LIST_FRAME_SLOT)
			continue;

		frame = state->frames[i];
		status = mh_menu_sprite_select(layout, GHOST_LAYOUT, i, mix, 0,
			0, frame, &state->sprites, random, &commands,
			error_out);

		if (status != MH_ERROR_NONE || art->count[i] == 0)
			continue;

		if (art->count[i] > MH_MENU_GHOST_ART_FRAMES ||
			frame >= art->count[i]) {
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"missing Ghost menu frame");
			break;
		}

		image = &art->images[i][frame];

		for (j = 0; j < commands.count && status == MH_ERROR_NONE;
			j++) {
			status = mh_menu_sprite_rows(&commands.commands[j],
				image->height, canvas->pitch, noise, rows,
				ROW_CAPACITY, &count, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_menu_sprite_blit(image,
					&commands.commands[j], rows, count,
					canvas, error_out);
		}
	}

	return status;
}

static enum mh_error_code
draw_benchmark_hint(const struct mh_font *font,
	const struct mh_menu_layout *layout, unsigned int mix,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	static const unsigned char first[] =
		"Benchmark results will be saved in file:";
	static const unsigned char second[] =
		"'Fps.txt' in your Motorhead directory.";

	long x, y;
	enum mh_error_code status;

	status = mh_menu_position(layout, GHOST_LAYOUT, BENCHMARK_HINT_SLOT,
		mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(
			font, first, sizeof(first), x, y, 1, canvas, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(font, second, sizeof(second), x,
			y + BENCHMARK_HINT_HEIGHT, 1, canvas, error_out);

	return status;
}

static enum mh_error_code
draw_recordings(const struct mh_menu_ghost_art *art, const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_ghost_state *menu, const char *const *names,
	unsigned int mix, const struct mh_menu_ghost_animation *state,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	const struct mh_sprite_indexed *image;
	unsigned int frame;
	long x, y;
	enum mh_error_code status;

	status = mh_menu_position(
		layout, GHOST_LAYOUT, LIST_ORIGIN_SLOT, mix, &x, &y, error_out);
	image = NULL;

	if (art->marker_count != 0) {
		frame = state->frames[MARKER_FRAME_SLOT];

		if (art->marker_count > MH_MENU_GHOST_ART_FRAMES ||
			frame >= art->marker_count)
			status = mh_error_set(error_out, MH_ERROR_INVALID,
				"missing Ghost list marker frame");
		else
			image = &art->marker[frame];
	}

	if (status == MH_ERROR_NONE)
		status = mh_menu_list_draw(font, names, menu->count,
			menu->selected, VISIBLE_ROWS,
			menu->focus == mh_menu_ghost_list_focus(menu), image, x,
			y, canvas, error_out);

	return status;
}

enum mh_error_code
mh_menu_ghost_render(const struct mh_menu_ghost_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	struct mh_menu_ghost_state *menu, const char *const *names,
	unsigned int mix, unsigned long clock_ms, unsigned long elapsed_ms,
	struct mh_menu_ghost_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	static const unsigned char hint[] =
		"Press: [Delete] To remove race file.";
	struct mh_menu_ghost_animation next;
	struct mh_menu_ghost_state selection;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_canvas work;
	unsigned long seed, phase;
	long x, y;
	enum mh_error_code status;

	if (art == NULL || menu == NULL || state == NULL || random == NULL ||
		noise == NULL ||
		(menu->focus > MH_MENU_GHOST_FOCUS_BENCHMARK &&
			menu->focus != mh_menu_ghost_list_focus(menu)) ||
		menu->patched != art->patched || menu->mode > MH_GAME_DEMO ||
		menu->count > MH_MENU_DEMO_MAX_FILES || menu->pending_delete ||
		(menu->count == 0 ? menu->selected != 0
				: menu->selected >= menu->count) ||
		canvas == NULL || canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		*random > MH_U32_MAX || *noise > MH_U32_MAX ||
		clock_ms > MH_U32_MAX || elapsed_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Ghost menu frame");

	next = *state;
	selection = *menu;
	seed = *random;
	phase = *noise;
	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);
	rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*rows));

	if (work.pixels == NULL || rows == NULL) {
		free(work.pixels);
		free(rows);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing Ghost menu");
	}

	memcpy(work.pixels, canvas->pixels, work.size);
	status = draw_art(
		art, layout, mix, &next, &seed, &phase, rows, &work, error_out);

	apply_focus(&selection, &next, clock_ms);

	if (status == MH_ERROR_NONE && selection.patched &&
		selection.benchmark && selection.mode == MH_GAME_DEMO)
		status = draw_benchmark_hint(
			font, layout, mix, &work, error_out);

	if (status == MH_ERROR_NONE && menu->count != 0)
		status = draw_recordings(art, font, layout, menu, names, mix,
			&next, &work, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, GHOST_LAYOUT,
			HINT_ORIGIN_SLOT, mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(
			font, hint, sizeof(hint), x, y, 1, &work, error_out);

	if (status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, work.pixels, work.size);
		*menu = selection;
		*state = next;
		*random = seed;
		*noise = phase;
	}

	free(work.pixels);
	free(rows);

	return status;
}
