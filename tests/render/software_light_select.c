#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_light_select.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/myw.h"
#include "engine/geometry/world.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
guards(void)
{
	struct mh_world world;
	struct mh_point_light lights[2], saved[2];
	struct mh_point_light source;
	struct mh_world_light_link links[2];
	struct mh_world_cell grid;
	float position[3];
	size_t count, cell;

	memset(&world, 0, sizeof(world));
	memset(&source, 0, sizeof(source));
	memset(&grid, 0, sizeof(grid));
	memset(position, 0, sizeof(position));
	memset(lights, 0, sizeof(lights));
	world.width = world.height = world.cell_count = 1;
	world.cell_size[0] = world.cell_size[1] = 19;
	world.light_count = 1;
	world.link_count = 2;
	world.lights = &source;
	world.cells = &grid;
	world.links = links;
	links[0].light = links[1].light = 0;
	links[0].next = 1;
	links[1].next = MH_WORLD_NO_LIGHT;
	count = 99;
	memcpy(saved, lights, sizeof(saved));
	CHECK(mh_software_light_select(&world, position, lights, 1, &count,
		NULL) == MH_ERROR_RANGE);
	CHECK(count == 99 && memcmp(lights, saved, sizeof(lights)) == 0);
	CHECK(mh_software_skid_light_select(&world, position, lights, 1, &count,
		NULL) == MH_ERROR_RANGE);
	CHECK(count == 99 && memcmp(lights, saved, sizeof(lights)) == 0);
	CHECK(mh_software_light_select(&world, position, lights, 2, &count,
		NULL) == MH_ERROR_NONE &&
		count == 2);
	CHECK(memcmp(lights, lights + 1, sizeof(*lights)) == 0);
	CHECK(mh_software_skid_light_select(&world, position, lights, 2, &count,
		NULL) == MH_ERROR_NONE &&
		count == 2);
	CHECK(memcmp(lights, lights + 1, sizeof(*lights)) == 0);
	position[0] = -.5f;
	CHECK(mh_software_light_cell(&world, position, &cell, NULL) ==
			MH_ERROR_NONE &&
		cell == 0);
	position[0] = -19;
	CHECK(mh_software_light_select(&world, position, NULL, 0, &count,
		NULL) == MH_ERROR_NONE &&
		count == 0);
	position[0] = 19;
	CHECK(mh_software_light_cell(&world, position, &cell, NULL) ==
			MH_ERROR_NONE &&
		cell == 1);
	cell = 99;
	world.cell_size[0] = 0;
	CHECK(mh_software_light_cell(&world, position, &cell, NULL) ==
			MH_ERROR_INVALID &&
		cell == 99);
	world.cell_size[0] = 19;
	position[0] = FLT_MAX;
	world.grid_minimum[0] = -FLT_MAX;
	CHECK(mh_software_light_cell(&world, position, &cell, NULL) ==
			MH_ERROR_RANGE &&
		cell == 99);

	return 0;
}

int
main(void)
{
	return guards();
}
