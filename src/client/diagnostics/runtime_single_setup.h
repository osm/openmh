#ifndef MH_CLIENT_DIAGNOSTICS_RUNTIME_SINGLE_SETUP_H
#define MH_CLIENT_DIAGNOSTICS_RUNTIME_SINGLE_SETUP_H

#include "client/session/single_setup.h"

/* Native filesystem conveniences; ownership follows the portable operations. */

enum mh_error_code	mh_single_setup_load(const char *asset_root,
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	int scenery, unsigned long *random, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_error *error_out);

enum mh_error_code	mh_attract_setup_load(const char *asset_root,
	const struct mh_profile_catalog *catalog,
	const struct mh_horn_catalog *horns,
	const struct mh_settings *saved_settings, char previous_track[16],
	int scenery, unsigned long *random, struct mh_race_context *context,
	struct mh_field *field, struct mh_single_selection *selection,
	struct mh_settings *race_settings, struct mh_error *error_out);

#endif
