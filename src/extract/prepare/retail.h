#ifndef MH_EXTRACT_PREPARE_RETAIL_H
#define MH_EXTRACT_PREPARE_RETAIL_H

#include "engine/core/error.h"

/* Add embedded resources before publishing a retail tree. */
enum mh_error_code	mh_extract_retail_prepare(const char *staging,
	struct mh_error *error_out);

#endif
