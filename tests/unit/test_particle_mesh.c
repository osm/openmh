#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/game/race_limits.h"
#include "engine/render/effect_particles.h"

static int failures;
static struct mh_particles states[8];
static struct mh_vertex saved[8 * 3 * 16 * 6];
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static size_t
visible_vertices(const struct mh_mesh *mesh)
{
	size_t i, count;

	count = 0;

	for (i = 0; i < mesh->vertex_count; i++)
		if (mesh->vertices[i].color[0] > 0)
			count++;

	return count;
}

static void
check_patched_fade(void)
{
	struct mh_particle particle;
	struct mh_particle_projection projection;
	struct mh_particle_quad quad;
	const float uv[8] = {0, 0, 1, 0, 1, 1, 0, 1};

	memset(&particle, 0, sizeof(particle));
	memset(&projection, 0, sizeof(projection));
	particle.flags = MH_PARTICLE_FADE;
	particle.lifespan = particle.remaining = 1;
	particle.width = particle.height = .01f;
	particle.color[0] = .7f;
	projection.center[2] = .0001f;
	projection.center[3] = .1f;
	projection.near_depth = .15f;
	projection.far_depth = 3000;
	projection.inverse_far = 1.0f / 3000;
	projection.focal = 1.1547f;
	projection.half_width = 480;
	projection.pixel_aspect = projection.brightness = 1;
	CHECK(mh_particle_quad(&particle, &projection, uv, &quad, NULL) ==
		MH_ERROR_NONE);

	/*
	 * Patched binary32 result; the disc's earlier store
	 * yields 46.66200256.
	 */
	CHECK(quad.color[0] == 46.66199493408203125f);
}

int
main(void)
{
	struct mh_mesh mesh;
	struct mh_particle_camera camera;
	struct mh_particle *particle;
	struct mh_particle_projection projection;
	struct mh_particle_quad quad, saved_quad;
	float uv[8], alias[8];
	unsigned int car, pool, slot, i;

	check_patched_fade();
	memset(&mesh, 0, sizeof(mesh));
	memset(&camera, 0, sizeof(camera));
	CHECK(mh_particle_mesh_create(0, &mesh, NULL) != MH_ERROR_NONE &&
		mesh.vertices == NULL);
	CHECK(mh_particle_mesh_create(MH_RACE_MAX_CARS + 1, &mesh, NULL) !=
			MH_ERROR_NONE &&
		mesh.vertices == NULL);
	CHECK(mh_particle_mesh_create(8, &mesh, NULL) == MH_ERROR_NONE);
	CHECK(mesh.vertex_count == 2304 && mesh.draw_count == 1 &&
		mesh.draws[0].prelit);
	CHECK(mesh.textures[0].width == 256 && mesh.textures[0].rgba[3] == 255);
	camera.near_depth = 1;
	camera.far_depth = 100;
	camera.width = 640;
	camera.height = 480;
	camera.brightness = 1;
	camera.smoke = camera.sparks = 1;
	camera.matrix[0] = camera.matrix[5] = camera.matrix[11] = 1;
	camera.matrix[10] = 100.0f / 99;
	camera.matrix[14] = -100.0f / 99;

	for (car = 0; car < 8; car++)
		for (pool = 0; pool < 3; pool++) {
			struct mh_particle_pool *pool_state;

			pool_state = &states[car].pools[pool];
			pool_state->enabled = 1;
			pool_state->count = 16;
			pool_state->first = 5;

			for (slot = 0; slot < 16; slot++) {
				particle = &pool_state->particles[slot];
				particle->active = 1;
				particle->flags = 0x43;
				particle->lifespan = particle->remaining = 1;
				particle->width = particle->height = .2f;
				particle->position[2] = 10;
				particle->color[0] = particle->color[1] =
					particle->color[2] = 1;
				particle->style = pool == 2 ? 3 : 4;
				particle->animation = pool == 2 ? 4 : 1;
			}
		}

	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
			MH_ERROR_NONE &&
		visible_vertices(&mesh) == 2304);

	/*
	 * Reverse batch order puts the last spark first. RGB truncates, like
	 * Glide.
	 */
	CHECK(mesh.vertices[0].position[0] == 0 &&
		mesh.vertices[0].uv[0] == 65.0f / 256);
	CHECK(mesh.vertices[0].color[0] == 66.0f / 255);
	CHECK(mesh.vertices[2303].color[0] == 49.0f / 255);
	mesh.material_rgb = calloc(mesh.vertex_count * 3, sizeof(float));
	CHECK(mesh.material_rgb != NULL);

	if (mesh.material_rgb == NULL) {
		mh_mesh_free(&mesh);
		return 1;
	}

	mesh.material_rgb_count = mesh.vertex_count;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
		MH_ERROR_NONE);
	CHECK(mesh.material_rgb[0] > 66 && mesh.material_rgb[0] < 67);
	CHECK(mesh.vertices[0].projection_w == 10);
	CHECK(mesh.vertices[0].color[0] == 66.0f / 255);

	camera.smoke = 0;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
			MH_ERROR_NONE &&
		visible_vertices(&mesh) == 768);
	camera.smoke = 1;
	camera.sparks = 0;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
			MH_ERROR_NONE &&
		visible_vertices(&mesh) == 1536);
	camera.smoke = 0;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
			MH_ERROR_NONE &&
		visible_vertices(&mesh) == 0);
	camera.smoke = camera.sparks = 1;
	memset(states, 0, sizeof(states));
	states[0].pools[0].enabled = states[0].pools[0].count = 1;
	particle = &states[0].pools[0].particles[0];
	particle->active = 1;
	particle->flags = 3;
	particle->lifespan = particle->remaining = 1;
	particle->width = particle->height = 1;
	particle->position[2] = 10;
	particle->color[0] = 1;
	particle->animation = 1;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
			MH_ERROR_NONE &&
		visible_vertices(&mesh) == 6);
	CHECK(mesh.vertices[0].position[2] > .89f &&
		mesh.vertices[0].position[2] < .9f);

	/*
	 * Billboards share the world projection's vertical scale at every
	 * aspect.
	 */
	CHECK(fabs(mesh.vertices[0].position[0] + .1) < 1e-6);
	CHECK(fabs(mesh.vertices[0].position[1] - .1) < 1e-6);
	camera.matrix[5] = 4.0f / 3;
	camera.width = 1920;
	camera.height = 1080;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
		MH_ERROR_NONE);
	CHECK(fabs(mesh.vertices[0].position[0] + .1) < 1e-6);
	CHECK(fabs(mesh.vertices[0].position[1] - .1 * 4 / 3) < 1e-6);
	camera.matrix[5] = 1;
	camera.width = 640;
	camera.height = 480;

	/* Center clips although the quad crosses the edge. */
	particle->position[0] = 10.01f;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
			MH_ERROR_NONE &&
		visible_vertices(&mesh) == 0);
	particle->position[0] = 0;
	particle->position[2] = .9f;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
			MH_ERROR_NONE &&
		visible_vertices(&mesh) == 0);
	particle->position[2] = 101;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
			MH_ERROR_NONE &&
		visible_vertices(&mesh) == 0);
	particle->position[2] = 10;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) ==
		MH_ERROR_NONE);
	memcpy(saved, mesh.vertices, sizeof(saved));
	particle->frame = 9;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) !=
			MH_ERROR_NONE &&
		memcmp(saved, mesh.vertices, sizeof(saved)) == 0);
	particle->frame = 0;
	states[0].pools[0].first = 16;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) !=
			MH_ERROR_NONE &&
		memcmp(saved, mesh.vertices, sizeof(saved)) == 0);
	states[0].pools[0].first = 0;
	camera.near_depth = 100;
	CHECK(mh_particle_mesh_update(&mesh, states, 8, &camera, NULL) !=
			MH_ERROR_NONE &&
		memcmp(saved, mesh.vertices, sizeof(saved)) == 0);

	for (i = 0; i < 9; i++) {
		CHECK(mh_particle_frame_uv(2, i, uv, NULL) == MH_ERROR_NONE &&
			mh_particle_frame_uv(3, i, alias, NULL) ==
				MH_ERROR_NONE);
		CHECK(memcmp(uv, alias, sizeof(uv)) == 0);
	}

	CHECK(mh_particle_frame_uv(4, 3, uv, NULL) == MH_ERROR_NONE &&
		uv[0] == 1.0f / 256 && uv[1] == 97.0f / 256);
	memcpy(alias, uv, sizeof(uv));
	CHECK(mh_particle_frame_uv(4, 6, uv, NULL) != MH_ERROR_NONE &&
		memcmp(uv, alias, sizeof(uv)) == 0);
	CHECK(mh_particle_frame_uv(0, 0, uv, NULL) != MH_ERROR_NONE &&
		memcmp(uv, alias, sizeof(uv)) == 0);
	memset(&projection, 0, sizeof(projection));
	memset(&quad, 0xa5, sizeof(quad));
	saved_quad = quad;
	CHECK(mh_particle_quad(particle, &projection, uv, &quad, NULL) !=
			MH_ERROR_NONE &&
		memcmp(&quad, &saved_quad, sizeof(quad)) == 0);
	mh_mesh_free(&mesh);

	return failures != 0;
}
