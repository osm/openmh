#include <string.h>

#include "client/session/loading.h"
#include "client/session/network_setup.h"
#include "engine/audio/horn.h"
#include "engine/catalog/load.h"
#include "engine/core/error.h"
#include "engine/core/text.h"
#include "engine/ui/menu_catalog.h"
#include "interfaces/audio/horn_source.h"

#define WIRE_NAME_SIZE 16
#define BODY_COLORS 3
#define PROFILE_COLORS 4
#define ASCII_FIRST 32
#define ASCII_LAST 126

enum mh_error_code
mh_network_setup_bind_cars_from(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_car car;
	char names[MH_CAR_CATALOG_LIMIT][WIRE_NAME_SIZE];
	const char *sorted_names[MH_CAR_CATALOG_LIMIT];
	enum mh_error_code status;
	unsigned long divisions[MH_CAR_CATALOG_LIMIT];
	unsigned int order[MH_CAR_CATALOG_LIMIT], i, car_count;

	if (resources == NULL || asset_root == NULL || session == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing host car catalog arguments");

	status = mh_car_catalog_count_from(
		catalog_source, resources, asset_root, &car_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < car_count; i++) {
		status = mh_car_config_load_from(catalog_source, resources,
			asset_root, i + 1, &car, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!car.has_division || strlen(car.name) > WIRE_NAME_SIZE - 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid host car catalog metadata");

		strcpy(names[i], car.name);
		divisions[i] = car.division;
	}

	status = mh_menu_catalog_order(
		divisions, car_count, order, MH_CAR_CATALOG_LIMIT, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < car_count; i++)
		sorted_names[i] = names[order[i]];

	return mh_net_session_cars(session, sorted_names, car_count, error_out);
}

static int
wire_name_equal(const char *wire_name, const char *name)
{
	size_t length;

	/* Asset names must be nonempty and terminated within the field. */
	for (length = 0; length < WIRE_NAME_SIZE && wire_name[length] != 0;
		length++) {
	}

	return length != 0 && length < WIRE_NAME_SIZE &&
		mh_ascii_equal_n(wire_name, name, length + 1);
}

static enum mh_error_code
resolve_track(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const char *wire_name, unsigned int track_count, unsigned int *number,
	struct mh_error *error_out)
{
	struct mh_track track;
	unsigned int i;
	enum mh_error_code status;

	for (i = 1; i <= track_count; i++) {
		status = mh_track_config_load_from(catalog_source, resources,
			asset_root, i, &track, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!wire_name_equal(wire_name, track.name))
			continue;

		if (*number != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"ambiguous network track name");

		*number = i;
	}

	if (*number == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"network TrackName is absent from the installed catalog");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_cars(const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_net_session *session,
	const struct mh_net_race_layout *layout, unsigned int car_count,
	unsigned int *cars, struct mh_error *error_out)
{
	struct mh_car car;
	unsigned int i, j, player;
	enum mh_error_code status;

	for (i = 1; i <= car_count; i++) {
		status = mh_car_config_load_from(catalog_source, resources,
			asset_root, i, &car, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		for (j = 0; j < layout->count; j++) {
			player = layout->player[j];

			if (!wire_name_equal(
				session->roster.players[player].car,
				car.name))
				continue;

			if (cars[j] != 0)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"ambiguous network car name");

			cars[j] = i;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_setup_resolve_from(const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_net_session *session,
	struct mh_net_asset_selection *output, struct mh_error *error_out)
{
	struct mh_net_asset_selection selection;
	struct mh_net_race_layout layout;
	struct mh_horn_catalog horns;
	unsigned int i, horn, car_count, track_count;
	enum mh_error_code status;

	if (resources == NULL || asset_root == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing network asset catalog");

	status = mh_net_race_layout(session, &layout, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_car_catalog_count_from(
		catalog_source, resources, asset_root, &car_count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_track_catalog_count_from(catalog_source, resources,
			asset_root, &track_count, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&selection, 0, sizeof(selection));

	status = resolve_track(catalog_source, resources, asset_root,
		session->roster.track, track_count, &selection.track,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_horn_catalog_load_from(
		horn_source, resources, asset_root, &horns, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = resolve_cars(catalog_source, resources, asset_root, session,
		&layout, car_count, selection.cars, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (car_count == 0)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"empty network car catalog");

	for (i = 0; i < layout.count; i++) {
		if (selection.cars[i] == 0)
			selection.cars[i] = 1;

		status = mh_horn_find(&horns,
			session->roster.players[layout.player[i]].horn, &horn,
			error_out);

		if (status == MH_ERROR_NOT_FOUND)
			status = mh_horn_default(&horns, &horn, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		strcpy(selection.horns[i], horns.horns[horn].name);
	}

	*output = selection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_setup_load_from(const struct mh_horn_source *horn_source,
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
	struct mh_net_session *session, int scenery,
	struct mh_race_context *context, struct mh_net_race *race,
	struct mh_error *error_out)
{
	return mh_network_setup_load_settings_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, sprites, fonts, image_source, image_decoder,
		portrait_decoder, resources, asset_root, session, scenery, NULL,
		context, race, error_out);
}

enum mh_error_code
mh_network_setup_load_settings_from(const struct mh_horn_source *horn_source,
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
	struct mh_net_session *session, int scenery,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_net_race *race, struct mh_error *error_out)
{
	return mh_network_setup_load_presented_from(horn_source,
		material_source, env_sound_source, catalog_source,
		navigation_source, animation_source, atlas_source,
		model_decoder, world_decoder, collision_source, sprites, fonts,
		image_source, image_decoder, portrait_decoder, resources,
		asset_root, session, scenery, settings, context, race, NULL,
		NULL, error_out);
}

static enum mh_error_code
paint_roster(struct mh_race_scene *scene, const struct mh_net_session *session,
	const struct mh_net_race_layout *layout,
	const struct mh_settings *settings, struct mh_error *error_out)
{
	unsigned long colors[BODY_COLORS];
	unsigned int i, j;
	enum mh_error_code status;

	for (i = 0; i < layout->count; i++) {
		const struct mh_net_profile *profile;

		if (i == 0 &&
			(settings == NULL || !settings->custom_car_colors))
			continue;

		profile = &session->roster.players[layout->player[i]];

		for (j = 0; j < BODY_COLORS; j++)
			colors[j] =
				profile->values[MH_NET_PROFILE_CAR_COLOR1 + j] &
				MH_PROFILE_RGB_MASK;

		status = mh_race_scene_paint(scene, i, colors, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_setup_load_presented_from(const struct mh_horn_source *horn_source,
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
	struct mh_net_session *session, int scenery,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_net_race *race, struct mh_session_loading *loading,
	const unsigned long *texture_budget, struct mh_error *error_out)
{
	struct mh_net_asset_selection selection;
	struct mh_net_race_layout layout;
	const struct mh_car *cars[MH_NET_WIRE_PLAYERS];
	unsigned int i;
	enum mh_error_code status;

	if (context == NULL || race == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing network race outputs");

	memset(context, 0, sizeof(*context));
	status = mh_network_setup_resolve_from(horn_source, catalog_source,
		resources, asset_root, session, &selection, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_race_layout(session, &layout, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_session_loading_begin(loading, selection.track,
			session->roster.laps, settings, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_context_load_observed_from(horn_source,
			material_source, env_sound_source, catalog_source,
			navigation_source, animation_source, atlas_source,
			model_decoder, world_decoder, collision_source, sprites,
			fonts, image_source, image_decoder, portrait_decoder,
			resources, asset_root, selection.cars[0],
			selection.track, scenery, settings,
			mh_session_loading_observer(loading), texture_budget,
			context, error_out);

	if (status == MH_ERROR_NONE && layout.count > 1)
		status = mh_race_scene_add_selected_from(context->resources,
			asset_root, selection.cars + 1, selection.track,
			layout.count - 1, &context->scene, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_CARS, error_out);

	/* Network entrants have no local AI-path load. */
	if (status == MH_ERROR_NONE)
		status = mh_session_loading_stage(
			loading, MH_RACE_LOAD_AI, error_out);

	if (status == MH_ERROR_NONE)
		status = paint_roster(
			&context->scene, session, &layout, settings, error_out);

	if (status == MH_ERROR_NONE) {
		context->scene.assets.track.laps = session->roster.laps;
		cars[0] = &context->scene.assets.car;

		for (i = 1; i < layout.count; i++)
			cars[i] = &context->scene.assets.opponents[i - 1];

		status = mh_net_race_start(session, cars,
			context->scene.assets.car_hulls,
			&context->scene.assets.world_collision,
			&context->scene.assets.materials,
			&context->scene.assets.route,
			&context->scene.assets.track, race, error_out);
	}

	if (status == MH_ERROR_NONE)
		for (i = 0; i < layout.count; i++)
			strcpy(session->roster.players[layout.player[i]].horn,
				selection.horns[i]);

	if (status == MH_ERROR_NONE)
		mh_race_scene_bind_cameras(&context->scene, &race->field);

	if (status == MH_ERROR_NONE)
		status = mh_race_scene_bind_objects(
			&context->scene, &race->field, error_out);

	if (status == MH_ERROR_NONE && settings != NULL)
		for (i = 0; i < layout.count; i++)
			race->field.cars[i].race.split_hold_ms =
				settings->av.checkpoint_delay;

	if (status != MH_ERROR_NONE)
		mh_race_context_free(context);

	return status;
}

static void
display_name(const char *source, char output[WIRE_NAME_SIZE])
{
	unsigned int i;
	unsigned char byte;

	for (i = 0; i < WIRE_NAME_SIZE - 1 && source[i] != 0; i++) {
		byte = (unsigned char)source[i];
		output[i] = byte >= ASCII_FIRST && byte <= ASCII_LAST
			? (char)byte
			: '?';
	}

	output[i] = 0;

	if (i == 0)
		strcpy(output, "Driver");
}

static void
copy_display_profile(
	const struct mh_net_profile *source, struct mh_profile *display)
{
	unsigned int i;

	for (i = 0; i < PROFILE_COLORS; i++)
		display->colors[i] = source->values[i] & MH_PROFILE_RGB_MASK;

	display->color_fields = MH_PROFILE_ALL_COLORS;
	strcpy(display->team, source->team);
}

enum mh_error_code
mh_network_setup_ui(const char *asset_root,
	const struct mh_net_session *session, const struct mh_net_race *race,
	struct mh_race_context *context, struct mh_error *error_out)
{
	return mh_network_setup_ui_presented(
		asset_root, session, race, context, NULL, error_out);
}

enum mh_error_code
mh_network_setup_ui_presented(const char *asset_root,
	const struct mh_net_session *session, const struct mh_net_race *race,
	struct mh_race_context *context, struct mh_session_loading *loading,
	struct mh_error *error_out)
{
	char names[MH_NET_WIRE_PLAYERS][WIRE_NAME_SIZE];
	const char *display_names[MH_NET_WIRE_PLAYERS];
	enum mh_error_code status;
	unsigned int i, player;

	if (session == NULL || race == NULL || context == NULL ||
		race->layout.count < 1 ||
		race->layout.count > MH_NET_WIRE_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network UI roster");

	for (i = 0; i < race->layout.count; i++) {
		player = race->layout.player[i];

		if (player >= session->roster.player_count ||
			player >= MH_NET_WIRE_PLAYERS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network UI slot");

		display_name(session->roster.players[player].name, names[i]);

		display_names[i] = names[i];
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
			player = race->layout.player[i];
			copy_display_profile(&session->roster.players[player],
				&context->results.profiles[i]);
		}

	return status;
}
