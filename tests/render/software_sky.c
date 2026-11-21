#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_sky.h"

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
	struct mh_software_surface surface;
	struct mh_software_sky_image image;
	struct mh_software_sky_state state, saved;
	struct mh_software_sky_camera camera;
	unsigned char pixels[32], source[8];
	unsigned int i;
	int horizon;

	memset(&surface, 0, sizeof(surface));
	memset(&state, 0, sizeof(state));
	memset(&camera, 0, sizeof(camera));
	memset(pixels, 0xa5, sizeof(pixels));
	memset(source, 0xff, sizeof(source));
	surface.width = surface.height = 4;
	surface.pitch = 8;
	surface.size = sizeof(pixels);
	surface.pixels = pixels;
	image.width = image.height = 2;
	image.size = sizeof(source);
	image.pixels = source;
	camera.focal = 1;
	camera.forward[0] = 1;
	CHECK(mh_software_sky_horizon(199, .25f, &horizon, NULL) ==
			MH_ERROR_NONE &&
		horizon == -50);
	CHECK(mh_software_sky_horizon(0, .25f, &horizon, NULL) ==
			MH_ERROR_INVALID &&
		horizon == -50);
	saved = state;
	image.size--;
	CHECK(mh_software_sky_draw(&surface, &image, &camera, &state,
		MH_SOFTWARE_SKY_HISTORY, 1, 2, 0,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&saved, &state, sizeof(state)) == 0);

	for (i = 0; i < sizeof(pixels); i++)
		CHECK(pixels[i] == 0xa5);

	image.size++;
	CHECK(mh_software_sky_draw(&surface, &image, &camera, &state,
		MH_SOFTWARE_SKY_HISTORY, 1, 2, 0, NULL) == MH_ERROR_NONE);
	CHECK(state.top == 1);
	CHECK(pixels[0] == 1 && pixels[8] == 255 && pixels[24] == 2);
	saved = state;
	CHECK(mh_software_sky_draw(&surface, NULL, &camera, &state,
		MH_SOFTWARE_SKY_HISTORY, 1, 2, 0, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&saved, &state, sizeof(state)) == 0);

	for (i = 0; i < sizeof(pixels); i++)
		CHECK(pixels[i] == 0);

	/* Direct drawing leaves pitch padding and uses the configured clear. */
	surface.width = 3;
	memset(pixels, 0xa5, sizeof(pixels));
	CHECK(mh_software_sky_draw(&surface, NULL, &camera, &state,
		MH_SOFTWARE_SKY_DIRECT, 1, 2, 0x5a3c,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&saved, &state, sizeof(state)) == 0);

	for (i = 0; i < surface.height; i++) {
		CHECK(pixels[i * 8] == 0x3c && pixels[i * 8 + 1] == 0x5a);
		CHECK(pixels[i * 8 + 4] == 0x3c && pixels[i * 8 + 5] == 0x5a);
		CHECK(pixels[i * 8 + 6] == 0xa5 && pixels[i * 8 + 7] == 0xa5);
	}

	CHECK(mh_software_sky_draw(&surface, &image, &camera, &state,
		MH_SOFTWARE_SKY_DIRECT, 1, 2, 0x5a3c,
		NULL) == MH_ERROR_NONE);
	CHECK(pixels[0] == 1 && pixels[8] == 255 && pixels[24] == 2);

	for (i = 0; i < surface.height; i++)
		CHECK(pixels[i * 8 + 6] == 0xa5 && pixels[i * 8 + 7] == 0xa5);

	/* Supercars' vertical view keeps edge fills and finite yaw history. */
	saved = state;
	camera.forward[0] = 0;
	camera.forward[1] = -1;
	CHECK(mh_software_sky_draw(&surface, &image, &camera, &state,
		MH_SOFTWARE_SKY_DIRECT, 1, 2, 0x5a3c,
		NULL) == MH_ERROR_NONE);
	CHECK(state.angle == saved.angle && state.phase == saved.phase);

	for (i = 0; i < surface.height; i++) {
		CHECK(pixels[i * 8] == 2 && pixels[i * 8 + 4] == 2);
		CHECK(pixels[i * 8 + 6] == 0xa5 && pixels[i * 8 + 7] == 0xa5);
	}

	camera.forward[0] = 1;
	camera.forward[1] = 0;
	CHECK(mh_software_sky_draw(&surface, &image, &camera, &state,
		MH_SOFTWARE_SKY_DIRECT, 1, 2, 0x5a3c,
		NULL) == MH_ERROR_NONE);
	CHECK(state.angle == saved.angle && state.phase >= 0 &&
		state.phase < image.width);
	CHECK(pixels[0] == 1 && pixels[8] == 255 && pixels[24] == 2);

	return 0;
}
