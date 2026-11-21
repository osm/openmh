#include <stdlib.h>

#include "engine/assets/tga.h"
#include "engine/assets/tga_layout.h"
#include "engine/core/error.h"
#include "engine/image/portrait.h"

enum mh_error_code
mh_image_tga_decode_portrait(const unsigned char *data, size_t size,
	struct mh_image *output, struct mh_error *error_out)
{
	struct mh_image image;
	unsigned int x, y, source_row, gray;
	size_t pixel_offset;

	if (output == NULL || data == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid results portrait input");

	if (size < MH_TGA_HEADER_SIZE + MH_PORTRAIT_PIXELS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated results portrait");

	image.width = MH_PORTRAIT_WIDTH;
	image.height = MH_PORTRAIT_HEIGHT;
	image.rgba = (unsigned char *)malloc((size_t)MH_PORTRAIT_PIXELS * 4);

	if (image.rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding results portrait");

	for (y = 0; y < MH_PORTRAIT_HEIGHT; y++) {
		source_row = !(data[TGA_DESCRIPTOR] & TGA_TOP_ORIGIN)
			? MH_PORTRAIT_HEIGHT - 1 - y
			: y;

		for (x = 0; x < MH_PORTRAIT_WIDTH; x++) {
			gray = data[MH_TGA_HEADER_SIZE +
				source_row * MH_PORTRAIT_WIDTH + x];
			pixel_offset = ((size_t)y * MH_PORTRAIT_WIDTH + x) * 4;
			image.rgba[pixel_offset] =
				image.rgba[pixel_offset + 1] =
					image.rgba[pixel_offset + 2] =
						(unsigned char)gray;
			image.rgba[pixel_offset + 3] = 255;
		}
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
