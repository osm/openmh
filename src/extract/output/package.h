#ifndef MH_EXTRACT_PACKAGE_H
#define MH_EXTRACT_PACKAGE_H

#include "extract/output/options.h"
#include "extract/progress/report.h"
#include "interfaces/storage/data_stream.h"

/* Pack a prepared installation, excluding Original/ and userdata/. */
enum mh_error_code	mh_extract_pack(const char *source,
	const char *destination, const struct mh_extract_options *options,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);

/* Borrow the empty stream; the caller seals and publishes the completed ZIP. */
enum mh_error_code	mh_extract_package_write(const char *source,
	struct mh_data_stream *stream, const struct mh_extract_options *options,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);

/* Open the held destination parent; name points into destination. */
enum mh_error_code	mh_extract_package_parent(const char *destination,
	const char *protected_source, struct mh_data_directory **directory,
	const char **name, struct mh_error *error_out);

#endif
