#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_ghost_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Ghost draw line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_ghost_art art;
static struct mh_menu_layout layout;
static unsigned char pixels[614400], saved_pixels[614400], glyphs[1024];

int
main(void)
{
	static const char *const names[] = {"a", "b", "c", "d", "e"};
	const char *invalid_names[5];
	struct mh_menu_ghost_state menu, saved_menu;
	struct mh_menu_ghost_animation animation, saved_animation;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	struct mh_sprite_indexed marker;
	unsigned char marker_pixels[4];
	unsigned long seed, noise, saved_seed, saved_noise;
	unsigned int i;
	size_t origin;

	memset(&menu, 0, sizeof(menu));
	memset(&animation, 0, sizeof(animation));
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
	CHECK(mh_menu_list_draw(&font, names, 5, 4, 4, 1, NULL, 60, 10, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 10 * 960 + 60] == 2 &&
		pixels[origin + 25 * 960 + 60] == 2);
	CHECK(pixels[origin + 40 * 960 + 60] == 5 &&
		pixels[origin + 39 * 960 + 56] == 2);
	CHECK(pixels[origin + 50 * 960 + 64] == 2 &&
		pixels[origin + 50 * 960 + 65] == 99);

	/* No bottom-edge backfill. */
	CHECK(pixels[origin + 55 * 960 + 60] == 99);
	memset(pixels, 99, sizeof(pixels));
	CHECK(mh_menu_list_draw(&font, names, 5, 0, 4, 0, NULL, 60, 10, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[origin + 10 * 960 + 60] == 5 &&
		pixels[origin + 9 * 960 + 56] == 1);
	CHECK(pixels[origin + 25 * 960 + 60] == 1 &&
		pixels[origin + 55 * 960 + 60] == 1);
	memcpy(saved_pixels, pixels, sizeof(pixels));

	for (i = 0; i < 5; i++)
		invalid_names[i] = names[i];

	invalid_names[4] = "";
	CHECK(mh_menu_list_draw(&font, invalid_names, 5, 0, 1, 1, NULL, 60, 10,
		&canvas, NULL) != MH_ERROR_NONE);

	/* Validate even hidden names. */
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_list_draw(&font, names, 5, 5, 4, 1, NULL, 60, 10, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_menu_list_draw(&font, NULL, 0, 0, 4, 1, NULL, 60, 10, &canvas,
		NULL) == MH_ERROR_NONE);
	memset(&marker, 0, sizeof(marker));
	marker.pixels = marker_pixels;
	marker.width = 1;
	marker.height = 1;
	CHECK(mh_menu_list_draw(&font, names, 5, 0, 4, 1, &marker, 60, 10,
		&canvas, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);

	for (i = 0; i < 32; i++) {
		layout.words[19][i][0] = layout.words[19][i][1] = 320;
		layout.words[19][i][2] = layout.words[19][i][3] = 100;
	}

	animation.frames[4] = 7;
	animation.frames[30] = 99;
	animation.sprites.timer[3] = 20;
	mh_menu_ghost_animation_init(&animation);
	CHECK(animation.frames[4] == 7 && animation.frames[30] == 99 &&
		animation.sprites.timer[3] == 0);
	seed = 1;
	noise = 741118;
	menu.mode = 6;
	menu.count = 5;
	CHECK(mh_menu_ghost_render(&art, &font, &layout, &menu, names, 0, 0, 16,
		&animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.mode == 3 && animation.frames[4] == 3 &&
		animation.frames[5] == 0 && animation.frames[6] == 1 &&
		animation.frames[20] == 1 && animation.frames[30] == 99);
	menu.focus = 1;
	CHECK(mh_menu_ghost_render(&art, &font, &layout, &menu, names, 0, 128,
		16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.mode == 6 && animation.frames[4] == 0 &&
		animation.frames[5] == 7);
	menu.focus = 2;
	CHECK(mh_menu_ghost_render(&art, &font, &layout, &menu, names, 0, 384,
		16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.mode == 6 && animation.frames[4] == 0 &&
		animation.frames[5] == 4 && animation.frames[6] == 0 &&
		animation.frames[20] == 0);
	menu.focus = 0;
	saved_animation = animation;
	saved_menu = menu;
	saved_seed = seed;
	saved_noise = noise;
	memcpy(saved_pixels, pixels, sizeof(pixels));

	/*
	 * Reject at the list after animation/RNG updates; preserve all outputs.
	 */
	CHECK(mh_menu_ghost_render(&art, &font, &layout, &menu, invalid_names,
		0, 16, 16, &animation, &seed, &noise, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&animation, &saved_animation, sizeof(saved_animation)) ==
			0 &&
		memcmp(&menu, &saved_menu, sizeof(menu)) == 0 &&
		saved_seed == seed && saved_noise == noise &&
		memcmp(saved_pixels, pixels, sizeof(pixels)) == 0);
	art.patched = menu.patched = 1;
	menu.focus = MH_MENU_GHOST_FOCUS_BENCHMARK;
	CHECK(mh_menu_ghost_render(&art, &font, &layout, &menu, names, 0, 128,
		16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.mode == 6 && menu.benchmark && animation.frames[4] == 0 &&
		animation.frames[5] == 0 && animation.frames[6] == 7 &&
		animation.frames[10] == 1 && animation.frames[20] == 1);
	menu.focus = MH_MENU_GHOST_FOCUS_PATCHED_RECORDINGS;
	CHECK(mh_menu_ghost_render(&art, &font, &layout, &menu, names, 0, 384,
		16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.benchmark && animation.frames[6] == 4 &&
		animation.frames[10] == 0 && animation.frames[20] == 0);
	menu.focus = MH_MENU_GHOST_FOCUS_DEMO;
	CHECK(mh_menu_ghost_render(&art, &font, &layout, &menu, names, 0, 128,
		16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.mode == 6 && !menu.benchmark && animation.frames[5] == 7 &&
		animation.frames[6] == 0);
	menu.focus = MH_MENU_GHOST_FOCUS_RACE;
	CHECK(mh_menu_ghost_render(&art, &font, &layout, &menu, names, 0, 128,
		16, &animation, &seed, &noise, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(menu.mode == 3 && !menu.benchmark && animation.frames[4] == 7 &&
		animation.frames[5] == 0 && animation.frames[6] == 0);
	puts("Ghost list bounds, colors, scrolling, retained frames, draw-time mode and atomic failures passed");

	return 0;
}
