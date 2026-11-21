#include "backends/render/sdl_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "client/hosts/sdl_application.h"
#include "engine/storage/storage.h"

enum mh_error_code
mh_application_open_with_storage(int argc, char **argv,
	struct mh_storage *storage, struct mh_application **output,
	struct mh_error *error_out)
{
	struct mh_app_services services;
	char directory[MH_APPLICATION_PATH_SIZE];
	enum mh_error_code status;

	status = mh_filesystem_executable_directory(
		directory, sizeof(directory), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_host_sdl_services(&services, directory, storage,
		mh_sdl_render_service(), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_application_open_with_services(
		argc, argv, &services, output, error_out);
}

enum mh_error_code
mh_application_open(int argc, char **argv, struct mh_application **output,
	struct mh_error *error_out)
{
	struct mh_storage *storage;
	enum mh_error_code status;

	status = mh_storage_open(&storage, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_application_open_with_storage(
		argc, argv, storage, output, error_out);

	if (status != MH_ERROR_NONE)
		mh_storage_close(storage);

	return status;
}
