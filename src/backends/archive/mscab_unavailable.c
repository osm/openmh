#include "backends/archive/mscab_provider.h"

enum mh_error_code
mh_mscab_open(const void *bytes, size_t size, struct mh_archive **output,
	struct mh_error *error_out)
{
	(void)bytes;

	(void)size;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"MS cabinet import is not included in this build");
}
