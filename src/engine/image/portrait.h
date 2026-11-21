#ifndef MH_IMAGE_PORTRAIT_H
#define MH_IMAGE_PORTRAIT_H

#include "interfaces/image/decoder.h"

#define MH_PORTRAIT_WIDTH 90
#define MH_PORTRAIT_HEIGHT 56
#define MH_PORTRAIT_PIXELS (MH_PORTRAIT_WIDTH * MH_PORTRAIT_HEIGHT)

struct mh_resource_source;

/*
 * Convert decoded 90x56 grayscale RGBA to the original opaque 32-level ramp.
 * Requires equal RGB channels; alpha is ignored. Validation leaves pixels
 * unchanged on error. This does not allocate or take ownership.
 */
enum mh_error_code	mh_portrait_prepare(struct mh_image *image,
	struct mh_error *error_out);

/* Original missing-file presentation. Caller frees output->rgba. */
enum mh_error_code	mh_portrait_blank(struct mh_image *output,
	struct mh_error *error_out);

/*
 * Selected decoder/resources are borrowed during loading. Missing/unreadable
 * files produce opaque black. Lookup failures and malformed readable images
 * fail. Caller frees output->rgba; failures leave output unchanged.
 */
enum mh_error_code	mh_portrait_load_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_image *output,
	struct mh_error *error_out);

#endif
