#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/assets/menu_layout.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_palette.h"
#include "engine/ui/menu_position.h"
#include "engine/ui/menu_sine.h"
#include "engine/ui/menu_sprite.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "menu sprite line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	LAYOUT_BYTES = 32768,
	ROW_CAPACITY = 64,
	SCREEN_BYTES = 64,
	PALETTE_BYTES = 768
};

static int
layout_and_selection(void)
{
	static struct mh_menu_layout layout, saved;
	static unsigned char data[LAYOUT_BYTES];
	struct mh_menu_sprite_state state;
	struct mh_menu_sprite_output output;
	unsigned int i;
	unsigned long random;
	long x, y;

	CHECK(mh_menu_layout_dta_decode(data, sizeof(data), &layout, NULL) ==
		MH_ERROR_NONE);
	layout.words[0][1][0] = 10;
	layout.words[0][1][1] = -10;
	layout.words[0][1][2] = -10;
	layout.words[0][1][3] = 10;
	CHECK(mh_menu_position(&layout, 0, 1, 1, &x, &y, NULL) ==
			MH_ERROR_NONE &&
		x == 9 && y == -10);
	CHECK(mh_menu_position(&layout, 0, 1, 256, &x, &y, NULL) ==
			MH_ERROR_NONE &&
		x == -10 && y == 10);
	CHECK(mh_menu_position(&layout, 32, 1, 256, &x, &y, NULL) !=
		MH_ERROR_NONE);
	saved = layout;

	for (i = 0; i < sizeof(data); i++)
		CHECK(mh_menu_layout_dta_decode(data, i, &layout, NULL) !=
			MH_ERROR_NONE);

	CHECK(memcmp(&layout, &saved, sizeof(layout)) == 0);
	data[3] = 127;
	CHECK(mh_menu_layout_dta_decode(data, sizeof(data), &layout, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&layout, &saved, sizeof(layout)) == 0);
	memset(&state, 0, sizeof(state));
	random = 1;
	layout.words[0][1][5] = 5;
	CHECK(mh_menu_sprite_select(&layout, 0, 1, 128, 0, 0, 0, &state,
		&random, &output, NULL) == MH_ERROR_NONE);
	CHECK(output.count == 2 &&
		output.commands[0].kind == MH_MENU_SPRITE_PHASE && random == 1);
	CHECK(memcmp(&output.commands[0], &output.commands[1],
		sizeof(output.commands[0])) == 0);

	return 0;
}

static int
palette_frame(void)
{
	unsigned char palette[PALETTE_BYTES], colors[PALETTE_BYTES];

	memset(colors, 99, sizeof(colors));
	memset(palette, 0xa5, sizeof(palette));
	CHECK(mh_menu_palette_frame(colors, colors, NULL, -1, palette, NULL) !=
			MH_ERROR_NONE &&
		palette[0] == 0xa5);
	CHECK(mh_menu_palette_frame(colors, colors, NULL, .5f, palette, NULL) ==
		MH_ERROR_NONE);
	CHECK(palette[3] == 255 && palette[5] == 99 && palette[194] == 20 &&
		palette[384] == 0 && palette[765] == 112 &&
		palette[766] == 124 && palette[767] == 127);
	memcpy(colors, palette, PALETTE_BYTES);
	CHECK(mh_menu_palette_submit(palette, 0x123456UL, NULL, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(palette, colors, PALETTE_BYTES) == 0);
	CHECK(mh_menu_palette_submit(palette, 0x123456UL, palette, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(palette, colors, PALETTE_BYTES) == 0);
#if ULONG_MAX > MH_U32_MAX
	CHECK(mh_menu_palette_submit(palette, MH_U32_MAX + 1UL, colors, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(palette, colors, PALETTE_BYTES) == 0);
#endif
	CHECK(mh_menu_palette_submit(palette, 0xff123456UL, colors, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(palette, colors, PALETTE_BYTES) == 0 &&
		colors[18] == 0x12 && colors[19] == 0x34 && colors[20] == 0x56);
	memset(palette, 0, PALETTE_BYTES);
	CHECK(colors[18] == 0x12 && colors[194] == 20);

	return 0;
}

static int
disabled_label(void)
{
	unsigned char pixels[] = {2, 255, 2, 255, 2, 255, 2, 255};
	unsigned char screen[16];
	struct mh_sprite_indexed image;
	struct mh_menu_sprite_command command;
	struct mh_menu_sprite_row rows[2];
	struct mh_menu_canvas canvas;
	unsigned int count;
	unsigned long noise;

	memset(&image, 0, sizeof(image));
	memset(&command, 0, sizeof(command));
	memset(&canvas, 0, sizeof(canvas));
	memset(screen, 99, sizeof(screen));
	image.width = image.height = 2;
	image.pixels = pixels;
	canvas.pixels = screen;
	canvas.size = canvas.end = sizeof(screen);
	canvas.pitch = 4;
	command.x = command.y = 1;
	noise = 0;
	CHECK(mh_menu_sprite_rows(&command, 2, 4, &noise, rows, 2, &count,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_sprite_blit_disabled(&image, &command, rows, count,
		&canvas, NULL) == MH_ERROR_NONE);
	CHECK(screen[5] == 3 && screen[6] == 99 && screen[9] == 99 &&
		screen[10] == 3);
	CHECK(mh_menu_sprite_blit(&image, &command, rows, count, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(screen[5] == 2 && screen[6] == 2 && screen[9] == 2 &&
		screen[10] == 2);

	return 0;
}

static int
command_sequence(void)
{
	unsigned char pixels[] = {2, 255, 4, 255, 6, 255, 8, 255};
	unsigned char actual[64], expected[64];
	struct mh_sprite_indexed image;
	struct mh_menu_sprite_output commands;
	struct mh_menu_sprite_row rows[2];
	struct mh_menu_canvas canvas;
	unsigned long noise, expected_noise;
	unsigned int i, count;
	int disabled;

	memset(&image, 0, sizeof(image));
	memset(&commands, 0, sizeof(commands));
	memset(&canvas, 0, sizeof(canvas));
	image.width = image.height = 2;
	image.pixels = pixels;
	canvas.size = canvas.end = sizeof(actual);
	canvas.pitch = 8;
	commands.count = 2;
	commands.commands[0].kind = MH_MENU_SPRITE_JITTER;
	commands.commands[0].slot = 3;
	commands.commands[0].x = commands.commands[0].y = 1;
	commands.commands[1] = commands.commands[0];
	commands.commands[1].x = 4;

	for (disabled = 0; disabled <= 1; disabled++) {
		memset(actual, 99, sizeof(actual));
		memset(expected, 99, sizeof(expected));
		noise = expected_noise = 1234;
		canvas.pixels = actual;
		CHECK(mh_menu_sprite_draw_commands(&image, &commands, rows, 2,
			&noise, &canvas, disabled,
			NULL) == MH_ERROR_NONE);
		canvas.pixels = expected;

		for (i = 0; i < commands.count; i++) {
			CHECK(mh_menu_sprite_rows(&commands.commands[i], 2, 8,
				&expected_noise, rows, 2, &count,
				NULL) == MH_ERROR_NONE);

			if (disabled)
				CHECK(mh_menu_sprite_blit_disabled(&image,
					&commands.commands[i], rows,
					count, &canvas,
					NULL) == MH_ERROR_NONE);
			else
				CHECK(mh_menu_sprite_blit(&image,
					&commands.commands[i], rows,
					count, &canvas,
					NULL) == MH_ERROR_NONE);
		}

		CHECK(memcmp(actual, expected, sizeof(actual)) == 0);
		CHECK(noise == expected_noise && noise != 1234);
	}

	canvas.pixels = actual;
	memset(actual, 99, sizeof(actual));
	memcpy(expected, actual, sizeof(actual));
	noise = 1234;
	CHECK(mh_menu_sprite_draw_commands(&image, &commands, rows, 1, &noise,
		&canvas, 0, NULL) != MH_ERROR_NONE);
	CHECK(noise == 1234 && memcmp(actual, expected, sizeof(actual)) == 0);
	commands.count = 3;
	CHECK(mh_menu_sprite_draw_commands(&image, &commands, rows, 2, &noise,
		&canvas, 0, NULL) == MH_ERROR_INVALID);
	CHECK(noise == 1234 && memcmp(actual, expected, sizeof(actual)) == 0);
	commands.count = 0;
	CHECK(mh_menu_sprite_draw_commands(&image, &commands, rows, 2, &noise,
		&canvas, 0, NULL) == MH_ERROR_NONE);

	return 0;
}

int
main(void)
{
	struct mh_menu_sprite_command command;
	struct mh_menu_sprite_row rows[ROW_CAPACITY], before[ROW_CAPACITY];
	struct mh_menu_canvas canvas;
	struct mh_sprite_indexed image;
	unsigned char pixels[8], screen[SCREEN_BYTES],
		screen_before[SCREEN_BYTES];
	unsigned int count, i;
	unsigned long noise;

	if (layout_and_selection() != 0 || disabled_label() != 0 ||
		command_sequence() != 0)
		return 1;

	memset(&command, 0, sizeof(command));
	noise = 1234;
	count = 99;
	memset(rows, 0xa5, sizeof(rows));
	memcpy(before, rows, sizeof(rows));
	CHECK(mh_menu_sprite_rows(&command, 10, 960, &noise, rows, 9, &count,
		NULL) != MH_ERROR_NONE);
	CHECK(count == 99 && noise == 1234 &&
		memcmp(rows, before, sizeof(rows)) == 0);
	CHECK(mh_menu_sprite_rows(&command, 10, 960, &noise, rows, ROW_CAPACITY,
		&count, NULL) == MH_ERROR_NONE &&
		count == 10);

	for (i = 0; i < count; i++)
		CHECK(rows[i].offset == (long)i * 960 && rows[i].source == i);

	command.kind = MH_MENU_SPRITE_CENTER;
	command.factor = 0;
	CHECK(mh_menu_sprite_rows(&command, 10, 960, &noise, rows, ROW_CAPACITY,
		&count, NULL) == MH_ERROR_NONE);

	for (i = 0; i < count; i++)
		CHECK(rows[i].offset == 4800 && rows[i].source == i);

	CHECK(mh_menu_sine(0) == 0 && mh_menu_sine(1024) == 1 &&
		mh_menu_sine(3072) == -1);

	if (palette_frame() != 0)
		return 1;

	pixels[0] = 0;
	pixels[1] = 255;
	pixels[2] = 5;
	pixels[3] = 0;
	pixels[4] = 7;
	pixels[5] = 255;
	pixels[6] = 8;
	pixels[7] = 255;
	image.width = image.height = 2;
	image.pixels = pixels;
	canvas.pixels = screen;
	canvas.size = sizeof(screen);
	canvas.pitch = 8;
	canvas.begin = 8;
	canvas.end = 40;
	memset(screen, 99, sizeof(screen));
	memset(&command, 0, sizeof(command));
	command.y = 1;
	CHECK(mh_menu_sprite_rows(&command, 2, 8, &noise, rows, ROW_CAPACITY,
		&count, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_sprite_blit(&image, &command, rows, count, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(screen[16] == 0 && screen[17] == 99 && screen[24] == 7 &&
		screen[25] == 8);
	memcpy(screen_before, screen, sizeof(screen));
	command.y = -2;
	CHECK(mh_menu_sprite_blit(&image, &command, rows, count, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(screen, screen_before, sizeof(screen)) == 0);
	command.kind = MH_MENU_SPRITE_CENTER;
	command.y = 0;
	rows[0].offset = 32;
	rows[1].offset = 0;
	CHECK(mh_menu_sprite_blit(&image, &command, rows, count, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(screen, screen_before, sizeof(screen)) == 0);
	rows[0].source = 2;
	CHECK(mh_menu_sprite_blit(&image, &command, rows, count, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(screen, screen_before, sizeof(screen)) == 0);
	puts("menu sprites: bounded layouts, signed interpolation, duplicate draws and row capacity passed");

	return 0;
}
