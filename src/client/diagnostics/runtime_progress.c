#include "backends/catalog/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/runtime_progress.h"
#include "client/hosts/storage.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_progress_path(const char *directory, char output[MH_PROGRESS_PATH_SIZE],
	struct mh_error *error_out)
{
	if (directory != NULL && directory[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid progress directory");

	return mh_progress_path_from(
		mh_host_user_data(), directory, output, error_out);
}

enum mh_error_code
mh_progress_award(const char *asset_root, const char *directory,
	const struct mh_league *league, int unlock_cheat,
	struct mh_error *error_out)
{
	return mh_progress_award_from(mh_catalog_fallback_source(),
		mh_filesystem_resources(), mh_filesystem_user_data(),
		asset_root, directory, league, unlock_cheat, error_out);
}
