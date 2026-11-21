#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/tga_provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/ui/menu_thumbnail.h"
#include "interfaces/image/decoder_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "thumbnail:%d: %s\n", __LINE__, #e);   \
			return 1;                                              \
		}                                                              \
	} while (0)

#define IMAGE_WIDTH 324
#define IMAGE_ROWS 269
#define IMAGE_PIXELS ((size_t)IMAGE_WIDTH * IMAGE_ROWS)

struct fixture {
	unsigned int	rows, calls;
	int		invalid;
};

static enum mh_error_code
read_rgba(void *context, const unsigned char *data, size_t size,
	const struct mh_image_decode_options *options, struct mh_image *output,
	struct mh_image_palette *palette, struct mh_error *error_out)
{
	(void)context;

	(void)data;
	(void)size;
	(void)options;
	(void)output;
	(void)palette;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"fixture provides indexed previews");
}

static enum mh_error_code
read_indexed(void *context, const unsigned char *data, size_t size,
	struct mh_indexed_image *output, struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_indexed_image image;
	size_t i;

	fixture = context;
	fixture->calls++;

	if (size != 1 || data[0] != 29)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid replacement preview");

	memset(&image, 0, sizeof(image));
	image.width = fixture->invalid ? 1 : IMAGE_WIDTH;
	image.height = fixture->rows;
	image.colors = 256;
	image.size = (size_t)image.width * image.height;
	image.indices = malloc(image.size);

	if (image.indices == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"fixture preview allocation");

	for (i = 0; i < image.size; i++)
		image.indices[i] = (unsigned char)(128 + (i * 7) % 64);

	for (i = 0; i < sizeof(image.palette); i++)
		image.palette[i] = (unsigned char)(i * 11);

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_alternate(unsigned int rows)
{
	static const struct mh_image_decoder_ops ops = {
		read_rgba, read_indexed};

	static const unsigned char value = 29;
	struct mh_image_decoder_provider decoder;
	struct mh_resource_entry entry;
	const struct mh_resource_source *resources;
	struct mh_menu_thumbnail thumbnail, before;
	struct mh_menu_canvas canvas;
	struct fixture fixture;
	unsigned char pixels[IMAGE_WIDTH * IMAGE_ROWS];
	size_t i, center;

	memset(&fixture, 0, sizeof(fixture));
	fixture.rows = rows;
	decoder.ops = &ops;
	decoder.context = &fixture;
	decoder.read = NULL;
	decoder.extension = "raw";
	entry.path = "converted/preview.raw";
	entry.data = &value;
	entry.size = 1;
	CHECK(mh_memory_resources_open(&entry, 1, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_menu_thumbnail_load_from(&decoder, resources, "converted",
		"preview.tga", &thumbnail, NULL) == MH_ERROR_NONE);
	CHECK(fixture.calls == 1);

	for (i = 0; i < IMAGE_PIXELS; i++)
		CHECK(thumbnail.pixels[i] == 128 + (i * 7) % 64);

	for (i = 0; i < sizeof(thumbnail.palette); i++)
		CHECK(thumbnail.palette[i] ==
			(unsigned char)((128 * 3 + i) * 11));

	before = thumbnail;
	fixture.invalid = 1;
	CHECK(mh_menu_thumbnail_load_from(&decoder, resources, "converted",
		"preview.tga", &thumbnail, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&thumbnail, &before, sizeof(thumbnail)) == 0);
	CHECK(mh_menu_thumbnail_load_from(&decoder, resources, "converted",
		"absent.tga", &thumbnail, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(memcmp(&thumbnail, &before, sizeof(thumbnail)) == 0);
	mh_memory_resources_close(resources);
	memset(pixels, 77, sizeof(pixels));
	canvas.pixels = pixels;
	canvas.size = canvas.end = sizeof(pixels);
	canvas.begin = 0;
	canvas.pitch = IMAGE_WIDTH;
	CHECK(mh_menu_thumbnail_draw(&thumbnail, 162, 134, &canvas, NULL) ==
		MH_ERROR_NONE);
	center = (size_t)134 * IMAGE_WIDTH + 162;
	CHECK(pixels[center] == 128 + (center * 7) % 64);
	CHECK(pixels[0] == 77 && pixels[IMAGE_PIXELS - 1] == 77);
	mh_menu_thumbnail_free(&thumbnail);

	return 0;
}

static int
check_original(unsigned int colors)
{
	const struct mh_image_decoder_provider *decoder;
	struct mh_indexed_image image, before;
	struct mh_menu_thumbnail thumbnail;
	unsigned char *bytes;
	size_t i, size;

	size = 18 + colors * 3 + (size_t)IMAGE_WIDTH * 270;
	bytes = malloc(size);
	CHECK(bytes != NULL);
	memset(bytes, 0, 18);
	bytes[1] = bytes[2] = 1;
	bytes[5] = (unsigned char)colors;
	bytes[7] = 24;
	bytes[12] = 68;
	bytes[13] = 1;
	bytes[14] = 14;
	bytes[15] = 1;
	bytes[16] = 8;
	bytes[17] = 40;

	for (i = 18; i < size; i++)
		bytes[i] = (unsigned char)(i * 13);

	decoder = mh_image_tga_thumbnail_decoder();
	CHECK(mh_image_decode_indexed(decoder, bytes, size, &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(image.width == IMAGE_WIDTH && image.height == IMAGE_ROWS &&
		image.size == IMAGE_PIXELS && image.colors == 256);
	CHECK(memcmp(image.indices, bytes + 594, IMAGE_PIXELS) == 0);

	for (i = 0; i < 64; i++) {
		CHECK(image.palette[(128 + i) * 3] == bytes[402 + i * 3 + 2]);
		CHECK(image.palette[(128 + i) * 3 + 1] ==
			bytes[402 + i * 3 + 1]);
		CHECK(image.palette[(128 + i) * 3 + 2] == bytes[402 + i * 3]);
	}

	before = image;
	CHECK(mh_image_decode_indexed(decoder, bytes, size - 1, &image, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&image, &before, sizeof(image)) == 0);
	free(bytes);
	CHECK(mh_menu_thumbnail_prepare(&image, &thumbnail, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(thumbnail.pixels, image.indices, IMAGE_PIXELS) == 0);
	CHECK(memcmp(thumbnail.palette, image.palette + 384, 192) == 0);
	free(image.indices);
	CHECK(thumbnail.pixels[0] == (unsigned char)(594 * 13));
	mh_menu_thumbnail_free(&thumbnail);

	return 0;
}

int
main(void)
{
	CHECK(check_original(191) == 0);
	CHECK(check_original(192) == 0);
	CHECK(check_alternate(269) == 0);
	CHECK(check_alternate(270) == 0);
	puts("Thumbnail images: original fixed offsets, replacement resources, palette slice, oval drawing and ownership passed");

	return 0;
}
