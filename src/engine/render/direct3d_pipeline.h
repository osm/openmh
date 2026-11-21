#ifndef MH_DIRECT3D_PIPELINE_H
#define MH_DIRECT3D_PIPELINE_H

#include "engine/render/pipeline.h"

/* Invalid passes leave output unchanged. */
enum mh_error_code	mh_direct3d_pipeline_state(
	enum mh_gpu_pipeline pipeline, struct mh_raster_state *output,
	struct mh_error *error_out);

#endif
