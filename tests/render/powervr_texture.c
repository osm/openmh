#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/render/mesh.h"
#include "engine/render/powervr_texture.h"

static int
check_usage(void)
{
	struct mh_mesh mesh;
	struct mh_atlas atlas;
	struct mh_draw draws[2];

	memset(&mesh, 0, sizeof(mesh));
	memset(&atlas, 0, sizeof(atlas));
	memset(draws, 0, sizeof(draws));
	mesh.environment_texture = 3;

	if (mh_powervr_texture_unused(&mesh, 2))
		return 1;

	mesh.atlas = &atlas;
	mesh.draws = draws;
	mesh.draw_count = 2;
	draws[0].texture = 1;
	draws[1].texture = 4;

	if (mh_powervr_texture_unused(&mesh, 0) ||
		mh_powervr_texture_unused(&mesh, 1) ||
		mh_powervr_texture_unused(&mesh, 3) ||
		mh_powervr_texture_unused(&mesh, 4) ||
		!mh_powervr_texture_unused(&mesh, 2))
		return 1;

	mesh.draw_count = 0;

	return !mh_powervr_texture_unused(&mesh, 1);
}

static int
check_texture(void)
{
	unsigned char source_pixels[17 * 4], output_pixels[32 * 32 * 4];
	struct mh_image source, output;
	unsigned int side;

	if (check_usage())
		return 1;

	side = 123;

	if (mh_powervr_texture_size(0, 16, &side, NULL) != MH_ERROR_INVALID ||
		side != 123 ||
		mh_powervr_texture_size(257, 32, &side, NULL) !=
			MH_ERROR_INVALID ||
		side != 123 ||
		mh_powervr_texture_size(16, 16, &side, NULL) != MH_ERROR_NONE ||
		side != 32 ||
		mh_powervr_texture_size(17, 1, &side, NULL) != MH_ERROR_NONE ||
		side != 32 ||
		mh_powervr_texture_size(128, 256, &side, NULL) !=
			MH_ERROR_NONE ||
		side != 256)
		return 1;

	memset(source_pixels, 127, sizeof(source_pixels));
	memset(output_pixels, 0xa5, sizeof(output_pixels));
	source.width = 17;
	source.height = 1;
	source.rgba = source_pixels;
	output.width = output.height = 32;
	output.rgba = output_pixels;

	if (mh_powervr_texture_prepare(&source, MH_TEXTURE_ARGB4444, &output,
		NULL) != MH_ERROR_NONE ||
		memcmp(output_pixels, "\x77\x77\x77\x77", 4) != 0 ||
		memcmp(output_pixels + 17 * 4, "\0\0\0\0", 4) != 0 ||
		source_pixels[0] != 127 || source_pixels[3] != 127)
		return 1;

	if (mh_powervr_texture_prepare(&source, MH_TEXTURE_RGB555, &output,
		NULL) != MH_ERROR_NONE ||
		memcmp(output_pixels, "\x7b\x7b\x7b\xff", 4) != 0 ||
		memcmp(output_pixels + 17 * 4, "\0\0\0\xff", 4) != 0)
		return 1;

	source.width = 1;

	if (mh_powervr_texture_prepare(&source, MH_TEXTURE_ARGB4444, &output,
		NULL) != MH_ERROR_NONE ||
		memcmp(output_pixels, "\x77\x77\x77\x77", 4) != 0 ||
		memcmp(output_pixels + 4, "\0\0\0\0", 4) != 0)
		return 1;

	output.height = 31;

	if (mh_powervr_texture_prepare(&source, MH_TEXTURE_RGB555, &output,
		NULL) != MH_ERROR_INVALID ||
		memcmp(output_pixels, "\x77\x77\x77\x77", 4) != 0)
		return 1;

	return 0;
}

int
main(void)
{
	return check_texture();
}
