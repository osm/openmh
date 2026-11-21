#ifndef MH_A3D20_SOURCE_H
#define MH_A3D20_SOURCE_H

#include "engine/audio/spatial_source.h"

#define MH_A3D20_VOICES 24

struct mh_a3d20_source_input {
	int		spatial, has_source;
	unsigned long	priority;
	float		gain, pan, near_distance, far_distance, pitch, elapsed;
	const float	*position;
};

struct mh_a3d20_source_listener {
	float	position[3], previous[3], right[3];
};

struct mh_a3d20_source_parameters {
	float	gain, pan, pitch;
};

/* Classify velocity per axis even when no time has elapsed. */
enum mh_a3d20_velocity_kind {
	MH_A3D20_VELOCITY_FINITE,
	MH_A3D20_VELOCITY_UNDEFINED,
	MH_A3D20_VELOCITY_POSITIVE_INFINITY,
	MH_A3D20_VELOCITY_NEGATIVE_INFINITY
};

struct mh_a3d20_source_update {
	int				changed;
	float				position[3], velocity[3];
	enum mh_a3d20_velocity_kind	velocity_kind[3];
};

/*
 * Finite inputs, nonnegative elapsed when requesting motion. Spatial position
 * is borrowed and required when a listener is supplied. Nonspatial sounds with
 * a listener require a source object in the original. A NULL listener skips
 * range testing and listener-relative positioning. Spatial gain/pan are
 * retained scratch; pitch is a ratio, not a frequency. History stays in game
 * coordinates, even when range testing rejects a sound.
 *
 * Output is in A3D coordinates: negate Z; spatial position also adds .5 to Y.
 * Zero elapsed classifies each velocity component instead of dividing by zero;
 * nonfinite components have a zero placeholder in velocity[]. Consumers must
 * interpret velocity_kind rather than treating those placeholders as motion.
 *
 * When changed, submit velocity, push geometry transform, translate by output
 * position, bind source, pop transform. Driver failures do not roll back state.
 */
int	mh_a3d20_source_prepare(struct mh_spatial_source_state *state,
	const struct mh_a3d20_source_input *input,
	const struct mh_a3d20_source_listener *listener, float master_gain,
	struct mh_a3d20_source_parameters *parameters,
	struct mh_a3d20_source_update *output);

/* Return a one-based voice slot, or zero when admission is rejected. */
unsigned int	mh_a3d20_source_slot(float score,
	const struct mh_spatial_source_state *const voices[MH_A3D20_VOICES]);

#endif
