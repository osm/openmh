#ifndef MH_CLIENT_DIAGNOSTICS_ASSET_PATH_H
#define MH_CLIENT_DIAGNOSTICS_ASSET_PATH_H

#include "engine/storage/asset_path.h"

/*
 * Native path lookup; selects the filesystem resource source.
 * Resolution and optional-lookup rules follow the portable _from operations.
 */
enum mh_error_code	mh_asset_path(const char *root, const char *relative,
	char *resolved, size_t capacity, struct mh_error *error_out);

enum mh_error_code	mh_asset_path_find(const char *root,
	const char *relative, char *resolved, size_t capacity, int *found,
	struct mh_error *error_out);

#endif
