#include <string.h>

#include "client/session/demo_setup.h"
#include "client/session/loading.h"
#include "engine/audio/race_playback.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/game/race_limits.h"

#define RECORDED_NAME_SIZE 17

static int
recorded_name_equal(const char *recorded, const char *installed)
{
	size_t length;

	/* Asset names must be nonempty and terminated within the field. */
	for (length = 0; length < RECORDED_NAME_SIZE && recorded[length] != 0;
		length++) {
	}

	return length != 0 && length < RECORDED_NAME_SIZE &&
		mh_ascii_equal_n(recorded, installed, length + 1);
}

static enum mh_error_code
resolve_track(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const char *recorded_name, unsigned int *track_number,
	struct mh_error *error_out)
{
	struct mh_track track;
	enum mh_error_code status;
	unsigned int i, track_count;

	status = mh_track_catalog_count_from(
		catalog_source, resources, asset_root, &track_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 1; i <= track_count; i++) {
		status = mh_track_config_load_from(catalog_source, resources,
			asset_root, i, &track, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!recorded_name_equal(recorded_name, track.name))
			continue;

		if (*track_number != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous recorded track name");

		*track_number = i;
	}

	if (*track_number == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"recorded track is absent from the installed catalog");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_cars(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_demo *demo, const struct mh_demo_layout *layout,
	unsigned int *cars, struct mh_error *error_out)
{
	struct mh_car car;
	enum mh_error_code status;
	unsigned int i, j, slot, car_count;

	status = mh_car_catalog_count_from(
		catalog_source, resources, asset_root, &car_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 1; i <= car_count; i++) {
		status = mh_car_config_load_from(catalog_source, resources,
			asset_root, i, &car, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (j = 0; j < layout->count; j++) {
			slot = layout->recording[j];

			if (slot == MH_DEMO_HUMAN_SLOT ||
				!recorded_name_equal(
					demo->players[slot].car, car.name))
				continue;

			if (cars[j] != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous recorded car name");

			cars[j] = i;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_setup_load_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_sprite_source_provider *sprites,
	const struct mh_font_source_provider *fonts,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_demo *demo, int ghost, unsigned int local_car_number,
	int manual, int scenery, struct mh_race_context *context,
	struct mh_demo_race *race, struct mh_error *error_out)
{
	return mh_demo_setup_load_settings_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, sprites, fonts, image_source, image_decoder,
		portrait_decoder, resources, asset_root, demo, ghost,
		local_car_number, manual, scenery, NULL, context, race,
		error_out);
}

enum mh_error_code
mh_demo_setup_load_settings_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_sprite_source_provider *sprites,
	const struct mh_font_source_provider *fonts,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_demo *demo, int ghost, unsigned int local_car_number,
	int manual, int scenery, struct mh_settings *settings,
	struct mh_race_context *context, struct mh_demo_race *race,
	struct mh_error *error_out)
{
	return mh_demo_setup_load_presented_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, sprites, fonts, image_source, image_decoder,
		portrait_decoder, resources, asset_root, demo, ghost,
		local_car_number, manual, scenery, settings, context, race,
		NULL, NULL, error_out);
}

static enum mh_error_code
paint_entries(struct mh_race_context *context, const struct mh_demo *demo,
	const struct mh_demo_layout *layout, const struct mh_settings *settings,
	struct mh_error *error_out)
{
	unsigned long colors[3];
	unsigned int i, j, slot;
	enum mh_error_code status;

	for (i = 0; i < layout->count; i++) {
		const unsigned long *body_colors;

		if (i == 0 &&
			(settings == NULL || !settings->custom_car_colors))
			continue;

		slot = layout->recording[i];

		body_colors = slot == MH_DEMO_HUMAN_SLOT
			? settings->car_colors
			: demo->players[slot].colors + 1;

		for (j = 0; j < 3; j++)
			colors[j] = body_colors[j] & MH_PROFILE_RGB_MASK;

		status = mh_race_scene_paint(
			&context->scene, i, colors, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
start_race(struct mh_race_context *context, const struct mh_demo *demo,
	const struct mh_demo_layout *layout, int ghost, int manual,
	struct mh_demo_race *race, struct mh_error *error_out)
{
	struct mh_drive entries[MH_RACE_MAX_CARS];
	const struct mh_car *config;
	unsigned int i;
	enum mh_error_code status;

	context->scene.assets.track.laps = (unsigned int)demo->laps;

	for (i = 0; i < layout->count; i++) {
		config = i == 0 ? &context->scene.assets.car
				: &context->scene.assets.opponents[i - 1];
		status = mh_drive_start(config,
			&context->scene.assets.world_collision,
			&context->scene.assets.materials,
			&context->scene.assets.route,
			&context->scene.assets.track, 0,
			ghost && i == 0 && manual, 1, &entries[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_demo_race_begin(demo, ghost, entries, layout->count,
		context->scene.assets.car_hulls, race, error_out);
}

enum mh_error_code
mh_demo_setup_load_presented_from(const struct mh_horn_source *horn_source,
	const struct mh_material_source *material_source,
	const struct mh_env_sound_source *env_sound_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_navigation_source *navigation_source,
	const struct mh_animation_source *animation_source,
	const struct mh_atlas_source *atlas_source,
	const struct mh_model_decoder *model_decoder,
	const struct mh_world_decoder *world_decoder,
	const struct mh_collision_source *collision_source,
	const struct mh_sprite_source_provider *sprites,
	const struct mh_font_source_provider *fonts,
	const struct mh_image_source_provider *image_source,
	const struct mh_image_decoder_provider *image_decoder,
	const struct mh_image_decoder_provider *portrait_decoder,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_demo *demo, int ghost, unsigned int local_car_number,
	int manual, int scenery, struct mh_settings *settings,
	struct mh_race_context *context, struct mh_demo_race *race,
	struct mh_session_loading *loading, const unsigned long *texture_budget,
	struct mh_error *error_out)
{
	struct mh_demo_layout layout;
	unsigned int track_number, cars[MH_RACE_MAX_CARS], i;
	enum mh_error_code status;

	if (resources == NULL || asset_root == NULL || demo == NULL ||
		context == NULL || race == NULL || local_car_number < 1 ||
		local_car_number > MH_CAR_CATALOG_LIMIT ||
		demo->laps < MH_RACE_MIN_LAPS ||
		demo->laps > MH_RACE_MAX_LAPS || (manual != 0 && manual != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo asset request");

	memset(context, 0, sizeof(*context));
	memset(cars, 0, sizeof(cars));
	track_number = 0;
	status = mh_demo_race_layout(demo, ghost, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = resolve_track(catalog_source, resources, asset_root,
		demo->track, &track_number, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = resolve_cars(catalog_source, resources, asset_root, demo,
		&layout, cars, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (ghost)
		cars[0] = local_car_number;

	for (i = 0; i < layout.count; i++)
		if (cars[i] == 0)
			return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
				"recorded car is absent from the installed catalog");

	status = mh_session_loading_begin(loading, track_number,
		(unsigned int)demo->laps, settings, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_race_context_load_observed_from(horn_source,
		material_source, env_sound_source, catalog_source,
		navigation_source, animation_source, atlas_source,
		model_decoder, world_decoder, collision_source, sprites, fonts,
		image_source, image_decoder, portrait_decoder, resources,
		asset_root, cars[0], track_number, scenery, settings,
		mh_session_loading_observer(loading), texture_budget, context,
		error_out);

	if (status == MH_ERROR_NONE && layout.count > 1)
		status = mh_race_scene_add_selected_from(context->resources,
			asset_root, cars + 1, track_number, layout.count - 1,
			&context->scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_CARS, error_out);

	/*
	 * Recorded cars do not load AI paths; the original still marks this
	 * row.
	 */
	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_AI, error_out);

	if (status == MH_ERROR_NONE)
		status = paint_entries(
			context, demo, &layout, settings, error_out);

	if (status == MH_ERROR_NONE)
		status = start_race(
			context, demo, &layout, ghost, manual, race, error_out);

	if (status == MH_ERROR_NONE)
		mh_race_scene_bind_cameras(&context->scene, &race->field);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(
			&context->scene, &race->field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_frame_update(&race->field, 0, error_out);

	if (status == MH_ERROR_NONE && settings != NULL)
		for (i = 0; i < layout.count; i++)
			race->field.cars[i].race.split_hold_ms =
				settings->av.checkpoint_delay;

	if (status != MH_ERROR_NONE)
		mh_race_context_free(context);

	return status;
}

static void
copy_display_name(char *destination, size_t capacity, const char *source)
{
	size_t i;
	unsigned char c;

	for (i = 0; i + 1 < capacity && source[i] != 0; i++) {
		c = (unsigned char)source[i];
		destination[i] = c >= 32 ? (char)c : '?';
	}

	destination[i] = 0;
}

static void
apply_recorded_identity(struct mh_race_context *context, unsigned int entry,
	const struct mh_demo_player *player)
{
	struct mh_profile *profile;
	unsigned int j;

	profile = &context->results.profiles[entry];
	context->info_metadata_mask |= 1U << entry;
	context->info_metadata[entry].automatic = player->gear_mode;
	context->info_metadata[entry].control_type = player->control_type;
	memcpy(context->info_metadata[entry].name, player->name,
		sizeof(player->name) - 1);
	context->info_metadata[entry].name[sizeof(player->name) - 1] = 0;
	memcpy(context->info_metadata[entry].team, player->team,
		sizeof(player->team) - 1);
	context->info_metadata[entry].team[sizeof(player->team) - 1] = 0;

	/*
	 * Portrait lookup may use a fallback; the Info identity
	 * remains the recording's exact nickname, including an
	 * empty string.
	 */
	memcpy(profile->nick, player->name, sizeof(profile->nick) - 1);
	profile->nick[sizeof(profile->nick) - 1] = 0;
	copy_display_name(profile->team, sizeof(profile->team), player->team);

	for (j = 0; j < 4; j++)
		profile->colors[j] = player->colors[j] & MH_PROFILE_RGB_MASK;

	profile->color_fields = MH_PROFILE_ALL_COLORS;
}

enum mh_error_code
mh_demo_setup_ui(const char *asset_root, const char *human_name,
	const struct mh_demo_race *race, struct mh_race_context *context,
	struct mh_error *error_out)
{
	return mh_demo_setup_ui_presented(
		asset_root, human_name, race, context, NULL, error_out);
}

enum mh_error_code
mh_demo_setup_ui_presented(const char *asset_root, const char *human_name,
	const struct mh_demo_race *race, struct mh_race_context *context,
	struct mh_session_loading *loading, struct mh_error *error_out)
{
	char names[MH_RACE_MAX_CARS][16];
	const char *display_names[MH_RACE_MAX_CARS];
	const char *source;
	enum mh_error_code status;
	const struct mh_demo_player *player;
	unsigned int i, slot;

	if (race == NULL || context == NULL || human_name == NULL ||
		race->layout.count < 1 || race->layout.count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded UI roster");

	for (i = 0; i < race->layout.count; i++) {
		slot = race->layout.recording[i];
		source = slot == MH_DEMO_HUMAN_SLOT
			? human_name
			: race->playback.demo->players[slot].name;
		copy_display_name(names[i], sizeof(names[i]), source);
		display_names[i] = names[i];

		if (names[i][0] == 0)
			strcpy(names[i], "Driver");
	}

	status = mh_race_context_load_hud(asset_root, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_FONT, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_players_from(context->font_source,
			context->image_decoder, context->portrait_decoder,
			context->resources, asset_root, race->layout.count,
			display_names, mh_race_context_atlas(context),
			&context->results, error_out);

	if (status == MH_ERROR_NONE)
		for (i = 0; i < race->layout.count; i++) {
			slot = race->layout.recording[i];

			if (slot == MH_DEMO_HUMAN_SLOT)
				continue;

			player = &race->playback.demo->players[slot];
			apply_recorded_identity(context, i, player);
		}

	return status;
}

void
mh_demo_setup_audio(
	const struct mh_demo_race *race, struct mh_audio_options *options)
{
	const struct mh_demo *demo;
	unsigned int i, slot;

	demo = race->playback.demo;

	if (demo->version == MH_DEMO_VERSION_25)
		return;

	for (i = 0; i < race->layout.count; i++) {
		slot = race->layout.recording[i];

		if (slot != MH_DEMO_HUMAN_SLOT &&
			demo->players[slot].horn[0] != 0)
			options->horns[i] = demo->players[slot].horn;
	}
}
