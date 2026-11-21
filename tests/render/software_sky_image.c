#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_fog.h"
#include "backends/render/software_sky_image.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_software_sky_image image, saved;
	unsigned char pixels[12];
	unsigned int width, height;
	size_t i;

	CHECK(mh_software_sky_size(1024, 196, 320, 200, &width, &height,
		NULL) == MH_ERROR_NONE &&
		width == 320 && height == 80);
	CHECK(mh_software_sky_size(0, 196, 320, 200, &width, &height, NULL) ==
			MH_ERROR_INVALID &&
		width == 320 && height == 80);
	CHECK(mh_software_sky_size(1024, 196, 320, 1, &width, &height, NULL) ==
			MH_ERROR_RANGE &&
		width == 320 && height == 80);
	memset(&image, 0, sizeof(image));
	saved = image;
	memset(pixels, 255, sizeof(pixels));
	CHECK(mh_software_sky_resize(pixels, sizeof(pixels) - 1, 2, 2, 1, 1, 6,
		&image, NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_sky_resize(pixels, sizeof(pixels), 2, 2, 1, 1, 7,
		&image, NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_sky_resize(pixels, sizeof(pixels), 2, 2, 0, 1, 6,
		&image, NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_sky_resize(pixels, sizeof(pixels), 2, 2, 4097, 1, 6,
		&image, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	CHECK(mh_software_sky_resize(pixels, sizeof(pixels), 2, 2, 3, 3, 6,
		&image, NULL) == MH_ERROR_NONE);
	CHECK(image.width == 3 && image.height == 3 && image.size == 18);

	for (i = 0; i < image.size; i++)
		CHECK(image.pixels[i] == 255);

	free(image.pixels);
	CHECK(mh_software_sky_resize(pixels, sizeof(pixels), 2, 2, 1, 1, 5,
		&image, NULL) == MH_ERROR_NONE);
	CHECK(image.pixels[0] == 255 && image.pixels[1] == 127);
	free(image.pixels);

	/* The first horizontal weight uses the BGR byte offset, not column. */
	memset(pixels, 0, sizeof(pixels));
	pixels[5] = 64;
	pixels[8] = 248;
	CHECK(mh_software_sky_resize(pixels, 9, 3, 1, 2, 1, 6, &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(image.pixels[2] == 0 && image.pixels[3] == 64);
	free(image.pixels);

	return 0;
}
