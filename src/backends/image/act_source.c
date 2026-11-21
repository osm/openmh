#include <stddef.h>
#include <stdlib.h>

#include "backends/image/act_provider.h"
#include "engine/assets/palette.h"
#include "engine/core/file.h"
#include "engine/storage/asset_path.h"
#include "interfaces/image/palette_backend.h"
#include "interfaces/storage/resource.h"

#define PALETTE_PATH_BYTES 1024

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference,
	unsigned long output[MH_PALETTE_COLORS], struct mh_error *error_out)
{
	struct mh_file file;
	char path[PALETTE_PATH_BYTES];
	enum mh_error_code status;

	(void)context;
	status = mh_asset_path_from(
		resources, root, reference, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read(resources, path, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_palette_act_decode(file.data, file.size, output, error_out);
	free(file.data);

	return status;
}

const struct mh_palette_source *
mh_palette_act_source(void)
{
	static const struct mh_palette_source source = {NULL, load};

	return &source;
}
