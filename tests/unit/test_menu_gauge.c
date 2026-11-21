#include <stdio.h>
#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_gauge.h"
#include "engine/ui/menu_triangles.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Menu gauge line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_menu_gauge_draw draw, before;

	CHECK(mh_menu_gauge(320, 200, 41.5f, 0, 4, &draw, NULL) ==
			MH_ERROR_NONE &&
		draw.count == 8);
	CHECK(draw.points[0].x == 320 && draw.points[0].y == 200 &&
		draw.color == 4);
	CHECK(mh_menu_gauge(320, 200, 35.5f, 1, 4, &draw, NULL) ==
			MH_ERROR_NONE &&
		draw.count == 1);
	CHECK(draw.points[1].x == draw.points[2].x &&
		draw.points[1].y == draw.points[2].y);
	before = draw;
	CHECK(mh_menu_gauge(320, 200, 35.5f, 17, 4, &draw, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&before, &draw, sizeof(draw)) == 0);
	CHECK(mh_menu_gauge(320, 200, -1, 0, 4, &draw, NULL) != MH_ERROR_NONE &&
		memcmp(&before, &draw, sizeof(draw)) == 0);
	CHECK(mh_menu_gauge(320, 200, 35.5f, 0, 256, &draw, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&before, &draw, sizeof(draw)) == 0);
	CHECK(mh_menu_gauge(320, 200, 35.5f, 0, 4, NULL, NULL) !=
		MH_ERROR_NONE);
	puts("Menu gauge empty/full fan and failure atomicity passed");

	return 0;
}
