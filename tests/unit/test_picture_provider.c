#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/assets/tga.h"
#include "engine/input/screenshot.h"
#include "engine/storage/picture_store.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "picture provider:%d: %s\n", __LINE__, \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	SCREENSHOT_SCAN = 0x58
};

struct capture_fixture {
	struct mh_picture_store	*store;
	struct mh_file		picture;
	int			fail;
};

static enum mh_error_code
picture_exists(void *context, const char *name, int *present,
	struct mh_error *error_out)
{
	struct capture_fixture *capture;

	capture = context;

	return mh_picture_store_exists(
		capture->store, name, present, error_out);
}

static enum mh_error_code
save_picture(void *context, const char *name, struct mh_error *error_out)
{
	struct capture_fixture *capture;

	capture = context;

	if (capture->fail)
		return mh_error_set(
			error_out, MH_ERROR_IO, "injected capture failure");

	return mh_picture_store_save(
		capture->store, name, &capture->picture, error_out);
}

int
main(void)
{
	static const unsigned char pixels[] = {1, 2, 3, 4, 5, 6, 7, 8};
	static const unsigned char bottom_up[] = {5, 6, 7, 8, 1, 2, 3, 4};
	const struct mh_user_data *data, *other_data;
	struct mh_data_directory *directory;
	struct mh_picture_store *unchanged, *other_store;
	struct capture_fixture capture;
	struct mh_picture image;
	struct mh_file bytes;
	struct mh_screenshot state;
	struct mh_screenshot_result result, previous;
	struct mh_screenshot_io io;
	struct mh_binding_input input;
	int present;

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	CHECK(mh_memory_user_data_open(&other_data, NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(data, "Screenshots", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(data, "Screenshots", "assets",
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_picture_store_open_in(directory, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_data_directory_validate(directory, NULL) == MH_ERROR_NONE);
	unchanged = NULL;
	CHECK(mh_picture_store_open_in(NULL, &unchanged, NULL) ==
		MH_ERROR_INVALID);
	CHECK(unchanged == NULL);
	memset(&capture, 0, sizeof(capture));
	CHECK(mh_picture_store_open_in(directory, &capture.store, NULL) ==
		MH_ERROR_NONE);

	memset(&image, 0, sizeof(image));
	image.format = MH_PICTURE_RGB555;
	image.width = image.height = 2;
	image.pixels = pixels;
	image.pixel_size = sizeof(pixels);
	CHECK(mh_image_tga_encode(&image, &capture.picture, NULL) ==
		MH_ERROR_NONE);
	CHECK(capture.picture.size == MH_TGA_HEADER_SIZE + sizeof(pixels));
	CHECK(memcmp(capture.picture.data + MH_TGA_HEADER_SIZE, bottom_up,
		sizeof(bottom_up)) == 0);
	CHECK(mh_user_data_replace(data, "Screenshots/Motor0.TGA", "keep", 4,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_create_directory(
		data, "Screenshots/motor1.tga", NULL) == MH_ERROR_NONE);
	CHECK(mh_picture_store_save(capture.store, "motor0.tga",
		&capture.picture, NULL) == MH_ERROR_EXISTS);
	CHECK(mh_picture_store_save(capture.store, "motor1.tga",
		&capture.picture, NULL) == MH_ERROR_EXISTS);
	present = 42;
	CHECK(mh_picture_store_exists(capture.store, "../motor0.tga", &present,
		NULL) == MH_ERROR_INVALID &&
		present == 42);

	memset(&input, 0, sizeof(input));
	memset(&state, 0, sizeof(state));
	memset(&result, 0, sizeof(result));
	io.context = &capture;
	io.exists = picture_exists;
	io.save = save_picture;
	input.down[SCREENSHOT_SCAN] = 0x80;
	capture.fail = 1;
	previous = result;
	CHECK(mh_screenshot_poll(&state, MH_BINDING_KEYBOARD | SCREENSHOT_SCAN,
		&input, &io, &result, NULL) == MH_ERROR_IO);
	CHECK(!state.latched &&
		memcmp(&result, &previous, sizeof(result)) == 0);
	capture.fail = 0;
	CHECK(mh_screenshot_poll(&state, MH_BINDING_KEYBOARD | SCREENSHOT_SCAN,
		&input, &io, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.saved && state.latched &&
		strcmp(result.name, "motor2.tga") == 0);
	CHECK(mh_screenshot_poll(&state, MH_BINDING_KEYBOARD | SCREENSHOT_SCAN,
		&input, &io, &result, NULL) == MH_ERROR_NONE);
	CHECK(!result.saved);
	input.down[SCREENSHOT_SCAN] = 0;
	CHECK(mh_screenshot_poll(&state, MH_BINDING_KEYBOARD | SCREENSHOT_SCAN,
		&input, &io, &result, NULL) == MH_ERROR_NONE);
	input.down[SCREENSHOT_SCAN] = 0x80;
	CHECK(mh_screenshot_poll(&state, MH_BINDING_KEYBOARD | SCREENSHOT_SCAN,
		&input, &io, &result, NULL) == MH_ERROR_NONE);
	CHECK(result.saved && strcmp(result.name, "motor3.tga") == 0);
	CHECK(mh_user_data_read(data, "Screenshots/motor2.tga", 1024, &bytes,
		NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == capture.picture.size &&
		memcmp(bytes.data, capture.picture.data, bytes.size) == 0);
	free(bytes.data);

	/*
	 * Storage publishes arbitrary encoded bytes, not a TGA-shaped payload.
	 */
	bytes.data = (unsigned char *)"raw";
	bytes.size = 3;
	CHECK(mh_picture_store_save(capture.store, "motor0.raw", &bytes,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_user_data_read(data, "Screenshots/motor0.raw", 1024, &bytes,
		NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == 3 && memcmp(bytes.data, "raw", 3) == 0);
	free(bytes.data);
	CHECK(mh_picture_store_exists(capture.store, "motor0../raw", &present,
		NULL) == MH_ERROR_INVALID);

	/* Lobby names have their own sequence and use repeat-event latching. */
	state.latched = 0;
	CHECK(mh_user_data_replace(data, "Screenshots/Lobby0.TGA", "keep", 4,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_lobby_screenshot_poll(&state, 1, &io, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.saved && strcmp(result.name, "lobby1.tga") == 0);
	CHECK(mh_lobby_screenshot_poll(&state, 1, &io, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(!result.saved);
	CHECK(mh_lobby_screenshot_poll(&state, 0, &io, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(!state.latched);
	capture.fail = 1;
	CHECK(mh_lobby_screenshot_poll(&state, 1, &io, &result, NULL) ==
		MH_ERROR_IO);
	CHECK(!state.latched);
	capture.fail = 0;
	CHECK(mh_lobby_screenshot_poll(&state, 1, &io, &result, NULL) ==
		MH_ERROR_NONE);
	CHECK(result.saved && strcmp(result.name, "lobby2.tga") == 0);
	CHECK(mh_user_data_read(data, "Screenshots/lobby1.tga", 1024, &bytes,
		NULL) == MH_ERROR_NONE);
	CHECK(bytes.size == capture.picture.size &&
		memcmp(bytes.data, capture.picture.data, bytes.size) == 0);
	free(bytes.data);

	/*
	 * Equal logical roots in different providers retain independent files.
	 */
	CHECK(mh_user_data_create_directory(other_data, "Screenshots", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_user_data_open_directory(other_data, "Screenshots", "assets",
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(mh_picture_store_open_in(directory, &other_store, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_picture_store_exists(other_store, "motor2.tga", &present,
		NULL) == MH_ERROR_NONE &&
		!present);
	CHECK(mh_picture_store_save(other_store, "motor0.tga", &capture.picture,
		NULL) == MH_ERROR_NONE);
	mh_picture_store_close(other_store);
	mh_picture_store_close(capture.store);
	free(capture.picture.data);
	CHECK(mh_user_data_read(data, "Screenshots/Motor0.TGA", 1024, &bytes,
		NULL) == MH_ERROR_NONE);
	mh_memory_user_data_close(other_data);
	mh_memory_user_data_close(data);
	CHECK(bytes.size == 4 && memcmp(bytes.data, "keep", 4) == 0);
	free(bytes.data);

	return 0;
}
