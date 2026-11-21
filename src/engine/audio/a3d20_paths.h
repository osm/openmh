#ifndef MH_A3D20_PATHS_H
#define MH_A3D20_PATHS_H

#include "engine/audio/a3d20_geometry.h"
#include "engine/audio/spatial_pcm.h"

/* Native bounded first-order solver, not Aureal Wavetracing emulation. */
#define MH_A3D20_REFLECTIONS 4
#define MH_A3D20_DELAY_MAX .3

struct mh_a3d20_reflection {
	double	delay, gain, pan;
	double	point[3];
};

struct mh_a3d20_paths {
	double	transmission, transmission_hf, reflection_hf;
	unsigned int			count;
	struct mh_a3d20_reflection	reflections[MH_A3D20_REFLECTIONS];
};

/*
 * Validated collected game-coordinate triangles and prepared positional source.
 * No allocation. Output unchanged on error. Repeated intersections at a shared
 * edge count once; reflection legs must be unobstructed. Gain is relative to
 * the unoccluded direct signal. Delay is excess path travel time, scaled by the
 * material setting. The strongest four unique paths survive. Equal gains retain
 * existing paths; the first matching reflection point wins.
 */
enum mh_error_code	mh_a3d20_paths_build(
	const struct mh_a3d20_geometry *geometry,
	const struct mh_a3d20_material *material,
	const struct mh_spatial_pcm_listener *listener,
	const struct mh_spatial_pcm_voice *source, struct mh_a3d20_paths *paths,
	struct mh_error *error_out);

#endif
