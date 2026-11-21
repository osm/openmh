#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_readback.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "engine/assets/tga.h"
#include "engine/core/error.h"

enum {
	DEPTH_PROBES_MAX = 100000,
	DEPTH_PROBE_STRIDE = 512,
	DEPTH_ROW_PIXELS = 64,
	CAPTURE_ROW_ALIGNMENT = 256
};

int
mh_gpu_renderer_depth_read(
	struct mh_gpu_renderer *gpu, unsigned char *visible, size_t count)
{
	const unsigned char *mapped;
	float depth;
	size_t i;

	if (gpu == NULL || (count != 0 && visible == NULL))
		return SDL_SetError("invalid visibility output");

	if (gpu->depth_fence == NULL)
		return 1;

	if (count != gpu->depth_count)
		return SDL_SetError("visibility history size changed");

	if (!SDL_WaitForGPUFences(gpu->device, true, &gpu->depth_fence, 1))
		return 0;

	mapped = (const unsigned char *)SDL_MapGPUTransferBuffer(
		gpu->device, gpu->depth_transfer, false);

	if (mapped == NULL)
		return 0;

	for (i = 0; i < count; i++)
		if (gpu->depth_probes[i].active) {
			memcpy(&depth, mapped + i * DEPTH_PROBE_STRIDE,
				sizeof(depth));

			/*
			 * Original reversed depth: sampled >= source*65536
			 * hides it. Forward float depth retains the comparison
			 * direction; it does not emulate the original 16-bit
			 * depth-buffer quantization.
			 */
			visible[i] =
				(unsigned char)(gpu->depth_probes[i].depth <
					depth);
		}

	SDL_UnmapGPUTransferBuffer(gpu->device, gpu->depth_transfer);
	SDL_ReleaseGPUFence(gpu->device, gpu->depth_fence);
	gpu->depth_fence = NULL;

	return 1;
}

int
mh_gpu_renderer_depth_queue(struct mh_gpu_renderer *gpu,
	const struct mh_depth_probe *probes, size_t count)
{
	SDL_GPUTransferBufferCreateInfo info;
	SDL_GPUTransferBuffer *transfer;
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *pass;
	SDL_GPUTextureRegion source;
	SDL_GPUTextureTransferInfo destination;
	struct mh_depth_probe *probes_copy;
	const struct mh_depth_probe *query;
	size_t i;
	int has_active_probe;

	if (gpu == NULL || count > DEPTH_PROBES_MAX ||
		(count != 0 && probes == NULL))
		return SDL_SetError("invalid depth queries");

	if (gpu->depth_fence != NULL)
		return SDL_SetError("unread depth queries");

	has_active_probe = 0;

	for (i = 0; i < count; i++) {
		query = &probes[i];

		if (query->active != 0 && query->active != 1)
			return SDL_SetError("invalid depth query flag");

		if (!query->active)
			continue;

		if (!(query->x > 0 && query->x < gpu->width && query->y > 0 &&
			query->y < gpu->height && query->depth >= 0 &&
			query->depth <= 1))
			return SDL_SetError("invalid depth query position");

		has_active_probe = 1;
	}

	if (!has_active_probe)
		return 1;

	if (count > gpu->depth_capacity) {
		probes_copy = (struct mh_depth_probe *)malloc(
			count * sizeof(*probes_copy));

		if (probes_copy == NULL)
			return SDL_SetError("out of memory for depth queries");

		memset(&info, 0, sizeof(info));
		info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;

		/* Portable aligned one-pixel rows. */
		info.size = (Uint32)(count * DEPTH_PROBE_STRIDE);
		transfer = SDL_CreateGPUTransferBuffer(gpu->device, &info);

		if (transfer == NULL) {
			free(probes_copy);
			return 0;
		}

		if (gpu->depth_transfer != NULL)
			SDL_ReleaseGPUTransferBuffer(
				gpu->device, gpu->depth_transfer);

		free(gpu->depth_probes);
		gpu->depth_probes = probes_copy;
		gpu->depth_transfer = transfer;
		gpu->depth_capacity = count;
	}

	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL)
		return 0;

	pass = SDL_BeginGPUCopyPass(command);

	if (pass == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		return 0;
	}

	memset(&source, 0, sizeof(source));
	source.texture = gpu->depth;
	source.w = source.h = source.d = 1;
	memset(&destination, 0, sizeof(destination));
	destination.transfer_buffer = gpu->depth_transfer;
	destination.pixels_per_row = DEPTH_ROW_PIXELS;
	destination.rows_per_layer = 1;

	for (i = 0; i < count; i++)
		if (probes[i].active) {
			source.x = (Uint32)probes[i].x;
			source.y = (Uint32)probes[i].y;
			destination.offset = (Uint32)(i * DEPTH_PROBE_STRIDE);
			SDL_DownloadFromGPUTexture(pass, &source, &destination);
		}

	SDL_EndGPUCopyPass(pass);
	gpu->depth_fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);

	if (gpu->depth_fence == NULL)
		return 0;

	memcpy(gpu->depth_probes, probes, count * sizeof(*probes));
	gpu->depth_count = count;

	return 1;
}

static int
encode_tga(const unsigned char *rgba, unsigned int width, unsigned int height,
	unsigned int pitch, struct mh_file *output)
{
	struct mh_picture picture;
	unsigned char *packed;
	const unsigned char *source, *pixel;
	struct mh_error error;
	enum mh_error_code status;
	size_t offset;
	unsigned int x, y, rgb555;

	if (width == 0 || height == 0 ||
		(size_t)width > MH_TGA_MAX_PIXELS / height)
		return SDL_SetError("invalid screenshot dimensions");

	packed = (unsigned char *)malloc((size_t)width * height * 2);

	if (packed == NULL)
		return SDL_SetError("out of memory preparing screenshot");

	offset = 0;

	for (y = 0; y < height; y++) {
		source = rgba + (size_t)y * pitch;

		for (x = 0; x < width; x++) {
			pixel = source + x * MH_GPU_PIXEL_BYTES;
			rgb555 = ((unsigned int)(pixel[0] >> 3) << 10) |
				((unsigned int)(pixel[1] >> 3) << 5) |
				(pixel[2] >> 3);
			packed[offset++] = (unsigned char)rgb555;
			packed[offset++] = (unsigned char)(rgb555 >> 8);
		}
	}

	memset(&picture, 0, sizeof(picture));
	picture.format = MH_PICTURE_RGB555;
	picture.width = width;
	picture.height = height;
	picture.pitch = (size_t)width * 2;
	picture.pixels = packed;
	picture.pixel_size = offset;
	status = mh_image_tga_encode(&picture, output, &error);
	free(packed);

	if (status != MH_ERROR_NONE)
		return SDL_SetError("%s", error.message);

	return 1;
}

static int
copy_pixels(const unsigned char *pixels, unsigned int width,
	unsigned int height, unsigned int pitch, struct mh_image *output)
{
	struct mh_image image;
	size_t row_bytes;
	unsigned int y;

	if (width == 0 || height == 0 ||
		(size_t)width > MH_TGA_MAX_PIXELS / height)
		return SDL_SetError("invalid screenshot dimensions");

	row_bytes = (size_t)width * MH_GPU_PIXEL_BYTES;
	image.width = width;
	image.height = height;
	image.rgba = malloc(row_bytes * height);

	if (image.rgba == NULL)
		return SDL_SetError("out of memory reading GPU pixels");

	for (y = 0; y < height; y++)
		memcpy(image.rgba + (size_t)y * row_bytes,
			pixels + (size_t)y * pitch, row_bytes);

	*output = image;

	return 1;
}

static int
capture_pixels(struct mh_gpu_renderer *gpu, void *pixels, unsigned int pitch,
	const char *path, struct mh_file *picture, struct mh_image *image)
{
	SDL_Surface *surface;
	int success;

	if (image != NULL)
		return copy_pixels(
			pixels, gpu->width, gpu->height, pitch, image);

	if (picture != NULL)
		return encode_tga((const unsigned char *)pixels, gpu->width,
			gpu->height, pitch, picture);

	surface = SDL_CreateSurfaceFrom(gpu->width, gpu->height,
		SDL_PIXELFORMAT_RGBA32, pixels, (int)pitch);

	if (surface == NULL)
		return 0;

	success = SDL_SaveBMP(surface, path);
	SDL_DestroySurface(surface);

	return success;
}

static int
capture_frame(struct mh_gpu_renderer *gpu, const char *path,
	struct mh_file *picture, struct mh_image *image)
{
	SDL_GPUTransferBufferCreateInfo info;
	SDL_GPUTransferBuffer *transfer;
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *pass;
	SDL_GPUTextureRegion source;
	SDL_GPUTextureTransferInfo destination;
	SDL_GPUFence *fence;
	void *pixels;
	int success;
	unsigned int pitch;

	if (gpu == NULL || (path == NULL && picture == NULL && image == NULL))
		return SDL_SetError("invalid frame capture");

	pitch = (gpu->width * MH_GPU_PIXEL_BYTES +
			(CAPTURE_ROW_ALIGNMENT - 1U)) &
		~(CAPTURE_ROW_ALIGNMENT - 1U);
	memset(&info, 0, sizeof(info));
	info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	info.size = pitch * gpu->height;
	transfer = SDL_CreateGPUTransferBuffer(gpu->device, &info);

	if (transfer == NULL) {
		return 0;
	}

	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL) {
		SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);
		return 0;
	}

	pass = SDL_BeginGPUCopyPass(command);

	if (pass == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);
		return 0;
	}

	memset(&source, 0, sizeof(source));
	source.texture = gpu->color;
	source.w = gpu->width;
	source.h = gpu->height;
	source.d = 1;
	memset(&destination, 0, sizeof(destination));
	destination.transfer_buffer = transfer;
	destination.pixels_per_row = pitch / MH_GPU_PIXEL_BYTES;
	destination.rows_per_layer = gpu->height;
	SDL_DownloadFromGPUTexture(pass, &source, &destination);
	SDL_EndGPUCopyPass(pass);
	fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
	success = 0;

	if (fence != NULL) {
		if (SDL_WaitForGPUFences(gpu->device, true, &fence, 1)) {
			pixels = SDL_MapGPUTransferBuffer(
				gpu->device, transfer, false);

			if (pixels != NULL) {
				success = capture_pixels(gpu, pixels, pitch,
					path, picture, image);

				SDL_UnmapGPUTransferBuffer(
					gpu->device, transfer);
			}
		}

		SDL_ReleaseGPUFence(gpu->device, fence);
	}

	SDL_ReleaseGPUTransferBuffer(gpu->device, transfer);

	return success;
}

int
mh_gpu_renderer_capture(struct mh_gpu_renderer *gpu, const char *path)
{
	return capture_frame(gpu, path, NULL, NULL);
}

int
mh_gpu_renderer_tga_capture(struct mh_gpu_renderer *gpu, struct mh_file *output)
{
	return capture_frame(gpu, NULL, output, NULL);
}

int
mh_gpu_renderer_read_pixels(
	struct mh_gpu_renderer *gpu, struct mh_image *output)
{
	return capture_frame(gpu, NULL, NULL, output);
}
