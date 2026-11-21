#include <stdio.h>
#include <string.h>

#include "engine/render/mipmap.h"

static int
check_palette_reduction(void)
{
	unsigned char source[64], pixels[16], saved[16];
	struct mh_image image, mip;

	memset(source, 255, sizeof(source));
	memset(source, 0, 4);
	image.width = image.height = 4;
	image.rgba = source;
	mip.width = mip.height = 2;
	mip.rgba = pixels;

	if (mh_mipmap_palette(&image, 1, &mip, NULL) != MH_ERROR_NONE ||
		memcmp(pixels, source, 4) != 0 ||
		memcmp(pixels + 4, source + 8, 12) != 0)
		return 1;

	memcpy(saved, pixels, sizeof(saved));

	if (mh_mipmap_palette(&image, 2, &mip, NULL) != MH_ERROR_INVALID ||
		memcmp(saved, pixels, sizeof(saved)) != 0)
		return 1;

	mip.width = mip.height = 1;

	if (mh_mipmap_palette(&image, 2, &mip, NULL) != MH_ERROR_NONE ||
		memcmp(pixels, source, 4) != 0 ||
		memcmp(pixels + 4, saved + 4, 12) != 0)
		return 1;

	return 0;
}

int
main(void)
{
	unsigned char source[256], pixels[64], saved[64];
	struct mh_image image, mip;
	unsigned int sample, i, level;

	if (check_palette_reduction() != 0)
		return 1;

	image.width = image.height = 8;
	image.rgba = source;
	mip.rgba = pixels;

	for (sample = 0; sample < 32; sample++) {
		for (i = 0; i < sizeof(source); i++)
			source[i] =
				(unsigned char)((i * 37 + sample * 19 + i * i) &
					255);

		for (level = 1; level < MH_TEXTURE_MIP_LEVELS; level++) {
			mip.width = mip.height = 8U >> level;
			memset(pixels, 0, sizeof(pixels));

			if (mh_mipmap_rgba(&image, level, &mip, NULL) !=
				MH_ERROR_NONE)
				return 1;
		}
	}

	/* Level two must average the base, not already truncated level one. */
	memset(source, 0, sizeof(source));
	source[0] = source[8] = source[64] = 3;
	source[72] = 7;
	mip.width = mip.height = 2;

	if (mh_mipmap_rgba(&image, 2, &mip, NULL) != MH_ERROR_NONE ||
		pixels[0] != 1)
		return 1;

	memcpy(saved, pixels, sizeof(saved));
	mip.width = 3;

	if (mh_mipmap_rgba(&image, 2, &mip, NULL) != MH_ERROR_INVALID ||
		memcmp(saved, pixels, sizeof(saved)) != 0)
		return 1;

	puts("Mip levels: RGBA averaging, palette selection, truncation and invalid dimensions passed");

	return 0;
}
