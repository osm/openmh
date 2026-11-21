#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/spr.h"
#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_digits.h"
#include "engine/ui/menu_preview.h"
#include "engine/ui/menu_race_draw.h"
#include "engine/ui/menu_thumbnail.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Menu widgets line %d: %s\n",          \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static enum mh_error_code
read_tga_thumbnail(const unsigned char *data, size_t size,
	struct mh_menu_thumbnail *output, struct mh_error *error_out)
{
	struct mh_indexed_image image;
	enum mh_error_code status;

	status = mh_image_tga_decode_thumbnail(data, size, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_menu_thumbnail_prepare(&image, output, error_out);
	free(image.indices);

	return status;
}

static unsigned char pixels[614400], saved_pixels[614400];
static struct mh_menu_canvas canvas;

static void
reset_canvas(void)
{
	size_t i;

	for (i = 0; i < sizeof(pixels); i++)
		pixels[i] = (unsigned char)(i * 73 + 19);

	canvas.pixels = pixels;
	canvas.size = sizeof(pixels);
	canvas.begin = 115360;
	canvas.end = 499360;
	canvas.pitch = 960;
}

static int
race_failure(void)
{
	struct mh_menu_race_art art;
	struct mh_menu_race_animation animation, saved_animation;
	struct mh_menu_digits numbers, saved_numbers;
	struct mh_menu_layout layout;
	struct mh_menu_race_setup_state menu;
	struct mh_menu_race_scene scene;
	struct mh_font font;
	struct mh_wireframe model;
	struct mh_wireframe_point point;
	unsigned char palette[768], saved_palette[768], background[192];
	unsigned long seed, noise;
	struct mh_error error;
	enum mh_error_code status;
	const char *names[1];
	char invalid_name[128];

	memset(&art, 0, sizeof(art));
	memset(&animation, 0, sizeof(animation));
	memset(&numbers, 0, sizeof(numbers));
	memset(&layout, 0, sizeof(layout));
	memset(&menu, 0, sizeof(menu));
	memset(&scene, 0, sizeof(scene));
	memset(&font, 0, sizeof(font));
	memset(&model, 0, sizeof(model));
	memset(&point, 0, sizeof(point));
	memset(palette, 73, sizeof(palette));
	memcpy(saved_palette, palette, sizeof(saved_palette));
	memset(background, 19, sizeof(background));
	memset(invalid_name, 'A', sizeof(invalid_name));
	names[0] = invalid_name;
	model.points = &point;
	model.point_count = 1;
	scene.names = names;
	scene.models = &model;
	scene.count = scene.unlocked = 1;
	scene.background_palette = background;
	menu.laps = 5;
	menu.catch_up = 1;
	mh_menu_race_animation_init(&animation);
	saved_animation = animation;
	saved_numbers = numbers;
	reset_canvas();
	memcpy(saved_pixels, pixels, sizeof(pixels));
	seed = 123;
	noise = 456;
	status = mh_menu_race_render(&art, &font, &layout, &menu, &scene, 0, 16,
		16, 0, &animation, &numbers, &seed, &noise, palette, &canvas,
		&error);

	/*
	 * Name validation runs after the oval, outline and ten sprite effects.
	 * The entire public frame must roll back, including palette and RNG.
	 */
	CHECK(status != MH_ERROR_NONE &&
		strcmp(error.message, "unterminated race setup text") == 0);
	CHECK(memcmp(&animation, &saved_animation, sizeof(animation)) == 0);
	CHECK(memcmp(&numbers, &saved_numbers, sizeof(numbers)) == 0);
	CHECK(seed == 123 && noise == 456 &&
		memcmp(palette, saved_palette, sizeof(palette)) == 0);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);

	return 0;
}

static int
preview_transition(void)
{
	unsigned int shown;
	float fade;

	shown = 1;
	fade = mh_menu_preview_advance(2, 100, &shown, 1);
	CHECK(shown == 1 && fade > .69f && fade < .71f);
	fade = mh_menu_preview_advance(3, 1000, &shown, fade);
	CHECK(shown == 1 && fade == 0);
	fade = mh_menu_preview_advance(3, 0, &shown, fade);
	CHECK(shown == 3 && fade == 0);
	fade = mh_menu_preview_advance(3, 1000, &shown, fade);
	CHECK(shown == 3 && fade == 1);
	fade = mh_menu_preview_advance(4, 0, &shown, fade);
	CHECK(shown == 3 && fade == 1);
	fade = mh_menu_preview_advance(4, 100, &shown, fade);
	fade = mh_menu_preview_advance(3, 1000, &shown, fade);
	CHECK(shown == 3 && fade == 1);

	return 0;
}

int
main(void)
{
	struct mh_menu_thumbnail thumbnail, saved_thumbnail;
	struct mh_menu_digits state, saved_digits;
	unsigned char data[18], numa_pixels[19 * 220 * 2],
		numb_pixels[6 * 40 * 2];
	struct mh_sprite_indexed numa, numb;

	CHECK(race_failure() == 0);
	reset_canvas();
	memset(&thumbnail, 0, sizeof(thumbnail));
	saved_thumbnail = thumbnail;
	memset(data, 0, sizeof(data));
	CHECK(read_tga_thumbnail(data, sizeof(data), &thumbnail, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(&thumbnail, &saved_thumbnail, sizeof(thumbnail)) == 0);
	memcpy(saved_pixels, pixels, sizeof(pixels));
	CHECK(mh_menu_thumbnail_draw(&thumbnail, 320, 200, &canvas, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_menu_thumbnail_draw(NULL, 262144, 262144, &canvas, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_thumbnail_draw(NULL, 320, 200, &canvas, NULL) ==
		MH_ERROR_NONE);
	CHECK(pixels[canvas.begin + 200 * 960 + 320] == 128);
	memset(&state, 0, sizeof(state));
	state.cursor = 63;
	state.position[0] = 5;
	saved_digits = state;
	memset(numa_pixels, 255, sizeof(numa_pixels));
	memset(numb_pixels, 255, sizeof(numb_pixels));
	numa.width = 19;
	numa.height = 220;
	numa.pixels = numa_pixels;
	numb.width = 6;
	numb.height = 40;
	numb.pixels = numb_pixels;
	memcpy(saved_pixels, pixels, sizeof(pixels));

	/*
	 * The second digit overflows after the first already drew into scratch.
	 */
	CHECK(mh_menu_digits_draw(&numa, &numb, 10, 2, 30, 30, MH_U32_MAX, 0,
		&state, &canvas, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_digits, sizeof(state)) == 0 &&
		memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_digits_draw(NULL, NULL, 10, 2, 30, 30, 16, 1, &state,
		&canvas, NULL) == MH_ERROR_NONE);
	CHECK(state.cursor == 1 && state.position[63] == 0 &&
		state.position[0] == 1);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	CHECK(preview_transition() == 0);
	mh_menu_thumbnail_free(&thumbnail);
	puts("Menu thumbnail bounds and rolling-digit state/pixel rollback passed");

	return 0;
}
