#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/ui/font_atlas.h"

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "font atlas:%d: %s\n", __LINE__, #e);  \
			return 1;                                              \
		}                                                              \
	} while (0)

int
main(void)
{
	struct mh_font font;
	struct mh_ui_atlas atlas, before;
	struct mh_ui_region glyphs[MH_FONT_GLYPHS], saved[MH_FONT_GLYPHS];
	unsigned int advances[MH_FONT_GLYPHS], saved_advances[MH_FONT_GLYPHS];
	unsigned char pixels[32 * 48 * 4], saved_pixels[70 * 52 * 4];
	unsigned int i;

	memset(&font, 0, sizeof(font));
	memset(pixels, 73, sizeof(pixels));
	font.image.rgba = pixels;
	font.image.width = 32;
	font.image.height = 48;
	font.cell_width = 2;
	font.height = 3;

	for (i = 0; i < MH_FONT_GLYPHS; i++)
		font.advance[i] = 2;

	font.advance[32] = 1;
	CHECK(mh_ui_atlas_init(70, 52, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(mh_ui_font_atlas_add(&atlas, &font, 0, glyphs, advances, NULL) ==
		MH_ERROR_NONE);
	CHECK(glyphs[17].x == 3 && glyphs[17].y == 4);
	CHECK(glyphs[255].x == 31 && glyphs[255].y == 46);
	CHECK(glyphs[17].width == 2 && glyphs[17].height == 3);
	CHECK(glyphs[32].width == 1 && advances[32] == 1);
	CHECK(atlas.image.rgba[(70 + 1) * 4] == 73);
	CHECK(atlas.image.rgba[0] == 0);
	pixels[0] = 99;
	CHECK(atlas.image.rgba[(70 + 1) * 4] == 73);

	CHECK(mh_ui_font_atlas_add(&atlas, &font, 1, glyphs, advances, NULL) ==
		MH_ERROR_NONE);
	CHECK(glyphs[17].x == 37 && glyphs[17].y == 4);
	CHECK(glyphs[17].width == 1 && advances[17] == 2);
	CHECK(glyphs[32].width == 0 && advances[32] == 1);
	CHECK(atlas.image.rgba[(70 + 35) * 4] == 99);
	before = atlas;
	memcpy(saved_pixels, atlas.image.rgba, sizeof(saved_pixels));
	memcpy(saved, glyphs, sizeof(saved));
	memcpy(saved_advances, advances, sizeof(saved_advances));
	CHECK(mh_ui_font_atlas_add(&atlas, &font, 0, glyphs, advances, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(&atlas, &before, sizeof(atlas)) == 0);
	CHECK(memcmp(atlas.image.rgba, saved_pixels, sizeof(saved_pixels)) ==
		0);
	CHECK(memcmp(glyphs, saved, sizeof(saved)) == 0);
	CHECK(memcmp(advances, saved_advances, sizeof(saved_advances)) == 0);
	font.advance[255] = 0;
	CHECK(mh_ui_font_atlas_add(&atlas, &font, 1, glyphs, advances, NULL) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(glyphs, saved, sizeof(saved)) == 0);
	CHECK(memcmp(advances, saved_advances, sizeof(saved_advances)) == 0);
	CHECK(memcmp(&atlas, &before, sizeof(atlas)) == 0);
	CHECK(memcmp(atlas.image.rgba, saved_pixels, sizeof(saved_pixels)) ==
		0);
	free(atlas.image.rgba);
	puts("Font atlas: glyph placement, trimming, independent pixels and atomic failure passed");

	return 0;
}
