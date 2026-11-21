#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_LEAGUE_SETUP_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_LEAGUE_SETUP_H

#include "client/session/league_setup.h"

/* Native filesystem conveniences; ownership follows the portable operations. */
enum mh_error_code	mh_league_setup_resolve(const char *asset_root,
	const struct mh_league_document *document,
	unsigned int human_car_number, const char *human_horn,
	struct mh_league_asset_selection *output, struct mh_error *error_out);

enum mh_error_code	mh_league_setup_load_settings(const char *asset_root,
	const struct mh_league_document *document,
	const struct mh_league_drive_options *options,
	struct mh_settings *settings, struct mh_race_context *context,
	struct mh_field *field, struct mh_league_asset_selection *selection,
	struct mh_error *error_out);

#endif
