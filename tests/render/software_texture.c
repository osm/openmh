#include <stdio.h>
#include <string.h>

#include "backends/render/software_fixed.h"
#include "backends/render/software_palette.h"
#include "backends/render/software_perspective.h"

static int
texture_guards(int cutout)
{
	struct mh_software_surface surface;
	struct mh_software_triangle triangle;
	struct mh_software_texture texture;
	struct mh_software_palette palette;
	struct mh_software_texture_state state, retained;
	unsigned char indices[256], pixels[48 * 136], saved[48 * 136];
	unsigned char rgb[3];
	unsigned int shift, i;

	memset(&surface, 0, sizeof(surface));
	memset(&triangle, 0, sizeof(triangle));
	memset(&texture, 0, sizeof(texture));
	memset(indices, 0, sizeof(indices));
	memset(pixels, 0xa5, sizeof(pixels));
	rgb[0] = 255;
	rgb[1] = rgb[2] = 0;
	triangle.color[0][0] = 128;
	triangle.texture = &texture;
	triangle.xy[1][0] = 64;
	triangle.xy[2][1] = 48;
	triangle.uv[0][0] = triangle.uv[0][1] = 0.5f;
	triangle.uv[1][0] = triangle.uv[2][1] = 15.5f;
	triangle.uv[1][1] = triangle.uv[2][0] = 0.5f;
	texture.width = texture.height = 16;
	texture.size = sizeof(indices);
	texture.indices = indices;
	texture.palette = &palette;
	texture.zero_transparent = cutout;
	surface.pixels = pixels;
	surface.size = sizeof(pixels);
	surface.width = 64;
	surface.height = 48;
	surface.pitch = 136;
	surface.bits[0] = surface.bits[2] = 5;
	surface.bits[1] = 6;
	surface.shift[0] = 11;
	surface.shift[1] = 5;

	if (mh_software_palette_build(
		&palette, rgb, 3, 1, &triangle, 6, NULL) != MH_ERROR_NONE ||
		mh_software_triangle(&surface, &triangle, NULL, NULL) !=
			MH_ERROR_NONE ||
		pixels[8 * 136 + 16] != (cutout ? 0xa5 : 0) ||
		pixels[8 * 136 + 17] != (cutout ? 0xa5 : 0xf8) ||
		pixels[47 * 136 + 126] != 0xa5)
		return 0;

	memcpy(saved, pixels, sizeof(saved));
	texture.size--;

	if (mh_software_triangle(&surface, &triangle, NULL, NULL) !=
		MH_ERROR_INVALID)
		return 0;

	texture.size++;
	triangle.uv[0][0] = -1;

	if (mh_software_triangle(&surface, &triangle, NULL, NULL) !=
		MH_ERROR_RANGE)
		return 0;

	triangle.uv[0][0] = 0.5f;
	indices[5 * 16 + 5] = 1;

	if (mh_software_triangle_check(&surface, &triangle, NULL, NULL) !=
			MH_ERROR_RANGE ||
		mh_software_triangle(&surface, &triangle, NULL, NULL) !=
			MH_ERROR_RANGE)
		return 0;

	indices[5 * 16 + 5] = 0;

	for (i = 0; i < 3; i++)
		triangle.perspective_weight[i] = 1;

	for (shift = 2; shift <= 5; shift++) {
		triangle.perspective_shift = shift;

		if (mh_software_triangle(&surface, &triangle, NULL, NULL) !=
			MH_ERROR_NONE)
			return 0;

		indices[5 * 16 + 5] = 1;

		if (mh_software_triangle(&surface, &triangle, NULL, NULL) !=
			MH_ERROR_RANGE)
			return 0;

		indices[5 * 16 + 5] = 0;
	}

	triangle.perspective_weight[1] = 0;

	if (mh_software_triangle(&surface, &triangle, NULL, NULL) !=
		(cutout ? MH_ERROR_NONE : MH_ERROR_RANGE))
		return 0;

	triangle.perspective_weight[1] = 1;
	triangle.perspective_shift = 1;

	if (mh_software_triangle(&surface, &triangle, NULL, NULL) !=
		(cutout ? MH_ERROR_NONE : MH_ERROR_UNSUPPORTED))
		return 0;

	triangle.perspective_shift = 0;
	triangle.xy[0][0] = triangle.xy[0][1] = 1;
	triangle.xy[1][0] = 1.01f;
	triangle.xy[1][1] = triangle.xy[2][0] = 1;
	triangle.xy[2][1] = 40;

	if (mh_software_triangle(&surface, &triangle, NULL, NULL) !=
		MH_ERROR_NONE)
		return 0;

	memcpy(saved, pixels, sizeof(saved));

	if (!cutout) {
		/*
		 * A thin triangle can extrapolate past zero at a block
		 * endpoint. The original still projects that endpoint; this
		 * empty span must not fail just because the extrapolated weight
		 * is negative.
		 */
		triangle.perspective_shift = 2;
		triangle.perspective_weight[1] = 1e-6f;

		if (mh_software_triangle(&surface, &triangle, NULL, NULL) !=
			MH_ERROR_NONE)
			return 0;
	}

	/*
	 * A rejected near-clipped face must retain the preceding span history.
	 */
	memset(&state, 0, sizeof(state));
	state.pixel_step[0] = 123;
	state.span_step[1] = -456;
	state.width = 16;
	retained = state;
	triangle.xy[1][0] = -30009.2344f;

	if (mh_software_triangle_check(&surface, &triangle, &state, NULL) !=
			MH_ERROR_NONE ||
		memcmp(&state, &retained, sizeof(state)) != 0 ||
		mh_software_triangle(&surface, &triangle, &state, NULL) !=
			MH_ERROR_NONE ||
		memcmp(&state, &retained, sizeof(state)) != 0)
		return 0;

	return memcmp(saved, pixels, sizeof(saved)) == 0;
}

static int
fallback_guards(void)
{
	struct mh_software_surface surface;
	struct mh_software_triangle triangle;
	struct mh_software_texture texture;
	struct mh_software_palette palette;
	struct mh_software_texture_state state, saved;
	unsigned char pixels[16], retained[16], indices[256];
	unsigned int i;

	memset(&surface, 0, sizeof(surface));
	memset(&triangle, 0, sizeof(triangle));
	memset(&texture, 0, sizeof(texture));
	memset(&palette, 0, sizeof(palette));
	memset(&state, 0, sizeof(state));
	memset(indices, 0, sizeof(indices));
	memset(pixels, 0xa5, sizeof(pixels));
	surface.pixels = pixels;
	surface.size = surface.pitch = sizeof(pixels);
	surface.width = 8;
	surface.height = 1;
	surface.bits[0] = surface.bits[2] = 5;
	surface.bits[1] = 6;
	surface.shift[0] = 11;
	surface.shift[1] = 5;
	texture.indices = indices;
	texture.size = sizeof(indices);
	texture.width = texture.height = 16;
	texture.palette = &palette;
	palette.count = 2;
	palette.green_bits = 6;
	palette.color[0] = 0x1234;
	palette.color[1] = 0x5678;
	triangle.texture = &texture;
	triangle.xy[1][0] = 8;
	triangle.xy[2][1] = 0.125f;
	triangle.uv[1][0] = triangle.uv[2][1] = 15.5f;
	indices[0] = indices[241] = 1;

	/* Base address is excluded, even if its source index is nonzero. */
	if (mh_software_triangle(&surface, &triangle, &state, NULL) !=
		MH_ERROR_NONE)
		return 0;

	for (i = 0; i < 8; i++)
		if (pixels[i * 2] != 0x34 || pixels[i * 2 + 1] != 0x12)
			return 0;

	/* A valid sample is retained after the cached step leaves the image. */
	state.span_step[0] = MH_SOFTWARE_TEXTURE_SCALE;
	state.span_step[1] = 15 * MH_SOFTWARE_TEXTURE_SCALE;
	state.width = 16;
	saved = state;

	if (mh_software_triangle(&surface, &triangle, &state, NULL) !=
			MH_ERROR_NONE ||
		memcmp(&state, &saved, sizeof(state)) != 0)
		return 0;

	for (i = 1; i < 8; i++)
		if (pixels[i * 2] != 0x78 || pixels[i * 2 + 1] != 0x56)
			return 0;

	memcpy(retained, pixels, sizeof(pixels));
	indices[241] = 2;

	if (mh_software_triangle_check(&surface, &triangle, &state, NULL) !=
			MH_ERROR_RANGE ||
		mh_software_triangle(&surface, &triangle, &state, NULL) !=
			MH_ERROR_RANGE ||
		memcmp(&state, &saved, sizeof(state)) != 0 ||
		memcmp(pixels, retained, sizeof(pixels)) != 0)
		return 0;

	/* Rejected cutouts do not read indices or disturb the cache. */
	texture.zero_transparent = 1;

	if (mh_software_triangle(&surface, &triangle, &state, NULL) !=
			MH_ERROR_NONE ||
		memcmp(&state, &saved, sizeof(state)) != 0 ||
		memcmp(pixels, retained, sizeof(pixels)) != 0)
		return 0;

	/* A failing regular span must not publish its newly computed steps. */
	texture.zero_transparent = 0;
	triangle.xy[2][1] = 8;
	memset(indices, 2, sizeof(indices));

	return mh_software_triangle(&surface, &triangle, &state, NULL) ==
		MH_ERROR_RANGE &&
		memcmp(&state, &saved, sizeof(state)) == 0 &&
		memcmp(pixels, retained, sizeof(pixels)) == 0;
}

int
main(void)
{
	return !texture_guards(0) || !texture_guards(1) || !fallback_guards();
}
