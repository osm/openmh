#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "backends/image/png_provider.h"
#include "engine/core/file.h"
#include "interfaces/image/encoder.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "PNG:%d: %s\n", __LINE__, #x);         \
			return 1;                                              \
		}                                                              \
	} while (0)

static unsigned long
read_be(const unsigned char *p)
{
	return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
		((unsigned long)p[2] << 8) | p[3];
}

static int
check_pixels(const struct mh_picture *picture, const unsigned char *expected)
{
	struct mh_file file;
	unsigned char decoded[18];

	uLongf length;
	size_t offset;
	unsigned long size;

	CHECK(mh_image_encode(mh_image_png_encoder(), picture, &file, NULL) ==
		MH_ERROR_NONE);
	CHECK(file.size >= 57 &&
		memcmp(file.data, "\211PNG\r\n\032\n", 8) == 0);
	CHECK(read_be(file.data + 16) == 2 && read_be(file.data + 20) == 2);
	CHECK(memcmp(file.data + 24, "\010\006\000\000\000", 5) == 0);
	offset = 8;

	while (offset < file.size) {
		CHECK(file.size - offset >= 12);
		size = read_be(file.data + offset);
		CHECK(size <= file.size - offset - 12);
		CHECK(read_be(file.data + offset + 8 + size) ==
			crc32(0, file.data + offset + 4, (uInt)(size + 4)));
		offset += size + 12;
	}

	CHECK(memcmp(file.data + 12, "IHDR", 4) == 0);
	CHECK(memcmp(file.data + 37, "IDAT", 4) == 0);
	CHECK(memcmp(file.data + file.size - 8, "IEND", 4) == 0);
	length = sizeof(decoded);
	CHECK(uncompress(decoded, &length, file.data + 41,
		read_be(file.data + 33)) == Z_OK);
	CHECK(length == sizeof(decoded));
	CHECK(decoded[0] == 0 && decoded[9] == 0);
	CHECK(memcmp(decoded + 1, expected, 8) == 0);
	CHECK(memcmp(decoded + 10, expected + 8, 8) == 0);
	free(file.data);

	return 0;
}

static int
check_formats(void)
{
	static const unsigned char rgba[] = {255, 0, 0, 255, 0, 255, 0, 127, 42,
		42, 42, 42, 0, 0, 255, 0, 12, 34, 56, 78};

	static const unsigned char expected_rgba[] = {
		255, 0, 0, 255, 0, 255, 0, 127, 0, 0, 255, 0, 12, 34, 56, 78};

	static const unsigned char palette[] = {
		255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

	static const unsigned char indexed[] = {0, 1, 99, 2, 3};
	static const unsigned char rgb565[] = {
		0, 248, 224, 7, 42, 42, 31, 0, 255, 255};

	static const unsigned char rgb555[] = {
		0, 124, 224, 3, 42, 42, 31, 0, 255, 127};

	static const unsigned char expected_rgb[] = {255, 0, 0, 255, 0, 255, 0,
		255, 0, 0, 255, 255, 255, 255, 255, 255};

	struct mh_picture picture;

	memset(&picture, 0, sizeof(picture));
	picture.width = picture.height = 2;
	picture.format = MH_PICTURE_RGBA8;
	picture.pixels = rgba;
	picture.pitch = 12;
	picture.pixel_size = sizeof(rgba);
	CHECK(check_pixels(&picture, expected_rgba) == 0);
	picture.format = MH_PICTURE_INDEXED;
	picture.pixels = indexed;
	picture.pitch = 3;
	picture.pixel_size = sizeof(indexed);
	picture.palette = palette;
	picture.palette_size = sizeof(palette);
	picture.palette_count = 4;
	CHECK(check_pixels(&picture, expected_rgb) == 0);
	picture.format = MH_PICTURE_RGB565;
	picture.pixels = rgb565;
	picture.pitch = 6;
	picture.pixel_size = sizeof(rgb565);
	CHECK(check_pixels(&picture, expected_rgb) == 0);
	picture.format = MH_PICTURE_RGB555;
	picture.pixels = rgb555;
	CHECK(check_pixels(&picture, expected_rgb) == 0);

	return 0;
}

static int
check_invalid(void)
{
	static const unsigned char pixels[] = {1, 2, 3, 4};
	struct mh_picture picture;
	struct mh_file file;
	unsigned int i;

	for (i = 0; i < 7; i++) {
		memset(&picture, 0, sizeof(picture));
		picture.width = picture.height = 1;
		picture.format = MH_PICTURE_RGBA8;
		picture.pixels = pixels;
		picture.pixel_size = sizeof(pixels);

		switch (i) {
		case 0:
			picture.width = 0;
			break;

		case 1:
			picture.width = picture.height = 65535;
			break;

		case 2:
			picture.pixel_size = 3;
			break;

		case 3:
			picture.height = 2;
			picture.pitch = (size_t)-1;
			break;

		case 4:
			picture.format = MH_PICTURE_INDEXED;
			break;

		case 5:
			picture.format = MH_PICTURE_INDEXED;
			picture.palette = pixels;
			picture.palette_size = 3;
			picture.palette_count = 1;
			break;

		case 6:
			picture.pixels = NULL;
			break;
		}

		file.data = NULL;
		file.size = 123;
		CHECK(mh_image_encode(mh_image_png_encoder(), &picture, &file,
			NULL) != MH_ERROR_NONE);
		CHECK(file.data == NULL && file.size == 123);
	}

	return 0;
}

int
main(void)
{
	CHECK(strcmp(mh_image_encoder_extension(mh_image_png_encoder()),
		"png") == 0);
	CHECK(check_formats() == 0);
	CHECK(check_invalid() == 0);
	puts("PNG formats, scanline orientation, padding, CRCs and invalid input passed");

	return 0;
}
