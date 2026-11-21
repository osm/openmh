#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/image/load.h"
#include "engine/ui/lobby_screen.h"

#define PIXEL_BYTES 256000UL
#define BACKDROP_WIDTH 640
#define BACKDROP_HEIGHT 400
#define PALETTE_COLORS 241
#define ORANGE_PALETTE_INDEX 241
#define WHITE_PALETTE_INDEX 242

enum mh_error_code
mh_lobby_backdrop_prepare(const struct mh_indexed_image *image,
	struct mh_lobby_backdrop *output, struct mh_error *error_out)
{
	struct mh_lobby_backdrop art;
	size_t i;

	if (image == NULL || output == NULL || image->indices == NULL ||
		image->width != BACKDROP_WIDTH ||
		image->height != BACKDROP_HEIGHT ||
		image->size != PIXEL_BYTES || image->colors < PALETTE_COLORS ||
		image->colors > MH_IMAGE_PALETTE_COLORS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby backdrop image");

	for (i = 0; i < image->size; i++)
		if (image->indices[i] >= PALETTE_COLORS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid lobby backdrop palette index");

	memset(&art, 0, sizeof(art));
	art.pixels = (unsigned char *)malloc(PIXEL_BYTES);

	if (art.pixels == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory loading lobby backdrop");

	memcpy(art.pixels, image->indices, PIXEL_BYTES);
	memcpy(art.palette, image->palette, PALETTE_COLORS * 3);

	art.palette[ORANGE_PALETTE_INDEX * 3] = 224;
	art.palette[ORANGE_PALETTE_INDEX * 3 + 1] = 104;
	art.palette[ORANGE_PALETTE_INDEX * 3 + 2] = 32;
	memset(art.palette + WHITE_PALETTE_INDEX * 3, 255, 3);
	*output = art;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_lobby_backdrop_load_from(const struct mh_image_decoder_provider *decoder,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_lobby_backdrop *output,
	struct mh_error *error_out)
{
	struct mh_indexed_image image;
	enum mh_error_code status;

	status = mh_image_load_indexed_from(
		decoder, resources, root, reference, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_lobby_backdrop_prepare(&image, output, error_out);
	free(image.indices);

	return status;
}

void
mh_lobby_backdrop_free(struct mh_lobby_backdrop *art)
{
	if (art != NULL) {
		free(art->pixels);
		memset(art, 0, sizeof(*art));
	}
}
