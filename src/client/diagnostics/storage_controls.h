#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_CONTROLS_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_CONTROLS_H

#include "client/settings/controls.h"

/* Select filesystem resources and the native host's user data provider. */
enum mh_error_code	mh_control_profiles_load(const char *asset_root,
	const char *directory, struct mh_settings *settings,
	struct mh_control_binding_set *output, struct mh_error *error_out);

#endif
