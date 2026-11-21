#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_multi_draw.h"
#include "engine/ui/menu_position.h"
#include "interfaces/sprite/collection.h"

#define MULTI_LAYOUT 9
#define TCP_IP_PROTOCOL 1
#define ROW_CAPACITY 65536
#define CATALOG_TEXT_BYTES 256
#define LIST_VISIBLE_ROWS 8
#define CURSOR_WIDTH_LIMIT 16711425UL
#define LAST_COMMON_ART_SLOT 3
#define JOIN_LABEL_SLOT 4
#define CREATE_LABEL_SLOT 5
#define CREATE_START_SLOT 6
#define ADDRESS_IP_LAYOUT_SLOT 9
#define ADDRESS_FRAME_SLOT 18
#define SEARCH_FRAME_SLOT 19
#define MARKER_SLOT 20
#define SESSION_LIST_SLOT 20
#define ADDRESS_LIST_SLOT 21
#define ADDRESS_INFO_SLOT 22
#define ADDRESS_INFO_OFFSET_SLOT 23
#define VALUE_OFFSET_SLOT 24
#define SEARCH_NOTICE_SLOT 25
#define PROTECTED_LIST_SLOT 27
#define SESSION_FRAME_SLOT 28
#define ADD_VALUE_X_SLOT 29
#define ADD_ORIGIN_SLOT 30

struct drawing {
	const struct mh_menu_multi_art	*art;
	const struct mh_font		*font;
	const struct mh_menu_layout	*layout;
	struct mh_menu_multi_animation	animation;
	struct mh_menu_sprite_row	*rows;
	struct mh_menu_canvas		canvas;
	unsigned long			seed, noise;
	unsigned int			mix;
	long	x[MH_MENU_LAYOUT_SLOTS], y[MH_MENU_LAYOUT_SLOTS];

	/* Preserve the first failure while composing scratch outputs. */
	enum mh_error_code		status;
	struct mh_error			*error_out;
};

void
mh_menu_multi_art_free(struct mh_menu_multi_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MULTI_ART_SLOTS; i++)
		for (j = 0; j < MH_MULTI_ART_FRAMES; j++)
			free(art->images[i][j].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_multi_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_multi_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MULTI_ART_SLOTS] = {NULL, "dots",
		"scrb", "multi", "tjoin", "tcrea", "tmsta", "tmssn", "tmssp",
		NULL, NULL, NULL, NULL, NULL, NULL, "tmdes", "tmipa", NULL,
		"faddr", "flsea", "mupp", NULL, NULL, NULL, NULL, NULL, NULL,
		NULL, "fsess"};

	struct mh_menu_multi_art parsed;
	enum mh_error_code status;
	unsigned int i;
	struct mh_menu_art_reader reader = {0, "invalid multiplayer sprite",
		"multiplayer artwork exceeds capacity"};

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid multiplayer artwork archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i < MH_MULTI_ART_SLOTS && status == MH_ERROR_NONE; i++) {
		if (names[i] == NULL)
			continue;

		status = mh_menu_art_read(&reader, collection, names[i],
			parsed.images[i], MH_MULTI_ART_FRAMES,
			MH_MENU_ART_ALL_FRAMES, &parsed.count[i], error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_multi_art_free(&parsed);

	return status;
}

void
mh_menu_multi_animation_init(struct mh_menu_multi_animation *state)
{
	if (state == NULL)
		return;

	memset(state, 0, sizeof(*state));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
}

static void
draw_sprite(struct drawing *draw, unsigned int slot, long dx, long dy)
{
	struct mh_menu_sprite_output commands;
	const struct mh_sprite_indexed *image;
	unsigned int frame;

	if (draw->status != MH_ERROR_NONE)
		return;

	frame = draw->animation.frames[slot];
	draw->status = mh_menu_sprite_select(draw->layout, MULTI_LAYOUT, slot,
		draw->mix, dx, dy, frame, &draw->animation.sprites, &draw->seed,
		&commands, draw->error_out);

	if (draw->status != MH_ERROR_NONE || draw->art->count[slot] == 0)
		return;

	if (draw->art->count[slot] > MH_MULTI_ART_FRAMES ||
		frame >= draw->art->count[slot]) {
		draw->status = mh_error_set(draw->error_out, MH_ERROR_INVALID,
			"missing multiplayer frame");
		return;
	}

	image = &draw->art->images[slot][frame];

	draw->status = mh_menu_sprite_draw_commands(image, &commands,
		draw->rows, ROW_CAPACITY, &draw->noise, &draw->canvas, 0,
		draw->error_out);
}

static void
draw_text(struct drawing *draw, const char *value, size_t capacity, long x,
	long y, unsigned int color)
{
	if (draw->status == MH_ERROR_NONE)
		draw->status = mh_menu_text_draw(draw->font,
			(const unsigned char *)value, capacity, x, y, color,
			&draw->canvas, draw->error_out);
}

static void
draw_list(struct drawing *draw, const char *const *names, unsigned int count,
	unsigned int selected, unsigned int slot, int active)
{
	const struct mh_sprite_indexed *marker;
	unsigned int frame;

	if (draw->status != MH_ERROR_NONE)
		return;

	frame = active ? 0 : 1;
	draw->animation.frames[MARKER_SLOT] = frame;
	marker = NULL;

	if (draw->art->count[MARKER_SLOT] != 0) {
		if (draw->art->count[MARKER_SLOT] > MH_MULTI_ART_FRAMES ||
			frame >= draw->art->count[MARKER_SLOT]) {
			draw->status = mh_error_set(draw->error_out,
				MH_ERROR_INVALID, "missing multiplayer marker");
			return;
		}

		marker = &draw->art->images[MARKER_SLOT][frame];
	}

	draw->status = mh_menu_record_list_draw(draw->font, names, count,
		selected, LIST_VISIBLE_ROWS, active, marker, draw->x[slot],
		draw->y[slot], &draw->canvas, draw->error_out);
}

static int
valid_text(const char *value)
{
	unsigned int i;

	if (value == NULL)
		return 0;

	for (i = 0; i < CATALOG_TEXT_BYTES; i++)
		if (value[i] == 0)
			return 1;

	return 0;
}

static void
draw_create(struct drawing *draw, const struct mh_menu_multi_state *menu,
	const struct mh_settings *settings, unsigned int label_frame)
{
	unsigned int i;

	if (menu->phase == MH_MULTI_EDIT)
		draw->animation.frames[CREATE_START_SLOT + menu->create_row] =
			label_frame;

	for (i = CREATE_START_SLOT; i <= MH_MULTI_SESSION_PASSWORD_SLOT; i++)
		draw_sprite(draw, i, 0, 0);

	draw_text(draw, settings->session_name, sizeof(settings->session_name),
		draw->x[MH_MULTI_SESSION_NAME_SLOT] +
			draw->x[VALUE_OFFSET_SLOT],
		draw->y[MH_MULTI_SESSION_NAME_SLOT] +
			draw->y[VALUE_OFFSET_SLOT],
		menu->create_row == MH_MULTI_CREATE_NAME ? 2 : 1);
	draw_text(draw, settings->session_password,
		sizeof(settings->session_password),
		draw->x[MH_MULTI_SESSION_PASSWORD_SLOT] +
			draw->x[VALUE_OFFSET_SLOT],
		draw->y[MH_MULTI_SESSION_PASSWORD_SLOT] +
			draw->y[VALUE_OFFSET_SLOT],
		menu->create_row == MH_MULTI_CREATE_PASSWORD ? 2 : 1);
}

static void
draw_address_form(struct drawing *draw, const struct mh_menu_multi_state *menu)
{
	long x, y;
	unsigned int name_color, address_color;

	x = draw->x[VALUE_OFFSET_SLOT] + draw->x[ADD_VALUE_X_SLOT];
	y = draw->y[VALUE_OFFSET_SLOT] + draw->y[ADD_ORIGIN_SLOT];
	name_color = menu->address_row == MH_MULTI_ADDRESS_NAME ? 2 : 1;
	address_color = menu->address_row == MH_MULTI_ADDRESS_IP ? 2 : 1;
	draw_text(draw, "Name:", sizeof("Name:"),
		draw->x[MH_MULTI_ADDRESS_NAME_SLOT] + draw->x[ADD_ORIGIN_SLOT],
		draw->y[MH_MULTI_ADDRESS_NAME_SLOT] + y, name_color);
	draw_text(draw, menu->address_name, sizeof(menu->address_name),
		draw->x[MH_MULTI_ADDRESS_NAME_SLOT] + x,
		draw->y[MH_MULTI_ADDRESS_NAME_SLOT] + y, name_color);
	draw_text(draw, "Server address:", sizeof("Server address:"),
		draw->x[MH_MULTI_IP_ADDRESS_SLOT] + draw->x[ADD_ORIGIN_SLOT],
		draw->y[MH_MULTI_IP_ADDRESS_SLOT] + y, address_color);
	draw_text(draw, menu->ip_address, sizeof(menu->ip_address),
		draw->x[MH_MULTI_IP_ADDRESS_SLOT] + x,
		draw->y[MH_MULTI_IP_ADDRESS_SLOT] + y, address_color);
}

static void
draw_join(struct drawing *draw, struct mh_menu_multi_state *menu,
	const struct mh_menu_multi_catalog *catalog, const char *const *names)
{
	if (menu->address >= catalog->address_count)
		menu->address = catalog->address_count == 0
			? 0
			: catalog->address_count - 1;

	if (catalog->address_count != 0) {
		draw_text(draw, "Server address:", sizeof("Server address:"),
			draw->x[ADDRESS_INFO_SLOT], draw->y[ADDRESS_INFO_SLOT],
			1);
		draw_text(draw, catalog->addresses[menu->address].ip,
			CATALOG_TEXT_BYTES,
			draw->x[ADDRESS_INFO_SLOT] +
				draw->x[ADDRESS_INFO_OFFSET_SLOT],
			draw->y[ADDRESS_INFO_SLOT], 2);
	}

	if (catalog->protected_count != 0) {
		draw->animation.frames[SEARCH_FRAME_SLOT] =
			menu->address < catalog->protected_count ? 0 : 1;
		draw_list(draw, names, catalog->protected_count,
			menu->address < catalog->protected_count
				? menu->address
				: catalog->protected_count - 1,
			PROTECTED_LIST_SLOT,
			menu->address < catalog->protected_count);
		draw_sprite(draw, SEARCH_FRAME_SLOT, 0, 0);
	} else {
		/* Use the former search panel for the saved server list. */
		draw->x[ADDRESS_LIST_SLOT] = draw->x[PROTECTED_LIST_SLOT];
		draw->y[ADDRESS_LIST_SLOT] = draw->y[PROTECTED_LIST_SLOT];
	}

	draw->animation.frames[ADDRESS_FRAME_SLOT] =
		menu->address < catalog->protected_count ? 1 : 0;
	draw_list(draw, names + catalog->protected_count,
		catalog->address_count - catalog->protected_count,
		menu->address < catalog->protected_count
			? 0
			: menu->address - catalog->protected_count,
		ADDRESS_LIST_SLOT, menu->address >= catalog->protected_count);
	draw_sprite(draw, ADDRESS_FRAME_SLOT, 0,
		catalog->protected_count == 0 ? draw->y[SEARCH_FRAME_SLOT] -
				draw->y[ADDRESS_FRAME_SLOT]
					: 0);

	if (catalog->address_count == 0)
		draw_text(draw, "No saved servers.",
			sizeof("No saved servers."), draw->x[ADDRESS_LIST_SLOT],
			draw->y[ADDRESS_LIST_SLOT], 1);

	if (menu->phase == MH_MULTI_SESSIONS) {
		draw_sprite(draw, SESSION_FRAME_SLOT, 0, 0);

		if (menu->session >= catalog->session_count)
			menu->session = catalog->session_count - 1;

		draw_list(draw, catalog->sessions, catalog->session_count,
			menu->session, SESSION_LIST_SLOT, 1);
	} else if (menu->phase == MH_MULTI_ADD_ADDRESS) {
		draw_address_form(draw, menu);
	} else {
		draw_text(draw, "Press:", 7, 344,
			draw->y[SEARCH_FRAME_SLOT] + 4, 1);

		if (catalog->address_count != 0) {
			draw_text(draw, "[Enter] to search", 18, 408,
				draw->y[SEARCH_FRAME_SLOT] + 4, 1);
			draw_text(draw, "[Insert] to add entry", 22, 408,
				draw->y[SEARCH_FRAME_SLOT] + 19, 1);
			draw_text(draw, "[Delete] to delete entry", 25, 408,
				draw->y[SEARCH_FRAME_SLOT] + 34, 1);
		} else
			draw_text(draw, "[Insert] to add entry", 22, 408,
				draw->y[SEARCH_FRAME_SLOT] + 4, 1);
	}
}

enum mh_error_code
mh_menu_multi_render(const struct mh_menu_multi_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	struct mh_menu_multi_state *menu, const struct mh_settings *settings,
	const struct mh_menu_multi_catalog *catalog, unsigned int mix,
	unsigned long clock_ms, struct mh_menu_multi_animation *state,
	unsigned long *random, unsigned long *noise,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct drawing draw;
	struct mh_menu_multi_state next;
	const char **names;
	unsigned int i, label_frame;

	if (art == NULL || font == NULL || menu == NULL || settings == NULL ||
		catalog == NULL || state == NULL || random == NULL ||
		noise == NULL || *random > MH_U32_MAX || *noise > MH_U32_MAX ||
		clock_ms > MH_U32_MAX || canvas == NULL ||
		canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		(menu->phase > MH_MULTI_SEARCHING &&
			menu->phase != MH_MULTI_ADD_ADDRESS) ||
		menu->choice > MH_MULTI_CHOICE_CREATE ||
		menu->create_row > MH_MULTI_CREATE_PASSWORD ||
		menu->address_row > MH_MULTI_ADDRESS_IP ||
		settings->protocol != TCP_IP_PROTOCOL ||
		(catalog->address_count != 0 && catalog->addresses == NULL) ||
		catalog->address_count > MH_MULTI_CATALOG_LIMIT ||
		catalog->protected_count > catalog->address_count ||
		catalog->session_count > MH_MULTI_CATALOG_LIMIT ||
		(catalog->session_count != 0 && catalog->sessions == NULL) ||
		(menu->phase == MH_MULTI_SESSIONS &&
			menu->choice == MH_MULTI_CHOICE_JOIN &&
			catalog->session_count == 0) ||
		memchr(menu->address_name, 0, sizeof(menu->address_name)) ==
			NULL ||
		memchr(menu->ip_address, 0, sizeof(menu->ip_address)) == NULL ||
		memchr(settings->session_name, 0,
			sizeof(settings->session_name)) == NULL ||
		memchr(settings->session_password, 0,
			sizeof(settings->session_password)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid multiplayer frame");

	for (i = 0; i < catalog->address_count; i++)
		if (!valid_text(catalog->addresses[i].name) ||
			!valid_text(catalog->addresses[i].ip))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid multiplayer address label");

	for (i = 0; i < catalog->session_count; i++)
		if (!valid_text(catalog->sessions[i]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid multiplayer session label");

	memset(&draw, 0, sizeof(draw));
	draw.error_out = error_out;
	draw.art = art;
	draw.font = font;
	draw.layout = layout;
	draw.mix = mix;

	for (i = 0; i < MH_MENU_LAYOUT_SLOTS && draw.status == MH_ERROR_NONE;
		i++)
		/* The address field occupies the removed protocol row. */
		draw.status = mh_menu_position(layout, MULTI_LAYOUT,
			i == MH_MULTI_IP_ADDRESS_SLOT ? ADDRESS_IP_LAYOUT_SLOT
						: i,
			mix, &draw.x[i], &draw.y[i], draw.error_out);

	if (draw.status == MH_ERROR_NONE)
		draw.status = mh_menu_text_draw(font, (const unsigned char *)"",
			1, 0, 0, 1, canvas, draw.error_out);

	if (draw.status != MH_ERROR_NONE)
		return draw.status;

	draw.canvas = *canvas;
	draw.canvas.pixels = (unsigned char *)malloc(canvas->size);
	draw.rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*draw.rows));
	names = (const char **)malloc(
		(catalog->address_count + 1) * sizeof(*names));

	if (draw.canvas.pixels == NULL || draw.rows == NULL || names == NULL) {
		free(draw.canvas.pixels);
		free(draw.rows);
		free(names);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing multiplayer");
	}

	for (i = 0; i < catalog->address_count; i++)
		names[i] = catalog->addresses[i].name;

	draw.animation = *state;
	memset(draw.animation.frames, 0, sizeof(draw.animation.frames));
	draw.seed = *random;
	draw.noise = *noise;
	next = *menu;
	memcpy(draw.canvas.pixels, canvas->pixels, canvas->size);
	label_frame = mh_menu_sprite_label_frame(clock_ms);
	draw.animation.frames[JOIN_LABEL_SLOT + next.choice] =
		next.phase == MH_MULTI_CHOOSE ? label_frame : 2;

	for (i = 1; i <= LAST_COMMON_ART_SLOT; i++)
		draw_sprite(&draw, i, 0, 0);

	if (next.phase == MH_MULTI_CHOOSE) {
		draw_sprite(&draw, JOIN_LABEL_SLOT, 0, 0);
		draw_sprite(&draw, CREATE_LABEL_SLOT, 0, 0);
	} else if (next.choice == MH_MULTI_CHOICE_CREATE)
		draw_create(&draw, &next, settings, label_frame);
	else
		draw_join(&draw, &next, catalog, names);

	if (draw.status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, draw.canvas.pixels, canvas->size);
		*menu = next;
		*state = draw.animation;
		*random = draw.seed;
		*noise = draw.noise;
	}

	free(draw.canvas.pixels);
	free(draw.rows);
	free(names);

	return draw.status;
}

static enum mh_error_code
draw_centered(const struct mh_font *font, const char *label, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	unsigned int i, width;

	width = 0;

	for (i = 0; label[i] != 0; i++)
		width += font->advance[(unsigned char)label[i]];

	return mh_menu_text_draw(font, (const unsigned char *)label,
		strlen(label) + 1, x - (long)(width / 2), y, 2, canvas,
		error_out);
}

enum mh_error_code
mh_menu_multi_overlay(const struct mh_font *font,
	const struct mh_menu_layout *layout,
	const struct mh_menu_multi_output *output, unsigned int mix,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	struct mh_menu_canvas work;
	enum mh_error_code status;
	long x, y, dx, dy;

	if (output == NULL || output->cursor > 1 || output->searching > 1 ||
		output->cursor_width > CURSOR_WIDTH_LIMIT ||
		(output->cursor &&
			output->cursor_slot != MH_MULTI_SESSION_NAME_SLOT &&
			output->cursor_slot != MH_MULTI_SESSION_PASSWORD_SLOT &&
			output->cursor_slot != MH_MULTI_ADDRESS_NAME_SLOT &&
			output->cursor_slot != MH_MULTI_IP_ADDRESS_SLOT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid multiplayer overlay");

	status = mh_menu_text_draw(
		font, (const unsigned char *)"", 1, 0, 0, 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (work.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing multiplayer overlay");

	memcpy(work.pixels, canvas->pixels, work.size);

	if (output->searching) {
		status = mh_menu_position(layout, MULTI_LAYOUT,
			SEARCH_NOTICE_SLOT, mix, &x, &y, error_out);

		/* Search notice uses centered font mode 2. */
		if (status == MH_ERROR_NONE)
			status = draw_centered(font, "Searching for sessions.",
				x, y, &work, error_out);

		if (status == MH_ERROR_NONE)
			status = draw_centered(font, "Please wait.", x, y + 20,
				&work, error_out);
	}

	if (output->cursor && status == MH_ERROR_NONE) {
		status = mh_menu_position(layout, MULTI_LAYOUT,
			output->cursor_slot == MH_MULTI_IP_ADDRESS_SLOT
				? ADDRESS_IP_LAYOUT_SLOT
				: output->cursor_slot,
			mix, &x, &y, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_menu_position(layout, MULTI_LAYOUT,
				VALUE_OFFSET_SLOT, mix, &dx, &dy, error_out);

		if (status == MH_ERROR_NONE) {
			x += dx;
			y += dy;
		}

		if (status == MH_ERROR_NONE &&
			output->cursor_slot >= MH_MULTI_ADDRESS_NAME_SLOT) {
			status = mh_menu_position(layout, MULTI_LAYOUT,
				ADD_VALUE_X_SLOT, mix, &dx, &dy, error_out);

			if (status == MH_ERROR_NONE)
				x += dx;

			if (status == MH_ERROR_NONE)
				status = mh_menu_position(layout, MULTI_LAYOUT,
					ADD_ORIGIN_SLOT, mix, &dx, &dy,
					error_out);

			if (status == MH_ERROR_NONE)
				y += dy;
		}

		if (status == MH_ERROR_NONE)
			status = mh_menu_text_draw(font,
				(const unsigned char *)"|", 2,
				x + (long)output->cursor_width, y, 2, &work,
				error_out);
	}

	if (status == MH_ERROR_NONE)
		memcpy(canvas->pixels, work.pixels, work.size);

	free(work.pixels);

	return status;
}
