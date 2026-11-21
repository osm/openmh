#ifndef MH_IMAGE_DECODER_BACKEND_H
#define MH_IMAGE_DECODER_BACKEND_H

#include "engine/core/file.h"
#include "interfaces/image/decoder.h"

struct mh_resource_source;

struct mh_image_decoder_provider {
	const struct mh_image_decoder_ops	*ops;
	void					*context;

	/*
	 * NULL retains original suffixes; otherwise 1..16 lowercase
	 * letters/digits.
	 */
	const char				*extension;

	/*
	 * NULL selects normal file lookup. Inputs are borrowed for the call.
	 * Success transfers malloc-owned bytes; failure preserves output.
	 */
	enum mh_error_code (*read)(void *context,
		const struct mh_resource_source *resources, const char *root,
		const char *reference, struct mh_file *output,
		struct mh_error			*error_out);
};

/*
 * Descriptors/context and all inputs are borrowed only during a call. Success
 * transfers malloc-owned pixels; failure frees partial state and preserves
 * outputs. RGBA receives FILE or INDEX_ZERO: portable dispatch applies MAGENTA
 * after palette replacement. Palette output is optional and zero-initialized.
 * Unsupported indexed output or palette policies report UNSUPPORTED explicitly.
 */
struct mh_image_decoder_ops {
	enum mh_error_code (*rgba)(void *context, const unsigned char *data,
		size_t size, const struct mh_image_decode_options *options,
		struct mh_image *output,
		struct mh_image_palette *palette_output,
		struct mh_error	*error_out);
	enum mh_error_code (*indexed)(void *context, const unsigned char *data,
		size_t size, struct mh_indexed_image *output,
		struct mh_error	*error_out);
};

#endif
