#ifndef MH_SOFTWARE_PARTICLES_H
#define MH_SOFTWARE_PARTICLES_H

#include "backends/render/software_face.h"
#include "backends/render/software_projection.h"
#include "engine/game/race_limits.h"
#include "engine/render/frustum.h"
#include "engine/render/software_scene_particle_assets.h"

#define MH_SOFTWARE_PARTICLE_CARS MH_RACE_MAX_CARS

struct mh_software_particle_face {
	struct mh_software_clipped_face	face;
	const struct mh_indexed_image	*image;
	unsigned int			bucket;
};

/* Reusable scratch, initialized to zero. Frame images remain borrowed. */
struct mh_software_particles {
	struct mh_software_particle_face	*faces;
	size_t					count, capacity;
};

/*
 * Prepare particles after a visible center has been projected. The output
 * borrows image; errors preserve the previous output.
 */
enum mh_error_code	mh_software_particle_quad(
	const struct mh_particle *particle,
	const struct mh_projection *projection,
	const struct mh_software_point *center, float brightness,
	const struct mh_indexed_image *image,
	struct mh_software_particle_face *output, struct mh_error *error_out);

/*
 * Original pool/slot order and radius-20/radius-2 visibility, followed by
 * center-point viewport rejection. All inputs are borrowed for the frame.
 * Failure clears count and retains reusable allocations; simulation is
 * untouched.
 */
enum mh_error_code	mh_software_particles_prepare(
	struct mh_software_particles *output, const struct mh_particles *states,
	unsigned int cars, const struct mh_software_particle_assets *assets,
	const struct mh_projection *projection, const float camera[12],
	const struct mh_frustum *frustum, float brightness, int smoke,
	int sparks, struct mh_error *error_out);
void			mh_software_particles_free(
	struct mh_software_particles *particles);

#endif
