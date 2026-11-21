#ifndef MH_EXTRACT_PROGRESS_REPORT_H
#define MH_EXTRACT_PROGRESS_REPORT_H

#include "engine/assets/prepare_unpack.h"

enum mh_extract_step {
	MH_EXTRACT_POLL,
	MH_EXTRACT_UNPACK,
	MH_EXTRACT_READ_DISC,
	MH_EXTRACT_INSTALLER,
	MH_EXTRACT_MOVIES,
	MH_EXTRACT_COPY,
	MH_EXTRACT_READ_PACKAGE,
	MH_EXTRACT_PATCH,
	MH_EXTRACT_WRITE_PACKAGE,
	MH_EXTRACT_CONVERT,
	MH_EXTRACT_DISC_FILE,
	MH_EXTRACT_AUDIO,
	MH_EXTRACT_NOTICE
};

/*
 * Synchronous progress; returning an error cancels the operation. A zero total
 * means an unknown item count. POLL only checks cancellation. CONVERT announces
 * a media file; DISC_FILE and AUDIO report a fraction of the current
 * file/track, with path naming the track. NOTICE carries explanatory text in
 * path.
 */
typedef enum mh_error_code (*mh_extract_progress)(void *context,
	enum mh_extract_step step, const char *path, size_t completed,
	size_t total, struct mh_error *error_out);

/* Adapt archive enumeration without exposing presentation to the decoder. */
enum mh_error_code	mh_extract_unpack(struct mh_archive *archive,
	const char *root, enum mh_extract_step step,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
