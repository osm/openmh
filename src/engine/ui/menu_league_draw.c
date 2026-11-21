#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/league_tracks.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_league_draw.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_text.h"
#include "engine/ui/menu_triangles.h"
#include "interfaces/sprite/collection.h"

#define CATALOG_ENTRY_LIMIT 4096
#define ROW_CAPACITY 65536
#define DISPLAY_NAME_BYTES 128
#define NUMBER_TEXT_BYTES 16
#define CURSOR_WIDTH_LIMIT 4128705UL
#define SEPARATOR_LENGTH_LIMIT 16777216L
#define SEPARATOR_COLOR 250
#define DRIVER_COLOR_BASE 128
#define DIVISION_SCALE 100
#define SPECIAL_CAR_SUFFIX 10
#define NEEDLE_RESPONSE 32
#define NEEDLE_LENGTH 2.5999999046325684f
#define NEEDLE_TARGET_SLOT 24
#define NEEDLE_ORIGIN_SLOT 30
#define SETUP_LABEL_SLOT 6
#define SETUP_LAST_ART_SLOT 12
#define LIST_NEIGHBOURS 3
#define NEW_PROFILE_ROWS 7
#define NEW_FIXED_ROWS 3
#define NEW_LABEL_SLOT 7
#define SETUP_INFO_SLOT 11
#define SETUP_INFO_OFFSET_SLOT 20
#define SETUP_SPACING_SLOT 21
#define SETUP_VALUE_SLOT 22
#define LIST_ORIGIN_SLOT 21
#define LIST_SELECTION_SLOT 5
#define LIST_SPACING_SLOT 20
#define LIST_INFO_ORIGIN_SLOT 22
#define LIST_INFO_ART_SLOT 4
#define LIST_INFO_SPACING_SLOT 23
#define LIST_INFO_VALUE_SLOT 24
#define NEW_ORIGIN_SLOT 5
#define NEW_ROW_SLOT 21
#define NEW_LABEL_OFFSET_SLOT 20
#define NEW_NAME_OFFSET_SLOT 23
#define STANDINGS_ORIGIN_SLOT 16
#define STANDINGS_SPACING_SLOT 17
#define STANDINGS_VALUE_SLOT 18
#define TABLE_ORIGIN_SLOT 19
#define TABLE_COLUMN_SLOT 20

enum league_layout {
	SETUP_LAYOUT = 12,
	NEW_LAYOUT = 13,
	LOAD_LAYOUT = 14,
	DELETE_LAYOUT = 15,
	STANDINGS_LAYOUT = 16
};

struct drawing {
	const struct mh_menu_league_art	*art;
	const struct mh_font		*font;
	const struct mh_menu_layout	*layout;
	struct mh_menu_league_animation	animation;
	struct mh_menu_canvas		canvas;
	struct mh_menu_sprite_row	*rows;
	unsigned long			random, noise;
	unsigned int			layout_index, mix, label_frame;

	/* Preserve the first failure while composing scratch outputs. */
	enum mh_error_code		status;
	struct mh_error			*error_out;
};

static unsigned int
page_layout(unsigned int page)
{
	if (page == MH_MENU_LEAGUE_SETUP)
		return SETUP_LAYOUT;

	if (page == MH_MENU_LEAGUE_STANDINGS)
		return STANDINGS_LAYOUT;

	if (page == MH_MENU_LEAGUE_LOAD)
		return LOAD_LAYOUT;

	if (page == MH_MENU_LEAGUE_NEW)
		return NEW_LAYOUT;

	if (page == MH_MENU_LEAGUE_DELETE)
		return DELETE_LAYOUT;

	return 0;
}

void
mh_menu_league_art_free(struct mh_menu_league_art *art)
{
	unsigned int i, j;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_LEAGUE_ART_SLOTS; i++)
		for (j = 0; j < MH_MENU_LEAGUE_ART_FRAMES; j++)
			free(art->images[i][j].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_league_art_read(const struct mh_sprite_collection *collection,
	unsigned int page, struct mh_menu_league_art *output,
	struct mh_error *error_out)
{
	static const char *const setup[MH_MENU_LEAGUE_ART_SLOTS] = {NULL,
		"dots", "scrc", "msel", NULL, "leagu", "tlcon", "tlvst",
		"tlloa", "tlnew", "tldel", "info1", "info2", NULL, NULL, NULL,
		NULL};

	static const char *const standings[MH_MENU_LEAGUE_ART_SLOTS] = {
		NULL, "dots", "scrc", "leagu"};

	static const char *const list[MH_MENU_LEAGUE_ART_SLOTS] = {
		NULL, "dots", "scrc", "leagu", "llea", "selco"};

	static const char *const create[MH_MENU_LEAGUE_ART_SLOTS] = {NULL,
		"dots", "scrc", "leagu", "lbox", "lbox2", NULL, "tlnam", NULL,
		"tldiv", "tlpl1", "tlpl2", "tlpl3", "tlpl4", "tlpl5", "tlpl6",
		"tlpl7"};

	const char *const *names;
	struct mh_menu_league_art parsed;
	enum mh_error_code status;
	struct mh_menu_art_reader reader = {0, "invalid League menu sprite",
		"League menu artwork exceeds capacity"};

	unsigned int i;

	if (collection == NULL || output == NULL || !page_layout(page))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League menu archive");

	if (page == MH_MENU_LEAGUE_SETUP)
		names = setup;
	else if (page == MH_MENU_LEAGUE_STANDINGS)
		names = standings;
	else if (page == MH_MENU_LEAGUE_NEW)
		names = create;
	else
		names = list;

	memset(&parsed, 0, sizeof(parsed));
	parsed.page = page;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i < MH_MENU_LEAGUE_ART_SLOTS && status == MH_ERROR_NONE;
		i++) {
		if (names[i] == NULL)
			continue;

		status = mh_menu_art_read(&reader, collection, names[i],
			parsed.images[i], MH_MENU_LEAGUE_ART_FRAMES,
			MH_MENU_ART_ALL_FRAMES, &parsed.count[i], error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_league_art_free(&parsed);

	return status;
}

enum mh_error_code
mh_menu_league_animation_init(unsigned int page,
	const struct mh_menu_layout *layout, unsigned int choice,
	unsigned int mix, struct mh_menu_league_animation *output,
	struct mh_error *error_out)
{
	struct mh_menu_league_animation next;
	enum mh_error_code status;
	long x, y;

	if (!page_layout(page) || choice > MH_MENU_LEAGUE_CHOICE_DELETE ||
		mix > MH_MENU_POSITION_MIX_MAX || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League menu animation");

	memset(&next, 0, sizeof(next));
	next.sprites.wobble[2] = 1;
	next.sprites.wobble[page == MH_MENU_LEAGUE_SETUP ? 5 : 3] = 1;
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (page == MH_MENU_LEAGUE_SETUP) {
		status = mh_menu_position(layout, SETUP_LAYOUT,
			choice + NEEDLE_TARGET_SLOT, mix, &x, &y, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_menu_needle_init(&next.needle,
				(float)(8 * x + y), NEEDLE_RESPONSE, error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = next;

	return status;
}

static void
locate_slot(struct drawing *draw, unsigned int slot, long *x, long *y)
{
	*x = *y = 0;

	if (draw->status == MH_ERROR_NONE)
		draw->status =
			mh_menu_position(draw->layout, draw->layout_index, slot,
				draw->mix, x, y, draw->error_out);
}

static void
draw_text(struct drawing *draw, long x, long y, unsigned int color,
	const char *buffer)
{
	if (draw->status == MH_ERROR_NONE)
		draw->status = mh_menu_text_draw(draw->font,
			(const unsigned char *)buffer, strlen(buffer) + 1, x, y,
			color, &draw->canvas, draw->error_out);
}

static void
draw_number(struct drawing *draw, long x, long y, unsigned int color,
	unsigned long value)
{
	char buffer[NUMBER_TEXT_BYTES];
	long signed_value;

	signed_value = value <= (unsigned long)MH_I32_MAX
		? (long)value
		: -1L - (long)(MH_U32_MAX - value);
	sprintf(buffer, "%ld", signed_value);
	draw_text(draw, x, y, color, buffer);
}

static void
draw_sprite(struct drawing *draw, unsigned int slot, unsigned int frame,
	long dx, long dy)
{
	struct mh_menu_sprite_output commands;
	const struct mh_sprite_indexed *image;
	unsigned int j, count;

	if (draw->status != MH_ERROR_NONE)
		return;

	draw->status = mh_menu_sprite_select(draw->layout, draw->layout_index,
		slot, draw->mix, dx, dy, frame, &draw->animation.sprites,
		&draw->random, &commands, draw->error_out);

	if (draw->status != MH_ERROR_NONE || draw->art->count[slot] == 0)
		return;

	if (draw->art->count[slot] > MH_MENU_LEAGUE_ART_FRAMES ||
		frame >= draw->art->count[slot]) {
		draw->status = mh_error_set(draw->error_out, MH_ERROR_INVALID,
			"missing League menu frame");
		return;
	}

	image = &draw->art->images[slot][frame];

	for (j = 0; j < commands.count && draw->status == MH_ERROR_NONE; j++) {
		draw->status = mh_menu_sprite_rows(&commands.commands[j],
			image->height, draw->canvas.pitch, &draw->noise,
			draw->rows, ROW_CAPACITY, &count, draw->error_out);

		if (draw->status == MH_ERROR_NONE)
			draw->status = mh_menu_sprite_blit(image,
				&commands.commands[j], draw->rows, count,
				&draw->canvas, draw->error_out);
	}
}

static int
name_valid(const char *name)
{
	unsigned int i;

	if (name == NULL)
		return 0;

	for (i = 0; i < DISPLAY_NAME_BYTES; i++)
		if (name[i] == 0)
			return 1;

	return 0;
}

static const char *
next_track_name(const struct mh_menu_league_details *details,
	const struct mh_menu_league_info *info)
{
	unsigned int i, eligible;

	eligible = 0;

	for (i = 0; i < details->track_count; i++)
		if (mh_league_track_eligible(
			details->tracks[i].division, info->stage, 0)) {
			if (eligible == info->race_index)
				return details->tracks[i].name;

			eligible++;
		}

	/*
	 * Native recovery: an accepted last race can remain saved while its
	 * season presentation/rollover is interrupted. Never invent a next
	 * track.
	 */

	return eligible != 0 && info->race_index == eligible ? "Season complete"
							: NULL;
}

static unsigned int
eligible_track_count(
	const struct mh_menu_league_details *details, unsigned int stage)
{
	unsigned int i, count;

	count = 0;

	for (i = 0; i < details->track_count; i++)
		if (mh_league_track_eligible(
			details->tracks[i].division, stage, 0))
			count++;

	return count;
}

static void
draw_setup(struct drawing *draw, const struct mh_menu_league_info *info,
	const char *track_name, unsigned int choice, unsigned long elapsed_ms)
{
	struct mh_menu_needle_draw needle;
	unsigned int i;
	long x, y, dx, dy, column, spacing, value_x, value_y;
	float angle;

	for (i = 1; i <= SETUP_LAST_ART_SLOT; i++)
		draw_sprite(draw, i,
			i == choice + SETUP_LABEL_SLOT ? draw->label_frame : 0,
			0, 0);

	locate_slot(draw, choice + NEEDLE_TARGET_SLOT, &x, &y);
	angle = (float)(8 * x + y);
	locate_slot(draw, NEEDLE_ORIGIN_SLOT, &x, &y);

	if (draw->status == MH_ERROR_NONE)
		draw->status = mh_menu_needle_step(&draw->animation.needle,
			(float)x, (float)y, angle, NEEDLE_LENGTH, 3, 1,
			elapsed_ms, &needle, draw->error_out);

	if (draw->status == MH_ERROR_NONE)
		draw->status = mh_menu_triangles(needle.points, 2, needle.color,
			0, &draw->canvas, draw->error_out);

	locate_slot(draw, SETUP_INFO_SLOT, &x, &y);
	locate_slot(draw, SETUP_INFO_OFFSET_SLOT, &dx, &dy);
	x += dx;
	y += dy;
	locate_slot(draw, SETUP_SPACING_SLOT, &column, &spacing);
	locate_slot(draw, SETUP_VALUE_SLOT, &value_x, &value_y);
	draw_text(draw, x, y, 3, "League:");
	draw_text(draw, x, y + spacing, 3, "Name:");
	draw_text(draw, x, y + 2 * spacing, 3, "Division:");
	draw_text(draw, x + column, y, 3, "Difficulty:");
	draw_text(draw, x + column, y + spacing, 3, "Score:");
	draw_text(draw, x + column, y + 2 * spacing, 3, "Track:");
	draw_text(draw, x + value_x, y, 1, info->name);
	draw_text(draw, x + value_x, y + spacing, 1, info->player);
	draw_number(draw, x + value_x, y + 2 * spacing, 1, info->stage);
	draw_text(draw, x + column + value_y, y, 1, "-");
	draw_number(draw, x + column + value_y, y + spacing, 1, info->points);
	draw_text(draw, x + column + value_y, y + 2 * spacing, 1, track_name);
}

static void
draw_list(struct drawing *draw, const struct mh_menu_league_state *menu,
	const struct mh_menu_league_details *details, const char *track_name)
{
	static const char *const labels[6] = {"League:", "Date:", "Name:",
		"Division:", "Next Track:", "Score:"};

	const struct mh_menu_league_info *info;
	long x, y, dx, dy, unused, spacing, value;
	unsigned int i;

	info = &details->leagues[menu->selected];
	locate_slot(draw, LIST_ORIGIN_SLOT, &x, &y);
	locate_slot(draw, LIST_SELECTION_SLOT, &dx, &dy);
	x += dx;
	y += dy;
	locate_slot(draw, LIST_SPACING_SLOT, &unused, &spacing);
	draw_text(draw, x, y, 2, info->name);

	for (i = 1; i <= LIST_NEIGHBOURS && i <= menu->selected; i++)
		draw_text(draw, x, y - (long)i * spacing, 3,
			details->leagues[menu->selected - i].name);

	for (i = 1; i <= LIST_NEIGHBOURS && menu->selected + i < menu->count;
		i++)
		draw_text(draw, x, y + (long)i * spacing, 3,
			details->leagues[menu->selected + i].name);

	locate_slot(draw, LIST_INFO_ORIGIN_SLOT, &x, &y);
	locate_slot(draw, LIST_INFO_ART_SLOT, &dx, &dy);
	x += dx;
	y += dy;
	locate_slot(draw, LIST_INFO_SPACING_SLOT, &unused, &spacing);
	locate_slot(draw, LIST_INFO_VALUE_SLOT, &value, &unused);

	for (i = 0; i < 6; i++)
		draw_text(draw, x, y + (long)i * spacing, 3, labels[i]);

	x += value;
	draw_text(draw, x, y, 1, info->name);
	draw_number(draw, x, y + spacing, 1, info->created);
	draw_text(draw, x, y + 2 * spacing, 1, info->player);
	draw_number(draw, x, y + 3 * spacing, 1, info->stage);
	draw_text(draw, x, y + 4 * spacing, 1, track_name);
	draw_number(draw, x, y + 5 * spacing, 1, info->points);

	/* Draw the text first, then slots 1..3 and 5 only. */
	for (i = 1; i <= 3; i++)
		draw_sprite(draw, i, 0, 0, 0);

	draw_sprite(draw, LIST_SELECTION_SLOT, 0, 0, 0);
}

static void
draw_new(struct drawing *draw, const struct mh_menu_league_state *menu,
	unsigned int profiles)
{
	unsigned int i, row_count;
	long x, y, dx, dy, unused, row, label_y, name_x, name_y;

	for (i = 1; i <= 3; i++)
		draw_sprite(draw, i, 0, 0, 0);

	row_count = profiles >= NEW_PROFILE_ROWS
		? NEW_PROFILE_ROWS + NEW_FIXED_ROWS
		: profiles + NEW_FIXED_ROWS;
	locate_slot(draw, NEW_ORIGIN_SLOT, &x, &y);
	locate_slot(draw, NEW_ROW_SLOT, &dx, &row);
	locate_slot(draw, NEW_LABEL_OFFSET_SLOT, &unused, &label_y);
	locate_slot(draw, NEW_NAME_OFFSET_SLOT, &name_x, &name_y);

	for (i = 0; i < row_count; i++) {
		/*
		 * Wrap the row offset to unsigned 32 bits before dividing by
		 * four.
		 */
		dy = (long)(((unsigned long)row * i & MH_U32_MAX) >> 2);
		draw_sprite(draw, i + NEW_LABEL_SLOT,
			i == menu->new_row ? draw->label_frame : 0, x + dx,
			y + label_y + dy);

		if (i == 0)
			draw_text(draw, x + name_x, y + name_y + dy,
				i == menu->new_row ? 2 : 1,
				(const char *)menu->name);
	}
}

static void
draw_separator(struct drawing *draw, long x, long y, long length)
{
	double begin, end;

	if (draw->status != MH_ERROR_NONE)
		return;

	if (length < 0 || length > SEPARATOR_LENGTH_LIMIT) {
		draw->status = mh_error_set(draw->error_out, MH_ERROR_INVALID,
			"invalid League separator length");
		return;
	}

	/*
	 * Original memset is linear and may cover padding. Clip to allocation,
	 * not the visible viewport; double avoids 32-bit coordinate overflow.
	 */
	begin = (double)draw->canvas.begin + (double)y * draw->canvas.pitch + x;
	end = begin + length;

	if (begin < 0)
		begin = 0;

	if (end > draw->canvas.size)
		end = (double)draw->canvas.size;

	if (end > begin)
		memset(draw->canvas.pixels + (size_t)begin, SEPARATOR_COLOR,
			(size_t)(end - begin));
}

static unsigned int
clamp_car(const struct mh_menu_league_details *details, unsigned int stage,
	unsigned int car)
{
	unsigned int i, ordinary, eligible;

	/* Count ordinary cars, then inspect that prefix, even if unsorted. */
	ordinary = eligible = 0;

	for (i = 0; i < details->car_count; i++)
		if (details->cars[i].division % DIVISION_SCALE <
			SPECIAL_CAR_SUFFIX)
			ordinary++;

	for (i = 0; i < ordinary; i++)
		if (details->cars[i].division / DIVISION_SCALE <=
			MH_LEAGUE_GROUPS - stage)
			eligible++;

	if (car >= eligible)
		car = eligible ? eligible - 1 : 0;

	return car;
}

static void
draw_standings(struct drawing *draw,
	const struct mh_menu_league_details *details,
	const struct mh_menu_league_info *info, const char *track_name,
	unsigned int *car, unsigned char *palette)
{
	const struct mh_menu_league_driver *driver;
	long x, y, column, spacing, value, unused, row_y;
	unsigned int i, driver_count;

	for (i = 1; i <= 3; i++)
		draw_sprite(draw, i, 0, 0, 0);

	locate_slot(draw, STANDINGS_ORIGIN_SLOT, &x, &y);
	locate_slot(draw, STANDINGS_SPACING_SLOT, &column, &spacing);
	locate_slot(draw, STANDINGS_VALUE_SLOT, &value, &unused);
	draw_text(draw, x, y, 3, "League name:");
	draw_text(draw, x + column, y, 3, "Races done:");
	draw_text(draw, x, y + spacing, 3, "Division:");
	draw_text(draw, x + column, y + spacing, 3, "Next track:");
	draw_text(draw, x, y + 2 * spacing, 3, "Player name:");
	draw_text(draw, x + column, y + 2 * spacing, 3, "Car:");
	draw_text(draw, value, y, 1, info->name);
	draw_number(draw, value + column, y, 1, info->race_index);
	draw_text(draw, value + column + 14, y, 1, "/");
	draw_number(draw, value + column + 28, y, 1,
		eligible_track_count(details, info->stage));
	draw_number(draw, value, y + spacing, 1, info->stage);
	draw_text(draw, value + column, y + spacing, 1, track_name);
	draw_text(draw, value, y + 2 * spacing, 1, info->player);
	*car = clamp_car(details, info->stage, *car);

	draw_text(draw, value + column, y + 2 * spacing, 1,
		details->cars[*car].name);
	locate_slot(draw, TABLE_ORIGIN_SLOT, &x, &y);
	locate_slot(draw, TABLE_COLUMN_SLOT, &column, &unused);
	draw_text(draw, x, y, 3, "Pos:");
	draw_text(draw, x + column, y, 3, "Nick:");
	draw_text(draw, x + 4 * column, y, 3, "Score:");

	for (i = 1; i <= 3; i++)
		draw_number(draw, x + (long)(i + 4) * column, y, 3, i);

	driver_count = info->stage == MH_LEAGUE_FINAL ? MH_LEAGUE_FINAL_SIZE
						: MH_LEAGUE_DIVISION_SIZE;

	for (i = 0; i < driver_count; i++) {
		palette[DRIVER_COLOR_BASE * 3 + i * 3] =
			(unsigned char)(info->drivers[i].color >> 16);
		palette[DRIVER_COLOR_BASE * 3 + 1 + i * 3] =
			(unsigned char)(info->drivers[i].color >> 8);
		palette[DRIVER_COLOR_BASE * 3 + 2 + i * 3] =
			(unsigned char)info->drivers[i].color;
	}

	draw_separator(draw, x - 8, y + 14, column * 8);
	y += 6;

	for (i = 0; i < driver_count; i++) {
		row_y = y + (long)(i + 1) * spacing;
		driver = &info->drivers[i];
		draw_number(draw, x, row_y, DRIVER_COLOR_BASE + i, i + 1);
		draw_text(draw, x + column, row_y, DRIVER_COLOR_BASE + i,
			driver->nick);
		draw_number(draw, x + 4 * column, row_y, DRIVER_COLOR_BASE + i,
			driver->points);
		draw_number(draw, x + 5 * column, row_y, DRIVER_COLOR_BASE + i,
			driver->wins);
		draw_number(draw, x + 6 * column, row_y, DRIVER_COLOR_BASE + i,
			driver->second_places);
		draw_number(draw, x + 7 * column, row_y, DRIVER_COLOR_BASE + i,
			driver->third_places);

		if (i == 1) {
			y += 6;
			draw_separator(draw, x - 8,
				y + (long)(i + 1) * spacing + 8, column * 8);
		}

		if (driver_count >= 3 && i == driver_count - 3) {
			y += 6;
			draw_separator(draw, x - 8,
				y + (long)(i + 1) * spacing + 8, column * 8);
		}
	}
}

enum mh_error_code
mh_menu_league_render(const struct mh_menu_league_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_league_state *menu,
	const struct mh_menu_league_details *details, unsigned int *car,
	unsigned char palette[MH_MENU_PALETTE_BYTES], unsigned int mix,
	unsigned long clock_ms, unsigned long elapsed_ms,
	struct mh_menu_league_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	struct drawing draw;
	const struct mh_menu_league_info *info;
	const char *track_name;
	unsigned char colors[MH_MENU_PALETTE_BYTES];
	unsigned int i, driver_count, selected_car;

	if (art == NULL || !page_layout(art->page) || menu == NULL ||
		details == NULL || state == NULL || random == NULL ||
		noise == NULL || car == NULL || palette == NULL ||
		menu->choice > MH_MENU_LEAGUE_CHOICE_DELETE ||
		menu->new_row > 1 || menu->count == 0 ||
		menu->count > MH_LEAGUE_CATALOG_MAX_ENTRIES ||
		menu->selected >= menu->count ||
		memchr(menu->name, 0, sizeof(menu->name)) == NULL ||
		details->leagues == NULL || details->cars == NULL ||
		details->tracks == NULL || details->car_count == 0 ||
		details->car_count > CATALOG_ENTRY_LIMIT ||
		details->track_count == 0 ||
		details->track_count > CATALOG_ENTRY_LIMIT ||
		*car >= details->car_count ||
		details->profile_count > CATALOG_ENTRY_LIMIT ||
		canvas == NULL || canvas->pixels == NULL || canvas->size == 0 ||
		canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		canvas->pitch == 0 ||
		canvas->pitch > MH_MENU_CANVAS_PITCH_LIMIT ||
		canvas->begin > canvas->end || canvas->end > canvas->size ||
		mix > MH_MENU_POSITION_MIX_MAX || *random > MH_U32_MAX ||
		*noise > MH_U32_MAX || clock_ms > MH_U32_MAX ||
		elapsed_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League menu frame");

	for (i = 0; i < menu->count; i++)
		if (!name_valid(details->leagues[i].name))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid League display name");

	info = &details->leagues[menu->selected];

	if (info->stage > MH_LEAGUE_THIRD || info->created > MH_U32_MAX ||
		info->race_index > MH_U32_MAX || info->points > MH_U32_MAX ||
		!name_valid(info->player))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League display information");

	for (i = 0; i < details->car_count; i++)
		if (!name_valid(details->cars[i].name) ||
			details->cars[i].division > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid League car catalog");

	for (i = 0; i < details->track_count; i++)
		if (!name_valid(details->tracks[i].name) ||
			details->tracks[i].division > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid League track catalog");

	if (art->page == MH_MENU_LEAGUE_STANDINGS) {
		driver_count = info->stage == MH_LEAGUE_FINAL
			? MH_LEAGUE_FINAL_SIZE
			: MH_LEAGUE_DIVISION_SIZE;

		for (i = 0; i < driver_count; i++) {
			const struct mh_menu_league_driver *driver;

			driver = &info->drivers[i];

			if (!name_valid(driver->nick) ||
				driver->color > MH_U32_MAX ||
				driver->points > MH_U32_MAX ||
				driver->wins > MH_U32_MAX ||
				driver->second_places > MH_U32_MAX ||
				driver->third_places > MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid League standings row");
		}
	}

	track_name = next_track_name(details, info);

	if (track_name == NULL && art->page != MH_MENU_LEAGUE_NEW)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"League menu next track is unavailable");

	memset(&draw, 0, sizeof(draw));
	draw.error_out = error_out;
	draw.art = art;
	draw.font = font;
	draw.layout = layout;
	draw.layout_index = page_layout(art->page);
	draw.mix = mix;
	draw.animation = *state;
	draw.random = *random;
	draw.noise = *noise;
	draw.canvas = *canvas;
	selected_car = *car;
	memcpy(colors, palette, MH_MENU_PALETTE_BYTES);
	draw.label_frame = mh_menu_sprite_label_frame(clock_ms);
	draw.canvas.pixels = (unsigned char *)malloc(draw.canvas.size);
	draw.rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*draw.rows));

	if (draw.canvas.pixels == NULL || draw.rows == NULL) {
		free(draw.canvas.pixels);
		free(draw.rows);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing League menu");
	}

	memcpy(draw.canvas.pixels, canvas->pixels, canvas->size);

	if (art->page == MH_MENU_LEAGUE_SETUP)
		draw_setup(&draw, info, track_name, menu->choice, elapsed_ms);
	else if (art->page == MH_MENU_LEAGUE_STANDINGS)
		draw_standings(&draw, details, info, track_name, &selected_car,
			colors);
	else if (art->page == MH_MENU_LEAGUE_NEW)
		draw_new(&draw, menu, details->profile_count);
	else
		draw_list(&draw, menu, details, track_name);

	if (draw.status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, draw.canvas.pixels, canvas->size);
		memcpy(palette, colors, MH_MENU_PALETTE_BYTES);
		*state = draw.animation;
		*car = selected_car;
		*random = draw.random;
		*noise = draw.noise;
	}

	free(draw.canvas.pixels);
	free(draw.rows);

	return draw.status;
}

enum mh_error_code
mh_menu_league_cursor(const struct mh_menu_layout *layout, unsigned int mix,
	const struct mh_menu_league_output *result, const struct mh_font *font,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	enum mh_error_code status;
	long x, y, dx, dy;

	if (result == NULL || result->cursor > 1 ||
		result->cursor_width > CURSOR_WIDTH_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League name cursor");

	if (!result->cursor)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_menu_position(
		layout, NEW_LAYOUT, NEW_ORIGIN_SLOT, mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, NEW_LAYOUT,
			NEW_NAME_OFFSET_SLOT, mix, &dx, &dy, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menu_text_draw(font, (const unsigned char *)"|", 2,
		x + dx + (long)result->cursor_width, y + dy, 2, canvas,
		error_out);
}
