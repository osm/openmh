#ifndef MH_CLIENT_DIAGNOSTICS_STORAGE_RECORDING_CATALOG_H
#define MH_CLIENT_DIAGNOSTICS_STORAGE_RECORDING_CATALOG_H

#include "engine/storage/recording_catalog.h"

/*
 * Native path convenience: installed enumeration first, optional user second.
 * Missing directories are empty. Retains exact filenames for launch/deletion;
 * the original basename/last-match rules select the visible entry.
 * Neither scanning nor path lookup creates files. Output is atomic and
 * must be empty; close releases names and platform directory handles.
 */
enum mh_error_code	mh_recording_catalog_read(const char *asset_root,
	const char *installed, const char *user, const char *saved,
	struct mh_recording_catalog *output, struct mh_error *error_out);

#endif
