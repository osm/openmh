#ifndef MH_SETTINGS_MENU_RETURN_H
#define MH_SETTINGS_MENU_RETURN_H

#include "engine/core/error.h"
#include "engine/settings/config.h"

/*
 * Start from freshly loaded CFG, then restore the pause options from the
 * preceding race context. Network mode additionally restores identity/paint,
 * car/track, horn, transmission and laps, plus the native snapshot-rate and
 * prediction preferences. Mode itself, RecordRace and all other values come
 * from CFG. NULL snapshot represents first entry. Indices refer to the same
 * installed catalogs. All errors preserve output; either input may alias it.
 */
enum mh_error_code	mh_settings_restore_menu(
	const struct mh_settings *loaded, const struct mh_settings *snapshot,
	unsigned int mode, struct mh_settings *output,
	struct mh_error *error_out);

#endif
