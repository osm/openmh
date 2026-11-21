#ifndef MH_IMAGE_COLLECTION_LOAD_H
#define MH_IMAGE_COLLECTION_LOAD_H

#include "interfaces/image/collection.h"
#include "interfaces/image/decoder.h"

/* Read one logical member, decode it, and release its encoded bytes. */
enum mh_error_code	mh_image_collection_load(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_image_collection *collection, const char *name,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out);
enum mh_error_code	mh_image_collection_load_indexed(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_image_collection *collection, const char *name,
	struct mh_indexed_image *output, struct mh_error *error_out);

#endif
