#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_sound_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Sound draw line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_menu_sound_art art;
static struct mh_menu_layout layout;

enum {
	CANVAS_BYTES = 614400,
	GLYPH_BYTES = 1024
};

static unsigned char pixels[CANVAS_BYTES], saved_pixels[CANVAS_BYTES];
static unsigned char glyphs[GLYPH_BYTES];

int
main(void)
{
	const char *names[3];
	unsigned long tracks[3], seconds[6], seed, noise, saved_seed,
		saved_noise;
	struct mh_menu_sound_state menu, saved_menu;
	struct mh_menu_sound_values values;
	struct mh_menu_sound_animation animation, saved_animation;
	struct mh_menu_sound_scene scene;
	struct mh_menu_digits digits, saved_digits;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	unsigned int i, failure;

	memset(&menu, 0, sizeof(menu));
	memset(&values, 0, sizeof(values));
	memset(&animation, 0, sizeof(animation));
	memset(&digits, 0, sizeof(digits));
	memset(&font, 0, sizeof(font));
	memset(glyphs, 255, sizeof(glyphs));

	for (i = 0; i < 256; i++)
		font.advance[i] = 1;

	font.image.rgba = glyphs;
	font.image.width = font.image.height = 16;
	font.cell_width = font.height = 1;
	canvas.pixels = pixels;
	canvas.size = sizeof(pixels);
	canvas.begin = 115360;
	canvas.end = 499360;
	canvas.pitch = 960;

	for (i = 0; i < 32; i++) {
		layout.words[6][i][0] = layout.words[6][i][1] = 80;
		layout.words[6][i][2] = layout.words[6][i][3] = 80;
	}

	layout.words[6][26][0] = layout.words[6][26][1] = 32;
	layout.words[6][27][0] = layout.words[6][27][1] = 192;
	names[0] = "a";
	names[1] = "b";
	names[2] = "c";
	tracks[0] = 4;
	tracks[1] = MH_U32_MAX;
	tracks[2] = 5;

	for (i = 0; i < 6; i++)
		seconds[i] = 61;

	scene.names = names;
	scene.tracks = tracks;
	scene.seconds = seconds;
	scene.count = 3;
	scene.seconds_count = 6;
	animation.loop_frame = 7;
	animation.sprites.timer[2] = 20;
	mh_menu_sound_animation_init(&animation);
	CHECK(animation.loop_frame == 7 && animation.sprites.timer[2] == 0 &&
		animation.sprites.wobble[2] == 1 &&
		animation.sprites.wobble[3] == 1);
	menu.selected = 3;
	menu.course = 1;
	menu.list_changed = 1;
	values.effects = 128;
	values.music = 255;
	values.loop = 1;
	seed = 1;
	noise = 741118;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_sound_render(&art, &font, &layout, &menu, &values, &scene,
		0, 0, 16, 0, &animation, &digits, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.list_changed == 0 && animation.loop_frame == 6 &&
		digits.cursor == 18);
	CHECK(digits.position[0] == 5 && digits.position[1] == 9 &&
		digits.position[2] == 0 && digits.position[3] == 0 &&
		digits.position[4] == 0 && digits.position[5] == 0);
	CHECK(digits.position[6] == 4 && digits.position[8] == 1 &&
		digits.position[10] == 1 && digits.position[12] == 5);
	CHECK(pixels[canvas.begin + 175 * 960 + 153] == 2 &&
		pixels[canvas.begin + 145 * 960 + 153] == 1 &&
		pixels[canvas.begin + 205 * 960 + 153] == 1);

	for (failure = 0; failure < 9; failure++) {
		menu.list_changed = 1;
		digits.cursor = 0;

		/* Fail after drawing selected and preceding courses. */
		if (failure == 0)
			names[2] = NULL;

		/* Missing selected course-list artwork frame. */
		if (failure == 1)
			art.count[21] = 1;

		if (failure == 2)
			scene.count = 0;

		if (failure == 3)
			scene.seconds_count = 257;

		if (failure == 4)
			scene.seconds = NULL;

		if (failure == 5)
			layout.words[6][27][0] = 32769;

		if (failure == 6)
			values.effects = 256;

		if (failure == 7)
			animation.loop_frame = 8;

		if (failure == 8)
			digits.cursor = 65;

		saved_menu = menu;
		saved_animation = animation;
		saved_digits = digits;
		saved_seed = seed;
		saved_noise = noise;
		memcpy(saved_pixels, pixels, sizeof(pixels));
		CHECK(mh_menu_sound_render(&art, &font, &layout, &menu, &values,
			&scene, 0, 128, 16, 0, &animation, &digits, &seed,
			&noise, &canvas, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&menu, &saved_menu, sizeof(menu)) == 0 &&
			memcmp(&animation, &saved_animation,
				sizeof(animation)) == 0 &&
			memcmp(&digits, &saved_digits, sizeof(digits)) == 0 &&
			memcmp(saved_pixels, pixels, sizeof(pixels)) == 0 &&
			seed == saved_seed && noise == saved_noise);
		names[2] = "c";
		art.count[21] = 0;
		scene.count = 3;
		scene.seconds_count = 6;
		scene.seconds = seconds;
		layout.words[6][27][0] = 192;
		values.effects = 128;
		animation.loop_frame = 6;
	}

	for (i = 0; i < 3; i += 2) {
		menu.course = i;
		digits.cursor = 0;
		scene.seconds = NULL;
		scene.seconds_count = 0;
		CHECK(mh_menu_sound_render(&art, &font, &layout, &menu, &values,
			&scene, 256, 512, 16, 1, &animation, &digits,
			&seed, &noise, &canvas, NULL) == MH_ERROR_NONE &&
			digits.cursor == 12);
	}

	if (ULONG_MAX > MH_U32_MAX) {
		tracks[0] = ULONG_MAX;
		memcpy(saved_pixels, pixels, sizeof(pixels));
		CHECK(mh_menu_sound_render(&art, &font, &layout, &menu, &values,
			&scene, 0, 0, 16, 0, &animation, &digits, &seed,
			&noise, &canvas, NULL) != MH_ERROR_NONE &&
			memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);
	}

	puts("Sound compositor retained switch, course order/colors, unknown durations and atomic failures passed");

	return 0;
}
