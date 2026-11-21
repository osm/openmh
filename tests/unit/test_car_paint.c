#include <stdio.h>
#include <string.h>

#include "engine/assets/car_paint.h"
#include "engine/core/error.h"

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
prepare_mesh(struct mh_mesh *mesh, struct mh_mesh_face faces[2],
	struct mh_vertex vertices[9], unsigned int type, unsigned int flags,
	unsigned long color)
{
	unsigned int face, channel, vertex;

	memset(mesh, 0, sizeof(*mesh));
	memset(faces, 0, 2 * sizeof(*faces));
	memset(vertices, 0, 9 * sizeof(*vertices));
	mesh->faces = faces;
	mesh->face_count = 2;
	mesh->vertices = vertices;
	mesh->vertex_count = 9;

	for (face = 0; face < 2; face++) {
		faces[face].type = (unsigned char)type;
		faces[face].flags = (unsigned char)flags;
		faces[face].first = face == 0 ? 0 : 3;
		faces[face].count = face == 0 ? 3 : 6;

		for (channel = 0; channel < 3; channel++) {
			faces[face].color[channel] =
				(unsigned char)((color >> (16 - channel * 8)) &
					255UL);

			for (vertex = (unsigned int)faces[face].first;
				vertex < faces[face].first + faces[face].count;
				vertex++)
				vertices[vertex].color[channel] =
					faces[face].color[channel] / 255.0f;
		}
	}
}

int
main(void)
{
	struct mh_mesh mesh;
	struct mh_mesh_face faces[2], saved_faces[2];
	struct mh_vertex vertices[9], saved_vertices[9];
	unsigned long from[3] = {0x112233UL, 0x445566UL, 0x778899UL};
	unsigned long to[3] = {0x445566UL, 0x778899UL, 0xabcdefUL};
	unsigned int i;

	prepare_mesh(&mesh, faces, vertices, 8, 0x20, from[0]);
	CHECK(mh_car_paint_apply(&mesh, from, to, NULL) == MH_ERROR_NONE);
	CHECK(faces[0].color[0] == 0xab && faces[1].color[1] == 0xcd &&
		faces[0].color[2] == 0xef);

	for (i = 0; i < 9; i++)
		CHECK(vertices[i].color[0] == 171 / 255.0f &&
			vertices[i].position[0] == 0);

	prepare_mesh(&mesh, faces, vertices, 9, 0x40, from[1]);
	faces[1].first = 100;
	memcpy(saved_faces, faces, sizeof(faces));
	memcpy(saved_vertices, vertices, sizeof(vertices));
	CHECK(mh_car_paint_apply(&mesh, from, to, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved_faces, faces, sizeof(faces)) == 0 &&
		memcmp(saved_vertices, vertices, sizeof(vertices)) == 0);
	prepare_mesh(&mesh, faces, vertices, 2, 0x60, from[0]);
	CHECK(mh_car_paint_apply(&mesh, from, to, NULL) == MH_ERROR_NONE &&
		faces[0].color[0] == 0x11);
	prepare_mesh(&mesh, faces, vertices, 8, 0, from[0]);
	CHECK(mh_car_paint_apply(&mesh, from, to, NULL) == MH_ERROR_NONE &&
		faces[0].color[0] == 0x11);
	to[0] = 0x1000000UL;
	CHECK(mh_car_paint_apply(&mesh, from, to, NULL) != MH_ERROR_NONE &&
		faces[0].color[0] == 0x11);
	CHECK(mh_car_paint_apply(NULL, from, to, NULL) != MH_ERROR_NONE);

	return failures != 0;
}
