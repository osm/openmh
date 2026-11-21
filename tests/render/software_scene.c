#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_provider.h"
#include "backends/render/software_raster.h"
#include "engine/core/binary.h"
#include "interfaces/render/backend.h"

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

static void
init_vertex(struct mh_vertex *output, float x, float y, unsigned int channel)
{
	memset(output, 0, sizeof(*output));
	output->position[0] = x / 160.0f - 1.0f;
	output->position[1] = 1.0f - y / 100.0f;
	output->position[2] = 0.5f;
	output->color[channel] = 1;
}

static unsigned long
picture_pixel(const struct mh_file *image, unsigned int x, unsigned int y)
{
	return mh_read_u16le(
		image->data + PIXEL_OFFSET + ((199 - y) * 320 + x) * 2);
}

static int
raster_guards(void)
{
	unsigned char pixels[128], saved[128];
	struct mh_software_surface surface;
	struct mh_software_triangle triangle;

	memset(&surface, 0, sizeof(surface));
	memset(&triangle, 0, sizeof(triangle));
	memset(pixels, 0xa5, sizeof(pixels));
	memcpy(saved, pixels, sizeof(saved));
	surface.pixels = pixels;
	surface.size = sizeof(pixels);
	surface.pitch = 16;
	surface.width = surface.height = 8;
	surface.bits[0] = surface.bits[1] = surface.bits[2] = 5;
	surface.shift[0] = 10;
	surface.shift[1] = 5;
	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(saved, pixels, sizeof(pixels)) == 0);
	surface.size--;
	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_INVALID);
	surface.size++;
	surface.pitch = 0;
	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_INVALID);
	surface.pitch = 16;
	surface.shift[1] = 10;
	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_INVALID);
	surface.shift[1] = 5;
	triangle.xy[0][1] = -30001;
	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(memcmp(saved, pixels, sizeof(pixels)) == 0);
	triangle.xy[0][1] = 0;
	triangle.fog = 2;
	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_RANGE);
	triangle.fog = 0;
	triangle.smooth = 1;
	triangle.color[2][1] = 256;
	CHECK(mh_software_triangle(&surface, &triangle, NULL, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(saved, pixels, sizeof(pixels)) == 0);

	return 0;
}

/* The scene adapter must pass offscreen XY through to the edge walker. */
static int
check_screen_scene(void)
{
	struct mh_mesh mesh;
	struct mh_vertex vertices[3];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	const struct mh_mesh *meshes[1];
	struct mh_renderer *renderer;
	struct mh_file image;
	unsigned int i;

	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(&primitive, 0, sizeof(primitive));
	init_vertex(&vertices[0], -20, -40, 0);
	init_vertex(&vertices[1], 240, 40, 0);
	init_vertex(&vertices[2], 80, 260, 0);
	draw.count = primitive.count = mesh.vertex_count = 3;
	draw.prelit = 1;
	mesh.vertices = vertices;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.primitives = &primitive;
	mesh.primitive_count = 1;
	meshes[0] = &mesh;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &image));
	CHECK(picture_pixel(&image, 0, 0) == 0x7c00);
	CHECK(picture_pixel(&image, 80, 0) == 0x7c00);
	CHECK(picture_pixel(&image, 80, 199) == 0x7c00);
	CHECK(picture_pixel(&image, 319, 0) == 0);
	free(image.data);

	/* Entirely above the viewport is a successful empty frame. */
	for (i = 0; i < 3; i++)
		init_vertex(&vertices[i], (float)(20 + i * 50),
			(float)(-10 - (int)i * 20), 0);

	CHECK(mh_renderer_update_vertices(renderer));
	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &image));

	for (i = PIXEL_OFFSET; i < image.size; i++)
		CHECK(image.data[i] == 0);

	free(image.data);
	mh_renderer_destroy(renderer);

	return 0;
}

static int
check_quad_scene(void)
{
	static const unsigned int types[5] = {0, 2, 2, 2, 4};
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	static const float xy[4][2] = {
		{20, -40}, {270, 50}, {240, 180}, {60, 160}};

	struct mh_mesh mesh, expected_mesh, crowded;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh_primitive quad, halves[2], duplicates[2];
	struct mh_indexed_image texture;
	const struct mh_mesh *meshes[1];
	struct mh_renderer *renderer, *reference, *retained;
	struct mh_file image, expected, saved;
	unsigned char indices[17 * 9];
	unsigned int mode, type, i, corner;

	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(&quad, 0, sizeof(quad));
	memset(halves, 0, sizeof(halves));
	memset(&texture, 0, sizeof(texture));
	draw.count = quad.count = mesh.vertex_count = 6;
	draw.prelit = 1;
	mesh.vertices = vertices;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.primitives = &quad;
	mesh.primitive_count = 1;
	mesh.indexed_textures = &texture;
	mesh.indexed_texture_count = 1;
	texture.indices = indices;
	texture.size = sizeof(indices);
	texture.width = 17;
	texture.height = 9;
	texture.colors = 256;

	for (i = 0; i < sizeof(indices); i++)
		indices[i] = (unsigned char)((i * 13) & 255);

	for (i = 0; i < sizeof(texture.palette); i++)
		texture.palette[i] = (unsigned char)((i * 37) & 255);

	expected_mesh = mesh;
	expected_mesh.primitives = halves;
	expected_mesh.primitive_count = 2;

	/* Independent triangle submissions in the 023,012 order. */
	halves[0].first = 3;
	halves[0].count = halves[1].count = 3;

	for (mode = 0; mode < 5; mode++) {
		type = types[mode];
		quad.flags = halves[0].flags = halves[1].flags =
			mode == 2 ? 2 : 0;
		draw.alpha_test = mode == 3;
		quad.type = (unsigned char)(type + 1);
		halves[0].type = halves[1].type = (unsigned char)type;

		for (i = 0; i < 6; i++) {
			corner = corners[i];
			init_vertex(&vertices[i], xy[corner][0], xy[corner][1],
				type == 4 ? corner % 3 : 0);
			vertices[i].uv[0] =
				(corner == 1 || corner == 2 ? 16.5f : .5f) / 17;
			vertices[i].uv[1] = (corner >= 2 ? 8.5f : .5f) / 9;
		}

		meshes[0] = &mesh;
		CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1,
			320, 200, 0, &renderer, NULL) == MH_ERROR_NONE);
		meshes[0] = &expected_mesh;
		CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1,
			320, 200, 0, &reference, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_frame(renderer, identity, identity, 0));
		CHECK(mh_renderer_frame(reference, identity, identity, 0));
		CHECK(mh_renderer_tga_capture(renderer, &image));
		CHECK(mh_renderer_tga_capture(reference, &expected));
		CHECK(image.size == expected.size &&
			memcmp(image.data, expected.data, image.size) == 0);
		CHECK(picture_pixel(&image, 100, 70) != 0);

		/*
		 * Malformed expanded duplicates must not replace a good frame.
		 */
		vertices[3].position[0] += .25f;
		CHECK(mh_renderer_update_vertices(renderer));
		CHECK(!mh_renderer_frame(renderer, identity, identity, 0));
		CHECK(mh_renderer_tga_capture(renderer, &saved));
		CHECK(saved.size == image.size &&
			memcmp(saved.data, image.data, image.size) == 0);
		free(saved.data);
		vertices[3] = vertices[0];
		CHECK(mh_renderer_update_vertices(renderer));
		CHECK(mh_renderer_frame(renderer, identity, identity, 0));

		quad.count = 3;
		meshes[0] = &mesh;
		retained = renderer;
		CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1,
			320, 200, 0, &retained,
			NULL) == MH_ERROR_UNSUPPORTED);
		CHECK(retained == renderer);
		quad.count = 6;
		crowded = mesh;
		duplicates[0] = duplicates[1] = quad;
		crowded.primitives = duplicates;
		crowded.primitive_count = 2;
		meshes[0] = &crowded;
		CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1,
			320, 200, 0, &retained,
			NULL) == MH_ERROR_UNSUPPORTED);
		CHECK(retained == renderer);
		free(image.data);
		free(expected.data);
		mh_renderer_destroy(renderer);
		mh_renderer_destroy(reference);
	}

	return 0;
}

int
main(int argc, char *argv[])
{
	struct mh_mesh mesh;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh_primitive primitives[2];
	const struct mh_mesh *meshes[1];
	struct mh_renderer *renderer, *retained;
	struct mh_file first, next, green;
	unsigned long shade;
	struct mh_error error;
	struct mh_fog fog;
	FILE *output;
	unsigned int i;
	int written;

	CHECK(argc == 1 || argc == 2);
	CHECK(raster_guards() == 0);
	CHECK(check_screen_scene() == 0);
	CHECK(check_quad_scene() == 0);
	memset(&mesh, 0, sizeof(mesh));
	memset(&draw, 0, sizeof(draw));
	memset(primitives, 0, sizeof(primitives));
	memset(&fog, 0, sizeof(fog));
	memset(&first, 0, sizeof(first));
	init_vertex(&vertices[0], 20, 20, 0);
	init_vertex(&vertices[1], 270, 50, 0);
	init_vertex(&vertices[2], 100, 180, 0);
	init_vertex(&vertices[3], 150, 60, 2);
	init_vertex(&vertices[4], 300, 150, 2);
	init_vertex(&vertices[5], 160, 180, 2);
	draw.count = 6;
	draw.prelit = 1;
	primitives[0].count = primitives[1].count = 3;
	primitives[1].first = 3;
	mesh.vertices = vertices;
	mesh.vertex_count = 6;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.primitives = primitives;
	mesh.primitive_count = 2;
	meshes[0] = &mesh;
	renderer = NULL;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &renderer, &error) == MH_ERROR_NONE);
	CHECK(mh_renderer_profile(renderer) == MH_RENDER_SOFTWARE);
	CHECK(!mh_renderer_tga_capture(renderer, &first));
	CHECK(first.data == NULL && first.size == 0);
	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &first));
	CHECK(first.size == PIXEL_OFFSET + 320UL * 200 * 2);
	CHECK(picture_pixel(&first, 0, 0) == 0);
	CHECK(picture_pixel(&first, 80, 60) == 0x7c00);
	CHECK(picture_pixel(&first, 175, 90) == 0x001f);
	CHECK(picture_pixel(&first, 190, 130) == 0x001f);

	if (argc == 2) {
		output = fopen(argv[1], "wb");
		CHECK(output != NULL);
		written =
			fwrite(first.data, 1, first.size, output) == first.size;

		if (fclose(output) != 0)
			written = 0;

		CHECK(written);
	}

	CHECK(mh_renderer_mesh_visible(renderer, 0, 0));
	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &next));
	CHECK(picture_pixel(&next, 80, 60) == 0);
	free(next.data);
	CHECK(mh_renderer_mesh_visible(renderer, 0, 1));
	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &next));
	CHECK(next.size == first.size &&
		memcmp(next.data, first.data, first.size) == 0);
	free(next.data);

	for (i = 3; i < 6; i++) {
		vertices[i].color[2] = 0;
		vertices[i].color[1] = 1;
	}

	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &next));
	CHECK(memcmp(next.data, first.data, first.size) == 0);
	free(next.data);
	CHECK(mh_renderer_update_vertices(renderer));
	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &green));
	CHECK(picture_pixel(&green, 190, 130) == 0x03e0);

	/*
	 * Varying colors on a flat primitive fail before replacing the image.
	 */
	vertices[4].color[0] = 1;
	CHECK(mh_renderer_update_vertices(renderer));
	CHECK(!mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &next));
	CHECK(next.size == green.size &&
		memcmp(next.data, green.data, green.size) == 0);
	free(next.data);
	CHECK(!mh_renderer_set_fog(renderer, &fog));
	CHECK(mh_renderer_set_fog(renderer, NULL));
	CHECK(mh_renderer_set_motion_blur(renderer, 1));
	CHECK(mh_renderer_set_motion_blur(renderer, 0));
	CHECK(!mh_renderer_mesh_visible(renderer, 1, 0));
	primitives[0].type = 2;
	retained = renderer;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &retained, &error) == MH_ERROR_UNSUPPORTED);
	CHECK(retained == renderer && error.code == MH_ERROR_UNSUPPORTED);
	free(first.data);
	free(green.data);
	mh_renderer_destroy(renderer);

	/* Type 4 keeps distinct corner colors through the backend contract. */
	primitives[0].type = primitives[1].type = 4;
	init_vertex(&vertices[0], 20, 20, 0);
	init_vertex(&vertices[1], 270, 50, 1);
	init_vertex(&vertices[2], 100, 180, 2);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &renderer, &error) == MH_ERROR_NONE);
	CHECK(mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &first));
	shade = picture_pixel(&first, 80, 60);
	CHECK((shade & 0x7c00) != 0 && (shade & 0x03e0) != 0 &&
		(shade & 0x001f) != 0);
	CHECK(shade != picture_pixel(&first, 80, 80));
	CHECK(picture_pixel(&first, 0, 0) == 0);
	vertices[1].color[1] = 2;
	CHECK(mh_renderer_update_vertices(renderer));
	CHECK(!mh_renderer_frame(renderer, identity, identity, 0));
	CHECK(mh_renderer_tga_capture(renderer, &next));
	CHECK(next.size == first.size &&
		memcmp(next.data, first.data, first.size) == 0);
	free(next.data);
	free(first.data);
	mh_renderer_destroy(renderer);

	CHECK(mh_renderer_open(mh_software_backend(), NULL, NULL, 0, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_ui_frame(renderer, NULL));
	CHECK(mh_renderer_tga_capture(renderer, &next));
	CHECK(picture_pixel(&next, 80, 60) == 0);
	free(next.data);
	mh_renderer_destroy(renderer);
	puts("CPU-only scene, snapshots, failure atomicity and raster bounds passed");

	return 0;
}
