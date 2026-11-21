#ifndef MH_BACKENDS_IMAGE_PNG_PROVIDER_H
#define MH_BACKENDS_IMAGE_PNG_PROVIDER_H

struct mh_image_encoder;

/* Process-lifetime, stateless lossless PNG encoder. */
const struct mh_image_encoder	*mh_image_png_encoder(void);

#endif
