#ifndef MH_IMAGE_PALETTE_SOURCE_H
#define MH_IMAGE_PALETTE_SOURCE_H

#include "engine/core/error.h"

#define MH_PALETTE_COLORS 256

struct mh_palette_source;
struct mh_resource_source;

/*
 * Return 256 ordered 0xRRGGBB values. Indices and duplicate colors are
 * significant; do not sort or quantize. Reference is a logical resource key.
 * Sources/resources are borrowed only during loading. Output owns its inline
 * values and is unchanged on failure.
 */
enum mh_error_code	mh_palette_load_from(
	const struct mh_palette_source *source,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, unsigned long output[MH_PALETTE_COLORS],
	struct mh_error *error_out);

#endif
