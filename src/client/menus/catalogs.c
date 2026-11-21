#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client/menus/internal.h"
#include "engine/assets/path_menus.h"
#include "engine/audio/horn.h"
#include "engine/core/error.h"
#include "engine/settings/course_music.h"
#include "interfaces/storage/resource.h"

#define PREVIEW_PATH_BYTES 512
#define CAR_PREVIEW_RADIUS 160
#define TRACK_PREVIEW_RADIUS 170
#define INITIAL_LEAGUE_STAGE 3
#define INITIAL_RACE_LAPS 5

static enum mh_error_code
load_thumbnail(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *asset_root,
	const char *relative_path, struct mh_menu_thumbnail *thumbnail,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_menu_thumbnail_load_from(decoder, resources, asset_root,
		relative_path, thumbnail, error_out);

	/* Missing previews retain the original empty oval. */
	if (status == MH_ERROR_NOT_FOUND)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return status;
}

static enum mh_error_code
load_preview_model(const struct mh_wireframe_source *source,
	const struct mh_resource_source *resources, const char *asset_root,
	const char *path, struct mh_wireframe *model, int first, float radius,
	float *scale, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_wireframe_load_from(
		source, resources, asset_root, path, model, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return first ? mh_wireframe_fit(model, radius, scale, error_out)
		: mh_wireframe_center_scale(model, *scale, error_out);
}

static enum mh_error_code
load_car_preview(const char *asset_root, struct mh_menus *menus,
	const struct mh_car *car, unsigned int index, float *scale,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	char relative_path[PREVIEW_PATH_BYTES], *separator;

	strcpy(relative_path, car->line_object);

	for (separator = relative_path; *separator != 0; separator++)
		if (*separator == '\\')
			*separator = '/';

	separator = strrchr(relative_path, '/');

	if (separator == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"car preview has no containing directory");

	strcpy(separator + 1, MH_ASSET_CAR_PREVIEW);
	status = load_thumbnail(menus->thumbnail_decoder, menus->resources,
		asset_root, relative_path, &menus->car_thumbnails[index],
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return load_preview_model(menus->wireframe_source, menus->resources,
		asset_root, car->line_object, &menus->car_models[index],
		index == 0, CAR_PREVIEW_RADIUS, scale, error_out);
}

static enum mh_error_code
load_track_preview(const char *asset_root, struct mh_menus *menus,
	const struct mh_track *track, unsigned int index, float *scale,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	char relative_path[PREVIEW_PATH_BYTES];

	if (track->base_path[0] == 0 || track->line_object[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing track preview path");

	sprintf(relative_path, MH_ASSET_TRACK_PREVIEW_FORMAT, track->base_path);
	status = load_thumbnail(menus->thumbnail_decoder, menus->resources,
		asset_root, relative_path, &menus->track_thumbnails[index],
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return load_preview_model(menus->wireframe_source, menus->resources,
		asset_root, track->line_object, &menus->track_models[index],
		index == 0, TRACK_PREVIEW_RADIUS, scale, error_out);
}

static enum mh_error_code
load_cars(const char *asset_root, struct mh_menus *menus,
	struct mh_error *error_out)
{
	const struct mh_car *cars;
	const struct mh_car *car;
	enum mh_error_code status;
	unsigned int i, source_index, saved_index, order[MH_CAR_CATALOG_LIMIT];
	unsigned long divisions[MH_CAR_CATALOG_LIMIT];
	float scale;

	cars = menus->car_catalog.entries;
	saved_index = menus->single.car;

	if (saved_index >= menus->car_catalog.count)
		saved_index = 0;

	for (i = 0; i < menus->car_catalog.count; i++) {
		if (!cars[i].has_division || !cars[i].has_statistics ||
			cars[i].line_object[0] == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"menu car has incomplete preview metadata");

		divisions[i] = cars[i].division;
	}

	status = mh_menu_catalog_order(divisions, menus->car_catalog.count,
		order, MH_CAR_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Later previews share the first sorted car's fitted scale. */
	scale = 0;

	for (i = 0; i < menus->car_catalog.count; i++) {
		source_index = order[i];
		car = &cars[source_index];
		menus->car_numbers[i] = source_index + 1;
		menus->cars[i].name = car->name;
		menus->cars[i].division = car->division;
		menus->car_titles[i] = car->name;
		menus->car_divisions[i] = car->division;
		menus->car_statistics[i][MH_MENU_CAR_SPEED] = car->top_speed;
		menus->car_statistics[i][MH_MENU_CAR_ACCELERATION] =
			car->acceleration;
		menus->car_statistics[i][MH_MENU_CAR_HANDLING] = car->handling;

		if (!mh_menus_compact(menus))
			status = load_car_preview(
				asset_root, menus, car, i, &scale, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_menu_catalog_restore(menus->car_titles,
		menus->car_catalog.count, cars[saved_index].name,
		&menus->single.car, error_out);
}

static enum mh_error_code
load_tracks(const char *asset_root, const char *progress_directory,
	struct mh_menus *menus, struct mh_error *error_out)
{
	const struct mh_track *tracks;
	const struct mh_track *track;
	enum mh_error_code status;
	unsigned int i, source_index, saved_index,
		order[MH_TRACK_CATALOG_LIMIT];
	unsigned long divisions[MH_TRACK_CATALOG_LIMIT],
		track_divisions[MH_TRACK_CATALOG_LIMIT],
		available[MH_TRACK_CATALOG_LIMIT];
	float scale;

	tracks = menus->track_catalog.entries;
	status = mh_settings_music_bind(
		menus->settings, tracks, menus->track_catalog.count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	saved_index = menus->details.track;

	if (saved_index >= menus->track_catalog.count)
		saved_index = 0;

	for (i = 0; i < menus->track_catalog.count; i++) {
		if (!tracks[i].has_division)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"menu track has no Division");

		divisions[i] = tracks[i].division >= MH_TRACK_CUSTOM_DIVISION
			? 0
			: tracks[i].division;
	}

	status = mh_menu_catalog_order(divisions, menus->track_catalog.count,
		order, MH_TRACK_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Track previews use their own first-model scale, independent of cars.
	 */
	scale = 0;

	for (i = 0; i < menus->track_catalog.count; i++) {
		source_index = order[i];
		track = &tracks[source_index];
		menus->track_numbers[i] = source_index + 1;
		menus->course_music[i] =
			mh_settings_track_music(menus->settings, track);
		menus->source_music[source_index] =
			(unsigned int)menus->course_music[i];
		menus->tracks[i].name = track->name;
		menus->tracks[i].division = track_divisions[i] =
			track->division;
		status = mh_progress_available_from(menus->resources,
			menus->user_data, asset_root, progress_directory, track,
			&available[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (available[i] && i < sizeof(menus->availability) * CHAR_BIT)
			menus->availability |= 1U << i;

		menus->track_titles[i] = track->name;

		if (!mh_menus_compact(menus))
			status = load_track_preview(
				asset_root, menus, track, i, &scale, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_menu_catalog_restore(menus->track_titles,
		menus->track_catalog.count, tracks[saved_index].name,
		&menus->details.track, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_menus_unlocks_apply(
		menus, track_divisions, available, error_out);
}

enum mh_error_code
mh_menus_catalogs_load(const char *asset_root, const char *progress_directory,
	struct mh_menus *menus, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i;

	status = mh_car_catalog_load_from(menus->catalog_source,
		menus->resources, asset_root, &menus->car_catalog, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_catalog_load_from(menus->catalog_source,
			menus->resources, asset_root, &menus->track_catalog,
			error_out);

	if (status == MH_ERROR_NONE)
		status = load_cars(asset_root, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = load_tracks(asset_root, progress_directory, menus, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	menus->details.cars = menus->cars;
	menus->details.car_count = menus->car_catalog.count;
	menus->details.tracks = menus->tracks;
	menus->details.track_count = menus->track_catalog.count;
	menus->details.stage = INITIAL_LEAGUE_STAGE;
	menus->details.laps = INITIAL_RACE_LAPS;
	menus->details.difficulty = menus->details.automatic = 1;
	menus->details.demo_name = menus->demo_name;
	menus->race_scene.names = menus->track_titles;
	menus->race_scene.thumbnails = menus->track_thumbnails;
	menus->race_scene.models = menus->track_models;
	menus->race_scene.background_palette = menus->backdrop.palette;
	status = mh_horn_catalog_load_from(menus->horn_source, menus->resources,
		asset_root, &menus->horns, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_horn_default(&menus->horns, &menus->car.horn, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < menus->horns.count; i++)
		menus->horn_titles[i] = menus->horns.horns[i].name;

	menus->car_scene.names = menus->car_titles;
	menus->car_scene.horn_names = menus->horn_titles;
	menus->car_scene.horn_count = menus->horns.count;
	menus->car_scene.models = menus->car_models;
	menus->car_scene.thumbnails = menus->car_thumbnails;
	menus->car_scene.statistics =
		(const float (*)[MH_MENU_CAR_STATISTICS])menus->car_statistics;
	menus->car_scene.catalog.divisions = menus->car_divisions;
	menus->car_scene.catalog.count = menus->car_catalog.count;
	menus->car_scene.background_palette = menus->backdrop.palette;

	return mh_menus_recordings_reload(asset_root, menus, error_out);
}

enum mh_error_code
mh_menus_profiles_load(struct mh_menus *menus, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned long previous_random_state;
	int new_catalog;

	new_catalog = menus->profiles == NULL;
	previous_random_state = menus->random_state;

	if (new_catalog)
		menus->profiles = (struct mh_profile_catalog *)calloc(
			1, sizeof(*menus->profiles));

	if (menus->profiles == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading driver profiles");

	status = mh_profile_catalog_load_from(menus->profile_source,
		menus->resources, menus->asset_root, &menus->horns,
		&menus->random_state, menus->profiles, error_out);

	if (status != MH_ERROR_NONE && new_catalog) {
		free(menus->profiles);
		menus->profiles = NULL;
	}

	if (status == MH_ERROR_NONE && menus->debug.profile_trace) {
		printf("Profile catalog: %u entries, RNG %lu -> %lu\n",
			menus->profiles->count, previous_random_state,
			menus->random_state);
		fflush(stdout);
	}

	return status;
}
