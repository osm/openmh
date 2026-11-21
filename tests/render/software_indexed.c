#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "backends/image/provider.h"
#include "backends/render/software_palette.h"
#include "backends/render/software_provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/ilbm.h"
#include "engine/assets/pdi.h"
#include "engine/assets/tga.h"
#include "engine/core/binary.h"
#include "interfaces/render/backend.h"

/* Synthetic PDI capacity, embedded ILBM start and captured TGA payload. */
#define ARCHIVE_CAPACITY 160
#define ILBM_OFFSET 88
#define PIXEL_OFFSET 18

#define CHECK(test)                                                            \
	do {                                                                   \
		if (!(test)) {                                                 \
			fprintf(stderr, "line %d: %s\n", __LINE__, #test);     \
			return 1;                                              \
		}                                                              \
	} while (0)

static const float identity[16] = {
	1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

static size_t
build_archive(unsigned char data[ARCHIVE_CAPACITY], unsigned int mask,
	unsigned int compressed)
{
	unsigned char *ilbm;
	unsigned int row, cursor, body, size;

	memset(data, 0, ARCHIVE_CAPACITY);
	memcpy(data, "PDI1", 4);
	mh_write_u32le(data + 4, 1);
	mh_write_u32le(data + 8, 1);
	mh_write_u32le(data + 12, 32);
	mh_write_u32le(data + 16, 36);
	mh_write_u32le(data + 20, ILBM_OFFSET);
	data[32] = 1;
	strcpy((char *)data + 56, "texture.iff");
	ilbm = data + ILBM_OFFSET;
	memcpy(ilbm, "FORM", 4);
	memcpy(ilbm + 8, "ILBMBMHD", 8);
	ilbm[19] = 20;
	ilbm[21] = 2;
	ilbm[23] = 1;
	ilbm[28] = 1;
	ilbm[29] = (unsigned char)mask;
	ilbm[30] = (unsigned char)compressed;
	memcpy(ilbm + 40, "CMAP", 4);
	ilbm[47] = 6;
	ilbm[48] = ilbm[51] = 255;
	memcpy(ilbm + 54, "BODY", 4);
	cursor = 62;

	for (row = 0; row < (mask == 1 ? 2U : 1U); row++) {
		if (compressed)
			ilbm[cursor++] = 1;

		ilbm[cursor++] = (unsigned char)(row == 0 ? 0x40 : 0x80);
		ilbm[cursor++] = 0;
	}

	body = cursor - 62;
	ilbm[61] = (unsigned char)body;
	size = cursor + (body & 1);
	ilbm[7] = (unsigned char)(size - 8);
	mh_write_u32le(data + 24, size);
	mh_write_u32le(data + 52, size);

	return ILBM_OFFSET + size;
}

static int
decode_guards(void)
{
	struct mh_indexed_image image, saved;
	struct mh_pdi pdi;
	struct mh_pdi_entry entry;
	unsigned char data[ARCHIVE_CAPACITY];
	unsigned int mask, compression;
	size_t size;

	for (mask = 0; mask < 4; mask++) {
		for (compression = 0; compression < 2; compression++) {
			size = build_archive(data, mask, compression);
			CHECK(mh_pdi_parse(data, size, &pdi, NULL) ==
				MH_ERROR_NONE);
			CHECK(mh_pdi_find(&pdi, "TEXTURE.IFF", &entry));
			CHECK(mh_image_ilbm_decode_indexed(entry.data,
				entry.size, &image,
				NULL) == MH_ERROR_NONE);
			CHECK(image.width == 2 && image.height == 1 &&
				image.size == 2);
			CHECK(image.colors == 2 && image.indices[0] == 0 &&
				image.indices[1] == 1);
			CHECK(memcmp(image.palette, image.palette + 3, 3) == 0);
			saved = image;
			CHECK(mh_image_ilbm_decode_indexed(entry.data,
				entry.size - 1, &image,
				NULL) != MH_ERROR_NONE);
			CHECK(memcmp(&image, &saved, sizeof(image)) == 0);
			free(image.indices);
		}
	}

	return 0;
}

static void
init_vertex(struct mh_vertex *vertex, float x, float y, float u, float v)
{
	unsigned int i;

	memset(vertex, 0, sizeof(*vertex));
	vertex->position[0] = x / 160 - 1;
	vertex->position[1] = 1 - y / 100;
	vertex->position[2] = 0.5f;
	vertex->uv[0] = u;
	vertex->uv[1] = v;

	for (i = 0; i < 3; i++)
		vertex->color[i] = 128.0f / 255.0f;
}

/* Borrowed geometry for the fixture; only texture tables are heap owned. */
static void
release_mesh(struct mh_mesh *mesh)
{
	mesh->vertices = NULL;
	mesh->draws = NULL;
	mesh->primitives = NULL;
	mh_mesh_free(mesh);
}

static int
render_image(const struct mh_image_collection *collection,
	const struct mh_pdi_entry *entry, const char *capture, int guards,
	int cutout)
{
	struct mh_mesh mesh;
	struct mh_vertex vertices[6];
	struct mh_draw draws[2];
	struct mh_mesh_primitive primitives[2];
	const struct mh_mesh *meshes[1];
	struct mh_renderer *renderer;
	struct mh_file first, next;
	struct mh_indexed_image *retained;
	unsigned int width, height, i;
	FILE *file;
	int written;

	memset(&mesh, 0, sizeof(mesh));
	memset(draws, 0, sizeof(draws));
	memset(primitives, 0, sizeof(primitives));
	mesh.textures = (struct mh_image *)calloc(1, sizeof(*mesh.textures));
	CHECK(mesh.textures != NULL);
	mesh.texture_count = 1;
	CHECK(mh_image_ilbm_decode_texture(entry->data, entry->size,
		mesh.textures, NULL) == MH_ERROR_NONE);
	width = mesh.textures[0].width;
	height = mesh.textures[0].height;
	init_vertex(&vertices[0], 20, 20, 0.5f / width, 0.5f / height);
	init_vertex(&vertices[1], 300, 20, 1 - 0.5f / width, 0.5f / height);
	init_vertex(&vertices[2], 20, 180, 0.5f / width, 1 - 0.5f / height);
	init_vertex(&vertices[3], 300, 20, 1 - 0.5f / width, 0.5f / height);
	init_vertex(
		&vertices[4], 300, 180, 1 - 0.5f / width, 1 - 0.5f / height);
	init_vertex(&vertices[5], 20, 180, 0.5f / width, 1 - 0.5f / height);

	for (i = 0; i < 2; i++) {
		draws[i].first = primitives[i].first = i * 3;
		draws[i].count = primitives[i].count = 3;
		draws[i].prelit = 1;
		draws[i].alpha_test = cutout;
		CHECK(strlen(entry->name) < sizeof(draws[i].texture_name));
		strcpy(draws[i].texture_name, entry->name);
		primitives[i].draw = i;
		primitives[i].type = 2;
	}

	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = draws;
	mesh.draw_count = 2;
	mesh.primitives = primitives;
	mesh.primitive_count = 2;
	meshes[0] = &mesh;
	CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(), &mesh,
		collection, NULL) == MH_ERROR_NONE);
	CHECK(mesh.indexed_texture_count == 1);
	retained = mesh.indexed_textures;
	strcpy(draws[1].texture_name, "missing.iff");
	CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(), &mesh,
		collection, NULL) != MH_ERROR_NONE);
	CHECK(mesh.indexed_textures == retained);
	strcpy(draws[1].texture_name, entry->name);
	mesh.textures[0].width++;
	CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(), &mesh,
		collection, NULL) != MH_ERROR_NONE);
	CHECK(mesh.indexed_textures == retained);
	mesh.textures[0].width--;
	strcpy(draws[0].texture_name, "missing.iff");
	CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(), &mesh,
		collection, NULL) != MH_ERROR_NONE);
	CHECK(mesh.indexed_textures == retained);
	strcpy(draws[0].texture_name, entry->name);
	CHECK(mh_mesh_load_indexed_textures(mh_image_fallback_decoder(), &mesh,
		collection, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &first));
	CHECK(first.size == PIXEL_OFFSET + 320UL * 200 * 2);

	if (capture != NULL) {
		file = fopen(capture, "wb");
		CHECK(file != NULL);
		written = fwrite(first.data, 1, first.size, file) == first.size;
		CHECK(fclose(file) == 0 && written);
	}

	if (guards) {
		/*
		 * Index zero is opaque here even though the RGBA texture masks
		 * it.
		 */
		CHECK(mesh.textures[0].rgba[3] == 0);
		CHECK(mh_read_u16le(first.data + PIXEL_OFFSET +
			((199 - 60) * 320 + 80) * 2) == 0x7c00);

		/*
		 * Different face light must not leak through the shared
		 * palette.
		 */
		for (i = 3; i < 6; i++)
			memset(vertices[i].color, 0, sizeof(vertices[i].color));

		CHECK(mh_renderer_update_vertices(renderer));
		CHECK(mh_renderer_frame(renderer, identity, identity, 0));
		free(first.data);
		CHECK(mh_renderer_tga_capture(renderer, &first));
		CHECK(mh_read_u16le(first.data + PIXEL_OFFSET +
			((199 - 60) * 320 + 80) * 2) == 0x7c00);
		CHECK(mh_read_u16le(first.data + PIXEL_OFFSET +
			((199 - 150) * 320 + 250) * 2) == 0);
		vertices[5].uv[0] = -1;
		CHECK(mh_renderer_update_vertices(renderer));
		CHECK(!mh_renderer_frame(renderer, identity, identity, 0));
		CHECK(mh_renderer_tga_capture(renderer, &next));
		CHECK(next.size == first.size &&
			memcmp(first.data, next.data, first.size) == 0);
		free(next.data);
	}

	free(first.data);
	mh_renderer_destroy(renderer);
	release_mesh(&mesh);

	return 0;
}

/* Identical RGB at indices zero/one must still give different coverage. */
static int
check_cutout_scene(const struct mh_pdi *pdi)
{
	struct mh_mesh mesh;
	struct mh_vertex vertices[6];
	struct mh_draw draws[2];
	struct mh_mesh_primitive primitives[2];
	struct mh_indexed_image image;
	struct mh_pdi_entry entry;
	const struct mh_mesh *meshes[1];
	struct mh_renderer *renderer, *retained;
	struct mh_file picture, next;
	unsigned int i, mode;

	CHECK(mh_pdi_find(pdi, "texture.iff", &entry));
	CHECK(mh_image_ilbm_decode_indexed(
		entry.data, entry.size, &image, NULL) == MH_ERROR_NONE);
	memset(&mesh, 0, sizeof(mesh));
	memset(draws, 0, sizeof(draws));
	memset(primitives, 0, sizeof(primitives));
	init_vertex(&vertices[0], 20, 20, 0.25f, 0.5f);
	init_vertex(&vertices[1], 300, 20, 0.75f, 0.5f);
	init_vertex(&vertices[2], 20, 180, 0.25f, 0.5f);
	memcpy(vertices + 3, vertices, 3 * sizeof(*vertices));

	for (i = 0; i < 3; i++) {
		memset(vertices[i].color, 0, sizeof(vertices[i].color));
		vertices[i].color[1] = 1;
	}

	for (i = 0; i < 2; i++) {
		draws[i].first = primitives[i].first = i * 3;
		draws[i].count = primitives[i].count = 3;
		draws[i].prelit = 1;
		primitives[i].draw = i;
	}

	primitives[1].type = 2;
	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = draws;
	mesh.draw_count = 2;
	mesh.primitives = primitives;
	mesh.primitive_count = 2;
	mesh.indexed_textures = &image;
	mesh.indexed_texture_count = 1;
	meshes[0] = &mesh;

	/* Opaque, MYO flag, track material, both, then black nonzero texels. */
	for (mode = 0; mode < 5; mode++) {
		primitives[1].flags = mode == 1 || mode == 3 ? 2 : 0;
		draws[1].alpha_test = mode >= 2;

		if (mode == 4)
			for (i = 3; i < 6; i++)
				memset(vertices[i].color, 0,
					sizeof(vertices[i].color));

		CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1,
			320, 200, 0, &renderer, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_frame(renderer, identity, identity, 0));
		CHECK(mh_renderer_tga_capture(renderer, &picture));
		CHECK(mh_read_u16le(picture.data + PIXEL_OFFSET +
			((199 - 60) * 320 + 80) * 2) ==
			(mode == 0 ? 0x7c00 : 0x03e0));
		CHECK(mh_read_u16le(picture.data + PIXEL_OFFSET +
			((199 - 30) * 320 + 250) * 2) ==
			(mode == 4 ? 0 : 0x7c00));
		vertices[5].uv[0] = -1;
		CHECK(mh_renderer_update_vertices(renderer));
		CHECK(!mh_renderer_frame(renderer, identity, identity, 0));
		CHECK(mh_renderer_tga_capture(renderer, &next));
		CHECK(next.size == picture.size &&
			memcmp(next.data, picture.data, picture.size) == 0);
		free(next.data);
		free(picture.data);
		vertices[5].uv[0] = 0.25f;
		retained = renderer;
		primitives[1].flags = 16; /* Unsupported lighting flag. */
		CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1,
			320, 200, 0, &retained,
			NULL) == MH_ERROR_UNSUPPORTED);
		CHECK(retained == renderer);
		mh_renderer_destroy(renderer);
	}

	free(image.indices);

	return 0;
}

/*
 * Compare scene preparation against explicit screen corners and camera depths.
 */
static int
check_perspective_scene(void)
{
	static const float xy[3][2] = {{40, 40}, {280, 40}, {40, 160}};
	static const float depths[3] = {4, 8, 16};
	struct mh_mesh mesh;
	struct mh_vertex vertices[3];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	struct mh_indexed_image image;
	struct mh_software_triangle triangle;
	struct mh_software_texture texture;
	struct mh_software_palette palette;
	struct mh_software_surface surface;
	struct mh_software_texture_state texture_state;
	const struct mh_mesh *meshes[1];
	struct mh_renderer *renderer, *other;
	struct mh_file picture, saved, next, expected;
	struct mh_picture reference;
	struct mh_error error;
	unsigned char indices[17 * 9], pixels[320 * 200 * 2];
	float matrix[16], model[16];
	unsigned int i, mode, flags, shift;

	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(&primitive, 0, sizeof(primitive));
	memset(&image, 0, sizeof(image));
	memset(&triangle, 0, sizeof(triangle));
	memset(&texture, 0, sizeof(texture));
	memset(&surface, 0, sizeof(surface));
	memset(&texture_state, 0, sizeof(texture_state));
	memset(&reference, 0, sizeof(reference));
	reference.format = MH_PICTURE_RGB565;
	reference.width = 320;
	reference.height = 200;
	reference.pitch = 640;
	reference.pixels = pixels;
	reference.pixel_size = sizeof(pixels);
	memcpy(matrix, identity, sizeof(matrix));
	memcpy(model, identity, sizeof(model));
	matrix[10] = 0.5f;
	matrix[11] = 1;
	matrix[15] = 0;
	model[14] = 2;
	image.indices = indices;
	image.width = 17;
	image.height = 9;
	image.size = sizeof(indices);
	image.colors = 256;

	for (i = 0; i < sizeof(image.palette); i++)
		image.palette[i] = (unsigned char)((i * 37) & 255);

	for (i = 0; i < sizeof(indices); i++)
		indices[i] = (unsigned char)((i * 13) & 255);

	for (i = 0; i < 3; i++) {
		init_vertex(&vertices[i], xy[i][0], xy[i][1],
			i == 1 ? 16.5f / 17 : 0.5f / 17,
			i == 2 ? 8.5f / 9 : 0.5f / 9);
		vertices[i].position[0] *= depths[i];
		vertices[i].position[1] *= depths[i];
		vertices[i].position[2] = depths[i] - 2;
		triangle.xy[i][0] = xy[i][0];
		triangle.xy[i][1] = xy[i][1];
		triangle.perspective_weight[i] = 1 / depths[i];
		triangle.uv[i][0] = vertices[i].uv[0] * 17;
		triangle.uv[i][1] = vertices[i].uv[1] * 9;
		triangle.color[0][i] = 128;
	}

	draw.count = primitive.count = 3;
	draw.prelit = 1;
	primitive.type = 2;
	primitive.flags = 1;
	mesh.vertices = vertices;
	mesh.vertex_count = 3;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.primitives = &primitive;
	mesh.primitive_count = 1;
	mesh.indexed_textures = &image;
	mesh.indexed_texture_count = 1;
	meshes[0] = &mesh;
	texture.indices = indices;
	texture.width = 17;
	texture.height = 9;
	texture.size = sizeof(indices);
	texture.palette = &palette;
	triangle.texture = &texture;
	surface.pixels = pixels;
	surface.size = sizeof(pixels);
	surface.width = 320;
	surface.height = 200;
	surface.pitch = 640;
	surface.bits[0] = surface.bits[2] = 5;
	surface.bits[1] = 6;
	surface.shift[0] = 11;
	surface.shift[1] = 5;
	CHECK(mh_software_palette_build(&palette, image.palette,
		sizeof(image.palette), 256, &triangle, 6,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &other, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_retro(
		renderer, matrix, identity, model, 0, NULL, NULL));
	CHECK(mh_renderer_tga_capture(renderer, &saved));

	for (mode = 0; mode < 3; mode++) {
		for (flags = 0; flags < 4; flags++) {
			primitive.flags = (unsigned char)flags;

			for (shift = 2; shift <= 5; shift++) {
				mh_error_set(&error, MH_ERROR_FAILED, "stale");
				CHECK(mh_renderer_set_texture_mapping(renderer,
					(enum mh_texture_mapping)mode,
					1U << shift,
					&error) == MH_ERROR_NONE);
				CHECK(error.code == MH_ERROR_NONE &&
					error.message == NULL);
				CHECK(mh_renderer_scene_frame_retro(renderer,
					matrix, identity, model, 0, NULL,
					NULL));
				CHECK(mh_renderer_tga_capture(
					renderer, &picture));
				triangle.perspective_shift =
					mode == 2 || (mode == 1 && (flags & 1))
					? shift
					: 0;

				texture.zero_transparent = (flags & 2) != 0;
				memset(pixels, 0, sizeof(pixels));
				CHECK(mh_software_triangle(&surface, &triangle,
					&texture_state,
					NULL) == MH_ERROR_NONE);

				CHECK(mh_image_tga_encode(&reference, &expected,
					NULL) == MH_ERROR_NONE);
				CHECK(picture.size == expected.size &&
					memcmp(picture.data, expected.data,
						expected.size) == 0);
				free(expected.data);

				if (mode == 1 && flags == 1 && shift == 5)
					CHECK(memcmp(saved.data, picture.data,
						saved.size) == 0);

				if (mode == 0 && flags == 1)
					CHECK(memcmp(saved.data, picture.data,
						saved.size) != 0);

				free(picture.data);
			}
		}
	}

	/* A later frame must inherit the previous frame's raster cache. */
	vertices[1].position[0] = (40.5f / 160 - 1) * depths[1];
	triangle.xy[1][0] = 40.5f;
	triangle.perspective_shift = 0;
	CHECK(mh_renderer_set_texture_mapping(
		renderer, MH_TEXTURE_AFFINE, 32, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_update_vertices(renderer));

	for (flags = 0; flags <= 2; flags += 2) {
		primitive.flags = (unsigned char)flags;
		texture.zero_transparent = flags != 0;
		CHECK(mh_renderer_scene_frame_retro(
			renderer, matrix, identity, model, 0, NULL, NULL));
		CHECK(mh_renderer_tga_capture(renderer, &picture));
		memset(pixels, 0, sizeof(pixels));
		CHECK(mh_software_triangle(&surface, &triangle, &texture_state,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_image_tga_encode(&reference, &expected, NULL) ==
			MH_ERROR_NONE);
		CHECK(picture.size == expected.size &&
			memcmp(picture.data, expected.data, expected.size) ==
				0);
		free(picture.data);
		free(expected.data);
	}

	primitive.flags = 1;
	CHECK(mh_renderer_scene_frame_retro(
		other, matrix, identity, model, 0, NULL, NULL));
	CHECK(mh_renderer_tga_capture(other, &next));
	CHECK(memcmp(saved.data, next.data, saved.size) == 0);
	free(next.data);
	CHECK(mh_renderer_set_texture_mapping(
		other, MH_TEXTURE_AFFINE, 3, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_set_texture_mapping(other, (enum mh_texture_mapping)3,
		32, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_scene_frame_retro(
		other, matrix, identity, model, 0, NULL, NULL));
	CHECK(mh_renderer_tga_capture(other, &next));
	CHECK(memcmp(saved.data, next.data, saved.size) == 0);
	free(next.data);
	model[14] = -20;
	CHECK(!mh_renderer_scene_frame_retro(
		other, matrix, identity, model, 0, NULL, NULL));
	CHECK(mh_renderer_tga_capture(other, &next));
	CHECK(memcmp(saved.data, next.data, saved.size) == 0);
	free(next.data);
	free(saved.data);
	mh_renderer_destroy(other);
	mh_renderer_destroy(renderer);

	return 0;
}

int
main(int argc, char *argv[])
{
	unsigned char data[ARCHIVE_CAPACITY];
	struct mh_file file;
	struct mh_pdi pdi;
	struct mh_image_collection *collection = NULL;
	struct mh_pdi_entry entry, first;
	struct mh_indexed_image indexed;
	struct mh_image image;
	size_t size, i;
	unsigned long slot, count;

	CHECK(argc >= 1 && argc <= 3);
	CHECK(decode_guards() == 0);

	if (argc == 1) {
		size = build_archive(data, 0, 0);
		CHECK(mh_pdi_parse(data, size, &pdi, NULL) == MH_ERROR_NONE);
		CHECK(mh_pdi_find(&pdi, "texture.iff", &entry));
		CHECK(mh_image_pdi_open(data, size, &collection, NULL) ==
			MH_ERROR_NONE);
		CHECK(render_image(collection, &entry, NULL, 1, 0) == 0);
		CHECK(check_cutout_scene(&pdi) == 0);
		CHECK(check_perspective_scene() == 0);
		puts("Indexed decode, mesh loading, opaque/cutout frames and failure preservation passed");
		mh_image_collection_close(collection);
		return 0;
	}

	CHECK(mh_file_read(argv[1], &file, NULL) == MH_ERROR_NONE);
	CHECK(mh_pdi_parse(file.data, file.size, &pdi, NULL) == MH_ERROR_NONE);
	CHECK(mh_image_pdi_open(file.data, file.size, &collection, NULL) ==
		MH_ERROR_NONE);
	memset(&first, 0, sizeof(first));
	count = 0;

	for (slot = 0; slot < pdi.capacity; slot++) {
		if (!mh_pdi_entry(&pdi, slot, &entry))
			continue;

		CHECK(mh_image_ilbm_decode_indexed(entry.data, entry.size,
			&indexed, NULL) == MH_ERROR_NONE);
		CHECK(mh_image_ilbm_decode_texture(entry.data, entry.size,
			&image, NULL) == MH_ERROR_NONE);
		CHECK(image.width == indexed.width &&
			image.height == indexed.height);

		for (i = 0; i < indexed.size; i++) {
			CHECK(memcmp(image.rgba + i * 4,
				indexed.palette + indexed.indices[i] * 3,
				3) == 0);
			CHECK(image.rgba[i * 4 + 3] ==
				(indexed.indices[i] == 0 ? 0 : 255));
		}

		if (first.data == NULL && indexed.width <= 256 &&
			indexed.height <= 256)
			first = entry;

		free(image.rgba);
		free(indexed.indices);
		count++;
	}

	CHECK(first.data != NULL);
	CHECK(render_image(collection, &first, argc == 3 ? argv[2] : NULL, 0,
		0) == 0);
	CHECK(render_image(collection, &first, NULL, 0, 1) == 0);
	printf("%lu indexed images checked; rendered %s opaque/cutout through the software backend\n",
		count, first.name);
	mh_image_collection_close(collection);
	free(file.data);

	return 0;
}
