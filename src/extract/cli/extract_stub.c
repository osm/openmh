#include "extract/cli/extract.h"

enum mh_error_code
mh_extract_run(const char *const *sources, size_t count,
	const char *destination, const struct mh_extract_options *options,
	int *patched, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	(void)sources;

	(void)count;

	(void)destination;
	(void)options;
	(void)patched;
	(void)progress;
	(void)context;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"extraction needs a filesystem staging backend and libunshield");
}
