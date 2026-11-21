#ifndef MH_CLIENT_SESSION_NETWORK_SETUP_H
#define MH_CLIENT_SESSION_NETWORK_SETUP_H

#include "client/session/race_context.h"
#include "engine/core/error.h"
#include "engine/network/race.h"
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
 * are declared in client/diagnostics/runtime_network_setup.h.
 */

struct mh_net_asset_selection {
	/* Installed catalog numbers in field entry order. */
	unsigned int	track, cars[MH_NET_WIRE_PLAYERS];
	char		horns[MH_NET_WIRE_PLAYERS][MH_NET_NAME_BYTES];
};

/*
 * Load the installed sorted CAR display names and copy them into an unused
 * host session for original admission fallback. No geometry allocations.
 */
enum mh_error_code	mh_network_setup_bind_cars_from(
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	struct mh_net_session *session, struct mh_error *error_out);

/*
 * Resolve original TrackName/CarName strings against installed catalogs,
 * case-insensitively. Peer strings never become filesystem paths.
 * Unknown driver equipment uses local defaults. Tracks must match. Ambiguous
 * names fail atomically. No geometry allocations.
 */
enum mh_error_code	mh_network_setup_resolve_from(
	const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_net_session *session,
	struct mh_net_asset_selection *output, struct mh_error *error_out);

/*
 * Caller supplies an empty assets object, freed/zeroed on any failure. Race
 * borrows that object's address; do not move it after success. Session/race
 * stay unchanged until the race-start stage succeeds. Normal and elimination
 * race modes. Remote cars receive their frozen roster paint, local uses stock.
 */
enum mh_error_code	mh_network_setup_load_from(
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
	struct mh_net_session *session, int scenery,
	struct mh_race_context *context, struct mh_net_race *race,
	struct mh_error *error_out);

/*
 * Borrow settings for the lifetime of assets. CustomCarColours gates entry
 * zero only; all painted entrants use the frozen roster's RGB words.
 */
enum mh_error_code	mh_network_setup_load_settings_from(
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
	struct mh_net_session *session, int scenery,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_net_race *race, struct mh_error *error_out);

/*
 * Appends original HUD/results images with the frozen network player names.
 * Non-printable name bytes become '?' for native presentation only.
 */
enum mh_error_code	mh_network_setup_ui(const char *asset_root,
	const struct mh_net_session *session, const struct mh_net_race *race,
	struct mh_race_context *context, struct mh_error *error_out);

/* Optional loading presentation; caller closes the loading owner. */
struct mh_session_loading;
enum mh_error_code	mh_network_setup_load_presented_from(
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
	struct mh_net_session *session, int scenery,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_net_race *race, struct mh_session_loading *loading,
	const unsigned long *texture_budget, struct mh_error *error_out);

/* Optional loading presentation; caller closes the loading owner. */
enum mh_error_code	mh_network_setup_ui_presented(const char *asset_root,
	const struct mh_net_session *session, const struct mh_net_race *race,
	struct mh_race_context *context, struct mh_session_loading *loading,
	struct mh_error *error_out);

#endif
