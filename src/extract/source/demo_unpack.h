#ifndef MH_EXTRACT_SOURCE_DEMO_UNPACK_H
#define MH_EXTRACT_SOURCE_DEMO_UNPACK_H

#include "extract/progress/report.h"

struct mh_demo_package;

/*
 * Extract the playable payload and retain the original component tree/source.
 */
enum mh_error_code	mh_extract_demo_unpack(const char *source,
	const char *staging, const struct mh_demo_package **package,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
