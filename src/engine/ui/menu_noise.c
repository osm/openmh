#include <stdlib.h>

#include "engine/assets/path_menus.h"
#include "engine/image/load.h"
#include "engine/ui/menu_noise.h"

#define NOISE_SIDE 256
#define NOISE_PALETTE_BASE 192

enum mh_error_code
mh_menu_noise_prepare(const struct mh_indexed_image *image,
	unsigned char output[MH_MENU_NOISE_BYTES], struct mh_error *error_out)
{
	size_t source, destination;
	unsigned int x, y;

	if (image == NULL || image->indices == NULL || output == NULL ||
		image->width != NOISE_SIDE || image->height != NOISE_SIDE ||
		image->size != MH_MENU_NOISE_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid menu noise image");

	/*
	 * Preserve the original effect's row traversal after format decoding.
	 */
	for (y = 0; y < NOISE_SIDE; y++) {
		source = (size_t)(NOISE_SIDE - 1 - y) * NOISE_SIDE;
		destination = (size_t)y * NOISE_SIDE;

		for (x = 0; x < NOISE_SIDE; x++)
			output[destination + x] =
				(unsigned char)(NOISE_PALETTE_BASE +
					(image->indices[source + x] >> 3));
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_menu_noise_load_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	unsigned char output[MH_MENU_NOISE_BYTES], struct mh_error *error_out)
{
	struct mh_indexed_image image;
	enum mh_error_code status;

	status = mh_image_load_indexed_from(decoder, resources, root,
		MH_ASSET_MENU_NOISE, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_menu_noise_prepare(&image, output, error_out);
	free(image.indices);

	return status;
}
