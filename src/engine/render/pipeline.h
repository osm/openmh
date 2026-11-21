#ifndef MH_RENDER_PIPELINE_H
#define MH_RENDER_PIPELINE_H

#include "engine/core/error.h"

enum mh_gpu_pipeline {
	MH_GPU_PIPELINE_OPAQUE,
	MH_GPU_PIPELINE_ALPHA,
	MH_GPU_PIPELINE_DISTANT,
	MH_GPU_PIPELINE_ADDITIVE,
	MH_GPU_PIPELINE_SKID,
	MH_GPU_PIPELINE_GROUND,
	MH_GPU_PIPELINE_SKY,
	MH_GPU_PIPELINE_BLUR_SKY,
	MH_GPU_PIPELINE_REFLECTION,
	MH_GPU_PIPELINE_PHONG,
	MH_GPU_PIPELINE_COUNT
};

enum mh_raster_factor {
	MH_RASTER_ZERO,
	MH_RASTER_ONE,
	MH_RASTER_SOURCE_ALPHA,
	MH_RASTER_INVERSE_SOURCE_ALPHA,
	MH_RASTER_DESTINATION_COLOR,
	MH_RASTER_CONSTANT_COLOR,
	MH_RASTER_INVERSE_CONSTANT_COLOR
};

/*
 * Raster policy shared by GPU implementations. Depth is forward LEQUAL unless
 * always is set. Blend equations are ADD; no culling or multisampling is used.
 * Depth bias uses native device units, preserving direction, not legacy depth
 * quantization. Resource formats and shader binding belong to each backend.
 */
struct mh_raster_state {
	int			depth_test, depth_write, depth_always;
	float			depth_bias;
	int			blend;
	enum mh_raster_factor	source_color, destination_color;
	enum mh_raster_factor	source_alpha, destination_alpha;
};

/* Validate the pass before replacing output with the common opaque state. */
enum mh_error_code	mh_raster_state_begin(enum mh_gpu_pipeline pipeline,
	struct mh_raster_state *output, struct mh_error *error_out);

/* Enable ADD blending with explicit, independent RGB and alpha factors. */
void	mh_raster_state_blend(struct mh_raster_state *state,
	enum mh_raster_factor source_color,
	enum mh_raster_factor destination_color,
	enum mh_raster_factor source_alpha,
	enum mh_raster_factor destination_alpha);

#endif
