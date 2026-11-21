#ifndef MH_BACKENDS_IMAGE_TGA_PROVIDER_H
#define MH_BACKENDS_IMAGE_TGA_PROVIDER_H

struct mh_image_encoder;
struct mh_image_decoder_provider;

/* Process-lifetime, stateless original-compatible TGA encoder. */
const struct mh_image_encoder	*mh_image_tga_encoder(void);

/* Process-lifetime descriptor for RGBA and indexed TGA decoding. */
const struct mh_image_decoder_provider	*mh_image_tga_decoder(void);

/* Original results gray-image layout, preserving its nonstandard TGA reads. */
const struct mh_image_decoder_provider	*mh_image_tga_portrait_decoder(void);

/* Original menu preview layout, including the fixed palette/pixel offsets. */
const struct mh_image_decoder_provider	*mh_image_tga_thumbnail_decoder(void);

#endif
