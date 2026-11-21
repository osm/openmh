#ifndef MH_IMAGE_DECODER_H
#define MH_IMAGE_DECODER_H

#include "engine/core/error.h"
#include "interfaces/image/pixels.h"

struct mh_image_decoder_provider;

enum mh_image_alpha {
	MH_IMAGE_ALPHA_FILE,
	MH_IMAGE_ALPHA_INDEX_ZERO,
	MH_IMAGE_ALPHA_MAGENTA
};

struct mh_image_decode_options {
	enum mh_image_alpha		alpha;

	/* Optional replacement RGB palette, borrowed during decode. */
	const struct mh_image_palette	*palette;
};

/*
 * Decode bounded encoded bytes to owned, top-down RGBA. NULL options retain
 * authored alpha/palette. INDEX_ZERO replaces alpha with zero for source index
 * zero and 255 for other indices; true-color decoders must reject that request.
 * MAGENTA preserves authored alpha except at RGB(255,0,255), which becomes
 * zero. Optional palette_output receives the selected RGB palette, or count
 * zero for true-color content. No format state or input pointers survive the
 * call. Caller frees output->rgba. Both outputs stay unchanged on failure.
 */
enum mh_error_code	mh_image_decode(
	const struct mh_image_decoder_provider *provider,
	const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette_output, struct mh_error *error_out);

/*
 * Preserve source indices and RGB entries, including duplicate colors. No
 * quantization, mask, alpha or color-key policy is applied. Providers consume
 * and validate encoded masks but this view does not retain them. Use RGBA for
 * authored alpha. Caller frees indices; output is unchanged on failure.
 */
enum mh_error_code	mh_image_decode_indexed(
	const struct mh_image_decoder_provider *provider,
	const unsigned char *data, size_t size, struct mh_indexed_image *output,
	struct mh_error *error_out);

/*
 * Borrowed extension without dot; NULL means retain logical reference suffixes.
 */
enum mh_error_code	mh_image_decoder_extension(
	const struct mh_image_decoder_provider *provider, const char **output,
	struct mh_error *error_out);

#endif
