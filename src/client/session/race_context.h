#ifndef MH_CLIENT_SESSION_RACE_CONTEXT_H
#define MH_CLIENT_SESSION_RACE_CONTEXT_H

#include "client/session/records.h"
#include "engine/core/error.h"
#include "engine/render/scene.h"
#include "engine/settings/config.h"
#include "engine/settings/control_layout.h"
#include "engine/ui/hud_assets.h"
#include "engine/ui/results_assets.h"
#include "engine/ui/software_hud_assets.h"
#include "interfaces/collision/source.h"
#include "interfaces/geometry/decoder.h"
#include "interfaces/image/atlas_source.h"

struct mh_horn_source;

struct mh_env_sound_source;
struct mh_material_source;
struct mh_catalog_source;
struct mh_navigation_source;
struct mh_animation_source;
struct mh_sprite_source_provider;
struct mh_font_source_provider;

struct mh_resource_source;
struct mh_user_data;

struct mh_image_source_provider;

/*
 * Owns one prepared race and its presentation. Field/camera pointers borrow the
 * scene's loaded data: keep this object at a stable address until race close.
 * Settings, controls and an optional score history remain caller-owned.
 */
struct mh_race_context {
	/*
	 * Borrowed through the context lifetime, including later HUD/map loads.
	 */
	const struct mh_resource_source		*resources;
	const struct mh_font_source_provider	*font_source;
	const struct mh_sprite_source_provider	*sprite_source;
	const struct mh_image_decoder_provider	*image_decoder;
	const struct mh_image_decoder_provider	*portrait_decoder;
	struct mh_race_scene			scene;

	/* Software sessions use hud only for shared message-wrap advances. */
	struct mh_hud_assets			hud;
	struct mh_software_hud_assets		software_hud;
	struct mh_results_assets		results;

	/* Borrowed until context close; selected when records are opened. */
	const struct mh_user_data		*record_data;

	/* Owned; NULL until records are opened. */
	char					*record_directory;
	struct mh_race_scores			scores;
	struct mh_av_settings			*settings;
	const struct mh_control_binding_set	*controls;
	struct mh_race_info_metadata		info_metadata[MH_RACE_MAX_CARS];

	/* Recorded identities, independent of input. */
	unsigned int				info_metadata_mask;
};

/*
 * Source-file car/track numbers are one-based. Load into a fresh context.
 * Apply ScreenDimension/CarDetail/TrackDetail before loading geometry.
 * Other saved values remain borrowed live preferences. The viewport, model
 * chain and scenery stay fixed for this asset lifetime; defaults match the
 * ordinary loader. Logical atlas capacity is 4 MiB for Glide, 32 MiB for
 * Direct3D; an explicit texture budget accepts zero through the profile limit.
 * This startup-only capacity does not describe actual GPU memory.
 * NULL preferences use the default included renderer.
 * Completion notifications are optional and borrowed only during preparation.
 */
enum mh_error_code	mh_race_context_load_observed_from(
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
	struct mh_settings *settings,
	const struct mh_race_load_observer *observer,
	const unsigned long *texture_budget, struct mh_race_context *context,
	struct mh_error *error_out);

/* Explicit resource profile; selection is fixed until this context is freed. */
enum mh_error_code	mh_race_context_load_profile_from(
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
	struct mh_race_context *context, struct mh_error *error_out);
enum mh_error_code	mh_race_context_load_profile_observed_from(
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
	struct mh_error *error_out);

/* Load HUD before results; the returned atlas owns both sets of sprites. */
enum mh_error_code	 mh_race_context_load_hud(const char *root,
	struct mh_race_context *context, struct mh_error *error_out);
enum mh_error_code	 mh_race_context_load_map(const char *root,
	struct mh_race_context *context, struct mh_error *error_out);
struct mh_ui_atlas	*mh_race_context_atlas(struct mh_race_context *context);

void	mh_race_context_free(struct mh_race_context *context);

#endif
