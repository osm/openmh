#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_controls.h"
#include "client/hosts/storage.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_control_profiles_load(const char *asset_root, const char *directory,
	struct mh_settings *settings, struct mh_control_binding_set *output,
	struct mh_error *error_out)
{
	const struct mh_user_data *data;

	data = mh_host_user_data();

	if (data == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"native control storage is unavailable");

	return mh_control_profiles_load_from(mh_filesystem_resources(), data,
		asset_root, directory, settings, output, error_out);
}
