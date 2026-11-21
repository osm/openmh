#include "backends/audio/provider.h"
#include "backends/catalog/provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_settings_store.h"
#include "client/hosts/storage.h"
#include "client/settings/render_choice.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_settings_store_open(const char *asset_root, const char *path,
	const struct mh_settings *defaults, unsigned long *random,
	struct mh_settings_store **output, struct mh_error *error_out)
{
	const struct mh_user_data *user_data;
	struct mh_settings_names renderers;

	user_data = mh_host_user_data();

	if (user_data == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"native settings storage is unavailable");

	renderers.names = mh_render_choice_names();
	renderers.count = MH_RENDER_CHOICE_COUNT;

	return mh_settings_store_open_with_sources(mh_horn_fallback_source(),
		mh_catalog_fallback_source(), mh_filesystem_resources(),
		user_data, &renderers, asset_root, path, defaults, random,
		output, error_out);
}
