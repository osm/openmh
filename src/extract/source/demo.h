#ifndef MH_EXTRACT_SOURCE_DEMO_H
#define MH_EXTRACT_SOURCE_DEMO_H

#include "engine/core/file.h"
#include "extract/source/packages.h"

struct mh_resource_source;

struct mh_extract_demo {
	const struct mh_demo_package	*package;
	struct mh_file			file;
};

/* Hash the held bytes, not an earlier read of a mutable source path. */
enum mh_error_code	mh_extract_demo_read(
	const struct mh_resource_source *source, const char *path,
	struct mh_extract_demo *output, struct mh_error *error_out);
void			mh_extract_demo_free(struct mh_extract_demo *demo);

#endif
