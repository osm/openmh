#ifndef MH_IMAGE_ENCODER_BACKEND_H
#define MH_IMAGE_ENCODER_BACKEND_H

#include "interfaces/image/encoder.h"

/* Descriptor, extension and context remain borrowed unchanged through use. */
struct mh_image_encoder {
	const char		*extension;
	void			*context;
	enum mh_error_code (*encode)(void *context,
		const struct mh_picture *picture, struct mh_file *output,
		struct mh_error	*error_out);
};

#endif
