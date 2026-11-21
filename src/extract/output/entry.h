#ifndef MH_EXTRACT_OUTPUT_ENTRY_H
#define MH_EXTRACT_OUTPUT_ENTRY_H

#include "engine/archive/zip_write.h"

/* Keep streamed music/movies directly seekable; compress other assets. */
enum mh_error_code	mh_extract_package_add(struct mh_zip_writer *writer,
	const struct mh_resource_source *source, const char *path,
	const char *name, int directory, struct mh_error *error_out);

#endif
