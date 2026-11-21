#ifndef MH_OPENMH_SCENE_LIGHTING_H
#define MH_OPENMH_SCENE_LIGHTING_H

#include "engine/render/scene.h"

/* Submit scene-derived light settings; the backend owns GPU light buffers. */
enum mh_error_code	mh_openmh_scene_lighting(
	const struct mh_race_scene *scene, const struct mh_drive *viewed,
	struct mh_renderer *renderer, unsigned int index, int enabled,
	struct mh_error *error_out);

#endif
