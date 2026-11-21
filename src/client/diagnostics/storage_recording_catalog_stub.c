#include "client/diagnostics/storage_recording_catalog.h"
#include "engine/core/error.h"
#include "engine/storage/recording_catalog.h"

enum mh_error_code
mh_recording_catalog_read(const char *asset_root, const char *installed,
	const char *user, const char *saved_name,
	struct mh_recording_catalog *output, struct mh_error *error_out)
{
	(void)asset_root;

	(void)installed;
	(void)user;
	(void)saved_name;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"recording catalog backend unavailable");
}
