#include <stdlib.h>

#include "backends/retro/mhretro_provider.h"
#include "engine/assets/retro.h"
#include "engine/storage/asset_path.h"
#include "interfaces/retro/source_backend.h"
#include "interfaces/storage/resource.h"

#define RETRO_PATH_BYTES 1024

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, enum mh_retro_part part,
	union mh_retro_part_data *output, struct mh_error *error_out)
{
	struct mh_file file;
	char directory[RETRO_PATH_BYTES], path[RETRO_PATH_BYTES];
	enum mh_error_code status;

	(void)context;
	file.data = NULL;
	status = mh_asset_path_from(resources, root, reference, directory,
		sizeof(directory), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_asset_path_from(resources, directory,
			mh_retro_mhretro_filename(part), path, sizeof(path),
			error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read_bounded(
			resources, path, MH_RETRO_FILE_LIMIT, &file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_retro_mhretro_decode_part(
			part, file.data, file.size, output, error_out);

	free(file.data);

	return status;
}

const struct mh_retro_source *
mh_retro_mhretro_source(void)
{
	static const struct mh_retro_source source = {NULL, load};

	return &source;
}
