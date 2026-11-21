#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_leagues.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_leagues_open(const char *asset_root, const char *directory,
	const struct mh_league_creation *creation, const char *selected_name,
	unsigned long *random, struct mh_leagues **output,
	struct mh_error *error_out)
{
	return mh_leagues_open_from(mh_filesystem_user_data(), asset_root,
		directory, creation, selected_name, random, output, error_out);
}

enum mh_error_code
mh_leagues_reload(const char *asset_root, const char *directory,
	const struct mh_league_creation *creation, const char *selected_name,
	unsigned long *random, struct mh_leagues **leagues,
	struct mh_error *error_out)
{
	return mh_leagues_reload_from(mh_filesystem_user_data(), asset_root,
		directory, creation, selected_name, random, leagues, error_out);
}
