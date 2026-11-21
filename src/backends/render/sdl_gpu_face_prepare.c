#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_face_prepare.h"
#include "backends/render/sdl_gpu_material_queue.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "engine/render/scene_material_face.h"

enum {
	PREPARED_INITIAL_CAPACITY = 1024
};

#define PREPARED_STRIDE                                                        \
	(sizeof(struct mh_vertex) + sizeof(struct mh_glenz_coordinates))

static int
reserve_vertices(struct mh_gpu_renderer *gpu, size_t count)
{
	struct mh_vertex *vertices;
	struct mh_glenz_coordinates *glenz;
	size_t capacity, limit;

	if (count <= gpu->prepared_capacity)
		return 1;

	limit = MH_U32_MAX / PREPARED_STRIDE;

	if (count > limit)
		return SDL_SetError("prepared scene exceeds GPU buffer limit");

	capacity = gpu->prepared_capacity ? gpu->prepared_capacity
					: PREPARED_INITIAL_CAPACITY;

	while (capacity < count)
		capacity = capacity > limit / 2 ? limit : capacity * 2;

	vertices = (struct mh_vertex *)malloc(capacity * sizeof(*vertices));
	glenz = (struct mh_glenz_coordinates *)malloc(
		capacity * sizeof(*glenz));

	if (vertices == NULL || glenz == NULL) {
		free(vertices);
		free(glenz);
		return SDL_SetError("cannot allocate prepared scene");
	}

	if (gpu->prepared_count != 0) {
		memcpy(vertices, gpu->prepared_vertices,
			gpu->prepared_count * sizeof(*vertices));
		memcpy(glenz, gpu->prepared_glenz,
			gpu->prepared_count * sizeof(*glenz));
	}

	free(gpu->prepared_vertices);
	free(gpu->prepared_glenz);
	gpu->prepared_vertices = vertices;
	gpu->prepared_glenz = glenz;
	gpu->prepared_capacity = capacity;

	return 1;
}

static void
face_source(const struct mh_gpu_renderer *gpu, const struct mh_gpu_mesh *model,
	size_t draw, struct mh_material_face_source *source)
{
	const struct mh_gpu_image *image;
	size_t texture;

	memset(source, 0, sizeof(*source));
	source->mesh = model->mesh;
	source->positions = model->positions;
	source->colors = model->colors;
	source->material_rgb = model->material_rgb;
	source->glenz_coordinates = model->glenz_coordinates;
	source->effect = model->effect;
	source->glenz = model->glenz;
	source->powervr = gpu->powervr;
	source->fog_enabled = gpu->fog_enabled;
	texture = model->mesh->draws[draw].texture;

	if (gpu->powervr && model->images[texture] != NO_IMAGE) {
		image = &gpu->images[model->images[texture]];
		source->image = image->source;
		source->texture_width = image->width;
		source->texture_height = image->height;
	}
}

int
mh_gpu_face_prepare(struct mh_gpu_renderer *gpu, size_t model_index,
	size_t draw_index, size_t first, size_t count, unsigned int type,
	const float camera_matrix[16], const struct mh_projection *projection,
	int reflections)
{
	struct mh_material_face_source source;
	struct mh_material_faces prepared;
	struct mh_error error;
	size_t start, expanded;
	unsigned int face;

	face_source(gpu, &gpu->meshes[model_index], draw_index, &source);

	if (mh_material_face_prepare(&source, draw_index, first, count, type,
		camera_matrix, projection, reflections, &prepared,
		&error) != MH_ERROR_NONE)
		return SDL_SetError("%s", error.message);

	/*
	 * Prepend the halves in reverse, preserving original within-face order.
	 */
	face = prepared.count;

	while (face != 0) {
		face--;
		start = gpu->prepared_count;
		expanded = prepared.faces[face].count == 4 ? 6 : 3;

		if (!reserve_vertices(gpu, start + expanded))
			return 0;

		mh_material_face_vertices(&source, &prepared, face, projection,
			gpu->prepared_vertices + start,
			gpu->prepared_glenz + start);
		gpu->prepared_count += expanded;

		if (!mh_gpu_queue_material(gpu, model_index, draw_index, start,
			expanded, prepared.lists[face],
			prepared.near_clipped, 1))
			return 0;
	}

	return 1;
}

static int
reserve_upload(struct mh_gpu_renderer *gpu)
{
	SDL_GPUBufferCreateInfo buffer;
	SDL_GPUTransferBufferCreateInfo transfer;
	SDL_GPUBuffer *vertices;
	SDL_GPUTransferBuffer *upload;

	if (gpu->prepared_capacity > gpu->prepared_buffer_capacity) {
		memset(&buffer, 0, sizeof(buffer));
		buffer.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
		buffer.size =
			(Uint32)(gpu->prepared_capacity * PREPARED_STRIDE);
		memset(&transfer, 0, sizeof(transfer));
		transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		transfer.size = buffer.size;
		vertices = SDL_CreateGPUBuffer(gpu->device, &buffer);
		upload = SDL_CreateGPUTransferBuffer(gpu->device, &transfer);

		if (vertices == NULL || upload == NULL) {
			if (vertices != NULL)
				SDL_ReleaseGPUBuffer(gpu->device, vertices);

			if (upload != NULL)
				SDL_ReleaseGPUTransferBuffer(
					gpu->device, upload);

			return 0;
		}

		if (gpu->prepared_buffer != NULL)
			SDL_ReleaseGPUBuffer(gpu->device, gpu->prepared_buffer);

		if (gpu->prepared_upload != NULL)
			SDL_ReleaseGPUTransferBuffer(
				gpu->device, gpu->prepared_upload);

		gpu->prepared_buffer = vertices;
		gpu->prepared_upload = upload;
		gpu->prepared_buffer_capacity = gpu->prepared_capacity;
	}

	return 1;
}

int
mh_gpu_upload_prepared(struct mh_gpu_renderer *gpu)
{
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *copy;
	SDL_GPUTransferBufferLocation source;
	SDL_GPUBufferRegion destination;
	unsigned char *mapped;
	Uint32 bytes, vertex_bytes;

	if (gpu->prepared_count == 0)
		return 1;

	if (!reserve_upload(gpu))
		return 0;

	mapped = (unsigned char *)SDL_MapGPUTransferBuffer(
		gpu->device, gpu->prepared_upload, true);

	if (mapped == NULL)
		return 0;

	vertex_bytes = (Uint32)(gpu->prepared_count * sizeof(struct mh_vertex));
	bytes = (Uint32)(gpu->prepared_count * PREPARED_STRIDE);
	memcpy(mapped, gpu->prepared_vertices, vertex_bytes);
	memcpy(mapped + vertex_bytes, gpu->prepared_glenz,
		bytes - vertex_bytes);
	SDL_UnmapGPUTransferBuffer(gpu->device, gpu->prepared_upload);
	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL)
		return 0;

	copy = SDL_BeginGPUCopyPass(command);

	if (copy == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		return 0;
	}

	memset(&source, 0, sizeof(source));
	source.transfer_buffer = gpu->prepared_upload;
	memset(&destination, 0, sizeof(destination));
	destination.buffer = gpu->prepared_buffer;
	destination.size = bytes;
	SDL_UploadToGPUBuffer(copy, &source, &destination, true);
	SDL_EndGPUCopyPass(copy);

	return SDL_SubmitGPUCommandBuffer(command);
}
