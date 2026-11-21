#ifndef MH_MENU_NOISE_H
#define MH_MENU_NOISE_H

#include "interfaces/image/decoder.h"

struct mh_resource_source;

#define MH_MENU_NOISE_BYTES 65536

/*
 * Consume bottom-to-top rows of 256x256 palette indices, rebased as
 * 192+(index>>3). Input uses the decoder's canonical top-to-bottom order.
 * Palette colors are not sampled. Failure preserves output.
 */
enum mh_error_code	mh_menu_noise_prepare(
	const struct mh_indexed_image *image,
	unsigned char output[MH_MENU_NOISE_BYTES], struct mh_error *error_out);

/* Decoder and resources are borrowed for this call; output owns no pointers. */
enum mh_error_code	mh_menu_noise_load_from(
	const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned char output[MH_MENU_NOISE_BYTES], struct mh_error *error_out);

#endif
