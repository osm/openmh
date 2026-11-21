#ifndef MH_CLIENT_SESSION_LEAGUE_SETUP_H
#define MH_CLIENT_SESSION_LEAGUE_SETUP_H

#include "client/session/race_context.h"
#include "engine/core/error.h"
#include "engine/game/league_document.h"
#include "engine/game/league_race.h"
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
 * are declared in client/diagnostics/runtime_league_setup.h.
 */

struct mh_league_drive_options {
	unsigned int	car, slot, difficulty;
	const char	*horn;
	int		manual, scenery, override_grid;
};

struct mh_league_asset_selection {
	struct mh_league_roster		roster;
	struct mh_league_schedule	schedule;
	unsigned int			track, cars[MH_LEAGUE_DIVISION_SIZE];
	char				horns[MH_LEAGUE_DIVISION_SIZE][16];
};

/*
 * Resolve the installed numbered track/car catalogs, case-insensitively. Tracks
 * use the stable ascending Division catalog order, matching the frontend.
 * Equal-code ties retain numeric installation order (native). Human car/horn
 * are current menu choices; AI uses its profile's stage-specific car and
 * canonical horn display name. Raw ADP horn names must first pass
 * mh_horn_profile_find during profile initialization. No geometry allocations.
 * Missing/ambiguous names, invalid identities or completed schedules fail
 * without changing output. No league mutation or RNG consumption.
 */
enum mh_error_code	mh_league_setup_resolve_from(
	const struct mh_horn_source *horn_source,
	const struct mh_catalog_source *catalog_source,
	const struct mh_resource_source *resources, const char *asset_root,
	const struct mh_league_document *document,
	unsigned int human_car_number, const char *human_horn,
	struct mh_league_asset_selection *output, struct mh_error *error_out);

/*
 * Load a physical race with selected cars, hulls, route, AI path, scenery and
 * cameras. Use the league grid by default; override_grid selects the diagnostic
 * human options.slot with AI at the lowest remaining slots. selection.roster
 * retains the league slots even with this override. Caller supplies empty
 * assets; free/zero on failure. Field/selection unchanged on failure. Their
 * borrowed asset addresses must remain stable after success. Does not prepare
 * league ranks, award results or save a profile.
 *
 * Borrow settings for the asset lifetime. Human body paint uses current CFG
 * only when CustomCarColours is On; AI always uses its bound league profile.
 */
enum mh_error_code	mh_league_setup_load_settings_from(
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
	const struct mh_league_document *document,
	const struct mh_league_drive_options *options,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_field *field, struct mh_league_asset_selection *selection,
	struct mh_error *error_out);

/*
 * Append HUD and roster portraits; caller discards assets on failure.
 *
 * Human name/map color stay bound to the saved league; body RGB/team come
 * from current preferences even with painting Off. NULL uses CFG defaults.
 * This changes display copies only; the league document is never modified.
 */
enum mh_error_code	mh_league_setup_ui_settings(const char *asset_root,
	const struct mh_league_document *document,
	const struct mh_league_asset_selection *selection,
	const struct mh_settings *settings, struct mh_race_context *context,
	struct mh_error *error_out);

/* Optional loading presentation; caller closes the loading owner. */
struct mh_session_loading;
enum mh_error_code	mh_league_setup_load_presented_from(
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
	const struct mh_league_document *document,
	const struct mh_league_drive_options *options,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_field *field, struct mh_league_asset_selection *selection,
	struct mh_session_loading *loading, const unsigned long *texture_budget,
	struct mh_error *error_out);

/* Optional loading presentation; caller closes the loading owner. */
enum mh_error_code	mh_league_setup_ui_presented(const char *asset_root,
	const struct mh_league_document *document,
	const struct mh_league_asset_selection *selection,
	const struct mh_settings *settings, struct mh_race_context *context,
	struct mh_session_loading *loading, struct mh_error *error_out);

#endif
