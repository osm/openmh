#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/menu_needle.h"
#include "engine/ui/menu_triangles.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "menu needle line %d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_needle state, saved;
	struct mh_menu_needle_draw output, before;
	struct mh_menu_point points[6];
	struct mh_menu_canvas canvas;
	unsigned char pixels[64], original[64];
	unsigned int i;

	CHECK(mh_menu_needle_init(&state, 0, 32, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_needle_step(&state, 320, 200, 1024, 2.6f, 3, 1, 0,
		&output, NULL) == MH_ERROR_NONE &&
		state.angle == 0);
	CHECK(output.points[1].x == 216 && output.points[1].y == 200 &&
		output.color == 1);
	CHECK(mh_menu_needle_step(&state, 320, 200, 1024, 2.6f, 3, 1, 150,
		&output, NULL) == MH_ERROR_NONE &&
		state.angle > 1024);
	saved = state;
	before = output;
	CHECK(mh_menu_needle_step(&state, 320, 200, 1024, 2.6f, 3, 256, 16,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0 &&
		memcmp(&output, &before, sizeof(output)) == 0);
	CHECK(mh_menu_needle_init(&state, 0, 1025, NULL) != MH_ERROR_NONE &&
		memcmp(&state, &saved, sizeof(state)) == 0);
	CHECK(mh_menu_needle_step(&state, 32768, 200, 0, 64, 256, 1, 150,
		&output, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&state, &saved, sizeof(state)) == 0 &&
		memcmp(&output, &before, sizeof(output)) == 0);
	canvas.pixels = pixels;
	canvas.size = sizeof(pixels);
	canvas.pitch = 8;
	canvas.begin = 8;
	canvas.end = 16;
	memset(pixels, 99, sizeof(pixels));
	points[0].x = 0;
	points[0].y = 0;
	points[1].x = 2;
	points[1].y = 0;
	points[2].x = 2;
	points[2].y = 2;
	CHECK(mh_menu_triangles(points, 1, 0, 1, &canvas, NULL) ==
		MH_ERROR_NONE);
	CHECK(pixels[16] == 0 && pixels[17] == 0 && pixels[18] == 0);
	CHECK(pixels[25] == 0 && pixels[26] == 0 && pixels[24] == 99);

	/*
	 * end is not a triangle clip limit, and palette index zero is opaque.
	 */
	memcpy(original, pixels, sizeof(pixels));
	memcpy(points + 3, points, sizeof(*points) * 3);
	points[5].x = 32769;
	CHECK(mh_menu_triangles(points, 2, 42, 0, &canvas, NULL) !=
			MH_ERROR_NONE &&
		memcmp(pixels, original, sizeof(pixels)) == 0);

	/*
	 * An invalid second walk must not partially draw the valid first one.
	 */
	points[3].x = 0;
	points[3].y = 0;
	points[4].x = 32768;
	points[4].y = .99999994f;
	points[5].x = 0;
	points[5].y = 1.00000012f;
	CHECK(mh_menu_triangles(points, 2, 42, 0, &canvas, NULL) !=
			MH_ERROR_NONE &&
		memcmp(pixels, original, sizeof(pixels)) == 0);

	for (i = 0; i < 3; i++)
		points[i].y = 2;

	CHECK(mh_menu_triangles(points, 1, 42, 0, &canvas, NULL) ==
			MH_ERROR_NONE &&
		memcmp(pixels, original, sizeof(pixels)) == 0);
	points[0].x = -30;
	points[0].y = -3;
	points[1].x = 30;
	points[1].y = -3;
	points[2].x = 0;
	points[2].y = 10;
	CHECK(mh_menu_triangles(points, 1, 42, 0, &canvas, NULL) ==
		MH_ERROR_NONE);
	puts("menu needle: overshoot, indexed spans, padding, bounds and failure atomicity passed");

	return 0;
}
