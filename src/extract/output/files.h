#ifndef MH_EXTRACT_OUTPUT_FILES_H
#define MH_EXTRACT_OUTPUT_FILES_H

#include "extract/progress/report.h"
#include "interfaces/storage/resource.h"

struct mh_filesystem_staging;

/* Paths and writes are confined to the caller's private staging tree. */
enum mh_error_code	mh_extract_write(const char *root, const char *relative,
	const struct mh_file *file, struct mh_error *error_out);
enum mh_error_code	mh_extract_copy(const char *source,
	const char *destination, mh_extract_progress progress, void *context,
	struct mh_error *error_out);

/*
 * Discard installer originals unless requested, then publish the staged tree.
 */
enum mh_error_code	mh_extract_publish_directory(
	struct mh_filesystem_staging *staging, int keep_original,
	struct mh_error *error_out);

#endif
