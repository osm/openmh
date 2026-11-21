#include "engine/core/error.h"
#include "engine/render/scene.h"

enum mh_error_code
mh_race_scene_world_view_profile(enum mh_render_profile profile,
	const struct mh_field *field, const struct mh_world *world,
	unsigned int width, unsigned int height, struct mh_world_view *view,
	struct mh_error *error_out)
{
	const struct mh_drive *viewed;
	float pose[12], focal, origin[3], spacing[2];
	unsigned long dimensions[2];
	unsigned int i;

	if (world == NULL || field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid world visibility field");

	viewed = &field->cars[mh_field_viewed(field)];
	focal = viewed->camera_goal || viewed->camera_mode == MH_CAMERA_TRACK
		? viewed->camera_focal
		: MH_CAMERA_RACE_FOCAL;

	for (i = 0; i < 9; i++)
		pose[i] = viewed->camera.rotation[i];

	for (i = 0; i < 3; i++) {
		pose[9 + i] = viewed->camera.position[i];
		origin[i] = (float)world->grid_minimum[i];
	}

	for (i = 0; i < 2; i++)
		spacing[i] = (float)world->cell_size[i];

	dimensions[0] = world->width;
	dimensions[1] = world->height;

	return mh_world_view_setup_profile(profile, width, height, focal,
		MH_CAMERA_RACE_NEAR, mh_drive_camera_far(viewed), pose, origin,
		spacing, dimensions, view, error_out);
}

static enum mh_error_code
scene_frustum(enum mh_render_profile profile, const struct mh_field *field,
	unsigned int width, unsigned int height, struct mh_frustum *frustum,
	struct mh_error *error_out)
{
	const struct mh_drive *viewed;
	float pose[12], focal;
	unsigned int i;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid object visibility field");

	viewed = &field->cars[mh_field_viewed(field)];
	focal = viewed->camera_goal || viewed->camera_mode == MH_CAMERA_TRACK
		? viewed->camera_focal
		: MH_CAMERA_RACE_FOCAL;

	for (i = 0; i < 9; i++)
		pose[i] = viewed->camera.rotation[i];

	for (i = 0; i < 3; i++)
		pose[9 + i] = viewed->camera.position[i];

	return mh_frustum_setup_profile(profile, width, height, focal,
		MH_CAMERA_RACE_NEAR, mh_drive_camera_far(viewed), pose, frustum,
		error_out);
}

enum mh_error_code
mh_race_scene_visibility_profile(enum mh_render_profile profile,
	struct mh_race_scene *scene, const struct mh_field *field,
	const float *models, unsigned int width, unsigned int height,
	unsigned int *roots, struct mh_error *error_out)
{
	struct mh_frustum frustum;
	struct mh_world_view world_view;
	unsigned int i, mask, root;
	enum mh_error_code status;

	if (scene == NULL || models == NULL || roots == NULL ||
		(unsigned int)profile >= MH_RENDER_PROFILE_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing object visibility input");

	if (scene->assets.lighting_world.cells != NULL) {
		status = mh_race_scene_world_view_profile(profile, field,
			&scene->assets.lighting_world, width, height,
			&world_view, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_mesh_world_cull(&scene->assets.meshes[0],
				&world_view, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		frustum = world_view.frustum;
	} else
		status = scene_frustum(
			profile, field, width, height, &frustum, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mask = 0;

	for (i = 0; i < field->count; i++) {
		root = 1 + MH_CAR_MESHES * i;

		if (mh_frustum_sphere_profile(profile, &frustum,
			models + root * 16 + 12,
			scene->assets.meshes[root].radius))
			mask |= 1U << i;
	}

	status = mh_mesh_scene_cull_profile(
		profile, &scene->assets.meshes[0], &frustum, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*roots = mask;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_world_view(const struct mh_field *field,
	const struct mh_world *world, unsigned int width, unsigned int height,
	struct mh_world_view *view, struct mh_error *error_out)
{
	return mh_race_scene_world_view_profile(
		MH_RENDER_GLIDE, field, world, width, height, view, error_out);
}

enum mh_error_code
mh_race_scene_frustum(const struct mh_field *field, unsigned int width,
	unsigned int height, struct mh_frustum *frustum,
	struct mh_error *error_out)
{
	return scene_frustum(
		MH_RENDER_GLIDE, field, width, height, frustum, error_out);
}

enum mh_error_code
mh_race_scene_visibility(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models, unsigned int width,
	unsigned int height, unsigned int *roots, struct mh_error *error_out)
{
	return mh_race_scene_visibility_profile(MH_RENDER_GLIDE, scene, field,
		models, width, height, roots, error_out);
}
