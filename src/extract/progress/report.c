#include "backends/storage/filesystem_provider.h"
#include "extract/progress/report.h"

struct unpack_progress {
	mh_extract_progress	report;
	enum mh_extract_step	step;
	void			*context;
};

static enum mh_error_code
report_unpack(void *context, const char *path, size_t completed, size_t total,
	struct mh_error *error_out)
{
	struct unpack_progress *progress;

	progress = context;

	return progress->report(progress->context, progress->step, path,
		completed, total, error_out);
}

enum mh_error_code
mh_extract_unpack(struct mh_archive *archive, const char *root,
	enum mh_extract_step step, mh_extract_progress progress, void *context,
	struct mh_error *error_out)
{
	struct unpack_progress observer;

	observer.report = progress;
	observer.step = step;
	observer.context = context;

	return mh_archive_unpack(archive, mh_filesystem_user_data(), root,
		progress == NULL ? NULL : report_unpack, &observer, error_out);
}
