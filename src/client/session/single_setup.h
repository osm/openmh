#ifndef MH_CLIENT_SESSION_SINGLE_SETUP_H
#define MH_CLIENT_SESSION_SINGLE_SETUP_H

#include "client/session/race_context.h"
#include "engine/catalog/profile_load.h"
#include "engine/core/error.h"
#include "engine/game/quick_race.h"
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
 * Source-taking loaders borrow resources and decoders through teardown;
 * resolve/bind operations use them only during the call. Native conveniences
 * are declared in client/diagnostics/runtime_single_setup.h.
 */

struct mh_single_selection {
	unsigned int		count;

	/* One-based installed file numbers; arrays follow field entry order. */
	unsigned int		track, cars[MH_QUICK_CARS];
	unsigned int	slots[MH_QUICK_CARS], profile_sources[MH_QUICK_CARS];

	/*
	 * Entry zero is human except in attract mode. Owns identity/horn text.
	 */
	struct mh_profile	profiles[MH_QUICK_CARS];
};

/*
 * Resolve current preferences into Quick Race (mode 0) or Time Attack (2).
 * settings car/track indices refer to numeric asset-file order, not sorted
 * frontend indices. Profile catalog must already have initialized horns; it may
 * be NULL for Time Attack. Selection owns all identity/horn strings. Quick Race
 * consumes the roster/grid draws; Time Attack consumes none. Missing/ambiguous
 * car/horn choices and invalid inputs preserve output/RNG; track/world/portrait
 * files are checked by the loader below.
 */
enum mh_error_code	mh_single_setup_resolve_from(
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, const struct mh_settings *settings,
	unsigned long *random, struct mh_single_selection *output,
	struct mh_error *error_out);

/*
 * Load the resolved race, AI personalities, paint, hulls, scenery, cameras,
 * HUD/map and roster portraits. Apply laps, transmission, difficulty, Quick
 * Race catch-up and identity cheats. Borrow settings until assets are freed.
 * Caller supplies empty assets; they are freed/zeroed on failure. Field,
 * selection and RNG are unchanged on failure. Asset addresses must remain
 * stable during the race. Does not open/save records, settings or recordings.
 */
enum mh_error_code	mh_single_setup_load_from(
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
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	int scenery, unsigned long *random, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_error *error_out);

/* Same ownership, with optional load-completion notifications. */
enum mh_error_code	mh_single_setup_load_observed_from(
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
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	int scenery, unsigned long *random,
	const struct mh_race_load_observer *observer,
	const unsigned long *texture_budget, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_error *error_out);

/*
 * Attract mode prepares all eight AI profiles and grid slots using the saved
 * car class, then draws an alternate eligible track from the full sorted
 * catalog. Runtime preferences are separate caller-owned storage, borrowed
 * by assets until freed. Saved settings are never changed. Previous track,
 * RNG, field, selection and runtime are published only after a complete load.
 * This prepares the race; it does not run attract timers/cameras or storage.
 */
enum mh_error_code	mh_attract_setup_load_from(
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
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns,
	const struct mh_settings *saved_settings, char previous_track[16],
	int scenery, unsigned long *random, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_settings *race_settings, struct mh_error *error_out);

/* Optional loading presentation; caller closes the loading owner. */
struct mh_session_loading;
enum mh_error_code	mh_attract_setup_load_presented_from(
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
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns,
	const struct mh_settings *saved_settings, char previous_track[16],
	int scenery, unsigned long *random, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_settings *race_settings, struct mh_session_loading *loading,
	const unsigned long *texture_budget, struct mh_error *error_out);

#endif
