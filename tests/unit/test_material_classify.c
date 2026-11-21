#include <stdio.h>
#include <string.h>

#include "engine/render/material_classify.h"
#include "engine/render/mesh.h"
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "material classification:%d: %s\n",    \
				__LINE__, #x);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_material_classification state;
	struct mh_mesh mesh;
	struct mh_draw draw;
	float positions[18], view[16], clip[16];
	unsigned char group;
	size_t list;
	unsigned int i;
	int near_clipped;

	memset(&state, 0, sizeof(state));
	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(positions, 0, sizeof(positions));
	memset(view, 0, sizeof(view));
	memset(clip, 0, sizeof(clip));
	view[10] = -1;
	clip[2] = 1;

	for (i = 0; i < 6; i++) {
		positions[i * 3] = 1;
		positions[i * 3 + 2] = .5f;
	}

	/* Duplicated quad vertices must not bias corner depth. */
	positions[11] = positions[14] = 100;
	positions[17] = 1;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.vertex_count = 6;
	mesh.draw_groups = &group;
	group = 0;
	state.mesh = &mesh;
	state.positions = positions;
	state.inverse_far = 1;
	draw.alpha_test = 1;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_ALPHA + 99 && !near_clipped);
	CHECK(mh_material_classify(&state, 0, 0, 6, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_ALPHA + 74 && !near_clipped);
	positions[15] = -1;
	CHECK(mh_material_classify(&state, 0, 0, 6, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_ALPHA + 124 && near_clipped);
	positions[15] = 0;
	CHECK(mh_material_classify(&state, 0, 0, 6, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_ALPHA + 74 && near_clipped);

	for (i = 0; i < 6; i++)
		positions[i * 3 + 2] = .8f;

	state.fog_enabled = 1;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_DISTANT_ALPHA);
	draw.alpha_test = 0;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_DISTANT_SOLID);
	state.fog_enabled = 0;
	draw.alpha_test = 1;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_ALPHA + 39);
	draw.alpha_test = 0;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_SOLID);
	group = 2;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_TEXTURE + 1);
	group = MH_TEXTURE_GROUPS + 1;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_INVALID);
	draw.environment = state.reflections = state.glenz = 1;
	mesh.environment_texture = 1;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_ENVIRONMENT);
	state.glenz = 0;
	mesh.draw_groups = NULL;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_INSPECTION);
	state.unavailable = 1;
	state.positions = NULL;
	mesh.draw_groups = &group;
	near_clipped = 1;
	CHECK(mh_material_classify(&state, 0, 0, 3, view, clip, &list,
		&near_clipped, NULL) == MH_ERROR_NONE);
	CHECK(list == MH_LIST_UNAVAILABLE && !near_clipped);

	return 0;
}
