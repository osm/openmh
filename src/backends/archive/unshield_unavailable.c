#include "backends/archive/unshield_provider.h"

enum mh_error_code
mh_unshield_open(const void *bytes, size_t size, struct mh_archive **output,
	struct mh_error *error_out)
{
	(void)bytes;

	(void)size;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"InstallShield import is not included in this build");
}

enum mh_error_code
mh_unshield_open_components(const void *bytes, size_t size,
	struct mh_archive **output, struct mh_error *error_out)
{
	return mh_unshield_open(bytes, size, output, error_out);
}

enum mh_error_code
mh_unshield_sfx_open(const void *bytes, size_t size, int compressed,
	struct mh_archive **output, struct mh_error *error_out)
{
	(void)compressed;

	return mh_unshield_open(bytes, size, output, error_out);
}

enum mh_error_code
mh_unshield_zip_cabinet(const void *bytes, size_t size, struct mh_file *output,
	struct mh_error *error_out)
{
	(void)output;

	return mh_unshield_open(bytes, size, NULL, error_out);
}
