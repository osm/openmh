#include "extract/prepare/install.h"

enum mh_error_code
mh_extract_install(const struct mh_extract_input *input,
	const char *const *patches, size_t patch_count, const char *destination,
	int package, const struct mh_extract_options *options,
	mh_extract_progress progress, void *context, int *patched,
	struct mh_error *error_out)
{
	(void)input;

	(void)patches;
	(void)patch_count;

	(void)destination;
	(void)package;
	(void)options;
	(void)progress;
	(void)context;
	(void)patched;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"extraction and patching need a filesystem staging backend and libunshield");
}
