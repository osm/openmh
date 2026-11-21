#ifndef MH_SOFTWARE_MODEL_LIGHT_H
#define MH_SOFTWARE_MODEL_LIGHT_H

#include "backends/render/software_lighting.h"
#include "engine/render/mesh.h"
#include "interfaces/render/lighting.h"

/* Zero-initialize; owns copied source lights and reusable prepared scratch. */
struct mh_software_model_light {
	struct mh_model_lighting	settings;
	struct mh_point_light		*lights;
	struct mh_prepared_light	*prepared;
	struct mh_software_lighting	frame;
	int				enabled;
};

void	mh_software_model_light_free(struct mh_software_model_light *state);

/* NULL settings clears the owner. Errors preserve the previous settings. */
enum mh_error_code	mh_software_model_light_set(
	struct mh_software_model_light *state,
	const struct mh_model_lighting *settings, struct mh_error *error_out);

/*
 * NULL model uses identity. Returns zero on failure; scratch/frame may be
 * partially updated and must not be used until preparation succeeds.
 */
int	mh_software_model_light_prepare(struct mh_software_model_light *state,
	const float *model);
int	mh_software_model_reflects(const struct mh_software_model_light *state,
	unsigned int flags);

/* Returns zero if disabled or shading fails, preserving color. */
int	mh_software_model_light_color(
	const struct mh_software_model_light *state,
	const struct mh_mesh_face *face, const struct mh_vertex *vertex,
	const float camera[12], int near_clipped, float color[3]);

#endif
