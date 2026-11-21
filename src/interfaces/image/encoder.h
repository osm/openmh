#ifndef MH_IMAGE_ENCODER_H
#define MH_IMAGE_ENCODER_H

#include "engine/core/error.h"
#include "interfaces/image/pixels.h"

struct mh_file;
struct mh_image_encoder;

/* Lowercase extension without a dot; borrowed through provider lifetime. */
const char	*mh_image_encoder_extension(
	const struct mh_image_encoder *encoder);

/*
 * Encode a borrowed pixel/palette view without reading or writing storage.
 * Success transfers malloc-owned output bytes; failure preserves output.
 * The provider and view remain borrowed for this call only. Encoders must
 * validate dimensions, pitches and spans before reading pixels.
 */
enum mh_error_code	mh_image_encode(const struct mh_image_encoder *encoder,
	const struct mh_picture *picture, struct mh_file *output,
	struct mh_error *error_out);

#endif
