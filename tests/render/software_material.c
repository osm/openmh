#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/myo.h"
#include "engine/core/binary.h"
#include "engine/geometry/model.h"
#include "engine/render/mesh_internal.h"

/*
 * Synthetic MYO container, fixed input face record and type/flags/RGB output.
 */
#define MODEL_BYTES 320
#define FACE_RECORD_BYTES 40
#define RESULT_BYTES 5

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static const unsigned int face_record_sizes[10] = {
	16, 20, 24, 24, 24, 32, 32, 36, 20, 24};

static void
build_model_fixture(unsigned char data[MODEL_BYTES],
	const unsigned char record[FACE_RECORD_BYTES])
{
	static const unsigned int counts[7] = {4, 1, 1, 1, 1, 4, 1};
	static const unsigned int lengths[7] = {48, 0, 16, 12, 12, 8, 12};
	static const unsigned int offsets[7] = {
		192, 128, 240, 256, 268, 280, 288};

	static const unsigned char uv[8] = {0, 0, 16, 0, 16, 8, 0, 8};
	unsigned int i;

	memset(data, 0, MODEL_BYTES);
	memcpy(data, "2OYM", 4);

	for (i = 0; i < 7; i++) {
		mh_write_u32le(data + 4 + i * 12, counts[i]);
		mh_write_u32le(data + 8 + i * 12,
			i == 1 ? face_record_sizes[record[0]] : lengths[i]);
		mh_write_u32le(data + 12 + i * 12, offsets[i]);
	}

	mh_write_u32le(data + 100, MODEL_BYTES);
	mh_write_u32le(data + 104, 0x3f800000UL);
	memcpy(data + 128, record, face_record_sizes[record[0]]);
	mh_write_u32le(data + 204, 0x3f800000UL);
	mh_write_u32le(data + 220, 0x3f800000UL);
	mh_write_u32le(data + 236, 0x3f800000UL);
	mh_write_u32le(data + 248, 0x3f800000UL);
	mh_write_u32le(data + 264, 0x3f800000UL);
	memcpy(data + 280, uv, 8);
	memcpy(data + 288, "texture.iff", 12);
}

static int
check_material(const unsigned char record[FACE_RECORD_BYTES], int emit)
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	const double brightness[3] = {1, 1, 1};
	struct mh_model model;
	struct mh_geometry_face source;
	struct mh_mesh mesh;
	struct mh_vertex vertices[6];
	struct mh_mesh_face face;
	struct mh_mesh_primitive primitive;
	struct mh_draw draw;
	struct mh_image images[2];
	unsigned char data[MODEL_BYTES], saved[MODEL_BYTES], colors[18],
		output[RESULT_BYTES];
	unsigned int profile, i, j, expected_type;

	CHECK(record[0] < 10);
	build_model_fixture(data, record);
	memcpy(saved, data, sizeof(saved));
	CHECK(mh_model_myo_decode(data, sizeof(data), &model, NULL) ==
		MH_ERROR_NONE);
	source = model.faces[0];

	for (profile = 0; profile < 2; profile++) {
		memset(&mesh, 0, sizeof(mesh));
		memset(images, 0, sizeof(images));
		mesh.vertices = vertices;
		mesh.faces = &face;
		mesh.primitives = &primitive;
		mesh.draws = &draw;
		mesh.textures = images;
		mesh.texture_count = 2;
		mesh.world_colors = colors;
		images[0].width = images[0].height = 1;
		images[1].width = 17;
		images[1].height = 9;
		CHECK(mh_mesh_append_model(&model, brightness,
			profile ? MH_RENDER_SOFTWARE : MH_RENDER_GLIDE,
			&mesh, NULL) == MH_ERROR_NONE);
		expected_type = source.type;

		if (profile && source.type >= 4 && source.type <= 7)
			expected_type -= 4;

		CHECK(mesh.primitive_count == 1 && mesh.face_count == 1 &&
			primitive.type == expected_type &&
			face.type == expected_type &&
			primitive.flags == source.flags &&
			face.flags == source.flags);

		for (i = 0; i < mesh.vertex_count; i++) {
			for (j = 0; j < 3; j++) {
				CHECK(vertices[i].position[j] ==
					(float)source.corners[corners[i]]
						.position[j]);
				CHECK(vertices[i].normal[j] ==
					(float)source.corners[corners[i]]
						.normal[j]);

				if (profile && source.type >= 4 &&
					source.type <= 7)
					CHECK(colors[i * 3 + j] ==
						face.color[j]);
				else
					CHECK(colors[i * 3 + j] ==
						source.corners[corners[i]]
							.color[j]);
			}

			CHECK(vertices[i].uv[0] ==
				(float)(source.corners[corners[i]].uv[0] /
					images[draw.texture].width));
			CHECK(vertices[i].uv[1] ==
				(float)(source.corners[corners[i]].uv[1] /
					images[draw.texture].height));
		}

		for (i = 0; i < 4; i++)
			CHECK(face.plane[i] == (float)source.plane[i]);

		CHECK(memcmp(saved, data, sizeof(saved)) == 0);
	}

	output[0] = primitive.type;
	output[1] = primitive.flags;
	memcpy(output + 2, face.color, 3);

	if (emit)
		CHECK(fwrite(output, 1, sizeof(output), stdout) ==
			sizeof(output));

	mh_model_free(&model);

	return 0;
}

static int
guards(void)
{
	struct mh_mesh mesh, saved;
	unsigned char record[FACE_RECORD_BYTES];
	unsigned int type, i, vertex_offset, count;

	memset(&mesh, 0xa5, sizeof(mesh));
	saved = mesh;
	CHECK(mh_mesh_build_native_profile(mh_image_fallback_decoder(), NULL,
		NULL, (enum mh_render_profile)99, &mesh,
		NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&mesh, &saved, sizeof(mesh)) == 0);
	CHECK(mh_mesh_build_world_native_profile(mh_image_fallback_decoder(),
		NULL, NULL, NULL, NULL, 0, (enum mh_render_profile)99,
		&mesh, NULL) == MH_ERROR_INVALID);
	CHECK(memcmp(&mesh, &saved, sizeof(mesh)) == 0);

	for (type = 0; type < 10; type++) {
		memset(record, 0, sizeof(record));
		record[0] = (unsigned char)type;
		count = 3 + (type & 1);
		vertex_offset = type >= 4 && type <= 7 ? 4 + count * 4 : 8;

		for (i = 0; i < count; i++) {
			record[vertex_offset + i * 2] = (unsigned char)i;
			record[4 + (type >= 4 && type <= 7 ? i * 4 : 0)] = 255;
		}

		CHECK(check_material(record, 0) == 0);
	}

	return 0;
}

int
main(void)
{
	return guards();
}
