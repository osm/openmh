#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/storage/memory_resource.h"
#include "engine/assets/atlas.h"
#include "engine/assets/ilbm.h"
#include "engine/assets/remap.h"
#include "engine/core/error.h"
#include "engine/render/mesh.h"
#include "engine/render/texture_policy.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "Atlas line %d: %s\n", __LINE__, #x);  \
			return 1;                                              \
		}                                                              \
	} while (0)

enum {
	ARCHIVE_BYTES = 272,
	ARCHIVE_DATA_OFFSET = 140,
	ARCHIVE_IMAGE_BYTES = 66,
	REMAP_HEADER_BYTES = 28,
	REMAP_ENTRY_BYTES = 49
};

static enum mh_error_code
choose_indexed(const struct mh_atlas_map *full,
	const struct mh_atlas_map *medium, unsigned long budget,
	unsigned int *quality, struct mh_error *error)
{
	return mh_scene_texture_policy(MH_RENDER_GLIDE)
		->choose_atlas(full, medium, budget, MH_TEXTURE_PALETTE_RGB, 1,
			quality, error);
}

static enum mh_error_code
choose_formats(const struct mh_atlas_map *full,
	const struct mh_atlas_map *medium, enum mh_texture_format format,
	unsigned int levels, unsigned long budget, unsigned int *quality,
	struct mh_error *error)
{
	return mh_scene_texture_policy(MH_RENDER_DIRECT3D)
		->choose_atlas(
			full, medium, budget, format, levels, quality, error);
}

static int
budgets(void)
{
	struct mh_atlas_map full, medium;
	unsigned int quality;
	unsigned long high, middle;

	memset(&full, 0, sizeof(full));
	memset(&medium, 0, sizeof(medium));
	full.pages = 20;
	full.cutout_pages = 3;
	medium.pages = 9;
	medium.cutout_pages = 2;
	high = 2064384UL;
	middle = 1277952UL;
	quality = 99;
	CHECK(choose_indexed(&full, &medium, high + 1, &quality, NULL) ==
			MH_ERROR_NONE &&
		quality == 0);
	CHECK(choose_indexed(&full, &medium, high, &quality, NULL) ==
			MH_ERROR_NONE &&
		quality == 1);
	CHECK(choose_indexed(&full, &medium, middle + 1, &quality, NULL) ==
			MH_ERROR_NONE &&
		quality == 1);
	CHECK(choose_indexed(&full, &medium, middle, &quality, NULL) !=
			MH_ERROR_NONE &&
		quality == 1);
	CHECK(choose_indexed(&full, &medium, 0, &quality, NULL) !=
			MH_ERROR_NONE &&
		quality == 1);
	full.pages = 0;
	CHECK(choose_indexed(&full, &medium, high + 1, &quality, NULL) !=
			MH_ERROR_NONE &&
		quality == 1);
	full.pages = 4097;
	CHECK(choose_indexed(&full, &medium, high + 1, &quality, NULL) !=
			MH_ERROR_NONE &&
		quality == 1);
	full.pages = 2;
	CHECK(choose_indexed(&full, &medium, high + 1, &quality, NULL) !=
			MH_ERROR_NONE &&
		quality == 1);
	full.pages = 20;
	medium.cutout_pages = 10;
	CHECK(choose_indexed(&full, &medium, high + 1, &quality, NULL) !=
			MH_ERROR_NONE &&
		quality == 1);
	CHECK(choose_indexed(NULL, &medium, high + 1, &quality, NULL) !=
		MH_ERROR_NONE);
	CHECK(choose_indexed(&full, NULL, high + 1, &quality, NULL) !=
		MH_ERROR_NONE);
	CHECK(choose_indexed(&full, &medium, high + 1, NULL, NULL) !=
		MH_ERROR_NONE);
	puts("Atlas budget: strict full/medium boundaries, exhaustion and atomic failure passed");

	return 0;
}

static int
format_budgets(void)
{
	struct mh_atlas_map full, medium;
	unsigned int quality;
	unsigned long required;

	memset(&full, 0, sizeof(full));
	memset(&medium, 0, sizeof(medium));
	full.pages = 5;
	medium.pages = 3;

	/* RGB565, three levels: trunc(655360 * binary64(1.4)) + 1114112. */
	required = 2031615UL;
	quality = 99;
	CHECK(choose_formats(&full, &medium, MH_TEXTURE_RGB565, 3, required + 1,
		&quality, NULL) == MH_ERROR_NONE &&
		quality == 0);
	CHECK(choose_formats(&full, &medium, MH_TEXTURE_RGB565, 3, required,
		&quality, NULL) == MH_ERROR_NONE &&
		quality == 1);
	CHECK(choose_formats(&full, &medium, MH_TEXTURE_XRGB8888, 3, required,
		&quality, NULL) != MH_ERROR_NONE &&
		quality == 1);
	CHECK(choose_formats(&full, &medium, MH_TEXTURE_RGB565, 1, required,
		&quality, NULL) == MH_ERROR_NONE &&
		quality == 0);

	return 0;
}

static int
profile_budgets(void)
{
	const struct mh_texture_policy *policy;
	struct mh_atlas_map full, medium;
	unsigned int quality, i;

	static const enum mh_render_profile profiles[] = {MH_RENDER_GLIDE,
		MH_RENDER_DIRECT3D, MH_RENDER_POWERVR, MH_RENDER_OPENMH,
		MH_RENDER_SOFTWARE};

	static const unsigned long capacities[] = {
		4194304UL, 33554432UL, 6291456UL, 4194304UL, 4194304UL};

	memset(&full, 0, sizeof(full));
	memset(&medium, 0, sizeof(medium));
	full.pages = 5;
	medium.pages = 3;

	for (i = 0; i < sizeof(profiles) / sizeof(profiles[0]); i++) {
		if (!mh_render_profile_available(profiles[i]))
			continue;

		policy = mh_scene_texture_policy(profiles[i]);
		CHECK(policy->capacity == capacities[i]);
		CHECK(policy->shared_palette ==
			(profiles[i] == MH_RENDER_GLIDE));
		quality = 99;

		if (profiles[i] == MH_RENDER_POWERVR) {
			/*
			 * PowerVR must still load medium textures with zero
			 * budget.
			 */
			CHECK(policy->choose_atlas(&full, &medium, 0,
				MH_TEXTURE_RGB565, 1, &quality,
				NULL) == MH_ERROR_NONE);
			CHECK(quality == 1);
		} else {
			CHECK(policy->choose_atlas(&full, &medium, 0,
				MH_TEXTURE_RGB565, 1, &quality,
				NULL) == MH_ERROR_INVALID);
			CHECK(quality == 99);
		}
	}

	return 0;
}

static int
coordinates(void)
{
	struct mh_atlas_map_entry entry, bad;
	struct mh_image source, page;
	unsigned char pixels[24], storage[80], saved[80];
	float uv[2], output[2];
	unsigned int i, n;

	memset(&entry, 0, sizeof(entry));
	entry.source_width = 3;
	entry.source_height = 2;
	entry.scaled_width = 3;
	entry.scaled_height = 2;
	entry.transpose = 1;
	entry.x = entry.y = 1;
	source.width = 3;
	source.height = 2;
	source.rgba = pixels;
	page.width = 4;
	page.height = 5;
	page.rgba = storage;

	for (i = 0; i < sizeof(pixels); i++)
		pixels[i] = (unsigned char)(i + 1);

	memset(storage, 0xa7, sizeof(storage));
	CHECK(mh_atlas_copy(&entry, &source, 0, &page, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 20; i++) {
		if (i % 4 >= 1 && i % 4 <= 2 && i / 4 >= 1 && i / 4 <= 3) {
			n = (i % 4 - 1) * 3 + (i / 4 - 1);
			CHECK(memcmp(storage + i * 4, pixels + n * 4, 4) == 0);
		} else
			CHECK(storage[i * 4] == 0xa7 &&
				storage[i * 4 + 3] == 0xa7);
	}

	uv[0] = 2.5f;
	uv[1] = 1.5f;
	CHECK(mh_atlas_uv(&entry, 4, 5, 0, uv, output, NULL) == MH_ERROR_NONE);
	CHECK(output[0] == .625f && output[1] == .7f);

	/*
	 * Asymmetric reduced dimensions retain the original unswapped ratios.
	 */
	entry.scaled_width = 2;
	entry.scaled_height = 1;
	CHECK(mh_atlas_uv(&entry, 4, 5, 0, uv, output, NULL) == MH_ERROR_NONE);
	CHECK(output[0] == .5f && output[1] == .3f);
	memset(storage, 0xa7, sizeof(storage));
	CHECK(mh_atlas_copy(&entry, &source, 0, &page, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(storage + 20, pixels + 12, 4) == 0 &&
		memcmp(storage + 36, pixels + 20, 4) == 0);
	memcpy(saved, storage, sizeof(saved));

	for (i = 0; i < 10; i++) {
		bad = entry;

		if (i == 0)
			bad.source_width = 0;

		if (i == 1)
			bad.source_height = 16385;

		if (i == 2)
			bad.scaled_width = 0;

		if (i == 3)
			bad.scaled_height = 16385;

		if (i == 4)
			bad.transpose = 2;

		if (i == 5)
			bad.x = ~0UL;

		if (i == 6)
			bad.y = 5;

		if (i == 7)
			bad.scaled_width = 5;

		if (i == 8)
			bad.scaled_height = 4;

		if (i == 9)
			bad.source_width = 1;

		output[0] = 77;
		output[1] = 88;
		CHECK(mh_atlas_uv(&bad, 4, 5, 0, uv, output, NULL) !=
				MH_ERROR_NONE &&
			output[0] == 77 && output[1] == 88);
		CHECK(mh_atlas_copy(&bad, &source, 0, &page, NULL) !=
				MH_ERROR_NONE &&
			memcmp(storage, saved, sizeof(saved)) == 0);
	}

	uv[0] = FLT_MAX;
	CHECK(mh_atlas_uv(&entry, 4, 5, 0, uv, output, NULL) != MH_ERROR_NONE &&
		output[0] == 77 && output[1] == 88);
	source.rgba = storage;
	CHECK(mh_atlas_copy(&entry, &source, 0, &page, NULL) != MH_ERROR_NONE &&
		memcmp(storage, saved, sizeof(saved)) == 0);
	source.rgba = NULL;
	CHECK(mh_atlas_copy(&entry, &source, 0, &page, NULL) != MH_ERROR_NONE);
	CHECK(mh_atlas_uv(NULL, 4, 5, 0, uv, output, NULL) != MH_ERROR_NONE);
	CHECK(mh_atlas_copy(&entry, NULL, 0, &page, NULL) != MH_ERROR_NONE);
	CHECK(mh_atlas_copy(&entry, &source, 0, NULL, NULL) != MH_ERROR_NONE);
	puts("Atlas: transpose, reduced sampling, borders and atomic failures passed");

	return 0;
}

static void
write_u32(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static void
build_archive(unsigned char data[ARCHIVE_BYTES], unsigned int red)
{
	unsigned char *image, *record;
	unsigned int i;

	memset(data, 0, ARCHIVE_BYTES);
	memcpy(data, "PDI1", 4);
	write_u32(data + 4, 2);
	write_u32(data + 8, 2);
	write_u32(data + 12, 32);
	write_u32(data + 16, 36);
	write_u32(data + 20, ARCHIVE_DATA_OFFSET);
	write_u32(data + 24, 132);
	data[32] = 3;

	for (i = 0; i < 2; i++) {
		record = data + 36 + 52 * i;
		write_u32(record + 12, ARCHIVE_IMAGE_BYTES * i);
		write_u32(record + 16, ARCHIVE_IMAGE_BYTES);
		strcpy((char *)record + 20,
			i == 0 ? "opaque.iff" : "cutout.iff");
		image = data + ARCHIVE_DATA_OFFSET + ARCHIVE_IMAGE_BYTES * i;
		memcpy(image, "FORM", 4);
		image[7] = 58;
		memcpy(image + 8, "ILBMBMHD", 8);
		image[19] = 20;
		image[21] = image[23] = 2;
		image[28] = 1;
		memcpy(image + 40, "CMAP", 4);
		image[47] = 6;
		image[48] = image[51] = (unsigned char)(red + i);
		image[49] = image[52] = 200;
		image[50] = image[53] = 51;
		memcpy(image + 54, "BODY", 4);
		image[61] = 4;
		image[62] = 0x40;
		image[64] = 0x80;
	}
}

static int
padded(void)
{
	static const unsigned char expected[64] = {0, 1, 9, 11, 13, 15, 6, 7, 8,
		9, 9, 11, 13, 15, 14, 15, 9, 9, 9, 11, 13, 15, 13, 15, 25, 25,
		25, 27, 29, 31, 29, 31, 41, 41, 41, 43, 45, 47, 45, 47, 57, 57,
		57, 59, 61, 63, 61, 63, 48, 49, 41, 43, 45, 47, 54, 55, 56, 57,
		57, 59, 61, 63, 62, 63};

	struct mh_image source, page;
	struct mh_atlas_map_entry entry;
	unsigned char pixels[256], output[256];
	float uv[2], mapped[2];
	unsigned int i;

	memset(&entry, 0, sizeof(entry));
	entry.source_width = entry.scaled_width = 8;
	entry.source_height = entry.scaled_height = 8;
	source.width = source.height = page.width = page.height = 8;
	source.rgba = pixels;
	page.rgba = output;

	for (i = 0; i < 64; i++)
		memset(pixels + i * 4, (int)i, 4);

	/* Corners survive, strips overlap, center wins. */
	CHECK(mh_atlas_copy(&entry, &source, MH_ATLAS_MIP_BORDER, &page,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < sizeof(output); i++)
		CHECK(output[i] == expected[i / 4]);

	uv[0] = .5f;
	uv[1] = 7.5f;
	CHECK(mh_atlas_uv(&entry, 8, 8, MH_ATLAS_MIP_BORDER, uv, mapped,
		NULL) == MH_ERROR_NONE);
	CHECK(mapped[0] == .3125f && mapped[1] == .6875f);
	entry.scaled_width = 4;
	CHECK(mh_atlas_copy(&entry, &source, MH_ATLAS_MIP_BORDER, &page,
		NULL) != MH_ERROR_NONE);

	for (i = 0; i < sizeof(output); i++)
		CHECK(output[i] == expected[i / 4]);

	return 0;
}

static int
assets(void)
{
	unsigned char first[ARCHIVE_BYTES], second[ARCHIVE_BYTES], data[126],
		saved[64], white[4];
	struct mh_image_collection *tex1, *tex2;
	const struct mh_resource_source *resources;
	struct mh_resource_entry entries[2];
	struct mh_atlas_map remap;
	struct mh_atlas atlas, before;
	struct mh_atlas_options options;
	struct mh_track track;
	struct mh_mesh mesh;
	struct mh_vertex vertices[9];
	struct mh_draw draws[3];
	struct mh_image images[4], image;
	struct mh_image_palette palette, old_palette;
	float *old_uv;
	size_t *old_images;
	unsigned int i, entry_offset;

	build_archive(first, 40);
	build_archive(second, 80);
	entries[0].path = "root/first.pdi";
	entries[0].data = first;
	entries[0].size = sizeof(first);
	entries[1].path = "root/second.pdi";
	entries[1].data = second;
	entries[1].size = sizeof(second);
	CHECK(mh_memory_resources_open(entries, 2, &resources, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_image_collection_open(mh_image_pdi_source(), resources, "root",
		"first.pdi", &tex1, NULL) == MH_ERROR_NONE);
	CHECK(mh_image_collection_open(mh_image_pdi_source(), resources, "root",
		"second.pdi", &tex2, NULL) == MH_ERROR_NONE);

	memset(data, 0, sizeof(data));
	write_u32(data, 2);
	write_u32(data + 4, 2);
	write_u32(data + 8, 1);
	write_u32(data + 12, 4);
	write_u32(data + 16, 4);
	write_u32(data + 20, 0x3f800000UL);
	write_u32(data + 24, 0x3f800000UL);

	for (i = 0; i < 2; i++) {
		entry_offset = REMAP_HEADER_BYTES + REMAP_ENTRY_BYTES * i;
		strcpy((char *)data + entry_offset,
			i == 0 ? "opaque.iff" : "cutout.iff");
		write_u32(data + entry_offset + 13, i);
		write_u32(data + entry_offset + 21, 1);
		write_u32(data + entry_offset + 25, 1);
		write_u32(data + entry_offset + 29, i);
		write_u32(data + entry_offset + 33, 2);
		write_u32(data + entry_offset + 37, 2);
		write_u32(data + entry_offset + 41, 2);
		write_u32(data + entry_offset + 45, 2);
	}

	CHECK(mh_atlas_remap_decode(data, sizeof(data), &remap, NULL) ==
		MH_ERROR_NONE);
	memset(data, 0xa5, sizeof(data));
	memset(&track, 0, sizeof(track));
	track.texture_brightness[0] = track.texture_brightness[1] =
		track.texture_brightness[2] = 1;
	strcpy(track.zero_transparent, "cutout.iff");
	track.zero_transparent_size = 11;
	memset(&atlas, 0, sizeof(atlas));
	options.shared_palette = 1;
	options.border = 0;
	options.opaque_format = MH_TEXTURE_PALETTE_RGB;
	options.alpha_format = MH_TEXTURE_ARGB1555;
	CHECK(mh_atlas_build(mh_image_fallback_decoder(), &remap, tex1, tex2,
		&track, &options, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(atlas.count == 2 && atlas.pages[0].width == 4 &&
		atlas.pages[0].height == 4);
	CHECK(memcmp(atlas.pages[0].rgba, "\120\310\063\377", 4) == 0);
	CHECK(memcmp(atlas.pages[1].rgba, "\0\0\0\0", 4) == 0);
	CHECK(memcmp(atlas.pages[1].rgba + 20, "\051\316\061\0", 4) == 0);
	CHECK(atlas.pages[1].rgba[24 + 3] == 255 &&
		atlas.pages[1].rgba[36 + 3] == 255);
	CHECK(atlas.formats[0] == MH_TEXTURE_PALETTE_RGB &&
		atlas.formats[1] == MH_TEXTURE_ARGB1555);
	mh_atlas_free(&atlas);
	options.opaque_format = MH_TEXTURE_RGB565;
	CHECK(mh_atlas_build(mh_image_fallback_decoder(), &remap, tex1, tex2,
		&track, &options, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(atlas.formats[0] == MH_TEXTURE_RGB565 &&
		atlas.formats[1] == MH_TEXTURE_ARGB1555);

	/* Both the copied texels and unused page background are quantized. */
	CHECK(atlas.pages[0].rgba[0] == 82 && atlas.pages[0].rgba[1] == 203 &&
		atlas.pages[0].rgba[2] == 49);
	CHECK(atlas.pages[0].rgba[20] == 82 && atlas.pages[0].rgba[21] == 203);
	CHECK(atlas.pages[1].rgba[23] == 0 && atlas.pages[1].rgba[27] == 255);
	mh_atlas_free(&atlas);
	options.opaque_format = MH_TEXTURE_XRGB8888;
	CHECK(mh_atlas_build(mh_image_fallback_decoder(), &remap, tex1, tex2,
		&track, &options, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(atlas.pages[0].rgba[0] == 80 && atlas.pages[0].rgba[1] == 200);

	mh_atlas_free(&atlas);
	options.opaque_format = MH_TEXTURE_RGB555;
	options.alpha_format = MH_TEXTURE_ARGB4444;
	options.shared_palette = 0;
	CHECK(mh_atlas_build(mh_image_fallback_decoder(), &remap, tex1, tex1,
		&track, &options, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(atlas.formats[0] == MH_TEXTURE_RGB555 &&
		atlas.formats[1] == MH_TEXTURE_ARGB4444);

	/* Individual RGB palettes use black gaps, preserving cutout alpha. */
	CHECK(memcmp(atlas.pages[0].rgba, "\0\0\0\377", 4) == 0);
	CHECK(memcmp(atlas.pages[0].rgba + 20, "\051\316\061\377", 4) == 0);
	CHECK(memcmp(atlas.pages[1].rgba + 20, "\042\314\063\0", 4) == 0);
	CHECK(atlas.pages[1].rgba[27] == 255 && atlas.pages[1].rgba[39] == 255);
	mh_atlas_free(&atlas);
	options.opaque_format = MH_TEXTURE_XRGB8888;
	options.alpha_format = MH_TEXTURE_ARGB1555;
	options.shared_palette = 1;
	CHECK(mh_atlas_build(mh_image_fallback_decoder(), &remap, tex1, tex2,
		&track, &options, &atlas, NULL) == MH_ERROR_NONE);

	options.opaque_format = MH_TEXTURE_PALETTE_RGB;
	before = atlas;
	memcpy(saved, atlas.pages[0].rgba, 64);
	remap.entries[1].page = 0;
	CHECK(mh_atlas_build(mh_image_fallback_decoder(), &remap, tex1, tex2,
		&track, &options, &atlas, NULL) != MH_ERROR_NONE);
	remap.entries[1].page = 1;
	remap.entries[0].source_width = 3;
	CHECK(mh_atlas_build(mh_image_fallback_decoder(), &remap, tex1, tex2,
		&track, &options, &atlas, NULL) != MH_ERROR_NONE);
	remap.entries[0].source_width = 2;
	track.zero_transparent_size = 0;
	CHECK(mh_atlas_build(mh_image_fallback_decoder(), &remap, tex1, tex2,
		&track, &options, &atlas, NULL) != MH_ERROR_NONE);
	track.zero_transparent_size = 11;
	CHECK(memcmp(&before, &atlas, sizeof(atlas)) == 0 &&
		memcmp(saved, atlas.pages[0].rgba, 64) == 0);
	memset(&image, 0, sizeof(image));
	memset(&palette, 0xa3, sizeof(palette));
	old_palette = palette;
	CHECK(mh_image_ilbm_decode_texture_palette(first + ARCHIVE_DATA_OFFSET,
		ARCHIVE_IMAGE_BYTES - 1, &image, &palette,
		NULL) != MH_ERROR_NONE);
	CHECK(image.rgba == NULL &&
		memcmp(&palette, &old_palette, sizeof(palette)) == 0);
	CHECK(mh_image_ilbm_decode_texture_palette(first + ARCHIVE_DATA_OFFSET,
		ARCHIVE_IMAGE_BYTES, &image, &palette,
		NULL) == MH_ERROR_NONE);
	CHECK(palette.count == 2 && palette.rgb[0] == 40 &&
		palette.rgb[1] == 200 && palette.rgb[6] == 0);
	free(image.rgba);
	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(draws, 0, sizeof(draws));
	memset(images, 0, sizeof(images));
	memset(white, 255, 4);

	for (i = 0; i < 4; i++) {
		images[i].width = images[i].height = 2;
		images[i].rgba = first;
	}

	images[0].width = images[0].height = 1;
	images[0].rgba = white;
	mesh.vertices = vertices;
	mesh.vertex_count = 9;
	mesh.draws = draws;
	mesh.draw_count = 3;
	mesh.textures = images;
	mesh.texture_count = 4;
	mesh.environment_texture = 3;

	for (i = 0; i < 3; i++) {
		draws[i].first = i * 3;
		draws[i].count = 3;
		draws[i].texture = i < 2 ? i + 1 : 0;

		if (i < 2)
			strcpy(draws[i].texture_name,
				i == 0 ? "OPAQUE.IFF" : "cutout.iff");

		vertices[i * 3].uv[0] = vertices[i * 3].uv[1] = .25f;
		vertices[i * 3 + 1].uv[0] = .75f;
		vertices[i * 3 + 1].uv[1] = .25f;
		vertices[i * 3 + 2].uv[0] = .25f;
		vertices[i * 3 + 2].uv[1] = .75f;
	}

	CHECK(mh_mesh_bind_atlas(&mesh, &remap, &atlas, NULL) == MH_ERROR_NONE);
	CHECK(mh_mesh_texture(&mesh, 0) == images &&
		mh_mesh_texture(&mesh, 1) == atlas.pages);
	CHECK(mh_mesh_texture_format(&mesh, 0) == 0 &&
		mh_mesh_texture_format(&mesh, 1) == MH_TEXTURE_XRGB8888 &&
		mh_mesh_texture_format(&mesh, 2) == MH_TEXTURE_ARGB1555);

	CHECK(mh_mesh_texture(&mesh, 2) == atlas.pages + 1 &&
		mh_mesh_texture(&mesh, 3) == images + 3);
	CHECK(mesh.atlas_uv[0] == .375f && mesh.atlas_uv[2] == .625f &&
		mesh.atlas_uv[8] == .375f && mesh.atlas_uv[9] == .625f);
	CHECK(mesh.atlas_uv[12] == .25f && vertices[0].uv[0] == .25f);
	CHECK(mh_mesh_bind_atlas(&mesh, &remap, &atlas, NULL) == MH_ERROR_NONE);
	old_uv = mesh.atlas_uv;
	old_images = mesh.atlas_images;
	vertices[0].uv[0] = .1f;
	CHECK(mh_mesh_bind_atlas(&mesh, &remap, &atlas, NULL) != MH_ERROR_NONE);
	vertices[0].uv[0] = .25f;
	draws[2].first = 0;
	CHECK(mh_mesh_bind_atlas(&mesh, &remap, &atlas, NULL) != MH_ERROR_NONE);
	draws[2].first = 6;
	strcpy(draws[0].texture_name, "missing.iff");
	CHECK(mh_mesh_bind_atlas(&mesh, &remap, &atlas, NULL) != MH_ERROR_NONE);
	CHECK(mesh.atlas_uv == old_uv && mesh.atlas_images == old_images &&
		mesh.atlas_uv[0] == .375f);
	CHECK(mh_mesh_texture(&mesh, 4) == NULL);
	free(mesh.atlas_uv);
	free(mesh.atlas_images);
	mh_atlas_free(&atlas);
	CHECK(atlas.pages == NULL && atlas.count == 0);
	mh_image_collection_close(tex2);
	mh_image_collection_close(tex1);
	mh_memory_resources_close(resources);
	mh_atlas_map_free(&remap);
	puts("Atlas assets: palettes, archive choice, packing, shared mesh bindings and failure atomicity passed");

	return 0;
}

int
main(void)
{
	return coordinates() || padded() || assets() || budgets() ||
		(MH_RENDER_BUILD_DIRECT3D && format_budgets()) ||
		profile_budgets();
}
