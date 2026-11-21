#include <string.h>

#include "engine/render/mipmap.h"

#define PIXEL_LIMIT 16777216UL
#define RGB5_MASK 0xf8U
#define RGB6_MASK 0xfcU

enum mh_error_code
mh_mipmap_texture(const struct mh_image *source, unsigned int format,
	unsigned int level, struct mh_image *output, struct mh_error *error_out)
{
	if (format == MH_TEXTURE_PALETTE_RGB)
		return mh_mipmap_palette(source, level, output, error_out);

	if (format == MH_TEXTURE_RGB565 || format == MH_TEXTURE_XRGB8888)
		return mh_mipmap_opaque(source, level,
			(enum mh_texture_format)format, output, error_out);

	return mh_mipmap_rgba(source, level, output, error_out);
}

static void
average_block(const struct mh_image *source, unsigned int x, unsigned int y,
	unsigned int level, const unsigned int mask[4], unsigned char output[4])
{
	unsigned int dx, dy, channel, size, sum[4];
	const unsigned char *pixel;

	size = 1U << level;

	for (channel = 0; channel < 4; channel++)
		sum[channel] = 0;

	for (dy = 0; dy < size; dy++) {
		pixel = source->rgba +
			((size_t)(y + dy) * source->width + x) * 4;

		for (dx = 0; dx < size; dx++) {
			for (channel = 0; channel < 4; channel++)
				sum[channel] +=
					pixel[dx * 4 + channel] & mask[channel];
		}
	}

	for (channel = 0; channel < 4; channel++)
		output[channel] = (unsigned char)(sum[channel] >> (2 * level));
}

static int
valid_level(const struct mh_image *source, unsigned int level,
	const struct mh_image *output)
{
	if (source == NULL || output == NULL || source->rgba == NULL ||
		output->rgba == NULL || source->rgba == output->rgba ||
		level == 0 || level >= MH_TEXTURE_MIP_LEVELS ||
		source->height == 0 ||
		source->width > PIXEL_LIMIT / source->height ||
		output->width == 0 || output->height == 0 ||
		output->width != (source->width >> level) ||
		output->height != (source->height >> level))
		return 0;

	return 1;
}

static enum mh_error_code
reduce_image(const struct mh_image *source, unsigned int level,
	const unsigned int mask[4], struct mh_image *output,
	struct mh_error *error_out)
{
	unsigned int x, y;
	size_t pixel;

	if (!valid_level(source, level, output))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid RGBA mip level");

	for (y = 0; y < output->height; y++) {
		for (x = 0; x < output->width; x++) {
			pixel = (size_t)y * output->width + x;
			average_block(source, x << level, y << level, level,
				mask, output->rgba + pixel * 4);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mipmap_palette(const struct mh_image *source, unsigned int level,
	struct mh_image *output, struct mh_error *error_out)
{
	unsigned int x, y;
	size_t from, to;

	if (!valid_level(source, level, output))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid RGBA mip level");

	/* Select an index; palette expansion commutes with selection. */
	for (y = 0; y < output->height; y++) {
		for (x = 0; x < output->width; x++) {
			from = (size_t)(y << level) * source->width +
				(x << level);
			to = (size_t)y * output->width + x;
			memcpy(output->rgba + to * 4, source->rgba + from * 4,
				4);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_mipmap_rgba(const struct mh_image *source, unsigned int level,
	struct mh_image *output, struct mh_error *error_out)
{
	static const unsigned int mask[4] = {255, 255, 255, 255};

	return reduce_image(source, level, mask, output, error_out);
}

enum mh_error_code
mh_mipmap_opaque(const struct mh_image *source, unsigned int level,
	enum mh_texture_format format, struct mh_image *output,
	struct mh_error *error_out)
{
	unsigned int mask[4];
	enum mh_error_code status;

	if (format != MH_TEXTURE_RGB565 && format != MH_TEXTURE_XRGB8888)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid opaque mip format");

	/* Original packed reads zero low bits; remove sampling expansion. */
	mask[0] = mask[2] = format == MH_TEXTURE_XRGB8888 ? 255 : RGB5_MASK;
	mask[1] = format == MH_TEXTURE_RGB565 ? RGB6_MASK : mask[0];
	mask[3] = 255;
	status = reduce_image(source, level, mask, output, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_image_texture_format(output, format, error_out);

	return status;
}
