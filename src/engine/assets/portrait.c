#include <stdlib.h>

#include "engine/assets/portrait.h"
#include "engine/assets/tga.h"

enum mh_error_code
mh_portrait_tga_decode(const unsigned char *data, size_t size,
	struct mh_image *output, struct mh_error *error_out)
{
	struct mh_image image;
	enum mh_error_code status;

	if (output == NULL || (data == NULL && size != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid results portrait input");

	if (data == NULL)
		return mh_portrait_blank(output, error_out);

	status = mh_image_tga_decode_portrait(data, size, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_portrait_prepare(&image, error_out);

	if (status != MH_ERROR_NONE) {
		free(image.rgba);
		return status;
	}

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
