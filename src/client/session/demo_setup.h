#ifndef MH_CLIENT_SESSION_DEMO_SETUP_H
#define MH_CLIENT_SESSION_DEMO_SETUP_H

#include "client/session/race_context.h"
#include "engine/core/error.h"
#include "engine/demo/race.h"
#include "engine/settings/config.h"
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

struct mh_image_source_provider;

/*
 * Source-taking loaders borrow resources and image decoders through teardown;
 * resolve/bind operations use them only during the call. Native conveniences
 * are declared in client/diagnostics/runtime_demo_setup.h.
 */

/*
 * Resolve recorded display names against installed catalogs; names are never
 * paths. Assets must remain at a stable address while the race borrows them.
 * Supply empty assets. Outputs own no original recording bytes.
 * Load failures release the assets created during this call.
 */
enum mh_error_code	mh_demo_setup_load_from(
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
	const struct mh_demo *demo, int ghost, unsigned int local_car_number,
	int manual, int scenery, struct mh_race_context *context,
	struct mh_demo_race *race, struct mh_error *error_out);

/*
 * Settings are borrowed for the lifetime of assets. NULL uses original
 * CustomCarColours Off. Other recorded entrants always use their own RGB;
 * with On, entry zero uses recorded RGB in demo mode or CFG RGB in ghost mode.
 */
enum mh_error_code	mh_demo_setup_load_settings_from(
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
	const struct mh_demo *demo, int ghost, unsigned int local_car_number,
	int manual, int scenery, struct mh_settings *settings,
	struct mh_race_context *context, struct mh_demo_race *race,
	struct mh_error *error_out);
enum mh_error_code	mh_demo_setup_ui(const char *asset_root,
	const char *human_name, const struct mh_demo_race *race,
	struct mh_race_context *context, struct mh_error *error_out);

struct mh_audio_options;

/* Borrow recorded 3.0 horn names; keep legacy/human selections untouched. */
void	mh_demo_setup_audio(const struct mh_demo_race *race,
	struct mh_audio_options *options);

/* Optional loading presentation; caller closes the loading owner. */
struct mh_session_loading;
enum mh_error_code	mh_demo_setup_load_presented_from(
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
	const struct mh_demo *demo, int ghost, unsigned int local_car_number,
	int manual, int scenery, struct mh_settings *settings,
	struct mh_race_context *context, struct mh_demo_race *race,
	struct mh_session_loading *loading, const unsigned long *texture_budget,
	struct mh_error *error_out);

/* Optional loading presentation; caller closes the loading owner. */
enum mh_error_code	mh_demo_setup_ui_presented(const char *asset_root,
	const char *human_name, const struct mh_demo_race *race,
	struct mh_race_context *context, struct mh_session_loading *loading,
	struct mh_error *error_out);

#endif
