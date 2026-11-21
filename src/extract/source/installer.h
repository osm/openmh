#ifndef MH_EXTRACT_SOURCE_INSTALLER_H
#define MH_EXTRACT_SOURCE_INSTALLER_H

#include "extract/progress/report.h"

/* Retain all valid cabinet entries beneath their original component names. */
enum mh_error_code	mh_extract_installer_components(const void *bytes,
	size_t size, const char *staging, const char *relative,
	mh_extract_progress progress, void *context,
	struct mh_error *error_out);

#endif
