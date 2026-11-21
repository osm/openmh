#ifndef MH_WEBGL_PIPELINE_H
#define MH_WEBGL_PIPELINE_H

#include "engine/render/pipeline.h"

/*
 * Apply to the current WebGL 2 context; shader/resources are bound separately.
 */
void	mh_webgl_pipeline_apply(const struct mh_raster_state *state);

#endif
