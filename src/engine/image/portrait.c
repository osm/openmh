#include <stdlib.h>
#include <string.h>

#include "engine/image/portrait.h"

#define GRAY_RAMP_MASK 248U

enum mh_error_code
mh_portrait_prepare(struct mh_image *image, struct mh_error *error_out)
{
	unsigned int i;
	unsigned char *pixel;

	if (image == NULL || image->rgba == NULL ||
		image->width != MH_PORTRAIT_WIDTH ||
		image->height != MH_PORTRAIT_HEIGHT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid results portrait dimensions");

	for (i = 0; i < MH_PORTRAIT_PIXELS; i++) {
		pixel = image->rgba + i * 4;

		if (pixel[0] != pixel[1] || pixel[0] != pixel[2])
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"results portrait must be grayscale");
	}

	/* Map gray into palette entries 224..255. */
	for (i = 0; i < MH_PORTRAIT_PIXELS; i++) {
		pixel = image->rgba + i * 4;
		pixel[0] = pixel[1] = pixel[2] =
			(unsigned char)(pixel[0] & GRAY_RAMP_MASK);
		pixel[3] = 255;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_portrait_blank(struct mh_image *output, struct mh_error *error_out)
{
	struct mh_image image;
	unsigned int i;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing results portrait output");

	image.width = MH_PORTRAIT_WIDTH;
	image.height = MH_PORTRAIT_HEIGHT;
	image.rgba = calloc((size_t)MH_PORTRAIT_PIXELS, 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding results portrait");

	for (i = 0; i < MH_PORTRAIT_PIXELS; i++)
		image.rgba[i * 4 + 3] = 255;

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
