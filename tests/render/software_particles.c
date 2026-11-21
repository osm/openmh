#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/software_particles.h"
#include "backends/render/software_provider.h"
#include "engine/assets/tga.h"
#include "interfaces/render/backend.h"

#define CHECK(c)                                                               \
	do {                                                                   \
		if (!(c)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #c);        \
			return 1;                                              \
		}                                                              \
	} while (0)

static int
check_scene(void)
{
	struct mh_software_particle_assets assets;
	struct mh_particles states, saved;
	struct mh_renderer_particles particles;
	struct mh_renderer_effects effects;
	struct mh_projection projection;
	struct mh_frustum frustum;
	struct mh_software_particles prepared;
	struct mh_renderer *renderer;
	struct mh_file base, picture, retained;
	struct mh_mesh mesh;
	struct mh_vertex vertices[6];
	struct mh_draw draw;
	struct mh_mesh_primitive primitive;
	const struct mh_mesh *meshes[1];
	unsigned char colors[18];
	static const unsigned int corners[6] = {0, 1, 2, 0, 2, 3};
	float depth;
	unsigned int pass, corner;
	struct mh_particle *particle;
	struct mh_indexed_image *image;
	unsigned char indices[64 * 64];
	float camera[12], view[16];
	unsigned int i;

	memset(&assets, 0, sizeof(assets));
	memset(&states, 0, sizeof(states));
	memset(&particles, 0, sizeof(particles));
	memset(&effects, 0, sizeof(effects));
	memset(&frustum, 0, sizeof(frustum));
	memset(&prepared, 0, sizeof(prepared));
	memset(camera, 0, sizeof(camera));
	memset(view, 0, sizeof(view));
	memset(indices, 1, sizeof(indices));
	camera[0] = camera[4] = camera[8] = 1;
	view[0] = view[5] = view[15] = 1;
	view[10] = -1;
	CHECK(mh_projection_setup(320, 200, 1.6f, 1, 1, 1000, &projection,
		NULL) == MH_ERROR_NONE);
	assets.animations[0].count = 1;
	image = &assets.animations[0].frames[0];
	image->width = image->height = 64;
	image->size = sizeof(indices);
	image->indices = indices;
	image->colors = 2;
	image->palette[3] = 255;
	image->palette[4] = 64;
	states.pools[0].enabled = states.pools[0].count = 1;
	particle = &states.pools[0].particles[0];
	particle->flags = 0x43;
	particle->animation = MH_PARTICLE_SMOKE;
	particle->active = 1;
	particle->position[2] = 10;
	particle->lifespan = particle->remaining = 1;
	particle->width = particle->height = 1;

	for (i = 0; i < 3; i++)
		particle->color[i] = 1;

	/* Visit physical slots, including an active slot outside count. */
	states.pools[0].particles[5] = *particle;
	states.pools[0].particles[5].style = MH_PARTICLE_STYLE_SPARK;
	CHECK(mh_software_particles_prepare(&prepared, &states, 1, &assets,
		&projection, camera, &frustum, 1, 1, 0,
		NULL) == MH_ERROR_NONE);
	CHECK(prepared.count == 1);
	CHECK(mh_software_particles_prepare(&prepared, &states, 1, &assets,
		&projection, camera, &frustum, 1, 1, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(prepared.count == 2);
	states.pools[0].particles[5].position[0] = 1000;
	CHECK(mh_software_particles_prepare(&prepared, &states, 1, &assets,
		&projection, camera, &frustum, 1, 1, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(prepared.count == 1);

	/* The newest-particle sphere can reject the whole enabled pool. */
	frustum.planes[0][3] = 21;
	CHECK(mh_software_particles_prepare(&prepared, &states, 1, &assets,
		&projection, camera, &frustum, 1, 1, 1,
		NULL) == MH_ERROR_NONE);
	CHECK(prepared.count == 0);
	frustum.planes[0][3] = 0;
	mh_software_particles_free(&prepared);
	particles.states = &states;
	particles.assets = &assets;
	particles.frustum = &frustum;
	particles.cars = particles.smoke = particles.sparks = 1;
	particles.brightness = 1;
	effects.particles = &particles;
	saved = states;
	CHECK(mh_renderer_open(mh_software_backend(), NULL, NULL, 0, 320, 200,
		0, &renderer, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, NULL, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(renderer, &base));
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, &effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(renderer, &picture));
	CHECK(base.size == picture.size &&
		memcmp(base.data, picture.data, base.size) != 0);
	CHECK(memcmp(&saved, &states, sizeof(states)) == 0);

	/* A missing selected frame leaves the published image intact. */
	particle->frame = 1;
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, &effects, NULL) != MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(renderer, &retained));
	CHECK(picture.size == retained.size &&
		memcmp(picture.data, retained.data, picture.size) == 0);
	free(retained.data);
	particle->frame = 0;
	particles.smoke = particles.sparks = 0;
	CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view, NULL, 0,
		NULL, NULL, &effects, NULL) == MH_ERROR_NONE);
	CHECK(mh_renderer_tga_capture(renderer, &retained));
	CHECK(base.size == retained.size &&
		memcmp(base.data, retained.data, base.size) == 0);
	free(base.data);
	free(picture.data);
	free(retained.data);
	mh_renderer_destroy(renderer);

	/* Mixed scenes must occlude particles behind an opaque road face. */
	memset(&mesh, 0, sizeof(mesh));
	memset(vertices, 0, sizeof(vertices));
	memset(&draw, 0, sizeof(draw));
	memset(&primitive, 0, sizeof(primitive));
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
	meshes[0] = &mesh;
	particles.smoke = 1;

	for (i = 0; i < 3; i++)
		mesh.world_brightness[i] = 1;

	for (i = 0; i < 18; i++)
		colors[i] = (unsigned char)(20 + i % 3 * 40);

	for (pass = 0; pass < 2; pass++) {
		depth = pass == 0 ? 2 : 100;

		for (i = 0; i < 6; i++) {
			corner = corners[i];
			vertices[i].position[0] = depth *
				(corner == 0 || corner == 3 ? -.9f : .9f);
			vertices[i].position[1] =
				depth * (corner < 2 ? .6f : -.6f);
			vertices[i].position[2] = depth;
		}

		CHECK(mh_renderer_open(mh_software_backend(), NULL, meshes, 1,
			320, 200, 0, &renderer, NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view,
			NULL, 0, NULL, NULL, NULL,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(renderer, &base));
		CHECK(mh_renderer_scene_frame_lens(renderer, &projection, view,
			NULL, 0, NULL, NULL, &effects,
			NULL) == MH_ERROR_NONE);
		CHECK(mh_renderer_tga_capture(renderer, &picture));
		CHECK(base.size == picture.size);
		CHECK((memcmp(base.data, picture.data, base.size) == 0) ==
			(pass == 0));
		free(base.data);
		free(picture.data);
		mh_renderer_destroy(renderer);
	}

	return 0;
}

int
main(void)
{
	return check_scene();
}
