#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_SETTINGS_STORE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_SETTINGS_STORE_H

#include "client/settings/store.h"

/*
 * Load display-name catalogs from the installed CAR/TRK/CHF assets, then a
 * native CFG. NULL path uses the platform's per-user Motorhead directory. It
 * never loads or writes the original game/MotorHead.CFG implicitly. Defaults
 * and random are caller-supplied; failed opens preserve random and output.
 */
enum mh_error_code	mh_settings_store_open(const char *asset_root,
	const char *path, const struct mh_settings *defaults,
	unsigned long *random, struct mh_settings_store **output,
	struct mh_error *error_out);

#endif
