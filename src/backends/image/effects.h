#ifndef MH_IMAGE_EFFECTS_H
#define MH_IMAGE_EFFECTS_H

#include "engine/core/error.h"
#include "engine/core/file.h"

struct mh_resource_source;

/*
 * Adapt installed software artwork only when its hardware counterpart is
 * absent.
 */
enum mh_error_code	mh_image_effect_fallback(
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_file *output,
	struct mh_error *error_out);

#endif
