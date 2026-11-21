#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "engine/assets/executable_retro.h"
#include "engine/assets/path_installation.h"
#include "engine/assets/path_retro.h"
#include "extract/prepare/retail.h"

/* The cabinet has already been verified and unpacked into private staging. */
static enum mh_error_code
prepare_retro(const char *staging, struct mh_error *error_out)
{
	struct mh_file executable;
	struct mh_retro_files resources;
	char *path;
	size_t prefix;
	unsigned int i;
	enum mh_error_code status;

	prefix = strlen(staging);

	if (prefix > (size_t)-1 -
			sizeof("/" MH_ASSET_RETRO_DIR
			"/" MH_ASSET_RETRO_ANIMATION_NAME))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"retail retro output path is too long");

	path = malloc(prefix +
		sizeof("/" MH_ASSET_RETRO_DIR
		"/" MH_ASSET_RETRO_ANIMATION_NAME));

	if (path == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory preparing retail retro path");

	memcpy(path, staging, prefix);
	strcpy(path + prefix, "/" MH_ASSET_EXECUTABLE);
	executable.data = NULL;
	memset(&resources, 0, sizeof(resources));
	status = mh_resource_source_read_bounded(mh_filesystem_resources(),
		path, mh_retro_executable_limit(), &executable, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_retro_executable_extract(
			&executable, &resources, error_out);

	strcpy(path + prefix, "/" MH_ASSET_RETRO_DIR);

	if (status == MH_ERROR_NONE)
		status = mh_user_data_create_directory(
			mh_filesystem_user_data(), path, error_out);

	for (i = 0; i < MH_RETRO_PART_COUNT && status == MH_ERROR_NONE; i++) {
		strcpy(path + prefix, "/" MH_ASSET_RETRO_DIR "/");
		strcat(path, mh_retro_mhretro_filename((enum mh_retro_part)i));
		status = mh_user_data_replace(mh_filesystem_user_data(), path,
			resources.parts[i].data, resources.parts[i].size,
			error_out);
	}

	free(executable.data);
	mh_retro_mhretro_free(&resources);
	free(path);

	return status;
}

enum mh_error_code
mh_extract_retail_prepare(const char *staging, struct mh_error *error_out)
{
	return prepare_retro(staging, error_out);
}
