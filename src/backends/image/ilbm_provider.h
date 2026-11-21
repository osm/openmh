#ifndef MH_BACKENDS_IMAGE_ILBM_PROVIDER_H
#define MH_BACKENDS_IMAGE_ILBM_PROVIDER_H

struct mh_image_decoder_provider;

/* Process-lifetime descriptor for indexed ILBM images and textures. */
const struct mh_image_decoder_provider	*mh_image_ilbm_decoder(void);

#endif
