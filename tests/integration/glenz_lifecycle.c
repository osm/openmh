#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_scene.h"
#include "engine/assets/ilbm.h"
#include "engine/render/scene.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

static struct mh_race_scene scene;
static struct mh_field field;
static float models[MH_FIELD_MESHES * 16];
static float matrix[16], view[16];

static enum mh_error_code
check(int condition, const char *message, struct mh_error *error_out)
{
	return mh_error_set(error_out,
		condition ? MH_ERROR_NONE : MH_ERROR_FAILED,
		condition ? NULL : message);
}

/* Read the current course's maps independently of the scene map loader. */
static enum mh_error_code
check_maps(const char *root, struct mh_error *error_out)
{
	struct mh_file file;
	struct mh_image image;
	const struct mh_image *actual;
	const struct mh_mesh *mesh;
	char path[1024];
	size_t part, index;
	unsigned int map;
	enum mh_error_code status;

	for (map = 0; map < 2; map++) {
		status = mh_asset_path_from(mh_filesystem_resources(), root,
			scene.assets.track.environment[map + 1], path,
			sizeof(path), error_out);

		if (status == MH_ERROR_NONE)
			status = mh_resource_source_read(
				mh_filesystem_resources(), path, &file,
				error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_image_ilbm_decode(
			file.data, file.size, &image, error_out);
		free(file.data);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_image_texture_format(
			&image, scene.assets.opaque_format, error_out);

		if (status != MH_ERROR_NONE) {
			free(image.rgba);
			return status;
		}

		for (part = 1; part < MH_FIELD_MESHES; part++) {
			mesh = &scene.assets.meshes[part];
			index = scene.visuals.glenz.textures[part][map];

			if (mesh->vertex_count == 0)
				continue;

			status =
				check(index != 0 && index < mesh->texture_count,
					"missing scene map binding", error_out);

			if (status != MH_ERROR_NONE)
				break;

			actual = &mesh->textures[index];
			status = check(actual->width == image.width &&
					actual->height == image.height &&
					memcmp(actual->rgba, image.rgba,
						(size_t)image.width *
							image.height * 4) == 0,
				"scene retained a different course's map",
				error_out);

			if (status != MH_ERROR_NONE)
				break;
		}

		free(image.rgba);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
prepare_frame(struct mh_error *error_out)
{
	struct mh_renderer_glenz material;
	size_t part;
	enum mh_error_code status;

	mh_field_matrices(&field, 1.5, models, matrix, view);
	status = mh_race_scene_glenz_scroll(
		&scene, &field, models, 1, error_out);

	for (part = 1; part < MH_FIELD_MESHES && status == MH_ERROR_NONE;
		part++) {
		if (scene.assets.meshes[part].vertex_count == 0)
			continue;

		status = mh_race_scene_glenz_material(&scene, part, models,
			matrix, view, field.cars[0].eye, 960, 640, &material,
			error_out);
	}

	return status;
}

static enum mh_error_code
check_retained(const char *root, int grid, struct mh_error *error_out)
{
	struct mh_scene_glenz retained;
	size_t counts[MH_FIELD_MESHES], part;
	enum mh_error_code status;

	retained = scene.visuals.glenz;

	for (part = 0; part < MH_FIELD_MESHES; part++)
		counts[part] = scene.assets.meshes[part].texture_count;

	status = mh_field_reset(&field, grid, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_glenz(
			mh_filesystem_resources(), root, &scene, error_out);

	if (status == MH_ERROR_NONE)
		status = check(memcmp(&retained, &scene.visuals.glenz,
				sizeof(retained)) == 0,
			"reset or map reload replaced retained Glenz state",
			error_out);

	for (part = 0; part < MH_FIELD_MESHES && status == MH_ERROR_NONE;
		part++)
		status = check(
			counts[part] == scene.assets.meshes[part].texture_count,
			"map reload appended duplicate textures", error_out);

	if (status == MH_ERROR_NONE)
		status = prepare_frame(error_out);

	if (status == MH_ERROR_NONE)
		status = check_maps(root, error_out);

	return status;
}

static enum mh_error_code
check_course(const char *root, unsigned int course, unsigned int precision,
	struct mh_error *error_out)
{
	struct mh_drive drive;
	struct mh_av_settings settings;
	struct mh_scene_glenz empty;
	const struct mh_car *roster[7];
	unsigned int car;
	enum mh_error_code status;

	memset(&empty, 0, sizeof(empty));
	status = mh_av_settings_init(&settings, 0, 0, error_out);
	settings.texture_precision = precision;

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_settings(
			root, 1, course, 0, &settings, &scene, error_out);

	if (status == MH_ERROR_NONE)
		status = check(scene.assets.opaque_format ==
				(precision == MH_AV_TEXTURE_16BIT
						? MH_TEXTURE_RGB565
						: MH_TEXTURE_XRGB8888),
			"scene did not select saved texture precision",
			error_out);

	/* Later menu edits cannot change this scene's fixed texture format. */
	settings.texture_precision = precision == MH_AV_TEXTURE_16BIT
		? MH_AV_TEXTURE_32BIT
		: MH_AV_TEXTURE_16BIT;

	if (status == MH_ERROR_NONE)
		status = check(memcmp(&scene.visuals.glenz, &empty,
				sizeof(empty)) == 0,
			"new scene inherited Glenz state", error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_ai(root, &scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_add_opponents(
			root, 1, course, 7, &scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_glenz(
			mh_filesystem_resources(), root, &scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_start(&scene.assets.car,
			&scene.assets.world_collision, &scene.assets.materials,
			&scene.assets.route, &scene.assets.track, 0, 0, 0,
			&drive, error_out);

	for (car = 0; car < 7; car++)
		roster[car] = &scene.assets.opponents[car];

	if (status == MH_ERROR_NONE)
		status = mh_field_start(&drive, roster, 7,
			&scene.assets.ai_path, 1, &field, error_out);

	if (status == MH_ERROR_NONE)
		status = prepare_frame(error_out);

	/* Leave the grid so recovery/reset exercise a changed object pose. */
	for (car = 0; car < field.count && status == MH_ERROR_NONE; car++)
		field.cars[car].vehicle.motion.position[2] += 7;

	if (status == MH_ERROR_NONE)
		status = prepare_frame(error_out);

	if (status == MH_ERROR_NONE)
		status = check_retained(root, 0, error_out);

	if (status == MH_ERROR_NONE)
		status = check_retained(root, 1, error_out);

	mh_race_scene_free(&scene);

	if (status == MH_ERROR_NONE)
		status = check(memcmp(&scene.visuals.glenz, &empty,
				sizeof(empty)) == 0,
			"scene teardown left Glenz bindings/history",
			error_out);

	return status;
}

int
main(int argc, char *argv[])
{
	static const unsigned int courses[] = {1, 2, 16, 1};
	struct mh_error error;
	enum mh_error_code status;
	unsigned int i;

	if (argc != 2) {
		fprintf(stderr, "Usage: mh-glenz-lifecycle GAME_ROOT\n");
		return 2;
	}

	for (i = 0; i < sizeof(courses) / sizeof(courses[0]); i++) {
		status = check_course(argv[1], courses[i],
			i % 2 == 0 ? MH_AV_TEXTURE_16BIT : MH_AV_TEXTURE_32BIT,
			&error);

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "Glenz course %u: %s\n", courses[i],
				error.message);
			return 1;
		}
	}

	puts("Glenz: eight-car recovery/restart, retained precision and 16/32-bit maps across 1/2/16/1 scene reloads passed");

	return 0;
}
