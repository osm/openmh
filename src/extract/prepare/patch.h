#ifndef MH_EXTRACT_PREPARE_PATCH_H
#define MH_EXTRACT_PREPARE_PATCH_H

#include "extract/progress/report.h"

enum mh_error_code	mh_extract_patch_validate(const char *installation,
	struct mh_error *error_out);
enum mh_error_code	mh_extract_patch_apply(const char *source,
	const char *staging, int overlay, mh_extract_progress progress,
	void *context, struct mh_error *error_out);

#endif
