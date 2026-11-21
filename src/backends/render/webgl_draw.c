#include <string.h>

#include "backends/render/webgl_draw.h"
#include "backends/render/webgl_error.h"
#include "backends/render/webgl_pipeline.h"
#include "backends/render/webgl_powervr_order.h"
#include "backends/render/webgl_sampler.h"
#include "engine/render/direct3d_pipeline.h"
#include "engine/render/material_pipeline.h"
#include "engine/render/openmh_light_buffer.h"
#include "engine/render/powervr_pipeline.h"
#include "engine/render/scene_material_policy.h"

enum mh_error_code
mh_webgl_drawing_create(
	struct mh_webgl_drawing *drawing, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i, repeat;

	if (drawing == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing WebGL drawing resources");

	if (drawing->blocks[0] != 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebGL drawing resources are already created");

	glGenBuffers(4, drawing->blocks);
	status = mh_webgl_check_error(error_out);

	for (i = 0; status == MH_ERROR_NONE && i <= MH_WEBGL_POWERVR; i++)
		if (mh_webgl_program_available((enum mh_webgl_program_kind)i))
			status = mh_webgl_program_create(&drawing->programs[i],
				(enum mh_webgl_program_kind)i, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < 3; i++)
		for (repeat = 0; status == MH_ERROR_NONE && repeat < 2;
			repeat++)
			status = mh_webgl_sampler_create(
				&drawing->samplers[i][repeat],
				(enum mh_texture_filter)(
					MH_TEXTURE_FILTER_NEAREST + i),
				repeat, repeat, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_sampler_create(&drawing->sky_sampler,
			MH_TEXTURE_FILTER_BILINEAR, 1, 0, error_out);

	if (status != MH_ERROR_NONE)
		mh_webgl_drawing_destroy(drawing);

	return status;
}

#if MH_RENDER_BUILD_OPENMH
enum mh_error_code
mh_webgl_drawing_openmh(
	struct mh_webgl_drawing *drawing, struct mh_error *error_out)
{
	if (drawing->programs[MH_WEBGL_OPENMH].handle != 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return mh_webgl_program_create(&drawing->programs[MH_WEBGL_OPENMH],
		MH_WEBGL_OPENMH, error_out);
}
#endif

void
mh_webgl_drawing_destroy(struct mh_webgl_drawing *drawing)
{
	unsigned int i;

	if (drawing == NULL)
		return;

	for (i = 0; i < MH_WEBGL_PROGRAM_COUNT; i++)
		mh_webgl_program_destroy(&drawing->programs[i]);

	glDeleteBuffers(4, drawing->blocks);
	memset(drawing->blocks, 0, sizeof(drawing->blocks));

	for (i = 0; i < 3; i++) {
		glDeleteSamplers(2, drawing->samplers[i]);
		drawing->samplers[i][0] = drawing->samplers[i][1] = 0;
	}

	glDeleteSamplers(1, &drawing->sky_sampler);
	drawing->sky_sampler = 0;

	if (MH_RENDER_BUILD_POWERVR)
		mh_webgl_order_destroy(drawing->order);

	drawing->order = NULL;
	drawing->bound_valid = 0;
}

static void
uniforms(GLuint buffer, GLuint binding, const float *data, size_t count)
{
	glBindBuffer(GL_UNIFORM_BUFFER, buffer);
	glBufferData(
		GL_UNIFORM_BUFFER, count * sizeof(*data), data, GL_STREAM_DRAW);
	glBindBufferBase(GL_UNIFORM_BUFFER, binding, buffer);
}

static enum mh_error_code
pipeline_state(const struct mh_webgl_draw_call *call,
	struct mh_raster_state *raster, struct mh_error *error_out)
{
	if (MH_RENDER_BUILD_POWERVR && call->program == MH_WEBGL_POWERVR)
		return mh_powervr_pipeline_state(
			call->pipeline, raster, error_out);

	if (MH_RENDER_BUILD_DIRECT3D && call->program >= MH_WEBGL_DIRECT3D &&
		call->program != MH_WEBGL_OPENMH)
		return mh_direct3d_pipeline_state(
			call->pipeline, raster, error_out);

	return mh_material_pipeline_state(call->pipeline, raster, error_out);
}

enum mh_error_code
mh_webgl_draw_bind(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_draw_call *call, struct mh_error *error_out)
{
	struct mh_raster_state raster;
	enum mh_error_code status;

	if (drawing == NULL || call == NULL ||
		call->program < MH_WEBGL_MATERIAL ||
		(call->program > MH_WEBGL_POWERVR &&
			call->program != MH_WEBGL_OPENMH) ||
		drawing->programs[call->program].handle == 0 ||
		call->geometry == NULL || call->texture == 0 ||
		call->sampler == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid WebGL draw call");

	status = pipeline_state(call, &raster, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!drawing->bound_valid || drawing->bound.program != call->program ||
		drawing->bound.pipeline != call->pipeline)
		mh_webgl_pipeline_apply(&raster);

	if (!drawing->bound_valid || drawing->bound.program != call->program)
		glUseProgram(drawing->programs[call->program].handle);

	if (!drawing->bound_valid ||
		memcmp(drawing->bound.camera, call->camera,
			sizeof(call->camera)) != 0)
		uniforms(drawing->blocks[0], MH_WEBGL_VERTEX_PARAMETERS,
			call->camera, MH_GPU_CAMERA_FLOATS);

	if (!drawing->bound_valid ||
		memcmp(drawing->bound.material, call->material,
			sizeof(call->material)) != 0)
		uniforms(drawing->blocks[1], MH_WEBGL_VERTEX_MATERIAL,
			call->material, MH_GPU_MATERIAL_FLOATS);

	if (call->program == MH_WEBGL_OPENMH) {
		if (call->lighting == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing OpenMH lighting block");

		if (!drawing->bound_valid ||
			drawing->bound.lighting != call->lighting)
			uniforms(drawing->blocks[2], MH_WEBGL_LIGHTING,
				call->lighting, MH_OPENMH_LIGHT_FLOATS);

		if (!drawing->bound_valid ||
			memcmp(drawing->bound.surface, call->surface,
				sizeof(call->surface)) != 0)
			uniforms(drawing->blocks[3], MH_WEBGL_SURFACE,
				call->surface, 4);
	}

	if (!drawing->bound_valid || drawing->bound.texture != call->texture) {
		glActiveTexture(GL_TEXTURE0 + MH_WEBGL_TEXTURE);
		glBindTexture(GL_TEXTURE_2D, call->texture);
	}

	if (!drawing->bound_valid || drawing->bound.sampler != call->sampler)
		glBindSampler(MH_WEBGL_TEXTURE, call->sampler);

	drawing->bound = *call;
	drawing->bound_valid = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_draw_submit(struct mh_webgl_drawing *drawing,
	const struct mh_webgl_draw_call *call, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_webgl_draw_bind(drawing, call, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_webgl_geometry_draw(
		call->geometry, call->first, call->count, error_out);
}

GLuint
mh_webgl_material_sampler(const struct mh_webgl_drawing *drawing, int powervr,
	int direct3d, int glenz, int environment, int alpha)
{
	struct mh_material_sampling sampling;
	enum mh_render_profile profile;

	profile = powervr  ? MH_RENDER_POWERVR
		: direct3d ? MH_RENDER_DIRECT3D
			: MH_RENDER_GLIDE;
	sampling = mh_scene_material_sampling(
		profile, drawing->filter, glenz, environment, alpha);

	return drawing->samplers[sampling.filter - MH_TEXTURE_FILTER_NEAREST]
				[sampling.repeat];
}
