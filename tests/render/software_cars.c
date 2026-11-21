#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/render/software_light_select.h"
#include "backends/render/software_provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/car.h"
#include "engine/assets/ilbm.h"
#include "engine/assets/myo.h"
#include "engine/assets/myw.h"
#include "engine/assets/tbl.h"
#include "engine/assets/track.h"
#include "engine/core/rotation.h"
#include "engine/geometry/model.h"
#include "engine/geometry/world.h"
#include "engine/render/scene.h"
#include "interfaces/render/backend.h"

#define PIXEL_OFFSET 18

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static struct mh_race_scene scene;
static struct mh_field field;

/* Each part and detail level is independent of the packed chain. */
static struct mh_mesh singles[MH_CAR_MESHES][MH_MESH_LODS];
static struct mh_model models[MH_CAR_MESHES][MH_MESH_LODS];
static struct mh_file model_files[MH_CAR_MESHES][MH_MESH_LODS];

static int
read_table(const char *path, struct mh_file *text)
{
	struct mh_file file;

	CHECK(mh_file_read(path, &file, NULL) == MH_ERROR_NONE);
	CHECK(mh_tbl_decode(file.data, file.size, &text->data, &text->size,
		NULL) == MH_ERROR_NONE);
	free(file.data);

	return 0;
}

static int
read_models(const struct mh_image_collection *archive)
{
	char path[1024];
	struct mh_file *file;
	struct mh_mesh *mesh;
	unsigned int part, level;
	size_t length;

	for (part = 0; part < MH_CAR_MESHES; part++)
		for (level = 0; level < MH_MESH_LODS; level++) {
			CHECK(fgets(path, sizeof(path), stdin) != NULL);
			length = strlen(path);
			CHECK(length > 1 && path[length - 1] == '\n');
			path[length - 1] = 0;
			file = &model_files[part][level];
			mesh = &singles[part][level];
			CHECK(mh_file_read(path, file, NULL) == MH_ERROR_NONE);
			CHECK(mh_model_myo_decode(file->data, file->size,
				&models[part][level],
				NULL) == MH_ERROR_NONE);
			CHECK(mh_mesh_build_lods_profile(
				mh_image_fallback_decoder(),
				&models[part][level], 1, archive,
				MH_RENDER_SOFTWARE, mesh,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_mesh_load_indexed_textures(
				mh_image_fallback_decoder(), mesh,
				archive, NULL) == MH_ERROR_NONE);
		}

	return 0;
}

static int
pack_detail_chains(
	const struct mh_image_collection *archive, unsigned int quality)
{
	struct mh_model chain[MH_MESH_LODS];
	struct mh_mesh *mesh;
	unsigned int part, level, source_lod;

	scene.assets.body_start_detail = quality;

	for (part = 0; part < MH_CAR_MESHES; part++) {
		for (level = 0; level < MH_MESH_LODS; level++) {
			source_lod =
				part == 0 && level < quality ? quality : level;
			chain[level] = models[part][source_lod];
		}

		mesh = &scene.assets.meshes[1 + part];
		CHECK(mh_mesh_build_lods_profile(mh_image_fallback_decoder(),
			chain, MH_MESH_LODS, archive, MH_RENDER_SOFTWARE,
			mesh, NULL) == MH_ERROR_NONE);
		CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(),
			mesh, archive, NULL) == MH_ERROR_NONE);
		CHECK(mesh->radius == (float)chain[0].radius);
	}

	return 0;
}

static void
set_car_pose(const float origin[3], unsigned int frame)
{
	static const double distances[4] = {8, 18, 30, 50};
	struct mh_drive *drive;
	double angle[3], distance;
	unsigned int axis, wheel;

	drive = &field.cars[0];
	memset(drive, 0, sizeof(*drive));
	drive->car = &scene.assets.car;
	drive->track = &scene.assets.track;
	drive->camera_mode = frame == 12 ? MH_CAMERA_IN_CAR : MH_CAMERA_CHASE;
	drive->camera_focal = 1;
	drive->suspension.length = scene.assets.car.spring_length;
	drive->vehicle.motion.rotation[0] = drive->vehicle.motion.rotation[4] =
		drive->vehicle.motion.rotation[8] = 1;
	angle[0] = angle[2] = 0;
	angle[1] = frame * .23;
	mh_rotation_turn(drive->vehicle.motion.rotation, angle);
	drive->vehicle.smoothed_steering = .6 * sin(frame * .7);
	drive->wheels.angle[0] = frame * .4;
	drive->wheels.angle[1] = frame * .7;
	drive->wheels.drawn[0] = (frame + frame / 4) % 4;
	drive->wheels.drawn[1] = (frame + frame / 4 + 1) % 4;

	for (wheel = 0; wheel < 4; wheel++)
		drive->vehicle.motion.extension[wheel] =
			(double)((frame + wheel) % 5) / 4;

	for (axis = 0; axis < 3; axis++)
		drive->vehicle.motion.position[axis] = origin[axis];

	drive->vehicle.motion.position[1] -= .3199999928474426;
	distance = distances[frame % 4];

	if (frame == 18)
		distance = .6; /* Cross the near plane. */
	else if (frame == 19)
		distance = mh_drive_camera_far(drive) + 100;

	drive->eye[0] = origin[0] + distance * .6;
	drive->eye[1] = origin[1] + (frame == 18 ? 0 : 1);
	drive->eye[2] = origin[2] - distance * .8;
	drive->camera.rotation[0] = .8;
	drive->camera.rotation[2] = .6;
	drive->camera.rotation[4] = 1;
	drive->camera.rotation[6] = -.6;
	drive->camera.rotation[8] = .8;

	if (frame == 14)
		drive->eye[0] += 1000;

	for (axis = 0; axis < 3; axis++)
		drive->camera.position[axis] = (float)drive->eye[axis];

	if (frame == 16)
		drive->cheats = MH_CHEAT_IGNITION;
	else if (frame == 17)
		drive->cheats = MH_CHEAT_SUPERCARS;

	field.count = 1;
	field.active_mask = frame == 13 ? 0 : 1;
}

static int
setup_renderer(const struct mh_mesh *meshes[], size_t count,
	const struct mh_indexed_image *maps[3], struct mh_renderer **renderer)
{
	struct mh_renderer_scene_part parts[MH_DRIVE_MESHES];
	struct mh_depth_cue cue;
	struct mh_error error;
	unsigned int part;

	if (mh_renderer_open(mh_software_backend(), NULL, meshes, count, 320,
		200, 0, renderer, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	CHECK(mh_renderer_set_environment(*renderer, maps, NULL) ==
		MH_ERROR_NONE);
	memset(&cue, 0, sizeof(cue));
	cue.color[0] = 120;
	cue.color[1] = 100;
	cue.color[2] = 80;
	CHECK(mh_renderer_set_depth_cue(*renderer, &cue, NULL) ==
		MH_ERROR_NONE);
	memset(parts, 0, sizeof(parts));

	for (part = 0; part < count; part++) {
		parts[part].mesh = part;
		parts[part].active_lod = 1;
		parts[part].model_role = part == count - MH_CAR_MESHES
			? MH_RENDER_MODEL_ROOT
			: MH_RENDER_MODEL_CHILD;

		if (count == MH_DRIVE_MESHES && part == 0)
			parts[part].model_role = MH_RENDER_MODEL_WORLD;
	}

	CHECK(mh_renderer_scene_order(*renderer, parts, count));

	return 0;
}

static int
save_picture(const char *path, const struct mh_file *picture)
{
	FILE *file;
	size_t written;
	int closed;

	file = fopen(path, "wb");
	CHECK(file != NULL);
	written = fwrite(picture->data, 1, picture->size, file);
	closed = fclose(file);
	CHECK(written == picture->size && closed == 0);

	return 0;
}

static int
check_car_frames(const struct mh_indexed_image *maps[3], const float origin[3],
	unsigned int quality, const char *capture)
{
	struct mh_renderer *packed, *separate;
	const struct mh_mesh *meshes[MH_CAR_MESHES];
	struct mh_model_lighting lighting;
	struct mh_av_settings settings;
	struct mh_projection projection;
	struct mh_point_light *lights;
	struct mh_file first, second;
	float transforms[MH_FIELD_MESHES * 16], view[16], matrix[16];
	unsigned int part, frame, source_lod, roots, expected, level, visible;
	size_t light_count, capacity, frame_bytes, total_bytes, i;

	capacity = scene.assets.lighting_world.link_count;
	CHECK(capacity <= (size_t)-1 / sizeof(*lights));
	lights = malloc((capacity ? capacity : 1) * sizeof(*lights));
	CHECK(lights != NULL);

	for (part = 0; part < MH_CAR_MESHES; part++)
		meshes[part] = &scene.assets.meshes[1 + part];

	CHECK(setup_renderer(meshes, MH_CAR_MESHES, maps, &packed) == 0);
	total_bytes = 0;
	memset(&settings, 0, sizeof(settings));

	for (frame = 0; frame < 20; frame++) {
		set_car_pose(origin, frame);
		settings.car_shading = frame / 4 % 3;
		mh_field_matrices(&field, 1.6, transforms, matrix, view);
		CHECK(mh_race_scene_details(&scene, &field, transforms, 320,
			MH_RENDER_SOFTWARE, NULL) == MH_ERROR_NONE);
		CHECK(mh_race_scene_visibility_profile(MH_RENDER_SOFTWARE,
			&scene, &field, transforms, 320, 200, &roots,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_projection_setup(320, 200, 1.3333333333333335f, 1,
			MH_CAMERA_RACE_NEAR,
			mh_drive_camera_far(&field.cars[0]), &projection,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_software_light_select(&scene.assets.lighting_world,
			transforms + 28, lights, capacity, &light_count,
			NULL) == MH_ERROR_NONE);
		level = scene.assets.meshes[1].lod;
		expected = frame == 16		     ? 2
			: frame == 17		     ? 3
			: frame == 18		     ? 0
			: frame == 14 || frame == 19 ? 3
						: frame % 4;
		CHECK(level == expected);

		for (part = 0; part < MH_CAR_MESHES; part++) {
			source_lod = scene.assets.meshes[1 + part].lod;

			if (part == 0 && source_lod < quality)
				source_lod = quality;

			meshes[part] = &singles[part][source_lod];
		}

		CHECK(setup_renderer(meshes, MH_CAR_MESHES, maps, &separate) ==
			0);

		for (part = 0; part < MH_CAR_MESHES; part++) {
			visible = (roots & 1) != 0 &&
				mh_race_scene_part_visible(
					&scene, &field, 1 + part);
			CHECK(mh_renderer_mesh_visible(packed, part, visible));
			CHECK(mh_renderer_mesh_visible(
				separate, part, visible));
			CHECK(mh_race_scene_software_lighting(&scene, &field,
				1 + part, &settings, &lighting,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_renderer_world_lighting(packed, part, 0,
				&scene.assets.lighting_world, &lighting,
				NULL) == MH_ERROR_NONE);
			lighting.lights = lights;
			lighting.light_count = light_count;
			CHECK(mh_renderer_model_lighting(separate, part,
				&lighting, NULL) == MH_ERROR_NONE);
		}

		CHECK(mh_renderer_scene_frame_lens(packed, &projection, view,
			transforms + 16, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(separate, &projection, view,
			transforms + 16, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(packed, &first));
		CHECK(mh_renderer_tga_capture(separate, &second));
		CHECK(first.size == second.size &&
			memcmp(first.data, second.data, first.size) == 0);
		frame_bytes = 0;

		for (i = PIXEL_OFFSET; i < first.size; i++)
			frame_bytes += first.data[i] != 0;

		if (frame == 12 || frame == 13 || frame == 14 || frame == 19)
			CHECK(frame_bytes == 0);
		else if (frame < 12)
			CHECK(frame_bytes > 0);

		if (capture != NULL && quality == 0 && frame == 8)
			CHECK(save_picture(capture, &first) == 0);

		total_bytes += frame_bytes;
		free(first.data);
		free(second.data);
		mh_renderer_destroy(separate);
	}

	CHECK(total_bytes > 0);
	mh_renderer_destroy(packed);
	free(lights);

	return 0;
}

static int
check_world_frames(const struct mh_image_collection *archive,
	const struct mh_indexed_image *maps[3], const float origin[3])
{
	struct mh_renderer *packed, *separate;
	const struct mh_mesh *meshes[MH_DRIVE_MESHES];
	struct mh_model_lighting lighting;
	struct mh_projection projection;
	struct mh_file first, second;
	float transforms[MH_FIELD_MESHES * 16], view[16], matrix[16];
	unsigned int frame, part, roots, visible;

	CHECK(mh_mesh_build_world_native_profile(mh_image_fallback_decoder(),
		&scene.assets.lighting_world, archive,
		scene.assets.track.world_brightness, NULL, 0,
		MH_RENDER_SOFTWARE, &scene.assets.meshes[0],
		NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(),
		&scene.assets.meshes[0], archive, NULL) == MH_ERROR_NONE);

	set_car_pose(origin, 0);
	mh_field_matrices(&field, 1.6, transforms, matrix, view);
	CHECK(mh_race_scene_visibility_profile(MH_RENDER_SOFTWARE, &scene,
		&field, transforms, 320, 200, &roots,
		NULL) == MH_ERROR_NONE);

	for (part = 0; part < MH_DRIVE_MESHES; part++)
		meshes[part] = &scene.assets.meshes[part];

	CHECK(setup_renderer(meshes, MH_DRIVE_MESHES, maps, &packed) == 0);

	for (frame = 0; frame < 4; frame++) {
		set_car_pose(origin, frame);
		mh_field_matrices(&field, 1.6, transforms, matrix, view);
		CHECK(mh_race_scene_details(&scene, &field, transforms, 320,
			MH_RENDER_SOFTWARE, NULL) == MH_ERROR_NONE);
		CHECK(mh_race_scene_visibility_profile(MH_RENDER_SOFTWARE,
			&scene, &field, transforms, 320, 200, &roots,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_projection_setup(320, 200, 1.3333333333333335f, 1,
			MH_CAMERA_RACE_NEAR,
			mh_drive_camera_far(&field.cars[0]), &projection,
			NULL) == MH_ERROR_NONE);

		for (part = 1; part < MH_DRIVE_MESHES; part++)
			meshes[part] = &singles[part - 1]
					[scene.assets.meshes[part].lod];

		CHECK(setup_renderer(
			meshes, MH_DRIVE_MESHES, maps, &separate) == 0);

		for (part = 1; part < MH_DRIVE_MESHES; part++) {
			visible = (roots & 1) != 0 &&
				mh_race_scene_part_visible(
					&scene, &field, part);
			CHECK(mh_renderer_mesh_visible(packed, part, visible));
			CHECK(mh_renderer_mesh_visible(
				separate, part, visible));
			CHECK(mh_race_scene_software_lighting(&scene, &field,
				part, NULL, &lighting,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_renderer_world_lighting(packed, part, 1,
				&scene.assets.lighting_world, &lighting,
				NULL) == MH_ERROR_NONE);
			CHECK(mh_renderer_world_lighting(separate, part, 1,
				&scene.assets.lighting_world, &lighting,
				NULL) == MH_ERROR_NONE);
		}

		CHECK(mh_renderer_scene_frame_lens(packed, &projection, view,
			transforms, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(separate, &projection, view,
			transforms, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(packed, &first));
		CHECK(mh_renderer_tga_capture(separate, &second));
		CHECK(first.size == second.size &&
			memcmp(first.data, second.data, first.size) == 0);
		free(first.data);
		free(second.data);
		mh_renderer_destroy(separate);
	}

	mh_renderer_destroy(packed);
	mh_mesh_free(&scene.assets.meshes[0]);

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_file car_text, track_text, world_file, archive_file, map_file;
	struct mh_image_collection *archive = NULL;
	struct mh_indexed_image images[3];
	const struct mh_indexed_image *maps[3];
	struct mh_world *world;
	float origin[3];
	size_t head;
	unsigned int quality, part, level, i;
	size_t cell;

	if (argc != 8 && argc != 9)
		return 2;

	CHECK(read_table(argv[1], &car_text) == 0);
	CHECK(mh_car_config_car_decode(car_text.data, car_text.size,
		&scene.assets.car, NULL) == MH_ERROR_NONE);
	CHECK(read_table(argv[2], &track_text) == 0);
	CHECK(mh_track_config_trk_decode(track_text.data, track_text.size,
		&scene.assets.track, NULL) == MH_ERROR_NONE);
	CHECK(mh_file_read(argv[3], &world_file, NULL) == MH_ERROR_NONE);
	world = &scene.assets.lighting_world;
	CHECK(mh_world_myw_decode(world_file.data, world_file.size, world,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_file_read(argv[4], &archive_file, NULL) == MH_ERROR_NONE);
	CHECK(mh_image_pdi_open(archive_file.data, archive_file.size, &archive,
		NULL) == MH_ERROR_NONE);
	CHECK(read_models(archive) == 0);

	for (i = 0; i < 3; i++) {
		CHECK(mh_file_read(argv[5 + i], &map_file, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_image_ilbm_decode_indexed(map_file.data, map_file.size,
			&images[i], NULL) == MH_ERROR_NONE);
		maps[i] = &images[i];
		free(map_file.data);
	}

	for (cell = 0; cell < world->cell_count; cell++) {
		CHECK(cell < world->cell_count);
		head = world->cells[cell].light_head;

		if (head != MH_WORLD_NO_LIGHT)
			break;
	}

	CHECK(cell < world->cell_count);
	origin[0] = (float)(world->grid_minimum[0] +
		(cell % world->width + .5) * world->cell_size[0]);
	origin[1] = (float)world->grid_minimum[1];
	origin[2] = (float)(world->grid_minimum[2] +
		(cell / world->width + .5) * world->cell_size[1]);

	for (quality = 0; quality < 3; quality++) {
		CHECK(pack_detail_chains(archive, quality) == 0);
		CHECK(check_car_frames(maps, origin, quality,
			argc == 9 ? argv[8] : NULL) == 0);

		if (quality == 0)
			CHECK(check_world_frames(archive, maps, origin) == 0);

		for (part = 0; part < MH_CAR_MESHES; part++)
			mh_mesh_free(&scene.assets.meshes[1 + part]);
	}

	for (part = 0; part < MH_CAR_MESHES; part++)
		for (level = 0; level < MH_MESH_LODS; level++) {
			mh_mesh_free(&singles[part][level]);
			free(model_files[part][level].data);
			mh_model_free(&models[part][level]);
		}

	for (i = 0; i < 3; i++)
		free(images[i].indices);

	free(car_text.data);
	free(track_text.data);
	free(world_file.data);
	mh_world_free(world);
	mh_image_collection_close(archive);
	free(archive_file.data);
	puts("60 assembled car and 4 world/car frames match independent detail models");

	return 0;
}
