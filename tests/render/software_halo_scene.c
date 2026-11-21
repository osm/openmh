#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_halos.h"
#include "backends/render/software_palette.h"
#include "backends/render/software_provider.h"
#include "engine/assets/tga.h"
#include "interfaces/render/backend.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 0;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_renderer		*renderer;
	struct mh_renderer_effects	effects;
	struct mh_renderer_halos	halos;
	struct mh_renderer_halo		sources[2];
	struct mh_indexed_image		image, lens;
	unsigned char			indices[32 * 32];
	struct mh_projection		projection;
	float				view[16];
};

static int
setup_fixture(struct fixture *f, const struct mh_mesh *mesh)
{
	unsigned int i;

	memset(f, 0, sizeof(*f));
	f->view[0] = f->view[5] = f->view[15] = 1;
	f->view[10] = -1;
	memset(f->indices, 1, sizeof(f->indices));
	f->image.width = f->image.height = 32;
	f->image.indices = f->indices;
	f->image.size = sizeof(f->indices);
	f->image.colors = 2;
	memset(f->image.palette + 3, 255, 3);
	f->lens = f->image;
	f->effects.halos = &f->halos;
	f->halos.sources = f->sources;
	f->halos.count = 1;
	f->halos.lens = &f->lens;
	f->halos.cue_start = .7f;

	for (i = 0; i < 2; i++) {
		f->sources[i].flags = MH_HALO_GLOW_FLARES | MH_HALO_WORLD;
		f->sources[i].intensity = 1;
		f->sources[i].size = .5f;
		f->sources[i].flare = .65f;
		memset(f->sources[i].color, 100, 3);
		f->sources[i].image = &f->image;
		f->sources[i].endpoints[0][2] = 100;
		f->sources[i].endpoints[1][2] = 100;
	}

	CHECK(mh_projection_setup(320, 200, 1.6f, 1, 1, 1000, &f->projection,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL,
		mesh == NULL ? NULL : &mesh, mesh == NULL ? 0 : 1, 320,
		200, 0, &f->renderer, NULL) == MH_ERROR_NONE);

	return 1;
}

static int
capture_frame(struct fixture *f, int effects, struct mh_file *picture)
{
	struct mh_error error;

	if (mh_renderer_scene_frame_lens(f->renderer, &f->projection, f->view,
		NULL, 0, NULL, NULL, effects ? &f->effects : NULL,
		&error) != MH_ERROR_NONE) {
		fprintf(stderr, "%s\n", error.message);
		return 0;
	}

	return mh_renderer_tga_capture(f->renderer, picture);
}

static int
pictures_equal(const struct mh_file *a, const struct mh_file *b)
{
	return a->size == b->size && memcmp(a->data, b->data, a->size) == 0;
}

/*
 * Compose two unobstructed chains without markers as an independent schedule.
 */
static int
unobstructed(struct fixture *f, struct mh_file *picture)
{
	struct mh_software_halos prepared;
	struct mh_software_surface surface;
	struct mh_software_palette palette;
	struct mh_software_texture texture;
	struct mh_software_texture_state state;
	struct mh_software_material material;
	struct mh_software_triangle triangles[2];
	struct mh_picture image;
	unsigned int i, j, count;
	size_t n;

	memset(&prepared, 0, sizeof(prepared));
	memset(&surface, 0, sizeof(surface));
	memset(&state, 0, sizeof(state));
	memset(&material, 0, sizeof(material));
	memset(&texture, 0, sizeof(texture));
	memset(&image, 0, sizeof(image));
	surface.width = 320;
	surface.height = 200;
	surface.pitch = 640;
	surface.size = surface.pitch * surface.height;
	surface.pixels = calloc(1, surface.size);
	CHECK(surface.pixels != NULL);
	surface.bits[0] = surface.bits[2] = 5;
	surface.bits[1] = 6;
	surface.shift[0] = 11;
	surface.shift[1] = 5;
	texture.width = texture.height = 32;
	texture.size = sizeof(f->indices);
	texture.indices = f->indices;
	texture.palette = &palette;
	material.texture = &texture;
	material.mapping = MH_TEXTURE_AFFINE;
	material.perspective_shift = 2;
	CHECK(mh_software_halos_prepare(&prepared, &f->halos, &f->projection,
		f->view, NULL) == MH_ERROR_NONE);
	CHECK(prepared.count == 2 &&
		prepared.entries[0].geometry.bucket ==
			prepared.entries[1].geometry.bucket);

	for (n = prepared.count; n > 0; n--)
		for (i = 0; i < prepared.entries[n - 1].geometry.count; i++) {
			CHECK(mh_software_face_prepare(
				&prepared.entries[n - 1]
					.geometry.faces[i],
				&material, triangles, &count,
				NULL) == MH_ERROR_NONE);

			for (j = 0; j < count; j++) {
				CHECK(mh_software_palette_build(&palette,
					f->image.palette,
					sizeof(f->image.palette),
					f->image.colors, &triangles[j], 6,
					NULL) == MH_ERROR_NONE);
				CHECK(mh_software_triangle(&surface,
					&triangles[j], &state,
					NULL) == MH_ERROR_NONE);
			}
		}

	image.format = MH_PICTURE_RGB565;
	image.width = surface.width;
	image.height = surface.height;
	image.pitch = surface.pitch;
	image.pixels = surface.pixels;
	image.pixel_size = surface.size;
	CHECK(mh_image_tga_encode(&image, picture, NULL) == MH_ERROR_NONE);
	free(surface.pixels);
	mh_software_halos_free(&prepared);

	return 1;
}

static int
empty_scene(void)
{
	struct fixture f;
	struct mh_file blank, glow, lenses, both, one, retained;
	float model[16];
	unsigned int i;

	CHECK(setup_fixture(&f, NULL));
	CHECK(capture_frame(&f, 0, &blank));
	CHECK(capture_frame(&f, 1, &glow));
	CHECK(!pictures_equal(&blank, &glow));
	f.sources[0].lenses = 1;
	CHECK(capture_frame(&f, 1, &lenses));
	CHECK(!pictures_equal(&glow, &lenses));
	free(lenses.data);
	f.sources[0].lenses = 0;

	/*
	 * Same-pixel markers resolve in reverse insertion order, not all at
	 * once.
	 */
	f.halos.count = 2;
	f.sources[1].color[1] = f.sources[1].color[2] = 0;
	CHECK(capture_frame(&f, 1, &both));
	f.halos.sources = f.sources + 1;
	f.halos.count = 1;
	CHECK(capture_frame(&f, 1, &one));
	CHECK(pictures_equal(&both, &one) && !pictures_equal(&glow, &one));
	free(both.data);
	free(one.data);
	f.halos.sources = f.sources;

	/*
	 * Each glow overlaps the other marker: all restores must happen first.
	 */
	f.halos.count = 2;
	f.sources[1].endpoints[0][0] = f.sources[1].endpoints[1][0] = 2;
	CHECK(capture_frame(&f, 1, &both));
	CHECK(unobstructed(&f, &one));
	CHECK(pictures_equal(&both, &one));
	free(both.data);
	free(one.data);
	f.halos.count = 1;

	/* Native model composition and directly positioned sources agree. */
	memset(model, 0, sizeof(model));

	for (i = 0; i < 4; i++)
		model[i * 5] = 1;

	model[12] = 5;
	f.sources[0].model = model;
	CHECK(capture_frame(&f, 1, &one));
	f.sources[0].model = NULL;
	f.sources[0].endpoints[0][0] = f.sources[0].endpoints[1][0] = 5;
	CHECK(capture_frame(&f, 1, &both));
	CHECK(pictures_equal(&one, &both));
	free(one.data);

	/* Late invalid insertion preserves the published frame. */
	f.halos.count = 2;
	f.sources[1].part = 1;
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, &f.effects, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &retained));
	CHECK(pictures_equal(&both, &retained));
	free(retained.data);
	free(both.data);

	/*
	 * No halo scratch survives into results or the next no-effects race
	 * frame.
	 */
	CHECK(mh_renderer_scene_frame_ui(
		f.renderer, NULL, NULL, NULL, 0, NULL));
	CHECK(mh_renderer_tga_capture(f.renderer, &retained));
	CHECK(pictures_equal(&blank, &retained));
	free(retained.data);
	CHECK(capture_frame(&f, 0, &retained));
	CHECK(pictures_equal(&blank, &retained));
	free(retained.data);
	free(blank.data);
	free(glow.data);
	mh_renderer_destroy(f.renderer);

	return 1;
}

static void
build_wall(struct mh_mesh *mesh, struct mh_vertex vertices[6],
	struct mh_draw *draw, struct mh_mesh_primitive *primitive,
	unsigned char colors[18], float depth)
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	unsigned int i, corner;

	memset(mesh, 0, sizeof(*mesh));
	memset(vertices, 0, 6 * sizeof(*vertices));
	memset(draw, 0, sizeof(*draw));
	memset(primitive, 0, sizeof(*primitive));
	memset(colors, 32, 18);
	mesh->vertices = vertices;
	mesh->vertex_count = draw->count = primitive->count = 6;
	mesh->draws = draw;
	mesh->draw_count = 1;
	mesh->primitives = primitive;
	mesh->primitive_count = 1;
	mesh->world_colors = colors;
	mesh->world_vertex_count = 6;
	primitive->type = 1;
	draw->prelit = 1;

	for (i = 0; i < 3; i++)
		mesh->world_brightness[i] = 1;

	for (i = 0; i < 6; i++) {
		corner = corners[i];
		vertices[i].position[0] =
			depth * (corner == 0 || corner == 3 ? -.9f : .9f);
		vertices[i].position[1] = depth * (corner < 2 ? .6f : -.6f);
		vertices[i].position[2] = depth;
	}
}

static int
occlusion(void)
{
	struct fixture f;
	struct mh_mesh mesh;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	unsigned char colors[18];
	struct mh_file base, picture;
	unsigned int pass;
	float depth;

	for (pass = 0; pass < 4; pass++) {
		/* Last two share the world halo's bucket at depth 100. */
		depth = pass == 0 ? 50 : pass == 1 ? 200 : 99;
		build_wall(&mesh, vertices, &draw, &primitive, colors, depth);
		CHECK(setup_fixture(&f, &mesh));
		f.sources[0].before_primitive = pass == 3 ? 1 : 0;
		CHECK(capture_frame(&f, 0, &base));
		CHECK(capture_frame(&f, 1, &picture));
		CHECK(pictures_equal(&base, &picture) ==
			(pass == 0 || pass == 3));
		free(base.data);
		free(picture.data);
		mh_renderer_destroy(f.renderer);
	}

	return 1;
}

int
main(void)
{
	return empty_scene() && occlusion() ? 0 : 1;
}
