#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_retro.h"
#include "backends/render/sdl_gpu_shaders.h"
#include "backends/render/sdl_gpu_vertex_input.h"

enum {
	PASS_BACKDROP,
	PASS_GEOMETRY,
	PASS_COUNT
};

struct mh_gpu_retro {
	SDL_GPUDevice		*device;
	SDL_GPUGraphicsPipeline	*pipelines[PASS_COUNT];
	SDL_GPUBuffer		*vertices;
	SDL_GPUTransferBuffer	*transfer;
};

void
mh_gpu_retro_destroy(struct mh_gpu_retro *retro)
{
	unsigned int pass;

	if (retro == NULL)
		return;

	for (pass = 0; pass < PASS_COUNT; pass++)
		if (retro->pipelines[pass] != NULL)
			SDL_ReleaseGPUGraphicsPipeline(
				retro->device, retro->pipelines[pass]);

	if (retro->vertices != NULL)
		SDL_ReleaseGPUBuffer(retro->device, retro->vertices);

	if (retro->transfer != NULL)
		SDL_ReleaseGPUTransferBuffer(retro->device, retro->transfer);

	free(retro);
}

static int
create_pipelines(struct mh_gpu_retro *retro)
{
	SDL_GPUDevice *device;
	SDL_GPUShader *vertex, *fragment;
	SDL_GPUGraphicsPipelineCreateInfo info;
	struct mh_gpu_vertex_input vertex_input;
	SDL_GPUColorTargetDescription target;
	unsigned int pass;

	device = retro->device;
	vertex = mh_gpu_shader_create(retro->device, MH_GPU_RETRO_VERTEX);

	if (vertex == NULL)
		return 0;

	fragment = mh_gpu_shader_create(retro->device, MH_GPU_RETRO_FRAGMENT);

	if (fragment == NULL) {
		SDL_ReleaseGPUShader(device, vertex);
		return 0;
	}

	memset(&target, 0, sizeof(target));
	target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	target.blend_state.enable_blend = true;
	target.blend_state.color_blend_op = target.blend_state.alpha_blend_op =
		SDL_GPU_BLENDOP_ADD;
	target.blend_state.src_color_blendfactor =
		SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	target.blend_state.dst_color_blendfactor =
		SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
	memset(&info, 0, sizeof(info));
	info.vertex_shader = vertex;
	info.fragment_shader = fragment;
	info.vertex_input_state =
		*mh_gpu_vertex_input(MH_GPU_RETRO_VERTEX, &vertex_input);
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.rasterizer_state.enable_depth_clip = true;
	info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	info.depth_stencil_state.enable_depth_test = true;
	info.depth_stencil_state.enable_depth_write = true;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;

	for (pass = 0; pass < PASS_COUNT; pass++) {
		info.depth_stencil_state.compare_op = pass == PASS_BACKDROP
			? SDL_GPU_COMPAREOP_ALWAYS
			: SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
		retro->pipelines[pass] =
			SDL_CreateGPUGraphicsPipeline(device, &info);

		if (retro->pipelines[pass] == NULL)
			break;
	}

	SDL_ReleaseGPUShader(device, vertex);
	SDL_ReleaseGPUShader(device, fragment);

	return pass == PASS_COUNT;
}

struct mh_gpu_retro *
mh_gpu_retro_create(SDL_GPUDevice *device)
{
	struct mh_gpu_retro *retro;
	SDL_GPUBufferCreateInfo vertices;
	SDL_GPUTransferBufferCreateInfo transfer;

	retro = (struct mh_gpu_retro *)calloc(1, sizeof(*retro));

	if (retro == NULL) {
		SDL_SetError("out of memory creating retro renderer");
		return NULL;
	}

	retro->device = device;

	if (!create_pipelines(retro)) {
		mh_gpu_retro_destroy(retro);
		return NULL;
	}

	memset(&vertices, 0, sizeof(vertices));
	vertices.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	vertices.size =
		MH_RETRO_MAX_VERTICES * sizeof(struct mh_retro_draw_vertex);

	retro->vertices = SDL_CreateGPUBuffer(device, &vertices);
	memset(&transfer, 0, sizeof(transfer));
	transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer.size = vertices.size;
	retro->transfer = SDL_CreateGPUTransferBuffer(device, &transfer);

	if (retro->vertices == NULL || retro->transfer == NULL) {
		mh_gpu_retro_destroy(retro);
		return NULL;
	}

	return retro;
}

int
mh_gpu_retro_prepare(
	struct mh_gpu_retro *retro, const struct mh_retro_draw *draw)
{
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *copy_pass;
	SDL_GPUTransferBufferLocation source;
	SDL_GPUBufferRegion target;
	void *mapped;
	struct mh_error error;

	if (retro == NULL)
		return SDL_SetError("invalid retro GPU frame");

	if (mh_retro_draw_validate(draw, &error) != MH_ERROR_NONE)
		return SDL_SetError("%s", error.message);

	mapped = SDL_MapGPUTransferBuffer(retro->device, retro->transfer, true);

	if (mapped == NULL)
		return 0;

	memcpy(mapped, draw->vertices, draw->count * sizeof(*draw->vertices));
	SDL_UnmapGPUTransferBuffer(retro->device, retro->transfer);
	command = SDL_AcquireGPUCommandBuffer(retro->device);

	if (command == NULL)
		return 0;

	copy_pass = SDL_BeginGPUCopyPass(command);

	if (copy_pass == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		return 0;
	}

	memset(&source, 0, sizeof(source));
	source.transfer_buffer = retro->transfer;
	memset(&target, 0, sizeof(target));
	target.buffer = retro->vertices;
	target.size = draw->count * sizeof(*draw->vertices);
	SDL_UploadToGPUBuffer(copy_pass, &source, &target, true);
	SDL_EndGPUCopyPass(copy_pass);

	return SDL_SubmitGPUCommandBuffer(command);
}

void
mh_gpu_retro_draw(struct mh_gpu_retro *retro, SDL_GPUCommandBuffer *command,
	SDL_GPURenderPass *pass, SDL_GPUTexture *atlas, SDL_GPUSampler *sampler,
	const struct mh_retro_draw *draw)
{
	SDL_GPUBufferBinding buffer;
	SDL_GPUTextureSamplerBinding texture;
	float parameters[4];

	buffer.buffer = retro->vertices;
	buffer.offset = 0;
	texture.texture = atlas;
	texture.sampler = sampler;
	SDL_BindGPUGraphicsPipeline(pass, retro->pipelines[PASS_BACKDROP]);
	SDL_BindGPUVertexBuffers(pass, 0, &buffer, 1);
	SDL_BindGPUFragmentSamplers(pass, 0, &texture, 1);
	parameters[0] = (float)draw->width;
	parameters[1] = (float)draw->height;
	parameters[2] = parameters[3] = 0;
	SDL_PushGPUVertexUniformData(
		command, 0, parameters, sizeof(parameters));
	parameters[0] = parameters[1] = 0;
	SDL_PushGPUFragmentUniformData(
		command, 0, parameters, sizeof(parameters));
	SDL_DrawGPUPrimitives(pass, MH_RETRO_BACKDROP_VERTICES, 1, 0, 0);
	SDL_BindGPUGraphicsPipeline(pass, retro->pipelines[PASS_GEOMETRY]);

	if (draw->text_end > MH_RETRO_BACKDROP_VERTICES) {
		parameters[0] = 1;
		SDL_PushGPUFragmentUniformData(
			command, 0, parameters, sizeof(parameters));
		SDL_DrawGPUPrimitives(pass,
			draw->text_end - MH_RETRO_BACKDROP_VERTICES, 1,
			MH_RETRO_BACKDROP_VERTICES, 0);
	}

	if (draw->count > draw->text_end) {
		parameters[0] = 0;
		SDL_PushGPUFragmentUniformData(
			command, 0, parameters, sizeof(parameters));
		SDL_DrawGPUPrimitives(pass, draw->count - draw->text_end, 1,
			draw->text_end, 0);
	}
}
