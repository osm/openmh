#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_provider.h"
#include "backends/render/software_world_light.h"
#include "engine/assets/myw.h"
#include "engine/core/binary.h"
#include "engine/geometry/world.h"
#include "engine/render/scene.h"
#include "interfaces/render/backend.h"

/* Synthetic MYW containing two grid cells, lights and links. */
#define WORLD_BYTES 696

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_scene scene;
static struct mh_field field;
static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

/* Two cells with red and green lights, on opposite sides of X = 2. */
static int
build_light_world(unsigned char data[WORLD_BYTES], struct mh_world *world)
{
	unsigned char *record;
	unsigned int i;

	memset(data, 0, WORLD_BYTES);
	memcpy(data, "!WYM", 4);
	mh_write_u32le(data + 4, WORLD_BYTES);
	mh_write_u32le(data + 8, 2);
	mh_write_u32le(data + 12, 1);
	mh_write_u32le(data + 0x14, 0x40000000UL); /* binary64 2 */
	mh_write_u32le(data + 0x1c, 0x40330000UL); /* binary64 19 */
	mh_write_u32le(data + 0x58, 648);
	data[0x44] = 255;
	mh_write_u32le(data + 0x48, 0x3f000000UL);
	mh_write_u32le(data + 0x50, 0xc0000000UL); /* Unnormalized -Y. */

	for (i = 0; i < MH_MYW_BLOCK_COUNT; i++)
		mh_write_u32le(data + 0x64 + i * 12, 240);

	mh_write_u32le(data + 0x5c + MH_MYW_LIGHTS * 12, 2);
	mh_write_u32le(data + 0x60 + MH_MYW_LIGHTS * 12, 392);
	mh_write_u32le(data + 0x5c + MH_MYW_LINKS * 12, 2);
	mh_write_u32le(data + 0x60 + MH_MYW_LINKS * 12, 16);
	mh_write_u32le(data + 0x64 + MH_MYW_LINKS * 12, 632);

	for (i = 0; i < 2; i++) {
		record = data + 240 + i * 196;
		record[0] = 1;
		mh_write_u32le(record + 4, 0x40000000UL);
		mh_write_u32le(record + 12, 0x41200000UL);
		mh_write_u32le(record + 0x1c + i * 4, 0x3f800000UL);
		mh_write_u32le(record + 0x28, 0x3f800000UL);
		mh_write_u32le(record + 0x2c, 0x42c80000UL);
		mh_write_u32le(data + 632 + i * 8, i * 196);
		mh_write_u32le(data + 636 + i * 8, MH_MYW_NO_LIGHT);
		mh_write_u32le(data + 648 + i * 24, MH_MYW_NO_MODEL);
		mh_write_u32le(data + 656 + i * 24, i * 8);
	}

	CHECK(mh_world_myw_decode(data, WORLD_BYTES, world, NULL) ==
		MH_ERROR_NONE);

	return 0;
}

static int
scene_settings(const struct mh_world *world)
{
	struct mh_model_lighting lighting, saved;
	struct mh_av_settings settings;
	float models[MH_FIELD_MESHES * 16];
	unsigned int i, mesh, shading, far, mode;

	memset(&scene, 0, sizeof(scene));
	memset(&field, 0, sizeof(field));
	memset(&settings, 0, sizeof(settings));
	memset(models, 0, sizeof(models));
	scene.assets.lighting_world = *world;
	scene.assets.track.light_intensity = 2;
	scene.assets.track.specular_factor = 6;
	field.count = 2;
	field.active_mask = 3;
	field.viewed_entry = 1;
	field.cars[1].eye[0] = 123;

	for (i = 0; i < 3; i++) {
		scene.assets.track.object_ambient[i] = .25;
		scene.assets.track.object_brightness[i] = 1;
		scene.visuals.brightness.object[i] = .5;
	}

	for (mesh = 1; mesh <= 10; mesh++)
		scene.assets.meshes[mesh].lod_count = 4;

	/*
	 * At width 512 and focal 1, strictly beyond 60 selects coarse lighting.
	 */
	for (far = 0; far < 2; far++) {
		models[30] = far ? 60.00001f : 60;
		CHECK(mh_race_scene_details(&scene, &field, models, 512,
			MH_RENDER_SOFTWARE, NULL) == MH_ERROR_NONE);
		CHECK(scene.assets.meshes[1].lod == (far ? 3 : 2));
		CHECK(scene.visuals.coarse[0] == (int)far);
		CHECK(scene.visuals.coarse[1] == 0);

		for (shading = 0; shading < 3; shading++) {
			settings.car_shading = shading;

			for (mesh = 0; mesh <= 10; mesh++) {
				CHECK(mh_race_scene_software_lighting(&scene,
					&field, mesh, &settings,
					&lighting,
					NULL) == MH_ERROR_NONE);
				CHECK(lighting.mode ==
					(mesh ? MH_LIGHT_CAR
					: MH_LIGHT_OBJECT));
				CHECK(lighting.eye[0] == 123 &&
					lighting.intensity == 2);
				CHECK(lighting.ambient[1] == .25f &&
					lighting.specular_scale == 6);
				CHECK(lighting.directional_color[0] == .5f &&
					lighting.direction[1] == -2);
				CHECK(lighting.brightness[2] == 1);
				CHECK(lighting.lights == NULL &&
					lighting.light_count == 0);
				CHECK(lighting.flat_car ==
					(shading == MH_AV_SHADING_REFLECTION &&
						!(far && mesh > 0 &&
							mesh <= 5)));
				CHECK(lighting.flat_detail ==
					(shading == MH_AV_SHADING_FLAT ||
						(far && mesh > 0 &&
							mesh <= 5)));
			}
		}
	}

	/*
	 * Invalid second entrant must preserve the first entrant's previous
	 * state.
	 */
	models[30] = 0;
	field.cars[1].wheels.drawn[1] = 4;
	CHECK(mh_race_scene_details(&scene, &field, models, 512,
		MH_RENDER_SOFTWARE, NULL) == MH_ERROR_INVALID);
	CHECK(scene.assets.meshes[1].lod == 3 && scene.visuals.coarse[0] == 1);
	field.cars[1].wheels.drawn[1] = 0;
	models[30] = 100;

	for (mode = 0; mode < 3; mode++) {
		field.cars[1].cheats =
			mode == 0 ? MH_CHEAT_IGNITION : MH_CHEAT_SUPERCARS;
		field.unavailable_mask = mode == 2 ? 1 : 0;
		CHECK(mh_race_scene_details(&scene, &field, models, 512,
			MH_RENDER_SOFTWARE, NULL) == MH_ERROR_NONE);
		CHECK(scene.visuals.coarse[0] == 0);
		CHECK(scene.assets.meshes[1].lod ==
			(mode == 0		    ? 2
					: mode == 1 ? 3
						: 0));
	}

	scene.visuals.brightness_ready = 1;
	CHECK(mh_race_scene_software_lighting(&scene, &field, 2, NULL,
		&lighting, NULL) == MH_ERROR_NONE);
	CHECK(lighting.brightness[2] == .5f && lighting.flat_car == 1);
	scene.assets.track.specular_factor = -1;
	CHECK(mh_race_scene_software_lighting(&scene, &field, 2, NULL,
		&lighting, NULL) == MH_ERROR_NONE &&
		lighting.specular_scale == 0);
	saved = lighting;
	field.cars[1].eye[0] = DBL_MAX;
	CHECK(mh_race_scene_software_lighting(&scene, &field, 2, NULL,
		&lighting, NULL) == MH_ERROR_RANGE);
	CHECK(memcmp(&lighting, &saved, sizeof(lighting)) == 0);
	field.cars[1].eye[0] = 0;
	settings.car_shading = 3;
	CHECK(mh_race_scene_software_lighting(&scene, &field, 2, &settings,
		&lighting, NULL) == MH_ERROR_INVALID);
	CHECK(mh_race_scene_software_lighting(&scene, &field, 11, NULL,
		&lighting, NULL) == MH_ERROR_INVALID);
	scene.assets.lighting_world.has_directional = 0;
	CHECK(mh_race_scene_software_lighting(&scene, &field, 2, NULL,
		&lighting, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&lighting, &saved, sizeof(lighting)) == 0);

	return 0;
}

static int
root_frames(const struct mh_world *world)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct mh_mesh mesh;
	struct mh_vertex vertices[6];
	struct mh_mesh_face face;
	struct mh_mesh_primitive primitive;
	struct mh_draw draw;
	const struct mh_mesh *meshes[2];
	struct mh_renderer *renderer;
	struct mh_model_lighting lighting;
	struct mh_software_world_light state;
	struct mh_point_light selected, *scratch;
	struct mh_prepared_light *prepared;
	struct mh_projection projection;
	struct mh_file image, saved;
	float models[32], view[16];
	unsigned long pixel;
	unsigned int i, cell;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&face, 0, sizeof(face));
	memset(&primitive, 0, sizeof(primitive));
	memset(&draw, 0, sizeof(draw));
	memset(&lighting, 0, sizeof(lighting));
	memset(&state, 0, sizeof(state));
	mesh.vertices = vertices;
	mesh.vertex_count = draw.count = face.count = primitive.count = 6;
	mesh.faces = &face;
	mesh.draws = &draw;
	mesh.primitives = &primitive;
	mesh.face_count = mesh.draw_count = mesh.primitive_count = 1;
	face.type = primitive.type = 1;
	face.plane[2] = -1;
	memset(face.color, 255, sizeof(face.color));
	memcpy(models, identity, sizeof(identity));
	memcpy(models + 16, identity, sizeof(identity));
	models[28] = 2.5f; /* Child in cell 1 regardless of its hidden root. */
	memcpy(view, identity, sizeof(identity));
	view[10] = -1;
	view[12] = -2.5f;
	lighting.mode = MH_LIGHT_OBJECT;
	lighting.intensity = 1;

	for (i = 0; i < 3; i++)
		lighting.brightness[i] = 1;

	for (i = 0; i < 6; i++) {
		vertices[i].position[0] = xy[i][0];
		vertices[i].position[1] = xy[i][1];
		vertices[i].position[2] = 4;
		vertices[i].normal[2] = 1;
	}

	meshes[0] = meshes[1] = &mesh;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 2, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_mesh_visible(renderer, 0, 0));
	CHECK(mh_projection_setup(320, 200, 1.6f, 1, 1, 100, &projection,
		NULL) == MH_ERROR_NONE);

	for (cell = 0; cell < 2; cell++) {
		models[12] = cell ? 2.5f : 1.5f;
		CHECK(mh_renderer_world_lighting(renderer, 1, 0, world,
			&lighting, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view,
			models, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(renderer, &saved));
		pixel = mh_read_u16le(saved.data + 18 + (100 * 320 + 160) * 2);
		CHECK(cell ? (pixel & 0x3e0) != 0 && (pixel & 0x7c00) == 0
			: (pixel & 0x7c00) != 0 && (pixel & 0x3e0) == 0);

		/*
		 * Direct selected-light submission must produce the same frame.
		 */
		CHECK(cell < world->light_count);
		selected = world->lights[cell];
		lighting.lights = &selected;
		lighting.light_count = 1;
		CHECK(mh_renderer_model_lighting(
			renderer, 1, &lighting, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view,
			models, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(renderer, &image));
		CHECK(image.size == saved.size &&
			memcmp(image.data, saved.data, image.size) == 0);
		free(image.data);
		lighting.lights = NULL;
		lighting.light_count = 0;
		CHECK(mh_renderer_world_lighting(renderer, 1, 0, world,
			&lighting, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_world_lighting(renderer, 1, 2, world,
			&lighting, NULL) == MH_ERROR_INVALID);
		lighting.mode = (enum mh_light_mode)0;
		CHECK(mh_renderer_world_lighting(renderer, 1, 0, world,
			&lighting, NULL) == MH_ERROR_INVALID);
		lighting.mode = MH_LIGHT_OBJECT;
		CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view,
			models, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(renderer, &image));
		CHECK(image.size == saved.size &&
			memcmp(image.data, saved.data, image.size) == 0);
		free(image.data);
		CHECK(mh_renderer_world_lighting(renderer, 1, 0, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view,
			models, 0, NULL, NULL, NULL,
			NULL) != MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(renderer, &image));
		CHECK(image.size == saved.size &&
			memcmp(image.data, saved.data, image.size) == 0);
		free(image.data);
		free(saved.data);
	}

	mh_renderer_destroy(renderer);

	/*
	 * Settings updates retain capacity and prepare each child's own pose.
	 */
	CHECK(mh_software_world_light_set(&state, world, &lighting, NULL) ==
		MH_ERROR_NONE);
	scratch = state.lighting.lights;
	prepared = state.lighting.prepared;
	CHECK(mh_software_world_light_prepare(&state, models + 16, models));
	CHECK(state.lighting.settings.light_count == 1);
	CHECK(state.lighting.prepared[0].direction[0] < 0);
	models[28] = 1;
	lighting.flat_detail = 1;
	CHECK(mh_software_world_light_set(&state, world, &lighting, NULL) ==
		MH_ERROR_NONE);
	CHECK(state.lighting.lights == scratch &&
		state.lighting.prepared == prepared);
	CHECK(mh_software_world_light_prepare(&state, models + 16, models));
	CHECK(state.lighting.prepared[0].direction[0] > 0);
	CHECK(state.lighting.lights[0].color[1] == 1);
	mh_software_world_light_free(&state);

	return 0;
}

int
main(void)
{
	unsigned char data[WORLD_BYTES];
	struct mh_world world;

	CHECK(build_light_world(data, &world) == 0);
	CHECK(scene_settings(&world) == 0);
	CHECK(root_frames(&world) == 0);
	mh_world_free(&world);
	puts("Software car lighting, detail policy and root-cell checks passed");

	return 0;
}
