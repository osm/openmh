#include "engine/assets/image.h"
#include "engine/core/error.h"

#define PIXEL_LIMIT 16777216UL
#define BRIGHTNESS_LIMIT 100

static int
valid_image(const struct mh_image *image)
{
	return image != NULL && image->rgba != NULL && image->width != 0 &&
		image->height != 0 &&
		image->width <= PIXEL_LIMIT / image->height;
}

enum mh_error_code
mh_image_texture_format(struct mh_image *image, enum mh_texture_format format,
	struct mh_error *error_out)
{
	size_t pixel_count, i;
	unsigned int channel, value;

	if (!valid_image(image) ||
		(format != MH_TEXTURE_PALETTE_RGB &&
			format != MH_TEXTURE_RGB565 &&
			format != MH_TEXTURE_RGB555 &&
			format != MH_TEXTURE_ARGB1555 &&
			format != MH_TEXTURE_ARGB4444 &&
			format != MH_TEXTURE_XRGB8888))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid hardware texture format or image");

	pixel_count = (size_t)image->width * image->height;

	for (i = 0; i < pixel_count; i++) {
		if (format == MH_TEXTURE_ARGB4444) {
			for (channel = 0; channel < 4; channel++) {
				value = image->rgba[i * 4 + channel] >> 4;
				image->rgba[i * 4 + channel] =
					(unsigned char)((value << 4) | value);
			}

			continue;
		}

		if (format == MH_TEXTURE_RGB565 ||
			format == MH_TEXTURE_RGB555 ||
			format == MH_TEXTURE_ARGB1555) {
			for (channel = 0; channel < 3; channel++) {
				value = image->rgba[i * 4 + channel];

				if (format == MH_TEXTURE_RGB565 &&
					channel == 1) {
					value >>= 2;
					value = (value << 2) | (value >> 4);
				} else {
					value >>= 3;
					value = (value << 3) | (value >> 2);
				}

				image->rgba[i * 4 + channel] =
					(unsigned char)value;
			}
		}

		if (format == MH_TEXTURE_ARGB1555)
			image->rgba[i * 4 + 3] =
				image->rgba[i * 4 + 3] >= 128 ? 255 : 0;
		else
			image->rgba[i * 4 + 3] = 255;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_texture_brightness(
	struct mh_image *image, const double rgb[3], struct mh_error *error_out)
{
	unsigned char brightness_map[3][256];
	unsigned int channel, value;
	size_t pixel_count, i;
	double scaled;

	if (!valid_image(image) || rgb == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid texture brightness image");

	for (channel = 0; channel < 3; channel++) {
		if (!(rgb[channel] >= 0 && rgb[channel] <= BRIGHTNESS_LIMIT))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid texture brightness factor");

		for (value = 0; value < 256; value++) {
			/*
			 * A byte times a float fits exactly in double.
			 * Palette intensities truncate toward zero.
			 */
			scaled = value * (double)(float)rgb[channel];
			brightness_map[channel][value] =
				scaled >= 255 ? 255 : (unsigned char)scaled;
		}
	}

	pixel_count = (size_t)image->width * image->height;

	for (i = 0; i < pixel_count; i++)
		for (channel = 0; channel < 3; channel++)
			image->rgba[i * 4 + channel] =
				brightness_map[channel]
					[image->rgba[i * 4 + channel]];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
