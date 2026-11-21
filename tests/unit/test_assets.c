#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/ilbm.h"
#include "engine/assets/myo_decode.h"
#include "engine/assets/pdi.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/geometry/model.h"
#include "engine/render/mesh.h"

#define MUTATION_MULTIPLIER 1664525UL
#define MUTATION_INCREMENT 1013904223UL

static int failures;

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "line %d: %s\n", __LINE__,             \
				#expression);                                  \
			failures++;                                            \
		}                                                              \
	} while (0)

/* Keep raw-format validation separate from decoded mesh construction. */
static enum mh_error_code
build_model(const struct mh_myo *source,
	const struct mh_image_collection *archive, struct mh_mesh *mesh,
	int native)
{
	struct mh_model model;
	enum mh_error_code status;

	status = mh_model_myo_expand(source, &model, NULL);

	if (status != MH_ERROR_NONE)
		return status;

	status = native ? mh_mesh_build_native(mh_image_fallback_decoder(),
				&model, archive, mesh, NULL)
			: mh_mesh_build(mh_image_fallback_decoder(), &model,
				archive, mesh, NULL);

	mh_model_free(&model);

	return status;
}

static void
write_le32(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)value;
	bytes[1] = (unsigned char)(value >> 8);
	bytes[2] = (unsigned char)(value >> 16);
	bytes[3] = (unsigned char)(value >> 24);
}

static void
write_be32(unsigned char *bytes, unsigned long value)
{
	bytes[0] = (unsigned char)(value >> 24);
	bytes[1] = (unsigned char)(value >> 16);
	bytes[2] = (unsigned char)(value >> 8);
	bytes[3] = (unsigned char)value;
}

/* Four palette colors, six known pixel indices, and an odd unknown chunk. */
static size_t
prepare_bitmap(unsigned char *data, int compression)
{
	static const unsigned char colors[12] = {
		255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

	static const unsigned char rows[4] = {0x40, 0x20, 0xa0, 0xc0};
	size_t cursor;
	int i;

	memset(data, 0, 128);
	memcpy(data, "FORM", 4);
	memcpy(data + 8, "ILBMBMHD", 8);
	write_be32(data + 16, 20);
	data[21] = 3;
	data[23] = 2;
	data[28] = 2;
	data[30] = (unsigned char)compression;
	memcpy(data + 40, "CMAP", 4);
	write_be32(data + 44, 12);
	memcpy(data + 48, colors, 12);
	memcpy(data + 60, "JUNK", 4);
	write_be32(data + 64, 1);
	data[68] = 99;
	memcpy(data + 70, "BODY", 4);
	cursor = 78;

	for (i = 0; i < 4; i++) {
		if (compression) {
			data[cursor++] = 1;
		}

		data[cursor++] = rows[i];
		data[cursor++] = 0;
	}

	write_be32(data + 74, (unsigned long)(cursor - 78));
	write_be32(data + 4, (unsigned long)(cursor - 8));

	return cursor;
}

static void
check_palette_copy(void)
{
	struct mh_image image, expected, saved_image;
	struct mh_image_palette palette, saved_palette;
	unsigned char data[128];
	size_t size, truncated, i;
	unsigned int compression, mask;
	enum mh_error_code status;

	for (compression = 0; compression < 2; compression++) {
		for (mask = 0; mask <= 3; mask++) {
			/* This fixture stores no extra mask plane. */
			if (mask == 1)
				continue;

			size = prepare_bitmap(data, (int)compression);
			data[29] = (unsigned char)mask;
			status = mh_image_ilbm_decode(
				data, size, &expected, NULL);
			CHECK(status == MH_ERROR_NONE);

			if (status != MH_ERROR_NONE)
				continue;

			status = mh_image_ilbm_decode_image_palette(
				data, size, &image, &palette, NULL);
			CHECK(status == MH_ERROR_NONE);

			if (status == MH_ERROR_NONE) {
				CHECK(image.width == expected.width &&
					image.height == expected.height);
				CHECK(memcmp(image.rgba, expected.rgba, 24) ==
					0);
				CHECK(palette.count == 4);
				CHECK(memcmp(palette.rgb, data + 48, 12) == 0);

				for (i = 12; i < sizeof(palette.rgb); i++)
					CHECK(palette.rgb[i] == 0);

				data[48] = 0;
				CHECK(palette.rgb[0] == 255);
				free(image.rgba);
			}

			free(expected.rgba);
		}
	}

	size = prepare_bitmap(data, 1);
	memset(&image, 0xa5, sizeof(image));
	memset(&palette, 0x5a, sizeof(palette));
	saved_image = image;
	saved_palette = palette;

	for (truncated = 0; truncated < size; truncated++) {
		CHECK(mh_image_ilbm_decode_image_palette(data, truncated,
			&image, &palette, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&image, &saved_image, sizeof(image)) == 0);
		CHECK(memcmp(&palette, &saved_palette, sizeof(palette)) == 0);
	}

	CHECK(mh_image_ilbm_decode_image_palette(
		data, size, &image, NULL, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&image, &saved_image, sizeof(image)) == 0);
	CHECK(mh_image_ilbm_decode_image_palette(
		data, size, NULL, &palette, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&palette, &saved_palette, sizeof(palette)) == 0);
}

static void
check_bitmap(void)
{
	static const unsigned char expected[24] = {255, 0, 0, 255, 0, 255, 0,
		255, 0, 0, 255, 255, 255, 255, 255, 255, 0, 0, 255, 255, 0, 255,
		0, 255};

	unsigned char data[128];
	unsigned char shared[12];
	struct mh_image image;
	struct mh_image saved;
	size_t size;
	size_t truncated;
	int compression;
	int i;
	unsigned long seed;
	enum mh_error_code status;

	for (compression = 0; compression < 2; compression++) {
		size = prepare_bitmap(data, compression);
		status = mh_image_ilbm_decode(data, size, &image, NULL);
		CHECK(status == MH_ERROR_NONE);

		if (status == MH_ERROR_NONE) {
			CHECK(image.width == 3 && image.height == 2);
			CHECK(memcmp(image.rgba, expected, sizeof(expected)) ==
				0);
			free(image.rgba);
		}

		for (truncated = 0; truncated < size; truncated++) {
			CHECK(mh_image_ilbm_decode(data, truncated, &image,
				NULL) != MH_ERROR_NONE);
		}

		/*
		 * Material transparency uses source indices, even if two
		 * entries share RGB or BMHD requests a different transparent
		 * index.
		 */
		memcpy(data + 51, data + 48, 3);
		data[29] = 2;
		data[33] = 1;
		CHECK(mh_image_ilbm_decode_texture(data, size, &image, NULL) ==
			MH_ERROR_NONE);
		CHECK(image.rgba[3] == 0 && image.rgba[7] == 255);
		CHECK(memcmp(image.rgba, image.rgba + 4, 3) == 0);
		free(image.rgba);
		memset(&image, 0xa5, sizeof(image));
		saved = image;
		CHECK(mh_image_ilbm_decode_texture(
			data, size - 1, &image, NULL) != MH_ERROR_NONE);
		CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	}

	/* Animation frames may intentionally borrow the first frame's CMAP. */
	memset(shared, 17, sizeof(shared));
	shared[3] = 80;
	shared[6] = 160;
	shared[9] = 240;
	size = prepare_bitmap(data, 1);
	data[29] = 2;
	CHECK(mh_image_ilbm_decode_palette(
		data, size, shared, 4, &image, NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[0] == 17 && image.rgba[3] == 0 &&
		image.rgba[4] == 80 && image.rgba[8] == 160 &&
		image.rgba[12] == 240 && image.rgba[7] == 255);
	free(image.rgba);
	memset(&image, 0xa5, sizeof(image));
	saved = image;
	CHECK(mh_image_ilbm_decode_palette(
		data, size, shared, 3, &image, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	CHECK(mh_image_ilbm_decode_palette(
		data, size, shared, 0, &image, NULL) != MH_ERROR_NONE);
	CHECK(mh_image_ilbm_decode_palette(data, size, NULL, 4, &image, NULL) !=
		MH_ERROR_NONE);
	CHECK(mh_image_ilbm_decode_palette(
		data, size, shared, 257, &image, NULL) != MH_ERROR_NONE);
	CHECK(mh_image_ilbm_decode_palette(data, size - 1, shared, 4, &image,
		NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
	size = prepare_bitmap(data, 0);
	data[29] = 2; /* Transparent palette index zero. */
	CHECK(mh_image_ilbm_decode(data, size, &image, NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[3] == 0 && image.rgba[7] == 255);
	free(image.rgba);

	size = prepare_bitmap(data, 0);
	data[29] = 1;

	/* Two rows: plane 0, plane 1, then an explicit mask plane. */
	memmove(data + 84, data + 82, 4);
	data[82] = 0xa0;
	data[83] = 0;
	data[88] = 0x60;
	data[89] = 0;
	write_be32(data + 74, 12);
	write_be32(data + 4, 82);
	CHECK(mh_image_ilbm_decode(data, 90, &image, NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[3] == 255 && image.rgba[7] == 0 &&
		image.rgba[11] == 255 && image.rgba[15] == 0);
	free(image.rgba);
	CHECK(mh_image_ilbm_decode_texture(data, 90, &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(image.rgba[3] == 0 && image.rgba[7] == 255 &&
		image.rgba[15] == 255);
	free(image.rgba);

	size = prepare_bitmap(data, 0);
	data[23] = 3;
	data[29] = 3;
	memset(data + 78, 0, 12);
	data[78] = 0x60;
	data[82] = 0xa0;
	data[86] = 0xe0;
	write_be32(data + 74, 12);
	write_be32(data + 4, 82);
	CHECK(mh_image_ilbm_decode(data, 90, &image, NULL) == MH_ERROR_NONE);
	CHECK(image.rgba[3] == 0 && image.rgba[4 * 4 + 3] == 255);
	free(image.rgba);
	CHECK(mh_image_ilbm_decode_texture(data, 90, &image, NULL) ==
		MH_ERROR_NONE);
	CHECK(image.rgba[3] == 0 && image.rgba[4 * 4 + 3] == 0);
	free(image.rgba);

	size = prepare_bitmap(data, 1);

	for (i = 0; i < 4; i++) {
		data[78 + i * 3] = 128; /* NOP, repeat two zero bytes. */
		data[79 + i * 3] = 255;
		data[80 + i * 3] = 0;
	}

	CHECK(mh_image_ilbm_decode(data, size, &image, NULL) == MH_ERROR_NONE);

	for (i = 0; i < 6; i++) {
		CHECK(image.rgba[i * 4] == 255 && image.rgba[i * 4 + 1] == 0);
	}

	free(image.rgba);
	data[78] = 3; /* Four literal bytes cannot fit a two-byte plane row. */
	CHECK(mh_image_ilbm_decode(data, size, &image, NULL) != MH_ERROR_NONE);
	size = prepare_bitmap(data, 0);
	data[30] = 2;
	CHECK(mh_image_ilbm_decode(data, size, &image, NULL) != MH_ERROR_NONE);
	size = prepare_bitmap(data, 0);
	write_be32(data + 44, 0xfffffffeUL);
	CHECK(mh_image_ilbm_decode(data, size, &image, NULL) != MH_ERROR_NONE);
	size = prepare_bitmap(data, 0);
	data[21] = 0;
	CHECK(mh_image_ilbm_decode(data, size, &image, NULL) != MH_ERROR_NONE);

	/* Legacy writer omitted the final odd BODY pad from FORM size. */
	size = prepare_bitmap(data, 1);
	data[21] = 1;
	data[23] = 1;
	data[28] = 1;
	write_be32(data + 4, 73);
	write_be32(data + 74, 3);
	data[78] = 1;
	data[79] = 0x80;
	data[80] = 0;
	data[81] = 0;
	CHECK(mh_image_ilbm_decode(data, 82, &image, NULL) == MH_ERROR_NONE);
	CHECK(image.width == 1 && image.rgba[1] == 255);
	free(image.rgba);
	CHECK(mh_image_ilbm_decode(data, 81, &image, NULL) != MH_ERROR_NONE);

	seed = 35;

	for (i = 0; i < 2000; i++) {
		size = prepare_bitmap(data, i & 1);
		seed = (seed * MUTATION_MULTIPLIER + MUTATION_INCREMENT) &
			MH_U32_MAX;
		data[seed % size] ^= (unsigned char)(seed >> 24);
		memset(&image, 0xa5, sizeof(image));
		memcpy(&saved, &image, sizeof(saved));
		status = mh_image_ilbm_decode(data, size, &image, NULL);

		if (status == MH_ERROR_NONE) {
			free(image.rgba);
		} else {
			CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
		}
	}
}

static size_t
prepare_archive(unsigned char *data)
{
	size_t size;

	memset(data, 0, 266);
	memcpy(data, "PDI1", 4);
	write_le32(data + 4, 2);
	write_le32(data + 8, 1);
	write_le32(data + 12, 32);
	write_le32(data + 16, 34);
	write_le32(data + 20, 138);
	data[32] = 2; /* Only slot one is occupied. */
	size = prepare_bitmap(data + 138, 1);
	write_le32(data + 24, (unsigned long)size);
	write_le32(data + 86 + 16, (unsigned long)size);
	memcpy(data + 86 + 20, "COLOR.IFF", 10);

	return size + 138;
}

static void
check_archive(void)
{
	unsigned char data[266];
	struct mh_pdi archive;
	struct mh_pdi saved;
	struct mh_pdi_entry entry;
	size_t size;
	size_t truncated;
	unsigned long seed;
	enum mh_error_code status;
	int i;

	size = prepare_archive(data);
	CHECK(mh_pdi_parse(data, size, &archive, NULL) == MH_ERROR_NONE);
	CHECK(!mh_pdi_entry(&archive, 0, &entry));
	CHECK(!mh_pdi_entry(&archive, 2, &entry));
	CHECK(mh_pdi_find(&archive, "color.iff", &entry));
	CHECK(entry.data == data + 138 && entry.size == size - 138);
	CHECK(!mh_pdi_find(&archive, "color.iffx", &entry));

	for (truncated = 0; truncated < size; truncated++) {
		CHECK(mh_pdi_parse(data, truncated, &archive, NULL) !=
			MH_ERROR_NONE);
	}

	write_le32(data + 8, 2);
	CHECK(mh_pdi_parse(data, size, &archive, NULL) != MH_ERROR_NONE);
	size = prepare_archive(data);
	write_le32(data + 86 + 12, MH_U32_MAX);
	CHECK(mh_pdi_parse(data, size, &archive, NULL) != MH_ERROR_NONE);
	size = prepare_archive(data);
	memset(data + 86 + 20, 'X', 32);
	CHECK(mh_pdi_parse(data, size, &archive, NULL) != MH_ERROR_NONE);
	seed = 53;

	for (i = 0; i < 2000; i++) {
		size = prepare_archive(data);
		seed = (seed * MUTATION_MULTIPLIER + MUTATION_INCREMENT) &
			MH_U32_MAX;
		write_le32(data + seed % (size - 3), seed);
		memset(&archive, 0xa5, sizeof(archive));
		memcpy(&saved, &archive, sizeof(saved));
		status = mh_pdi_parse(data, size, &archive, NULL);

		if (status != MH_ERROR_NONE) {
			CHECK(memcmp(&archive, &saved, sizeof(archive)) == 0);
		}
	}
}

/* One textured quad with four distinct positions/UVs and a flat normal. */
static void
prepare_model(unsigned char *data)
{
	static const unsigned long counts[7] = {4, 1, 1, 1, 1, 4, 1};
	static const unsigned long lengths[7] = {48, 24, 16, 12, 12, 8, 10};
	static const unsigned long offsets[7] = {
		124, 172, 196, 212, 224, 236, 244};

	int i;

	memset(data, 0, 254);
	memcpy(data, "2OYM", 4);

	for (i = 0; i < 7; i++) {
		write_le32(data + 4 + i * 12, counts[i]);
		write_le32(data + 8 + i * 12, lengths[i]);
		write_le32(data + 12 + i * 12, offsets[i]);
	}

	write_le32(data + 100, 254);
	write_le32(data + 124 + 12, 0x3f800000UL);
	write_le32(data + 124 + 24, 0x3f800000UL);
	write_le32(data + 124 + 28, 0x3f800000UL);
	write_le32(data + 124 + 40, 0x3f800000UL);
	data[172] = 3;
	data[182] = 1;
	data[184] = 2;
	data[186] = 3;
	data[176] = 17;
	data[177] = 64;
	data[178] = 129;

	/* +Z visibility plane, independent of normals. */
	write_le32(data + 204, 0x3f800000UL);
	write_le32(data + 220, 0x3f800000UL); /* +Z normal. */
	data[238] = 2;
	data[240] = 2;
	data[241] = 1;
	data[243] = 1;
	memcpy(data + 244, "color.iff", 10);
}

static void
check_geometry(void)
{
	unsigned char data[254];
	unsigned char packed[266];
	struct mh_myo model;
	struct mh_geometry_face face;
	struct mh_geometry_face saved;
	struct mh_image_collection *archive = NULL;
	struct mh_mesh mesh;
	struct mh_mesh_part parts[2];
	struct mh_model decoded;
	struct mh_track track;
	size_t cursor;
	size_t size;
	unsigned long seed;
	enum mh_error_code status;
	int i;

	prepare_model(data);

	/* A flagged textured quad is not an environment face. */
	data[173] = 0x61;
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) == MH_ERROR_NONE);
	cursor = 0;
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) == MH_ERROR_NONE);
	CHECK(face.count == 4 && face.texture == 0 && cursor == 24);
	CHECK(face.corners[2].position[0] == 1.0 &&
		face.corners[2].position[1] == 1.0);
	CHECK(face.corners[2].normal[2] == 1.0);
	CHECK(face.plane[2] == 1.0 && face.plane[3] == 0);
	CHECK(face.corners[2].uv[0] == 2.5 && face.corners[2].uv[1] == 1.5);
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) == MH_ERROR_NONE &&
		face.count == 0);
	CHECK(strcmp(mh_myo_texture_name(&model, 0), "color.iff") == 0);
	CHECK(mh_myo_texture_name(&model, 1) == NULL);
	size = prepare_archive(packed);
	CHECK(mh_image_pdi_open(packed, size, &archive, NULL) == MH_ERROR_NONE);
	CHECK(build_model(&model, NULL, &mesh, 0) != MH_ERROR_NONE);
	status = build_model(&model, archive, &mesh, 1);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.face_count == 1 && mesh.faces[0].first == 0 &&
			mesh.faces[0].count == 6);
		CHECK(mesh.primitive_count == 1 &&
			mesh.primitives[0].first == 0 &&
			mesh.primitives[0].count == 6 &&
			mesh.primitives[0].draw == 0);
		CHECK(mesh.primitives[0].type == 3 &&
			mesh.primitives[0].flags == 0x61);
		CHECK(mesh.faces[0].color[0] == 17 &&
			mesh.faces[0].color[2] == 129);
		CHECK(mesh.faces[0].plane[2] == 1 &&
			mesh.vertices[0].color[0] == 1);
		mh_mesh_free(&mesh);
	}

	status = build_model(&model, archive, &mesh, 0);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.vertex_count == 6 && mesh.draw_count == 1);
		CHECK(mesh.texture_count == 2 && mesh.draws[0].texture == 1);
		CHECK(mesh.draws[0].environment == 0 &&
			mesh.environment_texture == 0);
		CHECK(mesh.vertices[0].position[0] ==
			mesh.vertices[3].position[0]);
		CHECK(mesh.vertices[2].position[0] ==
			mesh.vertices[4].position[0]);
		CHECK(mesh.vertices[2].uv[0] > 0.83f &&
			mesh.vertices[2].uv[0] < 0.84f);
		CHECK(strcmp(mesh.draws[0].texture_name, "color.iff") == 0 &&
			!mesh.draws[0].alpha_test);
		memset(&track, 0, sizeof(track));
		strcpy(track.zero_transparent, "COLOR.IFF");
		track.zero_transparent_size = 10;
		mh_mesh_track_materials(&mesh, &track);
		CHECK(mesh.draws[0].alpha_test);
		track.zero_transparent_size = 0;
		mh_mesh_track_materials(&mesh, &track);

		/* Applying a list never removes flag 2. */
		CHECK(mesh.draws[0].alpha_test);
		track.zero_transparent_size = 10;
		mesh.draws[0].alpha_test = 0;
		mesh.draws[0].environment = 1;
		mh_mesh_track_materials(&mesh, &track);
		CHECK(!mesh.draws[0].alpha_test);
		mh_mesh_free(&mesh);
	}

	CHECK(mh_model_myo_expand(&model, &decoded, NULL) == MH_ERROR_NONE);
	memset(parts, 0, sizeof(parts));
	parts[0].model = &decoded;
	parts[1].model = &decoded;
	parts[1].offset[0] = 4.0;
	status = mh_mesh_build_parts(
		mh_image_fallback_decoder(), parts, 2, archive, &mesh, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.vertex_count == 12 && mesh.draw_count == 2);
		CHECK(mesh.texture_count == 4 && mesh.draws[1].texture == 3);
		CHECK(mesh.draws[1].first == 6 && mesh.draws[1].count == 6);
		CHECK(mesh.primitive_count == 2 &&
			mesh.primitives[1].first == 6 &&
			mesh.primitives[1].draw == 1 &&
			mesh.primitives[1].count == 6);
		CHECK(fabs(mesh.vertices[0].position[0] + 5.0 / sqrt(26.0)) <
			1e-6);
		CHECK(fabs(mesh.vertices[6].position[0] - 3.0 / sqrt(26.0)) <
			1e-6);
		CHECK(mesh.vertices[6].normal[2] == 1.0f);
		CHECK(mesh.vertices[0].uv[0] == mesh.vertices[6].uv[0]);
		mh_mesh_free(&mesh);
	}

	CHECK(mh_mesh_build_parts(mh_image_fallback_decoder(), parts, 0,
		archive, &mesh, NULL) != MH_ERROR_NONE);
	parts[1].offset[0] = 1e31;
	CHECK(mh_mesh_build_parts(mh_image_fallback_decoder(), parts, 2,
		archive, &mesh, NULL) != MH_ERROR_NONE);

	mh_model_free(&decoded);

	/*
	 * Bad vertex, normal, plane, UV, indirection, name, and normal value.
	 */
	for (i = 0; i < 11; i++) {
		prepare_model(data);

		if (i == 0)
			data[180] = 4;

		if (i == 1)
			data[188] = 1;

		if (i == 2)
			data[174] = 1;

		if (i == 3)
			data[190] = 1;

		if (i == 4)
			data[192] = 1;

		if (i == 5)
			data[224] = 1;

		if (i == 6)
			write_le32(data + 220, 0x7f800000UL);

		if (i >= 7)
			write_le32(data + 196 + (i - 7) * 4, 0x7fc00001UL);

		CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) ==
			MH_ERROR_NONE);
		cursor = 0;
		CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) !=
				MH_ERROR_NONE &&
			cursor == 0);
	}

	/*
	 * Official Dominator models have zero-area faces with NaN directions.
	 */
	prepare_model(data);
	data[182] = 0;
	data[184] = 1;
	data[186] = 1;
	write_le32(data + 204, 0x7fc00000UL);
	write_le32(data + 220, 0xffc00000UL);
	CHECK(mh_myo_parse(data, sizeof(data), &model, NULL) == MH_ERROR_NONE);
	cursor = 0;
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) == MH_ERROR_NONE);
	CHECK(face.count == 4 && face.degenerate && cursor == 24);
	status = build_model(&model, archive, &mesh, 1);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.vertex_count == 0 && mesh.primitive_count == 0 &&
			mesh.face_count == 0 && mesh.draw_count == 0);
		mh_mesh_free(&mesh);
	}

	/*
	 * Invalid references and infinities remain errors even on empty faces.
	 */
	data[188] = 1;
	cursor = 0;
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) != MH_ERROR_NONE);
	CHECK(cursor == 0);
	data[188] = 0;
	write_le32(data + 220, 0x7f800000UL);
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) != MH_ERROR_NONE);
	CHECK(cursor == 0);

	/* Valid Dominator faces retain geometry and use their finite plane. */
	prepare_model(data);

	for (i = 0; i < 3; i++)
		write_le32(data + 212 + i * 4, 0xffc00000UL);

	cursor = 0;
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) == MH_ERROR_NONE);
	CHECK(!face.degenerate && face.count == 4);
	CHECK(face.corners[2].normal[0] == 0 &&
		face.corners[2].normal[1] == 0 &&
		face.corners[2].normal[2] == 1);
	status = build_model(&model, archive, &mesh, 1);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.vertex_count == 6 && mesh.face_count == 1);
		CHECK(mesh.vertices[0].normal[2] == 1.0f);
		mh_mesh_free(&mesh);
	}

	/* A partial NaN vector or a missing plane direction is not repaired. */
	write_le32(data + 212, 0);
	cursor = 0;
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) != MH_ERROR_NONE);
	CHECK(cursor == 0);
	write_le32(data + 212, 0xffc00000UL);
	write_le32(data + 204, 0);
	CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) != MH_ERROR_NONE);
	CHECK(cursor == 0);

	seed = 71;

	for (i = 0; i < 4000; i++) {
		prepare_model(data);
		seed = (seed * MUTATION_MULTIPLIER + MUTATION_INCREMENT) &
			MH_U32_MAX;
		write_le32(data + seed % 251, seed);

		if (mh_myo_parse(data, sizeof(data), &model, NULL) ==
			MH_ERROR_NONE) {
			memset(&face, 0xa5, sizeof(face));
			memcpy(&saved, &face, sizeof(saved));
			cursor = 0;
			status = mh_myo_next_face(&model, &cursor, &face, NULL);

			if (status != MH_ERROR_NONE) {
				CHECK(cursor == 0 &&
					memcmp(&face, &saved, sizeof(face)) ==
						0);
			}
		}
	}

	mh_image_collection_close(archive);
}

/* Untextured faces sharing one material, with independently chosen flags. */
static size_t
prepare_surfaces(unsigned char data[400], const unsigned char *types,
	const unsigned char *flags, size_t count)
{
	size_t cursor;
	size_t i;
	unsigned int j;

	memset(data, 0, 400);
	memcpy(data, "2OYM", 4);
	write_le32(data + 4, 4);
	write_le32(data + 8, 48);
	write_le32(data + 12, 124);
	write_le32(data + 136, 0x3f800000UL);
	write_le32(data + 148, 0x3f800000UL);
	write_le32(data + 152, 0x3f800000UL);
	write_le32(data + 164, 0x3f800000UL);
	write_le32(data + 16, (unsigned long)count);
	write_le32(data + 24, 172);
	cursor = 172;

	for (i = 0; i < count; i++) {
		data[cursor] = types[i];
		data[cursor + 1] = flags[i];
		data[cursor + 4] = 75;
		data[cursor + 5] = 91;
		data[cursor + 6] = 34;

		for (j = 0; j < 3U + (types[i] & 1U); j++) {
			data[cursor + 8 + j * 2] = (unsigned char)j;
		}

		cursor += types[i] >= 8 ? 20 + (types[i] & 1) * 4
					: 16 + (types[i] & 1) * 4;
	}

	write_le32(data + 20, (unsigned long)(cursor - 172));
	write_le32(data + 28, 1);
	write_le32(data + 32, 16);
	write_le32(data + 36, (unsigned long)cursor);
	cursor += 16;
	write_le32(data + 40, 1);
	write_le32(data + 44, 12);
	write_le32(data + 48, (unsigned long)cursor);
	write_le32(data + cursor + 8, 0x3f800000UL);
	cursor += 12;

	for (i = 4; i < 7; i++)
		write_le32(data + 12 + i * 12, (unsigned long)cursor);

	write_le32(data + 100, (unsigned long)cursor);

	return cursor;
}

static void
check_corner_colors(void)
{
	static const unsigned int lengths[4] = {24, 32, 32, 36};
	static const unsigned long offsets[5] = {196, 212, 224, 236, 244};
	static const unsigned char colors[12] = {
		255, 0, 0, 0, 255, 0, 0, 0, 255, 255, 255, 255};

	unsigned char data[266];
	unsigned char packed[266];
	struct mh_myo model;
	struct mh_geometry_face face;
	struct mh_geometry_face saved;
	struct mh_mesh mesh;
	struct mh_image_collection *archive = NULL;
	enum mh_error_code status;
	size_t size;
	size_t cursor;
	size_t extra;
	unsigned int type;
	unsigned int corners;
	unsigned int vertex_offset;
	unsigned int i;
	unsigned int bad;
	unsigned char original;

	size = prepare_archive(packed);
	CHECK(mh_image_pdi_open(packed, size, &archive, NULL) == MH_ERROR_NONE);

	for (type = 4; type <= 7; type++) {
		prepare_model(data);
		extra = lengths[type - 4] - 24;
		memmove(data + 196 + extra, data + 196, 58);
		write_le32(data + 20, lengths[type - 4]);
		write_le32(data + 100, (unsigned long)(254 + extra));

		for (i = 0; i < 5; i++)
			write_le32(data + 36 + i * 12, offsets[i] + extra);

		memset(data + 172, 0, lengths[type - 4]);
		data[172] = (unsigned char)type;
		corners = 3 + (type & 1);
		vertex_offset = 4 + corners * 4;

		for (i = 0; i < corners; i++) {
			memcpy(data + 176 + i * 4, colors + i * 3, 3);
			data[179 + i * 4] = 0xcd;
			data[172 + vertex_offset + i * 2] = (unsigned char)i;
		}

		CHECK(mh_myo_parse(data, 254 + extra, &model, NULL) ==
			MH_ERROR_NONE);
		cursor = 0;
		CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) ==
			MH_ERROR_NONE);
		CHECK(face.count == corners && face.type == type);
		CHECK(cursor == lengths[type - 4]);

		for (i = 0; i < corners; i++) {
			CHECK(memcmp(face.corners[i].color, colors + i * 3,
				3) == 0);
			CHECK(face.corners[i].normal[2] == 1.0);
		}

		CHECK(face.texture == (type >= 6 ? 0 : MH_GEOMETRY_NO_TEXTURE));

		if (type >= 6)
			CHECK(face.corners[2].uv[0] == 2.5);

		status = build_model(&model, archive, &mesh, 0);
		CHECK(status == MH_ERROR_NONE);

		if (status == MH_ERROR_NONE) {
			CHECK(mesh.vertex_count == (corners == 3 ? 3U : 6U));
			CHECK(mesh.draws[0].prelit == 1 &&
				mesh.draws[0].environment == 0);
			CHECK(mesh.draws[0].alpha_test == 0);
			CHECK(mesh.vertices[0].color[0] == 1.0f &&
				mesh.vertices[0].color[1] == 0.0f);
			CHECK(mesh.vertices[1].color[1] == 1.0f &&
				mesh.vertices[2].color[2] == 1.0f);
			mh_mesh_free(&mesh);
		}

		data[173] = 2;
		status = build_model(&model, archive, &mesh, 0);
		CHECK(status == MH_ERROR_NONE);

		if (status == MH_ERROR_NONE) {
			CHECK(mesh.draws[0].alpha_test == (type >= 6));
			mh_mesh_free(&mesh);
		}

		data[173] = 0;
		saved = face;

		for (i = 0; i < (type >= 6 ? 5U : 3U); i++) {
			bad = 172 + vertex_offset;

			if (i == 1)
				bad += corners * 2;

			if (i == 2)
				bad = 174;

			if (i == 3)
				bad = 172 + (type == 6 ? 26 : 30);

			if (i == 4)
				bad = 172 + (type == 6 ? 28 : 32);

			original = data[bad];
			data[bad] = 255;
			cursor = 0;
			CHECK(mh_myo_next_face(&model, &cursor, &face, NULL) !=
				MH_ERROR_NONE);
			CHECK(cursor == 0 &&
				memcmp(&face, &saved, sizeof(face)) == 0);
			data[bad] = original;
		}
	}

	mh_image_collection_close(archive);
}

static void
check_environment(void)
{
	static const unsigned char mixed_types[4] = {0, 1, 8, 9};
	static const unsigned char mixed_flags[4] = {0x60, 0x60, 0x60, 0x60};
	static const unsigned char quad_types[5] = {9, 9, 9, 9, 9};
	static const unsigned char quad_flags[5] = {0, 0x20, 0x40, 0, 0x60};
	unsigned char data[400];
	unsigned char pixels[128];
	struct mh_myo model;
	struct mh_mesh mesh;
	struct mh_image saved;
	enum mh_error_code status;
	size_t size;

	size = prepare_surfaces(data, mixed_types, mixed_flags, 4);
	CHECK(mh_myo_parse(data, size, &model, NULL) == MH_ERROR_NONE);
	status = build_model(&model, NULL, &mesh, 0);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.draw_count == 2 && mesh.vertex_count == 18);
		CHECK(mesh.draws[0].environment == 0 &&
			mesh.draws[0].count == 9);
		CHECK(mesh.draws[1].environment == 1 &&
			mesh.draws[1].count == 9);
		CHECK(fabs(mesh.vertices[9].color[0] - 75.0 / 255.0) < 1e-6);
		size = prepare_bitmap(pixels, 0);
		CHECK(mh_mesh_set_environment(mh_image_fallback_decoder(),
			&mesh, pixels, size, MH_TEXTURE_PALETTE_RGB,
			NULL) == MH_ERROR_NONE);
		CHECK(mesh.texture_count == 2 && mesh.environment_texture == 1);
		CHECK(mesh.textures[1].width == 3 &&
			mesh.textures[1].height == 2);
		CHECK(mh_mesh_texture_format(&mesh, 1) ==
			MH_TEXTURE_PALETTE_RGB);
		saved = mesh.textures[1];
		CHECK(mh_mesh_set_environment(mh_image_fallback_decoder(),
			&mesh, pixels, size - 1, MH_TEXTURE_PALETTE_RGB,
			NULL) != MH_ERROR_NONE);
		CHECK(mesh.texture_count == 2 && mesh.environment_texture == 1);
		CHECK(mesh.textures[1].rgba == saved.rgba);
		pixels[48] = 42;
		CHECK(mh_mesh_set_environment(mh_image_fallback_decoder(),
			&mesh, pixels, size, MH_TEXTURE_PALETTE_RGB,
			NULL) == MH_ERROR_NONE);
		CHECK(mesh.texture_count == 2 && mesh.environment_texture == 1);
		CHECK(mesh.textures[1].rgba[0] == 42 &&
			mesh.textures[0].rgba[0] == 255);
		CHECK(mh_mesh_set_environment(mh_image_fallback_decoder(),
			&mesh, pixels, size, MH_TEXTURE_RGB555,
			NULL) == MH_ERROR_NONE);
		CHECK(mesh.texture_count == 2 && mesh.environment_texture == 1);
		CHECK(mh_mesh_texture_format(&mesh, 0) == 0 &&
			mh_mesh_texture_format(&mesh, 1) == MH_TEXTURE_RGB555);
		CHECK(mesh.textures[1].rgba[0] == 41);
		mh_mesh_free(&mesh);
	}

	size = prepare_surfaces(data, quad_types, quad_flags, 5);
	CHECK(mh_myo_parse(data, size, &model, NULL) == MH_ERROR_NONE);
	status = build_model(&model, NULL, &mesh, 0);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(mesh.draw_count == 4 && mesh.vertex_count == 30);
		CHECK(mesh.draws[0].environment == 0 &&
			mesh.draws[0].count == 6);
		CHECK(mesh.draws[1].environment == 1 &&
			mesh.draws[1].count == 12);
		CHECK(mesh.draws[2].environment == 0 &&
			mesh.draws[2].first == 18);
		CHECK(mesh.draws[3].environment == 1 &&
			mesh.draws[3].first == 24);
		mh_mesh_free(&mesh);
	}
}

int
main(void)
{

	check_bitmap();
	check_palette_copy();
	check_archive();
	check_geometry();
	check_corner_colors();
	check_environment();

	if (failures != 0) {
		fprintf(stderr, "%d asset checks failed\n", failures);
		return 1;
	}

	puts("Geometry, PDI, ILBM, mesh conversion and malformed-input checks passed");

	return 0;
}
