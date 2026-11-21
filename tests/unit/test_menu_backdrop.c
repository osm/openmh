#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/tga.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_backdrop.h"

#define TGA_BYTES 256207
#define CANVAS_PIXELS 614400

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Backdrop line %d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static enum mh_error_code
prepare_tga(const unsigned char *data, size_t size,
	struct mh_menu_backdrop *output, struct mh_error *error_out)
{
	struct mh_indexed_image image;
	enum mh_error_code status;

	status = mh_image_tga_decode_indexed(data, size, &image, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_menu_backdrop_prepare(&image, output, error_out);
	free(image.indices);

	return status;
}

static unsigned char tga[TGA_BYTES];
static unsigned char work_pixels[CANVAS_PIXELS], display_pixels[CANVAS_PIXELS];

static void
init_canvas(struct mh_menu_canvas *canvas, unsigned char *pixels,
	unsigned int pitch, unsigned int begin)
{
	canvas->pixels = pixels;

	canvas->size = sizeof(work_pixels);
	canvas->begin = begin;
	canvas->end = canvas->size;
	canvas->pitch = pitch;
}

int
main(void)
{
	static const unsigned char header[18] = {
		0, 1, 1, 0, 0, 63, 0, 24, 0, 0, 0, 0, 128, 2, 144, 1, 8, 32};

	static const size_t short_sizes[] = {0, 1, 17, 18, 206, 207, 256206};
	struct mh_menu_backdrop backdrop, saved;
	struct mh_menu_canvas work, display;
	size_t i, pos;
	unsigned int x, y;

	memcpy(tga, header, sizeof(header));

	for (i = 18; i < 207; i++)
		tga[i] = (unsigned char)i;

	for (i = 207; i < sizeof(tga); i++)
		tga[i] = (unsigned char)((i - 207) % 63);

	CHECK(prepare_tga(tga, sizeof(tga), &backdrop, NULL) == MH_ERROR_NONE);
	CHECK(backdrop.palette[0] == 20 && backdrop.palette[1] == 19 &&
		backdrop.palette[2] == 18);
	CHECK(memcmp(backdrop.palette + 189, backdrop.palette + 186, 3) == 0);

	for (y = 0; y < 640; y++)
		for (x = 0; x < 960; x++) {
			pos = (size_t)y * 960 + x;

			if (y >= 100 && y < 500 && x >= 160 && x < 800)
				CHECK(backdrop.pixels[pos] ==
					192 +
						tga[207 +
							(size_t)(y - 100) *
								640 +
							x - 160]);
			else
				CHECK(backdrop.pixels[pos] == 192);
		}

	saved = backdrop;

	for (i = 0; i < sizeof(short_sizes) / sizeof(short_sizes[0]); i++)
		CHECK(prepare_tga(tga, short_sizes[i], &backdrop, NULL) !=
			MH_ERROR_NONE);

	tga[256206] = 63;
	CHECK(prepare_tga(tga, sizeof(tga), &backdrop, NULL) != MH_ERROR_NONE);
	tga[256206] = 0;
	tga[16] = 7;
	CHECK(prepare_tga(tga, sizeof(tga), &backdrop, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&backdrop, &saved, sizeof(backdrop)) == 0);
	tga[16] = 8;
	tga[256206] = (unsigned char)(255999UL % 63);
	memset(work_pixels, 42, sizeof(work_pixels));
	memset(display_pixels, 23, sizeof(display_pixels));
	init_canvas(&work, work_pixels, 960, 115360);
	init_canvas(&display, display_pixels, 640, 0);
	CHECK(mh_menu_backdrop_exchange(&backdrop, 640, 400, &work, &display,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < 256000; i++)
		CHECK(display_pixels[i] == 42);

	CHECK(display_pixels[256000] == 23);

	for (y = 0; y < 400; y++)
		for (x = 0; x < 640; x++)
			CHECK(work_pixels[work.begin + (size_t)y * 960 + x] ==
				192 + tga[207 + (size_t)y * 640 + x]);

	CHECK(work_pixels[work.begin - 1] == 42 &&
		work_pixels[work.begin + 640] == 42);

	/*
	 * Second exchange must present the prior composition, not the backdrop
	 * restored during this call.
	 */
	work_pixels[work.begin + 7] = 255;
	CHECK(mh_menu_backdrop_exchange(&backdrop, 640, 400, &work, &display,
		NULL) == MH_ERROR_NONE);
	CHECK(display_pixels[7] == 255 &&
		work_pixels[work.begin + 7] == 192 + tga[214]);
	memset(work_pixels, 42, sizeof(work_pixels));
	memset(display_pixels, 23, sizeof(display_pixels));
	CHECK(mh_menu_backdrop_exchange(&backdrop, 639, 400, &work, &display,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_menu_backdrop_exchange(&backdrop, 640, 601, &work, &display,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_menu_backdrop_exchange(&backdrop, 640, 400, &work, &work,
		NULL) != MH_ERROR_NONE);
	display.end = 255999;
	CHECK(mh_menu_backdrop_exchange(&backdrop, 640, 400, &work, &display,
		NULL) != MH_ERROR_NONE);

	for (i = 0; i < sizeof(work_pixels); i++)
		CHECK(work_pixels[i] == 42 && display_pixels[i] == 23);

	mh_menu_backdrop_free(&backdrop);
	mh_menu_backdrop_free(&backdrop);
	puts("Menu backdrop: palette indices, padded placement, previous-frame presentation and bounds passed");

	return 0;
}
