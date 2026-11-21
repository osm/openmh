#ifndef MH_ASSETS_ICON_H
#define MH_ASSETS_ICON_H

#include "engine/core/error.h"
#include "interfaces/image/pixels.h"

struct mh_resource_source;

/*
 * Original installation icon, then preserved disc icon, then motor.exe.
 * Caller frees output->rgba. Failure leaves output unchanged.
 */
enum mh_error_code	mh_icon_load_from(
	const struct mh_resource_source *resources, const char *root,
	struct mh_image *output, struct mh_error *error_out);

#endif
