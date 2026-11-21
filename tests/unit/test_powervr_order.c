#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/render/powervr_order.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#c);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
region_bounds(void)
{
	struct mh_powervr_order_vertex vertices[3], swap;
	struct mh_powervr_order_triangle triangle;
	struct mh_water_frame water;

	memset(&triangle, 0, sizeof(triangle));
	triangle.call = 4;
	triangle.first = 9;
	vertices[0].xy[0] = vertices[2].xy[0] = -1;
	vertices[0].xy[1] = vertices[1].xy[1] = 1;
	vertices[1].xy[0] = 64.0f / 65 - 1;
	vertices[2].xy[1] = 1 - 64.0f / 70;
	vertices[0].w = 16;
	vertices[1].w = 8;
	vertices[2].w = 4;
	CHECK(mh_powervr_order_bounds(vertices, 65, 70, NULL, &triangle));
	CHECK(triangle.left == 0 && triangle.right == 0 && triangle.top == 0 &&
		triangle.bottom == 0);
	CHECK(triangle.winding == 0 &&
		fabs(triangle.depth - 0.000625) < 0.00000001);
	CHECK(triangle.call == 4 && triangle.first == 9);
	memset(&water, 0, sizeof(water));
	water.scale = 0.5f;
	CHECK(mh_powervr_order_bounds(vertices, 65, 70, &water, &triangle));
	CHECK(triangle.left == 1 && triangle.right == 1 && triangle.top == 1 &&
		triangle.bottom == 1);
	swap = vertices[1];
	vertices[1] = vertices[2];
	vertices[2] = swap;
	CHECK(mh_powervr_order_bounds(vertices, 65, 70, NULL, &triangle) &&
		triangle.winding == 1);
	vertices[0].w = 0;
	CHECK(!mh_powervr_order_bounds(vertices, 65, 70, NULL, &triangle));
	vertices[0] = vertices[1];
	CHECK(!mh_powervr_order_bounds(vertices, 65, 70, NULL, &triangle));

	return 0;
}

static int
region_order(void)
{
	struct mh_powervr_order_triangle triangles[5];
	struct mh_powervr_order_set sets[8];
	size_t item;

	memset(triangles, 0, sizeof(triangles));
	triangles[0].depth = 0.2f;
	triangles[1].depth = 0.5f;
	triangles[1].right = 1;
	triangles[2].call = 1;
	triangles[2].depth = 0.4f;
	triangles[2].right = 1;
	triangles[3].call = 2;
	triangles[3].depth = 0.4f;
	triangles[3].winding = 1;
	triangles[4].call = 3;
	triangles[4].depth = 0.1f;
	triangles[4].left = triangles[4].right = 1;
	item = mh_powervr_order_region(triangles, 5, sets, 4, 0, 0);
	CHECK(item != MH_POWERVR_NO_ITEM && sets[item].call == 2);
	item = sets[item].next;
	CHECK(item != MH_POWERVR_NO_ITEM && sets[item].call == 1);
	item = sets[item].next;
	CHECK(item != MH_POWERVR_NO_ITEM && sets[item].call == 0);
	CHECK(sets[item].first == 0 && triangles[0].next == 1 &&
		triangles[1].next == MH_POWERVR_NO_ITEM);
	CHECK(sets[item].next == MH_POWERVR_NO_ITEM);
	item = mh_powervr_order_region(triangles, 5, sets, 4, 1, 0);
	CHECK(item != MH_POWERVR_NO_ITEM && sets[item].call == 3);
	item = sets[item].next;
	CHECK(item != MH_POWERVR_NO_ITEM && sets[item].call == 1);
	item = sets[item].next;
	CHECK(item != MH_POWERVR_NO_ITEM && sets[item].call == 0);
	CHECK(sets[item].first == 1 && triangles[1].next == MH_POWERVR_NO_ITEM);
	CHECK(sets[item].next == MH_POWERVR_NO_ITEM);
	CHECK(mh_powervr_order_region(triangles, 5, sets, 4, 2, 0) ==
		MH_POWERVR_NO_ITEM);

	return 0;
}

int
main(void)
{
	return region_bounds() || region_order();
}
