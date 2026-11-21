#ifndef MH_EXTRACT_SOURCE_PATCH_H
#define MH_EXTRACT_SOURCE_PATCH_H

#include "extract/progress/report.h"

/*
 * Identify by bytes, not the supplied filename. Nonpatches set found to zero.
 */
enum mh_error_code	mh_extract_patch_identify(const char *path, int *found,
	struct mh_error *error_out);
enum mh_error_code	mh_extract_patch_read(const char *path,
	const char *staging, struct mh_file *cabinet,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
