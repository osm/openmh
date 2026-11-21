#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_single.h"
#include "engine/ui/menu_single_draw.h"
#include "engine/ui/menu_text.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Single menu line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_menu_layout layout;
static struct mh_menu_single_art art;

int
main(void)
{
	struct mh_menu_single_state state, saved_state;
	struct mh_menu_input keys;
	struct mh_menu_output output;
	struct mh_menu_single_summary summary, saved_summary;
	struct mh_menu_single_details details;
	struct mh_menu_single_item cars[3], tracks[2];
	struct mh_menu_single_animation animation, saved_animation;
	struct mh_menu_canvas canvas;
	struct mh_font font;
	unsigned char pixels[64], saved_pixels[64], glyphs[1024];
	unsigned long seed, noise;
	unsigned int i;

	memset(&keys, 0, sizeof(keys));
	state.selected = 0;
	state.mode = 3;
	state.car = 2;
	keys.left = keys.right = keys.enter = keys.escape = 1;
	CHECK(mh_menu_single_update(&state, 0, &keys, &output, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.selected == 1 && state.mode == 2 && output.count == 6);
	CHECK(output.events[0].value == 6 && output.events[1].value == 5);
	CHECK(output.events[3].kind == MH_MENU_PAGE &&
		output.events[3].value == MH_MENU_RACE_SETUP);
	CHECK(output.events[5].kind == MH_MENU_PAGE &&
		output.events[5].value == MH_MENU_MAIN);
	CHECK(mh_menu_single_leave(&state, NULL) == MH_ERROR_NONE &&
		state.mode == 0);
	state.selected = 0;
	state.mode = 1;
	CHECK(mh_menu_single_leave(&state, NULL) == MH_ERROR_NONE &&
		state.mode == 1);
	memset(&details, 0, sizeof(details));
	details.cars = cars;
	details.car_count = 3;
	details.tracks = tracks;
	details.track_count = 2;
	details.stage = 3;
	details.laps = 3;
	details.demo_name = "demo.dem";
	cars[0].name = "First";
	cars[0].division = 101;
	cars[1].name = "Special";
	cars[1].division = 110;
	cars[2].name = "Last";
	cars[2].division = 401;
	tracks[0].name = "Locked";
	tracks[0].division = 400;
	tracks[1].name = "Available";
	tracks[1].division = 100;
	CHECK(mh_menu_single_summary(&layout, 0, &state, &details, &summary,
		NULL) == MH_ERROR_NONE);

	/* Ordinary count is two, so the prefix includes Special, not Last. */
	CHECK(state.car == 1 && summary.count == 12 &&
		strcmp(summary.labels[7].text, "Special") == 0);
	CHECK(strcmp(summary.labels[11].text, "Available") == 0 &&
		strcmp(summary.labels[8].text, "3") == 0);
	details.race_index = 1;
	CHECK(mh_menu_single_summary(&layout, 0, &state, &details, &summary,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(summary.labels[11].text, "Season complete") == 0);
	state.car = 2;
	details.race_index = 2;
	saved_state = state;
	saved_summary = summary;
	CHECK(mh_menu_single_summary(&layout, 0, &state, &details, &summary,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&summary, &saved_summary, sizeof(summary)) == 0);
	state.mode = 3;
	details.track = 99;
	CHECK(mh_menu_single_summary(&layout, 0, &state, &details, &summary,
		NULL) == MH_ERROR_NONE &&
		summary.count == 10);
	CHECK(strcmp(summary.labels[7].text, "demo.dem") == 0 &&
		strcmp(summary.labels[8].text, "-") == 0);
	state.selected = 5;
	saved_state = state;
	CHECK(mh_menu_single_update(&state, 1, &keys, &output, NULL, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&state, &saved_state, sizeof(state)) == 0);
	CHECK(mh_menu_single_leave(&state, NULL) != MH_ERROR_NONE &&
		memcmp(&state, &saved_state, sizeof(state)) == 0);
	state.selected = 0;
	state.mode = 1;
	state.car = 2;
	details.race_index = 0;
	details.track = 0;
	CHECK(mh_menu_single_animation_init(&layout, 0, 0, &animation, NULL) ==
		MH_ERROR_NONE);
	canvas.pixels = pixels;
	canvas.size = canvas.end = sizeof(pixels);
	canvas.begin = 0;
	canvas.pitch = 8;
	memset(pixels, 73, sizeof(pixels));
	memcpy(saved_pixels, pixels, sizeof(pixels));
	saved_state = state;
	saved_animation = animation;
	seed = 123;
	noise = 741118;

	/*
	 * Font failure occurs after sprite/needle work in scratch. In
	 * particular the league car clamp must not escape the failed complete
	 * frame.
	 */
	CHECK(mh_menu_single_render(&art, NULL, &layout, &state, &details, 0,
		16, 16, &animation, &seed, &noise, &canvas, NULL,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved_state, sizeof(state)) == 0 &&
		memcmp(&animation, &saved_animation, sizeof(animation)) == 0);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0 &&
		seed == 123 && noise == 741118);
	memset(&font, 0, sizeof(font));
	memset(glyphs, 255, sizeof(glyphs));
	font.image.rgba = glyphs;
	font.image.width = font.image.height = 16;
	font.height = font.cell_width = 1;

	for (i = 0; i < 256; i++)
		font.advance[i] = 1;

	CHECK(mh_menu_text_draw(&font, (const unsigned char *)"A", 1, 0, 0, 1,
		&canvas, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_text_draw(&font, (const unsigned char *)"A", 2, 262144,
		262144, 1, &canvas, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(pixels, saved_pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_single_render(&art, &font, &layout, &state, &details, 0,
		16, 16, &animation, &seed, &noise, &canvas, NULL,
		NULL) == MH_ERROR_NONE);
	CHECK(state.car == 1 &&
		memcmp(pixels, saved_pixels, sizeof(pixels)) != 0);
	mh_menu_single_art_free(&art);
	puts("Single-player departure, input ordering, league prefix eligibility and summary rollback passed");

	return 0;
}
