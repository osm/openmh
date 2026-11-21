#ifndef MH_BACKENDS_IMAGE_PROVIDER_H
#define MH_BACKENDS_IMAGE_PROVIDER_H

struct mh_image_decoder_provider;

/* Process-lifetime TGA/ILBM selector retaining asset reference suffixes. */
const struct mh_image_decoder_provider	*mh_image_fallback_decoder(void);

#endif
