#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_league_store.h"
#include "engine/storage/league_store.h"
#include "interfaces/storage/data_directory_backend.h"

enum mh_error_code
mh_league_store_open(const char *asset_root, const char *directory,
	const struct mh_profile *profiles, unsigned int profile_count,
	struct mh_league_catalog *catalog, struct mh_league_store **output,
	struct mh_error *error_out)
{
	struct mh_data_directory *data;
	enum mh_error_code status;

	status = mh_filesystem_data_directory_open(
		directory, asset_root, &data, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_league_store_open_in(
		data, profiles, profile_count, catalog, output, error_out);

	if (status != MH_ERROR_NONE)
		mh_data_directory_close(data);

	return status;
}
