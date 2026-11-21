#include "extract/hosts/optical.h"

enum mh_error_code
mh_extract_optical_probe(
	const char *path, int *matches, struct mh_error *error_out)
{
	(void)path;

	*matches = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_extract_optical_open(const char *path, struct mh_optical_disc **output,
	struct mh_error *error_out)
{
	(void)path;

	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"direct CD access is unavailable on this platform; use a disc image");
}
