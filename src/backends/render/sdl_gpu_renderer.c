#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_device.h"
#include "backends/render/sdl_gpu_filter.h"
#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_pipelines.h"
#include "backends/render/sdl_gpu_powervr_order.h"
#include "backends/render/sdl_gpu_provider.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_retro.h"
#include "backends/render/sdl_gpu_shaders.h"
#include "backends/render/sdl_gpu_tron.h"
#include "engine/core/error.h"

enum {
	SCENE_MESHES_MAX = 128
};

#define CAMERA_FAR_MIN 60.0f
#define CAMERA_FAR_MAX 300.0f

static void
destroy_mesh(struct mh_gpu_renderer *gpu, struct mh_gpu_mesh *model)
{
	if (gpu->device != NULL) {
		if (model->glenz_vertices != NULL)
			SDL_ReleaseGPUBuffer(
				gpu->device, model->glenz_vertices);

		if (model->glenz_upload != NULL)
			SDL_ReleaseGPUTransferBuffer(
				gpu->device, model->glenz_upload);

		if (model->vertices != NULL)
			SDL_ReleaseGPUBuffer(gpu->device, model->vertices);

		if (model->vertex_upload != NULL)
			SDL_ReleaseGPUTransferBuffer(
				gpu->device, model->vertex_upload);
	}

	if (MH_RENDER_BUILD_OPENMH)
		mh_openmh_light_buffer_free(&model->openmh);

	free(model->images);
	free(model->positions);
	free(model->colors);
	free(model->projection_w);
	free(model->material_rgb);
	free(model->glenz_coordinates);
}

void
mh_gpu_renderer_destroy(struct mh_gpu_renderer *gpu)
{
	size_t i;

	if (gpu == NULL)
		return;

	if (gpu->device != NULL)
		SDL_WaitForGPUIdle(gpu->device);

	for (i = 0; i < gpu->mesh_count; i++)
		destroy_mesh(gpu, &gpu->meshes[i]);

	if (MH_RENDER_BUILD_POWERVR)
		mh_gpu_powervr_order_destroy(gpu);

	if (gpu->device != NULL) {
		if (gpu->prepared_buffer != NULL)
			SDL_ReleaseGPUBuffer(gpu->device, gpu->prepared_buffer);

		if (gpu->prepared_upload != NULL)
			SDL_ReleaseGPUTransferBuffer(
				gpu->device, gpu->prepared_upload);

		for (i = 0; i < gpu->image_count; i++)
			if (gpu->images[i].texture != NULL)
				SDL_ReleaseGPUTexture(
					gpu->device, gpu->images[i].texture);

		mh_gpu_tron_destroy(gpu->tron);
		mh_gpu_retro_destroy(gpu->retro);

		if (gpu->depth_fence != NULL)
			SDL_ReleaseGPUFence(gpu->device, gpu->depth_fence);

		if (gpu->depth_transfer != NULL)
			SDL_ReleaseGPUTransferBuffer(
				gpu->device, gpu->depth_transfer);

		if (gpu->ui_transfer != NULL)
			SDL_ReleaseGPUTransferBuffer(
				gpu->device, gpu->ui_transfer);

		if (gpu->ui_vertices != NULL)
			SDL_ReleaseGPUBuffer(gpu->device, gpu->ui_vertices);

		if (gpu->ui_texture != NULL)
			SDL_ReleaseGPUTexture(gpu->device, gpu->ui_texture);

		if (gpu->ui_sampler != NULL)
			SDL_ReleaseGPUSampler(gpu->device, gpu->ui_sampler);

		if (gpu->ui_pipeline != NULL)
			SDL_ReleaseGPUGraphicsPipeline(
				gpu->device, gpu->ui_pipeline);

		if (gpu->color != NULL)
			SDL_ReleaseGPUTexture(gpu->device, gpu->color);

		if (gpu->back_color != NULL)
			SDL_ReleaseGPUTexture(gpu->device, gpu->back_color);

		if (gpu->third_color != NULL)
			SDL_ReleaseGPUTexture(gpu->device, gpu->third_color);

		if (gpu->depth != NULL)
			SDL_ReleaseGPUTexture(gpu->device, gpu->depth);

		if (gpu->sampler != NULL)
			SDL_ReleaseGPUSampler(gpu->device, gpu->sampler);

		if (gpu->sky_sampler != NULL)
			SDL_ReleaseGPUSampler(gpu->device, gpu->sky_sampler);

		if (gpu->repeat_sampler != NULL)
			SDL_ReleaseGPUSampler(gpu->device, gpu->repeat_sampler);

		for (i = 0; i < 3; i++) {
			if (gpu->filter_samplers[i] != NULL)
				SDL_ReleaseGPUSampler(
					gpu->device, gpu->filter_samplers[i]);

			if (gpu->repeat_filters[i] != NULL)
				SDL_ReleaseGPUSampler(
					gpu->device, gpu->repeat_filters[i]);
		}

		if (gpu->alpha_sampler != NULL)
			SDL_ReleaseGPUSampler(gpu->device, gpu->alpha_sampler);

		mh_gpu_destroy_pipelines(gpu);

		if (gpu->window_claimed)
			SDL_ReleaseWindowFromGPUDevice(
				gpu->device, gpu->window);

		SDL_DestroyGPUDevice(gpu->device);
	}

	free(gpu->meshes);
	free(gpu->images);
	free(gpu->parts);
	free(gpu->depth_probes);
	free(gpu->primitives);
	free(gpu->prepared_vertices);
	free(gpu->prepared_glenz);
	mh_tron_scene_destroy(&gpu->tron_scene);
	free(gpu);
}

SDL_GPUTexture *
mh_gpu_create_texture_levels(struct mh_gpu_renderer *gpu, Uint32 width,
	Uint32 height, SDL_GPUTextureFormat format,
	SDL_GPUTextureUsageFlags usage, unsigned int levels)
{
	SDL_GPUTextureCreateInfo info;

	memset(&info, 0, sizeof(info));
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = format;
	info.usage = usage;
	info.width = width;
	info.height = height;
	info.layer_count_or_depth = 1;
	info.num_levels = levels;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;

	return SDL_CreateGPUTexture(gpu->device, &info);
}

SDL_GPUTexture *
mh_gpu_create_texture(struct mh_gpu_renderer *gpu, Uint32 width, Uint32 height,
	SDL_GPUTextureFormat format, SDL_GPUTextureUsageFlags usage)
{
	return mh_gpu_create_texture_levels(
		gpu, width, height, format, usage, 1);
}

static SDL_GPUTexture *
create_history_target(struct mh_gpu_renderer *gpu)
{
	return mh_gpu_create_texture(gpu, gpu->width, gpu->height,
		SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
			SDL_GPU_TEXTUREUSAGE_SAMPLER);
}

enum mh_error_code
mh_gpu_set_blur_history(struct mh_renderer *renderer, unsigned int frames,
	struct mh_error *error_out)
{
	struct mh_gpu_renderer *gpu;
	SDL_GPUTexture *back, *third;

	gpu = (struct mh_gpu_renderer *)renderer;

	if (gpu->powervr)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"PowerVR has no retained blur history");

	back = gpu->back_color;
	third = gpu->third_color;

	if (back != NULL && (frames == 3) == (third != NULL))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (back == NULL)
		back = create_history_target(gpu);

	if (back != NULL && frames == 3 && third == NULL)
		third = create_history_target(gpu);

	if (back == NULL || (frames == 3 && third == NULL)) {
		if (back != NULL && gpu->back_color == NULL)
			SDL_ReleaseGPUTexture(gpu->device, back);

		return mh_error_set(
			error_out, MH_ERROR_BACKEND, SDL_GetError());
	}

	if (frames == 2 && third != NULL) {
		SDL_ReleaseGPUTexture(gpu->device, third);
		third = NULL;
	}

	gpu->back_color = back;
	gpu->third_color = third;
	gpu->color_ready = gpu->back_color_ready = gpu->third_color_ready = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_gpu_renderer_set_motion_blur(struct mh_gpu_renderer *gpu, int enabled)
{
	SDL_GPUTexture *texture;

	if (gpu == NULL || (enabled != 0 && enabled != 1))
		return SDL_SetError("invalid motion blur setting");

	/* PC 3.1 stores the request, but PowerSGL never applies it. */
	if (gpu->powervr)
		return 1;

	if (enabled && gpu->back_color == NULL) {
		texture = create_history_target(gpu);

		if (texture == NULL)
			return 0;

		gpu->back_color = texture;
	}

	gpu->motion_blur = enabled;

	return 1;
}

static void
print_diagnostics(const struct mh_gpu_renderer *gpu)
{
	int version;
#if SDL_VERSION_ATLEAST(3, 4, 0)
	SDL_PropertiesID properties;
#endif

	version = SDL_GetVersion();
	fprintf(stderr,
		"SDL runtime %d.%d.%d; compiled %d.%d.%d; video %s; GPU backend %s\n",
		SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
		SDL_VERSIONNUM_MICRO(version), SDL_MAJOR_VERSION,
		SDL_MINOR_VERSION, SDL_MICRO_VERSION,
		SDL_GetCurrentVideoDriver() == NULL
			? "none"
			: SDL_GetCurrentVideoDriver(),
		gpu->device == NULL ? "unavailable"
				: SDL_GetGPUDeviceDriver(gpu->device));

#if SDL_VERSION_ATLEAST(3, 4, 0)
	if (gpu->device != NULL) {
		properties = SDL_GetGPUDeviceProperties(gpu->device);
		fprintf(stderr, "GPU device: %s; driver: %s; version: %s\n",
			SDL_GetStringProperty(properties,
				SDL_PROP_GPU_DEVICE_NAME_STRING, "unknown"),
			SDL_GetStringProperty(properties,
				SDL_PROP_GPU_DEVICE_DRIVER_NAME_STRING,
				"unknown"),
			SDL_GetStringProperty(properties,
				SDL_PROP_GPU_DEVICE_DRIVER_VERSION_STRING,
				"unknown"));
	}
#endif
}

static struct mh_gpu_renderer *
open_renderer(SDL_Window *window, const struct mh_mesh *const *meshes,
	size_t count, unsigned int width, unsigned int height, int powervr,
	int verbose)
{
	struct mh_gpu_renderer *gpu;
	SDL_GPUSamplerCreateInfo sampler;
	SDL_GPUTransferBufferCreateInfo transfer_info;
	SDL_GPUTransferBuffer *texture_upload;
	Uint32 upload_size;
	char error_text[MH_RENDERER_ERROR_SIZE];
	size_t i;

	if (width < MH_VIEW_MIN_WIDTH || width > MH_VIEW_MAX_WIDTH ||
		height < MH_VIEW_MIN_HEIGHT || height > MH_VIEW_MAX_HEIGHT) {
		SDL_SetError("viewport outside native rendering bounds");
		return NULL;
	}

	if ((meshes == NULL && count != 0) || count > SCENE_MESHES_MAX) {
		SDL_SetError("invalid scene meshes");
		return NULL;
	}

	for (i = 0; i < count; i++) {
		if (meshes[i] == NULL || meshes[i]->vertex_count == 0 ||
			meshes[i]->texture_count == 0) {
			SDL_SetError("empty scene mesh");
			return NULL;
		}
	}

	texture_upload = NULL;
	upload_size = 0;
	gpu = (struct mh_gpu_renderer *)calloc(1, sizeof(*gpu));

	if (gpu == NULL) {
		SDL_SetError("out of memory creating renderer");
		return NULL;
	}

	gpu->base.backend = MH_RENDER_BUILD_POWERVR && powervr
		? mh_sdl_gpu_powervr_backend()
		: mh_sdl_gpu_glide_backend();

	gpu->powervr = powervr;
	gpu->width = width;
	gpu->height = height;
	gpu->meshes = (struct mh_gpu_mesh *)calloc(
		count == 0 ? 1 : count, sizeof(*gpu->meshes));

	if (gpu->meshes == NULL) {
		free(gpu);
		SDL_SetError("out of memory creating mesh table");
		return NULL;
	}

	gpu->mesh_count = count;

	for (i = 0; i < count; i++) {
		gpu->meshes[i].mesh = meshes[i];

		if (meshes[i]->vertex_count / 3 >
			(size_t)-1 - gpu->primitive_capacity) {
			SDL_SetError("too many material primitives");
			goto failure;
		}

		gpu->primitive_capacity += meshes[i]->vertex_count / 3;
	}

	if (gpu->primitive_capacity > (size_t)-1 / sizeof(*gpu->primitives)) {
		SDL_SetError("material lists are too large");
		goto failure;
	}

	gpu->primitives = (struct mh_gpu_primitive *)malloc(
		(gpu->primitive_capacity ? gpu->primitive_capacity : 1) *
		sizeof(*gpu->primitives));

	if (gpu->primitives == NULL) {
		SDL_SetError("out of memory creating material lists");
		goto failure;
	}

	gpu->window = window;

	/*
	 * Driver and shader-format selection stay behind the private GPU API.
	 */
	gpu->device = mh_gpu_device_create(verbose);

	if (gpu->device == NULL) {
		goto failure;
	}

	if (verbose)
		print_diagnostics(gpu);

	if (window != NULL) {
		if (!SDL_ClaimWindowForGPUDevice(gpu->device, window)) {
			goto failure;
		}

		gpu->window_claimed = 1;
	}

	gpu->color = mh_gpu_create_texture(gpu, gpu->width, gpu->height,
		SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
		SDL_GPU_TEXTUREUSAGE_COLOR_TARGET |
			SDL_GPU_TEXTUREUSAGE_SAMPLER);
	gpu->depth = mh_gpu_create_texture(gpu, gpu->width, gpu->height,
		SDL_GPU_TEXTUREFORMAT_D32_FLOAT,
		SDL_GPU_TEXTUREUSAGE_DEPTH_STENCIL_TARGET);
	memset(&sampler, 0, sizeof(sampler));

	/* Glide opaque/environment batches use grTexFilterMode(TMU0,1,1). */
	sampler.min_filter = SDL_GPU_FILTER_LINEAR;
	sampler.mag_filter = SDL_GPU_FILTER_LINEAR;
	sampler.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	gpu->sampler = SDL_CreateGPUSampler(gpu->device, &sampler);
	sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	gpu->sky_sampler = SDL_CreateGPUSampler(gpu->device, &sampler);
	sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	gpu->repeat_sampler = SDL_CreateGPUSampler(gpu->device, &sampler);
	sampler.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

	/* Both distant and ordinary alpha batches explicitly select POINT. */
	sampler.min_filter = sampler.mag_filter = SDL_GPU_FILTER_NEAREST;
	gpu->alpha_sampler = SDL_CreateGPUSampler(gpu->device, &sampler);

	if (gpu->color == NULL || gpu->depth == NULL || gpu->sampler == NULL ||
		gpu->sky_sampler == NULL || gpu->repeat_sampler == NULL ||
		gpu->alpha_sampler == NULL || !mh_gpu_create_filters(gpu) ||
		!mh_gpu_create_pipelines(gpu)) {
		goto failure;
	}

	if (!mh_gpu_prepare_images(gpu, &upload_size, verbose))
		goto failure;

	if (count != 0) {
		memset(&transfer_info, 0, sizeof(transfer_info));
		transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transfer_info.size = upload_size;

		if (verbose)
			fprintf(stderr,
				"GPU startup texture transfer: %lu bytes, one reusable buffer\n",
				(unsigned long)upload_size);

		texture_upload = SDL_CreateGPUTransferBuffer(
			gpu->device, &transfer_info);

		if (texture_upload == NULL) {
			mh_gpu_error("create startup texture transfer buffer",
				0, 0, upload_size);
			goto failure;
		}
	}

	for (i = 0; i < count; i++) {
		if (!mh_gpu_upload_mesh(gpu, i, texture_upload))
			goto failure;
	}

	if (texture_upload != NULL)
		SDL_ReleaseGPUTransferBuffer(gpu->device, texture_upload);

	printf("Video: %s; GPU: %s\n", SDL_GetCurrentVideoDriver(),
		SDL_GetGPUDeviceDriver(gpu->device));
	return gpu;

failure:
	/* Diagnostics and cleanup can overwrite SDL's borrowed error text. */
	SDL_strlcpy(error_text, SDL_GetError(), sizeof(error_text));
	if (!verbose || gpu->device == NULL)
		print_diagnostics(gpu);

	if (texture_upload != NULL)
		SDL_ReleaseGPUTransferBuffer(gpu->device, texture_upload);

	mh_gpu_renderer_destroy(gpu);
	SDL_SetError("%s", error_text);

	return NULL;
}

struct mh_gpu_renderer *
mh_gpu_renderer_open(SDL_Window *window, const struct mh_mesh *const *meshes,
	size_t count, unsigned int width, unsigned int height, int diagnostics)
{
	return open_renderer(
		window, meshes, count, width, height, 0, diagnostics);
}

#if MH_RENDER_BUILD_POWERVR
struct mh_gpu_renderer *
mh_gpu_powervr_open(SDL_Window *window, const struct mh_mesh *const *meshes,
	size_t count, unsigned int width, unsigned int height, int diagnostics)
{
	return open_renderer(
		window, meshes, count, width, height, 1, diagnostics);
}
#endif

int
mh_gpu_renderer_set_fog(struct mh_gpu_renderer *gpu, const struct mh_fog *fog)
{
	unsigned int channel;

	if (gpu == NULL)
		return SDL_SetError("missing fog renderer");

	if (fog == NULL) {
		memset(&gpu->fog, 0, sizeof(gpu->fog));
		gpu->fog_enabled = 0;
		return 1;
	}

	if (!(fog->inverse_far >= 1.0f / CAMERA_FAR_MAX &&
		fog->inverse_far <= 1.0f / CAMERA_FAR_MIN))
		return SDL_SetError("invalid fog distance");

	for (channel = 0; channel < 3; channel++) {
		if (!(fog->color[channel] >= 0 && fog->color[channel] <= 1))
			return SDL_SetError("invalid fog color");
	}

	gpu->fog = *fog;
	gpu->fog_enabled = 1;

	return 1;
}

int
mh_gpu_renderer_set_water(
	struct mh_gpu_renderer *gpu, const struct mh_water_frame *frame)
{
	float xy[2];
	struct mh_error error;
	enum mh_error_code status;

	if (gpu == NULL)
		return SDL_SetError("missing underwater renderer");

	if (frame == NULL) {
		memset(&gpu->water, 0, sizeof(gpu->water));
		gpu->water_enabled = 0;
		return 1;
	}

	xy[0] = xy[1] = 0;
	status = mh_water_point(frame, xy, xy, &error);

	if (status != MH_ERROR_NONE)
		return SDL_SetError("%s", error.message);

	if (frame->width != gpu->width || frame->height != gpu->height)
		return SDL_SetError("underwater frame does not match viewport");

	gpu->water = *frame;
	gpu->water_enabled = 1;

	return 1;
}
