#include "backends/storage/filesystem_provider.h"
#include "engine/assets/packages.h"
#include "extract/hosts/optical.h"
#include "extract/source/input.h"

enum mh_error_code
mh_extract_input_identify(const char *path, struct mh_extract_input *output,
	struct mh_error *error_out)
{
	struct mh_data_directory *directory;
	struct mh_content_package_info info;
	int device;
	enum mh_error_code status;

	output->path = path;
	output->kind = MH_EXTRACT_INPUT_FILE;
	status = mh_extract_optical_probe(path, &device, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (device) {
		output->kind = MH_EXTRACT_INPUT_DEVICE;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	directory = NULL;
	status = mh_resource_source_open_directory(
		mh_filesystem_resources(), path, &directory, error_out);

	if (status == MH_ERROR_IO)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (status != MH_ERROR_NONE)
		return status;

	mh_data_directory_close(directory);
	status = mh_content_package_probe(
		mh_filesystem_resources(), path, &info, error_out);

	if (status == MH_ERROR_NOT_FOUND) {
		output->kind = MH_EXTRACT_INPUT_DISC;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status == MH_ERROR_NONE)
		output->kind = MH_EXTRACT_INPUT_GAME;

	return status;
}
