#include <stddef.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/render/scene.h"

enum {
	HEADLIGHT_RESOURCE = 0,
	TAILLIGHT_RESOURCE = 1
};

static void
prepare_car_halo(struct mh_geometry_halo *halo, int braking,
	struct mh_attached_halo *style)
{
	memset(style, 0, sizeof(*style));

	style->flags = MH_HALO_GLOW;
	style->intensity = 1;
	style->size = .5f;

	/*
	 * Car resource 0=head, 1=rear. The original hardware backend selects a
	 * shared glow atlas.
	 */
	if (halo->resource == HEADLIGHT_RESOURCE) {
		style->flags = MH_HALO_HEADLIGHT;
		style->intensity = .7f;
		style->size = .6f;
		style->flare = .5f;
		style->direction[0] =
			halo->endpoints[0][0] < 0 ? -.1961f : .1961f;
		style->direction[2] = .9806f;
		memset(halo->color, 150, 3);
	} else if (halo->resource == TAILLIGHT_RESOURCE) {
		style->flags = MH_HALO_TAILLIGHT;
		style->intensity = braking ? .75f : .5f;
		style->size = braking ? (float)((double).3f * 1.75) : .3f;

		style->direction[2] = -1;
		halo->color[0] = 255;
		halo->color[1] = 0;
		halo->color[2] = braking ? 0 : 10;
	}
}

static enum mh_error_code
count_attached(const struct mh_race_scene *scene, unsigned int cars,
	size_t scenery_count, size_t *output, struct mh_error *error_out)
{
	size_t count, sources;
	unsigned int i;

	count = scenery_count;

	if (count > MH_LAMP_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "too many scenery halos");

	for (i = 1; i < 1 + MH_CAR_MESHES * cars; i++) {
		sources = scene->assets.meshes[i].halo_count;

		if (sources > MH_LAMP_LIMIT - count)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"too many car halos");

		count += sources;
	}

	*output = count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scene_prepare_lamps(struct mh_race_scene *scene,
	const struct mh_field *field, struct mh_error *error_out)
{
	size_t count = 0, scenery_count;
	enum mh_error_code status;

	if (scene == NULL || field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attached lamp field");

	scenery_count = mh_mesh_scene_halo_count(&scene->assets.meshes[0]);
	status = count_attached(
		scene, field->count, scenery_count, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_lamps_reserve_attached(
		&scene->visuals.lamps, count, error_out);
}

enum mh_error_code
mh_race_scene_update_lamps(struct mh_race_scene *scene,
	const struct mh_field *field, const float *models,
	const float matrix[16], const float view[16], unsigned int width,
	unsigned int height, const struct mh_av_settings *settings,
	struct mh_error *error_out)
{
	struct mh_attached_halo style;
	struct mh_geometry_halo halo;
	struct mh_mesh_lod range;
	struct mh_frustum frustum;
	struct mh_world_view world_view;
	const struct mh_mesh *mesh;
	const struct mh_drive *viewed;
	enum mh_error_code status;
	size_t scenery_count, count = 0, index, n;
	unsigned int i, entry, viewed_entry, root;
	float far_depth, cue_start, position[3], radius;
	int eligible, braking;

	if (scene == NULL || field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || models == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attached lamp field");

	scenery_count = mh_mesh_scene_halo_count(&scene->assets.meshes[0]);
	status = count_attached(
		scene, field->count, scenery_count, &count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (scene->visuals.lamps.count < scene->visuals.lamps.world_count ||
		count !=
			scene->visuals.lamps.count -
				scene->visuals.lamps.world_count)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"attached lamp pool is not prepared");

	viewed_entry = mh_field_viewed(field);
	viewed = &field->cars[viewed_entry];
	far_depth = (float)mh_drive_camera_far(viewed);
	cue_start = (float)scene->assets.track.cue_start;

	if (scene->assets.lighting_world.cells != NULL) {
		status = mh_race_scene_world_view(field,
			&scene->assets.lighting_world, width, height,
			&world_view, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_lamps_world_cull(&scene->visuals.lamps,
				&scene->assets.lighting_world, &world_view,
				error_out);

		if (status != MH_ERROR_NONE)
			return status;

		frustum = world_view.frustum;
	} else
		status = mh_race_scene_frustum(
			field, width, height, &frustum, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	scene->visuals.lamps.halos =
		settings == NULL ? 1 : (int)settings->halos;
	scene->visuals.lamps.flares =
		settings == NULL ? 1 : (int)settings->lens_flares;
	status = mh_lamps_update(&scene->visuals.lamps,
		&scene->assets.lighting_world, matrix, width, height,
		MH_CAMERA_RACE_NEAR, far_depth, cue_start, error_out);

	if (status != MH_ERROR_NONE || !scene->visuals.lamps.halos)
		return status;

	index = scene->visuals.lamps.world_count;
	memset(&style, 0, sizeof(style));
	style.flags = MH_HALO_GLOW;
	style.intensity = .6f;
	style.size = .5f;
	style.scenery = style.tinted = 1;

	/* Clamp negative flare intensity to zero. */
	for (n = 0; n < scenery_count; n++, index++) {
		if (!mh_mesh_scene_halo_bounds(
			&scene->assets.meshes[0], n, position, &radius))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scenery halo bounds");

		/*
		 * Reject the parent before visiting its sources. Skipped slots
		 * have no geometry/query and keep their visibility history.
		 */
		if (!mh_frustum_sphere(&frustum, position, radius))
			continue;

		if (!mh_mesh_scene_halo(
			&scene->assets.meshes[0], n, &halo, style.tint))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid scenery halo pose");

		status = mh_lamps_attached_update(&scene->visuals.lamps, index,
			&halo, &style, NULL, matrix, view, width, height,
			MH_CAMERA_RACE_NEAR, far_depth, cue_start, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 1; i < 1 + MH_CAR_MESHES * field->count; i++) {
		mesh = &scene->assets.meshes[i];
		entry = (i - 1) / MH_CAR_MESHES;
		root = 1 + MH_CAR_MESHES * entry;
		status = mh_mesh_active_lod(mesh, &range, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		/*
		 * The original child loop prepares wheel geometry, but never
		 * visits child auxiliary lights. Only the selected root emits
		 * car sources.
		 */
		eligible = i == root &&
			mh_race_scene_part_visible(scene, field, i) &&
			!(field->unavailable_mask & (1U << entry)) &&
			mh_frustum_sphere(&frustum, models + root * 16 + 12,
				scene->assets.meshes[root].radius);
		braking = field->cars[entry].vehicle.brake_active != 0;

		for (n = 0; n < mesh->halo_count; n++, index++) {
			if (!eligible || n < range.halo_first ||
				n - range.halo_first >= range.halo_count)
				continue;

			halo = mesh->halos[n];
			prepare_car_halo(&halo, braking, &style);

			status = mh_lamps_attached_update(&scene->visuals.lamps,
				index, &halo, &style, models + i * 16, matrix,
				view, width, height, MH_CAMERA_RACE_NEAR,
				far_depth, cue_start, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
