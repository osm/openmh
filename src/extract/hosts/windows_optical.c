#include "backends/storage/optical_windows_provider.h"
#include "extract/hosts/optical.h"

enum mh_error_code
mh_extract_optical_probe(
	const char *path, int *matches, struct mh_error *error_out)
{
	return mh_windows_optical_probe(path, matches, error_out);
}

enum mh_error_code
mh_extract_optical_open(const char *path, struct mh_optical_disc **output,
	struct mh_error *error_out)
{
	return mh_windows_optical_open(path, output, error_out);
}
