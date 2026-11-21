#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_skids.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/myw.h"
#include "engine/geometry/world.h"
#include "engine/render/software_skid.h"

/* Segment geometry/color, ambient/brightness, fog and one point light. */

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_projection		projection;
	struct mh_world_view		world_view;
	struct mh_model_lighting	lighting;
	struct mh_point_light		light;
	struct mh_depth_cue		cue;
	float				camera[12];
	struct mh_software_skid_view	view;
};

static int
setup_fixture(struct fixture *fixture)
{
	unsigned int i;

	memset(fixture, 0, sizeof(*fixture));
	CHECK(mh_projection_setup(320, 200, 1.3333333333333335f, 1, 1, 1000,
		&fixture->projection, NULL) == MH_ERROR_NONE);
	fixture->camera[0] = fixture->camera[4] = fixture->camera[8] = 1;
	fixture->world_view.profile = MH_RENDER_SOFTWARE;
	fixture->world_view.bounds[0] = fixture->world_view.bounds[2] =
		-1000000;
	fixture->world_view.bounds[1] = fixture->world_view.bounds[3] = 1000000;
	fixture->lighting.light_count = 1;
	fixture->lighting.lights = &fixture->light;
	fixture->light.radius = 20;
	fixture->light.intensity = 1;

	for (i = 0; i < 3; i++) {
		fixture->lighting.ambient[i] = .2f;
		fixture->lighting.brightness[i] = 1;
		fixture->light.color[i] = 1;
		fixture->cue.color[i] = (float)(80 + i * 10);
	}

	fixture->view.projection = &fixture->projection;
	fixture->view.world_view = &fixture->world_view;
	fixture->view.camera = fixture->camera;
	fixture->view.lighting = &fixture->lighting;

	return 0;
}

static int
guards(void)
{
	struct fixture fixture;
	struct mh_software_skids output;
	struct mh_skids histories[MH_SOFTWARE_SKID_CARS],
		saved[MH_SOFTWARE_SKID_CARS];
	struct mh_skid_segment *segment;
	struct mh_world broken_world;
	struct mh_software_skid_face *faces;
	unsigned int car, wheel, n;
	int feedback[MH_SOFTWARE_SKID_CARS][4];
	size_t count, capacity;

	CHECK(setup_fixture(&fixture) == 0);
	memset(&output, 0, sizeof(output));
	memset(histories, 0, sizeof(histories));

	for (car = 0; car < MH_SOFTWARE_SKID_CARS; car++)
		for (wheel = 0; wheel < 4; wheel++) {
			histories[car].wheels[wheel].count = 50;
			histories[car].wheels[wheel].first = 49;
			histories[car].wheels[wheel].partial = 1;

			for (n = 0; n < MH_SKID_SEGMENTS; n++) {
				segment = &histories[car]
						.wheels[wheel]
						.segments[n];
				segment->start[0] = (float)wheel - 2;
				segment->end[0] = segment->start[0] + 3;
				segment->start[1] = segment->end[1] = -1;
				segment->start[2] = segment->end[2] =
					10 + (float)car;
				segment->normal[1] = 1;
				segment->color[0] = 80;
				segment->color[1] = 60;
				segment->color[2] = 20;
			}
		}

	memcpy(saved, histories, sizeof(saved));
	CHECK(mh_software_skids_prepare(&output, histories,
		MH_SOFTWARE_SKID_CARS, &fixture.view,
		NULL) == MH_ERROR_NONE);
	CHECK(output.count == MH_SOFTWARE_SKID_CARS * 4 * 51 * 3);
	CHECK(memcmp(saved, histories, sizeof(saved)) == 0);
	faces = output.faces;
	capacity = output.capacity;
	count = output.count;
	memcpy(feedback, output.oldest_visible, sizeof(feedback));

	for (car = 0; car < MH_SOFTWARE_SKID_CARS; car++)
		for (wheel = 0; wheel < 4; wheel++)
			CHECK(feedback[car][wheel] == 1);

	CHECK(mh_software_skids_prepare(&output, histories,
		MH_SOFTWARE_SKID_CARS, &fixture.view,
		NULL) == MH_ERROR_NONE);
	CHECK(output.faces == faces && output.capacity == capacity &&
		output.count == count);

	/*
	 * Fail after earlier cars have prepared faces; do not publish feedback.
	 */
	histories[0].wheels[0].count = histories[0].wheels[0].partial = 0;
	histories[7].wheels[3].segments[48].normal[0] = FLT_MAX;
	CHECK(mh_software_skids_prepare(&output, histories,
		MH_SOFTWARE_SKID_CARS, &fixture.view,
		NULL) == MH_ERROR_INVALID);
	CHECK(output.count == 0 &&
		memcmp(feedback, output.oldest_visible, sizeof(feedback)) == 0);
	CHECK(output.faces == faces && output.capacity == capacity);

	/*
	 * Black marks must not inspect a broken light grid or require lighting.
	 */
	memset(histories, 0, sizeof(histories));
	memset(&broken_world, 0, sizeof(broken_world));
	fixture.view.world = &broken_world;
	fixture.view.lighting = NULL;
	histories[0].wheels[0].count = 1;
	segment = &histories[0].wheels[0].segments[0];
	segment->start[2] = 10;
	segment->end[2] = 12;
	segment->start[1] = segment->end[1] = -1;
	segment->normal[1] = 1;
	CHECK(mh_software_skids_prepare(&output, histories, 1, &fixture.view,
		NULL) == MH_ERROR_NONE);
	CHECK(output.count == 1 &&
		output.faces[0].triangles[0].color[0][0] == 0);
	segment->color[0] = 80;
	CHECK(mh_software_skids_prepare(&output, histories, 1, &fixture.view,
		NULL) == MH_ERROR_INVALID);
	CHECK(output.count == 0);
	mh_software_skids_free(&output);
	CHECK(output.faces == NULL && output.lights == NULL &&
		output.capacity == 0);

	return 0;
}

int
main(void)
{
	return guards();
}
