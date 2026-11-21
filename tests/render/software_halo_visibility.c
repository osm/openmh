#include <stdio.h>
#include <string.h>

#include "backends/render/software_halo_visibility.h"
#include "backends/render/software_palette.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"

/* Ten padded rows; three marker positions and replacement words per input. */
#define SURFACE_BYTES 360
/* Three offset/saved/visible records, each containing three u32 words. */

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static void
write_pixel(unsigned char *bytes, unsigned int pixel)
{
	bytes[0] = (unsigned char)pixel;
	bytes[1] = (unsigned char)(pixel >> 8);
}

static void
setup_surface(struct mh_software_surface *surface,
	unsigned char pixels[SURFACE_BYTES], unsigned int green_bits)
{
	unsigned int i;

	memset(surface, 0, sizeof(*surface));
	surface->pixels = pixels;
	surface->width = 16;
	surface->height = 10;
	surface->pitch = 36;
	surface->size = SURFACE_BYTES;
	surface->bits[0] = surface->bits[2] = 5;
	surface->bits[1] = green_bits;
	surface->shift[0] = green_bits + 5;
	surface->shift[1] = 5;

	for (i = 0; i < 180; i++)
		write_pixel(pixels + i * 2, 0x1234);
}

static int
geometry(unsigned int green_bits)
{
	struct mh_software_surface surface;
	struct mh_software_halo_marker marker;
	struct mh_software_triangle triangle;
	struct mh_software_texture texture;
	struct mh_software_palette palette;
	unsigned char pixels[SURFACE_BYTES], saved[SURFACE_BYTES], index;
	unsigned int i;

	setup_surface(&surface, pixels, green_bits);
	memcpy(saved, pixels, sizeof(saved));
	memset(&triangle, 0, sizeof(triangle));
	memset(&texture, 0, sizeof(texture));
	memset(&palette, 0, sizeof(palette));
	CHECK(mh_software_halo_mark(&surface, 4, 4, &marker, NULL) ==
		MH_ERROR_NONE);
	triangle.xy[0][0] = triangle.xy[0][1] = 2;
	triangle.xy[1][0] = 10;
	triangle.xy[1][1] = 2;
	triangle.xy[2][0] = 2;
	triangle.xy[2][1] = 9;
	triangle.color[0][0] = 255;
	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_software_halo_resolve(&surface, &marker, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(!marker.visible);
	CHECK(mh_read_u16le(pixels + marker.offset) ==
		(31U << (5 + green_bits)));

	/*
	 * Index-zero cutouts leave the marker visible, just as they leave
	 * geometry.
	 */
	setup_surface(&surface, pixels, green_bits);
	CHECK(mh_software_halo_mark(&surface, 4, 4, &marker, NULL) ==
		MH_ERROR_NONE);
	index = 0;
	texture.indices = &index;
	texture.width = texture.height = texture.size = 1;
	texture.palette = &palette;
	texture.zero_transparent = 1;
	palette.count = 1;
	palette.green_bits = green_bits;
	palette.color[0] = MH_U16_MAX;
	triangle.texture = &texture;

	for (i = 0; i < 3; i++)
		triangle.uv[i][0] = triangle.uv[i][1] = .5f;

	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_software_halo_resolve(&surface, &marker, 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(marker.visible && memcmp(pixels, saved, sizeof(saved)) == 0);

	/* Quantization at the right edge must not touch row padding. */
	CHECK(mh_software_halo_mark(&surface, 15.5f, 4, &marker, NULL) ==
		MH_ERROR_NONE);
	CHECK(!marker.active && memcmp(pixels, saved, sizeof(saved)) == 0);

	return 0;
}

int
main(void)
{

	CHECK(geometry(5) == 0);
	CHECK(geometry(6) == 0);
	puts("Software halo markers follow geometry writes and cutout holes");

	return 0;
}
