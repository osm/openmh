#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_glenz.h"
#include "backends/render/sdl_gpu_renderer_internal.h"

#define COORDINATE_LIMIT 1000000.0f

static enum mh_error_code
upload_coordinates(struct mh_gpu_renderer *gpu, struct mh_gpu_mesh *model,
	const struct mh_renderer_glenz *glenz, struct mh_error *error_out)
{
	SDL_GPUBufferCreateInfo buffer;
	SDL_GPUTransferBufferCreateInfo transfer;
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *copy;
	SDL_GPUTransferBufferLocation source;
	SDL_GPUBufferRegion destination;
	void *mapped;
	Uint32 bytes;

	bytes = (Uint32)(glenz->count * sizeof(*glenz->coordinates));

	if (model->glenz_coordinates == NULL)
		model->glenz_coordinates =
			(struct mh_glenz_coordinates *)malloc(bytes);

	if (model->glenz_coordinates == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot retain Glenz coordinates");

	if (model->glenz_vertices == NULL) {
		memset(&buffer, 0, sizeof(buffer));
		buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
		buffer.size = bytes;
		model->glenz_vertices =
			SDL_CreateGPUBuffer(gpu->device, &buffer);
	}

	if (model->glenz_upload == NULL) {
		memset(&transfer, 0, sizeof(transfer));
		transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transfer.size = bytes;
		model->glenz_upload =
			SDL_CreateGPUTransferBuffer(gpu->device, &transfer);
	}

	if (model->glenz_vertices == NULL || model->glenz_upload == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED, SDL_GetError());

	mapped = SDL_MapGPUTransferBuffer(
		gpu->device, model->glenz_upload, true);

	if (mapped == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED, SDL_GetError());

	memcpy(mapped, glenz->coordinates, bytes);
	SDL_UnmapGPUTransferBuffer(gpu->device, model->glenz_upload);
	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL)
		return mh_error_set(error_out, MH_ERROR_FAILED, SDL_GetError());

	copy = SDL_BeginGPUCopyPass(command);

	if (copy == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		return mh_error_set(error_out, MH_ERROR_FAILED, SDL_GetError());
	}

	memset(&source, 0, sizeof(source));
	source.transfer_buffer = model->glenz_upload;
	memset(&destination, 0, sizeof(destination));
	destination.buffer = model->glenz_vertices;
	destination.size = bytes;
	SDL_UploadToGPUBuffer(copy, &source, &destination, true);
	SDL_EndGPUCopyPass(copy);

	if (!SDL_SubmitGPUCommandBuffer(command))
		return mh_error_set(error_out, MH_ERROR_FAILED, SDL_GetError());

	model->glenz_reflection = glenz->reflection_texture;
	model->glenz_phong = glenz->phong_texture;
	model->glenz = 1;
	memcpy(model->glenz_coordinates, glenz->coordinates, bytes);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_gpu_mesh_glenz(struct mh_renderer *renderer, size_t index,
	const struct mh_renderer_glenz *glenz, struct mh_error *error_out)
{
	struct mh_gpu_renderer *gpu;
	struct mh_gpu_mesh *model;
	size_t vertex;
	unsigned int axis;
	float reflection, phong;

	gpu = (struct mh_gpu_renderer *)renderer;

	if (index >= gpu->mesh_count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Glenz mesh");

	model = &gpu->meshes[index];

	if (glenz == NULL) {
		model->glenz = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (gpu->powervr)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"PowerVR does not support Glenz materials");

	if (glenz->coordinates == NULL || glenz->count == 0 ||
		glenz->count != model->mesh->vertex_count ||
		glenz->count > MH_U32_MAX / sizeof(*glenz->coordinates) ||
		glenz->reflection_texture >= model->mesh->texture_count ||
		glenz->phong_texture >= model->mesh->texture_count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid Glenz material");

	for (vertex = 0; vertex < glenz->count; vertex++) {
		for (axis = 0; axis < 2; axis++) {
			reflection =
				glenz->coordinates[vertex].reflection[axis];
			phong = glenz->coordinates[vertex].phong[axis];

			if (!(reflection >= -COORDINATE_LIMIT &&
				reflection <= COORDINATE_LIMIT) ||
				!(phong >= 0 && phong <= 1))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid Glenz coordinates");
		}
	}

	return upload_coordinates(gpu, model, glenz, error_out);
}
