#include <stddef.h>
#include <stdlib.h>

#include "backends/ui/dta_provider.h"
#include "engine/assets/menu_layout.h"
#include "engine/core/file.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/ui/layout_source_backend.h"

#define LAYOUT_PATH_BYTES 1024

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, struct mh_menu_layout *output,
	struct mh_error *error_out)
{
	struct mh_file file;
	char path[LAYOUT_PATH_BYTES];
	enum mh_error_code status;

	(void)context;
	status = mh_asset_path_from(
		resources, root, reference, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_menu_layout_dta_decode(
		file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

const struct mh_menu_layout_source *
mh_menu_layout_dta_source(void)
{
	static const struct mh_menu_layout_source source = {NULL, load};

	return &source;
}
