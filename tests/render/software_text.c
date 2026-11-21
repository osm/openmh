#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_text.h"
#include "backends/render/software_ui.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/font.h"
#include "engine/core/binary.h"

/* Five u32 fields followed by a NUL-terminated byte string. */

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
fixtures(void)
{
	struct mh_font_program font;
	struct mh_software_surface surface;
	unsigned char map[256 * 16], pixels[24], saved[24];
	const unsigned char text[2] = {1, 1};
	unsigned int width;

	memset(&font, 0, sizeof(font));
	memset(&surface, 0, sizeof(surface));
	memset(map, 0, sizeof(map));
	font.cell_width = 16;
	font.width = 256;
	font.height = 1;
	font.rows = 16;
	font.bytes = map;
	font.advance[1] = 3;
	map[16] = MH_FONT_LOW;
	map[17] = MH_FONT_HIGH;
	map[18] = MH_FONT_WIDE_LOW;
	surface.width = 5;
	surface.height = 2;
	surface.pitch = 12;
	surface.size = sizeof(pixels);
	surface.pixels = pixels;
	memset(pixels, 0xa5, sizeof(pixels));
	CHECK(mh_software_text_measure(&font, text, 2, &width, NULL) ==
		MH_ERROR_NONE);
	CHECK(width == 3); /* Floor once after the sum of odd byte advances. */
	CHECK(mh_software_text_draw(&surface, &font, text, 2, 3, 0, 0x7ff0,
		MH_UI_TEXT_RIGHT, NULL) == MH_ERROR_NONE);
	CHECK(pixels[0] == 0xf0 && pixels[1] == 0x7f && pixels[2] == 0xf8);
	CHECK(memcmp(pixels, pixels + 3, 3) == 0);
	CHECK(pixels[6] == 0xa5 && pixels[10] == 0xa5);
	memcpy(saved, pixels, sizeof(saved));
	CHECK(mh_software_text_draw(&surface, &font, text, 2, 3, 0, 0,
		(enum mh_ui_text_align)3, NULL) == MH_ERROR_INVALID);
	CHECK(mh_software_text_draw(&surface, &font, text, 1025, 3, 0, 0,
		MH_UI_TEXT_RIGHT, NULL) == MH_ERROR_INVALID);
	font.advance[1] = 17;
	CHECK(mh_software_text_draw(&surface, &font, text, 2, 3, 0, 0,
		MH_UI_TEXT_RIGHT, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(saved, pixels, sizeof(saved)) == 0);
	font.advance[1] = 3;
	memset(pixels, 0xa5, sizeof(pixels));
	CHECK(mh_software_text_draw(&surface, &font, text, 2, -1, 1, 0x1234,
		MH_UI_TEXT_LEFT, NULL) == MH_ERROR_NONE);
	CHECK(pixels[12] == 0x34 && pixels[13] == 0x34 && pixels[14] == 0x12 &&
		pixels[15] == 0x34 && pixels[16] == 0xa5);
	CHECK(pixels[0] == 0xa5 && pixels[22] == 0xa5);
	memset(pixels, 0xa5, sizeof(pixels));
	CHECK(mh_software_text_draw(&surface, &font, text, 2, 4, 0, 0x1234,
		MH_UI_TEXT_LEFT, NULL) == MH_ERROR_NONE);
	CHECK(pixels[8] == 0x34 && pixels[9] == 0x12 && pixels[10] == 0xa5);
	CHECK(mh_software_text_draw(&surface, &font, NULL, 0, 0, -1, 0,
		MH_UI_TEXT_LEFT, NULL) == MH_ERROR_NONE);

	return 0;
}

int
main(void)
{
	return fixtures();
}
