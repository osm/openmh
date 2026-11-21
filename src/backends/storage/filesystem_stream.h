#ifndef MH_FILESYSTEM_STREAM_H
#define MH_FILESYSTEM_STREAM_H

#include <stdio.h>

#include "engine/core/error.h"

/* Private file I/O. Open transfers a regular binary stream to the caller. */
enum mh_error_code	mh_filesystem_open_read(const char *path, FILE **output,
	struct mh_error *error_out);

/* Size leaves the stream position unchanged; seek uses an absolute offset. */
enum mh_error_code	mh_filesystem_stream_size(FILE *stream,
	unsigned long *output, struct mh_error *error_out);
enum mh_error_code	mh_filesystem_stream_seek(FILE *stream,
	unsigned long offset, struct mh_error *error_out);

#endif
