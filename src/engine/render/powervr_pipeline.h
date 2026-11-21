#ifndef MH_POWERVR_PIPELINE_H
#define MH_POWERVR_PIPELINE_H

#include "engine/render/pipeline.h"

/* Invalid passes leave output unchanged. */
enum mh_error_code	mh_powervr_pipeline_state(enum mh_gpu_pipeline pipeline,
	struct mh_raster_state *output, struct mh_error *error_out);

#endif
