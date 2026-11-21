#include <stdlib.h>
#include <string.h>

#include "client/session/race_context.h"
#include "client/settings/render_choice.h"
#include "engine/core/error.h"
#include "engine/settings/course_music.h"

#define SOFTWARE_GREEN_BITS 6

enum mh_error_code
mh_race_context_load_observed_from(const struct mh_horn_source *horn_source,
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
	unsigned int car_number, unsigned int track_number, int scenery,
	struct mh_settings *settings,
	const struct mh_race_load_observer *observer,
	const unsigned long *texture_budget, struct mh_race_context *context,
	struct mh_error *error_out)
{
	enum mh_render_profile profile;
	enum mh_error_code status;

	status = mh_render_choice_profile(settings == NULL
			? mh_render_choice_default()
			: settings->renderer,
		&profile, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_race_context_load_profile_observed_from(horn_source,
		material_source, env_sound_source, catalog_source,
		navigation_source, animation_source, atlas_source,
		model_decoder, world_decoder, collision_source, sprites, fonts,
		image_source, image_decoder, portrait_decoder, resources,
		asset_root, car_number, track_number, scenery,
		settings == NULL ? NULL : &settings->av, profile, observer,
		texture_budget, context, error_out);

	if (status == MH_ERROR_NONE) {
		context->scene.assets.track.cd_track = mh_settings_track_music(
			settings, &context->scene.assets.track);
	}

	return status;
}

enum mh_error_code
mh_race_context_load_profile_from(const struct mh_horn_source *horn_source,
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
	unsigned int car_number, unsigned int track_number, int scenery,
	struct mh_av_settings *settings, enum mh_render_profile profile,
	struct mh_race_context *context, struct mh_error *error_out)
{
	return mh_race_context_load_profile_observed_from(horn_source,
		material_source, env_sound_source, catalog_source,
		navigation_source, animation_source, atlas_source,
		model_decoder, world_decoder, collision_source, sprites, fonts,
		image_source, image_decoder, portrait_decoder, resources,
		asset_root, car_number, track_number, scenery, settings,
		profile, NULL, NULL, context, error_out);
}

enum mh_error_code
mh_race_context_load_profile_observed_from(
	const struct mh_horn_source *horn_source,
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
	unsigned int car_number, unsigned int track_number, int scenery,
	struct mh_av_settings *settings, enum mh_render_profile profile,
	const struct mh_race_load_observer *observer,
	const unsigned long *texture_budget, struct mh_race_context *context,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (sprites == NULL || fonts == NULL || image_source == NULL ||
		image_decoder == NULL || portrait_decoder == NULL ||
		model_decoder == NULL || world_decoder == NULL ||
		collision_source == NULL || resources == NULL ||
		context == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing race resources or context");

	memset(context, 0, sizeof(*context));
	mh_render_choice_size(profile, settings);
	status = mh_race_scene_load_observed_from(horn_source, material_source,
		env_sound_source, catalog_source, navigation_source,
		animation_source, atlas_source, model_decoder, world_decoder,
		collision_source, image_source, image_decoder, resources,
		asset_root, car_number, track_number, scenery, settings,
		profile, observer, texture_budget, &context->scene, error_out);

	if (status == MH_ERROR_NONE) {
		context->settings = settings;
		context->resources = resources;
		context->font_source = fonts;
		context->sprite_source = sprites;
		context->image_decoder = image_decoder;
		context->portrait_decoder = portrait_decoder;
	}

	return status;
}

struct mh_ui_atlas *
mh_race_context_atlas(struct mh_race_context *context)
{
	return context->scene.assets.profile == MH_RENDER_SOFTWARE
		? &context->software_hud.atlas
		: &context->hud.atlas;
}

enum mh_error_code
mh_race_context_load_hud(const char *root, struct mh_race_context *context,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (!MH_RENDER_BUILD_SOFTWARE ||
		context->scene.assets.profile != MH_RENDER_SOFTWARE) {
		status = mh_hud_assets_load_from(context->font_source,
			context->image_decoder, context->resources, root,
			&context->hud, error_out);

		if (status == MH_ERROR_NOT_FOUND)
			status = mh_hud_assets_load_software_from(
				context->sprite_source, context->font_source,
				context->resources, root,
				&context->scene.assets.track, &context->hud,
				error_out);

		if (status == MH_ERROR_NONE && context->settings != NULL &&
			context->settings->system == MH_UNITS_IMPERIAL &&
			context->hud.sprites[MH_HUD_MPH].width == 0)
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"Imperial HUD requires patched game assets");

		return status;
	}

	/* The software renderer presents an RGB565 surface. */
	status = mh_software_hud_assets_load_from(context->sprite_source,
		context->font_source, context->resources, root,
		&context->scene.assets.track, context->scene.assets.view_width,
		SOFTWARE_GREEN_BITS, &context->software_hud, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (context->settings != NULL &&
		context->settings->system == MH_UNITS_IMPERIAL &&
		context->software_hud.sprites[MH_SOFTWARE_HUD_MPH].width == 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"Imperial HUD requires patched game assets");

	status = mh_hud_assets_message_widths_from(context->font_source,
		context->resources, root, context->hud.message_advance,
		error_out);

	if (status != MH_ERROR_NONE)
		mh_software_hud_assets_free(&context->software_hud);

	return status;
}

enum mh_error_code
mh_race_context_load_map(const char *root, struct mh_race_context *context,
	struct mh_error *error_out)
{
	if (context->scene.assets.profile == MH_RENDER_SOFTWARE ||
		context->hud.map.width != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return mh_hud_assets_map_from(context->image_decoder,
		context->resources, root, &context->scene.assets.track,
		&context->hud, error_out);
}

void
mh_race_context_free(struct mh_race_context *context)
{
	mh_race_scene_free(&context->scene);
	free(context->record_directory);
	free(context->hud.atlas.image.rgba);
	mh_software_hud_assets_free(&context->software_hud);
	memset(context, 0, sizeof(*context));
}
