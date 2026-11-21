#include <stdio.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/core/error.h"
#include "engine/ui/menu_graphics_draw.h"
#include "engine/ui/menu_list.h"
#include "engine/ui/menu_slider.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Graphics draw line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_graphics_art art;
static struct mh_menu_layout layout;
static unsigned char pixels[614400], saved_pixels[614400], glyphs[1024],
	sprite_bytes[814];

static void
write_word(unsigned int offset, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		sprite_bytes[offset + i] = (unsigned char)(value >> (i * 8));
}

int
main(void)
{
	static const char *const names[5] = {"a", "b", "c", "d", "e"};
	struct mh_sprite_collection *collection;
	struct mh_menu_graphics_art rejected_art;
	struct mh_menu_graphics_state menu;
	struct mh_menu_graphics_animation animation, saved_animation;
	struct mh_menu_graphics_device device;
	struct mh_menu_graphics_mode mode;
	struct mh_menu_graphics_catalog catalog;
	struct mh_av_settings settings;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	unsigned long seed, noise, saved_seed, saved_noise;
	unsigned int i;
	size_t origin;
	const char *invalid_names[5];

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
	origin = canvas.begin;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_choices_draw(&font, names, 5, 4, 4, 1, 60, 10, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 10 * 960 + 60] == 2 &&
		pixels[origin + 10 * 960 + 77] == 2);
	CHECK(pixels[origin + 10 * 960 + 94] == 5 &&
		pixels[origin + 9 * 960 + 90] == 2);
	CHECK(pixels[origin + 20 * 960 + 98] == 2 &&
		pixels[origin + 20 * 960 + 99] == 99);

	/* No backfill at last choice. */
	CHECK(pixels[origin + 10 * 960 + 111] == 99);
	memcpy(saved_pixels, pixels, sizeof(pixels));

	for (i = 0; i < 5; i++)
		invalid_names[i] = names[i];

	invalid_names[4] = "";
	CHECK(mh_menu_choices_draw(&font, invalid_names, 5, 0, 1, 0, 60, 10,
		&canvas, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_slider_draw(&font, 50, 0, 100, "%", 200, 0, 60, 50,
		&canvas, NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 50 * 960 + 61] == 1 &&
		pixels[origin + 50 * 960 + 65] == 1);
	memcpy(saved_pixels, pixels, sizeof(pixels));
	CHECK(mh_menu_slider_draw(&font, 50, 0, 0, "%", 200, 0, 60, 50, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_menu_slider_draw(&font, 50, 0, 100, "123456789", 200, 0, 60,
		50, &canvas, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_slider_draw(&font, 1000000, 0, .001f, "%", 200, 0, 60, 50,
		&canvas, NULL) == MH_ERROR_NONE);

	/*
	 * With one-pixel bar glyphs the cursor has zero travel, even for a
	 * large out-of-range value. No divide or float-to-integer overflow.
	 */
	font.advance[0xf3] = 0;
	memcpy(saved_pixels, pixels, sizeof(pixels));
	CHECK(mh_menu_slider_draw(&font, 50, 0, 100, "%", 200, 0, 60, 50,
		&canvas, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	font.advance[0xf3] = 1;

	write_word(0, 0x80000001UL);
	memcpy(sprite_bytes + 4, "dots", 4);
	write_word(12, 784);
	write_word(784, 1);
	write_word(788, 2);
	write_word(792, 2);
	write_word(796, 16);
	write_word(800, 8);
	write_word(804, 13);
	memcpy(sprite_bytes + 808, "\306\100\0\0\303\303", 6);
	CHECK(mh_sprite_spr_open(sprite_bytes, sizeof(sprite_bytes),
		MH_PICTURE_INDEXED, &collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_graphics_art_read(collection, &art, NULL) ==
			MH_ERROR_NONE &&
		art.images[1].pixels != NULL && art.images[3].pixels == NULL);
	memset(&rejected_art, 0xa5, sizeof(rejected_art));
	sprite_bytes[808] = 0xe8;
	mh_sprite_collection_close(collection);
	CHECK(mh_sprite_spr_open(sprite_bytes, sizeof(sprite_bytes),
		MH_PICTURE_INDEXED, &collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_graphics_art_read(collection, &rejected_art, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(rejected_art); i++)
		CHECK(((unsigned char *)&rejected_art)[i] == 0xa5);

	mh_sprite_collection_close(collection);

	memset(&menu, 0, sizeof(menu));
	memset(&device, 0, sizeof(device));
	device.name = "Glide";
	device.modes = &mode;
	device.count = 1;
	device.motion_blur = 1;
	mode.width = 640;
	mode.height = 400;
	catalog.devices = &device;
	catalog.fullscreen = 0;
	catalog.presentation = 0;
	catalog.screenshots = 1;
	catalog.count = 1;
	CHECK(mh_av_settings_init(&settings, 255, 255, NULL) == MH_ERROR_NONE);
	layout.words[5][4][0] = layout.words[5][4][1] = 40;
	layout.words[5][4][2] = layout.words[5][4][3] = 30;
	layout.words[5][24][0] = layout.words[5][24][1] = 200;
	layout.words[5][24][2] = layout.words[5][24][3] = 15;
	memset(&animation, 0xa5, sizeof(animation));
	mh_menu_graphics_animation_init(&animation);

	for (i = 0; i < 32; i++)
		CHECK(animation.sprites.timer[i] == 0 &&
			animation.sprites.wobble[i] == (i == 2 || i == 3));

	seed = 1;
	noise = 741118;
	menu.selected = 8;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);

	/* Custom-only row is absent. */
	CHECK(pixels[origin + 180 * 960 + 40] == 99);
	settings.detail_mode = 4;
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);

	/* Retained high focus becomes visible. */
	CHECK(pixels[origin + 180 * 960 + 40] == 2);
	CHECK(pixels[origin + 405 * 960 + 40] == 99);
	device.glenz = 1;
	device.texture_precision = 1;
	menu.selected = MH_MENU_GRAPHICS_TEXTURE_PRECISION;
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 390 * 960 + 40] == 2);
	device.texture_filter = 1;
	menu.selected = MH_MENU_GRAPHICS_TEXTURE_FILTER;
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 390 * 960 + 40] == 2);
	device.frame_queue = 1;
	menu.selected = MH_MENU_GRAPHICS_FRAME_QUEUE;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);

	/* The new last row scrolls into the old last row's visible position. */
	CHECK(pixels[origin + 390 * 960 + 40] == 2);
	CHECK(pixels[origin + 375 * 960 + 40] != 2);

	device.blur_history = 1;
	menu.selected = MH_MENU_GRAPHICS_TRIPLE_BUFFER;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 390 * 960 + 40] == 2);
	CHECK(pixels[origin + 375 * 960 + 40] != 2);

	catalog.presentation = 1;
	catalog.screenshots = 1;
	menu.selected = MH_MENU_GRAPHICS_MAX_FPS;
	menu.fps_editing = 1;
	menu.fps_text[0] = 0;
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	strcpy(menu.fps_text, "237");
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	menu.fps_editing = 0;
	catalog.presentation = 0;
	catalog.screenshots = 1;

	/* Keep the device row visible for the late invalid-name check. */
	menu.selected = MH_MENU_GRAPHICS_DEVICE;
	saved_animation = animation;
	saved_seed = seed;
	saved_noise = noise;
	memcpy(saved_pixels, pixels, sizeof(pixels));
	device.name = NULL; /* Late rejection, after sprites and all labels. */
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 128, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&animation, &saved_animation, sizeof(saved_animation)) ==
			0 &&
		seed == saved_seed && noise == saved_noise);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	device.name = "Glide";
	settings.brightness[0] = settings.brightness[1] =
		settings.brightness[2] = -1e30f;
	CHECK(mh_menu_graphics_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&animation, &saved_animation, sizeof(saved_animation)) ==
			0 &&
		memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	mh_menu_graphics_art_free(&art);
	mh_menu_graphics_art_free(&art);
	puts("Graphics choices, slider bounds, Custom rows, artwork ownership and complete frame failure atomicity passed");

	return 0;
}
