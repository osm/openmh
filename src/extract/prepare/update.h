#ifndef MH_EXTRACT_UPDATE_H
#define MH_EXTRACT_UPDATE_H

#include "extract/output/options.h"
#include "extract/progress/report.h"

enum mh_error_code	mh_extract_update_directory(const char *patch,
	const char *destination, int keep_original,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);
enum mh_error_code	mh_extract_update_package(const char *patch,
	const char *destination, const struct mh_extract_options *options,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
