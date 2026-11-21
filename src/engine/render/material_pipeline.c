#include <stddef.h>

#include "engine/render/material_pipeline.h"

enum mh_error_code
mh_material_pipeline_state(enum mh_gpu_pipeline pipeline,
	struct mh_raster_state *output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_raster_state_begin(pipeline, output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	switch (pipeline) {
	case MH_GPU_PIPELINE_DISTANT:
		/* Fog blends against the background but still writes depth. */
		mh_raster_state_blend(output, MH_RASTER_SOURCE_ALPHA,
			MH_RASTER_INVERSE_SOURCE_ALPHA, MH_RASTER_ZERO,
			MH_RASTER_ONE);
		break;

	case MH_GPU_PIPELINE_ADDITIVE:
		/* Additive RGB without depth writes. */
		output->depth_write = 0;
		mh_raster_state_blend(output, MH_RASTER_ONE, MH_RASTER_ONE,
			MH_RASTER_ONE, MH_RASTER_ZERO);
		break;

	case MH_GPU_PIPELINE_GROUND:
		output->depth_bias = -16.0f;

		/* Both ground marks multiply existing RGB. */
		/* fall through */
	case MH_GPU_PIPELINE_SKID:
		output->depth_write = 0;
		mh_raster_state_blend(output, MH_RASTER_DESTINATION_COLOR,
			MH_RASTER_ZERO, MH_RASTER_ZERO, MH_RASTER_ONE);
		break;

	case MH_GPU_PIPELINE_BLUR_SKY:
		/* Constant alpha 128. */
		mh_raster_state_blend(output, MH_RASTER_CONSTANT_COLOR,
			MH_RASTER_INVERSE_CONSTANT_COLOR, MH_RASTER_ZERO,
			MH_RASTER_ONE);

		/* Skies do not test or write depth. */
		/* fall through */
	case MH_GPU_PIPELINE_SKY:
		output->depth_test = 0;
		output->depth_write = 0;
		break;

	case MH_GPU_PIPELINE_PHONG:
		/* Reflection and Phong retain matching positions/depth. */
		mh_raster_state_blend(output, MH_RASTER_ONE, MH_RASTER_ONE,
			MH_RASTER_ZERO, MH_RASTER_ONE);
		break;

	default:
		break;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
