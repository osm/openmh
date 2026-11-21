#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_background.h"
#include "engine/ui/menu_gear.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "menu background line %d: %s\n",       \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)
static struct mh_menu_background_draw output, before;

int
main(void)
{
	struct mh_menu_background state, saved;
	struct mh_menu_line lines[2];
	struct mh_menu_canvas canvas;
	unsigned char pixels[64], original[64];
	unsigned int count;
	unsigned long seed, seed_before;

	memset(&state, 0, sizeof(state));
	seed = 123;
	CHECK(mh_menu_background_reset(&state, 3, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_background_flash(&state, &seed, NULL) == MH_ERROR_NONE);
	CHECK(state.lightning_frames >= 3 && state.lightning_frames <= 10);
	saved = state;
	seed_before = seed;
	CHECK(mh_menu_background_step(&state, 2, 0, 1000, &seed, &output,
		NULL) == MH_ERROR_NONE);
	CHECK(output.line_count == 0 && output.model_count == 0 &&
		seed == seed_before);
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(mh_menu_background_step(&state, 0, 0, 16, &seed, &output, NULL) ==
		MH_ERROR_NONE);
	CHECK(output.line_count > 72 && output.model_count == 3);
	CHECK(state.lightning_frames == saved.lightning_frames - 1);
	CHECK(state.objects[0].position[2] >= 1024 &&
		state.objects[0].position[2] <= 3071);
	saved = state;
	CHECK(mh_menu_background_reset(&state, 3, NULL) == MH_ERROR_NONE);
	CHECK(state.objects[0].position[2] == 0 && state.objects[0].timer == 0);
	CHECK(state.objects[0].speed == saved.objects[0].speed &&
		state.objects[0].angle[0] == saved.objects[0].angle[0]);
	state.objects[1].model = 9;
	saved = state;
	before = output;
	seed_before = seed;
	CHECK(mh_menu_background_step(&state, 0, 1000, 16, &seed, &output,
		NULL) != MH_ERROR_NONE);
	CHECK(seed == seed_before &&
		memcmp(&state, &saved, sizeof(state)) == 0 &&
		memcmp(&output, &before, sizeof(output)) == 0);
	count = 99;
	CHECK(mh_menu_gear(100, 150, 6, 200, 0, 16, output.lines, 35, &count,
		NULL) != MH_ERROR_NONE &&
		count == 99);
	CHECK(memcmp(&output, &before, sizeof(output)) == 0);
	canvas.pixels = pixels;
	canvas.size = sizeof(pixels);
	canvas.begin = 8;
	canvas.end = 40;
	canvas.pitch = 8;
	memset(pixels, 250, sizeof(pixels));
	lines[0].x1 = 0;
	lines[0].y1 = 0;
	lines[0].x2 = 3;
	lines[0].y2 = 0;
	lines[0].brightness = 16;
	CHECK(mh_menu_lines(lines, 1, 4, 4, &canvas, NULL) == MH_ERROR_NONE);
	CHECK(pixels[8] == 255 && pixels[9] == 255 && pixels[10] == 255 &&
		pixels[11] == 250);
	lines[0].x1 = 3;
	CHECK(mh_menu_lines(lines, 1, 4, 4, &canvas, NULL) == MH_ERROR_NONE &&
		pixels[11] == 255);
	memcpy(original, pixels, sizeof(pixels));
	lines[1] = lines[0];
	lines[1].x1 = 1000000001;
	CHECK(mh_menu_lines(lines, 2, 4, 4, &canvas, NULL) != MH_ERROR_NONE &&
		memcmp(original, pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_lines(lines, 1, 4, 9, &canvas, NULL) != MH_ERROR_NONE &&
		memcmp(original, pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_lines(lines, 1, 9, 4, &canvas, NULL) != MH_ERROR_NONE &&
		memcmp(original, pixels, sizeof(pixels)) == 0);
	lines[0].x1 = -100;
	lines[0].y1 = -100;
	lines[0].x2 = 100;
	lines[0].y2 = 100;
	CHECK(mh_menu_lines(lines, 1, 4, 4, &canvas, NULL) == MH_ERROR_NONE);
	memset(pixels, 0, sizeof(pixels));
	lines[0].x1 = -20;
	lines[0].y1 = -1;
	lines[0].x2 = 20;
	lines[0].y2 = -1;
	CHECK(mh_menu_lines_rect(lines, 1, 1, -1, 4, 2, &canvas, NULL) ==
		MH_ERROR_NONE);
	CHECK(pixels[0] == 0 && pixels[1] == 16 && pixels[2] == 16 &&
		pixels[3] == 16 && pixels[4] == 0);
	memcpy(original, pixels, sizeof(pixels));
	CHECK(mh_menu_lines_rect(lines, 1, -1, -1, 4, 2, &canvas, NULL) !=
			MH_ERROR_NONE &&
		memcmp(original, pixels, sizeof(pixels)) == 0);
	CHECK(mh_menu_lines_rect(lines, 1, 1, -1, 4, 8, &canvas, NULL) !=
			MH_ERROR_NONE &&
		memcmp(original, pixels, sizeof(pixels)) == 0);
	puts("menu background: ordered reset, disabled pages, shared RNG, line bounds and atomic rejection passed");

	return 0;
}
