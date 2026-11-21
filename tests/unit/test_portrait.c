#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/portrait.h"
#include "engine/core/error.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "portrait line %d: %s\n", __LINE__,    \
				#x);                                           \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
check_decoding(void)
{
	unsigned char data[18 + MH_PORTRAIT_PIXELS], byte;
	struct mh_image image, before, reference;
	unsigned int flags, i, x, y, expected;

	memset(data, 0, sizeof(data));
	data[2] = 3;
	data[12] = MH_PORTRAIT_WIDTH;
	data[14] = MH_PORTRAIT_HEIGHT;
	data[16] = 8;

	for (i = 0; i < MH_PORTRAIT_PIXELS; i++)
		data[18 + i] = (unsigned char)(i & 255U);

	for (flags = 0; flags < 256; flags++) {
		data[17] = (unsigned char)flags;
		CHECK(mh_portrait_tga_decode(data, sizeof(data), &image,
			NULL) == MH_ERROR_NONE);
		CHECK(image.width == MH_PORTRAIT_WIDTH &&
			image.height == MH_PORTRAIT_HEIGHT);

		for (i = 0; i < MH_PORTRAIT_PIXELS; i++) {
			x = i % MH_PORTRAIT_WIDTH;
			y = i / MH_PORTRAIT_WIDTH;

			if (!(flags & 32))
				y = MH_PORTRAIT_HEIGHT - 1 - y;

			expected = data[18 + y * MH_PORTRAIT_WIDTH + x] & 248U;
			CHECK(image.rgba[i * 4] == expected &&
				image.rgba[i * 4 + 1] == expected &&
				image.rgba[i * 4 + 2] == expected &&
				image.rgba[i * 4 + 3] == 255);
		}

		free(image.rgba);
	}

	CHECK(mh_portrait_tga_decode(data, sizeof(data), &reference, NULL) ==
		MH_ERROR_NONE);

	for (i = 0; i < 17; i++) {
		byte = data[i];
		data[i] ^= 255;
		CHECK(mh_portrait_tga_decode(data, sizeof(data), &image,
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(image.rgba, reference.rgba,
			(size_t)MH_PORTRAIT_PIXELS * 4) == 0);
		free(image.rgba);
		data[i] = byte;
	}

	free(reference.rgba);
	CHECK(mh_portrait_tga_decode(NULL, 0, &image, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_PORTRAIT_PIXELS; i++)
		CHECK(image.rgba[i * 4] == 0 && image.rgba[i * 4 + 1] == 0 &&
			image.rgba[i * 4 + 2] == 0 &&
			image.rgba[i * 4 + 3] == 255);

	free(image.rgba);
	memset(&image, 0x5a, sizeof(image));
	before = image;
	CHECK(mh_portrait_tga_decode(NULL, 1, &image, NULL) != MH_ERROR_NONE &&
		memcmp(&image, &before, sizeof(image)) == 0);
	CHECK(mh_portrait_tga_decode(data, sizeof(data), NULL, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < sizeof(data); i++)
		CHECK(mh_portrait_tga_decode(data, i, &image, NULL) !=
				MH_ERROR_NONE &&
			memcmp(&image, &before, sizeof(image)) == 0);
}

static void
check_preparation(void)
{
	unsigned char pixels[MH_PORTRAIT_PIXELS * 4],
		before[MH_PORTRAIT_PIXELS * 4];
	struct mh_image image;
	unsigned int i;

	image.width = MH_PORTRAIT_WIDTH;
	image.height = MH_PORTRAIT_HEIGHT;
	image.rgba = pixels;
	memset(pixels, 247, sizeof(pixels));
	pixels[sizeof(pixels) - 2] = 246;
	memcpy(before, pixels, sizeof(pixels));
	CHECK(mh_portrait_prepare(&image, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(before, pixels, sizeof(pixels)) == 0);
	pixels[sizeof(pixels) - 2] = 247;
	CHECK(mh_portrait_prepare(&image, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_PORTRAIT_PIXELS; i++) {
		CHECK(pixels[i * 4] == 240 && pixels[i * 4 + 1] == 240 &&
			pixels[i * 4 + 2] == 240 && pixels[i * 4 + 3] == 255);
	}
}

int
main(void)
{

	check_decoding();
	check_preparation();

	return failures != 0;
}
