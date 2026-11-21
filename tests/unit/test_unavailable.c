#include <float.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/unavailable_mesh.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__,     \
				#x);                                           \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_materials(void)
{
	unsigned char rgb[3] = {0, 128, 255};
	float light[3] = {-1, 1, 100}, color[3], saved[3];

	CHECK(mh_light_unavailable(rgb, 0, light, color, NULL) ==
		MH_ERROR_NONE);
	CHECK(color[0] == 5 && fabs(color[1] - 51.2) < 1e-5 && color[2] == 70);
	CHECK(mh_light_unavailable(rgb, 1, light, color, NULL) ==
		MH_ERROR_NONE);
	CHECK(color[0] == 5 && color[1] == 5 && color[2] == 5);
	rgb[0] = 255;
	light[0] = -2;
	CHECK(mh_light_unavailable(rgb, 2, light, color, NULL) ==
			MH_ERROR_NONE &&
		color[0] == 70);
	memcpy(saved, color, sizeof(saved));
	CHECK(mh_light_unavailable(NULL, 0, light, color, NULL) !=
		MH_ERROR_NONE);
	light[0] = FLT_MAX;
	CHECK(mh_light_unavailable(rgb, -FLT_MAX, light, color, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(saved, color, sizeof(saved)) == 0);
}

static void
check_meshes(void)
{
	struct mh_mesh mesh;
	struct mh_mesh_face face;
	struct mh_vertex source[6], saved[6], output[6];
	struct mh_prepared_light point;
	double eye[3] = {0, 0, 20}, ambient[3] = {0.5, 0.5, 0.5};
	float model[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
	float view[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, -20, 1};
	float light[3] = {0.7f, 0.7f, 0.7f}, expected[3], raw[18];
	unsigned int i, component;

	memset(&mesh, 0, sizeof(mesh));
	memset(&face, 0, sizeof(face));
	memset(source, 0, sizeof(source));
	mesh.vertices = source;
	mesh.vertex_count = 6;
	mesh.faces = &face;
	mesh.face_count = 1;
	face.count = 6;
	face.color[0] = 100;
	face.color[1] = 150;
	face.color[2] = 200;
	face.plane[2] = 1;
	face.plane[3] = -10;

	for (i = 0; i < 6; i++) {
		source[i].position[0] = (float)i;
		source[i].position[2] = 10 - (float)i;
		source[i].normal[2] = 1;

		/* Textured inspection tint. */
		for (component = 0; component < 3; component++)
			source[i].color[component] = 1;
	}

	memcpy(saved, source, sizeof(saved));
	CHECK(mh_light_unavailable(face.color, 0.1f, light, expected, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_unavailable_mesh_update(&mesh, NULL, 0, ambient, eye, model,
		view, .5, 100, output, 6, raw, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 6; i++)
		for (component = 0; component < 3; component++) {
			CHECK(fabs(output[i].color[component] * 255.0 -
				expected[component]) < 1e-5);
			CHECK(output[i].position[component] ==
				source[i].position[component]);
			CHECK(raw[i * 3 + component] == expected[component]);
		}

	/* A near-clipped first corner supplies camera Z, not Z/far. */
	source[0].position[2] = 19.75f;
	CHECK(mh_unavailable_mesh_update(&mesh, NULL, 0, ambient, eye, model,
		view, .5, 100, output, 6, raw, NULL) == MH_ERROR_NONE);
	CHECK(raw[0] == 21 && raw[1] == 31.5f && raw[2] == 42);
	source[0] = saved[0];

	/*
	 * Whole quad shares first corner's fade/color; reverse facing collapses
	 * all six output vertices without changing original data or draw spans.
	 */
	eye[2] = 0;
	CHECK(mh_unavailable_mesh_update(&mesh, NULL, 0, ambient, eye, model,
		view, .5, 100, output, 6, NULL, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 6; i++)
		CHECK(memcmp(output[i].position, source[0].position,
			sizeof(output[i].position)) == 0);

	CHECK(memcmp(saved, source, sizeof(saved)) == 0);

	/*
	 * Rotate the object and eye together: the original face remains front
	 * facing, and light directions must meet the transformed normals.
	 */
	memset(&point, 0, sizeof(point));
	point.direction[2] = point.view[2] = -1;
	point.color[0] = point.color[1] = point.color[2] = 1;
	point.specular = 1;
	model[0] = model[10] = -1;
	eye[2] = -20;
	view[0] = view[10] = -1;
	ambient[0] = ambient[1] = ambient[2] = 0;
	CHECK(mh_unavailable_mesh_update(&mesh, &point, 1, ambient, eye, model,
		view, .5, 100, output, 6, NULL, NULL) == MH_ERROR_NONE);
	CHECK(output[0].color[0] == 70.0f / 255 &&
		output[0].color[1] == 70.0f / 255);
	CHECK(mh_unavailable_mesh_update(&mesh, NULL, 0, ambient, eye, model,
		view, .5, 100, output, 5, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_unavailable_mesh_update(&mesh, NULL, 0, ambient, eye, model,
		view, .5, 100, source, 6, NULL, NULL) != MH_ERROR_NONE);
	face.first = 1;
	CHECK(mh_unavailable_mesh_update(&mesh, NULL, 0, ambient, eye, model,
		view, .5, 100, output, 6, NULL, NULL) != MH_ERROR_NONE);
}

int
main(void)
{
	check_materials();
	check_meshes();

	if (failures)
		return 1;

	puts("Unavailable material bounds, primitive fade, raw RGB, face visibility and ownership passed");

	return 0;
}
