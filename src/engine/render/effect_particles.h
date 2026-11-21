#ifndef MH_PARTICLE_MESH_H
#define MH_PARTICLE_MESH_H

#include "engine/core/error.h"
#include "engine/render/mesh.h"
#include "engine/simulation/particles.h"

#define MH_PARTICLE_ATLAS_SIZE 256

struct mh_particle_projection {
	float	center[4]; /* Pixel X/Y, view Z/far, reciprocal view Z. */
	float	near_depth, far_depth, focal, half_width, pixel_aspect;
	float	inverse_far, brightness;
};

struct mh_particle_quad {
	float	position[4][3], uv[4][2], color[3], reciprocal_depth;
};

struct mh_particle_camera {
	/* Native world-to-clip, standard [0,w] depth. */
	float		matrix[16];
	float		near_depth, far_depth, brightness;
	unsigned int	width, height;
	int		smoke, sparks;
};

/*
 * Prepare particles after visibility/projection. Pixel coordinates are top-left
 * based; depth is the reversed near/Z value. UV is four atlas corners. No
 * GPU-specific camera assumptions here. Output unchanged on invalid bounds;
 * caller handles active/visibility gates.
 */
enum mh_error_code	mh_particle_quad(const struct mh_particle *particle,
	const struct mh_particle_projection *projection, const float uv[8],
	struct mh_particle_quad *output, struct mh_error *error_out);

/*
 * Original 256x256 atlas packing: nine 32px smoke, nine 32px grass and
 * six 64px sparks, with one-pixel UV inset. Gravel aliases grass.
 */
enum mh_error_code	mh_particle_frame_uv(unsigned int animation,
	unsigned int frame, float uv[8], struct mh_error *error_out);

/*
 * Fixed capacity up to MH_RACE_MAX_CARS, one prelit draw and owned black
 * 256x256 atlas. Loader fills the atlas before GPU upload; free with
 * mh_mesh_free.
 */
enum mh_error_code	mh_particle_mesh_create(unsigned int cars,
	struct mh_mesh *output, struct mh_error *error_out);

/*
 * Projects with the native camera, then uses original quad math. Vertices
 * contain clip-normalized positions and require an identity GPU transform.
 * An optional material_rgb buffer receives unquantized colors; in that mode
 * vertices also retain the original reciprocal depth through projection_w.
 * Active pools must come from the particle core. No per-frame allocation.
 * Failures preserve vertex and material_rgb contents.
 */
enum mh_error_code	mh_particle_mesh_update(struct mh_mesh *mesh,
	const struct mh_particles *states, unsigned int cars,
	const struct mh_particle_camera *camera, struct mh_error *error_out);

#endif
