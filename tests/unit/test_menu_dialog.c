#include <stdio.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/ui/menu_dialog.h"
#include "engine/ui/menu_dialog_draw.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Dialog line %d: %s\n", __LINE__, #x); \
			return 1;                                              \
		}                                                              \
	} while (0)

static unsigned char indices[MH_MENU_NOISE_BYTES], noise[MH_MENU_NOISE_BYTES],
	pixels[1024], before[1024], glyphs[1024];

int
main(void)
{
	struct mh_indexed_image image;
	struct mh_menu_dialog_text text, saved;
	struct mh_font font;
	struct mh_sprite_indexed background;
	struct mh_menu_canvas canvas;
	unsigned char bad[256], sprite[8];
	unsigned long seed;
	unsigned int i;

	CHECK(mh_menu_dialog_builtin(MH_MENU_CONFIRM_EXIT, &text, NULL) ==
			MH_ERROR_NONE &&
		text.title_count == 1 && text.choice_count == 2);
	saved = text;
	memset(bad, 'x', sizeof(bad));
	CHECK(mh_menu_dialog_text_read(bad, sizeof(bad), text.choices, 256,
		&text, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&text, &saved, sizeof(text)) == 0);
	CHECK(mh_menu_dialog_text_read((const unsigned char *)"", 1,
		(const unsigned char *)"A\0", 3, &text,
		NULL) == MH_ERROR_NONE);
	CHECK(text.title_count == 0 && text.choice_count == 1);
	CHECK(mh_menu_dialog_builtin(MH_MENU_BLOCK_EXIT, &text, NULL) ==
			MH_ERROR_NONE &&
		text.choice_count == 1);
	memset(&image, 0, sizeof(image));
	image.width = image.height = image.colors = 256;
	image.size = sizeof(indices);
	image.indices = indices;

	for (i = 0; i < sizeof(indices); i++)
		indices[i] = (unsigned char)((i / 256 + i % 256) & 255);

	CHECK(mh_menu_noise_prepare(&image, noise, NULL) == MH_ERROR_NONE);

	for (i = 0; i < sizeof(noise); i++)
		CHECK(noise[i] ==
			192 + (((255 - i / 256 + i % 256) & 255) >> 3));

	memset(noise, 37, sizeof(noise));
	image.size--;
	CHECK(mh_menu_noise_prepare(&image, noise, NULL) != MH_ERROR_NONE &&
		noise[MH_U16_MAX] == 37);
	memset(&font, 0, sizeof(font));
	memset(glyphs, 255, sizeof(glyphs));
	font.image.rgba = glyphs;
	font.image.width = font.image.height = 16;
	font.height = font.cell_width = 1;

	for (i = 0; i < 256; i++)
		font.advance[i] = 1;

	memset(&background, 0, sizeof(background));
	background.width = background.height = 2;
	canvas.pixels = pixels;
	canvas.size = sizeof(pixels);
	canvas.pitch = 2;
	canvas.begin = 0;
	canvas.end = sizeof(pixels);
	memset(pixels, 73, sizeof(pixels));
	memcpy(before, pixels, sizeof(before));
	seed = 123;

	/*
	 * Noise has already been composed in scratch when this missing sprite
	 * fails. Neither pixels nor the shared random state may leak out.
	 */
	CHECK(mh_menu_dialog_render(&text, &font, &background, noise, 160, 0,
		&seed, &canvas, NULL) != MH_ERROR_NONE);
	CHECK(seed == 123 && memcmp(pixels, before, sizeof(pixels)) == 0);
	memset(sprite, 0, sizeof(sprite));
	background.pixels = sprite;
	CHECK(mh_menu_dialog_render(&text, &font, &background, noise, 160, 0,
		&seed, &canvas, NULL) == MH_ERROR_NONE);
	CHECK(seed != 123 && memcmp(pixels, before, sizeof(pixels)) != 0);
	saved = text;
	text.choice_count = 0;
	seed = 456;
	CHECK(mh_menu_dialog_render(&text, &font, &background, noise, 160, 0,
		&seed, &canvas, NULL) != MH_ERROR_NONE &&
		seed == 456);
	CHECK(mh_menu_dialog_builtin(99, &saved, NULL) != MH_ERROR_NONE);
	puts("Dialog text/noise bounds, original labels and complete composition rollback passed");

	return 0;
}
