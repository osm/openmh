#ifndef MH_EXTRACT_COMPACT_H
#define MH_EXTRACT_COMPACT_H

#include "engine/archive/zip_write.h"
#include "extract/output/options.h"
#include "extract/progress/report.h"
#include "interfaces/media/encode.h"

enum mh_error_code	mh_extract_compact_available(
	const struct mh_extract_options *options, struct mh_error *error_out);

/* Add one file, converting only music WAVs and the movie collection. */
enum mh_error_code	mh_extract_compact_add(struct mh_zip_writer *writer,
	const struct mh_resource_source *resources, const char *path,
	const char *relative, mh_extract_progress progress, void *context,
	struct mh_error *error_out);

/* Retain DIVI index metadata and already-encoded WebM clips. */
enum mh_error_code	mh_extract_compact_movies(
	const struct mh_resource_source *resources, const char *path,
	mh_media_encode_check check, void *context, struct mh_file *output,
	struct mh_error *error_out);

#endif
