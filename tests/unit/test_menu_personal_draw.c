#include <stdio.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/core/error.h"
#include "engine/ui/menu_personal_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Personal drawing line %d: %s\n",      \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_personal_art art;
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
	struct mh_sprite_collection *collection;
	struct mh_menu_personal_art rejected_art;
	struct mh_menu_personal_state menu;
	struct mh_menu_personal_output output;
	struct mh_menu_personal_animation animation, saved_animation;
	struct mh_settings settings;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	unsigned char palette[768], saved_palette[768];
	unsigned long colors[32], seed, noise, saved_seed, saved_noise;
	unsigned int i;
	size_t origin;

	memset(&menu, 0, sizeof(menu));
	memset(&font, 0, sizeof(font));
	memset(glyphs, 255, sizeof(glyphs));
	memset(palette, 77, sizeof(palette));

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
	CHECK(mh_menu_personal_art_read(collection, &art, NULL) ==
			MH_ERROR_NONE &&
		art.images[1].pixels != NULL && art.images[3].pixels == NULL);
	memset(&rejected_art, 0xa5, sizeof(rejected_art));
	sprite_bytes[808] = 0xe8;
	mh_sprite_collection_close(collection);
	CHECK(mh_sprite_spr_open(sprite_bytes, sizeof(sprite_bytes),
		MH_PICTURE_INDEXED, &collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_personal_art_read(collection, &rejected_art, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(rejected_art); i++)
		CHECK(((unsigned char *)&rejected_art)[i] == 0xa5);

	mh_sprite_collection_close(collection);
	CHECK(mh_settings_init(&settings, "Player", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 32; i++)
		colors[i] = i * 0x080808UL;

	layout.words[17][20][0] = layout.words[17][20][1] = 40;
	layout.words[17][20][2] = layout.words[17][20][3] = 30;
	layout.words[17][21][0] = layout.words[17][21][1] = 200;
	layout.words[17][21][2] = layout.words[17][21][3] = 12;
	memset(&animation, 0xa5, sizeof(animation));
	mh_menu_personal_animation_init(&animation);

	for (i = 0; i < 32; i++)
		CHECK(animation.sprites.timer[i] == 0 &&
			animation.sprites.wobble[i] == (i == 2 || i == 3));

	seed = 1;
	noise = 741118;
	menu.selected = 14;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_personal_render(&art, &font, &layout, &menu, &settings,
		colors, 0, &animation, &seed, &noise, palette, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 198 * 960 + 40] == 99 &&
		palette[384 + 31 * 3] == 248 && palette[480] == 77);
	settings.custom_car_colors = 1;
	CHECK(mh_menu_personal_render(&art, &font, &layout, &menu, &settings,
		colors, 0, &animation, &seed, &noise, palette, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 198 * 960 + 40] == 2);
	saved_animation = animation;
	saved_seed = seed;
	saved_noise = noise;
	memcpy(saved_pixels, pixels, sizeof(pixels));
	memcpy(saved_palette, palette, 768);

	/* Fail after palette, sprites and labels. */
	memset(settings.short_keys[9], 'X', 128);
	CHECK(mh_menu_personal_render(&art, &font, &layout, &menu, &settings,
		colors, 128, &animation, &seed, &noise, palette, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&animation, &saved_animation, sizeof(saved_animation)) ==
			0 &&
		seed == saved_seed && noise == saved_noise);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0 &&
		memcmp(saved_palette, palette, 768) == 0);

	memset(&output, 0, sizeof(output));
	output.swatch_count = 1;
	output.swatches[0].color = 0x123456UL;
	output.swatches[0].active = 1;
	output.swatches[0].nearest = 0;
	CHECK(mh_menu_personal_overlay(&font, &layout, &output, 0, palette,
		&canvas, NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 53 * 960 + 200] == 2 &&
		pixels[origin + 54 * 960 + 201] == 160);
	CHECK(pixels[origin + 52 * 960 + 242] == 2 &&
		pixels[origin + 53 * 960 + 243] == 0 &&
		pixels[origin + 54 * 960 + 244] == 128);
	CHECK(palette[480] == 0x12 && palette[481] == 0x34 &&
		palette[482] == 0x56 && palette[483] == 77);
	memcpy(saved_pixels, pixels, sizeof(pixels));
	memcpy(saved_palette, palette, 768);
	output.cursor = 1;
	output.cursor_row = 0;
	output.cursor_width = 16711425UL; /* Late cursor error after widgets. */
	CHECK(mh_menu_personal_overlay(&font, &layout, &output, 0, palette,
		&canvas, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0 &&
		memcmp(saved_palette, palette, 768) == 0);
	output.cursor = 0;
	output.swatch_count = 2;
	CHECK(mh_menu_personal_overlay(&font, &layout, &output, 0, palette,
		&canvas, NULL) != MH_ERROR_NONE);
	output.swatch_count = 4;
	output.swatches[3].nearest = 32;
	CHECK(mh_menu_personal_overlay(&font, &layout, &output, 0, palette,
		&canvas, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0 &&
		memcmp(saved_palette, palette, 768) == 0);
	output.swatch_count = 1;
	output.swatches[0].nearest = 31;
	layout.words[17][21][0] = layout.words[17][21][1] = -4;
	CHECK(mh_menu_personal_overlay(&font, &layout, &output, 0, palette,
		&canvas, NULL) == MH_ERROR_NONE);

	/* Padded drawing, no logical-screen clipping. */
	CHECK(pixels[origin + 54 * 960 - 3] == 160);
	mh_menu_personal_art_free(&art);
	mh_menu_personal_art_free(&art);
	puts("Personal palette, text, color geometry, padding and complete-frame failure atomicity passed");

	return 0;
}
