#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_provider.h"
#include "engine/assets/col.h"
#include "engine/collision/data.h"
#include "engine/core/binary.h"
#include "engine/render/scene_matrices.h"
#include "engine/render/shadow.h"
#include "engine/render/software_shadow_mesh.h"
#include "interfaces/render/backend.h"

/* Synthetic COL size and captured TGA pixel offset. */
#define ROAD_BYTES 158
#define PIXEL_OFFSET 18

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_field field;
static struct mh_car car;

static void
write_u16le(unsigned char *bytes, unsigned int value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
}

/* One broad floor in a single collision-grid cell, no proprietary data. */
static void
build_road(unsigned char data[ROAD_BYTES])
{
	unsigned int i;
	unsigned char *plane;

	memset(data, 0, ROAD_BYTES);
	write_u16le(data, 42);

	for (i = 0; i < 3; i++) {
		write_u16le(data + 4 + i * 2, 1);
		mh_write_u32le(data + 20 + i * 4, 0x43000000UL);
	}

	write_u16le(data + 10, 1);
	write_u16le(data + 12, 1);
	write_u16le(data + 14, 2);
	write_u16le(data + 18, 4);
	mh_write_u32le(data + 32, 0x3b800000UL);
	mh_write_u32le(data + 36, 0x43800000UL);
	write_u16le(data + 40, 0x8001);
	memset(data + 42, 255, 20);
	write_u16le(data + 62, 1);
	write_u16le(data + 68, 1);
	write_u16le(data + 70, 0x8002);
	write_u16le(data + 72, 3);
	write_u16le(data + 74, 0x8004);
	mh_write_u32le(data + 82, 0x3f800000UL);

	for (i = 0; i < 4; i++) {
		plane = data + 94 + i * 16;
		mh_write_u32le(plane + (i < 2 ? 0 : 8), 0x3f800000UL);
		mh_write_u32le(plane + 12, i & 1 ? 0xc2c80000UL : 0x42c80000UL);
	}
}

static unsigned int
visible_shadows(const struct mh_mesh *mesh)
{
	unsigned int i, count;

	count = 0;

	for (i = 0; i < MH_RACE_MAX_CARS; i++)
		count += !mesh->draw_hidden[i];

	return count;
}

int
main(void)
{
	struct mh_mesh mesh;
	const struct mh_mesh *meshes[1];
	struct mh_renderer *renderer;
	struct mh_collision road;
	struct mh_frustum frustum;
	struct mh_projection projection;
	struct mh_file picture, saved_picture;
	struct mh_renderer_scene_part parts[MH_RACE_MAX_CARS];
	struct mh_vertex saved_vertices[MH_RACE_MAX_CARS * 54];
	unsigned char data[ROAD_BYTES], hidden[MH_RACE_MAX_CARS];
	float models[MH_FIELD_MESHES * 16], view[16], identity[16];
	unsigned int i, j, colored_bytes;

	build_road(data);
	CHECK(mh_collision_col_decode(data, sizeof(data), &road, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_software_shadow_mesh_create(&mesh, NULL) == MH_ERROR_NONE);
	CHECK(visible_shadows(&mesh) == 0);
	memset(&frustum, 0, sizeof(frustum));
	memset(models, 0, sizeof(models));
	memset(view, 0, sizeof(view));
	memset(identity, 0, sizeof(identity));
	memset(parts, 0, sizeof(parts));

	for (i = 0; i < 16; i += 5)
		view[i] = identity[i] = 1;

	view[10] = -1;
	view[13] = -5;
	field.count = MH_RACE_MAX_CARS;
	field.active_mask = (1U << field.count) - 1;

	for (i = 0; i < field.count; i++) {
		field.cars[i].car = &car;
		field.cars[i].col = &road;
		field.cars[i].camera_mode = MH_CAMERA_CHASE;

		for (j = 0; j < 16; j += 5)
			models[(1 + 5 * i) * 16 + j] = 1;

		models[(1 + 5 * i) * 16 + 12] = (float)i * 3 - 10;
		models[(1 + 5 * i) * 16 + 13] = 1;
		models[(1 + 5 * i) * 16 + 14] = 20;
		parts[i].span.draw_first = i;
		parts[i].span.draw_count = 1;
		parts[i].span.primitive_first = i * MH_SHADOW_QUADS;
		parts[i].span.primitive_count = MH_SHADOW_QUADS;
	}

	CHECK(mh_software_shadow_mesh_update(
		&mesh, &field, models, &frustum, NULL) == MH_ERROR_NONE);
	CHECK(visible_shadows(&mesh) == field.count);

	for (i = 0; i < mesh.vertex_count; i++)
		CHECK(mesh.vertices[i].position[1] == 0);

	meshes[0] = &mesh;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_mesh_shadows(renderer, 0));
	CHECK(!mh_renderer_mesh_shadows(renderer, 1));
	CHECK(mh_projection_setup(320, 200, 1.3333333333333335f, 1, 1, 1000,
		&projection, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(renderer, &saved_picture));
	colored_bytes = 0;

	for (i = PIXEL_OFFSET; i < saved_picture.size; i++)
		colored_bytes += saved_picture.data[i] != 0;

	CHECK(colored_bytes > 100);
	CHECK(mh_renderer_scene_order(renderer, parts, MH_RACE_MAX_CARS));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(renderer, &picture));
	CHECK(picture.size == saved_picture.size &&
		memcmp(picture.data, saved_picture.data, picture.size) == 0);
	free(picture.data);

	/* Invalid live material metadata must not publish a partial frame. */
	mesh.primitives[0].flags = 4;
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(renderer, &picture));
	CHECK(picture.size == saved_picture.size &&
		memcmp(picture.data, saved_picture.data, picture.size) == 0);
	free(picture.data);
	free(saved_picture.data);
	mesh.primitives[0].flags = 0;
	CHECK(!mh_renderer_scene_frame(renderer, identity, view, NULL, 0));

	for (i = 0; i < field.count; i++) {
		field.active_mask = ((1U << field.count) - 1) ^ (1U << i);
		CHECK(mh_software_shadow_mesh_update(&mesh, &field, models,
			&frustum, NULL) == MH_ERROR_NONE);
		CHECK(visible_shadows(&mesh) == field.count - 1 &&
			mesh.draw_hidden[i]);
		field.active_mask = (1U << field.count) - 1;
		field.unavailable_mask = 1U << i;
		CHECK(mh_software_shadow_mesh_update(&mesh, &field, models,
			&frustum, NULL) == MH_ERROR_NONE);
		CHECK(visible_shadows(&mesh) == field.count - 1 &&
			mesh.draw_hidden[i]);
		field.unavailable_mask = 0;
		field.viewed_entry = i;
		field.cars[i].camera_mode = MH_CAMERA_IN_CAR;
		CHECK(mh_software_shadow_mesh_update(&mesh, &field, models,
			&frustum, NULL) == MH_ERROR_NONE);
		CHECK(visible_shadows(&mesh) == field.count - 1 &&
			mesh.draw_hidden[i]);
		field.cars[i].camera_mode = MH_CAMERA_CHASE;
	}

	models[(1 + 5 * 7) * 16 + 13] += 20;
	CHECK(mh_software_shadow_mesh_update(
		&mesh, &field, models, &frustum, NULL) == MH_ERROR_NONE);
	CHECK(visible_shadows(&mesh) == field.count - 1 && mesh.draw_hidden[7]);
	memcpy(saved_vertices, mesh.vertices, sizeof(saved_vertices));
	memcpy(hidden, mesh.draw_hidden, sizeof(hidden));
	models[(1 + 5 * 7) * 16] = FLT_MAX;
	CHECK(mh_software_shadow_mesh_update(&mesh, &field, models, &frustum,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(saved_vertices, mesh.vertices, sizeof(saved_vertices)) ==
		0);
	CHECK(memcmp(hidden, mesh.draw_hidden, sizeof(hidden)) == 0);
	models[(1 + 5 * 7) * 16] = 1;
	frustum.planes[0][3] = 100;
	CHECK(mh_software_shadow_mesh_update(
		&mesh, &field, models, &frustum, NULL) == MH_ERROR_NONE);
	CHECK(visible_shadows(&mesh) == 0);
	CHECK(mh_renderer_update_mesh(renderer, 0));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	mh_renderer_destroy(renderer);
	mh_mesh_free(&mesh);

	mh_collision_free(&road);

	return 0;
}
