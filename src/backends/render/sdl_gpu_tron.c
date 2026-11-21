#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_shaders.h"
#include "backends/render/sdl_gpu_tron.h"
#include "backends/render/sdl_gpu_vertex_input.h"
#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/render/tron.h"
#include "interfaces/render/renderer.h"

enum {
	PRIMITIVE_FILL,
	PRIMITIVE_OUTLINE,
	PRIMITIVE_COUNT,
	BLEND_COUNT = MH_TRON_ADD + 1
};

struct mh_gpu_tron {
	SDL_GPUDevice		*device;
	SDL_GPUGraphicsPipeline	*pipelines[BLEND_COUNT][PRIMITIVE_COUNT];
	SDL_GPUBuffer		*vertices;
	SDL_GPUTransferBuffer	*transfer;
	SDL_GPUSampler		*sampler;
	struct mh_tron_draw	*draws;
	size_t			face_capacity;
	unsigned int		width, height;
};

void
mh_gpu_tron_destroy(struct mh_gpu_tron *tron)
{
	unsigned int blend, primitive;

	if (tron == NULL)
		return;

	for (blend = 0; blend < BLEND_COUNT; blend++)
		for (primitive = 0; primitive < PRIMITIVE_COUNT; primitive++)
			if (tron->pipelines[blend][primitive] != NULL)
				SDL_ReleaseGPUGraphicsPipeline(tron->device,
					tron->pipelines[blend][primitive]);

	if (tron->vertices != NULL)
		SDL_ReleaseGPUBuffer(tron->device, tron->vertices);

	if (tron->transfer != NULL)
		SDL_ReleaseGPUTransferBuffer(tron->device, tron->transfer);

	if (tron->sampler != NULL)
		SDL_ReleaseGPUSampler(tron->device, tron->sampler);

	free(tron->draws);
	free(tron);
}

static int
create_pipelines(struct mh_gpu_tron *tron)
{
	SDL_GPUDevice *device;
	SDL_GPUShader *vertex, *fragment;
	SDL_GPUGraphicsPipelineCreateInfo info;
	struct mh_gpu_vertex_input vertex_input;
	SDL_GPUColorTargetDescription target;
	unsigned int blend, primitive;
	int success;

	device = tron->device;
	vertex = mh_gpu_shader_create(tron->device, MH_GPU_TRON_VERTEX);

	if (vertex == NULL)
		return 0;

	fragment = mh_gpu_shader_create(tron->device, MH_GPU_TRON_FRAGMENT);

	if (fragment == NULL) {
		SDL_ReleaseGPUShader(device, vertex);
		return 0;
	}

	memset(&target, 0, sizeof(target));
	target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	target.blend_state.enable_blend = true;
	target.blend_state.color_blend_op = target.blend_state.alpha_blend_op =
		SDL_GPU_BLENDOP_ADD;
	target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
	memset(&info, 0, sizeof(info));
	info.vertex_shader = vertex;
	info.fragment_shader = fragment;
	info.vertex_input_state =
		*mh_gpu_vertex_input(MH_GPU_TRON_VERTEX, &vertex_input);
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.rasterizer_state.enable_depth_clip = true;
	info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	info.depth_stencil_state.enable_depth_test = true;
	info.depth_stencil_state.enable_depth_write = true;
	info.depth_stencil_state.compare_op = SDL_GPU_COMPAREOP_LESS_OR_EQUAL;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	info.target_info.has_depth_stencil_target = true;
	info.target_info.depth_stencil_format = SDL_GPU_TEXTUREFORMAT_D32_FLOAT;
	success = 1;

	for (blend = 0; blend < BLEND_COUNT && success; blend++) {
		target.blend_state.src_color_blendfactor =
			blend == MH_TRON_BACKGROUND
			? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
			: blend == MH_TRON_SOURCE_ALPHA
			? SDL_GPU_BLENDFACTOR_SRC_ALPHA
			: SDL_GPU_BLENDFACTOR_ONE;
		target.blend_state.dst_color_blendfactor =
			blend == MH_TRON_BACKGROUND
			? SDL_GPU_BLENDFACTOR_SRC_ALPHA
			: blend == MH_TRON_SOURCE_ALPHA
			? SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA
			: blend == MH_TRON_ADD ? SDL_GPU_BLENDFACTOR_ONE
					: SDL_GPU_BLENDFACTOR_ZERO;

		for (primitive = 0; primitive < PRIMITIVE_COUNT && success;
			primitive++) {
			info.primitive_type = primitive == PRIMITIVE_OUTLINE
				? SDL_GPU_PRIMITIVETYPE_LINELIST
				: SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
			tron->pipelines[blend][primitive] =
				SDL_CreateGPUGraphicsPipeline(device, &info);
			success = tron->pipelines[blend][primitive] != NULL;
		}
	}

	SDL_ReleaseGPUShader(device, vertex);
	SDL_ReleaseGPUShader(device, fragment);

	return success;
}

struct mh_gpu_tron *
mh_gpu_tron_create(
	SDL_GPUDevice *device, unsigned int width, unsigned int height)
{
	struct mh_gpu_tron *tron;
	SDL_GPUSamplerCreateInfo sampler;

	tron = (struct mh_gpu_tron *)calloc(1, sizeof(*tron));

	if (tron == NULL) {
		SDL_SetError("out of memory creating TRON renderer");
		return NULL;
	}

	tron->device = device;
	tron->width = width;
	tron->height = height;
	memset(&sampler, 0, sizeof(sampler));
	sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
	sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sampler.address_mode_u = sampler.address_mode_v =
		sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	tron->sampler = SDL_CreateGPUSampler(device, &sampler);

	if (tron->sampler == NULL) {
		mh_gpu_tron_destroy(tron);
		return NULL;
	}

	if (!create_pipelines(tron)) {
		mh_gpu_tron_destroy(tron);
		return NULL;
	}

	return tron;
}

static int
reserve_frame(struct mh_gpu_tron *tron, size_t count)
{
	SDL_GPUBufferCreateInfo buffer;
	SDL_GPUTransferBufferCreateInfo transfer;
	SDL_GPUBuffer *vertices;
	SDL_GPUTransferBuffer *upload;
	struct mh_tron_draw *draws;

	if (count <= tron->face_capacity)
		return 1;

	if (count > MH_TRON_MAX_FACES || count > (size_t)-1 / sizeof(*draws) ||
		count > MH_U32_MAX /
				(MH_TRON_FACE_VERTICES_MAX *
					sizeof(struct mh_tron_vertex)))
		return SDL_SetError("TRON frame exceeds buffer limits");

	draws = (struct mh_tron_draw *)malloc(count * sizeof(*draws));

	if (draws == NULL)
		return SDL_SetError("out of memory allocating TRON draws");

	memset(&buffer, 0, sizeof(buffer));
	buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	buffer.size = (Uint32)(count * MH_TRON_FACE_VERTICES_MAX *
		sizeof(struct mh_tron_vertex));
	vertices = SDL_CreateGPUBuffer(tron->device, &buffer);

	if (vertices == NULL) {
		free(draws);
		return 0;
	}

	memset(&transfer, 0, sizeof(transfer));
	transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer.size = buffer.size;
	upload = SDL_CreateGPUTransferBuffer(tron->device, &transfer);

	if (upload == NULL) {
		SDL_ReleaseGPUBuffer(tron->device, vertices);
		free(draws);
		return 0;
	}

	if (tron->vertices != NULL)
		SDL_ReleaseGPUBuffer(tron->device, tron->vertices);

	if (tron->transfer != NULL)
		SDL_ReleaseGPUTransferBuffer(tron->device, tron->transfer);

	free(tron->draws);
	tron->vertices = vertices;
	tron->transfer = upload;
	tron->draws = draws;
	tron->face_capacity = count;

	return 1;
}

static int
upload_vertices(struct mh_gpu_tron *tron, size_t count)
{
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *copy_pass;
	SDL_GPUTransferBufferLocation source;
	SDL_GPUBufferRegion target;

	command = SDL_AcquireGPUCommandBuffer(tron->device);

	if (command == NULL)
		return 0;

	copy_pass = SDL_BeginGPUCopyPass(command);

	if (copy_pass == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		return 0;
	}

	memset(&source, 0, sizeof(source));
	source.transfer_buffer = tron->transfer;
	memset(&target, 0, sizeof(target));
	target.buffer = tron->vertices;
	target.size = (Uint32)(count * sizeof(struct mh_tron_vertex));
	SDL_UploadToGPUBuffer(copy_pass, &source, &target, true);
	SDL_EndGPUCopyPass(copy_pass);

	return SDL_SubmitGPUCommandBuffer(command);
}

int
mh_gpu_tron_prepare(struct mh_gpu_tron *tron,
	const struct mh_renderer_tron_face *faces, size_t count)
{
	struct mh_tron_vertex *vertices;
	struct mh_error error;
	enum mh_error_code status;
	size_t i, used;

	if (count != 0 && faces == NULL)
		return SDL_SetError("missing TRON faces");

	if (count == 0)
		return 1;

	if (!reserve_frame(tron, count))
		return 0;

	vertices = (struct mh_tron_vertex *)SDL_MapGPUTransferBuffer(
		tron->device, tron->transfer, true);

	if (vertices == NULL)
		return 0;

	used = 0;

	for (i = 0; i < count; i++) {
		status = mh_tron_draw_face(&faces[i], tron->width, tron->height,
			&tron->draws[i], vertices + used, &error);

		if (status != MH_ERROR_NONE) {
			SDL_UnmapGPUTransferBuffer(
				tron->device, tron->transfer);
			return SDL_SetError("%s", error.message);
		}

		tron->draws[i].fill_first += (unsigned int)used;
		tron->draws[i].outline_first += (unsigned int)used;
		used += tron->draws[i].fill_count +
			tron->draws[i].outline_count;
	}

	SDL_UnmapGPUTransferBuffer(tron->device, tron->transfer);

	return upload_vertices(tron, used);
}

void
mh_gpu_tron_draw(struct mh_gpu_tron *tron, SDL_GPUCommandBuffer *command,
	SDL_GPURenderPass *pass, size_t index, SDL_GPUTexture *texture)
{
	const struct mh_tron_draw *draw;
	const struct mh_tron_style *style;
	SDL_GPUBufferBinding buffer;
	SDL_GPUTextureSamplerBinding image;
	float viewport[4], material[4];

	draw = &tron->draws[index];
	style = mh_tron_style(draw->pass);
	viewport[0] = tron->width;
	viewport[1] = tron->height;
	viewport[2] = viewport[3] = 0;
	material[0] = (float)style->fog;
	material[1] = (float)style->texture_alpha;
	material[2] = (float)style->vertex_alpha;
	material[3] = 0;
	buffer.buffer = tron->vertices;
	buffer.offset = 0;
	image.texture = texture;
	image.sampler = tron->sampler;
	SDL_BindGPUGraphicsPipeline(
		pass, tron->pipelines[style->blend][PRIMITIVE_FILL]);
	SDL_BindGPUVertexBuffers(pass, 0, &buffer, 1);
	SDL_BindGPUFragmentSamplers(pass, 0, &image, 1);
	SDL_PushGPUVertexUniformData(command, 0, viewport, sizeof(viewport));
	SDL_PushGPUFragmentUniformData(command, 0, material, sizeof(material));
	SDL_DrawGPUPrimitives(pass, draw->fill_count, 1, draw->fill_first, 0);

	if (draw->outline_count != 0) {
		material[3] = (float)style->outline_bias;
		SDL_PushGPUFragmentUniformData(
			command, 0, material, sizeof(material));
		SDL_BindGPUGraphicsPipeline(
			pass, tron->pipelines[style->blend][PRIMITIVE_OUTLINE]);
		SDL_DrawGPUPrimitives(
			pass, draw->outline_count, 1, draw->outline_first, 0);
	}
}
