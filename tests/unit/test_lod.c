#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/lod.h"
#include "engine/render/mesh.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_lod_input input;
	struct mh_lod_selection output, saved;
	struct mh_mesh mesh;
	struct mh_mesh_lod range, prior;

	memset(&input, 0, sizeof(input));
	input.levels = 4;
	input.renderer = MH_LOD_HARDWARE;
	input.width = 960;
	input.focal = 1;
	input.eye[2] = 37.5f;
	CHECK(mh_lod_select(&input, &output, NULL) == MH_ERROR_NONE &&
		output.level == 0 && !output.coarse);
	input.eye[2] = 37.500004f;
	CHECK(mh_lod_select(&input, &output, NULL) == MH_ERROR_NONE &&
		output.level == 1 && !output.coarse);

	/* The software comparison rounds the threshold before comparing it. */
	input.focal = 1.2345670461654663f;
	input.width = 320;
	input.eye[2] = 15.432088851928711f;
	input.renderer = MH_LOD_SOFTWARE;
	CHECK(mh_lod_select(&input, &output, NULL) == MH_ERROR_NONE &&
		output.level == 0 && !output.coarse);
	input.renderer = MH_LOD_HARDWARE;
	CHECK(mh_lod_select(&input, &output, NULL) == MH_ERROR_NONE &&
		output.level == 1 && !output.coarse);
	input.focal = 1;
	input.width = 960;
	input.eye[2] = 200;
	input.renderer = MH_LOD_SOFTWARE;
	input.levels = 3;
	CHECK(mh_lod_select(&input, &output, NULL) == MH_ERROR_NONE &&
		output.level == 2 && output.coarse);
	input.unavailable = input.ignition = input.supercars = 1;
	CHECK(mh_lod_select(&input, &output, NULL) == MH_ERROR_NONE &&
		output.level == 0 && !output.coarse);
	saved = output;
	input.levels = 5;
	CHECK(mh_lod_select(&input, &output, NULL) != MH_ERROR_NONE &&
		memcmp(&output, &saved, sizeof(output)) == 0);
	input.levels = 4;
	input.width = 0;
	CHECK(mh_lod_select(&input, &output, NULL) != MH_ERROR_NONE &&
		memcmp(&output, &saved, sizeof(output)) == 0);
	input.width = 960;
	input.eye[0] = 1e9f;
	CHECK(mh_lod_select(&input, &output, NULL) != MH_ERROR_NONE &&
		memcmp(&output, &saved, sizeof(output)) == 0);
	memset(&mesh, 0, sizeof(mesh));
	mesh.vertex_count = 12;
	mesh.draw_count = 2;
	CHECK(mh_mesh_active_lod(&mesh, &range, NULL) == MH_ERROR_NONE &&
		range.vertex_count == 12 && range.draw_count == 2);
	mesh.lod_count = 2;
	mesh.lod = 1;
	mesh.lods[1].vertex_first = 6;
	mesh.lods[1].vertex_count = 6;
	mesh.lods[1].draw_first = 1;
	mesh.lods[1].draw_count = 1;
	CHECK(mh_mesh_active_lod(&mesh, &range, NULL) == MH_ERROR_NONE &&
		range.vertex_first == 6 && range.draw_first == 1);
	prior = range;
	mesh.lods[1].vertex_count = (size_t)-1;
	CHECK(mh_mesh_active_lod(&mesh, &range, NULL) != MH_ERROR_NONE &&
		memcmp(&range, &prior, sizeof(range)) == 0);
	mesh.lod = 2;
	CHECK(mh_mesh_active_lod(&mesh, &range, NULL) != MH_ERROR_NONE &&
		memcmp(&range, &prior, sizeof(range)) == 0);
	puts("Car detail boundaries, unavailable priority and invalid-input atomicity checked");

	return failures != 0;
}
