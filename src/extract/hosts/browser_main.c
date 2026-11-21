#include "extract/hosts/application.h"

extern void	mh_browser_extract_progress(const char *step,
	const char *detail, double completed, double total);

static const char *
step_name(enum mh_extract_step step)
{
	switch (step) {
	case MH_EXTRACT_UNPACK:
		return "unpack";
	case MH_EXTRACT_READ_DISC:
		return "read-disc";
	case MH_EXTRACT_INSTALLER:
		return "installer";
	case MH_EXTRACT_MOVIES:
		return "movies";
	case MH_EXTRACT_COPY:
		return "copy";
	case MH_EXTRACT_READ_PACKAGE:
		return "read-package";
	case MH_EXTRACT_PATCH:
		return "patch";
	case MH_EXTRACT_WRITE_PACKAGE:
		return "write-package";
	case MH_EXTRACT_CONVERT:
		return "convert";
	case MH_EXTRACT_DISC_FILE:
		return "disc-file";
	case MH_EXTRACT_AUDIO:
		return "audio";
	default:
		return NULL;
	}
}

static enum mh_error_code
report(void *context, enum mh_extract_step step, const char *path,
	size_t completed, size_t total, struct mh_error *error_out)
{
	const char *name;

	(void)context;
	name = step_name(step);

	if (name != NULL)
		mh_browser_extract_progress(name,
			(step == MH_EXTRACT_AUDIO ||
				step == MH_EXTRACT_CONVERT) &&
					path != NULL
				? path
				: "",
			(double)completed, (double)total);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	return mh_extract_main(argc, argv, report, NULL);
}
