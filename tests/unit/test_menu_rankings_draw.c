#include <limits.h>
#include <stdio.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_rankings_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Rankings draw line %d: %s\n",         \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_rankings_art art;
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
	struct mh_menu_rankings_art rejected_art;
	struct mh_menu_rankings_animation animation, saved_animation;
	struct mh_menu_rankings_state menu;
	struct mh_highscore_table table, saved_table;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	const char *tracks[2] = {"RedRock", "NolbyHills"};
	unsigned long seed, noise, saved_seed, saved_noise;
	unsigned int i;
	size_t origin;

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
	CHECK(mh_menu_rankings_art_read(collection, &art, NULL) ==
			MH_ERROR_NONE &&
		art.count[1] == 1 && art.count[8] == 0);
	memset(&rejected_art, 0xa5, sizeof(rejected_art));
	sprite_bytes[808] = 0xe8;
	mh_sprite_collection_close(collection);
	CHECK(mh_sprite_spr_open(sprite_bytes, sizeof(sprite_bytes),
		MH_PICTURE_INDEXED, &collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_rankings_art_read(collection, &rejected_art, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(rejected_art); i++)
		CHECK(((unsigned char *)&rejected_art)[i] == 0xa5);

	mh_sprite_collection_close(collection);

	for (i = 0; i < 4; i++) {
		layout.words[11][24 + i][0] = layout.words[11][24 + i][1] =
			10 + i * 140;
		layout.words[11][16 + i][0] = layout.words[11][16 + i][1] =
			10 + i * 140;
	}

	layout.words[11][24][2] = layout.words[11][24][3] = 20;
	layout.words[11][16][2] = layout.words[11][16][3] = 140;
	layout.words[11][17][2] = layout.words[11][17][3] = 15;
	mh_menu_rankings_defaults(&menu);
	menu.category = 2;
	mh_menu_rankings_animation_init(&animation);
	CHECK(mh_highscore_defaults(2, 5, &table, NULL) == MH_ERROR_NONE);
	seed = 1;
	noise = 741118;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_rankings_render(&art, &font, &layout, &menu, tracks, 2,
		&table, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 140 * 960 + 10] == 1 &&
		pixels[origin + 275 * 960 + 10] == 1);
	CHECK(pixels[origin + 140 * 960 + 369] == 1 &&
		pixels[origin + 140 * 960 + 393] == 1 &&
		pixels[origin + 140 * 960 + 413] == 1);
	CHECK(animation.frames[4] == 0 && animation.frames[7] == 1 &&
		animation.frames[20] == 1);

	/* Unterminated disk fields are drawn only to their own boundary. */
	memset(table.rows[0].name, 0x99, 24);
	memset(table.rows[0].car, 'X', 16);
	saved_table = table;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_rankings_render(&art, &font, &layout, &menu, tracks, 2,
		&table, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 140 * 960 + 33] == 1 &&
		pixels[origin + 140 * 960 + 34] == 99);
	CHECK(pixels[origin + 140 * 960 + 165] == 1 &&
		pixels[origin + 140 * 960 + 166] == 99);
	CHECK(memcmp(&table, &saved_table, sizeof(table)) == 0);
	menu.category = 3;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_rankings_render(&art, &font, &layout, &menu, tracks, 2,
		&table, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);

	/* Category 3 has no fourth visible label/highlight. */
	CHECK(pixels[origin + 20 * 960 + 10] == 2);
	CHECK(pixels[origin + 140 * 960 + 153] == 1 &&
		pixels[origin + 140 * 960 + 154] == 99);
	menu.category = 0;
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_rankings_render(&art, &font, &layout, &menu, tracks, 2,
		&table, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 140 * 960 + 506] == 1 &&
		pixels[origin + 140 * 960 + 510] == 99);
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_rankings_render(&art, &font, &layout, &menu, tracks, 2,
		NULL, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 140 * 960 + 10] == 99);
	saved_animation = animation;
	saved_seed = seed;
	saved_noise = noise;
	memcpy(saved_pixels, pixels, sizeof(pixels));
	art.marker_count = 9;
	CHECK(mh_menu_rankings_render(&art, &font, &layout, &menu, tracks, 2,
		&table, 0, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0 &&
		memcmp(&saved_animation, &animation, sizeof(saved_animation)) ==
			0 &&
		seed == saved_seed && noise == saved_noise);
	art.marker_count = 0;
	CHECK(mh_menu_rankings_render(&art, &font, &layout, &menu, tracks, 2,
		&table, 257, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
#if ULONG_MAX > MH_U32_MAX
	table.rows[9].lap_ms = MH_U32_MAX + 1UL;
	CHECK(mh_menu_rankings_render(&art, &font, &layout, &menu, tracks, 2,
		&table, 0, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
#endif
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0 &&
		memcmp(&saved_animation, &animation, sizeof(saved_animation)) ==
			0 &&
		seed == saved_seed && noise == saved_noise);
	mh_menu_rankings_art_free(&art);
	mh_menu_rankings_art_free(&art);
	puts("Rankings draw: artwork bounds, score columns, retained category, bounded raw text, absent table and atomic failures passed");

	return 0;
}
