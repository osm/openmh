#include <GLES3/gl3.h>

#include "backends/render/webgl_pipeline.h"

static GLenum
blend_factor(enum mh_raster_factor factor)
{
	static const GLenum factors[] = {GL_ZERO, GL_ONE, GL_SRC_ALPHA,
		GL_ONE_MINUS_SRC_ALPHA, GL_DST_COLOR, GL_CONSTANT_COLOR,
		GL_ONE_MINUS_CONSTANT_COLOR};

	return factors[factor];
}

static void
set_enabled(GLenum capability, int enabled)
{
	if (enabled)
		glEnable(capability);
	else
		glDisable(capability);
}

void
mh_webgl_pipeline_apply(const struct mh_raster_state *state)
{
	glDisable(GL_CULL_FACE);
	glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
	set_enabled(GL_DEPTH_TEST, state->depth_test);
	glDepthMask(state->depth_write ? GL_TRUE : GL_FALSE);
	glDepthFunc(state->depth_always ? GL_ALWAYS : GL_LEQUAL);
	set_enabled(GL_POLYGON_OFFSET_FILL, state->depth_bias != 0);
	glPolygonOffset(0, state->depth_bias);
	set_enabled(GL_BLEND, state->blend);
	glBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD);
	glBlendFuncSeparate(blend_factor(state->source_color),
		blend_factor(state->destination_color),
		blend_factor(state->source_alpha),
		blend_factor(state->destination_alpha));
}
