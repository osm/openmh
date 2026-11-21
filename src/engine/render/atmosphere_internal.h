#ifndef MH_ATMOSPHERE_INTERNAL_H
#define MH_ATMOSPHERE_INTERNAL_H

#include "engine/core/error.h"
#include "engine/render/halo.h"
#include "engine/render/mesh.h"
#include "engine/render/sky.h"

enum {
	QUAD_VERTICES = 6,
	SKY_QUADS = MH_SKY_QUADS,
	SKY_VERTICES = SKY_QUADS * QUAD_VERTICES,
	SKY_TEXTURES = 2,
	WEATHER_QUADS = 2,
	GLOW_QUADS = MH_WORLD_GLOW_QUADS,
	FLARE_QUADS = MH_LENS_QUADS,
	WORLD_LAMP_QUADS = MH_WORLD_LAMP_QUADS,
	ATTACHED_LAMP_QUADS = MH_ATTACHED_LAMP_QUADS,
	LAMP_VERTICES = WORLD_LAMP_QUADS * QUAD_VERTICES,
	GLOW_VERTICES = GLOW_QUADS * QUAD_VERTICES,
	FLARE_VERTICES = FLARE_QUADS * QUAD_VERTICES,
	LAMP_TEXTURES = 2,
	GLOW_TEXTURE_SIZE = 128,
	FLARE_TEXTURE_SIZE = 64,
	VIEWPORT_LIMIT = 20000
};

#define MATRIX_LIMIT 1e7f
#define FAR_DEPTH_LIMIT 1e6
#define FOCAL_LIMIT 100
#define SKY_DEPTH .9999f
#define SKY_TEXTURE_HEIGHT_LIMIT 4096
#define ATTACHED_MIN_FOCAL .01
#define ENDPOINT_LIMIT 1e6

/* Callers bound nonzero counts and supply fresh, unowned mesh storage. */
enum mh_error_code	mh_atmosphere_mesh_allocate(size_t vertex_count,
	size_t draw_count, size_t texture_count, struct mh_mesh *mesh,
	struct mh_error *error_out);
void			mh_atmosphere_quad_vertices(struct mh_vertex *output,
	const struct mh_effect_quad *quad, unsigned int width,
	unsigned int height, float depth, float uv_scale);
void			mh_atmosphere_powervr_quad(struct mh_vertex *output,
	float *colors, const struct mh_effect_quad *quad, unsigned int width,
	unsigned int height);
int			mh_atmosphere_camera_valid(const float matrix[16],
	unsigned int width, unsigned int height);

#endif
