#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/render/scene.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static struct mh_race_scene scene;
static struct mh_field field;
static float models[MH_FIELD_MESHES * 16];

static void
check_history(void)
{
	struct mh_scene_glenz *glenz;
	float *model;
	unsigned int i;

	glenz = &scene.visuals.glenz;
	field.count = 2;
	field.active_mask = 3;

	for (i = 0; i < MH_FIELD_MESHES; i++) {
		model = models + i * 16;
		model[0] = model[5] = model[10] = model[15] = 1;
	}

	/*
	 * A nonzero first pose is measured from constructor zero, even if
	 * shadows were initially disabled or the car was unavailable.
	 */
	models[16 + 14] = 4;
	field.unavailable_mask = 1;
	CHECK(mh_race_scene_glenz_scroll(&scene, &field, models, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(glenz->scroll[0].previous[2] == 0 && glenz->scroll[0].phase == 0);
	field.unavailable_mask = 0;
	CHECK(mh_race_scene_glenz_scroll(&scene, &field, models, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(glenz->scroll[0].previous[2] == 0 && glenz->scroll[0].phase == 0);
	models[6 * 16 + 14] = -9;
	CHECK(mh_race_scene_glenz_scroll(&scene, &field, models, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(glenz->scroll[0].phase == 2 && glenz->scroll[1].phase == -3);

	models[16 + 14] = 8;
	models[6 * 16 + 14] = -13;
	field.unavailable_mask = 1;
	CHECK(mh_race_scene_glenz_scroll(&scene, &field, models, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(glenz->scroll[0].phase == 2 && glenz->scroll[0].previous[2] == 4);
	CHECK(glenz->scroll[1].phase == -5);
	CHECK(mh_race_scene_glenz_scroll(&scene, &field, models, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(glenz->scroll[0].phase == 2 && glenz->scroll[1].phase == -5);
	field.unavailable_mask = 0;
	CHECK(mh_race_scene_glenz_scroll(&scene, &field, models, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(glenz->scroll[0].phase == 4);
}

static void
check_coordinates(void)
{
	static const float identity[16] = {
		1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

	struct mh_vertex vertices[3];
	struct mh_draw draw;
	struct mh_mesh *mesh;
	struct mh_renderer_glenz material;
	float *model, local_eye[3], expected[2];
	double eye[3];
	size_t i;

	memset(vertices, 0, sizeof(vertices));
	memset(&draw, 0, sizeof(draw));
	draw.count = 3;
	draw.environment = 1;
	mesh = &scene.assets.meshes[1];
	mesh->vertices = vertices;
	mesh->vertex_count = 3;
	mesh->draws = &draw;
	mesh->draw_count = 1;
	scene.visuals.glenz.textures[1][0] = 2;
	scene.visuals.glenz.textures[1][1] = 3;
	model = models + 16;
	memcpy(model, identity, sizeof(identity));

	/* Rotate the object by 90 degrees around Y and translate. */
	model[0] = model[10] = 0;
	model[2] = -1;
	model[8] = 1;
	model[12] = 2;
	model[13] = 3;
	model[14] = 4;
	local_eye[0] = 0;
	local_eye[1] = 2;
	local_eye[2] = -10;
	eye[0] = -8;
	eye[1] = 5;
	eye[2] = 4;

	for (i = 0; i < 3; i++)
		vertices[i].normal[0] = 1;

	CHECK(mh_race_scene_glenz_material(&scene, 1, models, identity,
		identity, eye, 960, 640, &material,
		NULL) == MH_ERROR_NONE);
	CHECK(material.count == 3 && material.reflection_texture == 2 &&
		material.phong_texture == 3);
	CHECK(mh_glenz_reflection_uv(vertices[0].position, vertices[0].normal,
		local_eye, 4, expected, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(material.coordinates[0].reflection, expected,
		sizeof(expected)) == 0);

	/* Projected center is (1440,-640), transformed normal XY is zero. */
	CHECK(fabs(material.coordinates[0].phong[0] - .999f) < .000001);
	CHECK(fabs(material.coordinates[0].phong[1] - .999f) < .000001);

	/* Wheel helpers prepare UVs without advancing the root's history. */
	scene.assets.meshes[2] = *mesh;
	scene.visuals.glenz.textures[2][0] = 2;
	scene.visuals.glenz.textures[2][1] = 3;
	memcpy(models + 32, model, 16 * sizeof(float));
	CHECK(mh_race_scene_glenz_material(&scene, 2, models, identity,
		identity, eye, 960, 640, &material,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_glenz_reflection_uv(vertices[0].position, vertices[0].normal,
		local_eye, 0, expected, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(material.coordinates[0].reflection, expected,
		sizeof(expected)) == 0);
	CHECK(scene.visuals.glenz.scroll[0].phase == 4);

	/* Stack-owned fixture geometry must not be freed by scene cleanup. */
	memset(&scene.assets.meshes[1], 0, sizeof(*mesh));
	memset(&scene.assets.meshes[2], 0, sizeof(*mesh));
}

int
main(void)
{
	check_history();
	check_coordinates();
	mh_race_scene_free(&scene);

	return failures != 0;
}
