#include <stddef.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_shaders.h"
#include "backends/render/sdl_gpu_ui.h"
#include "backends/render/sdl_gpu_vertex_input.h"

enum {
	ATLAS_SIZE_MAX = 2048,
	QUAD_VERTICES = 6
};

static int
create_ui_pipeline(struct mh_gpu_renderer *gpu)
{
	SDL_GPUShader *vertex, *fragment;
	SDL_GPUGraphicsPipelineCreateInfo info;
	struct mh_gpu_vertex_input vertex_input;
	SDL_GPUColorTargetDescription target;

	vertex = mh_gpu_shader_create(gpu->device, MH_GPU_UI_VERTEX);

	if (vertex == NULL)
		return 0;

	fragment = mh_gpu_shader_create(gpu->device, MH_GPU_UI_FRAGMENT);

	if (fragment == NULL) {
		SDL_ReleaseGPUShader(gpu->device, vertex);
		return 0;
	}

	memset(&target, 0, sizeof(target));
	target.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	target.blend_state.enable_blend = true;
	target.blend_state.src_color_blendfactor =
		SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	target.blend_state.dst_color_blendfactor =
		SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	target.blend_state.dst_alpha_blendfactor =
		SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	memset(&info, 0, sizeof(info));
	info.vertex_shader = vertex;
	info.fragment_shader = fragment;
	info.vertex_input_state =
		*mh_gpu_vertex_input(MH_GPU_UI_VERTEX, &vertex_input);
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	info.rasterizer_state.enable_depth_clip = true;
	info.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	info.target_info.color_target_descriptions = &target;
	info.target_info.num_color_targets = 1;
	gpu->ui_pipeline = SDL_CreateGPUGraphicsPipeline(gpu->device, &info);
	SDL_ReleaseGPUShader(gpu->device, fragment);
	SDL_ReleaseGPUShader(gpu->device, vertex);

	return gpu->ui_pipeline != NULL;
}

int
mh_gpu_renderer_ui_upload(
	struct mh_gpu_renderer *gpu, const struct mh_ui_atlas *atlas)
{
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_GPUBufferCreateInfo buffer_info;
	SDL_GPUSamplerCreateInfo sampler;
	SDL_GPUTransferBuffer *upload;
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *copy_pass;
	SDL_GPUTextureTransferInfo source;
	SDL_GPUTextureRegion destination;
	void *mapped;
	Uint32 bytes;
	int submitted;

	if (atlas == NULL || atlas->image.rgba == NULL ||
		atlas->image.width == 0 || atlas->image.height == 0 ||
		atlas->image.width > ATLAS_SIZE_MAX ||
		atlas->image.height > ATLAS_SIZE_MAX || gpu->ui_texture != NULL)
		return SDL_SetError("invalid or already uploaded UI atlas");

	if (!create_ui_pipeline(gpu))
		return 0;

	memset(&sampler, 0, sizeof(sampler));
	sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
	sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sampler.address_mode_u = sampler.address_mode_v =
		sampler.address_mode_w =
			SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	gpu->ui_sampler = SDL_CreateGPUSampler(gpu->device, &sampler);
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	buffer_info.size =
		sizeof(struct mh_ui_vertex) * MH_UI_MAX_QUADS * QUAD_VERTICES;
	gpu->ui_vertices = SDL_CreateGPUBuffer(gpu->device, &buffer_info);
	memset(&transfer_info, 0, sizeof(transfer_info));
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = buffer_info.size;
	gpu->ui_transfer =
		SDL_CreateGPUTransferBuffer(gpu->device, &transfer_info);
	gpu->ui_texture = mh_gpu_create_texture(gpu, atlas->image.width,
		atlas->image.height, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		SDL_GPU_TEXTUREUSAGE_SAMPLER);

	if (gpu->ui_sampler == NULL || gpu->ui_vertices == NULL ||
		gpu->ui_transfer == NULL || gpu->ui_texture == NULL)
		return 0;

	bytes = atlas->image.width * atlas->image.height * MH_GPU_PIXEL_BYTES;
	transfer_info.size = bytes;
	upload = SDL_CreateGPUTransferBuffer(gpu->device, &transfer_info);

	if (upload == NULL)
		return 0;

	mapped = SDL_MapGPUTransferBuffer(gpu->device, upload, false);

	if (mapped == NULL) {
		SDL_ReleaseGPUTransferBuffer(gpu->device, upload);
		return 0;
	}

	memcpy(mapped, atlas->image.rgba, bytes);
	SDL_UnmapGPUTransferBuffer(gpu->device, upload);
	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL) {
		SDL_ReleaseGPUTransferBuffer(gpu->device, upload);
		return 0;
	}

	copy_pass = SDL_BeginGPUCopyPass(command);

	if (copy_pass == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		SDL_ReleaseGPUTransferBuffer(gpu->device, upload);
		return 0;
	}

	memset(&source, 0, sizeof(source));
	source.transfer_buffer = upload;
	memset(&destination, 0, sizeof(destination));
	destination.texture = gpu->ui_texture;
	destination.w = atlas->image.width;
	destination.h = atlas->image.height;
	destination.d = 1;
	SDL_UploadToGPUTexture(copy_pass, &source, &destination, false);
	SDL_EndGPUCopyPass(copy_pass);
	submitted = SDL_SubmitGPUCommandBuffer(command);
	SDL_ReleaseGPUTransferBuffer(gpu->device, upload);

	return submitted;
}

int
mh_gpu_ui_upload_vertices(
	struct mh_gpu_renderer *gpu, const struct mh_ui_list *ui)
{
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *copy_pass;
	SDL_GPUTransferBufferLocation source;
	SDL_GPUBufferRegion destination;
	void *mapped;

	if (ui->text_count != 0 || ui->text_size != 0)
		return SDL_SetError(
			"word-font UI commands require the software profile");

	if (gpu->ui_pipeline == NULL || gpu->ui_texture == NULL ||
		gpu->ui_vertices == NULL || gpu->ui_transfer == NULL ||
		ui->count > MH_UI_MAX_QUADS * QUAD_VERTICES ||
		ui->count % QUAD_VERTICES != 0 || ui->width != gpu->width ||
		ui->height != gpu->height)
		return SDL_SetError(
			"invalid UI frame or uninitialized overlay");

	if (ui->count == 0)
		return 1;

	mapped = SDL_MapGPUTransferBuffer(gpu->device, gpu->ui_transfer, true);

	if (mapped == NULL)
		return 0;

	memcpy(mapped, ui->vertices, ui->count * sizeof(struct mh_ui_vertex));
	SDL_UnmapGPUTransferBuffer(gpu->device, gpu->ui_transfer);
	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL)
		return 0;

	copy_pass = SDL_BeginGPUCopyPass(command);

	if (copy_pass == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		return 0;
	}

	memset(&source, 0, sizeof(source));
	source.transfer_buffer = gpu->ui_transfer;
	memset(&destination, 0, sizeof(destination));
	destination.buffer = gpu->ui_vertices;
	destination.size = ui->count * sizeof(struct mh_ui_vertex);
	SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
	SDL_EndGPUCopyPass(copy_pass);

	return SDL_SubmitGPUCommandBuffer(command);
}

int
mh_gpu_ui_draw(struct mh_gpu_renderer *gpu, SDL_GPUCommandBuffer *command,
	SDL_GPUColorTargetInfo *color, const struct mh_ui_list *ui)
{
	SDL_GPURenderPass *pass;
	SDL_GPUBufferBinding vertex_binding;
	SDL_GPUTextureSamplerBinding texture_binding;
	float viewport[4];

	color->load_op = SDL_GPU_LOADOP_LOAD;
	color->cycle = false;
	pass = SDL_BeginGPURenderPass(command, color, 1, NULL);

	if (pass == NULL)
		return 0;

	SDL_BindGPUGraphicsPipeline(pass, gpu->ui_pipeline);
	viewport[0] = (float)ui->width;
	viewport[1] = (float)ui->height;
	viewport[2] = viewport[3] = 0;
	SDL_PushGPUVertexUniformData(command, 0, viewport, 4 * sizeof(float));
	vertex_binding.buffer = gpu->ui_vertices;
	vertex_binding.offset = 0;
	SDL_BindGPUVertexBuffers(pass, 0, &vertex_binding, 1);
	texture_binding.texture = gpu->ui_texture;
	texture_binding.sampler = gpu->ui_sampler;
	SDL_BindGPUFragmentSamplers(pass, 0, &texture_binding, 1);
	SDL_DrawGPUPrimitives(pass, ui->count, 1, 0, 0);
	SDL_EndGPURenderPass(pass);

	return 1;
}
