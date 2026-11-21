#ifndef MH_EXTRACT_CLI_EXTRACT_H
#define MH_EXTRACT_CLI_EXTRACT_H

#include "engine/core/error.h"
#include "extract/output/options.h"
#include "extract/progress/report.h"

/* Sources are one base followed by patches, or a single legacy source. */
enum mh_error_code	mh_extract_run(const char *const *sources, size_t count,
	const char *destination, const struct mh_extract_options *options,
	int *patched, mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
