#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_resource.h"
#include "engine/ui/lobby_screen.h"
#include "engine/ui/menu_backdrop.h"
#include "engine/ui/menu_noise.h"
#include "interfaces/image/decoder_backend.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "backdrop image:%d: %s\n", __LINE__,   \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	IMAGE_WIDTH = 640,
	IMAGE_HEIGHT = 400,
	IMAGE_PIXELS = IMAGE_WIDTH * IMAGE_HEIGHT,
	MENU_PIXELS = 960 * 640,
	MENU_ORIGIN = 100 * 960 + 160
};

struct fixture {
	unsigned int	calls;
	int		invalid_size, invalid_index;
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
		"fixture provides indexed artwork only");
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

	if (size != 1 || (data[0] != 'M' && data[0] != 'L' && data[0] != 'N'))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid fixture artwork");

	memset(&image, 0, sizeof(image));
	image.width = fixture->invalid_size ? 1 : IMAGE_WIDTH;
	image.height = IMAGE_HEIGHT;

	if (data[0] == 'N') {
		image.width = fixture->invalid_size ? 1 : 256;
		image.height = 256;
	}

	image.colors = data[0] == 'M' ? 63 : 256;
	image.size = (size_t)image.width * image.height;
	image.indices = malloc(image.size);

	if (image.indices == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"fixture backdrop allocation");

	for (i = 0; i < image.size; i++)
		image.indices[i] = data[0] == 'N'
			? (unsigned char)((i / 256 + i % 256) & 255)
			: (unsigned char)(i % 63);

	for (i = 0; i < sizeof(image.palette); i++)
		image.palette[i] = (unsigned char)(i * 7);

	if (fixture->invalid_index)
		image.indices[image.size - 1] = 255;

	*output = image;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(void)
{
	static const struct mh_image_decoder_ops ops = {
		read_rgba, read_indexed};

	static const struct mh_resource_entry entries[] = {
		{"converted/Data/back.raw", (const unsigned char *)"M", 1},
		{"converted/Data/Server.raw", (const unsigned char *)"L", 1},
		{"converted/Data/LastRace.raw", (const unsigned char *)"L", 1},
		{"converted/Data/Noise.raw", (const unsigned char *)"N", 1}};

	struct mh_image_decoder_provider decoder;
	const struct mh_resource_source *resources;
	struct mh_menu_backdrop menu, saved_menu;
	struct mh_lobby_backdrop lobby, saved_lobby, statistics;
	struct fixture fixture;
	unsigned char noise[MH_MENU_NOISE_BYTES];
	size_t i;
	unsigned int x, y, expected;

	memset(&fixture, 0, sizeof(fixture));
	decoder.ops = &ops;
	decoder.context = &fixture;
	decoder.read = NULL;
	decoder.extension = "raw";
	CHECK(mh_memory_resources_open(entries, 4, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_menu_backdrop_load_from(&decoder, resources, "converted",
		&menu, NULL) == MH_ERROR_NONE);
	CHECK(mh_lobby_backdrop_load_from(&decoder, resources, "converted",
		"Data/Server.tga", &lobby, NULL) == MH_ERROR_NONE);
	CHECK(mh_lobby_backdrop_load_from(&decoder, resources, "converted",
		"Data/LastRace.TGA", &statistics, NULL) == MH_ERROR_NONE);
	CHECK(mh_menu_noise_load_from(&decoder, resources, "converted", noise,
		NULL) == MH_ERROR_NONE);
	CHECK(fixture.calls == 4);

	for (i = 0; i < sizeof(noise); i++)
		CHECK(noise[i] ==
			192 + (((255 - i / 256 + i % 256) & 255) >> 3));

	CHECK(memcmp(statistics.pixels, lobby.pixels, IMAGE_PIXELS) == 0);
	CHECK(memcmp(statistics.palette, lobby.palette,
		sizeof(lobby.palette)) == 0);
	mh_lobby_backdrop_free(&statistics);

	for (i = 0; i < MENU_PIXELS; i++) {
		x = (unsigned int)(i % 960);
		y = (unsigned int)(i / 960);
		expected = 192;

		if (y >= 100 && y < 500 && x >= 160 && x < 800)
			expected += ((y - 100) * IMAGE_WIDTH + x - 160) % 63;

		CHECK(menu.pixels[i] == expected);
	}

	for (i = 0; i < IMAGE_PIXELS; i++)
		CHECK(lobby.pixels[i] == i % 63);

	for (i = 0; i < 63 * 3; i++)
		CHECK(menu.palette[i] == (unsigned char)(i * 7));

	CHECK(memcmp(menu.palette + 189, menu.palette + 186, 3) == 0);

	for (i = 0; i < 241 * 3; i++)
		CHECK(lobby.palette[i] == (unsigned char)(i * 7));

	CHECK(lobby.palette[723] == 224 && lobby.palette[724] == 104 &&
		lobby.palette[725] == 32);
	CHECK(lobby.palette[726] == 255 && lobby.palette[728] == 255);

	for (i = 729; i < sizeof(lobby.palette); i++)
		CHECK(lobby.palette[i] == 0);

	saved_menu = menu;
	saved_lobby = lobby;
	fixture.invalid_size = 1;
	memset(noise, 37, sizeof(noise));
	CHECK(mh_menu_noise_load_from(&decoder, resources, "converted", noise,
		NULL) == MH_ERROR_INVALID);

	for (i = 0; i < sizeof(noise); i++)
		CHECK(noise[i] == 37);

	CHECK(mh_menu_backdrop_load_from(&decoder, resources, "converted",
		&menu, NULL) == MH_ERROR_INVALID);
	CHECK(mh_lobby_backdrop_load_from(&decoder, resources, "converted",
		"Data/LastRace.TGA", &lobby, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&menu, &saved_menu, sizeof(menu)) == 0);
	CHECK(memcmp(&lobby, &saved_lobby, sizeof(lobby)) == 0);
	fixture.invalid_size = 0;
	fixture.invalid_index = 1;
	CHECK(mh_menu_backdrop_load_from(&decoder, resources, "converted",
		&menu, NULL) == MH_ERROR_INVALID);
	CHECK(mh_lobby_backdrop_load_from(&decoder, resources, "converted",
		"Data/LastRace.TGA", &lobby, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&menu, &saved_menu, sizeof(menu)) == 0);
	CHECK(memcmp(&lobby, &saved_lobby, sizeof(lobby)) == 0);
	mh_memory_resources_close(resources);
	CHECK(menu.pixels[MENU_ORIGIN + 1] == 193 && lobby.pixels[1] == 1);
	mh_menu_backdrop_free(&menu);
	mh_lobby_backdrop_free(&lobby);
	puts("Menu/lobby artwork and noise use selected decoding without TGA files; palette, row order and ownership pass");

	return 0;
}
