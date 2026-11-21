#ifndef MH_ASSETS_PREPARE_DEMO_CAR_H
#define MH_ASSETS_PREPARE_DEMO_CAR_H

#include "engine/core/error.h"
#include "engine/core/file.h"

/*
 * Convert a supplied demo's TBL05 binary car record into a TBL05 text CAR.
 * Package identification is the caller's responsibility. No retail defaults
 * or runtime tuning are applied. Caller frees output.data; unchanged on error.
 */
enum mh_error_code	mh_demo_car_prepare(const struct mh_file *source,
	struct mh_file *output, struct mh_error *error_out);

#endif
