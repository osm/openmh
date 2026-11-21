#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_art.h"
#include "engine/ui/menu_graphics_draw.h"
#include "engine/ui/menu_list.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_slider.h"
#include "interfaces/sprite/collection.h"

#define GRAPHICS_LAYOUT 5
#define LAST_ART_SLOT 3
#define ROW_CAPACITY 65536
#define LABEL_ORIGIN_SLOT 4
#define VALUE_OFFSET_SLOT 24
#define ORDINARY_ROWS (MH_MENU_GRAPHICS_DETAIL + 1)
#define MODE_TEXT_BYTES 48
#define SLIDER_VALUE_LIMIT 1000000
#define SLIDER_WIDTH 200
#define VISIBLE_ROWS 25
#define VISIBLE_CHOICES 15
#define VISIBLE_QUALITY_CHOICES 16

void
mh_menu_graphics_art_free(struct mh_menu_graphics_art *art)
{
	unsigned int i;

	if (art == NULL)
		return;

	for (i = 0; i < MH_MENU_GRAPHICS_ART_SLOTS; i++)
		free(art->images[i].pixels);

	memset(art, 0, sizeof(*art));
}

enum mh_error_code
mh_menu_graphics_art_read(const struct mh_sprite_collection *collection,
	struct mh_menu_graphics_art *output, struct mh_error *error_out)
{
	static const char *const names[MH_MENU_GRAPHICS_ART_SLOTS] = {
		NULL, "dots", "opt", "scr4"};

	struct mh_menu_graphics_art parsed;
	enum mh_error_code status;
	struct mh_menu_art_reader reader = {0, "invalid Graphics sprite",
		"Graphics artwork exceeds capacity"};

	unsigned int i;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid Graphics artwork archive");

	memset(&parsed, 0, sizeof(parsed));
	status = mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		status = mh_menu_art_read(&reader, collection, names[i],
			&parsed.images[i], 1, MH_MENU_ART_FIRST_FRAME, NULL,
			error_out);
	}

	if (status == MH_ERROR_NONE)
		*output = parsed;
	else
		mh_menu_graphics_art_free(&parsed);

	return status;
}

void
mh_menu_graphics_animation_init(struct mh_menu_graphics_animation *state)
{
	if (state == NULL)
		return;

	memset(state, 0, sizeof(*state));
	state->sprites.wobble[2] = state->sprites.wobble[3] = 1;
}

static long
signed_word(unsigned long value)
{
	return value >= MH_U32_SIGN_BIT ? -1L - (long)(MH_U32_MAX - value)
					: (long)value;
}

static enum mh_error_code
slider_values(const struct mh_av_settings *settings, volatile float *brightness,
	volatile float *delay, struct mh_error *error_out)
{
	long double sum;

	/*
	 * Original drawing stores the sum, then average, then percent as
	 * separate floats; this differs from input's double average.
	 */
	sum = (long double)settings->brightness[0] + settings->brightness[1] +
		settings->brightness[2];

	if (!(sum >= -FLT_MAX && sum <= FLT_MAX))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"Graphics brightness sum outside float range");

	*brightness = (float)sum;
	*brightness =
		(float)((long double)*brightness * (long double)(1.0 / 3.0));
	sum = (long double)*brightness * 100;

	if (!(sum >= -SLIDER_VALUE_LIMIT && sum <= SLIDER_VALUE_LIMIT))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"Graphics brightness outside slider range");

	*brightness = (float)sum;
	*delay = (float)((long double)settings->checkpoint_delay *
		(long double)0.001);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
count_rows(const struct mh_av_settings *settings,
	const struct mh_menu_graphics_device *device)
{
	unsigned int row_count;

	row_count = settings->detail_mode == MH_DETAIL_CUSTOM
		? (device->motion_blur ? MH_MENU_GRAPHICS_MOTION_BLUR
				: MH_MENU_GRAPHICS_VIEW_DISTANCE) +
			1
		: ORDINARY_ROWS;

	if (settings->detail_mode == MH_DETAIL_CUSTOM &&
		device->texture_precision)
		row_count = MH_MENU_GRAPHICS_TEXTURE_PRECISION + 1;

	if (settings->detail_mode == MH_DETAIL_CUSTOM && device->texture_filter)
		row_count = MH_MENU_GRAPHICS_TEXTURE_FILTER + 1;

	if (settings->detail_mode == MH_DETAIL_CUSTOM && device->frame_queue)
		row_count = MH_MENU_GRAPHICS_FRAME_QUEUE + 1;

	if (settings->detail_mode == MH_DETAIL_CUSTOM && device->blur_history)
		row_count = MH_MENU_GRAPHICS_TRIPLE_BUFFER + 1;

	return row_count;
}

static void
choice_values(const struct mh_av_settings *settings,
	unsigned int values[MH_MENU_GRAPHICS_ROWS])
{
	memset(values, 0, MH_MENU_GRAPHICS_ROWS * sizeof(*values));

	values[MH_MENU_GRAPHICS_INFO_DETAIL] = settings->info_detail;
	values[MH_MENU_GRAPHICS_ROAD_MAP] = settings->road_map;
	values[MH_MENU_GRAPHICS_CHECKPOINT_INFO] = settings->checkpoint_info;
	values[MH_MENU_GRAPHICS_FULLSCREEN] = settings->fullscreen;
	values[MH_MENU_GRAPHICS_VSYNC] = settings->vsync;
	values[MH_MENU_GRAPHICS_SHOW_FPS] = settings->show_fps;
	values[MH_MENU_GRAPHICS_SCREENSHOT_FORMAT] =
		settings->screenshot_format;
	values[MH_MENU_GRAPHICS_DETAIL] = settings->detail_mode;
	values[MH_MENU_GRAPHICS_LENS_FLARES] = settings->lens_flares;
	values[MH_MENU_GRAPHICS_SPARKS] = settings->sparks;
	values[MH_MENU_GRAPHICS_SMOKE] = settings->smoke;
	values[MH_MENU_GRAPHICS_HALOS] = settings->halos;
	values[MH_MENU_GRAPHICS_SHADOW] = settings->shadow;
	values[MH_MENU_GRAPHICS_SKID_MARKS] = settings->skid_marks;
	values[MH_MENU_GRAPHICS_NAME_PLATES] = settings->name_plates;
	values[MH_MENU_GRAPHICS_SKY] = settings->sky_image;
	values[MH_MENU_GRAPHICS_TRACK_DETAIL] = settings->track_detail;
	values[MH_MENU_GRAPHICS_CAR_DETAIL] = settings->car_detail;
	values[MH_MENU_GRAPHICS_CAR_SHADING] = settings->car_shading;
	values[MH_MENU_GRAPHICS_PERSPECTIVE] = settings->perspective_on;
	values[MH_MENU_GRAPHICS_PERSPECTIVE_QUALITY] =
		settings->perspective == 16 ? 1 : 0;
	values[MH_MENU_GRAPHICS_MOTION_BLUR] = settings->motion_blur;
	values[MH_MENU_GRAPHICS_TEXTURE_PRECISION] =
		settings->texture_precision;
	values[MH_MENU_GRAPHICS_TEXTURE_FILTER] = settings->texture_filter;
	values[MH_MENU_GRAPHICS_FRAME_QUEUE] = settings->frame_queue;
	values[MH_MENU_GRAPHICS_TRIPLE_BUFFER] = settings->triple_buffer;
}

static enum mh_error_code
draw_fps_limit(const struct mh_font *font,
	const struct mh_menu_graphics_state *menu,
	const struct mh_av_settings *settings, long x, long y,
	struct mh_menu_canvas *canvas, struct mh_error *error_out)
{
	char text[16];
	const char *hint;
	int selected;
	enum mh_error_code status;

	selected = menu->selected == MH_MENU_GRAPHICS_MAX_FPS;

	if (menu->fps_editing)
		sprintf(text, "[%s|]", menu->fps_text);
	else
		sprintf(text, "[%u]", (unsigned int)settings->max_fps);

	status = mh_menu_text_draw(font, (const unsigned char *)text,
		sizeof(text), x, y, selected ? 2 : 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	hint = settings->max_fps == 0 ? "Unlimited" : "";

	if (selected)
		hint = menu->fps_editing ? "Enter: apply / Esc: cancel"
					: "Type 0-10000 (0 = unlimited)";

	return mh_menu_text_draw(font, (const unsigned char *)hint,
		strlen(hint) + 1, x + 80, y, 1, canvas, error_out);
}

enum mh_error_code
mh_menu_graphics_render(const struct mh_menu_graphics_art *art,
	const struct mh_font *font, const struct mh_menu_layout *layout,
	const struct mh_menu_graphics_state *menu,
	const struct mh_av_settings *settings,
	const struct mh_menu_graphics_catalog *catalog, unsigned int mix,
	struct mh_menu_graphics_animation *state, unsigned long *random,
	unsigned long *noise, struct mh_menu_canvas *canvas,
	struct mh_error *error_out)
{
	static const char *const labels[MH_MENU_GRAPHICS_ROWS] = {
		"Render device:", "Screen size:", "Brightness:", "Info detail:",
		"Info map:", "Checkpoint info:", "Checkpoint displaytime:",
		"Graphic detail:", "Lensflares:", "Sparks:", "Smoke:", "Halos:",
		"Shadows:", "Skidmarks:", "Nameplates:", "Background:",
		"Trackdetail:", "Cardetail:", "Carshading:",
		"Perspective correction:", "Perspective quality:",
		"View distance:", "Motion blur:", "Texture precision:",
		"Texture filtering:", "Race frame queue:",
		"Triple-buffer blur:", "Fullscreen:", "VSync:", "FPS Limit:",
		"Screenshot format:", "Show FPS:"};

	static const char *const switches[2] = {"Off", "On"},
				*const info[3] = {"None", "Selective", "All"};

	static const char *const detail[5] = {
		"Low", "Medium", "High", "Max", "Custom"};

	static const char *const names[3] = {"Off", "Flat", "Transparent"},
				*const sky[2] = {"Blank", "Picture"};

	static const char *const shading[4] = {
		"Flat", "Gouraud", "Reflection", "Glenz"};

	static const char *const screenshot_format[] = {"PNG", "TGA"};
	static const char *const frame_queue[3] = {"1", "2", "3"};
	static const char *const texture_precision[2] = {"16-bit", "32-bit"};
	static const char *const texture_filter[4] = {
		"Original", "Nearest", "Bilinear", "Trilinear"};

	static const char *const perspective[2] = {"Selective", "All"},
				*const quality[2] = {"Normal", "Better"};

	struct mh_menu_graphics_animation next;
	struct mh_menu_sprite_output commands;
	struct mh_menu_sprite_row *rows;
	struct mh_menu_canvas work;
	const struct mh_menu_graphics_device *device;
	const char *device_names[MH_MENU_GRAPHICS_DEVICES];
	const char **mode_names;
	const char *const *choices;
	enum mh_error_code status;
	char (*mode_text)[MODE_TEXT_BYTES];
	unsigned int i, position, count, shown, choice, visible, row_count,
		first_row, last_row, values[MH_MENU_GRAPHICS_ROWS];
	unsigned long seed, phase;
	long x, y, dx, dy, value_x;
	volatile float brightness, delay;

	if (art == NULL || font == NULL || state == NULL || random == NULL ||
		noise == NULL || canvas == NULL || canvas->pixels == NULL ||
		canvas->size == 0 || canvas->size > MH_MENU_CANVAS_BYTE_LIMIT ||
		*random > MH_U32_MAX || *noise > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Graphics frame");

	status = mh_menu_graphics_validate(menu, settings, catalog, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, GRAPHICS_LAYOUT,
			LABEL_ORIGIN_SLOT, mix, &x, &y, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_position(layout, GRAPHICS_LAYOUT,
			VALUE_OFFSET_SLOT, mix, &dx, &dy, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_menu_text_draw(font, (const unsigned char *)"", 1,
			x, y, 1, canvas, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = slider_values(settings, &brightness, &delay, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	device = &catalog->devices[menu->device];
	mode_text = (char (*)[MODE_TEXT_BYTES])malloc(
		device->count * sizeof(*mode_text));
	mode_names = (const char **)malloc(device->count * sizeof(*mode_names));
	rows = (struct mh_menu_sprite_row *)malloc(
		(unsigned long)ROW_CAPACITY * sizeof(*rows));
	work = *canvas;
	work.pixels = (unsigned char *)malloc(work.size);

	if (mode_text == NULL || mode_names == NULL || rows == NULL ||
		work.pixels == NULL) {
		free(mode_text);
		free(mode_names);
		free(rows);
		free(work.pixels);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory composing Graphics");
	}

	for (i = 0; i < catalog->count; i++)
		device_names[i] = catalog->devices[i].name;

	for (i = 0; i < device->count; i++) {
		sprintf(mode_text[i], "%4ldx%-4ld",
			signed_word(device->modes[i].width),
			signed_word(device->modes[i].height));
		mode_names[i] = mode_text[i];
	}

	next = *state;
	seed = *random;
	phase = *noise;
	memcpy(work.pixels, canvas->pixels, work.size);

	for (i = 1; i <= LAST_ART_SLOT && status == MH_ERROR_NONE; i++) {
		status = mh_menu_sprite_select(layout, GRAPHICS_LAYOUT, i, mix,
			0, 0, 0, &next.sprites, &seed, &commands, error_out);

		if (status != MH_ERROR_NONE || art->images[i].pixels == NULL)
			continue;

		status = mh_menu_sprite_draw_commands(&art->images[i],
			&commands, rows, ROW_CAPACITY, &phase, &work, 0,
			error_out);
	}

	row_count = count_rows(settings, device) + catalog->fullscreen +
		2 * catalog->presentation + 1 + catalog->screenshots;

	position = mh_menu_graphics_row_position(catalog, menu->selected);

	first_row = position >= VISIBLE_ROWS ? position - VISIBLE_ROWS + 1 : 0;
	last_row = first_row + VISIBLE_ROWS;

	if (last_row > row_count)
		last_row = row_count;

	value_x = x + dx;

	for (position = first_row;
		position < last_row && status == MH_ERROR_NONE; position++) {
		i = mh_menu_graphics_row_at(catalog, position);

		if (mh_menu_graphics_row_available(device, i))
			status = mh_menu_text_draw(font,
				(const unsigned char *)labels[i],
				strlen(labels[i]) + 1, x,
				y + (long)(position - first_row) * dy,
				i == menu->selected ? 2 : 3, &work, error_out);
	}

	choice_values(settings, values);

	for (position = first_row;
		position < last_row && status == MH_ERROR_NONE; position++) {
		i = mh_menu_graphics_row_at(catalog, position);

		if (!mh_menu_graphics_row_available(device, i))
			continue;

		if (i == MH_MENU_GRAPHICS_BRIGHTNESS ||
			i == MH_MENU_GRAPHICS_CHECKPOINT_DELAY ||
			i == MH_MENU_GRAPHICS_VIEW_DISTANCE) {
			const char *unit;
			float value, minimum, maximum;

			minimum = 0.0f;
			unit = "%";

			if (i == MH_MENU_GRAPHICS_BRIGHTNESS) {
				value = brightness;
				maximum = 200.0f;
			} else if (i == MH_MENU_GRAPHICS_CHECKPOINT_DELAY) {
				value = delay;
				maximum = 10.0f;
				unit = "s";
			} else {
				value = settings->view_distance;
				minimum = 30.0f;
				maximum = 150.0f;
			}

			status = mh_menu_slider_draw(font, value, minimum,
				maximum, unit, SLIDER_WIDTH,
				i == menu->selected, value_x,
				y + (long)(position - first_row) * dy, &work,
				error_out);
			continue;
		}

		choices = switches;
		count = 2;
		choice = values[i];
		visible = VISIBLE_CHOICES;

		if (i == MH_MENU_GRAPHICS_MAX_FPS) {
			status = draw_fps_limit(font, menu, settings, value_x,
				y + (long)(position - first_row) * dy, &work,
				error_out);
			continue;
		}

		if (i == MH_MENU_GRAPHICS_SCREENSHOT_FORMAT)
			choices = screenshot_format;

		if (i == MH_MENU_GRAPHICS_DEVICE) {
			choices = device_names;
			count = catalog->count;
			choice = menu->device;
			visible = 1;
		}

		if (i == MH_MENU_GRAPHICS_MODE) {
			choices = mode_names;
			count = device->count;
			choice = menu->mode;
			visible = 3;
		}

		if (i >= MH_MENU_GRAPHICS_INFO_DETAIL &&
			i <= MH_MENU_GRAPHICS_CHECKPOINT_INFO) {
			choices = info;
			count = 3;
		}

		if (i == MH_MENU_GRAPHICS_DETAIL) {
			choices = detail;
			count = 5;
		}

		if (i == MH_MENU_GRAPHICS_NAME_PLATES) {
			choices = names;
			count = 3;
		}

		if (i == MH_MENU_GRAPHICS_SKY)
			choices = sky;

		if (i == MH_MENU_GRAPHICS_TRACK_DETAIL)
			choices = detail + 1;

		if (i == MH_MENU_GRAPHICS_CAR_DETAIL) {
			choices = detail;
			count = 3;
		}

		if (i == MH_MENU_GRAPHICS_CAR_SHADING) {
			choices = shading;
			count = device->glenz ? 4 : 3;
		}

		if (i == MH_MENU_GRAPHICS_PERSPECTIVE)
			choices = perspective;

		if (i == MH_MENU_GRAPHICS_TEXTURE_PRECISION) {
			if (!device->texture_precision)
				continue;

			choices = texture_precision;
		}

		if (i == MH_MENU_GRAPHICS_TEXTURE_FILTER) {
			count = 4;
			choices = texture_filter;
		}

		if (i == MH_MENU_GRAPHICS_FRAME_QUEUE) {
			count = 3;
			choices = frame_queue;
		}

		if (i == MH_MENU_GRAPHICS_PERSPECTIVE_QUALITY) {
			choices = quality;
			visible = VISIBLE_QUALITY_CHOICES;
		}

		shown = i == menu->selected;
		status = mh_menu_choices_draw(font, choices, count, choice,
			visible, (int)shown, value_x,
			y + (long)(position - first_row) * dy, &work,
			error_out);
	}

	if (status == MH_ERROR_NONE) {
		memcpy(canvas->pixels, work.pixels, work.size);
		*state = next;
		*random = seed;
		*noise = phase;
	}

	free(mode_text);
	free(mode_names);
	free(rows);
	free(work.pixels);

	return status;
}
