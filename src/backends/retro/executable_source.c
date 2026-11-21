#include <stdlib.h>
#include <string.h>

#include "backends/retro/executable_provider.h"
#include "engine/assets/executable_retro.h"
#include "engine/assets/path_installation.h"
#include "engine/storage/asset_path.h"
#include "interfaces/retro/source_backend.h"
#include "interfaces/storage/resource.h"

#define RETRO_PATH_BYTES 1024

static enum mh_error_code
load(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, enum mh_retro_part part,
	union mh_retro_part_data *output, struct mh_error *error_out)
{
	struct mh_retro_files files;
	struct mh_file executable;
	char path[RETRO_PATH_BYTES];
	enum mh_error_code status;

	(void)context;
	(void)reference;
	memset(&files, 0, sizeof(files));
	executable.data = NULL;
	status = mh_asset_path_from(resources, root, MH_ASSET_EXECUTABLE, path,
		sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_resource_source_read_bounded(resources, path,
			mh_retro_executable_limit(), &executable, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_retro_executable_extract(
			&executable, &files, error_out);

	free(executable.data);

	if (status == MH_ERROR_NONE)
		status = mh_retro_mhretro_decode_part(part,
			files.parts[part].data, files.parts[part].size, output,
			error_out);

	mh_retro_mhretro_free(&files);

	return status;
}

const struct mh_retro_source *
mh_retro_executable_source(void)
{
	static const struct mh_retro_source source = {NULL, load};

	return &source;
}
