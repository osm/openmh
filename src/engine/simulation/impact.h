#ifndef MH_IMPACT_H
#define MH_IMPACT_H

#include "engine/core/error.h"

/*
 * Any pair contact, last other object's sound class,
 * and greatest positive closing-speed numerator within the frame. Cars use
 * class zero. This is NOT the mass/inertia-dependent solver impulse.
 */
struct mh_impact_effects {
	int		contact;
	unsigned int	sound_class;
	double		severity;
};

/* Binary32 inputs/output, y+x+z dot order. */
enum mh_error_code	mh_impact_severity(const double relative_velocity[3],
	const double normal[3], double *output, struct mh_error *error_out);

/*
 * Zero initialization starts a frame; separating contacts still set contact
 * and sound_class. Strict maximum retains the earlier severity on ties.
 */
enum mh_error_code	mh_impact_add(struct mh_impact_effects *effects,
	unsigned int sound_class, double severity, struct mh_error *error_out);

/* Clear flag/maximum at frame entry; keep the preceding class. */
void	mh_impact_begin_frame(struct mh_impact_effects *effects);

#endif
