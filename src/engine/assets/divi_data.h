#ifndef MH_VIDEO_DIVI_DATA_H
#define MH_VIDEO_DIVI_DATA_H

#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_resource_source;

/*
 * Read one DIVI payload through a prepared source. Only the fixed index and
 * selected entry are read; the source stays unchanged across those reads.
 * Success transfers malloc-owned bytes; failure leaves output unchanged.
 */
enum mh_error_code	mh_video_divi_read(
	const struct mh_resource_source *source, const char *path,
	unsigned int index, struct mh_file *output, struct mh_error *error_out);

#endif
