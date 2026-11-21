#include <stddef.h>
#include <string.h>

#include "engine/render/pipeline.h"

enum mh_error_code
mh_raster_state_begin(enum mh_gpu_pipeline pipeline,
	struct mh_raster_state *output, struct mh_error *error_out)
{
	if (output == NULL || pipeline < 0 || pipeline >= MH_GPU_PIPELINE_COUNT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid render pipeline");

	memset(output, 0, sizeof(*output));
	output->depth_test = 1;
	output->depth_write = 1;
	output->source_color = MH_RASTER_ONE;
	output->source_alpha = MH_RASTER_ONE;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_raster_state_blend(struct mh_raster_state *state,
	enum mh_raster_factor source_color,
	enum mh_raster_factor destination_color,
	enum mh_raster_factor source_alpha,
	enum mh_raster_factor destination_alpha)
{
	state->blend = 1;

	state->source_color = source_color;
	state->destination_color = destination_color;
	state->source_alpha = source_alpha;
	state->destination_alpha = destination_alpha;
}
