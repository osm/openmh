#ifndef MH_EXTRACT_SOURCE_PACKAGE_H
#define MH_EXTRACT_SOURCE_PACKAGE_H

#include "extract/progress/report.h"

/* Copy verified package bytes into an empty private staging tree. */
enum mh_error_code	mh_extract_package_unpack(const char *source,
	const char *staging, mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
