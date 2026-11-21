#include <string.h>

#include "engine/core/error.h"
#include "engine/render/lod.h"
#include "engine/render/scene.h"

enum mh_error_code
mh_race_scene_details(struct mh_race_scene *scene, const struct mh_field *field,
	const float *models, unsigned int width, enum mh_render_profile profile,
	struct mh_error *error_out)
{
	struct mh_lod_input input;
	struct mh_lod_selection selected[MH_RACE_MAX_CARS];
	const struct mh_drive *viewed;
	const struct mh_mesh *wheel_mesh;
	enum mh_error_code status;
	unsigned int entry, axis, wheel, variant, body;

	if (scene == NULL || field == NULL || models == NULL ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS ||
		(unsigned int)profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid car detail field");

	viewed = &field->cars[mh_field_viewed(field)];
	memset(&input, 0, sizeof(input));
	input.renderer = profile == MH_RENDER_SOFTWARE ? MH_LOD_SOFTWARE
						: MH_LOD_HARDWARE;
	input.focal =
		viewed->camera_goal || viewed->camera_mode == MH_CAMERA_TRACK
		? viewed->camera_focal
		: MH_CAMERA_RACE_FOCAL;
	input.width = (float)width;
	input.ignition = (viewed->cheats & MH_CHEAT_IGNITION) != 0;
	input.supercars = (viewed->cheats & MH_CHEAT_SUPERCARS) != 0;

	for (axis = 0; axis < 3; axis++)
		input.eye[axis] = viewed->camera.position[axis];

	for (entry = 0; entry < field->count; entry++) {
		body = 1 + MH_CAR_MESHES * entry;
		input.levels = scene->assets.meshes[body].lod_count;
		input.unavailable =
			(field->unavailable_mask & (1U << entry)) != 0;

		for (axis = 0; axis < 3; axis++)
			input.position[axis] = models[body * 16 + 12 + axis];

		status = mh_lod_select(&input, &selected[entry], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (wheel = 0; wheel < 4; wheel++) {
			variant = field->cars[entry].wheels.drawn[wheel / 2];
			wheel_mesh = &scene->assets.meshes[body + 1 + wheel];

			if (variant >= wheel_mesh->lod_count)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid wheel animation variant");
		}
	}

	for (entry = 0; entry < field->count; entry++) {
		body = 1 + MH_CAR_MESHES * entry;
		scene->assets.meshes[body].lod = selected[entry].level;
		scene->visuals.coarse[entry] = selected[entry].coarse;

		for (wheel = 0; wheel < 4; wheel++)
			scene->assets.meshes[body + 1 + wheel].lod =
				field->cars[entry].wheels.drawn[wheel / 2];
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_race_scene_part_visible(const struct mh_race_scene *scene,
	const struct mh_field *field, unsigned int mesh)
{
	unsigned int entry, viewed, body;

	if (scene == NULL || field == NULL || mesh == 0 ||
		mesh >= 1 + MH_CAR_MESHES * field->count ||
		mesh >= MH_FIELD_MESHES)
		return 0;

	entry = (mesh - 1) / MH_CAR_MESHES;
	body = 1 + MH_CAR_MESHES * entry;
	viewed = mh_field_viewed(field);

	return (field->active_mask & (1U << entry)) != 0 &&
		!(entry == viewed &&
			mh_drive_camera_in_car(&field->cars[viewed]))
		/* Gate wheels by quality; share with body 1. */
		&& ((mesh - 1) % MH_CAR_MESHES == 0 ||
			(scene->assets.body_start_detail != 2 &&
				scene->assets.meshes[body].lod <= 1));
}
