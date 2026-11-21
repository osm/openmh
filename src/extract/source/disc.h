#ifndef MH_EXTRACT_SOURCE_DISC_H
#define MH_EXTRACT_SOURCE_DISC_H

#include "extract/progress/report.h"

/* Extract original files only; report available CD audio on success. */
enum mh_error_code	mh_extract_disc_unpack(const char *source,
	const char *staging, int *has_audio, mh_extract_progress progress,
	void *context, struct mh_error *error_out);

enum mh_error_code	mh_extract_disc_files(const char *directory,
	const char *staging, mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
