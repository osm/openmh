#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/input/screenshot.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Screenshot line %d: %s\n", __LINE__,  \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	unsigned long	occupied;
	int		lobby;
	unsigned int	read_ok, write_ok, probes, locks, writes, unlocks, index;
};

static enum mh_error_code
exists(void *context, const char *name, int *present,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	char *end;
	unsigned long index;

	fixture = (struct fixture *)context;

	if (strncmp(name, fixture->lobby ? "lobby" : "motor", 5) != 0)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "bad screenshot prefix");

	index = strtoul(name + 5, &end, 10);

	if (strcmp(end, ".tga") != 0 || index > 32)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"bad screenshot suffix/index");

	fixture->probes++;
	fixture->index = (unsigned int)index;
	*present = index < 32 && (fixture->occupied & (1UL << index)) != 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
save(void *context, const char *name, struct mh_error *error_out)
{
	struct fixture *fixture;

	(void)name;
	fixture = (struct fixture *)context;
	fixture->locks++;

	if (!fixture->read_ok)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "readback failed");

	fixture->writes++;
	fixture->unlocks++;

	return fixture->write_ok
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_error_set(error_out, MH_ERROR_FAILED, "write failed");
}

int
main(void)
{
	struct mh_picture picture;
	struct mh_file file, before;
	struct mh_screenshot state;
	struct mh_screenshot_io io;
	struct mh_screenshot_result result;
	struct mh_binding_input input;
	struct fixture fixture;
	unsigned char pixels[12] = {
		0, 248, 224, 7, 77, 88, 31, 0, 255, 255, 99, 100};

	unsigned char palette[3] = {11, 22, 33};

	memset(&picture, 0, sizeof(picture));
	picture.width = picture.height = 2;
	picture.pitch = 6;
	picture.pixel_size = sizeof(pixels);
	picture.format = MH_PICTURE_RGB565;
	picture.pixels = pixels;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) == MH_ERROR_NONE);
	CHECK(file.size == 26 && file.data[2] == 2 && file.data[16] == 16 &&
		file.data[17] == 0);
	CHECK(mh_read_u16le(file.data + 18) == 31 &&
		mh_read_u16le(file.data + 20) == 0x7fff);
	CHECK(mh_read_u16le(file.data + 22) == 0x7c00 &&
		mh_read_u16le(file.data + 24) == 0x3e0);
	before = file;
	picture.pixel_size = 9;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) != MH_ERROR_NONE &&
		file.data == before.data && file.size == before.size);
	picture.pixel_size = 12;
	picture.pitch = (size_t)-1;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) != MH_ERROR_NONE &&
		file.data == before.data);
	picture.pitch = 4;
	picture.width = 65536U;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) != MH_ERROR_NONE &&
		file.data == before.data);
	picture.width = picture.height = MH_U16_MAX;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) != MH_ERROR_NONE &&
		file.data == before.data);
	picture.width = picture.height = 2;
	picture.format = MH_PICTURE_INDEXED;
	picture.palette = palette;
	picture.palette_count = 1;
	picture.palette_size = 2;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) != MH_ERROR_NONE &&
		file.data == before.data);
	picture.palette_size = 3;
	picture.palette_count = 257;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) != MH_ERROR_NONE &&
		file.data == before.data);
	picture.palette_count = 1;
	picture.format = (enum mh_picture_format)2;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) != MH_ERROR_NONE &&
		file.data == before.data);
	free(file.data);
	picture.format = MH_PICTURE_INDEXED;
	CHECK(mh_image_tga_encode(&picture, &file, NULL) == MH_ERROR_NONE &&
		file.size == 25);
	CHECK(file.data[5] == 1 && file.data[6] == 1 && file.data[7] == 8 &&
		file.data[17] == 8);
	CHECK(file.data[18] == 33 && file.data[19] == 22 &&
		file.data[20] == 11);
	free(file.data);
	memset(&state, 0, sizeof(state));
	memset(&input, 0, sizeof(input));
	memset(&fixture, 0, sizeof(fixture));
	io.context = &fixture;
	io.exists = exists;
	io.save = save;
	input.down[0x58] = 128;
	fixture.occupied = 5;
	fixture.write_ok = 1;
	CHECK(mh_screenshot_poll(&state, 0x258, &input, &io, &result, NULL) !=
			MH_ERROR_NONE &&
		state.latched == 0);
	fixture.read_ok = 1;
	fixture.write_ok = 0;
	CHECK(mh_screenshot_poll(&state, 0x258, &input, &io, &result, NULL) !=
			MH_ERROR_NONE &&
		state.latched == 0);
	fixture.write_ok = 1;
	CHECK(mh_screenshot_poll(&state, 0x258, &input, &io, &result, NULL) ==
			MH_ERROR_NONE &&
		state.latched == 1);
	CHECK(result.saved && strcmp(result.name, "motor1.tga") == 0 &&
		fixture.locks == 3);
	CHECK(mh_screenshot_poll(&state, 0x258, &input, &io, &result, NULL) ==
			MH_ERROR_NONE &&
		!result.saved && fixture.locks == 3);
	input.down[0x58] = 0;
	CHECK(mh_screenshot_poll(&state, 0x258, &input, &io, &result, NULL) ==
			MH_ERROR_NONE &&
		state.latched == 0);
	input.down[0x58] = 128;
	CHECK(mh_screenshot_poll(&state, 0x258, &input, &io, &result, NULL) ==
			MH_ERROR_NONE &&
		result.saved && fixture.locks == 4);

	return 0;
}
