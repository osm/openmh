#ifndef MH_ENGINE_STORAGE_SETTINGS_FILE_H
#define MH_ENGINE_STORAGE_SETTINGS_FILE_H

#include "engine/core/error.h"
#include "engine/settings/config.h"

struct mh_user_data;

/*
 * Decode CFG bytes from the selected writable view. Settings must be
 * initialized. Missing data succeeds with present=0 and retains settings and
 * RNG; all other errors preserve settings, RNG and present. Accept up to 1 MiB.
 * The key is host-selected; legacy config values never select a destination.
 * The provider is needed only during the call. Byte I/O failures use its
 * diagnostics; validation/encoding errors come from the settings format.
 */
enum mh_error_code	mh_settings_file_load_from(
	const struct mh_user_data *data, const char *path,
	const struct mh_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, int *present, struct mh_error *error_out);

/* Encode/validate before atomically replacing the value in the local view. */
enum mh_error_code	mh_settings_file_save_to(
	const struct mh_user_data *data, const char *path,
	const struct mh_settings_catalog *catalog,
	const struct mh_settings *settings, struct mh_error *error_out);

#endif
