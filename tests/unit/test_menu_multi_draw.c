#include <stdio.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/core/error.h"
#include "engine/ui/menu_multi_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Multi draw line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_multi_art art;
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
	static const char *const names[2] = {"", "Visible"};
	struct mh_menu_multi_address addresses[2];
	struct mh_menu_multi_catalog catalog;
	struct mh_menu_multi_state menu, saved_menu;
	struct mh_menu_multi_animation animation, saved_animation;
	struct mh_menu_multi_output output;
	struct mh_menu_canvas canvas;
	struct mh_menu_multi_art rejected_art;
	struct mh_settings settings;
	struct mh_sprite_collection *collection;
	struct mh_font font;
	unsigned long seed, noise, saved_seed, saved_noise;
	unsigned int i;
	size_t origin;
	char invalid_name[256];

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
	CHECK(mh_menu_record_list_draw(&font, names, 2, 0, 8, 1, NULL, 60, 10,
		&canvas, NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 9 * 960 + 56] == 2 &&
		pixels[origin + 9 * 960 + 63] == 2);
	CHECK(pixels[origin + 9 * 960 + 64] == 99 &&
		pixels[origin + 25 * 960 + 60] == 2);
	CHECK(mh_menu_record_list_draw(&font, names, 2, 1, 8, 0, NULL, 60, 10,
		&canvas, NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 24 * 960 + 56] == 1 &&
		pixels[origin + 25 * 960 + 60] == 5);
	memcpy(saved_pixels, pixels, sizeof(pixels));
	CHECK(mh_menu_list_draw(&font, names, 2, 0, 8, 1, NULL, 60, 10, &canvas,
		NULL) != MH_ERROR_NONE);

	/* Filename API retains its restriction. */
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);

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
	CHECK(mh_menu_multi_art_read(collection, &art, NULL) == MH_ERROR_NONE &&
		art.count[1] == 1 && art.count[28] == 0);
	memset(&rejected_art, 0xa5, sizeof(rejected_art));
	sprite_bytes[808] = 0xe8;
	mh_sprite_collection_close(collection);
	CHECK(mh_sprite_spr_open(sprite_bytes, sizeof(sprite_bytes),
		MH_PICTURE_INDEXED, &collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_multi_art_read(collection, &rejected_art, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(rejected_art); i++)
		CHECK(((unsigned char *)&rejected_art)[i] == 0xa5);

	mh_sprite_collection_close(collection);
	memset(&menu, 0, sizeof(menu));
	memset(&output, 0, sizeof(output));
	CHECK(mh_settings_init(&settings, "Player", 255, 255, 0, NULL) ==
		MH_ERROR_NONE);
	settings.protocol = 1;
	addresses[0].name = "";
	addresses[1].name = "TCP/IP";
	addresses[0].ip = addresses[1].ip = "127.0.0.1";
	catalog.addresses = addresses;
	catalog.address_count = 2;
	catalog.sessions = names;
	catalog.session_count = 2;
	catalog.protected_count = 2;
	mh_menu_multi_animation_init(&animation);
	seed = 1;
	noise = 741118;

	for (i = 0; i < 32; i++) {
		CHECK(animation.sprites.timer[i] == 0 &&
			animation.sprites.wobble[i] == (i == 2 || i == 3));
		layout.words[9][i][0] = layout.words[9][i][1] = 30;
		layout.words[9][i][2] = layout.words[9][i][3] = 10;
	}

	menu.phase = 4;
	menu.address = menu.session = 9;
	saved_menu = menu;
	saved_animation = animation;
	saved_seed = seed;
	saved_noise = noise;
	memcpy(saved_pixels, pixels, sizeof(pixels));
	art.count[28] = 9;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&menu, &saved_menu, sizeof(menu)) == 0 &&
		memcmp(&animation, &saved_animation, sizeof(saved_animation)) ==
			0);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0 &&
		seed == saved_seed && noise == saved_noise);
	art.count[28] = 0;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.address == 1 && menu.session == 1 &&
		animation.frames[20] == 0);
	memcpy(saved_pixels, pixels, sizeof(pixels));
	saved_animation = animation;
	saved_menu = menu;
	saved_seed = seed;
	saved_noise = noise;
	memset(invalid_name, 'X', sizeof(invalid_name));
	addresses[0].name = invalid_name;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&menu, &saved_menu, sizeof(menu)) == 0 &&
		memcmp(&animation, &saved_animation, sizeof(saved_animation)) ==
			0);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0 &&
		seed == saved_seed && noise == saved_noise);
	addresses[0].name = "";
	catalog.session_count = 0;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	menu.phase = MH_MULTI_SEARCHING;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	catalog.address_count = catalog.protected_count = 1;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.address == 0 && animation.frames[19] == 0 &&
		animation.frames[18] == 1);
	catalog.address_count = 2;
	menu.address = 1;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(animation.frames[19] == 1 && animation.frames[18] == 0);

	/* Native catalogs have no protected row and can be entirely empty. */
	catalog.protected_count = 0;
	catalog.address_count = 1;
	menu.phase = MH_MULTI_EDIT;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.address == 0);
	catalog.address_count = 0;
	catalog.addresses = NULL;
	menu.address = 8;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.address == 0);
	menu.phase = MH_MULTI_ADD_ADDRESS;
	CHECK(mh_menu_multi_render(&art, &font, &layout, &menu, &settings,
		&catalog, 0, 0, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);

	/* Search notice must roll back if a later cursor position fails. */
	memcpy(saved_pixels, pixels, sizeof(pixels));
	output.searching = output.cursor = 1;
	output.cursor_slot = 15;
	output.cursor_width = 16711425UL;
	CHECK(mh_menu_multi_overlay(&font, &layout, &output, 0, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	output.searching = 0;
	output.cursor_width = 5;
	CHECK(mh_menu_multi_overlay(&font, &layout, &output, 0, &canvas,
		NULL) == MH_ERROR_NONE);

	/* slot15 +24 +29 X; slot15 +24 +30 Y. */
	CHECK(pixels[origin + 30 * 960 + 95] == 2);
	mh_menu_multi_art_free(&art);
	mh_menu_multi_art_free(&art);

	return 0;
}
