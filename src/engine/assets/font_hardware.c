#include <stdlib.h>
#include <string.h>

#include "engine/assets/font.h"
#include "interfaces/image/decoder.h"

#define GRID_SIZE 16
#define IMAGE_LIMIT 256

enum {
	PALETTE_COLORS = 256,
	FOREGROUND_INDEX = 255
};

static void
font_palette(struct mh_image_palette *palette)
{
	/* Only index 255 survives hardware font conversion. */
	memset(palette, 0, sizeof(*palette));
	palette->count = PALETTE_COLORS;
	memset(palette->rgb + FOREGROUND_INDEX * 3, 255, 3);
}

/* Consume the decoded allocation on both success and failure. */
static enum mh_error_code
prepare_font(struct mh_image *image, struct mh_font *output,
	struct mh_error *error_out)
{
	struct mh_font font;
	unsigned int i, x, y, advance;
	size_t row, pixel;

	memset(&font, 0, sizeof(font));
	font.image = *image;

	if (font.image.width < GRID_SIZE || font.image.height < GRID_SIZE ||
		font.image.width > IMAGE_LIMIT ||
		font.image.height > IMAGE_LIMIT ||
		font.image.width % GRID_SIZE != 0 ||
		font.image.height % GRID_SIZE != 0) {
		free(font.image.rgba);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid hardware HUD font grid");
	}

	font.cell_width = font.image.width / GRID_SIZE;
	font.height = font.image.height / GRID_SIZE;

	for (i = 0; i < font.image.width * font.image.height; i++)
		font.image.rgba[i * 4 + 3] = font.image.rgba[i * 4];

	for (i = 0; i < MH_FONT_GLYPHS; i++) {
		advance = 0;

		for (y = 0; y < font.height; y++) {
			row = (size_t)(i / GRID_SIZE * font.height + y) *
				font.image.width;

			for (x = 0; x < font.cell_width; x++) {
				pixel = (row + i % GRID_SIZE * font.cell_width +
						x) *
					4;

				if (font.image.rgba[pixel] == 255 &&
					x >= advance)
					advance = x + 1;
			}
		}

		if (i == ' ')
			advance = font.cell_width / 2;

		font.advance[i] = advance + 1;
	}

	*output = font;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_font_hardware_decode_from(const struct mh_image_decoder_provider *decoder,
	const unsigned char *data, size_t size, struct mh_font *output,
	struct mh_error *error_out)
{
	struct mh_image image;
	struct mh_image_palette palette;
	struct mh_image_decode_options options;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid hardware font output");

	font_palette(&palette);
	options.palette = &palette;
	options.alpha = MH_IMAGE_ALPHA_FILE;
	status = mh_image_decode(
		decoder, data, size, &options, &image, NULL, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return prepare_font(&image, output, error_out);
}
