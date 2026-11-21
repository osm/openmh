#include "backends/render/software_blur.h"
#include "engine/core/binary.h"

static unsigned int
blend_pixel(
	unsigned int current, unsigned int previous, unsigned int green_bits)
{
	unsigned int red, green, blue, red_shift, green_mask;

	red_shift = green_bits + 5;
	green_mask = (1U << green_bits) - 1;
	red = (((current >> red_shift) & 31U) +
		((previous >> red_shift) & 31U)) /
		2;
	green = (((current >> 5) & green_mask) +
			((previous >> 5) & green_mask)) /
		2;
	blue = ((current & 31U) + (previous & 31U)) / 2;

	return (red << red_shift) | (green << 5) | blue;
}

enum mh_error_code
mh_software_blur(struct mh_software_surface *surface,
	const unsigned char *history, size_t size, struct mh_error *error_out)
{
	size_t row_bytes, offset, row_offset;
	unsigned int pixel, row;

	if (surface == NULL || surface->pixels == NULL || history == NULL ||
		surface->width == 0 || surface->height == 0 ||
		surface->width > surface->pitch / 2 ||
		surface->height > surface->size / surface->pitch ||
		surface->height > size / surface->pitch ||
		surface->bits[0] != 5 || surface->bits[2] != 5 ||
		(surface->bits[1] != 5 && surface->bits[1] != 6) ||
		surface->shift[0] != surface->bits[1] + 5 ||
		surface->shift[1] != 5 || surface->shift[2] != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software motion blur surface or history");

	/* Blend visible pixel pairs, leaving padding alone. */
	row_bytes = (size_t)surface->width * 2;
	row_bytes -= row_bytes % 4;

	for (row = 0; row < surface->height; row++) {
		row_offset = (size_t)row * surface->pitch;

		for (offset = row_offset; offset < row_offset + row_bytes;
			offset += 2) {
			pixel = blend_pixel(
				mh_read_u16le(surface->pixels + offset),
				mh_read_u16le(history + offset),
				surface->bits[1]);
			surface->pixels[offset] = (unsigned char)pixel;
			surface->pixels[offset + 1] =
				(unsigned char)(pixel >> 8);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
