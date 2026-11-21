#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_CONTROL_STORE_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_CONTROL_STORE_H

#include "engine/storage/control_store.h"

/*
 * Filesystem adapter for the CLO catalog. Read source_directory and optionally
 * overlay user_directory by ASCII case-insensitive basename. Directories must
 * exist; open never writes. source_directory must be inside canonical
 * asset_root; user files must be outside it. NULL user_directory gives a
 * session-only catalog. Enumeration uses deterministic folded filename order.
 *
 * Reject case aliases, files over 1 MiB, aggregate text over 32 MiB, and more
 * than 4096 merged profiles. Installed and user files reject links and
 * nonregular entries through the held directory provider. A file disappearing
 * after enumeration supplies the original numbered defaults, dirty=1. catalog
 * must be empty; both outputs remain unchanged on failure.
 */
enum mh_error_code	mh_control_store_open(const char *asset_root,
	const char *source_directory, const char *user_directory,
	const char *selection, struct mh_control_catalog *catalog,
	struct mh_control_store **output, struct mh_error *error_out);

#endif
