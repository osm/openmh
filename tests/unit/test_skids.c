#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/render/effect_skids.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

int
main(void)
{
	static const float identity[16] = {
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	struct mh_skids state, saved;
	struct mh_skid_input input;
	struct mh_mesh mesh;
	double eye[3] = {0, 0, 0};
	unsigned int i, j;

	memset(&input, 0, sizeof(input));
	memset(&mesh, 0, sizeof(mesh));
	input.rotation[0] = input.rotation[4] = input.rotation[8] = 1;
	input.viewed = 1;
	input.wheelspin = 1;

	for (i = 0; i < 4; i++) {
		input.wheels[i].supported = 1;
		input.wheels[i].mark = i + 1;
		input.wheels[i].normal[1] = 1;
	}

	mh_skids_reset(&state);
	CHECK(mh_skids_update(&input, &state, &state, NULL) == MH_ERROR_NONE);
	CHECK(!state.wheels[0].active && !state.wheels[1].active &&
		state.wheels[2].active && state.wheels[3].active);
	input.position[2] = .5f;
	CHECK(mh_skids_update(&input, &state, &state, NULL) == MH_ERROR_NONE);
	CHECK(state.wheels[2].partial && state.wheels[2].count == 0);
	CHECK(state.wheels[2].segments[0].color[0] == 80 &&
		state.wheels[3].segments[0].color[1] == 35);
	input.position[2] = 1;
	CHECK(mh_skids_update(&input, &state, &state, NULL) == MH_ERROR_NONE);
	CHECK(state.wheels[2].committed && state.wheels[2].count == 1 &&
		state.wheels[2].next == 1);
	input.heavy_brake = 1;

	for (i = 2; i < 130; i++) {
		input.position[2] = (float)i;
		CHECK(mh_skids_update(&input, &state, &state, NULL) ==
			MH_ERROR_NONE);
	}

	CHECK(state.wheels[2].count == 50 && state.wheels[2].first < 50 &&
		state.wheels[2].next < 50);
	input.viewed = 0;
	state.wheels[2].oldest_visible = 1;
	saved = state;
	CHECK(mh_skids_update(&input, &state, &state, NULL) == MH_ERROR_NONE &&
		!state.wheels[2].active);
	CHECK(memcmp(state.wheels[2].segments, saved.wheels[2].segments,
		sizeof(state.wheels[2].segments)) == 0);
	saved = state;
	input.wheels[3].normal[0] = 3;
	CHECK(mh_skids_update(&input, &state, &state, NULL) != MH_ERROR_NONE &&
		memcmp(&saved, &state, sizeof(state)) == 0);
	input.wheels[3].normal[0] = 0;
	state.wheels[3].next = 50;
	saved = state;
	CHECK(mh_skids_update(&input, &state, &state, NULL) != MH_ERROR_NONE &&
		memcmp(&saved, &state, sizeof(state)) == 0);
	CHECK(mh_skids_update(NULL, &state, &state, NULL) != MH_ERROR_NONE);
	CHECK(mh_skids_update(&input, NULL, &state, NULL) != MH_ERROR_NONE);
	CHECK(mh_skids_update(&input, &state, NULL, NULL) != MH_ERROR_NONE);
	mh_skids_reset(&state);
	CHECK(mh_skid_mesh_create(1, &mesh, NULL) == MH_ERROR_NONE);
	state.wheels[0].count = 1;
	state.wheels[0].segments[0].end[2] = 1;
	state.wheels[0].segments[0].normal[1] = 1;
	CHECK(mh_skid_mesh_update(&mesh, &state, 1, identity, eye, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.wheels[0].oldest_visible);
	CHECK(fabs(mesh.vertices[0].position[0] + .1) < 1e-7 &&
		fabs(mesh.vertices[0].position[1] - .01) < 1e-7);
	CHECK(fabs(mesh.vertices[0].position[2] + .01) < 1e-7 &&
		fabs(mesh.vertices[1].position[2] - 1.01) < 1e-7);

	for (i = 0; i < 6; i++)
		for (j = 0; j < 3; j++)
			CHECK(mesh.vertices[i].color[j] == 216.0f / 255.0f);

	CHECK(mesh.vertices[6].color[0] == 0);
	eye[2] = 200;
	CHECK(mh_skid_mesh_update(&mesh, &state, 1, identity, eye, NULL) ==
			MH_ERROR_NONE &&
		!state.wheels[0].oldest_visible);
	CHECK(mesh.vertices[0].color[0] == 0);
	eye[2] = 0;
	state.wheels[0].segments[0].normal[1] = 0;
	CHECK(mh_skid_mesh_update(&mesh, &state, 1, identity, eye, NULL) ==
			MH_ERROR_NONE &&
		!state.wheels[0].oldest_visible);

	/*
	 * count+partial can be 51; the original traverses the oldest slot
	 * twice.
	 */
	state.wheels[0].count = 50;
	state.wheels[0].partial = 1;

	for (i = 0; i < 50; i++) {
		state.wheels[0].segments[i].end[2] = 1;
		state.wheels[0].segments[i].normal[1] = 1;
	}

	CHECK(mh_skid_mesh_update(&mesh, &state, 1, identity, eye, NULL) ==
		MH_ERROR_NONE);
	CHECK(mesh.vertices[305].color[0] > 0 &&
		mesh.vertices[306].color[0] == 0);
	state.wheels[3].first = 50;
	saved = state;
	CHECK(mh_skid_mesh_update(&mesh, &state, 1, identity, eye, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&saved, &state, sizeof(state)) == 0);
	mh_mesh_free(&mesh);
	CHECK(mh_skid_mesh_create(0, &mesh, NULL) != MH_ERROR_NONE &&
		mh_skid_mesh_create(MH_RACE_MAX_CARS + 1, &mesh, NULL) !=
			MH_ERROR_NONE);

	return failures != 0;
}
