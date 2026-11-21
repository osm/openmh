#ifndef MH_EXTRACT_DRIVE_H
#define MH_EXTRACT_DRIVE_H

#include "extract/progress/report.h"

enum mh_error_code	mh_extract_drive_unpack(const char *path,
	const char *staging, int include_music, mh_extract_progress progress,
	void *context, struct mh_error *error_out);

#endif
