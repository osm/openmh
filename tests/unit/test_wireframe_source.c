#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../support/wireframe_source.h"
#include "engine/geometry/wireframe.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "wireframe source:%d: %s\n", __LINE__, \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	static const unsigned char key[2] = {0, 0};
	static const unsigned char bad_key[2] = {1, 0};
	struct mh_test_wireframe_source fixture;
	struct mh_wireframe_source source;
	struct mh_resource_entry entries[2];
	const struct mh_resource_source *resources;
	struct mh_wireframe output, saved;
	float scale;

	memset(&fixture, 0, sizeof(fixture));
	memset(&output, 0, sizeof(output));
	fixture.count = 1;
	fixture.meshes[0].point_count = 2;
	fixture.meshes[0].edge_count = 1;
	fixture.meshes[0].points = calloc(2, sizeof(*fixture.meshes[0].points));
	fixture.meshes[0].edges = calloc(1, sizeof(*fixture.meshes[0].edges));
	CHECK(fixture.meshes[0].points != NULL &&
		fixture.meshes[0].edges != NULL);
	fixture.meshes[0].points[0].xyz[0] = 4;
	fixture.meshes[0].points[1].xyz[0] = 12;
	fixture.meshes[0].edges[0].second = 1;
	entries[0].path = "memory/Models/car.fixturewireframe";
	entries[0].data = key;
	entries[0].size = sizeof(key);
	entries[1].path = "memory/Models/bad.fixturewireframe";
	entries[1].data = bad_key;
	entries[1].size = sizeof(bad_key);
	mh_test_wireframe_source_init(&fixture, &source);
	CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_wireframe_load_from(&source, resources, "memory",
		"MODELS\\CAR.LOB", &output, NULL) == MH_ERROR_NONE);
	CHECK(output.points != fixture.meshes[0].points &&
		output.edges != fixture.meshes[0].edges);
	CHECK(output.point_count == 2 && output.edge_count == 1);
	CHECK(memcmp(output.points, fixture.meshes[0].points,
		2 * sizeof(*output.points)) == 0);
	CHECK(memcmp(output.edges, fixture.meshes[0].edges,
		sizeof(*output.edges)) == 0);
	saved = output;
	CHECK(mh_wireframe_load_from(&source, resources, "memory",
		"Models/bad.lob", &output, NULL) == MH_ERROR_INVALID);
	CHECK(mh_wireframe_load_from(NULL, resources, "memory",
		"Models/car.lob", &output, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(memcmp(&saved, &output, sizeof(output)) == 0);
	mh_memory_resources_close(resources);
	mh_test_wireframe_source_free(&fixture);
	CHECK(mh_wireframe_fit(&output, 160, &scale, NULL) == MH_ERROR_NONE);
	CHECK(scale == 40 && output.points[0].xyz[0] == -160 &&
		output.points[1].xyz[0] == 160);
	CHECK(output.edges[0].first == 0 && output.edges[0].second == 1);
	mh_wireframe_free(&output);
	puts("Wireframe source: authored points/edges, renamed resources and independent ownership passed");

	return 0;
}
