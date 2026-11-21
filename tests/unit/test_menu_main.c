#include <stdio.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/core/error.h"
#include "engine/ui/menu_main.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Main menu line %d: %s\n", __LINE__,   \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_menu_layout layout;
static unsigned char bytes[814];

static void
write_u32le(unsigned int offset, unsigned long value)
{
	unsigned int i;

	for (i = 0; i < 4; i++)
		bytes[offset + i] = (unsigned char)(value >> (i * 8));
}

int
main(void)
{
	struct mh_sprite_collection *collection;
	struct mh_menu_main_art art, untouched;
	struct mh_menu_main_animation state, saved;
	struct mh_menu_canvas canvas;
	unsigned char screen[1024], before[1024];
	unsigned long seed, noise, old_seed, old_noise;
	unsigned int i;

	/* One SPR entry with a tiny executable sprite program. */
	write_u32le(0, 0x80000001UL);
	memcpy(bytes + 4, "dots", 4);
	write_u32le(12, 784);
	write_u32le(784, 1);
	write_u32le(788, 2);
	write_u32le(792, 2);
	write_u32le(796, 16);
	write_u32le(800, 8);
	write_u32le(804, 13);
	memcpy(bytes + 808, "\306\100\0\0\303\303", 6);
	CHECK(mh_sprite_spr_open(bytes, sizeof(bytes), MH_PICTURE_INDEXED,
		&collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_main_art_read(collection, &art, NULL) == MH_ERROR_NONE);
	CHECK(art.count[1] == 1 && art.count[4] == 0 && art.count[10] == 0);
	CHECK(art.images[1][0].pixels[0] == 0 &&
		art.images[1][0].pixels[1] == 255);
	memset(&untouched, 0xa5, sizeof(untouched));
	bytes[808] = 0xe8;
	mh_sprite_collection_close(collection);
	CHECK(mh_sprite_spr_open(bytes, sizeof(bytes), MH_PICTURE_INDEXED,
		&collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_main_art_read(collection, &untouched, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(untouched); i++)
		CHECK(((unsigned char *)&untouched)[i] == 0xa5);

	mh_sprite_collection_close(collection);
	layout.words[0][24][0] = 100;
	layout.words[0][24][1] = 200;
	layout.words[0][24][2] = 30;
	layout.words[0][24][3] = 60;

	for (i = 0; i < 4; i++)
		layout.words[0][30][i] = 500;

	CHECK(mh_menu_main_animation_init(&layout, 0, 128, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.needle.angle == 1245 && state.needle.response == 32);

	for (i = 0; i < 32; i++)
		CHECK(state.sprites.timer[i] == 0 &&
			state.sprites.wobble[i] == (i == 2 || i == 5));

	saved = state;
	CHECK(mh_menu_main_animation_init(&layout, 5, 0, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved, &state, sizeof(state)) == 0);
	memset(screen, 23, sizeof(screen));
	memcpy(before, screen, sizeof(screen));
	canvas.pixels = screen;
	canvas.size = sizeof(screen);
	canvas.pitch = 32;
	canvas.begin = 128;
	canvas.end = 640;
	seed = 123;
	noise = 456;
	old_seed = seed;
	old_noise = noise;

	/*
	 * A late sprite failure follows a valid early draw. No partial pixels,
	 * timers, RNG, row noise or needle state may become visible.
	 */
	art.count[10] = 1;
	art.images[10][0].height = 1;
	CHECK(mh_menu_main_render(&art, &layout, 0, 128, 0, 16, &state, &seed,
		&noise, &canvas, NULL, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(screen, before, sizeof(screen)) == 0 &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(seed == old_seed && noise == old_noise);
	art.count[10] = 0;
	CHECK(mh_menu_main_render(&art, &layout, 0, 0, 0, 16, &state, &seed,
		&noise, &canvas, NULL, NULL) == MH_ERROR_NONE);
	CHECK(seed != old_seed && screen[128] == 0);
	mh_menu_main_art_free(&art);
	mh_menu_main_art_free(&art);
	puts("Main menu: original bindings/defaults, opaque zero and complete frame failure atomicity passed");

	return 0;
}
