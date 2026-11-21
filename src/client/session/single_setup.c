#include <string.h>

#include "client/session/loading.h"
#include "client/session/single_setup.h"
#include "engine/assets/path_menus.h"
#include "engine/audio/horn.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "engine/game/cheats.h"
#include "engine/game/mode.h"
#include "engine/game/race_limits.h"
#include "engine/ui/attract.h"
#include "engine/ui/menu_catalog.h"

static int
valid_profile_text(const char *value, size_t size, int allow_empty)
{
	size_t i;

	for (i = 0; i < size; i++) {
		if (value[i] == 0)
			return allow_empty || i != 0;

		if ((unsigned char)value[i] < 32)
			return 0;
	}

	return 0;
}

static enum mh_error_code
bind_ai_profile(const struct mh_car_catalog *cars,
	const struct mh_horn_catalog *horns, unsigned int car_field,
	struct mh_profile *profile, unsigned int *car_number,
	struct mh_error *error_out)
{
	const char *name;
	enum mh_error_code status;
	unsigned int j, horn;

	name = profile->cars[car_field];

	if (!valid_profile_text(name, sizeof(profile->cars[0]), 0) ||
		!valid_profile_text(profile->horn, sizeof(profile->horn), 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing single-player AI car or horn");

	for (j = 0; j < cars->count; j++)
		if (mh_ascii_equal(name, cars->entries[j].name)) {
			if (*car_number != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous single-player AI car");

			*car_number = j + 1;
		}

	if (*car_number == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"single-player AI car absent from catalog");

	for (j = 0; j < 4; j++)
		if (profile->colors[j] > MH_PROFILE_RGB_MASK)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid single-player AI color");

	status = mh_horn_find(horns, profile->horn, &horn, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (strlen(horns->horns[horn].name) >= sizeof(profile->horn))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"single-player horn name too long");

	strcpy(profile->horn, horns->horns[horn].name);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
bind_human_profile(const struct mh_settings *settings,
	const struct mh_horn_catalog *horns, struct mh_profile *profile,
	struct mh_error *error_out)
{
	const char *name;
	unsigned int i;

	strcpy(profile->name, settings->player_name);
	strcpy(profile->nick, settings->player_name);
	strcpy(profile->team, settings->team_name);
	strcpy(profile->picture, MH_ASSET_PLAYER_PICTURE);
	name = horns->horns[settings->horn].name;

	if (strlen(name) >= sizeof(profile->horn))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"single-player horn name too long");

	strcpy(profile->horn, name);
	profile->colors[0] = settings->player_color;

	for (i = 0; i < 3; i++)
		profile->colors[i + 1] = settings->car_colors[i];

	profile->color_fields = MH_PROFILE_ALL_COLORS;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
valid_game_mode(unsigned int mode, int attract)
{
	if (attract)
		return mode == MH_GAME_ATTRACT;

	return mode == MH_GAME_QUICK_RACE || mode == MH_GAME_TIME_ATTACK;
}

static enum mh_error_code
resolve_catalog_selection(const struct mh_car_catalog *cars,
	unsigned int track_count, const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, const struct mh_settings *settings,
	unsigned long *random, int attract, struct mh_single_selection *output,
	struct mh_error *error_out)
{
	struct mh_single_selection next;
	struct mh_quick_roster roster;
	struct mh_profile *profile;
	enum mh_error_code status;
	unsigned int i;
	unsigned long seed;

	if (cars == NULL || horns == NULL || settings == NULL ||
		random == NULL || output == NULL || *random > MH_U32_MAX ||
		!valid_game_mode(settings->game_type, attract) ||
		settings->car >= cars->count ||
		settings->track >= track_count || settings->difficulty > 2 ||
		settings->laps < MH_RACE_MIN_LAPS ||
		settings->laps > MH_RACE_MAX_LAPS || settings->automatic > 1 ||
		settings->catch_up > 1 || settings->custom_car_colors > 1 ||
		horns->count == 0 || horns->count > MH_HORNS ||
		settings->horn >= horns->count ||
		!valid_profile_text(settings->player_name,
			sizeof(settings->player_name), 1) ||
		!valid_profile_text(
			settings->team_name, sizeof(settings->team_name), 1) ||
		settings->player_color > MH_PROFILE_RGB_MASK)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid single-player race preferences");

	for (i = 0; i < 3; i++)
		if (settings->car_colors[i] > MH_PROFILE_RGB_MASK)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid single-player car color");

	if (settings->game_type != MH_GAME_TIME_ATTACK &&
		(catalog == NULL || catalog->count < MH_QUICK_CARS ||
			catalog->count > MH_PROFILE_CATALOG_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid single-player profile catalog");

	for (i = 0; i < horns->count; i++)
		if (!valid_profile_text(horns->horns[i].name,
			sizeof(horns->horns[i].name), 0))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid single-player horn catalog");

	memset(&next, 0, sizeof(next));
	seed = *random;
	next.count =
		settings->game_type == MH_GAME_TIME_ATTACK ? 1 : MH_QUICK_CARS;

	if (settings->game_type != MH_GAME_TIME_ATTACK) {
		if (!cars->entries[settings->car].has_division)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"selected car has no Division");

		status = mh_quick_roster_prepare(catalog->profiles,
			catalog->count, cars->entries[settings->car].division,
			&seed, &roster, error_out);

		if (status == MH_ERROR_NONE && !attract)
			status = mh_quick_roster_local(&roster, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		memcpy(next.slots, roster.slots, sizeof(next.slots));
		memcpy(next.profile_sources, roster.profiles,
			sizeof(next.profile_sources));

		for (i = attract ? 0 : 1; i < next.count; i++) {
			next.profiles[i] =
				catalog->profiles[roster.profiles[i]];
			profile = &next.profiles[i];
			profile->team[0] = 0;
			status = bind_ai_profile(cars, horns, roster.car_field,
				profile, &next.cars[i], error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	next.track = settings->track + 1;

	if (attract) {
		*output = next;
		*random = seed;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	next.cars[0] = settings->car + 1;
	next.profile_sources[0] = MH_QUICK_HUMAN;
	status = bind_human_profile(
		settings, horns, &next.profiles[0], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = next;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_selection(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_profile_catalog *profiles,
	const struct mh_horn_catalog *horns, const struct mh_settings *settings,
	unsigned long *random, int attract, struct mh_single_selection *output,
	struct mh_error *error_out)
{
	struct mh_car_catalog cars;
	unsigned int track_count;
	enum mh_error_code status;

	memset(&cars, 0, sizeof(cars));
	status = mh_car_catalog_load_from(
		catalog_source, resources, asset_root, &cars, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_catalog_count_from(catalog_source, resources,
			asset_root, &track_count, error_out);

	if (status == MH_ERROR_NONE)
		status = resolve_catalog_selection(&cars, track_count, profiles,
			horns, settings, random, attract, output, error_out);

	mh_car_catalog_close(&cars);

	return status;
}

enum mh_error_code
mh_single_setup_resolve_from(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, const struct mh_settings *settings,
	unsigned long *random, struct mh_single_selection *output,
	struct mh_error *error_out)
{
	return resolve_selection(catalog_source, resources, asset_root, catalog,
		horns, settings, random, 0, output, error_out);
}

static enum mh_error_code
start_field(struct mh_race_context *context,
	const struct mh_single_selection *selection,
	const struct mh_settings *settings, unsigned int first_ai,
	struct mh_field *field, struct mh_error *error_out)
{
	struct mh_drive first;
	const struct mh_car *opponents[MH_QUICK_CARS - 1];
	unsigned int i;
	enum mh_error_code status;

	context->scene.assets.track.laps = settings->laps;
	status = mh_drive_start(&context->scene.assets.car,
		&context->scene.assets.world_collision,
		&context->scene.assets.materials, &context->scene.assets.route,
		&context->scene.assets.track, selection->slots[0],
		first_ai && !settings->automatic, 1, &first, error_out);

	if (status == MH_ERROR_NONE && !first_ai)
		status = mh_drive_enable_ai(&first,
			&context->scene.assets.ai_path,
			settings->difficulty + 1, error_out);

	if (status == MH_ERROR_NONE) {
		for (i = 1; i < selection->count; i++)
			opponents[i - 1] =
				&context->scene.assets.opponents[i - 1];

		status = mh_field_start_grid(&first, opponents,
			selection->count - 1, &context->scene.assets.ai_path,
			settings->difficulty + 1, selection->slots, field,
			error_out);
	}

	if (status == MH_ERROR_NONE)
		for (i = first_ai; i < selection->count; i++) {
			status = mh_race_scene_paint(&context->scene, i,
				selection->profiles[i].colors + 1, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_drive_ai_profile(&field->cars[i],
					(float)selection->profiles[i]
						.aggressiveness,
					(float)selection->profiles[i].eagerness,
					error_out);

			if (status != MH_ERROR_NONE)
				break;
		}

	return status;
}

static enum mh_error_code
load_race_ui(const char *asset_root, struct mh_race_context *context,
	const struct mh_single_selection *selection, unsigned int first_ai,
	const struct mh_race_load_observer *observer,
	struct mh_error *error_out)
{
	const struct mh_profile *profiles[MH_QUICK_CARS];
	unsigned int i;
	enum mh_error_code status;

	status = mh_race_context_load_hud(asset_root, context, error_out);

	/* Software rendering loads the racing FONT2/FONT2S. */
	if (status == MH_ERROR_NONE)
		status = mh_race_load_complete(
			observer, MH_RACE_LOAD_FONT, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load_map(
			asset_root, context, error_out);

	if (status == MH_ERROR_NONE) {
		for (i = 0; i < selection->count; i++)
			profiles[i] = &selection->profiles[i];

		status = mh_results_assets_roster_from(context->font_source,
			context->image_decoder, context->portrait_decoder,
			context->resources, asset_root, selection->count,
			profiles, first_ai ? 1U : 0U,
			mh_race_context_atlas(context), &context->results,
			error_out);
	}

	return status;
}

static enum mh_error_code
load_selection(const struct mh_horn_source *horn_source,
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
	const struct mh_single_selection *selection,
	struct mh_settings *settings, int scenery, unsigned long seed,
	const struct mh_race_load_observer *observer,
	const unsigned long *texture_budget, struct mh_race_context *context,
	struct mh_field *field, struct mh_error *error_out)
{
	struct mh_single_selection chosen;
	struct mh_field next;
	enum mh_error_code status;
	unsigned int i, first_ai;

	chosen = *selection;
	first_ai = settings->game_type == MH_GAME_ATTRACT ? 0 : 1;
	status = mh_race_context_load_observed_from(horn_source,
		material_source, env_sound_source, catalog_source,
		navigation_source, animation_source, atlas_source,
		model_decoder, world_decoder, collision_source, sprites, fonts,
		image_source, image_decoder, portrait_decoder, resources,
		asset_root, chosen.cars[0], chosen.track, scenery, settings,
		observer, texture_budget, context, error_out);

	if (status == MH_ERROR_NONE && chosen.count > 1)
		status = mh_race_scene_add_selected_from(context->resources,
			asset_root, chosen.cars + 1, chosen.track,
			chosen.count - 1, &context->scene, error_out);

	/*
	 * Publish loading progress after the complete selected-car loader, not
	 * each car.
	 */
	if (status == MH_ERROR_NONE)
		status = mh_race_load_complete(
			observer, MH_RACE_LOAD_CARS, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_load_ai_from(context->resources,
			asset_root, &context->scene, error_out);

	/*
	 * Publish loading progress even when the original mode skips AI-path
	 * loading.
	 */
	if (status == MH_ERROR_NONE)
		status = mh_race_load_complete(
			observer, MH_RACE_LOAD_AI, error_out);

	if (status == MH_ERROR_NONE && first_ai && settings->custom_car_colors)
		status = mh_race_scene_paint(
			&context->scene, 0, settings->car_colors, error_out);

	if (status == MH_ERROR_NONE)
		status = start_field(
			context, &chosen, settings, first_ai, &next, error_out);

	if (status == MH_ERROR_NONE &&
		settings->game_type == MH_GAME_TIME_ATTACK)
		status = mh_field_enable_time_attack(&next, error_out);

	if (status == MH_ERROR_NONE && settings->game_type == MH_GAME_ATTRACT)
		status = mh_field_enable_attract(&next, error_out);

	if (status == MH_ERROR_NONE &&
		settings->game_type == MH_GAME_QUICK_RACE && settings->catch_up)
		status = mh_field_enable_catchup(
			&next, settings->difficulty + 1, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_cheats(&next,
			mh_cheat_flags(
				settings->player_name, settings->team_name),
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_set_hulls(&next,
			context->scene.assets.car_hulls, next.count, error_out);

	if (status == MH_ERROR_NONE)
		mh_race_scene_bind_cameras(&context->scene, &next);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(
			&context->scene, &next, error_out);

	if (status == MH_ERROR_NONE)
		status = load_race_ui(asset_root, context, &chosen, first_ai,
			observer, error_out);

	if (status != MH_ERROR_NONE) {
		mh_race_context_free(context);
		return status;
	}

	for (i = 0; i < next.count; i++)
		next.cars[i].race.split_hold_ms = settings->av.checkpoint_delay;

	next.ai_random = context->scene.visuals.particle_system.random = seed;
	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_single_setup_load_from(const struct mh_horn_source *horn_source,
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
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	int scenery, unsigned long *random, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_error *error_out)
{
	return mh_single_setup_load_observed_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, sprites, fonts, image_source, image_decoder,
		portrait_decoder, resources, asset_root, catalog, horns,
		settings, scenery, random, NULL, NULL, context, field,
		selection, error_out);
}

enum mh_error_code
mh_single_setup_load_observed_from(const struct mh_horn_source *horn_source,
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
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	int scenery, unsigned long *random,
	const struct mh_race_load_observer *observer,
	const unsigned long *texture_budget, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_error *error_out)
{
	struct mh_single_selection chosen;
	unsigned long seed;
	enum mh_error_code status;

	if (resources == NULL || context == NULL || field == NULL ||
		selection == NULL || random == NULL || settings == NULL ||
		(scenery != 0 && scenery != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid single-player race outputs");

	memset(context, 0, sizeof(*context));
	seed = *random;
	status = mh_single_setup_resolve_from(catalog_source, resources,
		asset_root, catalog, horns, settings, &seed, &chosen,
		error_out);

	if (status == MH_ERROR_NONE)
		status = load_selection(horn_source, material_source,
			env_sound_source, catalog_source, navigation_source,
			animation_source, atlas_source, model_decoder,
			world_decoder, collision_source, sprites, fonts,
			image_source, image_decoder, portrait_decoder,
			resources, asset_root, &chosen, settings, scenery, seed,
			observer, texture_budget, context, field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*selection = chosen;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_attract_setup_load_from(const struct mh_horn_source *horn_source,
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
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns,
	const struct mh_settings *saved_settings, char previous_track[16],
	int scenery, unsigned long *random, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_settings *race_settings, struct mh_error *error_out)
{
	return mh_attract_setup_load_presented_from(horn_source,
		material_source, env_sound_source, catalog_source,
		navigation_source, animation_source, atlas_source,
		model_decoder, world_decoder, collision_source, sprites, fonts,
		image_source, image_decoder, portrait_decoder, resources,
		asset_root, catalog, horns, saved_settings, previous_track,
		scenery, random, context, field, selection, race_settings, NULL,
		NULL, error_out);
}

static enum mh_error_code
select_attract_track(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const char previous_track[16], unsigned long *random,
	unsigned int *track_index, struct mh_attract_course *course,
	struct mh_error *error_out)
{
	struct mh_attract_course courses[MH_TRACK_CATALOG_LIMIT],
		sorted_courses[MH_TRACK_CATALOG_LIMIT];
	struct mh_track track;
	unsigned long divisions[MH_TRACK_CATALOG_LIMIT];
	unsigned int i, order[MH_TRACK_CATALOG_LIMIT], selected, track_count;
	enum mh_error_code status;

	memset(courses, 0, sizeof(courses));
	status = mh_track_catalog_count_from(
		catalog_source, resources, asset_root, &track_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < track_count; i++) {
		status = mh_track_config_load_from(catalog_source, resources,
			asset_root, i + 1, &track, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!track.has_division)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid attract track metadata");

		courses[i].division = divisions[i] = track.division;

		/*
		 * Keep rejected draws in the catalog, without copying a name
		 * that attract mode can never select into its original field.
		 */
		if (track.division >= MH_TRACK_CUSTOM_DIVISION)
			continue;

		if (strlen(track.name) >= sizeof(courses[i].name))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid attract track metadata");

		strcpy(courses[i].name, track.name);
	}

	status = mh_menu_catalog_order(divisions, track_count, order,
		MH_TRACK_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < track_count; i++)
		sorted_courses[i] = courses[order[i]];

	status = mh_attract_course_select(sorted_courses, track_count,
		previous_track, random, &selected, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*track_index = order[selected];
	*course = sorted_courses[selected];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_attract_setup_load_presented_from(const struct mh_horn_source *horn_source,
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
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns,
	const struct mh_settings *saved_settings, char previous_track[16],
	int scenery, unsigned long *random, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_settings *race_settings, struct mh_session_loading *loading,
	const unsigned long *texture_budget, struct mh_error *error_out)
{
	struct mh_attract_course course;
	struct mh_single_selection chosen;
	struct mh_settings settings;
	unsigned long seed;
	enum mh_error_code status;

	if (resources == NULL || asset_root == NULL || context == NULL ||
		field == NULL || selection == NULL || random == NULL ||
		saved_settings == NULL || race_settings == NULL ||
		race_settings == saved_settings || previous_track == NULL ||
		(scenery != 0 && scenery != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid attract race outputs");

	memset(context, 0, sizeof(*context));
	seed = *random;
	status = mh_attract_settings(
		saved_settings, saved_settings->track, &settings, error_out);

	if (status == MH_ERROR_NONE)
		status = resolve_selection(catalog_source, resources,
			asset_root, catalog, horns, &settings, &seed, 1,
			&chosen, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&course, 0, sizeof(course));
	status = select_attract_track(catalog_source, resources, asset_root,
		previous_track, &seed, &settings.track, &course, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	chosen.track = settings.track + 1;
	status = mh_session_loading_begin(
		loading, chosen.track, settings.laps, &settings, error_out);

	if (status == MH_ERROR_NONE)
		status = load_selection(horn_source, material_source,
			env_sound_source, catalog_source, navigation_source,
			animation_source, atlas_source, model_decoder,
			world_decoder, collision_source, sprites, fonts,
			image_source, image_decoder, portrait_decoder,
			resources, asset_root, &chosen, &settings, scenery,
			seed, mh_session_loading_observer(loading),
			texture_budget, context, field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*race_settings = settings;
	context->settings = &race_settings->av;
	memcpy(previous_track, course.name, sizeof(course.name));
	*selection = chosen;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
