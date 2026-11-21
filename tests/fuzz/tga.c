#include <stdlib.h>
#include <string.h>

#include "engine/assets/tga.h"

int	LLVMFuzzerTestOneInput(const unsigned char *data, size_t size);

int
LLVMFuzzerTestOneInput(const unsigned char *data, size_t size)
{
	struct mh_image image, before;
	struct mh_indexed_image indexed, before_indexed;
	int key;

	for (key = 0; key <= 1; key++) {
		memset(&image, 0, sizeof(image));
		memcpy(&before, &image, sizeof(before));

		if (mh_image_tga_decode(data, size, key, &image, NULL) ==
			MH_ERROR_NONE)
			free(image.rgba);
		else if (memcmp(&image, &before, sizeof(image)) != 0)
			abort();
	}

	memset(&indexed, 0, sizeof(indexed));
	memcpy(&before_indexed, &indexed, sizeof(before_indexed));

	if (mh_image_tga_decode_indexed(data, size, &indexed, NULL) ==
		MH_ERROR_NONE)
		free(indexed.indices);
	else if (memcmp(&indexed, &before_indexed, sizeof(indexed)) != 0)
		abort();

	return 0;
}
