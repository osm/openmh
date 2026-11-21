#include <string.h>

#include "client/session/league_setup.h"
#include "client/session/loading.h"
#include "client/settings/render_choice.h"
#include "engine/audio/horn.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/ui/menu_catalog.h"
#include "interfaces/audio/horn_source.h"

#define DIAGNOSTIC_GRID_SLOTS 64

static enum mh_error_code
validate_roster(const struct mh_league_document *document,
	const struct mh_league_roster *roster, struct mh_error *error_out)
{
	const struct mh_profile *profile;
	const char *name;
	unsigned int i;

	for (i = 0; i < roster->count; i++) {
		if (roster->drivers[i] >= MH_LEAGUE_ENTRIES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league profile identity");

		if (i == 0)
			continue;

		profile = &document->profiles[roster->drivers[i]];
		name = profile->cars[roster->stage];

		if (name[0] == 0 ||
			memchr(name, 0, sizeof(profile->cars[0])) == NULL ||
			profile->horn[0] == 0 ||
			memchr(profile->horn, 0, sizeof(profile->horn)) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing league vehicle or horn name");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_track(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_league *league, unsigned int track_count,
	struct mh_league_asset_selection *selection, struct mh_error *error_out)
{
	struct mh_track track;
	unsigned long divisions[MH_TRACK_CATALOG_LIMIT] = {0};
	unsigned long sorted_divisions[MH_TRACK_CATALOG_LIMIT];
	unsigned int i, order[MH_TRACK_CATALOG_LIMIT];
	enum mh_error_code status;

	for (i = 0; i < track_count; i++) {
		status = mh_track_config_load_from(catalog_source, resources,
			asset_root, i + 1, &track, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!track.has_division)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"installed track has no Division code");

		divisions[i] = track.division;
	}

	status = mh_menu_catalog_order(divisions, track_count, order,
		MH_TRACK_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < track_count; i++)
		sorted_divisions[i] = divisions[order[i]];

	status = mh_league_schedule(league, sorted_divisions, track_count,
		&selection->schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (selection->schedule.complete)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"league schedule is complete");

	selection->track = order[selection->schedule.track_index] + 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_vehicles(const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_league_document *document, unsigned int car_count,
	unsigned int human_car_number, const char *human_horn,
	struct mh_league_asset_selection *selection, struct mh_error *error_out)
{
	struct mh_car car;
	struct mh_horn_catalog horns;
	const char *name;
	unsigned int i, j, horn;
	enum mh_error_code status;

	selection->cars[0] = human_car_number;

	for (i = 1; i <= car_count; i++) {
		status = mh_car_config_load_from(catalog_source, resources,
			asset_root, i, &car, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (j = 1; j < selection->roster.count; j++) {
			name = document->profiles[selection->roster.drivers[j]]
				.cars[selection->roster.stage];

			if (!mh_ascii_equal(name, car.name))
				continue;

			if (selection->cars[j] != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous league car name");

			selection->cars[j] = i;
		}
	}

	status = mh_horn_catalog_load_from(
		horn_source, resources, asset_root, &horns, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < selection->roster.count; i++) {
		if (selection->cars[i] == 0)
			return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
				"league car absent from installed catalog");

		name = i == 0
			? human_horn
			: document->profiles[selection->roster.drivers[i]].horn;
		status = mh_horn_find(&horns, name, &horn, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (strlen(horns.horns[horn].name) >=
			sizeof(selection->horns[i]))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"league horn name too long");

		strcpy(selection->horns[i], horns.horns[horn].name);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_setup_resolve_from(const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_league_document *document,
	unsigned int human_car_number, const char *human_horn,
	struct mh_league_asset_selection *output, struct mh_error *error_out)
{
	struct mh_league_asset_selection next;
	unsigned int car_count, track_count;
	enum mh_error_code status;

	if (resources == NULL || asset_root == NULL || document == NULL ||
		output == NULL || human_horn == NULL || human_car_number < 1 ||
		human_car_number > MH_CAR_CATALOG_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league asset selection");

	status = mh_car_catalog_count_from(
		catalog_source, resources, asset_root, &car_count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_catalog_count_from(catalog_source, resources,
			asset_root, &track_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (human_car_number > car_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"selected league car is absent");

	memset(&next, 0, sizeof(next));
	status = mh_league_roster(&document->league, &next.roster, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = validate_roster(document, &next.roster, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = resolve_track(catalog_source, resources, asset_root,
		&document->league, track_count, &next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = resolve_vehicles(horn_source, catalog_source, resources,
		asset_root, document, car_count, human_car_number, human_horn,
		&next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_setup_load_settings_from(const struct mh_horn_source *horn_source,
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
	const struct mh_league_document *document,
	const struct mh_league_drive_options *options,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_field *field, struct mh_league_asset_selection *selection,
	struct mh_error *error_out)
{
	return mh_league_setup_load_presented_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, sprites, fonts, image_source, image_decoder,
		portrait_decoder, resources, asset_root, document, options,
		settings, context, field, selection, NULL, NULL, error_out);
}

static void
select_grid(const struct mh_league_roster *roster,
	const struct mh_league_drive_options *options,
	unsigned int slots[MH_LEAGUE_DIVISION_SIZE])
{
	unsigned int i, slot;

	memcpy(slots, roster->slots, sizeof(roster->slots));

	if (options->override_grid) {
		slots[0] = options->slot;
		slot = 0;

		for (i = 1; i < roster->count; i++) {
			if (slot == options->slot)
				slot++;

			slots[i] = slot++;
		}
	}
}

static enum mh_error_code
start_field(struct mh_race_context *context,
	const struct mh_league_document *document,
	const struct mh_league_asset_selection *selection,
	const struct mh_league_drive_options *options,
	const unsigned int slots[MH_LEAGUE_DIVISION_SIZE],
	struct mh_field *field, struct mh_error *error_out)
{
	struct mh_drive first;
	const struct mh_car *opponents[MH_LEAGUE_DIVISION_SIZE - 1];
	unsigned int i;
	enum mh_error_code status;

	status = mh_drive_start(&context->scene.assets.car,
		&context->scene.assets.world_collision,
		&context->scene.assets.materials, &context->scene.assets.route,
		&context->scene.assets.track, slots[0], options->manual, 1,
		&first, error_out);

	if (status == MH_ERROR_NONE) {
		for (i = 1; i < selection->roster.count; i++)
			opponents[i - 1] =
				&context->scene.assets.opponents[i - 1];

		status = mh_field_start_grid(&first, opponents,
			selection->roster.count - 1,
			&context->scene.assets.ai_path, options->difficulty,
			slots, field, error_out);
	}

	if (status == MH_ERROR_NONE)
		for (i = 1; i < selection->roster.count; i++) {
			const struct mh_profile *profile;

			profile = &document->profiles[selection->roster
					.drivers[i]];
			status = mh_race_scene_paint(&context->scene, i,
				profile->colors + 1, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_drive_ai_profile(&field->cars[i],
					(float)profile->aggressiveness,
					(float)profile->eagerness, error_out);

			if (status != MH_ERROR_NONE)
				break;
		}

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(field,
			context->scene.assets.car_hulls, field->count,
			error_out);

	if (status == MH_ERROR_NONE)
		mh_race_scene_bind_cameras(&context->scene, field);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(
			&context->scene, field, error_out);

	return status;
}

enum mh_error_code
mh_league_setup_load_presented_from(const struct mh_horn_source *horn_source,
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
	const struct mh_league_document *document,
	const struct mh_league_drive_options *options,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_field *field, struct mh_league_asset_selection *selection,
	struct mh_session_loading *loading, const unsigned long *texture_budget,
	struct mh_error *error_out)
{
	struct mh_league_asset_selection chosen;
	struct mh_field next;
	unsigned int i, slots[MH_LEAGUE_DIVISION_SIZE];
	enum mh_error_code status;

	if (context == NULL || field == NULL || selection == NULL ||
		options == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing league driving outputs");

	memset(context, 0, sizeof(*context));

	if (options->difficulty < 1 || options->difficulty > 3 ||
		(options->override_grid != 0 && options->override_grid != 1) ||
		(options->override_grid &&
			options->slot >= DIAGNOSTIC_GRID_SLOTS) ||
		(options->manual != 0 && options->manual != 1) ||
		(options->scenery != 0 && options->scenery != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league driving options");

	status = mh_league_setup_resolve_from(horn_source, catalog_source,
		resources, asset_root, document, options->car, options->horn,
		&chosen, error_out);

	if (status == MH_ERROR_NONE)
		select_grid(&chosen.roster, options, slots);

	if (status == MH_ERROR_NONE && loading != NULL) {
		struct mh_track track;

		status = mh_track_config_load_from(catalog_source, resources,
			asset_root, chosen.track, &track, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_session_loading_begin(loading, chosen.track,
				track.laps, settings, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load_observed_from(horn_source,
			material_source, env_sound_source, catalog_source,
			navigation_source, animation_source, atlas_source,
			model_decoder, world_decoder, collision_source, sprites,
			fonts, image_source, image_decoder, portrait_decoder,
			resources, asset_root, chosen.cars[0], chosen.track,
			options->scenery, settings,
			mh_session_loading_observer(loading), texture_budget,
			context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_add_selected_from(context->resources,
			asset_root, chosen.cars + 1, chosen.track,
			chosen.roster.count - 1, &context->scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_CARS, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_ai_from(context->resources,
			asset_root, &context->scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_AI, error_out);

	if (status == MH_ERROR_NONE && settings != NULL &&
		settings->custom_car_colors)
		status = mh_race_scene_paint(
			&context->scene, 0, settings->car_colors, error_out);

	if (status == MH_ERROR_NONE)
		status = start_field(context, document, &chosen, options, slots,
			&next, error_out);

	if (status == MH_ERROR_NONE && settings != NULL)
		for (i = 0; i < next.count; i++)
			next.cars[i].race.split_hold_ms =
				settings->av.checkpoint_delay;

	if (status != MH_ERROR_NONE) {
		mh_race_context_free(context);
		return status;
	}

	*field = next;
	*selection = chosen;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_setup_ui_settings(const char *asset_root,
	const struct mh_league_document *document,
	const struct mh_league_asset_selection *selection,
	const struct mh_settings *settings, struct mh_race_context *context,
	struct mh_error *error_out)
{
	return mh_league_setup_ui_presented(asset_root, document, selection,
		settings, context, NULL, error_out);
}

enum mh_error_code
mh_league_setup_ui_presented(const char *asset_root,
	const struct mh_league_document *document,
	const struct mh_league_asset_selection *selection,
	const struct mh_settings *settings, struct mh_race_context *context,
	struct mh_session_loading *loading, struct mh_error *error_out)
{
	const struct mh_profile *profiles[MH_LEAGUE_DIVISION_SIZE];
	struct mh_settings defaults;
	enum mh_error_code status;
	unsigned int i;

	if (document == NULL || selection == NULL || context == NULL ||
		selection->roster.count == 0 ||
		selection->roster.count > MH_LEAGUE_DIVISION_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league UI roster");

	if (settings == NULL) {
		status = mh_settings_init(&defaults, "Player", 0, 0,
			mh_render_choice_default(), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		settings = &defaults;
	}

	if (memchr(settings->team_name, 0, sizeof(settings->team_name)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league human team");

	for (i = 0; i < 3; i++)
		if (settings->car_colors[i] > MH_PROFILE_RGB_MASK)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league human paint");

	for (i = 0; i < selection->roster.count; i++) {
		if (selection->roster.drivers[i] >= MH_LEAGUE_ENTRIES ||
			memchr(selection->horns[i], 0,
				sizeof(selection->horns[i])) == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid league UI identity");

		profiles[i] = &document->profiles[selection->roster.drivers[i]];
	}

	status = mh_race_context_load_hud(asset_root, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_FONT, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load_map(
			asset_root, context, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_results_assets_profiles_from(context->font_source,
			context->image_decoder, context->portrait_decoder,
			context->resources, asset_root, selection->roster.count,
			profiles, mh_race_context_atlas(context),
			&context->results, error_out);

	if (status == MH_ERROR_NONE) {
		for (i = 0; i < selection->roster.count; i++)
			strcpy(context->results.profiles[i].horn,
				selection->horns[i]);

		strcpy(context->results.profiles[0].team, settings->team_name);

		for (i = 0; i < 3; i++)
			context->results.profiles[0].colors[i + 1] =
				settings->car_colors[i];

		context->results.profiles[0].color_fields |=
			MH_PROFILE_BODY_COLORS;

		for (i = 1; i < selection->roster.count; i++)
			strcpy(context->results.profiles[i].team, "CPU");
	}

	return status;
}
