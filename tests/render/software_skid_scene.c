#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_provider.h"
#include "engine/core/binary.h"
#include "interfaces/render/backend.h"

/* Segment, lighting, fog and background depth; mode follows separately. */

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

struct fixture {
	struct mh_renderer		*renderer;
	struct mh_mesh			mesh;
	struct mh_vertex		vertices[6];
	struct mh_mesh_primitive	primitive;
	struct mh_draw			draw;
	unsigned char			colors[18];
	struct mh_projection		projection;
	struct mh_world_view		world_view;
	struct mh_model_lighting	lighting;
	struct mh_point_light		light;
	struct mh_depth_cue		cue;
	struct mh_skids			history;
	struct mh_renderer_skids	skids;
	struct mh_renderer_effects	effects;
	float				view[16];
	int				feedback[1][4];
};

static void
set_background_depth(struct fixture *f, float depth)
{
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	unsigned int i, corner;

	for (i = 0; i < 6; i++) {
		corner = corners[i];
		f->vertices[i].position[0] = (float)((double)depth *
			(corner == 0 || corner == 3 ? -.9 : .9));
		f->vertices[i].position[1] =
			(float)((double)depth * (corner < 2 ? .6 : -.6));
		f->vertices[i].position[2] = depth;
	}
}

static int
setup_fixture(struct fixture *f)
{
	const struct mh_mesh *meshes[1];
	unsigned int i;

	memset(f, 0, sizeof(*f));
	f->effects.skids = &f->skids;
	f->mesh.vertices = f->vertices;
	f->mesh.vertex_count = 6;
	f->mesh.draws = &f->draw;
	f->mesh.draw_count = 1;
	f->mesh.primitives = &f->primitive;
	f->mesh.primitive_count = 1;
	f->mesh.world_colors = f->colors;
	f->mesh.world_vertex_count = 6;
	f->primitive.type = 1;
	f->primitive.count = 6;
	f->draw.count = 6;
	f->draw.prelit = 1;

	for (i = 0; i < 18; i++)
		f->colors[i] = (unsigned char)(30 + 20 * (i % 3));

	for (i = 0; i < 3; i++) {
		f->mesh.world_brightness[i] = 1;
		f->lighting.brightness[i] = 1;
		f->lighting.ambient[i] = .4f;
		f->cue.color[i] = (float)(80 + 10 * i);
	}

	for (i = 0; i < 16; i += 5)
		f->view[i] = 1;

	f->view[10] = -1;
	f->world_view.profile = MH_RENDER_SOFTWARE;
	f->world_view.bounds[0] = f->world_view.bounds[2] = -1000000;
	f->world_view.bounds[1] = f->world_view.bounds[3] = 1000000;
	f->skids.histories = &f->history;
	f->skids.cars = 1;
	f->skids.view = &f->world_view;
	f->skids.lighting = &f->lighting;
	f->skids.oldest_visible = f->feedback;
	f->history.wheels[0].count = 1;
	set_background_depth(f, 20);
	meshes[0] = &f->mesh;
	CHECK(mh_projection_setup(320, 200, 1.3333333333333335f, 1, 1, 1000,
		&f->projection, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1, 320, 200,
		0, &f->renderer, NULL) == MH_ERROR_NONE);

	return 0;
}

static int
guards(void)
{
	struct fixture f;
	struct mh_skid_segment *segment;
	struct mh_skids saved;
	struct mh_file base, picture, retained;
	unsigned int i;

	CHECK(setup_fixture(&f) == 0);
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &base));
	segment = &f.history.wheels[0].segments[0];
	segment->start[0] = -.3f;
	segment->end[0] = .3f;
	segment->start[1] = segment->end[1] = -1;
	segment->start[2] = 10;
	segment->end[2] = 14;
	segment->normal[1] = 1;
	segment->color[0] = 80;
	segment->color[1] = 60;
	segment->color[2] = 20;
	saved = f.history;
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, &f.effects, NULL) == MH_ERROR_NONE);
	CHECK(memcmp(&saved, &f.history, sizeof(saved)) == 0);
	CHECK(f.feedback[0][0] == 1 && f.feedback[0][1] == 0);
	CHECK(mh_renderer_tga_capture(f.renderer, &picture));
	CHECK(base.size == picture.size &&
		memcmp(base.data, picture.data, base.size) != 0);

	for (i = 0; i < 4; i++)
		f.feedback[0][i] = 9;

	segment->normal[0] = FLT_MAX;
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, &f.effects, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &retained));
	CHECK(picture.size == retained.size &&
		memcmp(picture.data, retained.data, picture.size) == 0);
	free(retained.data);
	segment->normal[0] = 0;

	/*
	 * Skids prepare first, but a later ordinary-mesh failure cannot publish
	 * them.
	 */
	f.mesh.vertex_count = 0;
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, &f.effects, NULL) != MH_ERROR_NONE);
	f.mesh.vertex_count = 6;
	CHECK(mh_renderer_tga_capture(f.renderer, &retained));
	CHECK(picture.size == retained.size &&
		memcmp(picture.data, retained.data, picture.size) == 0);
	free(retained.data);

	for (i = 0; i < 4; i++)
		CHECK(f.feedback[0][i] == 9);

	/*
	 * Disabling the effect restores the base picture and retains feedback.
	 */
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &retained));
	CHECK(base.size == retained.size &&
		memcmp(base.data, retained.data, base.size) == 0);
	CHECK(f.feedback[0][0] == 9);
	free(retained.data);
	free(picture.data);
	free(base.data);
	mh_renderer_destroy(f.renderer);

	/* A mesh-free scene must grow its initially empty ordering scratch. */
	CHECK(mh_renderer_open(mh_software_backend(), NULL, NULL, 0, 320, 200,
		0, &f.renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, &f.effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &picture));

	for (i = 1; i < MH_SKID_SEGMENTS; i++)
		f.history.wheels[0].segments[i] = *segment;

	f.history.wheels[0].count = MH_SKID_SEGMENTS;
	f.history.wheels[0].partial = 1;
	CHECK(mh_renderer_scene_frame_lens(f.renderer, &f.projection, f.view,
		NULL, 0, NULL, NULL, &f.effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(f.renderer, &retained));
	CHECK(picture.size == retained.size &&
		memcmp(picture.data, retained.data, picture.size) == 0);
	free(retained.data);
	free(picture.data);
	mh_renderer_destroy(f.renderer);

	return 0;
}

int
main(void)
{
	return guards();
}
