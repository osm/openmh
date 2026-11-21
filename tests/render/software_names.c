#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_name_geometry.h"
#include "backends/render/software_provider.h"
#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/font.h"
#include "engine/assets/name_images.h"
#include "interfaces/render/backend.h"

/* Projection, view, world/camera position, size and ordering band. */

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "software names line %d: %s\n",        \
				__LINE__, #c);                                 \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
geometry(void)
{
	static const float view[12] = {1, 0, 0, 0, 1, 0, 0, 0, 1, 0, 0, 0};
	struct mh_software_name_source source;
	struct mh_software_name_geometry flat, faded, saved;
	struct mh_software_depth_band band;
	struct mh_projection projection;
	struct mh_indexed_image image;

	memset(&source, 0, sizeof(source));
	memset(&image, 0, sizeof(image));
	image.width = 60;
	image.height = 20;
	source.image = &image;
	source.height = 2;
	source.size[0] = source.size[1] = .8f;
	source.world[2] = source.camera[2] = 35;
	band.offset = .05f;
	band.scale = .95f;
	CHECK(mh_projection_setup(640, 400, 1.6f, 1, .5f, 300, &projection,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_software_name_project(&source, &projection, view, &band, &flat,
		NULL) == MH_ERROR_NONE);
	CHECK(flat.face.count == 4 && flat.face.flags == 0x403);
	CHECK(flat.face.corners[0].rgb[0] == 80);
	CHECK(flat.face.corners[1].xy[0] - flat.face.corners[0].xy[0] == 60);
	source.transparent = 1;
	CHECK(mh_software_name_project(&source, &projection, view, &band,
		&faded, NULL) == MH_ERROR_NONE);
	CHECK(faded.face.flags == 0x43 && faded.face.corners[0].rgb[0] == 40);
	CHECK(faded.bucket == flat.bucket);
	saved = faded;
	image.width = 0;
	CHECK(mh_software_name_project(&source, &projection, view, &band,
		&faded, NULL) != MH_ERROR_NONE);
	CHECK(memcmp(&faded, &saved, sizeof(saved)) == 0);
	image.width = 60;
	source.world[2] = 51;
	CHECK(mh_software_name_project(&source, &projection, view, &band,
		&faded, NULL) == MH_ERROR_NONE &&
		faded.face.count == 0);
	source.world[2] = .5f;
	CHECK(mh_software_name_project(&source, &projection, view, &band,
		&faded, NULL) == MH_ERROR_NONE &&
		faded.face.count == 0);

	return 0;
}

static int
capture_frame(struct mh_renderer *renderer,
	const struct mh_projection *projection, const float view[16],
	const struct mh_renderer_effects *effects, struct mh_file *picture)
{
	CHECK(mh_renderer_scene_frame_lens(renderer, projection, view, NULL, 0,
		NULL, NULL, effects, NULL) == MH_ERROR_NONE);

	CHECK(mh_renderer_tga_capture(renderer, picture));

	return 0;
}

static int
pictures_equal(const struct mh_file *a, const struct mh_file *b)
{
	return a->size == b->size && memcmp(a->data, b->data, a->size) == 0;
}

static int
scene(void)
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	struct mh_renderer *renderer;
	struct mh_mesh mesh;
	const struct mh_mesh *input;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	struct mh_renderer_name sources[2];
	struct mh_renderer_names names;
	struct mh_renderer_effects effects;
	struct mh_indexed_image images[2];
	struct mh_projection projection;
	struct mh_file base, flat, transparent, both, retained;
	unsigned char colors[18], indices[32 * 16];
	float view[16], model[16], depth;
	unsigned int i, pass, corner;

	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&draw, 0, sizeof(draw));
	memset(&primitive, 0, sizeof(primitive));
	memset(&effects, 0, sizeof(effects));
	memset(&names, 0, sizeof(names));
	memset(sources, 0, sizeof(sources));
	memset(images, 0, sizeof(images));
	memset(view, 0, sizeof(view));
	memset(model, 0, sizeof(model));
	memset(colors, 32, sizeof(colors));

	for (i = 0; i < sizeof(indices); i++)
		indices[i] = i % 32 >= 16;

	for (i = 0; i < 4; i++)
		view[i * 5] = model[i * 5] = 1;

	view[10] = -1;
	model[14] = 35;
	mesh.vertices = vertices;
	mesh.vertex_count = draw.count = primitive.count = 6;
	mesh.draws = &draw;
	mesh.draw_count = 1;
	mesh.primitives = &primitive;
	mesh.primitive_count = 1;
	mesh.world_colors = colors;
	mesh.world_vertex_count = 6;
	primitive.type = 1;
	draw.prelit = 1;

	for (i = 0; i < 3; i++)
		mesh.world_brightness[i] = 1;

	for (i = 0; i < 2; i++) {
		images[i].width = 32;
		images[i].height = 16;
		images[i].size = sizeof(indices);
		images[i].indices = indices;
		images[i].colors = 2;
		images[i].palette[3 + i] = 255;
		sources[i].image = &images[i];
		sources[i].model = model;
		sources[i].height = 2;
		sources[i].size[0] = sources[i].size[1] = .8f;
	}

	names.sources = sources;
	effects.names = &names;
	CHECK(mh_projection_setup(320, 200, 1.6f, 1, .5f, 300, &projection,
		NULL) == MH_ERROR_NONE);
	input = &mesh;

	for (pass = 0; pass < 2; pass++) {
		depth = pass == 0 ? 5 : 60;

		for (i = 0; i < 6; i++) {
			corner = corners[i];
			vertices[i].position[0] = depth *
				(corner == 0 || corner == 3 ? -.9f : .9f);
			vertices[i].position[1] =
				depth * (corner < 2 ? .6f : -.6f);
			vertices[i].position[2] = depth;
		}

		CHECK(mh_renderer_open(mh_software_backend(), NULL, &input, 1,
			320, 200, 0, &renderer, NULL) == MH_ERROR_NONE);
		CHECK(capture_frame(renderer, &projection, view, NULL, &base) ==
			0);
		names.count = 1;
		names.transparent = 0;
		CHECK(capture_frame(renderer, &projection, view, &effects,
			&flat) == 0);
		names.transparent = 1;
		CHECK(capture_frame(renderer, &projection, view, &effects,
			&transparent) == 0);
		CHECK(pictures_equal(&base, &flat) == (pass == 0));
		CHECK(pictures_equal(&base, &transparent) == (pass == 0));
		CHECK(pictures_equal(&flat, &transparent) == (pass == 0));
		free(transparent.data);

		/* Equal-bucket labels draw in reverse source order. */
		names.transparent = 0;
		names.count = 2;
		CHECK(capture_frame(renderer, &projection, view, &effects,
			&both) == 0);
		CHECK(pictures_equal(&flat, &both));
		free(both.data);
		sources[1].part = 1;
		CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view,
			NULL, 0, NULL, NULL, &effects,
			NULL) != MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(renderer, &retained));
		CHECK(pictures_equal(&flat, &retained));
		free(retained.data);
		sources[1].part = 0;
		CHECK(capture_frame(renderer, &projection, view, NULL,
			&retained) == 0);
		CHECK(pictures_equal(&base, &retained));
		free(retained.data);
		free(base.data);
		CHECK(mh_renderer_mesh_visible(renderer, 0, 0));
		CHECK(capture_frame(renderer, &projection, view, NULL, &base) ==
			0);
		CHECK(capture_frame(renderer, &projection, view, &effects,
			&retained) == 0);
		CHECK(pictures_equal(&base, &retained));
		free(retained.data);
		free(base.data);
		free(flat.data);
		mh_renderer_destroy(renderer);
	}

	return 0;
}

int
main(void)
{

	CHECK(geometry() == 0);
	CHECK(scene() == 0);

	return 0;
}
