#include <stdlib.h>

#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_demo_file.h"
#include "client/diagnostics/storage_demo_file_internal.h"
#include "engine/storage/demo_file.h"
#include "engine/storage/recording_path.h"
#include "interfaces/storage/data_directory_backend.h"

enum mh_error_code
mh_diagnostic_demo_directory(const char *path, const char *asset_root,
	struct mh_data_directory **output, const char **name,
	struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	const char *filename;
	char *parent;
	enum mh_error_code status;

	if (output == NULL || name == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing recording output path");

	parent = NULL;
	filename = NULL;
	directory = NULL;
	status = mh_recording_path_split(path, &parent, &filename, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status =
		mh_filesystem_data_directory_open(parent[0] == 0 ? "." : parent,
			asset_root, &directory, error_out);
	free(parent);

	if (status != MH_ERROR_NONE)
		return status;

	*output = directory;
	*name = filename;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_file(const char *path, const char *asset_root,
	const struct mh_demo *header, struct mh_demo_file **output,
	struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	const char *filename;
	enum mh_error_code status;

	directory = NULL;
	filename = NULL;
	status = mh_diagnostic_demo_directory(
		path, asset_root, &directory, &filename, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_demo_file_open_in(directory, filename, asset_root != NULL,
		header, output, error_out);

	if (status != MH_ERROR_NONE) {
		mh_data_directory_close(directory);
		return mh_error_set(error_out, status,
			status == MH_ERROR_EXISTS
				? "recording destination already exists"
				: "cannot open recording file");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_file_open(const char *path, const struct mh_demo *header,
	struct mh_demo_file **output, struct mh_error *error_out)
{
	return open_file(path, NULL, header, output, error_out);
}

enum mh_error_code
mh_demo_file_open_user(const char *path, const char *asset_root,
	const struct mh_demo *header, struct mh_demo_file **output,
	struct mh_error *error_out)
{
	if (asset_root == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing original asset directory");

	return open_file(path, asset_root, header, output, error_out);
}
