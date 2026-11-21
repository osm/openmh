#include <stdio.h>
#include <string.h>

#include "engine/assets/myo.h"
#include "engine/core/binary.h"
#include "engine/geometry/model.h"
#include "engine/render/mesh.h"

static int failures;

#define CHECK(expression)                                                      \
	do {                                                                   \
		if (!(expression)) {                                           \
			fprintf(stderr, "line %d: %s\n", __LINE__,             \
				#expression);                                  \
			failures++;                                            \
		}                                                              \
	} while (0)

enum {
	HEADER_BYTES = 124,
	VERTICES = 124,
	PRIMITIVES = 172,
	PLANES = 424,
	NORMALS = 440,
	TEXTURES = 452,
	UV = 464,
	NAMES = 472,
	HALOS = 492,
	FIXTURE_SIZE = 732
};

/* All ten material modes, two names and two lamps; no external assets. */
static void
prepare_model(unsigned char data[FIXTURE_SIZE])
{
	static const unsigned long counts[7] = {4, 10, 1, 1, 1, 4, 2};
	static const unsigned long sizes[7] = {48, 252, 16, 12, 12, 8, 20};
	static const unsigned long offsets[7] = {
		VERTICES, PRIMITIVES, PLANES, NORMALS, TEXTURES, UV, NAMES};

	static const unsigned int lengths[10] = {
		16, 20, 24, 24, 24, 32, 32, 36, 20, 24};

	unsigned char *face, *halo;
	unsigned int i, corner, count, vertices, channel;

	memset(data, 0, FIXTURE_SIZE);
	memcpy(data, "2OYM", 4);

	for (i = 0; i < 7; i++) {
		mh_write_u32le(data + 4 + i * 12, counts[i]);
		mh_write_u32le(data + 8 + i * 12, sizes[i]);
		mh_write_u32le(data + 12 + i * 12, offsets[i]);
	}

	mh_write_u32le(data + 92, 2);
	mh_write_u32le(data + 100, HALOS);
	mh_write_u32le(data + 104, 0x40800000UL); /* Radius 4. */
	mh_write_u32le(data + VERTICES + 12, 0x3f800000UL);
	mh_write_u32le(data + VERTICES + 24, 0x3f800000UL);
	mh_write_u32le(data + VERTICES + 28, 0x3f800000UL);
	mh_write_u32le(data + VERTICES + 40, 0x3f800000UL);
	mh_write_u32le(data + PLANES + 8, 0x3f800000UL);
	mh_write_u32le(data + NORMALS + 8, 0x3f800000UL);
	mh_write_u32le(data + TEXTURES, 1);
	memcpy(data + NAMES, "paint.iff\0glass.iff", 20);

	for (i = 0; i < 8; i++)
		data[UV + i] = (unsigned char)(10 + i);

	face = data + PRIMITIVES;

	for (i = 0; i < 10; i++) {
		face[0] = (unsigned char)i;
		face[1] = (unsigned char)(i >= 8 ? 0x60 : 2);
		count = 3 + (i & 1);
		vertices = i >= 4 && i <= 7 ? 4 + count * 4 : 8;

		for (corner = 0; corner < count; corner++) {
			face[vertices + corner * 2] = (unsigned char)corner;

			for (channel = 0; channel < 3; channel++)
				face[4 + (vertices == 8 ? 0 : corner * 4) +
					channel] = (unsigned char)(i + channel);
		}

		face += lengths[i];
	}

	for (i = 0; i < 2; i++) {
		halo = data + HALOS + i * 120;
		mh_write_u32le(halo, 2);
		mh_write_u32le(halo + 4, 7 + i);
		mh_write_u32le(halo + 8, 0xbf800000UL);
		mh_write_u32le(halo + 28, 0x3f800000UL);
		halo[48] = (unsigned char)(33 + i);
		halo[49] = 99;
		halo[50] = 255;
	}
}

static void
check_values(const struct mh_model *model)
{
	const struct mh_geometry_face *face;
	unsigned int i, corner, channel;
	int textured;

	CHECK(model->vertex_count == 4 && model->face_count == 10);
	CHECK(model->texture_count == 2 && model->halo_count == 2);
	CHECK(model->radius == 4 && model->minimum[0] == 0 &&
		model->maximum[0] == 1 && model->maximum[1] == 1 &&
		model->maximum[2] == 0);
	CHECK(strcmp(model->textures[0], "paint.iff") == 0);
	CHECK(strcmp(model->textures[1], "glass.iff") == 0);

	for (i = 0; i < 10; i++) {
		face = &model->faces[i];
		textured = i == 2 || i == 3 || i == 6 || i == 7;
		CHECK(face->type == i && face->count == 3 + (i & 1));
		CHECK(!face->degenerate && face->plane[2] == 1);
		CHECK(face->flags == (unsigned int)(i >= 8 ? 0x60 : 2));
		CHECK(face->texture ==
			(textured ? 1UL : MH_GEOMETRY_NO_TEXTURE));
		CHECK(face->corners[1].position[0] == 1 &&
			face->corners[2].position[1] == 1);

		for (corner = 0; corner < face->count; corner++) {
			CHECK(face->corners[corner].normal[2] == 1);

			for (channel = 0; channel < 3; channel++)
				CHECK(face->corners[corner].color[channel] ==
					i + channel);

			if (textured) {
				CHECK(face->corners[corner].uv[0] ==
					10.5 + corner * 2);
				CHECK(face->corners[corner].uv[1] ==
					11.5 + corner * 2);
			}
		}
	}

	for (i = 0; i < 2; i++) {
		CHECK(model->halos[i].resource == 7 + i);
		CHECK(model->halos[i].endpoints[0][0] == -1 &&
			model->halos[i].endpoints[1][2] == 1);
		CHECK(model->halos[i].color[0] == 33 + i &&
			model->halos[i].color[1] == 99 &&
			model->halos[i].color[2] == 255);
	}
}

static void
check_owned_model(void)
{
	unsigned char data[FIXTURE_SIZE], original[FIXTURE_SIZE];
	struct mh_model model, saved, empty;
	struct mh_error error;
	enum mh_error_code status;
	size_t size;

	prepare_model(data);
	memcpy(original, data, sizeof(data));
	memset(&empty, 0, sizeof(empty));
	model = empty;
	status = mh_model_myo_decode(data, sizeof(data), &model, &error);
	CHECK(status == MH_ERROR_NONE && error.message == NULL);

	if (status != MH_ERROR_NONE)
		return;

	CHECK(memcmp(data, original, sizeof(data)) == 0);
	check_values(&model);
	saved = model;

	for (size = 0; size < sizeof(data); size++) {
		CHECK(mh_model_myo_decode(data, size, &model, NULL) !=
			MH_ERROR_NONE);
		CHECK(memcmp(&model, &saved, sizeof(model)) == 0);
	}

	/*
	 * Last face has an invalid normal: earlier decoded faces must be freed.
	 */
	data[PLANES - 8] = 99;
	CHECK(mh_model_myo_decode(data, sizeof(data), &model, &error) ==
		MH_ERROR_INVALID);
	CHECK(memcmp(&model, &saved, sizeof(model)) == 0);
	CHECK(error.message != NULL);
	CHECK(mh_model_myo_decode(NULL, 0, &model, NULL) != MH_ERROR_NONE);
	CHECK(mh_model_myo_decode(original, sizeof(original), NULL, NULL) !=
		MH_ERROR_NONE);

	/*
	 * Owned geometry and strings survive destruction of both input copies.
	 */
	memset(data, 0xa5, sizeof(data));
	memset(original, 0xa5, sizeof(original));
	check_values(&model);
	mh_model_free(&model);
	CHECK(memcmp(&model, &empty, sizeof(model)) == 0);
	mh_model_free(&model);
	mh_model_free(NULL);
}

static void
check_empty_model(void)
{
	unsigned char data[HEADER_BYTES];
	struct mh_model model;
	unsigned int i;
	enum mh_error_code status;

	memset(data, 0, sizeof(data));
	memcpy(data, "!OYM", 4);

	for (i = 0; i < 7; i++)
		mh_write_u32le(data + 12 + i * 12, HEADER_BYTES);

	status = mh_model_myo_decode(data, sizeof(data), &model, NULL);
	CHECK(status == MH_ERROR_NONE);

	if (status == MH_ERROR_NONE) {
		CHECK(model.face_count == 0 && model.faces == NULL);
		CHECK(model.texture_count == 0 && model.textures == NULL);
		CHECK(model.halo_count == 0 && model.halos == NULL);
		mh_model_free(&model);
	}
}

/*
 * Directly authored data exercises consumers without an original-file reader.
 */
static void
check_authored_mesh(void)
{
	static const double positions[4][3] = {
		{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};

	struct mh_geometry_face faces[2], saved[2];
	struct mh_geometry_halo halo;
	struct mh_model model;
	struct mh_mesh mesh;
	enum mh_error_code status;
	unsigned int profile, face, corner;

	for (profile = 0; profile < MH_RENDER_PROFILE_COUNT; profile++) {
		memset(&model, 0, sizeof(model));
		memset(faces, 0, sizeof(faces));
		memset(&halo, 0, sizeof(halo));
		model.faces = faces;
		model.face_count = 2;
		model.vertex_count = 4;
		model.maximum[0] = model.maximum[1] = 1;
		model.radius = 4;
		model.halos = &halo;
		model.halo_count = 1;
		halo.resource = 7;
		halo.endpoints[1][2] = 2;
		halo.color[0] = 255;

		for (face = 0; face < 2; face++) {
			faces[face].count = 3 + face;
			faces[face].type = face == 0 ? 4 : 9;
			faces[face].flags = face == 0 ? 0 : 0x60;
			faces[face].texture = MH_GEOMETRY_NO_TEXTURE;
			faces[face].plane[2] = 1;
			faces[face].color[0] = 64;

			for (corner = 0; corner < faces[face].count; corner++) {
				memcpy(faces[face].corners[corner].position,
					positions[corner],
					sizeof(positions[corner]));
				faces[face].corners[corner].normal[2] = 1;
				faces[face].corners[corner].color[0] =
					(unsigned char)(64 + corner * 32);
			}
		}

		memcpy(saved, faces, sizeof(saved));
		status = mh_mesh_build_native_profile(NULL, &model, NULL,
			(enum mh_render_profile)profile, &mesh, NULL);
		CHECK(status == MH_ERROR_NONE);

		if (status != MH_ERROR_NONE)
			continue;

		CHECK(memcmp(faces, saved, sizeof(faces)) == 0);
		memset(faces, 0, sizeof(faces));
		memset(&halo, 0, sizeof(halo));
		CHECK(mesh.vertex_count == 9 && mesh.primitive_count == 2);
		CHECK(mesh.face_count == 2 && mesh.draw_count == 2);
		CHECK(mesh.radius == 4 && mesh.halo_count == 1);
		CHECK(mesh.halos[0].resource == 7 &&
			mesh.halos[0].endpoints[1][2] == 2);
		CHECK(mesh.primitives[0].type ==
			(profile == MH_RENDER_SOFTWARE ? 0 : 4));
		CHECK(mesh.primitives[1].type == 9 &&
			mesh.draws[1].environment);
		CHECK(mesh.vertices[2].position[0] == 1 &&
			mesh.vertices[2].position[1] == 1);
		CHECK(mesh.vertices[2].color[0] ==
			(float)((profile == MH_RENDER_SOFTWARE ? 64 : 128) /
				255.0));
		mh_mesh_free(&mesh);
	}
}

int
main(void)
{
	check_owned_model();
	check_empty_model();
	check_authored_mesh();

	return failures != 0;
}
