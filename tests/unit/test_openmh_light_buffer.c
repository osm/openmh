#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/render/matrix.h"
#include "engine/render/openmh_light_buffer.h"

#define CHECK(value)                                                           \
	do {                                                                   \
		if (!(value)) {                                                \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#value);                                       \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_openmh_light_buffer	state;
	struct mh_model_lighting	settings;
	struct mh_mesh			mesh;
	struct mh_mesh_face		faces[3];
	struct mh_world			world;
	struct mh_world_cell		cells[2];
	struct mh_world_light_link	links[MH_OPENMH_LIGHTS + 1];
	struct mh_point_light		lights[MH_OPENMH_LIGHTS + 1];
};

static void
initialize(struct fixture *f)
{
	unsigned int i;

	memset(f, 0, sizeof(*f));
	f->mesh.faces = f->faces;
	f->mesh.face_count = 3;
	f->mesh.vertex_count = 9;

	for (i = 0; i < 3; i++) {
		f->faces[i].first = i * 3;
		f->faces[i].count = 3;
		f->faces[i].color[0] = 80;
		f->faces[i].color[1] = 100;
		f->faces[i].color[2] = 120;
		f->settings.ambient[i] = .1f;
		f->settings.brightness[i] = 1;
	}

	f->faces[0].type = 2;
	f->faces[1].flags = 0x10;
	f->faces[2].type = 4;
	f->settings.mode = MH_LIGHT_CAR;
	f->settings.intensity = 1;
	f->world.width = 2;
	f->world.height = 1;
	f->world.cell_count = 2;
	f->world.cell_size[0] = f->world.cell_size[1] = 10;
	f->world.cells = f->cells;
	f->world.links = f->links;
	f->world.lights = f->lights;
	f->cells[1].light_head = MH_WORLD_NO_LIGHT;
	f->world.light_count = f->world.link_count = 1;
	f->links[0].next = MH_WORLD_NO_LIGHT;
	f->lights[0].position[1] = 2;
	f->lights[0].color[0] = 1;
	f->lights[0].radius = 20;
	f->lights[0].intensity = 1;
}

static int
materials_and_selection(void)
{
	struct fixture f;
	const float *light;
	float surface[4], root[16];
	struct mh_openmh_object *allocation;

	initialize(&f);
	CHECK(mh_openmh_light_buffer_set(&f.state, &f.mesh, 0, &f.world,
		&f.settings, NULL) == MH_ERROR_NONE);
	allocation = f.state.objects;
	CHECK(mh_openmh_light_buffer_prepare(&f.state, &f.mesh,
		mh_render_identity, mh_render_identity,
		NULL) == MH_ERROR_NONE);
	light = mh_openmh_light_buffer_surface(&f.state, 0, surface);
	CHECK(light != NULL && light[3] == 1 && light[7] == 4);
	CHECK(fabs(light[0] - .14) < 1e-6 && light[13] == 1);
	CHECK(surface[3] == 1 && fabs(surface[0] - 200.0 / 255) < 1e-6);
	CHECK(mh_openmh_light_buffer_surface(&f.state, 3, surface) == light);
	CHECK(surface[3] == 2 && fabs(surface[0] - 80.0 / 255) < 1e-6);
	CHECK(mh_openmh_light_buffer_surface(&f.state, 6, surface) == NULL);
	CHECK(mh_openmh_light_buffer_surface(&f.state, 9, surface) == NULL);

	/* Wheel/body light selection uses root position, not child position. */
	memcpy(root, mh_render_identity, sizeof(root));
	root[12] = 15;
	CHECK(mh_openmh_light_buffer_prepare(&f.state, &f.mesh,
		mh_render_identity, root, NULL) == MH_ERROR_NONE);
	CHECK(f.state.objects[0].lighting[3] == 0);
	f.settings.brightness[0] = .5f;
	CHECK(mh_openmh_light_buffer_set(&f.state, &f.mesh, 0, &f.world,
		&f.settings, NULL) == MH_ERROR_NONE);
	CHECK(f.state.objects == allocation);
	f.settings.brightness[0] = -1;
	CHECK(mh_openmh_light_buffer_set(&f.state, &f.mesh, 0, &f.world,
		&f.settings, NULL) == MH_ERROR_INVALID);
	CHECK(f.state.settings.brightness[0] == .5f);
	CHECK(mh_openmh_light_buffer_set(
		&f.state, &f.mesh, 0, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(!f.state.enabled && f.state.objects == NULL);

	return 0;
}

static int
bounded_light_selection(void)
{
	struct fixture f;
	unsigned int i;
	float strongest;

	initialize(&f);
	f.world.light_count = f.world.link_count = MH_OPENMH_LIGHTS + 1;

	for (i = 0; i <= MH_OPENMH_LIGHTS; i++) {
		f.lights[i] = f.lights[0];
		f.lights[i].color[0] = i + 1;
		f.links[i].light = i;
		f.links[i].next =
			i == MH_OPENMH_LIGHTS ? MH_WORLD_NO_LIGHT : i + 1;
	}

	CHECK(mh_openmh_light_buffer_set(&f.state, &f.mesh, 0, &f.world,
		&f.settings, NULL) == MH_ERROR_NONE);
	CHECK(mh_openmh_light_buffer_prepare(&f.state, &f.mesh,
		mh_render_identity, mh_render_identity,
		NULL) == MH_ERROR_NONE);
	CHECK(f.state.objects[0].lighting[3] == MH_OPENMH_LIGHTS);
	strongest = f.state.objects[0].lighting[16];

	for (i = 1; i < MH_OPENMH_LIGHTS; i++)
		CHECK(strongest > f.state.objects[0].lighting[16 + i * 12]);

	mh_openmh_light_buffer_free(&f.state);

	return 0;
}

int
main(void)
{
	return materials_and_selection() || bounded_light_selection();
}
