#include "backends/storage/filesystem_provider.h"
#include "engine/assets/prepare_demo_audio.h"
#include "engine/assets/prepare_demo_catalog.h"
#include "engine/assets/prepare_demo_collision.h"
#include "engine/assets/prepare_demo_drivers.h"
#include "engine/assets/prepare_demo_font.h"
#include "engine/assets/prepare_demo_horn.h"
#include "engine/assets/prepare_demo_loading.h"
#include "engine/assets/prepare_demo_results.h"
#include "extract/prepare/demo.h"
#include "extract/source/packages.h"

static enum mh_error_code
prepare_game(const struct mh_demo_recipe *recipe, const char *root,
	struct mh_error *error_out)
{
	const struct mh_resource_source *source;
	const struct mh_user_data *data;
	enum mh_error_code status;

	source = mh_filesystem_resources();
	data = mh_filesystem_user_data();
	status = mh_demo_catalog_prepare(source, data, recipe, root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_collision_prepare(
			source, data, recipe, root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_font_prepare(
			source, data, recipe, root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_results_prepare(
			source, data, recipe, root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_horn_prepare(
			source, data, recipe, root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_drivers_prepare(
			source, data, recipe, root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_audio_prepare(
			source, data, recipe, root, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_demo_loading_prepare(
			source, data, recipe, root, error_out);

	return status;
}

enum mh_error_code
mh_extract_demo_prepare(const struct mh_demo_package *package,
	const char *staging, struct mh_error *error_out)
{
	return prepare_game(&package->recipe, staging, error_out);
}
