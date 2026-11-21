#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_provider.h"
#include "engine/core/binary.h"
#include "interfaces/render/backend.h"

#define PIXEL_OFFSET 18

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_projection		projection;
	struct mh_mesh			mesh;
	struct mh_vertex		vertices[6];
	struct mh_draw			draw;
	struct mh_mesh_primitive	primitive;
	struct mh_mesh_face		face;
	struct mh_indexed_image		image;
	const struct mh_mesh		*meshes[1];
	unsigned char			indices[17 * 9];
	unsigned char			world_colors[18];
	float				view[16], model[16];
	struct mh_renderer		*renderer;
};

/* Two independently submitted faces share one mesh, around a second mesh. */
struct span_fixture {
	struct mh_mesh			mesh;
	struct mh_vertex		vertices[12];
	struct mh_draw			draws[2];
	struct mh_mesh_primitive	primitives[2];
	struct mh_indexed_image		images[2];
};

struct detail_fixture {
	struct mh_mesh			mesh;
	struct mh_vertex		vertices[MH_MESH_LODS * 6];
	struct mh_draw			draws[MH_MESH_LODS];
	struct mh_mesh_primitive	primitives[MH_MESH_LODS];
	struct mh_mesh_face		faces[MH_MESH_LODS];
};

static void
pack_spans(const struct fixture f[3], struct span_fixture *packed)
{
	unsigned int i;

	packed->mesh = f[0].mesh;
	packed->mesh.vertices = packed->vertices;
	packed->mesh.vertex_count = 12;
	packed->mesh.draws = packed->draws;
	packed->mesh.draw_count = 2;
	packed->mesh.primitives = packed->primitives;
	packed->mesh.primitive_count = 2;
	packed->mesh.indexed_textures = packed->images;
	packed->mesh.indexed_texture_count = 2;

	for (i = 0; i < 2; i++) {
		memcpy(packed->vertices + i * 6, f[i * 2].vertices,
			sizeof(f[i * 2].vertices));
		packed->draws[i] = f[i * 2].draw;
		packed->draws[i].first = i * 6;
		packed->draws[i].texture = i;
		packed->primitives[i] = f[i * 2].primitive;
		packed->primitives[i].first = i * 6;
		packed->primitives[i].draw = i;
		packed->images[i] = f[i * 2].image;
	}
}

static void
pack_details(const struct fixture *f, struct detail_fixture *packed,
	unsigned int selected)
{
	struct mh_mesh_lod *range;
	unsigned int i, j;

	packed->mesh = f->mesh;
	packed->mesh.vertices = packed->vertices;
	packed->mesh.vertex_count = MH_MESH_LODS * 6;
	packed->mesh.draws = packed->draws;
	packed->mesh.draw_count = MH_MESH_LODS;
	packed->mesh.primitives = packed->primitives;
	packed->mesh.primitive_count = MH_MESH_LODS;
	packed->mesh.lod_count = MH_MESH_LODS;
	packed->mesh.lod = selected;

	if (f->mesh.faces != NULL) {
		packed->mesh.faces = packed->faces;
		packed->mesh.face_count = MH_MESH_LODS;
	}

	for (i = 0; i < MH_MESH_LODS; i++) {
		memcpy(packed->vertices + i * 6, f->vertices,
			sizeof(f->vertices));
		packed->draws[i] = f->draw;
		packed->draws[i].first = i * 6;
		packed->primitives[i] = f->primitive;
		packed->primitives[i].first = i * 6;
		packed->primitives[i].draw = i;
		range = &packed->mesh.lods[i];
		range->vertex_first = i * 6;
		range->vertex_count = 6;
		range->draw_first = range->primitive_first = i;
		range->draw_count = range->primitive_count = 1;

		if (f->mesh.faces != NULL) {
			packed->faces[i] = f->face;
			packed->faces[i].first = i * 6;
			range->face_first = i;
			range->face_count = 1;
		}

		/* Nonselected levels would produce an empty picture. */
		if (i != selected)
			for (j = 0; j < 6; j++)
				packed->vertices[i * 6 + j].position[2] = 1000;
	}
}

static int
init_fixture(struct fixture *f)
{
	unsigned int i, x, y;

	memset(f, 0, sizeof(*f));
	f->view[0] = f->view[5] = f->view[15] = 1;
	f->view[10] = -1;
	f->model[0] = f->model[5] = f->model[10] = f->model[15] = 1;
	f->draw.count = f->mesh.vertex_count = f->primitive.count = 6;
	f->primitive.type = 1;
	f->draw.prelit = 1;
	f->mesh.vertices = f->vertices;
	f->mesh.primitives = &f->primitive;
	f->mesh.primitive_count = 1;
	f->mesh.draws = &f->draw;
	f->mesh.draw_count = 1;
	f->mesh.indexed_textures = &f->image;
	f->mesh.indexed_texture_count = 1;
	f->image.indices = f->indices;
	f->image.size = sizeof(f->indices);
	f->image.width = 17;
	f->image.height = 9;
	f->image.colors = 256;
	f->meshes[0] = &f->mesh;

	for (i = 0; i < sizeof(f->image.palette); i++)
		f->image.palette[i] =
			(unsigned char)(((i / 3) * 37 + (i % 3) * 83) & 255);

	for (y = 0; y < 9; y++)
		for (x = 0; x < 17; x++)
			f->indices[y * 17 + x] =
				(unsigned char)((x * 13 + y * 31 +
							(x ^ y) * 7) &
					255);

	CHECK(mh_projection_setup(320, 200, 1.3333333333333335f, 1, 1, 1000,
		&f->projection, NULL) == MH_ERROR_NONE);

	return 0;
}

static int
open_fixture(struct fixture *f)
{
	CHECK(init_fixture(f) == 0);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, f->meshes, 1, 320,
		200, 0, &f->renderer, NULL) == MH_ERROR_NONE);

	return 0;
}

static int
draw_fixture(struct fixture *f, struct mh_file *image)
{
	struct mh_error error;

	CHECK(mh_renderer_update_vertices(f->renderer));

	if (mh_renderer_scene_frame_lens(f->renderer, &f->projection, f->view,
		f->model, 0, NULL, NULL, NULL, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 1;
	}

	CHECK(mh_renderer_tga_capture(f->renderer, image));

	return 0;
}

static int
install_colored_environment(struct mh_renderer *renderer)
{
	struct mh_indexed_image images[3];
	const struct mh_indexed_image *maps[3];
	unsigned char *indices;
	unsigned int i;

	indices = calloc((size_t)256 * 256, 1);
	CHECK(indices != NULL);
	memset(images, 0, sizeof(images));

	for (i = 0; i < 3; i++) {
		images[i].width = images[i].height = 256;
		images[i].colors = 1;
		images[i].size = (size_t)256 * 256;
		images[i].indices = indices;
		images[i].palette[i] = 255;
		maps[i] = &images[i];
	}

	CHECK(mh_renderer_set_environment(renderer, maps, NULL) ==
		MH_ERROR_NONE);
	free(indices);
	memset(images, 0, sizeof(images));

	return 0;
}

/* Ordered stream modes: model, smooth and reflection add lighting payloads. */

static int
cue_guards(void)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct fixture f[2];
	struct mh_depth_cue cue;
	struct mh_depth_cue_gradient gradient;
	struct mh_file baseline, fogged, image;
	unsigned int i, j;

	for (j = 0; j < 2; j++) {
		CHECK(open_fixture(&f[j]) == 0);

		for (i = 0; i < 6; i++) {
			f[j].vertices[i].position[0] = xy[i][0] * 250;
			f[j].vertices[i].position[1] = xy[i][1] * 250;
			f[j].vertices[i].position[2] = 500;
			f[j].vertices[i].color[0] = 1;
		}
	}

	CHECK(draw_fixture(&f[0], &baseline) == 0);
	memset(&cue, 0, sizeof(cue));
	cue.color[1] = 255;
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, &cue, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(&f[0], &fogged) == 0);
	CHECK(baseline.size == fogged.size &&
		memcmp(baseline.data, fogged.data, baseline.size) != 0);
	CHECK(draw_fixture(&f[1], &image) == 0);
	CHECK(image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);

	/* The renderer owns the whole gradient, including unused rows. */
	memset(&gradient, 0, sizeof(gradient));
	gradient.height = 200;
	gradient.above[1] = gradient.below[1] = 255;

	for (i = 0; i < MH_DEPTH_CUE_ROWS; i++)
		gradient.rows[i][1] = 255;

	cue.gradient = &gradient;
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, &cue, NULL) ==
		MH_ERROR_NONE);
	memset(&gradient, 0, sizeof(gradient));
	CHECK(draw_fixture(&f[0], &image) == 0);
	CHECK(image.size == fogged.size &&
		memcmp(image.data, fogged.data, image.size) == 0);
	free(image.data);
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, &cue, NULL) ==
		MH_ERROR_INVALID);
	gradient.height = MH_DEPTH_CUE_ROWS + 1;
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, &cue, NULL) ==
		MH_ERROR_INVALID);
	gradient.height = 1;
	gradient.rows[MH_DEPTH_CUE_ROWS - 1][2] = -1;
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, &cue, NULL) ==
		MH_ERROR_INVALID);
	gradient.rows[MH_DEPTH_CUE_ROWS - 1][2] = 0;
	gradient.top = 1e7f;
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, &cue, NULL) ==
		MH_ERROR_INVALID);
	cue.gradient = NULL;

	/*
	 * Invalid settings and unsupported matrix frames retain the last image.
	 */
	cue.start = 1;
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, &cue, NULL) ==
		MH_ERROR_INVALID);
	cue.start = 0;
	cue.color[0] = -1;
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, &cue, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_renderer_set_depth_cue(NULL, NULL, NULL) == MH_ERROR_INVALID);
	CHECK(!mh_renderer_scene_frame(
		f[0].renderer, f[0].model, f[0].view, NULL, 0));
	CHECK(mh_renderer_tga_capture(f[0].renderer, &image));
	CHECK(image.size == fogged.size &&
		memcmp(image.data, fogged.data, image.size) == 0);
	free(image.data);
	CHECK(draw_fixture(&f[0], &image) == 0);
	CHECK(image.size == fogged.size &&
		memcmp(image.data, fogged.data, image.size) == 0);
	free(image.data);
	CHECK(mh_renderer_set_depth_cue(f[0].renderer, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(&f[0], &image) == 0);
	CHECK(image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);
	free(baseline.data);
	free(fogged.data);

	for (j = 0; j < 2; j++)
		mh_renderer_destroy(f[j].renderer);

	return 0;
}

static int
lens_guards(void)
{
	static const float points[6][3] = {{-1, 1, .5f}, {1, 1, 2}, {1, -1, 2},
		{-1, 1, .5f}, {1, -1, 2}, {-1, -1, 2}};

	struct fixture f;
	struct mh_file image, next;
	struct mh_ui_list ui;
	unsigned int i;

	CHECK(open_fixture(&f) == 0);
	memset(&ui, 0, sizeof(ui));

	for (i = 0; i < 6; i++) {
		memcpy(f.vertices[i].position, points[i], sizeof(points[i]));
		f.vertices[i].color[0] = 1;
	}

	CHECK(draw_fixture(&f, &image) == 0);
	CHECK(mh_read_u16le(image.data + PIXEL_OFFSET +
		((199 - 100) * 320 + 160) * 2) != 0);

	/* Equal and opposite native model/view translations preserve pixels. */
	f.model[12] = .25f;
	f.view[12] = -.25f;
	CHECK(draw_fixture(&f, &next) == 0);
	CHECK(next.size == image.size &&
		memcmp(next.data, image.data, image.size) == 0);
	free(next.data);
	f.model[3] = 1;
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		f.model, 0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	f.model[3] = 0;
	f.projection.clip.extent[0] = 640;
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, NULL, NULL) == MH_ERROR_INVALID);
	f.projection.clip.extent[0] = 320;
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, &ui, NULL, NULL, NULL) == MH_ERROR_INVALID);
	CHECK(mh_renderer_tga_capture(f.renderer, &next));
	CHECK(next.size == image.size &&
		memcmp(next.data, image.data, image.size) == 0);
	free(next.data);
	free(image.data);

	/*
	 * A single far corner rejects the whole primitive, rather than
	 * clipping.
	 */
	f.vertices[1].position[2] = 1000;
	CHECK(draw_fixture(&f, &image) == 0);

	for (i = PIXEL_OFFSET; i < image.size; i++)
		CHECK(image.data[i] == 0);

	free(image.data);
	mh_renderer_destroy(f.renderer);

	return 0;
}

static unsigned int
center_pixel(const struct mh_file *image)
{
	return mh_read_u16le(
		image->data + PIXEL_OFFSET + ((199 - 100) * 320 + 160) * 2);
}

static int
brightness_guards(void)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	static const float invalid[] = {-1, 201, FLT_MAX};
	struct fixture f;
	struct mh_file bright, image;
	double brightness[3];
	unsigned int i;

	CHECK(init_fixture(&f) == 0);
	f.mesh.world_colors = f.world_colors;
	f.mesh.world_vertex_count = 6;
	brightness[0] = brightness[1] = brightness[2] = 4;

	for (i = 0; i < 6; i++) {
		f.vertices[i].position[0] = xy[i][0];
		f.vertices[i].position[1] = xy[i][1];
		f.vertices[i].position[2] = 2;
		f.world_colors[i * 3] = 64;
	}

	CHECK(mh_mesh_world_brightness(&f.mesh, brightness, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, f.meshes, 1, 320,
		200, 0, &f.renderer, NULL) == MH_ERROR_NONE);
	CHECK(draw_fixture(&f, &bright) == 0 &&
		center_pixel(&bright) == 0x7c00);

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		f.mesh.world_brightness[0] = invalid[i];
		CHECK(!mh_renderer_update_vertices(f.renderer));
		CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection,
			f.view, NULL, 0, NULL, NULL, NULL,
			NULL) != MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(f.renderer, &image));
		CHECK(image.size == bright.size &&
			memcmp(image.data, bright.data, image.size) == 0);
		free(image.data);
	}

	/*
	 * Dim and restore from source RGB, never from already clamped colors.
	 */
	brightness[0] = 2;
	CHECK(mh_mesh_world_brightness(&f.mesh, brightness, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(&f, &image) == 0 && center_pixel(&image) == 0x4000);
	free(image.data);
	brightness[0] = 4;
	CHECK(mh_mesh_world_brightness(&f.mesh, brightness, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(&f, &image) == 0 && image.size == bright.size &&
		memcmp(image.data, bright.data, image.size) == 0);
	free(image.data);
	free(bright.data);
	mh_renderer_destroy(f.renderer);

	return 0;
}

static int
unavailable_state(struct fixture *f, struct mh_model_lighting *lighting)
{
	struct mh_file baseline, image;
	unsigned long pixel;
	size_t i, blue_pixels;

	CHECK(draw_fixture(f, &baseline) == 0);
	lighting->mode = MH_LIGHT_UNAVAILABLE;
	CHECK(mh_renderer_model_lighting(f->renderer, 0, lighting, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(f, &image) == 0);
	blue_pixels = 0;

	for (i = PIXEL_OFFSET; i < image.size; i += 2) {
		pixel = mh_read_u16le(image.data + i);
		CHECK(pixel == 0 || pixel == 31);
		blue_pixels += pixel == 31;
	}

	CHECK(blue_pixels > 0 && blue_pixels < 1000);
	free(image.data);
	f->vertices[0].position[2] = f->vertices[3].position[2] = .5f;
	CHECK(draw_fixture(f, &image) == 0);

	for (i = PIXEL_OFFSET; i < image.size; i += 2)
		CHECK(mh_read_u16le(image.data + i) == 0);

	free(image.data);
	f->vertices[0].position[2] = f->vertices[3].position[2] = 2;
	lighting->mode = MH_LIGHT_CAR;
	CHECK(mh_renderer_model_lighting(f->renderer, 0, lighting, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(f, &image) == 0 && image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);
	free(baseline.data);

	return 0;
}

static int
model_light_guards(void)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct fixture f;
	struct mh_model_lighting lighting;
	struct mh_point_light light;
	struct mh_file baseline, image;
	const struct mh_indexed_image *maps[3];
	unsigned long pixels[3];
	unsigned int i;

	CHECK(init_fixture(&f) == 0);
	f.mesh.faces = &f.face;
	f.mesh.face_count = 1;
	f.face.count = 6;
	f.face.type = 1;
	f.face.color[0] = 255;
	f.face.plane[2] = -1;
	f.draw.prelit = 0;

	for (i = 0; i < 6; i++) {
		f.vertices[i].position[0] = xy[i][0];
		f.vertices[i].position[1] = xy[i][1];
		f.vertices[i].position[2] = 2;
		f.vertices[i].normal[2] = 1;
	}

	CHECK(mh_renderer_open(mh_software_backend(), NULL, f.meshes, 1, 320,
		200, 0, &f.renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		f.model, 0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	memset(&lighting, 0, sizeof(lighting));
	memset(&light, 0, sizeof(light));
	lighting.mode = MH_LIGHT_OBJECT;
	lighting.brightness[0] = lighting.brightness[1] =
		lighting.brightness[2] = 1;
	lighting.lights = &light;
	lighting.light_count = 1;
	lighting.intensity = 1;
	light.position[2] = 10;
	light.color[0] = 1;
	light.radius = 100;
	light.intensity = 1;
	CHECK(mh_renderer_model_lighting(f.renderer, 0, &lighting, NULL) ==
		MH_ERROR_NONE);
	light.color[0] = 0;
	CHECK(draw_fixture(&f, &baseline) == 0 && center_pixel(&baseline) != 0);

	lighting.brightness[2] = -1;
	CHECK(mh_renderer_model_lighting(f.renderer, 0, &lighting, NULL) ==
		MH_ERROR_INVALID);
	CHECK(mh_renderer_model_lighting(f.renderer, 1, NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(draw_fixture(&f, &image) == 0 && image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);
	CHECK(mh_renderer_model_lighting(f.renderer, 0, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		f.model, 0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &image) &&
		image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);

	lighting.brightness[2] = 1;
	lighting.ambient[0] = .25f;
	lighting.mode = MH_LIGHT_CAR;
	CHECK(mh_renderer_model_lighting(f.renderer, 0, &lighting, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(&f, &image) == 0);
	free(baseline.data);
	baseline = image;

	/* Tilt the visible face around its center, preserving winding. */
	f.model[0] = f.model[10] = .5f;
	f.model[2] = -.8660254f;
	f.model[8] = .8660254f;
	f.model[12] = -2 * f.model[8];
	f.model[14] = 1;
	CHECK(draw_fixture(&f, &image) == 0 && center_pixel(&image) != 0 &&
		center_pixel(&image) < center_pixel(&baseline));
	free(image.data);
	f.model[0] = f.model[10] = 1;
	f.model[2] = f.model[8] = f.model[12] = f.model[14] = 0;
	CHECK(draw_fixture(&f, &image) == 0 && image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);
	free(baseline.data);

	f.face.type = f.primitive.type = 9;
	CHECK(draw_fixture(&f, &baseline) == 0);
	lighting.flat_detail = 1;
	CHECK(mh_renderer_model_lighting(f.renderer, 0, &lighting, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(&f, &image) == 0 && center_pixel(&image) != 0 &&
		center_pixel(&image) < center_pixel(&baseline));
	free(baseline.data);
	baseline = image;
	lighting.flat_detail = 2;
	CHECK(mh_renderer_model_lighting(f.renderer, 0, &lighting, NULL) ==
		MH_ERROR_INVALID);
	CHECK(draw_fixture(&f, &image) == 0 && image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);
	free(baseline.data);
	f.face.color[0] = f.face.color[1] = f.face.color[2] = 128;
	f.face.flags = f.primitive.flags = 0x60;
	f.draw.environment = 1;
	lighting.flat_detail = 0;
	lighting.flat_car = 1;
	lighting.ambient[0] = lighting.ambient[1] = lighting.ambient[2] = 1;
	CHECK(mh_renderer_model_lighting(f.renderer, 0, &lighting, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		f.model, 0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(install_colored_environment(f.renderer) == 0);

	for (i = 0; i < 3; i++) {
		f.face.flags = f.primitive.flags =
			(unsigned char)((i + 1) << 5);
		CHECK(draw_fixture(&f, &image) == 0);
		pixels[i] = center_pixel(&image);
		CHECK(pixels[i] != 0);
		free(image.data);
	}

	CHECK(pixels[0] != pixels[1] && pixels[1] != pixels[2] &&
		pixels[0] != pixels[2]);
	CHECK(draw_fixture(&f, &baseline) == 0);
	maps[0] = maps[1] = maps[2] = &f.image;
	CHECK(mh_renderer_set_environment(f.renderer, maps, NULL) ==
		MH_ERROR_INVALID);
	CHECK(draw_fixture(&f, &image) == 0 && image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);
	CHECK(mh_renderer_set_environment(f.renderer, NULL, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		f.model, 0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &image) &&
		image.size == baseline.size &&
		memcmp(image.data, baseline.data, image.size) == 0);
	free(image.data);
	free(baseline.data);
	lighting.flat_car = 0;
	CHECK(mh_renderer_model_lighting(f.renderer, 0, &lighting, NULL) ==
		MH_ERROR_NONE);
	CHECK(draw_fixture(&f, &image) == 0 && center_pixel(&image) != 0);
	free(image.data);
	CHECK(unavailable_state(&f, &lighting) == 0);
	mh_renderer_destroy(f.renderer);

	return 0;
}

static int
ordering_guards(void)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct fixture f;
	struct mh_vertex vertices[12];
	struct mh_mesh_primitive primitives[2], saved_primitive;
	struct mh_draw draws[2];
	struct mh_file image, saved;
	unsigned char hidden[2];
	unsigned int i;

	CHECK(init_fixture(&f) == 0);
	memset(vertices, 0, sizeof(vertices));
	memset(hidden, 0, sizeof(hidden));
	primitives[0] = primitives[1] = f.primitive;
	draws[0] = draws[1] = f.draw;
	primitives[1].first = draws[1].first = 6;
	primitives[1].draw = 1;
	f.mesh.vertices = vertices;
	f.mesh.vertex_count = 12;
	f.mesh.primitives = primitives;
	f.mesh.primitive_count = 2;
	f.mesh.draws = draws;
	f.mesh.draw_count = 2;
	f.mesh.draw_hidden = hidden;

	for (i = 0; i < 12; i++) {
		vertices[i].position[0] = xy[i % 6][0];
		vertices[i].position[1] = xy[i % 6][1];
		vertices[i].position[2] = i < 6 ? 2 : 4;
		vertices[i].color[i < 6 ? 0 : 2] = 1;
	}

	CHECK(mh_renderer_open(mh_software_backend(), NULL, f.meshes, 1, 320,
		200, 0, &f.renderer, NULL) == MH_ERROR_NONE);
	CHECK(draw_fixture(&f, &image) == 0);
	CHECK(center_pixel(&image) == 0x7c00);
	free(image.data);

	/* Same-bucket source order reverses, without reversing quad halves. */
	for (i = 6; i < 12; i++)
		vertices[i].position[2] = 2;

	CHECK(draw_fixture(&f, &image) == 0);
	CHECK(center_pixel(&image) == 0x7c00);
	free(image.data);
	saved_primitive = primitives[0];
	primitives[0] = primitives[1];
	primitives[1] = saved_primitive;
	CHECK(draw_fixture(&f, &image) == 0);
	CHECK(center_pixel(&image) == 31);
	free(image.data);
	hidden[1] = 1;
	CHECK(draw_fixture(&f, &saved) == 0);
	CHECK(center_pixel(&saved) == 0x7c00);

	/* A rejected prepared frame must preserve the published image. */
	vertices[1].color[0] = .5f;
	CHECK(mh_renderer_update_vertices(f.renderer));
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &image));
	CHECK(image.size == saved.size &&
		memcmp(image.data, saved.data, image.size) == 0);
	free(image.data);
	free(saved.data);
	vertices[1].color[0] = 1;
	hidden[1] = 0;
	CHECK(draw_fixture(&f, &image) == 0 && center_pixel(&image) == 31);
	free(image.data);
	CHECK(mh_renderer_mesh_visible(f.renderer, 0, 0));
	CHECK(draw_fixture(&f, &image) == 0 && center_pixel(&image) == 0);
	free(image.data);
	mh_renderer_destroy(f.renderer);

	return 0;
}

static int
check_scene_pixel(struct fixture *f, const float *models, unsigned int expected)
{
	struct mh_file image;

	CHECK(mh_renderer_scene_frame_lens(f->renderer, &f->projection, f->view,
		models, 0, NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f->renderer, &image));
	CHECK(center_pixel(&image) == expected);
	free(image.data);

	return 0;
}

static int
submission_guards(void)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct fixture f[3];
	const struct mh_mesh *meshes[3];
	struct mh_renderer_scene_part parts[3], bad[3];
	struct mh_file before, after;
	float models[48];
	unsigned int i, j;

	memset(parts, 0, sizeof(parts));

	for (i = 0; i < 3; i++) {
		CHECK(init_fixture(&f[i]) == 0);
		meshes[i] = &f[i].mesh;
		f[i].primitive.flags = 128;
		parts[i].mesh = i;
		parts[i].active_lod = 1;
		memcpy(models + i * 16, f[i].model, sizeof(f[i].model));

		for (j = 0; j < 6; j++) {
			f[i].vertices[j].position[0] = xy[j][0];
			f[i].vertices[j].position[1] = xy[j][1];
			f[i].vertices[j].position[2] = 2;
			f[i].vertices[j].color[i] = 1;
		}
	}

	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 3, 320, 200,
		0, &f[0].renderer, NULL) == MH_ERROR_NONE);
	CHECK(check_scene_pixel(f, NULL, 0x7c00) == 0);
	parts[1].model_role = MH_RENDER_MODEL_CHILD;
	CHECK(mh_renderer_scene_order(f[0].renderer, parts, 3));
	CHECK(check_scene_pixel(f, NULL, 0x03e0) == 0);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 3, 320, 200,
		0, &f[1].renderer, NULL) == MH_ERROR_NONE);
	CHECK(check_scene_pixel(&f[1], NULL, 0x7c00) == 0);
	mh_renderer_destroy(f[1].renderer);

	/* Invalid replacements preserve both the picture and retained order. */
	CHECK(mh_renderer_tga_capture(f[0].renderer, &before));
	CHECK(!mh_renderer_scene_order(f[0].renderer, NULL, 3));
	CHECK(!mh_renderer_scene_order(f[0].renderer, parts, 0));
	CHECK(!mh_renderer_scene_order(f[0].renderer, parts, 2));
	CHECK(!mh_renderer_scene_order(f[0].renderer, parts, (size_t)-1));

	for (i = 0; i < 6; i++) {
		memcpy(bad, parts, sizeof(bad));

		if (i == 0)
			bad[1].mesh = bad[0].mesh;
		else if (i == 1)
			bad[1].mesh = 3;
		else if (i == 2)
			bad[1].active_lod = 0;
		else if (i == 3)
			bad[1].model_role = (enum mh_renderer_model_role) - 1;
		else if (i == 4)
			bad[0].model_role = MH_RENDER_MODEL_CHILD;
		else
			bad[0].model_role = MH_RENDER_MODEL_WORLD;

		CHECK(!mh_renderer_scene_order(f[0].renderer, bad, 3));
	}

	CHECK(mh_renderer_tga_capture(f[0].renderer, &after));
	CHECK(after.size == before.size &&
		memcmp(after.data, before.data, before.size) == 0);
	free(before.data);
	free(after.data);
	CHECK(check_scene_pixel(f, NULL, 0x03e0) == 0);

	/* The stored copy must survive caller mutation. */
	memset(parts, 0, sizeof(parts));
	CHECK(check_scene_pixel(f, NULL, 0x03e0) == 0);
	CHECK(mh_renderer_scene_order(f[0].renderer, NULL, 0));
	CHECK(check_scene_pixel(f, NULL, 0x7c00) == 0);

	for (i = 0; i < 3; i++) {
		parts[i].mesh = 2 - i;
		parts[i].active_lod = 1;
	}

	CHECK(mh_renderer_scene_order(f[0].renderer, parts, 3));
	CHECK(check_scene_pixel(f, NULL, 31) == 0);

	/* Model matrices and visibility still address meshes, not parts. */
	models[2 * 16 + 12] = 10;
	CHECK(check_scene_pixel(f, models, 0x03e0) == 0);
	CHECK(mh_renderer_mesh_visible(f[0].renderer, 1, 0));
	CHECK(check_scene_pixel(f, models, 0x7c00) == 0);
	CHECK(mh_renderer_mesh_visible(f[0].renderer, 1, 1));
	CHECK(check_scene_pixel(f, models, 0x03e0) == 0);

	/* Matrix inspection follows submitted order, without depth sorting. */
	f[0].model[10] = .25f;
	parts[1].model_role = MH_RENDER_MODEL_CHILD;
	CHECK(mh_renderer_scene_order(f[0].renderer, parts, 3));
	CHECK(mh_renderer_scene_frame(
		f[0].renderer, f[0].model, NULL, NULL, 0));
	CHECK(mh_renderer_tga_capture(f[0].renderer, &after));
	CHECK(center_pixel(&after) == 0x7c00);
	free(after.data);
	CHECK(mh_renderer_scene_order(f[0].renderer, NULL, 0));
	CHECK(mh_renderer_scene_frame(
		f[0].renderer, f[0].model, NULL, NULL, 0));
	CHECK(mh_renderer_tga_capture(f[0].renderer, &after));
	CHECK(center_pixel(&after) == 31);
	free(after.data);
	mh_renderer_destroy(f[0].renderer);

	return 0;
}

static int
span_guards(void)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct fixture f[3];
	struct span_fixture packed;
	const struct mh_mesh *meshes[2];
	struct mh_renderer_scene_part parts[4], bad[4], saved;
	struct mh_file before, after;
	unsigned char hidden[2];
	float models[32];
	unsigned int i, j;

	for (i = 0; i < 3; i++) {
		CHECK(init_fixture(&f[i]) == 0);

		for (j = 0; j < 6; j++) {
			f[i].vertices[j].position[0] = xy[j][0];
			f[i].vertices[j].position[1] = xy[j][1];
			f[i].vertices[j].position[2] = 2;
			f[i].vertices[j].color[i] = 1;
		}
	}

	pack_spans(f, &packed);
	memset(hidden, 0, sizeof(hidden));
	packed.mesh.draw_hidden = hidden;
	meshes[0] = &packed.mesh;
	meshes[1] = &f[1].mesh;
	memcpy(models, f[0].model, sizeof(f[0].model));
	memcpy(models + 16, f[1].model, sizeof(f[1].model));
	memset(parts, 0, sizeof(parts));
	parts[0].span.draw_count = parts[0].span.primitive_count = 1;
	parts[1].mesh = parts[1].active_lod = 1;
	parts[2].span = parts[0].span;
	parts[2].span.draw_first = parts[2].span.primitive_first = 1;

	/* An empty end span contributes no geometry or capacity. */
	parts[3].span.draw_first = parts[3].span.primitive_first = 2;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 2, 320, 200,
		0, &f[0].renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_order(f[0].renderer, parts, 4));
	CHECK(check_scene_pixel(f, NULL, 0x7c00) == 0);

	/*
	 * Interleaved A/B/A spans use producer order, not backing-array order.
	 */
	saved = parts[0];
	parts[0] = parts[2];
	parts[2] = saved;
	CHECK(mh_renderer_scene_order(f[0].renderer, parts, 4));
	CHECK(check_scene_pixel(f, NULL, 31) == 0);
	CHECK(mh_renderer_tga_capture(f[0].renderer, &before));

	for (i = 0; i < 9; i++) {
		memcpy(bad, parts, sizeof(bad));

		if (i == 0)
			bad[0].span.draw_first = (size_t)-1;
		else if (i == 1)
			bad[0].span.primitive_first = (size_t)-1;
		else if (i == 2)
			bad[0].span.draw_count = (size_t)-1;
		else if (i == 3)
			bad[0].span.primitive_count = (size_t)-1;
		else if (i == 4)
			bad[2] = bad[0];
		else if (i == 5)
			bad[2].span.primitive_count = 0;
		else if (i == 6)
			bad[0].span.draw_count = 0;
		else if (i == 7)
			bad[0].span.primitive_first = 0;
		else
			bad[0].active_lod = 1;

		CHECK(!mh_renderer_scene_order(f[0].renderer, bad, 4));
	}

	CHECK(!mh_renderer_scene_order(f[0].renderer, parts, 2));
	packed.mesh.lod_count = 1;
	CHECK(!mh_renderer_scene_order(f[0].renderer, parts, 4));
	packed.mesh.lod_count = 0;
	packed.mesh.primitives = NULL;
	CHECK(!mh_renderer_scene_order(f[0].renderer, parts, 4));
	packed.mesh.primitives = packed.primitives;
	CHECK(mh_renderer_tga_capture(f[0].renderer, &after));
	CHECK(before.size == after.size &&
		memcmp(before.data, after.data, before.size) == 0);
	free(after.data);
	CHECK(check_scene_pixel(f, NULL, 31) == 0);

	/* A live primitive must still belong to its retained draw span. */
	packed.primitives[1].draw = 0;
	CHECK(mh_renderer_scene_frame_lens(f[0].renderer, &f[0].projection,
		f[0].view, NULL, 0, NULL, NULL, NULL,
		NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f[0].renderer, &after));
	CHECK(before.size == after.size &&
		memcmp(before.data, after.data, before.size) == 0);
	free(before.data);
	free(after.data);
	packed.primitives[1].draw = 1;
	CHECK(check_scene_pixel(f, NULL, 31) == 0);
	hidden[1] = 1;
	CHECK(check_scene_pixel(f, NULL, 0x03e0) == 0);
	hidden[1] = 0;
	models[12] = 10;
	CHECK(check_scene_pixel(f, models, 0x03e0) == 0);
	models[12] = 0;
	models[16 + 12] = 10;
	CHECK(check_scene_pixel(f, models, 31) == 0);
	CHECK(mh_renderer_mesh_visible(f[0].renderer, 0, 0));
	CHECK(check_scene_pixel(f, NULL, 0x03e0) == 0);
	CHECK(mh_renderer_mesh_visible(f[0].renderer, 0, 1));
	CHECK(mh_renderer_scene_order(f[0].renderer, NULL, 0));
	CHECK(check_scene_pixel(f, NULL, 0x7c00) == 0);
	mh_renderer_destroy(f[0].renderer);

	return 0;
}

static int
detail_guards(void)
{
	static const float xy[6][2] = {
		{-1, 1}, {1, 1}, {1, -1}, {-1, 1}, {1, -1}, {-1, -1}};

	struct fixture f;
	struct detail_fixture packed;
	const struct mh_mesh *mesh;
	struct mh_renderer *invalid;
	struct mh_file before, after;
	struct mh_renderer_scene_part part;
	unsigned int i;

	CHECK(init_fixture(&f) == 0);

	for (i = 0; i < 6; i++) {
		f.vertices[i].position[0] = xy[i][0];
		f.vertices[i].position[1] = xy[i][1];
		f.vertices[i].position[2] = 2;
		f.vertices[i].color[0] = 1;
	}

	pack_details(&f, &packed, 0);
	mesh = &packed.mesh;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, &mesh, 1, 320, 200,
		0, &f.renderer, NULL) == MH_ERROR_NONE);

	for (i = 0; i < MH_MESH_LODS; i++) {
		pack_details(&f, &packed, i);
		CHECK(draw_fixture(&f, &after) == 0);
		CHECK(center_pixel(&after) == 0x7c00);
		free(after.data);
	}

	CHECK(mh_renderer_tga_capture(f.renderer, &before));
	packed.mesh.lod = MH_MESH_LODS;
	CHECK(!mh_renderer_update_vertices(f.renderer));
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	packed.mesh.lod = MH_MESH_LODS - 1;

	/*
	 * Even an inactive level must not reference a different level's data.
	 */
	packed.primitives[0].draw = 1;
	invalid = NULL;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, &mesh, 1, 320, 200,
		0, &invalid, NULL) != MH_ERROR_NONE &&
		invalid == NULL);
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, NULL, NULL) != MH_ERROR_NONE);
	packed.primitives[0].draw = 0;
	packed.primitives[0].first = 6;
	CHECK(!mh_renderer_update_vertices(f.renderer));
	packed.primitives[0].first = 0;
	packed.mesh.lods[0].vertex_count = (size_t)-1;
	CHECK(!mh_renderer_update_vertices(f.renderer));
	packed.mesh.lods[0].vertex_count = 6;
	CHECK(mh_renderer_tga_capture(f.renderer, &after));
	CHECK(after.size == before.size &&
		memcmp(after.data, before.data, before.size) == 0);
	free(before.data);
	free(after.data);
	CHECK(check_scene_pixel(&f, NULL, 0x7c00) == 0);

	memset(&part, 0, sizeof(part));
	part.span.draw_count = part.span.primitive_count = MH_MESH_LODS;
	CHECK(!mh_renderer_scene_order(f.renderer, &part, 1));
	part.active_lod = 1;
	CHECK(mh_renderer_scene_order(f.renderer, &part, 1));

	/* Detail selection is live and does not need a new vertex upload. */
	packed.mesh.lod = 0;
	CHECK(check_scene_pixel(&f, NULL, 0) == 0);
	packed.mesh.lod = MH_MESH_LODS - 1;
	CHECK(check_scene_pixel(&f, NULL, 0x7c00) == 0);
	CHECK(mh_renderer_scene_order(f.renderer, NULL, 0));
	CHECK(check_scene_pixel(&f, NULL, 0x7c00) == 0);
	mh_renderer_destroy(f.renderer);

	return 0;
}

int
main(void)
{
	CHECK(cue_guards() == 0);
	CHECK(brightness_guards() == 0);
	CHECK(model_light_guards() == 0);
	CHECK(lens_guards() == 0);
	CHECK(ordering_guards() == 0);
	CHECK(submission_guards() == 0);
	CHECK(span_guards() == 0);

	return detail_guards();
}
