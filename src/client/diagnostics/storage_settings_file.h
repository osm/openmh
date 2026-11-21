#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_SETTINGS_FILE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_SETTINGS_FILE_H

#include "engine/storage/settings_file.h"

/*
 * Diagnostic adapters select filesystem user data. Native reads accept regular
 * files and symlinks to them. Native saves use a private sibling temporary,
 * flush/sync/close and rename; failure preserves the destination and removes
 * the temporary. Parent directories must exist. Symlinks are replaced rather
 * than followed. Saves require caller serialization. The directory itself is
 * not fsynced, so this does not promise survival of a sudden power failure.
 */
enum mh_error_code	mh_settings_file_load(const char *path,
	const struct mh_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, int *present, struct mh_error *error_out);

enum mh_error_code	mh_settings_file_save(const char *path,
	const struct mh_settings_catalog *catalog,
	const struct mh_settings *settings, struct mh_error *error_out);

#endif
