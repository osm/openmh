#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_settings_file.h"
#include "engine/storage/settings_file.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_settings_file_load(const char *path,
	const struct mh_settings_catalog *catalog, unsigned long *random,
	struct mh_settings *settings, int *present, struct mh_error *error_out)
{
	return mh_settings_file_load_from(mh_filesystem_user_data(), path,
		catalog, random, settings, present, error_out);
}

enum mh_error_code
mh_settings_file_save(const char *path,
	const struct mh_settings_catalog *catalog,
	const struct mh_settings *settings, struct mh_error *error_out)
{
	return mh_settings_file_save_to(
		mh_filesystem_user_data(), path, catalog, settings, error_out);
}
