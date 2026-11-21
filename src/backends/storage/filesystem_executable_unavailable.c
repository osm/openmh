#include "backends/storage/filesystem_provider.h"

enum mh_error_code
mh_filesystem_executable_directory(
	char *output, size_t capacity, struct mh_error *error_out)
{
	(void)output;

	(void)capacity;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"executable directory lookup is unavailable on this platform");
}
