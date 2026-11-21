#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/tga_provider.h"
#include "engine/assets/tga.h"
#include "engine/input/screenshot.h"
#include "interfaces/image/encoder_backend.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "image encoder:%d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	calls;
	int		fail;
	char		name[MH_SCREENSHOT_NAME_SIZE];
};

/* A lossless test format, not a PNG implementation. */
static enum mh_error_code
encode(void *context, const struct mh_picture *picture, struct mh_file *output,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct mh_file file;

	fixture = context;
	fixture->calls++;

	if (fixture->fail)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test encoder failure");

	if (picture->format != MH_PICTURE_RGBA8 || picture->width != 1 ||
		picture->height != 1 || picture->pixel_size < 4)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "test pixel view");

	file.data = malloc(4);

	if (file.data == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test allocation");

	memcpy(file.data, picture->pixels, 4);
	file.size = 4;
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
exists(void *context, const char *name, int *present,
	struct mh_error *error_out)
{
	(void)context;

	*present = strcmp(name, "motor0.raw") == 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
save(void *context, const char *name, struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;

	if (fixture->fail)
		return mh_error_set(
			error_out, MH_ERROR_IO, "test save failure");

	strcpy(fixture->name, name);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
check_encoding(void)
{
	static const unsigned char rgba[] = {19, 105, 251, 37};
	static const unsigned char packed[] = {0xbf, 0x09};
	struct fixture fixture;
	struct mh_image_encoder provider;
	struct mh_picture picture;
	struct mh_file file, legacy, sentinel;
	struct mh_error error;

	memset(&fixture, 0, sizeof(fixture));
	memset(&picture, 0, sizeof(picture));
	picture.format = MH_PICTURE_RGBA8;
	picture.width = picture.height = 1;
	picture.pixels = rgba;
	picture.pixel_size = sizeof(rgba);
	provider.extension = "raw";
	provider.context = &fixture;
	provider.encode = encode;
	CHECK(strcmp(mh_image_encoder_extension(&provider), "raw") == 0);
	CHECK(mh_image_encode(&provider, &picture, &file, NULL) ==
		MH_ERROR_NONE);
	CHECK(file.size == sizeof(rgba) &&
		memcmp(file.data, rgba, file.size) == 0);
	free(file.data);
	CHECK(mh_image_encode(mh_image_tga_encoder(), &picture, &file, NULL) ==
		MH_ERROR_NONE);
	CHECK(file.size == MH_TGA_HEADER_SIZE + 2);
	CHECK(memcmp(file.data + MH_TGA_HEADER_SIZE, packed, sizeof(packed)) ==
		0);
	picture.format = MH_PICTURE_RGB555;
	picture.pixels = packed;
	picture.pixel_size = sizeof(packed);
	CHECK(mh_image_tga_encode(&picture, &legacy, NULL) == MH_ERROR_NONE);
	CHECK(file.size == legacy.size &&
		memcmp(file.data, legacy.data, file.size) == 0);
	free(file.data);
	free(legacy.data);

	sentinel.data = NULL;
	sentinel.size = 123;
	file = sentinel;
	fixture.fail = 1;
	CHECK(mh_image_encode(&provider, &picture, &file, &error) ==
		MH_ERROR_IO);
	CHECK(file.data == sentinel.data && file.size == sentinel.size);
	CHECK(error.code == MH_ERROR_IO && fixture.calls == 2);
	provider.extension = "../raw";
	CHECK(mh_image_encoder_extension(&provider) == NULL);
	CHECK(mh_image_encode(&provider, &picture, &file, NULL) ==
		MH_ERROR_INVALID);
	CHECK(fixture.calls == 2);
	CHECK(mh_image_encode(NULL, &picture, &file, NULL) ==
		MH_ERROR_UNSUPPORTED);

	return 0;
}

/* Fixed wire bytes keep this check independent of the writer's layout enum. */
static int
check_indexed_tga(void)
{
	static const unsigned char header[] = {
		0, 1, 1, 0, 0, 0, 1, 24, 0, 0, 0, 0, 2, 0, 2, 0, 8, 32};

	unsigned char indices[] = {255, 1, 0, 1};
	struct mh_indexed_image image, decoded;
	struct mh_file file;
	unsigned int i;

	memset(&image, 0, sizeof(image));
	image.width = image.height = 2;
	image.indices = indices;
	image.size = sizeof(indices);

	for (i = 0; i < MH_IMAGE_PALETTE_COLORS; i++) {
		image.palette[i * 3] = (unsigned char)i;
		image.palette[i * 3 + 1] = (unsigned char)(255 - i);
		image.palette[i * 3 + 2] = (unsigned char)(i / 2);
	}

	CHECK(mh_image_tga_encode_indexed(&image, &file, NULL) ==
		MH_ERROR_NONE);
	CHECK(file.size ==
		sizeof(header) + sizeof(image.palette) + sizeof(indices));
	CHECK(memcmp(file.data, header, sizeof(header)) == 0);

	for (i = 0; i < MH_IMAGE_PALETTE_COLORS; i++) {
		CHECK(file.data[sizeof(header) + i * 3] ==
			image.palette[i * 3 + 2]);
		CHECK(file.data[sizeof(header) + i * 3 + 1] ==
			image.palette[i * 3 + 1]);
		CHECK(file.data[sizeof(header) + i * 3 + 2] ==
			image.palette[i * 3]);
	}

	CHECK(memcmp(file.data + sizeof(header) + sizeof(image.palette),
		indices, sizeof(indices)) == 0);
	CHECK(mh_image_tga_decode_indexed(
		file.data, file.size, &decoded, NULL) == MH_ERROR_NONE);
	CHECK(decoded.width == image.width && decoded.height == image.height);
	CHECK(memcmp(decoded.indices, indices, sizeof(indices)) == 0);
	CHECK(memcmp(decoded.palette, image.palette, sizeof(image.palette)) ==
		0);
	free(decoded.indices);
	free(file.data);

	file.data = NULL;
	file.size = 123;
	image.size--;
	CHECK(mh_image_tga_encode_indexed(&image, &file, NULL) ==
		MH_ERROR_INVALID);
	image.size++;
	image.width = 0;
	CHECK(mh_image_tga_encode_indexed(&image, &file, NULL) ==
		MH_ERROR_INVALID);
	image.width = 65536UL;
	CHECK(mh_image_tga_encode_indexed(&image, &file, NULL) ==
		MH_ERROR_INVALID);
	CHECK(file.data == NULL && file.size == 123);
	CHECK(mh_image_tga_encode_indexed(NULL, &file, NULL) ==
		MH_ERROR_INVALID);

	return 0;
}

static int
check_names(void)
{
	struct fixture fixture;
	struct mh_screenshot state;
	struct mh_screenshot_result result, previous;
	struct mh_screenshot_io io;
	struct mh_binding_input input;

	memset(&fixture, 0, sizeof(fixture));
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&result, 0, sizeof(result));
	io.context = &fixture;
	io.exists = exists;
	io.save = save;
	input.down[0x58] = 0x80;
	fixture.fail = 1;
	previous = result;
	CHECK(mh_screenshot_poll_format(&state, MH_BINDING_KEYBOARD | 0x58,
		&input, "raw", &io, &result, NULL) == MH_ERROR_IO);
	CHECK(!state.latched &&
		memcmp(&previous, &result, sizeof(result)) == 0);
	fixture.fail = 0;
	CHECK(mh_screenshot_poll_format(&state, MH_BINDING_KEYBOARD | 0x58,
		&input, "raw", &io, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.saved && strcmp(result.name, "motor1.raw") == 0);
	CHECK(strcmp(fixture.name, "motor1.raw") == 0);
	CHECK(mh_screenshot_poll_format(&state, MH_BINDING_KEYBOARD | 0x58,
		&input, "raw", &io, &result, NULL) == MH_ERROR_NONE);
	CHECK(!result.saved);
	CHECK(mh_lobby_screenshot_poll_format(
		&state, 0, NULL, &io, &result, NULL) == MH_ERROR_NONE);
	CHECK(!state.latched);
	CHECK(mh_lobby_screenshot_poll_format(
		&state, 1, "raw", &io, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.saved && strcmp(result.name, "lobby0.raw") == 0);

	return 0;
}

int
main(void)
{
	CHECK(check_encoding() == 0);
	CHECK(check_indexed_tga() == 0);
	CHECK(check_names() == 0);

	return 0;
}
