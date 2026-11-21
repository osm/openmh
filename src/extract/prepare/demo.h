#ifndef MH_EXTRACT_PREPARE_DEMO_H
#define MH_EXTRACT_PREPARE_DEMO_H

#include "engine/core/error.h"

struct mh_demo_package;

/* Complete a verified demo in private staging before publication. */
enum mh_error_code	mh_extract_demo_prepare(
	const struct mh_demo_package *package, const char *staging,
	struct mh_error *error_out);

#endif
