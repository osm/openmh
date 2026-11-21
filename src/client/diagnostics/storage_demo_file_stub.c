#include "client/diagnostics/storage_demo_file.h"
#include "client/diagnostics/storage_demo_file_internal.h"
#include "engine/storage/demo_file.h"

enum mh_error_code
mh_demo_file_open(const char *path, const struct mh_demo *header,
	struct mh_demo_file **output, struct mh_error *error_out)
{
	(void)path;

	(void)header;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"native recording paths are unavailable");
}

enum mh_error_code
mh_demo_file_open_user(const char *path, const char *asset_root,
	const struct mh_demo *header, struct mh_demo_file **output,
	struct mh_error *error_out)
{
	(void)asset_root;

	return mh_demo_file_open(path, header, output, error_out);
}

enum mh_error_code
mh_diagnostic_demo_directory(const char *path, const char *asset_root,
	struct mh_data_directory **output, const char **filename,
	struct mh_error *error_out)
{
	(void)path;

	(void)asset_root;
	(void)output;
	(void)filename;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"native recording paths are unavailable");
}
