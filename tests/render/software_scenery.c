#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/render/software_light_select.h"
#include "backends/render/software_provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/ilbm.h"
#include "engine/assets/myo_decode.h"
#include "engine/assets/myw.h"
#include "engine/assets/scene.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track.h"
#include "engine/core/limits.h"
#include "engine/geometry/model.h"
#include "engine/geometry/world.h"
#include "interfaces/render/backend.h"

/* Synthetic MYW byte size and captured TGA pixel start. */
#define WORLD_BYTES 1240
#define PIXEL_OFFSET 18

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
write_u32le(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

/*
 * Two geometry slices separated by an empty descriptor. Their global
 * positions are deliberately far apart; no descriptor translation applies.
 */
static void
build_world(unsigned char data[WORLD_BYTES])
{
	static const unsigned long counts[12] = {
		3, 7, 3, 2, 2, 2, 0, 0, 0, 0, 0, 0};

	static const unsigned long lengths[12] = {
		684, 84, 72, 56, 32, 24, 0, 0, 0, 0, 0, 0};

	static const unsigned long offsets[12] = {240, 924, 1008, 1080, 1136,
		1168, 1192, 1192, 1192, 1192, 1192, 1192};

	static const unsigned char colors[12] = {
		255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

	unsigned char *record;
	unsigned int i;
	unsigned int j;
	unsigned int corners;

	memset(data, 0, WORLD_BYTES);
	memcpy(data, "!WYM", 4);
	write_u32le(data + 4, WORLD_BYTES);
	write_u32le(data + 8, 2);
	write_u32le(data + 12, 1);

	/* Binary64 cell size 19, X and Z. */
	write_u32le(data + 0x14, 0x40330000UL);
	write_u32le(data + 0x1c, 0x40330000UL);
	write_u32le(data + 0x58, 1192);

	for (i = 0; i < 12; i++) {
		write_u32le(data + 0x5c + i * 12, counts[i]);
		write_u32le(data + 0x60 + i * 12, lengths[i]);
		write_u32le(data + 0x64 + i * 12, offsets[i]);
	}

	for (i = 0; i < 3; i++) {
		record = data + 240 + i * 228;
		memset(record + 4, 0xcd, 108); /* Untouched runtime scratch. */
		write_u32le(record + 0x70,
			i == 2 ? 0x41280000UL
			: 0x3f000000UL); /* x=10.5 or .5 */
		write_u32le(record + 0x74, 0x3f000000UL);
		write_u32le(record + 0x7c, 0x3f800000UL);
		record[0x80] = (unsigned char)(i == 0 ? 3 : i == 1 ? 0 : 4);
		record[0x82] = record[0x84] = record[0x86] =
			(unsigned char)(i != 1);
		write_u32le(record + 0x8c, i == 0 ? 0 : 36);
		write_u32le(record + 0x90, i == 0 ? 0 : 16);
		write_u32le(record + 0x94, i == 0 ? 0 : 12);
		write_u32le(record + 0x9c, i == 0 ? 0 : 24);
	}

	write_u32le(data + 936, 0x3f800000UL); /* x=1 */
	write_u32le(data + 952, 0x3f800000UL); /* y=1 */

	for (i = 0; i < 4; i++) {
		write_u32le(data + 960 + i * 12,
			(i == 1 || i == 2) ? 0x41300000UL : 0x41200000UL);

		if (i >= 2)
			write_u32le(data + 964 + i * 12, 0x3f800000UL);
	}

	write_u32le(data + 1176, 0x3f800000UL);
	write_u32le(data + 1188, 0x3f800000UL);

	for (i = 0; i < 2; i++) {
		record = data + 1080 + i * 24;
		record[0] = (unsigned char)(4 + i);
		corners = 3 + i;

		for (j = 0; j < corners; j++) {
			memcpy(record + 4 + j * 4, colors + j * 3, 3);
			record[7 + j * 4] = 0xcd; /* Not alpha. */
			record[4 + corners * 4 + j * 2] = (unsigned char)j;
		}
	}

	write_u32le(data + 1192, 0);
	write_u32le(data + 1200, MH_U32_MAX);
	write_u32le(data + 1216, 456);
	write_u32le(data + 1224, MH_U32_MAX);
}

static int
check_picture_pair(
	struct mh_renderer *a, struct mh_renderer *b, int require_visible)
{
	struct mh_file first, second;
	size_t i, colored_bytes;

	CHECK(mh_renderer_tga_capture(a, &first));
	CHECK(mh_renderer_tga_capture(b, &second));
	CHECK(first.size == second.size);
	CHECK(memcmp(first.data, second.data, first.size) == 0);
	colored_bytes = 0;

	for (i = PIXEL_OFFSET; i < first.size; i++)
		colored_bytes += first.data[i] != 0;

	CHECK(!require_visible || colored_bytes > 20);
	free(first.data);
	free(second.data);

	return 0;
}

static int
check_fixture_frames(void)
{
	const double brightness[3] = {1, 1, 1};
	unsigned char data[WORLD_BYTES], other[WORLD_BYTES];
	struct mh_world world, alternate;
	struct mh_model models[2];
	struct mh_scene_object poses[3];
	struct mh_scene_instance instances[3];
	struct mh_scene_transform moved[3];
	struct mh_mesh packed, separate[4];
	struct mh_mesh_object object, before;
	struct mh_mesh_span span;
	const struct mh_mesh *meshes[4];
	struct mh_renderer *together, *apart;
	struct mh_renderer_scene_part parts[4];
	struct mh_model_lighting lighting;
	struct mh_world_view world_view;
	struct mh_projection projection;
	struct mh_depth_cue cue;
	float view[16], transforms[64], owner[16];
	struct mh_file picture, retained;
	unsigned int i, k, frame;

	build_world(data);
	build_world(other);

	/* Flat software materials retain the first source color. */
	other[1084] = 0;
	other[1085] = 255;
	CHECK(mh_world_myw_decode(data, sizeof(data), &world, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_world_myw_decode(other, sizeof(other), &alternate, NULL) ==
		MH_ERROR_NONE);
	models[0] = world.models[0].geometry;
	models[1] = alternate.models[0].geometry;
	memset(poses, 0, sizeof(poses));
	memset(moved, 0, sizeof(moved));
	memset(transforms, 0, sizeof(transforms));
	memset(view, 0, sizeof(view));
	view[0] = view[5] = view[15] = 1;
	view[10] = -1;
	memset(owner, 0, sizeof(owner));
	owner[0] = owner[5] = owner[10] = owner[15] = 1;
	memcpy(transforms, owner, sizeof(owner));

	for (i = 0; i < 3; i++) {
		poses[i].rotation[0] = poses[i].rotation[8] = -1;
		poses[i].rotation[4] = 1;
		poses[i].position[2] = 3;

		/* All motion RGB is zero; this must not blacken geometry. */
		instances[i].pose = &poses[i];
		instances[i].model = &models[i == 1];
		moved[i].scene_index = i;
		memcpy(moved[i].rotation, poses[i].rotation,
			sizeof(moved[i].rotation));
	}

	CHECK(mh_mesh_build_world_native_profile(mh_image_fallback_decoder(),
		&world, NULL, brightness, instances, 3,
		MH_RENDER_SOFTWARE, &packed, NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_build_world_native_profile(mh_image_fallback_decoder(),
		&world, NULL, brightness, NULL, 0, MH_RENDER_SOFTWARE,
		&separate[0], NULL) == MH_ERROR_NONE);
	memset(&world_view, 0, sizeof(world_view));
	world_view.profile = MH_RENDER_SOFTWARE;
	world_view.end[0] = 2;
	world_view.end[1] = 1;
	CHECK(mh_mesh_world_cull(&packed, &world_view, NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_world_cull(&separate[0], &world_view, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_mesh_scene_object(&packed, 0, &before));
	CHECK(mh_mesh_scene_object(&packed, 2, &object));
	CHECK(object.span.primitive_first == before.span.primitive_first + 1);
	CHECK(mh_mesh_scene_object(&packed, 1, &object));
	CHECK(object.span.primitive_first == before.span.primitive_first + 2);
	before = object;
	CHECK(!mh_mesh_scene_object(&packed, 3, &object));
	CHECK(memcmp(&object, &before, sizeof(object)) == 0);
	meshes[0] = &packed;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &together, NULL) == MH_ERROR_NONE);
	meshes[0] = &separate[0];

	for (i = 0; i < 3; i++) {
		CHECK(mh_mesh_build_lods_profile(mh_image_fallback_decoder(),
			instances[i].model, 1, NULL, MH_RENDER_SOFTWARE,
			&separate[i + 1], NULL) == MH_ERROR_NONE);
		meshes[i + 1] = &separate[i + 1];
	}

	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 4, 320, 200,
		0, &apart, NULL) == MH_ERROR_NONE);
	memset(&lighting, 0, sizeof(lighting));
	lighting.mode = MH_LIGHT_OBJECT;
	lighting.intensity = lighting.specular_scale = 1;

	for (i = 0; i < 3; i++) {
		lighting.ambient[i] = .75f;
		lighting.brightness[i] = 1;
	}

	CHECK(mh_renderer_world_lighting(together, 0, 0, &world, &lighting,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_world_lighting(
		apart, 1, 4, &world, &lighting, NULL) != MH_ERROR_NONE);

	for (i = 1; i < 4; i++)
		CHECK(mh_renderer_model_lighting(apart, i, &lighting, NULL) ==
			MH_ERROR_NONE);

	CHECK(mh_projection_setup(320, 200, 1.6f, 1, 1, 100, &projection,
		NULL) == MH_ERROR_NONE);
	memset(&cue, 0, sizeof(cue));
	cue.color[0] = 70;
	cue.color[1] = 110;
	cue.color[2] = 150;
	CHECK(mh_renderer_set_depth_cue(together, &cue, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_set_depth_cue(apart, &cue, NULL) == MH_ERROR_NONE);
	memset(parts, 0, sizeof(parts));

	for (frame = 0; frame < 5; frame++) {
		for (i = 0; i < 3; i++) {
			moved[i].position[0] =
				frame == 0 ? 0 : (double)i * .3 - .5;
			moved[i].position[1] = -.5;
			moved[i].position[2] = frame < 2 ? 3 : .8 + i * .3;

			if (frame > 2) {
				moved[i].rotation[0] = moved[i].rotation[8] =
					-.8;
				moved[i].rotation[2] = .6;
				moved[i].rotation[6] = -.6;
			}

			memcpy(transforms + (i + 1) * 16, owner, sizeof(owner));

			for (k = 0; k < 3; k++) {
				transforms[(i + 1) * 16 + 12 + k] =
					(float)moved[i].position[k];
				transforms[(i + 1) * 16 + k] =
					(float)moved[i].rotation[k];
				transforms[(i + 1) * 16 + 4 + k] =
					(float)moved[i].rotation[3 + k];
				transforms[(i + 1) * 16 + 8 + k] =
					(float)moved[i].rotation[6 + k];
			}
		}

		CHECK(mh_mesh_animate_objects(&packed, frame, moved, 3, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_renderer_update_mesh(together, 0));
		CHECK(mh_renderer_scene_frame_lens(together, &projection, view,
			NULL, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(apart, &projection, view,
			transforms, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(check_picture_pair(together, apart, 1) == 0);

		for (i = 0; i < 4; i++) {
			CHECK(mh_mesh_scene_span(&packed, i, &parts[i].span));
			parts[i].model_role = i == 0 ? MH_RENDER_MODEL_WORLD
						: MH_RENDER_MODEL_ROOT;
		}

		CHECK(mh_renderer_scene_order(together, parts, 4));
		CHECK(mh_renderer_scene_frame_lens(together, &projection, view,
			NULL, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(check_picture_pair(together, apart, 1) == 0);
		CHECK(mh_renderer_scene_order(together, NULL, 0));
	}

	CHECK(mh_renderer_tga_capture(together, &retained));
	lighting.mode = MH_LIGHT_CAR;
	CHECK(mh_renderer_world_lighting(together, 0, 0, &world, &lighting,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(together, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(check_picture_pair(together, apart, 1) == 0);
	owner[12] = 1;
	CHECK(mh_renderer_scene_frame_lens(together, &projection, view, owner,
		0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_world_lighting(together, 0, 0, NULL, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(together, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(together, &picture));
	CHECK(picture.size == retained.size &&
		memcmp(picture.data, retained.data, picture.size) == 0);
	free(picture.data);
	free(retained.data);

	/* Clearing lighting still permits frames with every instance culled. */
	CHECK(mh_mesh_scene_span(&packed, 0, &span));

	for (i = (unsigned int)span.draw_count; i < packed.draw_count; i++)
		packed.draw_hidden[i] = 1;

	CHECK(mh_renderer_scene_frame_lens(together, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	mh_renderer_destroy(together);
	mh_renderer_destroy(apart);
	mh_mesh_free(&packed);

	for (i = 0; i < 4; i++)
		mh_mesh_free(&separate[i]);

	mh_world_free(&world);
	mh_world_free(&alternate);

	return 0;
}

/* Whole packed scenes, followed by individual-instance comparisons. */
static int
check_asset_frames(char *argv[])
{
	struct mh_file world_file, archive_file, scene_file, map_file,
		config_file;
	struct mh_file *model_files;
	struct mh_world world;
	struct mh_image_collection *archive = NULL;
	struct mh_scene scene;
	struct mh_track track;
	struct mh_directional_light directional;
	struct mh_model *models;
	struct mh_scene_instance *instances;
	struct mh_mesh packed, single;
	struct mh_mesh_object object;
	struct mh_renderer *renderer, *reference;
	const struct mh_mesh *meshes[1];
	struct mh_model_lighting lighting, selected;
	struct mh_point_light *lights;
	const float *position;
	struct mh_indexed_image environment;
	const struct mh_indexed_image *maps[3];
	struct mh_world_view world_view;
	struct mh_projection projection;
	struct mh_depth_cue cue;
	struct mh_error error;
	struct mh_file picture, without_direction;
	unsigned char *text;
	size_t size, i, j, capacity, frame_bytes, total_bytes, changed_frames;
	unsigned int axis, frame;
	float view[16], distance, extent, low[3], high[3], center[3];
	float pose[12], origin[3], spacing[2];
	unsigned long dimensions[2];
	char path[1024];

	CHECK(mh_file_read(argv[1], &world_file, NULL) == MH_ERROR_NONE);
	CHECK(mh_world_myw_decode(world_file.data, world_file.size, &world,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_file_read(argv[2], &archive_file, NULL) == MH_ERROR_NONE);
	CHECK(mh_image_pdi_open(archive_file.data, archive_file.size, &archive,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_file_read(argv[3], &scene_file, NULL) == MH_ERROR_NONE);
	CHECK(mh_tbl_decode(scene_file.data, scene_file.size, &text, &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_scene_lws_parse(text, size, &scene, NULL) == MH_ERROR_NONE);
	free(text);
	CHECK(mh_file_read(argv[5], &config_file, NULL) == MH_ERROR_NONE);
	CHECK(mh_tbl_decode(config_file.data, config_file.size, &text, &size,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_track_config_trk_decode(text, size, &track, NULL) ==
		MH_ERROR_NONE);
	free(text);
	CHECK(scene.object_count != 0 && scene.object_count < 10000);
	model_files = calloc(scene.object_count, sizeof(*model_files));
	models = calloc(scene.object_count, sizeof(*models));
	instances = calloc(scene.object_count, sizeof(*instances));
	CHECK(model_files != NULL && models != NULL && instances != NULL);

	for (i = 0; i < scene.object_count; i++) {
		instances[i].pose = &scene.objects[i];

		for (j = 0; j < i; j++)
			if (strcmp(scene.objects[j].model,
				scene.objects[i].model) == 0)
				break;

		if (j < i) {
			instances[i].model = instances[j].model;
			continue;
		}

		CHECK(fgets(path, sizeof(path), stdin) != NULL);
		path[strcspn(path, "\r\n")] = 0;
		CHECK(mh_file_read(path, &model_files[i], NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_model_myo_decode(model_files[i].data,
			model_files[i].size, &models[i],
			NULL) == MH_ERROR_NONE);
		instances[i].model = &models[i];
	}

	CHECK(mh_mesh_build_world_native_profile(mh_image_fallback_decoder(),
		&world, archive, track.world_brightness, instances,
		scene.object_count, MH_RENDER_SOFTWARE, &packed,
		NULL) == MH_ERROR_NONE);
	mh_mesh_track_materials(&packed, &track);
	CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(),
		&packed, archive, NULL) == MH_ERROR_NONE);
	memset(&world_view, 0, sizeof(world_view));
	world_view.profile = MH_RENDER_SOFTWARE;
	world_view.end[0] = world.width;
	world_view.end[1] = world.height;
	CHECK(mh_mesh_world_cull(&packed, &world_view, NULL) == MH_ERROR_NONE);
	meshes[0] = &packed;

	if (mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &renderer, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	CHECK(mh_file_read(argv[4], &map_file, NULL) == MH_ERROR_NONE);
	CHECK(mh_image_ilbm_decode_indexed(map_file.data, map_file.size,
		&environment, NULL) == MH_ERROR_NONE);
	maps[0] = maps[1] = maps[2] = &environment;
	CHECK(mh_renderer_set_environment(renderer, maps, NULL) ==
		MH_ERROR_NONE);
	memset(&lighting, 0, sizeof(lighting));
	lighting.mode = MH_LIGHT_OBJECT;
	lighting.intensity = (float)track.light_intensity;
	lighting.specular_scale = (float)track.specular_factor;
	CHECK(world.has_directional);
	directional = world.directional;
	CHECK(mh_light_directional_color(directional.color,
		(float)directional.intensity, lighting.directional_color,
		NULL) == MH_ERROR_NONE);

	for (axis = 0; axis < 3; axis++) {
		lighting.direction[axis] = (float)directional.direction[axis];
		lighting.ambient[axis] = (float)track.object_ambient[axis];
		lighting.brightness[axis] =
			(float)track.object_brightness[axis];
	}

	capacity = (size_t)world.link_count;
	CHECK(capacity <= (size_t)-1 / sizeof(*lights));
	lights = malloc((capacity ? capacity : 1) * sizeof(*lights));
	CHECK(lights != NULL);
	memset(&cue, 0, sizeof(cue));
	cue.color[0] = 200;
	cue.color[1] = 150;
	cue.color[2] = 100;
	CHECK(mh_renderer_set_depth_cue(renderer, &cue, NULL) == MH_ERROR_NONE);
	total_bytes = changed_frames = 0;

	for (i = 0; i < scene.object_count; i++) {
		CHECK(mh_mesh_animate(&packed, (double)(i % 11), NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_mesh_scene_object(&packed, i, &object));
		CHECK(mh_mesh_build_lods_profile(mh_image_fallback_decoder(),
			instances[i].model, 1, archive,
			MH_RENDER_SOFTWARE, &single,
			NULL) == MH_ERROR_NONE);
		mh_mesh_track_materials(&single, &track);
		CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(),
			&single, archive, NULL) == MH_ERROR_NONE);
		meshes[0] = &single;
		CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1,
			320, 200, 0, &reference, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_set_environment(reference, maps, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_renderer_set_depth_cue(reference, &cue, NULL) ==
			MH_ERROR_NONE);

		position = packed.vertices[object.vertex_first].position;

		for (axis = 0; axis < 3; axis++)
			low[axis] = high[axis] = position[axis];

		for (j = 0; j < object.vertex_count; j++) {
			position = packed.vertices[object.vertex_first + j]
					.position;

			for (axis = 0; axis < 3; axis++) {
				if (position[axis] < low[axis])
					low[axis] = position[axis];

				if (position[axis] > high[axis])
					high[axis] = position[axis];
			}
		}

		extent = 0;

		for (axis = 0; axis < 3; axis++) {
			center[axis] = (low[axis] + high[axis]) * .5f;

			if (high[axis] - low[axis] > extent)
				extent = high[axis] - low[axis];
		}

		distance = extent * 2.5f + 2;
		CHECK(mh_projection_setup(320, 200, 1.6f, 1, 1,
			distance < 50 ? 101 : distance * 2 + 1,
			&projection, NULL) == MH_ERROR_NONE);
		memset(packed.draw_hidden, 1, packed.draw_count);
		memset(packed.draw_hidden + object.span.draw_first, 0,
			object.span.draw_count);
		memset(view, 0, sizeof(view));
		view[0] = view[5] = view[15] = 1;
		view[10] = -1;

		for (axis = 0; axis < 3; axis++)
			lighting.eye[axis] = center[axis];

		lighting.eye[2] -= distance;
		view[12] = -lighting.eye[0];
		view[13] = -lighting.eye[1];
		view[14] = lighting.eye[2];
		CHECK(mh_renderer_world_lighting(renderer, 0, 0, &world,
			&lighting, NULL) == MH_ERROR_NONE);
		selected = lighting;
		selected.lights = lights;
		CHECK(mh_software_light_select(&world, object.model + 12,
			lights, capacity, &selected.light_count,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_model_lighting(
			reference, 0, &selected, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view,
			NULL, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(reference, &projection, view,
			object.model, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);

		/*
		 * Some authored planes face away from this fixed inspection
		 * camera.
		 */
		CHECK(check_picture_pair(renderer, reference, 0) == 0);
		CHECK(mh_renderer_tga_capture(renderer, &picture));
		memset(selected.directional_color, 0,
			sizeof(selected.directional_color));
		CHECK(mh_renderer_model_lighting(
			reference, 0, &selected, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(reference, &projection, view,
			object.model, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(reference, &without_direction));
		CHECK(picture.size == without_direction.size);
		changed_frames += memcmp(picture.data, without_direction.data,
					picture.size) != 0;
		free(without_direction.data);
		frame_bytes = 0;

		for (j = PIXEL_OFFSET; j < picture.size; j++)
			frame_bytes += picture.data[j] != 0;

		total_bytes += frame_bytes;
		free(picture.data);
		mh_renderer_destroy(reference);
		mh_mesh_free(&single);

		/*
		 * Exercise the full mixed world/scenery producer every 16
		 * instances.
		 */
		if (i % 16 == 0) {
			memset(packed.draw_hidden, 0, packed.draw_count);
			memset(pose, 0, sizeof(pose));
			pose[0] = pose[4] = pose[8] = 1;

			for (axis = 0; axis < 3; axis++) {
				pose[9 + axis] = lighting.eye[axis];
				origin[axis] = (float)world.grid_minimum[axis];
			}

			for (axis = 0; axis < 2; axis++)
				spacing[axis] = (float)world.cell_size[axis];

			dimensions[0] = world.width;
			dimensions[1] = world.height;
			CHECK(mh_world_view_setup_profile(MH_RENDER_SOFTWARE,
				320, 200, 1, 1, projection.far_distance,
				pose, origin, spacing, dimensions,
				&world_view, NULL) == MH_ERROR_NONE);
			CHECK(mh_mesh_world_cull(&packed, &world_view, NULL) ==
				MH_ERROR_NONE);

			for (frame = 0; frame < 2; frame++) {
				CHECK(mh_mesh_animate(&packed, frame * .5,
					NULL) == MH_ERROR_NONE);
				CHECK(mh_renderer_update_mesh(renderer, 0));

				if (mh_renderer_scene_frame_lens(renderer,
					&projection, view, NULL, 0, NULL,
					NULL, NULL,
					&error) != MH_ERROR_NONE) {
					fprintf(stderr, "scene %lu: %s\n",
						(unsigned long)i,
						error.message);
					return 1;
				}
			}
		}
	}

	CHECK(total_bytes != 0 && changed_frames != 0);
	printf("%lu instances match separate draws; %lu nonzero channels; %lu frames change with directional light\n",
		(unsigned long)scene.object_count, (unsigned long)total_bytes,
		(unsigned long)changed_frames);
	mh_renderer_destroy(renderer);
	mh_mesh_free(&packed);

	for (i = 0; i < scene.object_count; i++) {
		free(model_files[i].data);
		mh_model_free(&models[i]);
	}

	free(model_files);
	free(models);
	free(instances);
	free(lights);
	free(environment.indices);
	mh_scene_free(&scene);
	free(world_file.data);
	mh_world_free(&world);
	mh_image_collection_close(archive);
	free(archive_file.data);
	free(scene_file.data);
	free(map_file.data);
	free(config_file.data);

	return 0;
}

int
main(int argc, char *argv[])
{
	if (argc == 1)
		return check_fixture_frames();

	return argc == 6 ? check_asset_frames(argv) : 2;
}
