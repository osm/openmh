#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/ilbm.h"
#include "engine/catalog/track.h"
#include "engine/core/error.h"
#include "engine/render/mesh.h"

static int failures;
#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #x);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static void
write_le32(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static void
prepare_archive(unsigned char data[268], unsigned int red)
{
	unsigned char *bitmap, *record;
	unsigned int i;

	memset(data, 0, 268);
	memcpy(data, "PDI1", 4);
	write_le32(data + 4, 2);
	write_le32(data + 8, 2);
	write_le32(data + 12, 32);
	write_le32(data + 16, 36);
	write_le32(data + 20, 140);
	write_le32(data + 24, 128);
	data[32] = 3;

	for (i = 0; i < 2; i++) {
		record = data + 36 + 52 * i;
		write_le32(record + 12, 64 * i);
		write_le32(record + 16, 64);
		strcpy((char *)record + 20,
			i == 0 ? "opaque.iff" : "cutout.iff");
		bitmap = data + 140 + 64 * i;
		memcpy(bitmap, "FORM", 4);
		bitmap[7] = 56;
		memcpy(bitmap + 8, "ILBMBMHD", 8);
		bitmap[19] = 20;
		bitmap[21] = 2;
		bitmap[23] = 1;
		bitmap[28] = 1;
		memcpy(bitmap + 40, "CMAP", 4);
		bitmap[47] = 6;
		bitmap[48] = bitmap[51] = (unsigned char)(red + i);
		bitmap[49] = bitmap[52] = 200;
		bitmap[50] = bitmap[53] = 51;
		memcpy(bitmap + 54, "BODY", 4);
		bitmap[61] = 2;
		bitmap[62] = 0x40;
	}
}

static void
check_texture_precision(void)
{
	static const enum mh_texture_format texture_formats[] = {
		MH_TEXTURE_RGB565, MH_TEXTURE_RGB555, MH_TEXTURE_XRGB8888,
		MH_TEXTURE_ARGB4444};

	static const unsigned char source[] = {
		123, 126, 7, 0, 255, 255, 255, 127};

	static const unsigned char expected[][8] = {
		{123, 125, 0, 255, 255, 255, 255, 255},
		{123, 123, 0, 255, 255, 255, 255, 255},
		{123, 126, 7, 255, 255, 255, 255, 255},
		{119, 119, 0, 0, 255, 255, 255, 119}};

	struct mh_image image;
	unsigned char rgba[8];
	size_t i;

	image.width = 1;
	image.height = 2;
	image.rgba = rgba;

	for (i = 0; i < sizeof(texture_formats) / sizeof(texture_formats[0]);
		i++) {
		memcpy(rgba, source, sizeof(rgba));
		CHECK(mh_image_texture_format(&image, texture_formats[i],
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(rgba, expected[i], sizeof(rgba)) == 0);
		CHECK(mh_image_texture_format(&image, texture_formats[i],
			NULL) == MH_ERROR_NONE);
		CHECK(memcmp(rgba, expected[i], sizeof(rgba)) == 0);
	}
}

int
main(void)
{
	struct mh_mesh mesh;
	struct mh_draw draws[5];
	struct mh_image images[4];
	struct mh_track track;
	struct mh_image_collection *first_archive = NULL,
				*second_archive = NULL;
	unsigned char first[268], second[268], saved[8], *saved_pixels;
	size_t i;
	double invalid[3];

	check_texture_precision();
	prepare_archive(first, 10);
	prepare_archive(second, 80);
	CHECK(mh_image_pdi_open(first, sizeof(first), &first_archive, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_image_pdi_open(second, sizeof(second), &second_archive,
		NULL) == MH_ERROR_NONE);

	/* The collection owns its bytes; the input buffers can be reused. */
	memset(first, 0, sizeof(first));
	memset(second, 0, sizeof(second));
	memset(&mesh, 0, sizeof(mesh));
	memset(draws, 0, sizeof(draws));
	memset(&track, 0, sizeof(track));
	mesh.textures = images;
	mesh.texture_count = 4;
	mesh.draws = draws;
	mesh.draw_count = 5;
	mesh.environment_texture = 3;

	for (i = 0; i < 4; i++) {
		images[i].width = 2;
		images[i].height = 1;
		images[i].rgba = (unsigned char *)malloc(8);

		if (images[i].rgba == NULL)
			return 1;

		memset(images[i].rgba, 255, 8);
	}

	strcpy(track.zero_transparent, "cutout.iff");
	track.zero_transparent_size = 11;
	CHECK(mh_track_texture_cutout(&track, "CUTOUT.IFF"));
	CHECK(!mh_track_texture_cutout(&track, "cutout") &&
		!mh_track_texture_cutout(&track, "cutout.iff.extra"));
	CHECK(mh_track_zero_transparent(&track, "cutout"));
	track.texture_brightness[0] = 1.5;
	track.texture_brightness[1] = 2;
	track.texture_brightness[2] = .5;
	draws[0].texture = 1;
	strcpy(draws[0].texture_name, "opaque.iff");
	draws[1] = draws[0];
	draws[2].texture = 2;
	strcpy(draws[2].texture_name, "cutout.iff");
	draws[3].texture = 0;
	draws[4].texture = 3;
	strcpy(draws[4].texture_name, "reflection");
	CHECK(mh_mesh_prepare_track_textures(mh_image_fallback_decoder(), &mesh,
		first_archive, second_archive, &track,
		MH_TEXTURE_PALETTE_RGB, MH_TEXTURE_ARGB1555,
		NULL) == MH_ERROR_NONE);
	CHECK(images[1].rgba[0] == 120 && images[2].rgba[0] == 16);
	CHECK(images[1].rgba[1] == 255 && images[1].rgba[2] == 25);
	CHECK(images[1].rgba[3] == 255 && images[1].rgba[7] == 255);
	CHECK(images[2].rgba[3] == 0 && images[2].rgba[7] == 255);
	CHECK(images[2].rgba[1] == 255 && images[2].rgba[2] == 24);

	for (i = 0; i < 8; i++)
		CHECK(images[0].rgba[i] == 255 && images[3].rgba[i] == 255);

	CHECK(mh_mesh_texture_format(&mesh, 1) == MH_TEXTURE_PALETTE_RGB);
	CHECK(mh_mesh_texture_format(&mesh, 2) == MH_TEXTURE_ARGB1555);
	CHECK(mh_mesh_texture_format(&mesh, 3) == 0);
	mesh.texture_formats[3] = MH_TEXTURE_RGB555;
	CHECK(mh_mesh_prepare_track_textures(mh_image_fallback_decoder(), &mesh,
		first_archive, second_archive, &track, MH_TEXTURE_RGB565,
		MH_TEXTURE_ARGB1555, NULL) == MH_ERROR_NONE);
	CHECK(images[1].rgba[0] == 123 && images[1].rgba[1] == 255 &&
		images[1].rgba[2] == 24 && images[1].rgba[3] == 255);
	CHECK(images[2].rgba[0] == 16 && images[2].rgba[3] == 0 &&
		images[2].rgba[7] == 255);
	CHECK(mh_mesh_texture_format(&mesh, 1) == MH_TEXTURE_RGB565);
	CHECK(mh_mesh_texture_format(&mesh, 3) == MH_TEXTURE_RGB555);
	CHECK(mh_mesh_prepare_track_textures(mh_image_fallback_decoder(), &mesh,
		first_archive, second_archive, &track,
		MH_TEXTURE_XRGB8888, MH_TEXTURE_ARGB1555,
		NULL) == MH_ERROR_NONE);
	CHECK(images[1].rgba[0] == 120 && images[1].rgba[2] == 25);

	CHECK(mh_mesh_prepare_track_textures(mh_image_fallback_decoder(), &mesh,
		first_archive, first_archive, &track, MH_TEXTURE_RGB555,
		MH_TEXTURE_ARGB4444, NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_texture_format(&mesh, 1) == MH_TEXTURE_RGB555 &&
		mh_mesh_texture_format(&mesh, 2) == MH_TEXTURE_ARGB4444);
	CHECK(images[1].rgba[0] == 8 && images[1].rgba[1] == 255 &&
		images[1].rgba[2] == 24 && images[1].rgba[3] == 255);
	CHECK(images[2].rgba[0] == 17 && images[2].rgba[1] == 255 &&
		images[2].rgba[2] == 17 && images[2].rgba[3] == 0);
	CHECK(mh_mesh_prepare_track_textures(mh_image_fallback_decoder(), &mesh,
		first_archive, second_archive, &track,
		MH_TEXTURE_XRGB8888, MH_TEXTURE_ARGB1555,
		NULL) == MH_ERROR_NONE);

	memcpy(saved, images[1].rgba, 8);
	CHECK(mh_mesh_prepare_track_textures(mh_image_fallback_decoder(), &mesh,
		first_archive, second_archive, &track,
		MH_TEXTURE_PALETTE_RGB, MH_TEXTURE_ARGB1555,
		NULL) == MH_ERROR_NONE);
	CHECK(memcmp(saved, images[1].rgba, 8) == 0);
	saved_pixels = images[1].rgba;
	CHECK(mh_image_texture_format(&images[1], (enum mh_texture_format)0,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(saved, images[1].rgba, 8) == 0);
	strcpy(draws[2].texture_name, "missing.iff");
	CHECK(mh_mesh_prepare_track_textures(mh_image_fallback_decoder(), &mesh,
		first_archive, second_archive, &track,
		MH_TEXTURE_PALETTE_RGB, MH_TEXTURE_ARGB1555,
		NULL) != MH_ERROR_NONE);
	CHECK(saved_pixels == images[1].rgba &&
		memcmp(saved, images[1].rgba, 8) == 0);
	strcpy(draws[2].texture_name, "cutout.iff");
	images[2].width = 1;
	CHECK(mh_mesh_prepare_track_textures(mh_image_fallback_decoder(), &mesh,
		first_archive, second_archive, &track,
		MH_TEXTURE_PALETTE_RGB, MH_TEXTURE_ARGB1555,
		NULL) != MH_ERROR_NONE);
	CHECK(saved_pixels == images[1].rgba &&
		memcmp(saved, images[1].rgba, 8) == 0);
	invalid[0] = 0;
	invalid[1] = 1;
	invalid[2] = -1;
	CHECK(mh_image_texture_brightness(&images[1], invalid, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(saved, images[1].rgba, 8) == 0);
	invalid[2] = 101;
	CHECK(mh_image_texture_brightness(&images[1], invalid, NULL) !=
		MH_ERROR_NONE);
	CHECK(memcmp(saved, images[1].rgba, 8) == 0);
	images[1].width = 16777217UL;
	CHECK(mh_image_texture_brightness(&images[1], track.texture_brightness,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_image_texture_format(&images[1], MH_TEXTURE_ARGB1555, NULL) !=
		MH_ERROR_NONE);

	for (i = 0; i < 4; i++)
		free(images[i].rgba);

	free(mesh.texture_formats);

	mh_image_collection_close(first_archive);
	mh_image_collection_close(second_archive);

	return failures != 0;
}
