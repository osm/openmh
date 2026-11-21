#include <stdio.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/ui/menu_options.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Options line %d: %s\n", __LINE__,     \
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

static int
invalid_inputs(void)
{
	struct mh_menu_options_state state, saved;
	struct mh_menu_input input;
	struct mh_menu_output output, previous;
	unsigned int i;

	memset(&input, 0, sizeof(input));
	input.left = input.right = input.enter = input.escape = 1;

	for (i = 0; i < 5; i++) {
		state.selected = i == 0 ? 5 : 0;
		state.difficulty = i == 1 ? 3 : 0;
		memset(&output, 0xa5, sizeof(output));
		saved = state;
		previous = output;
		CHECK(mh_menu_options_update(i == 2 ? NULL : &state,
			i == 3 ? NULL : &input, i == 4 ? NULL : &output,
			NULL, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&state, &saved, sizeof(state)) == 0 &&
			memcmp(&output, &previous, sizeof(output)) == 0);
	}

	return 0;
}

int
main(void)
{
	struct mh_sprite_collection *collection;
	struct mh_menu_options_art art, untouched;
	struct mh_menu_options_animation state, saved;
	struct mh_menu_options_state menu;
	struct mh_menu_canvas canvas;
	unsigned char screen[1024], before[1024];
	unsigned long seed, noise, old_seed, old_noise;
	unsigned int i;

	CHECK(invalid_inputs() == 0);
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
	CHECK(mh_menu_options_art_read(collection, &art, NULL) ==
		MH_ERROR_NONE);
	CHECK(art.count[1] == 1 && art.count[11] == 0);
	memset(&untouched, 0xa5, sizeof(untouched));
	bytes[808] = 0xe8;
	mh_sprite_collection_close(collection);
	CHECK(mh_sprite_spr_open(bytes, sizeof(bytes), MH_PICTURE_INDEXED,
		&collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_options_art_read(collection, &untouched, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(untouched); i++)
		CHECK(((unsigned char *)&untouched)[i] == 0xa5);

	mh_sprite_collection_close(collection);
	layout.words[7][24][0] = 100;
	layout.words[7][24][1] = 200;
	layout.words[7][24][2] = 30;
	layout.words[7][24][3] = 60;

	for (i = 0; i < 4; i++)
		layout.words[7][30][i] = 500;

	memset(&state, 0, sizeof(state));
	state.difficulty_frame = 8;
	CHECK(mh_menu_options_animation_init(&layout, 0, 128, &state, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.needle.angle == 1245 && state.needle.response == 32 &&
		state.difficulty_frame == 8);

	for (i = 0; i < 32; i++)
		CHECK(state.sprites.timer[i] == 0 &&
			state.sprites.wobble[i] == (i == 2 || i == 3));

	saved = state;
	CHECK(mh_menu_options_animation_init(&layout, 5, 0, &state, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved, &state, sizeof(state)) == 0);
	memset(screen, 23, sizeof(screen));
	memcpy(before, screen, sizeof(screen));
	canvas.pixels = screen;
	canvas.size = sizeof(screen);
	canvas.pitch = 32;
	canvas.begin = 128;
	canvas.end = 640;
	seed = old_seed = 123;
	noise = old_noise = 456;
	menu.selected = 0;
	menu.difficulty = 2;

	/*
	 * The final sprite fails after an earlier draw and follower update.
	 * Pixels, timers, follower, RNG and row noise must all be preserved.
	 */
	art.count[11] = 17;
	art.images[11][9].height = 1;
	CHECK(mh_menu_options_render(&art, &layout, &menu, 128, 0, 16, 0,
		&state, &seed, &noise, &canvas, NULL,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(screen, before, sizeof(screen)) == 0 &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(seed == old_seed && noise == old_noise);
	art.count[11] = 0;
	CHECK(mh_menu_options_render(&art, &layout, &menu, 0, 0, 16, 0, &state,
		&seed, &noise, &canvas, NULL, NULL) == MH_ERROR_NONE);
	CHECK(seed != old_seed && screen[128] == 0 &&
		state.difficulty_frame == 9);
	menu.difficulty = 0;
	CHECK(mh_menu_options_render(&art, &layout, &menu, 0, 0, 16, 1, &state,
		&seed, &noise, &canvas, NULL, NULL) == MH_ERROR_NONE);
	CHECK(state.difficulty_frame == 8);
	mh_menu_options_art_free(&art);
	mh_menu_options_art_free(&art);
	puts("Options: atomic bounds, original bindings, retained difficulty follower and complete frame failure atomicity passed");

	return 0;
}
