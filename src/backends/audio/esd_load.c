#include <stdlib.h>
#include <string.h>

#include "backends/audio/esd_internal.h"
#include "engine/assets/env_sound.h"
#include "engine/core/file.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"

enum mh_error_code
mh_env_sounds_esd_load(const struct mh_resource_source *source,
	const char *root, const char *name, struct mh_env_sounds *definitions,
	struct mh_error *error_out)
{
	struct mh_file file;
	char path[1024];
	enum mh_error_code status;

	memset(&file, 0, sizeof(file));
	status = mh_asset_path_from(
		source, root, name, path, sizeof(path), error_out);

	if (status == MH_ERROR_NONE)
		status =
			mh_resource_source_read(source, path, &file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_env_sounds_esd_decode(
			file.data, file.size, definitions, error_out);

	free(file.data);

	return status;
}
