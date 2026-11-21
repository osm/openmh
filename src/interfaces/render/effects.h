#ifndef MH_INTERFACES_RENDER_EFFECTS_H
#define MH_INTERFACES_RENDER_EFFECTS_H

#include "interfaces/render/halos.h"
#include "interfaces/render/names.h"
#include "interfaces/render/skids.h"

struct mh_particles;
struct mh_software_particle_assets;

/*
 * Software particle inputs are borrowed until the lens frame returns. The
 * frustum describes that frame's camera. Assets contain the software animation
 * images; brightness is the effective world RGB mean. States are never changed.
 */
struct mh_renderer_particles {
	const struct mh_particles			*states;
	const struct mh_software_particle_assets	*assets;
	const struct mh_frustum				*frustum;
	unsigned int					cars;
	float						brightness;
	int						smoke, sparks;
};

/*
 * Names precede their body's halos/faces; halos retain source insertion
 * positions. Skids then particles follow models.
 */
struct mh_renderer_effects {
	const struct mh_renderer_names		*names;
	const struct mh_renderer_halos		*halos;
	const struct mh_renderer_skids		*skids;
	const struct mh_renderer_particles	*particles;
};

#endif
