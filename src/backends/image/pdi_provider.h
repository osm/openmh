#ifndef MH_IMAGE_PDI_PROVIDER_H
#define MH_IMAGE_PDI_PROVIDER_H

#include <stddef.h>

#include "engine/core/error.h"

struct mh_image_source_provider;
struct mh_image_collection;

/* Process-lifetime provider for named images in original PDI collections. */
const struct mh_image_source_provider	*mh_image_pdi_source(void);

/* Copy and validate original bytes for native inspection/reference tools. */
enum mh_error_code	mh_image_pdi_open(const unsigned char *data,
	size_t size, struct mh_image_collection **output,
	struct mh_error *error_out);

#endif
