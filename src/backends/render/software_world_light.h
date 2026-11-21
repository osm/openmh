#ifndef MH_SOFTWARE_WORLD_LIGHT_H
#define MH_SOFTWARE_WORLD_LIGHT_H

#include "backends/render/software_model_light.h"

/*
 * Zero-initialize; owns selected-light scratch and borrows immutable world
 * arrays. Keep them alive until replaced or cleared. No frame-time allocation.
 */
struct mh_software_world_light {
	struct mh_world			world;
	struct mh_software_model_light	lighting;
	size_t				capacity;
};

void	mh_software_world_light_free(struct mh_software_world_light *state);

/* NULL world and lighting clear the owner; errors preserve its state. */
enum mh_error_code	mh_software_world_light_set(
	struct mh_software_world_light *state, const struct mh_world *world,
	const struct mh_model_lighting *lighting, struct mh_error *error_out);

/*
 * Select at root translation (NULL uses origin), then prepare with model
 * (NULL uses identity). Zero means disabled or failed; scratch may change.
 */
int	mh_software_world_light_prepare(struct mh_software_world_light *state,
	const float *model, const float *root);

#endif
