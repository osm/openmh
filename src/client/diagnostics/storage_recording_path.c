#include "client/diagnostics/storage_recording_path.h"
#include "client/hosts/storage.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_recording_network_path(const char *directory, const char *command,
	char path[MH_RECORDING_PATH_SIZE], struct mh_error *error_out)
{
	return mh_recording_network_path_from(
		mh_host_user_data(), directory, command, path, error_out);
}
