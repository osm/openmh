#ifndef MH_EXTRACT_INSTALL_H
#define MH_EXTRACT_INSTALL_H

#include "extract/output/options.h"
#include "extract/progress/report.h"
#include "extract/source/input.h"

/*
 * Prepare a base and apply the supplied patches in order before publication.
 * A single patch input retains standalone-overlay and existing-output updates.
 */
enum mh_error_code	mh_extract_install(const struct mh_extract_input *input,
	const char *const *patches, size_t patch_count, const char *destination,
	int package, const struct mh_extract_options *options,
	mh_extract_progress progress, void *context, int *patched,
	struct mh_error *error_out);

#endif
