#include <math.h>
#include <string.h>

#include "backends/render/software_halo_visibility.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"

#define MARKER_WORD 0x07ffU
#define MARKER_MASK 0x07deU
#define MARKER_HIGH_LIMIT 10U
#define MARKER_HIGH_SHIFT 11
#define PIXEL_SCALE 65536UL
#define COORDINATE_LIMIT 30000

static void
write_pixel(unsigned char *bytes, unsigned int pixel)
{
	bytes[0] = (unsigned char)pixel;
	bytes[1] = (unsigned char)(pixel >> 8);
}

static int
surface_valid(const struct mh_software_surface *surface)
{
	return surface != NULL && surface->pixels != NULL &&
		surface->width > 0 && surface->width <= COORDINATE_LIMIT &&
		surface->height > 0 && surface->height <= COORDINATE_LIMIT &&
		surface->pitch >= (size_t)surface->width * 2 &&
		surface->height <= surface->size / surface->pitch;
}

/* 16.16 conversion to nearest/even, then (fixed + 65535) >> 16. */
static unsigned long
pixel_position(float position)
{
	double value, whole, fraction;

	value = (double)position * PIXEL_SCALE;
	whole = floor(value);
	fraction = value - whole;

	if (fraction > .5 || (fraction == .5 && fmod(whole, 2) != 0))
		whole++;

	return ((unsigned long)whole + PIXEL_SCALE - 1) / PIXEL_SCALE;
}

enum mh_error_code
mh_software_halo_mark(struct mh_software_surface *surface, float x, float y,
	struct mh_software_halo_marker *output, struct mh_error *error_out)
{
	struct mh_software_halo_marker next;
	unsigned long column, row;

	if (!surface_valid(surface) || output == NULL ||
		!(x >= 0 && x < surface->width) ||
		!(y >= 0 && y < surface->height))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software halo marker");

	memset(&next, 0, sizeof(next));
	column = pixel_position(x);
	row = pixel_position(y);

	if (column < surface->width && row < surface->height) {
		next.active = 1;
		next.offset = (size_t)row * surface->pitch + column * 2;
		next.saved = mh_read_u16le(surface->pixels + next.offset);
		write_pixel(surface->pixels + next.offset, MARKER_WORD);
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_software_halo_resolve(struct mh_software_surface *surface,
	struct mh_software_halo_marker *markers, size_t count,
	struct mh_error *error_out)
{
	struct mh_software_halo_marker *marker;
	unsigned int pixel;
	size_t i;

	if (!surface_valid(surface) || (count != 0 && markers == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid software halo marker list");

	for (i = 0; i < count; i++)
		if (markers[i].active &&
			(markers[i].offset > surface->size - 2 ||
				markers[i].saved > MH_U16_MAX))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid software halo marker offset");

	for (i = 0; i < count; i++) {
		marker = &markers[i];
		marker->visible = 0;

		if (!marker->active)
			continue;

		pixel = mh_read_u16le(surface->pixels + marker->offset);
		marker->visible = (pixel & MARKER_MASK) == MARKER_MASK &&
			(pixel >> MARKER_HIGH_SHIFT) <= MARKER_HIGH_LIMIT;

		if (marker->visible)
			write_pixel(surface->pixels + marker->offset,
				marker->saved);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
