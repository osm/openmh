#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_car_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "car draw line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_car_art art;
static struct mh_menu_layout layout;
static unsigned char pixels[614400], saved_pixels[614400], glyphs[1024];

int
main(void)
{
	static const char *const names[1] = {"Car"}, *const horns[1] = {"Horn"};
	static const unsigned long divisions[1] = {300};
	struct mh_menu_car_setup_state menu;
	struct mh_menu_car_scene scene;
	struct mh_menu_car_animation animation, saved_animation;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	struct mh_wireframe model;
	struct mh_wireframe_point points[2];
	struct mh_wireframe_edge edge;
	unsigned char palette[768], saved_palette[768], background[192];
	unsigned long seed, noise, saved_seed, saved_noise;
	unsigned int i;
	float statistics[1][3];

	memset(&menu, 0, sizeof(menu));
	memset(&scene, 0, sizeof(scene));
	memset(&animation, 0, sizeof(animation));
	memset(&font, 0, sizeof(font));
	memset(points, 0, sizeof(points));
	font.image.rgba = glyphs;
	font.image.width = font.image.height = 16;
	font.cell_width = font.height = 1;
	points[0].xyz[0] = -100;
	points[1].xyz[0] = 100;
	edge.first = 0;
	edge.second = 1;
	model.point_count = 2;
	model.edge_count = 1;
	model.points = points;
	model.edges = &edge;

	for (i = 0; i < 32; i++) {
		layout.words[3][i][0] = layout.words[3][i][1] = 320;
		layout.words[3][i][2] = layout.words[3][i][3] = 200;
	}

	for (i = 0; i < 192; i++)
		background[i] = (unsigned char)(i * 29 + 7);

	statistics[0][0] = 300;
	statistics[0][1] = 2.7f;
	statistics[0][2] = 6;
	scene.names = names;
	scene.horn_names = horns;
	scene.horn_count = 1;
	scene.models = &model;
	scene.statistics = (const float (*)[3])statistics;
	scene.background_palette = background;
	scene.catalog.divisions = divisions;
	scene.catalog.count = scene.catalog.visible = scene.catalog.unlocked =
		1;
	canvas.pixels = pixels;
	canvas.size = sizeof(pixels);
	canvas.begin = 115360;
	canvas.end = 499360;
	canvas.pitch = 960;
	seed = 1;
	noise = 741118;
	menu.automatic = 1;
	animation.shown_car = 17;
	animation.fade = 0.5f;
	animation.automatic_frame = 3;
	animation.record_frame = 4;
	animation.statistics[0] = 10;
	mh_menu_car_animation_init(&animation);
	CHECK(animation.fade == 0 && animation.shown_car == 17 &&
		animation.automatic_frame == 3 && animation.record_frame == 4 &&
		animation.statistics[0] == 10 &&
		animation.sprites.wobble[2] == 1 &&
		animation.sprites.wobble[3] == 1);
	CHECK(mh_menu_car_render(&art, &font, &layout, &menu, &scene, 0, 0, 16,
		0, &animation, &seed, &noise, palette, &canvas,
		NULL) == MH_ERROR_NONE);
	CHECK(animation.shown_car == 0 && animation.fade > 0 &&
		animation.fade < 1 && animation.automatic_frame == 4 &&
		animation.record_frame == 5);
	CHECK(animation.statistics[0] > 10 && animation.statistics[0] < 300);
	CHECK(memcmp(palette + 765, background + 189, 3) == 0);
	saved_animation = animation;
	saved_seed = seed;
	saved_noise = noise;
	memcpy(saved_pixels, pixels, sizeof(pixels));
	memcpy(saved_palette, palette, sizeof(palette));

	/*
	 * Fail in the last gauge after outline, thumbnail, labels and two
	 * gauges. The transaction must retain every caller-owned result.
	 */
	statistics[0][2] = 1000000;
	CHECK(mh_menu_car_render(&art, &font, &layout, &menu, &scene, 0, 16, 16,
		0, &animation, &seed, &noise, palette, &canvas,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&animation, &saved_animation, sizeof(animation)) == 0 &&
		saved_seed == seed && saved_noise == noise &&
		memcmp(saved_pixels, pixels, sizeof(pixels)) == 0 &&
		memcmp(saved_palette, palette, sizeof(palette)) == 0);
	puts("car compositor: retained entry state, switches, followers, palette and late-error atomicity passed");

	return 0;
}
