#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_mesh.h"
#include "backends/render/sdl_gpu_powervr_texture.h"
#include "backends/render/sdl_gpu_renderer.h"
#include "backends/render/sdl_gpu_renderer_internal.h"
#include "engine/render/powervr_material.h"
#include "engine/render/powervr_texture.h"

int
mh_gpu_error(const char *operation, size_t mesh, size_t item, Uint32 size)
{
	char error_text[MH_RENDERER_ERROR_SIZE];

	SDL_strlcpy(error_text, SDL_GetError(), sizeof(error_text));
	SDL_SetError("%s (mesh %lu, item %lu, %lu bytes): %s", operation,
		(unsigned long)mesh, (unsigned long)item, (unsigned long)size,
		error_text);

	return 0;
}

static int
prepare_mips(struct mh_gpu_renderer *gpu, Uint32 *maximum_upload)
{
	const struct mh_mesh *mesh;
	const struct mh_gpu_mesh *model;
	size_t model_index, i, image_index;
	unsigned int level, width, height;
	Uint32 bytes, *totals;

	/*
	 * Atlas pages are downsampled as pages, preserving their authored UVs.
	 * Environment and Glenz-only slots never request a mip chain.
	 */
	for (model_index = 0; model_index < gpu->mesh_count; model_index++) {
		model = &gpu->meshes[model_index];
		mesh = model->mesh;

		if (mesh->draw_count != 0 && mesh->draws == NULL)
			return SDL_SetError("missing GPU texture draws");

		for (i = 0; i < mesh->draw_count; i++) {
			if (mesh->draws[i].texture >= mesh->texture_count)
				return SDL_SetError("invalid GPU draw texture");

			if (gpu->powervr || mesh->draws[i].environment)
				continue;

			image_index = model->images[mesh->draws[i].texture];
			width = gpu->images[image_index].source->width;
			height = gpu->images[image_index].source->height;

			for (level = 1; level < MH_TEXTURE_MIP_LEVELS &&
				width >= 2 && height >= 2;
				level++) {
				width >>= 1;
				height >>= 1;
			}

			gpu->images[image_index].levels = level;
		}
	}

	totals = (Uint32 *)calloc(gpu->mesh_count, sizeof(*totals));

	if (totals == NULL)
		return SDL_SetError("out of memory planning mip uploads");

	*maximum_upload = 0;

	for (i = 0; i < gpu->image_count; i++) {
		model_index = gpu->images[i].owner;
		gpu->images[i].offset = totals[model_index];

		for (level = 0; level < gpu->images[i].levels; level++) {
			bytes = (Uint32)((size_t)(gpu->images[i].width >>
						level) *
				(gpu->images[i].height >> level) *
				MH_GPU_PIXEL_BYTES);

			if (bytes > MH_U32_MAX - totals[model_index]) {
				free(totals);
				return SDL_SetError(
					"mip images exceed GPU transfer size limit");
			}

			totals[model_index] += bytes;
		}

		if (totals[model_index] > *maximum_upload)
			*maximum_upload = totals[model_index];
	}

	free(totals);

	return 1;
}

int
mh_gpu_prepare_images(
	struct mh_gpu_renderer *gpu, Uint32 *maximum_upload, int verbose)
{
	const struct mh_image *image;
	const struct mh_mesh *mesh;
	struct mh_gpu_mesh *model;
	struct mh_gpu_image *shared;
	Uint32 total, bytes;
	size_t model_index, i, image_index, capacity, first;

	capacity = 0;

	for (model_index = 0; model_index < gpu->mesh_count; model_index++) {
		model = &gpu->meshes[model_index];
		mesh = model->mesh;

		if (mesh->draw_count != 0 && mesh->draws == NULL)
			return SDL_SetError("missing GPU texture draws");

		if (mesh->texture_count >
				(size_t)-1 / sizeof(*gpu->images) - capacity ||
			mesh->texture_count >
				(size_t)-1 / sizeof(*model->images))
			return SDL_SetError("too many shared GPU images");

		capacity += mesh->texture_count;
	}

	gpu->images = (struct mh_gpu_image *)calloc(
		capacity ? capacity : 1, sizeof(*gpu->images));

	if (gpu->images == NULL)
		return SDL_SetError(
			"out of memory creating shared GPU image pool");

	*maximum_upload = 0;

	for (model_index = 0; model_index < gpu->mesh_count; model_index++) {
		model = &gpu->meshes[model_index];
		mesh = model->mesh;
		total = 0;
		first = gpu->image_count;
		model->images = (size_t *)malloc(
			mesh->texture_count * sizeof(*model->images));

		if (model->images == NULL)
			return SDL_SetError("out of memory binding GPU images");

		for (i = 0; i < mesh->texture_count; i++) {
			if (MH_RENDER_BUILD_POWERVR && gpu->powervr &&
				mh_powervr_texture_unused(mesh, i)) {
				model->images[i] = NO_IMAGE;
				continue;
			}

			image = mh_mesh_texture(mesh, i);

			if (image == NULL || image->rgba == NULL ||
				image->width == 0 || image->height == 0 ||
				image->width > MH_U32_MAX / MH_GPU_PIXEL_BYTES /
						image->height)
				return SDL_SetError(
					"invalid GPU texture dimensions or pixels");

			for (image_index = 0; image_index < gpu->image_count &&
				image->rgba !=
					gpu->images[image_index].source->rgba;
				image_index++) {
			}

			shared = &gpu->images[image_index];

			if (image_index == gpu->image_count) {
				shared->source = image;
				shared->format =
					mh_mesh_texture_format(mesh, i);

				shared->width = shared->source->width;
				shared->height = shared->source->height;

				if (MH_RENDER_BUILD_POWERVR && gpu->powervr &&
					!mh_gpu_powervr_image_size_prepare(
						shared))
					return 0;

				bytes = (Uint32)((size_t)shared->width *
					shared->height * MH_GPU_PIXEL_BYTES);

				if (bytes > MH_U32_MAX - total)
					return SDL_SetError(
						"mesh images exceed GPU transfer size limit");

				shared->levels = 1;
				shared->owner = model_index;
				shared->offset = total;
				total += bytes;
				gpu->image_count++;
			} else if (image->width != shared->source->width ||
				image->height != shared->source->height ||
				mh_mesh_texture_format(mesh, i) !=
					shared->format)
				return SDL_SetError(
					"inconsistent shared GPU image format or dimensions");

			model->images[i] = image_index;
		}

		if (total > *maximum_upload)
			*maximum_upload = total;

		if (verbose)
			fprintf(stderr,
				"GPU mesh %lu: %lu vertex bytes, %lu image slots, %lu new images, %lu texture bytes\n",
				(unsigned long)model_index,
				(unsigned long)(mesh->vertex_count *
					sizeof(struct mh_vertex)),
				(unsigned long)mesh->texture_count,
				(unsigned long)(gpu->image_count - first),
				(unsigned long)total);
	}

	return prepare_mips(gpu, maximum_upload);
}

int
mh_gpu_renderer_mesh_visible(
	struct mh_gpu_renderer *gpu, size_t index, int visible)
{
	if (gpu == NULL || index >= gpu->mesh_count) {
		SDL_SetError("invalid GPU mesh index");
		return 0;
	}

	gpu->meshes[index].hidden = !visible;

	return 1;
}

static int
update_mesh(struct mh_gpu_renderer *gpu, size_t index,
	const struct mh_vertex *vertices, enum mh_scene_effect effect)
{
	SDL_GPUTransferBufferCreateInfo info;
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *copy_pass;
	SDL_GPUTransferBufferLocation source;
	SDL_GPUBufferRegion destination;
	void *mapped;
	Uint32 size;
	struct mh_gpu_mesh *model;
	struct mh_error error;
	size_t i;

	if (index >= gpu->mesh_count) {
		SDL_SetError("invalid GPU mesh index");
		return 0;
	}

	model = &gpu->meshes[index];

	if (model->mesh->material_rgb != NULL) {
		if (model->mesh->material_rgb_count !=
			model->mesh->vertex_count)
			return SDL_SetError("invalid raw material color count");

		if (model->material_rgb == NULL)
			model->material_rgb = (float *)malloc(
				model->mesh->vertex_count * 3 * sizeof(float));

		if (model->material_rgb == NULL)
			return SDL_SetError(
				"cannot retain GPU material colors");
	}

	size = (Uint32)(model->mesh->vertex_count * sizeof(struct mh_vertex));

	if (model->vertex_upload == NULL) {
		memset(&info, 0, sizeof(info));
		info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
		info.size = size;
		model->vertex_upload =
			SDL_CreateGPUTransferBuffer(gpu->device, &info);

		if (model->vertex_upload == NULL)
			return mh_gpu_error("create vertex transfer buffer",
				index, 0, size);
	}

	/*
	 * Cycle both buffers so an in-flight frame keeps its own data. Upload
	 * the complete mesh: a cycled buffer does not preserve static ranges.
	 */
	mapped = SDL_MapGPUTransferBuffer(
		gpu->device, model->vertex_upload, true);

	if (mapped == NULL)
		return mh_gpu_error(
			"map vertex transfer buffer", index, 0, size);

	memcpy(mapped, vertices == NULL ? model->mesh->vertices : vertices,
		size);

	if (model->mesh->atlas_uv != NULL)
		for (i = 0; i < model->mesh->vertex_count; i++)
			memcpy(((struct mh_vertex *)mapped)[i].uv,
				model->mesh->atlas_uv + i * 2,
				2 * sizeof(float));

	if (MH_RENDER_BUILD_POWERVR && gpu->powervr &&
		effect == MH_SCENE_EFFECT_SKY &&
		!mh_gpu_powervr_sky_uv_scale(
			gpu, model, (struct mh_vertex *)mapped)) {
		SDL_UnmapGPUTransferBuffer(gpu->device, model->vertex_upload);
		return 0;
	}

	if (MH_RENDER_BUILD_POWERVR && gpu->powervr &&
		(effect == MH_SCENE_EFFECT_PARTICLES ||
			effect == MH_SCENE_EFFECT_HALOS) &&
		mh_powervr_effect_colors_prepare(model->mesh,
			(struct mh_vertex *)mapped, effect,
			&error) != MH_ERROR_NONE) {
		SDL_UnmapGPUTransferBuffer(gpu->device, model->vertex_upload);
		return SDL_SetError("%s", error.message);
	}

	SDL_UnmapGPUTransferBuffer(gpu->device, model->vertex_upload);
	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL)
		return 0;

	copy_pass = SDL_BeginGPUCopyPass(command);

	if (copy_pass == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		return 0;
	}

	memset(&source, 0, sizeof(source));
	source.transfer_buffer = model->vertex_upload;
	memset(&destination, 0, sizeof(destination));
	destination.buffer = model->vertices;
	destination.size = size;
	SDL_UploadToGPUBuffer(copy_pass, &source, &destination, true);
	SDL_EndGPUCopyPass(copy_pass);

	if (!SDL_SubmitGPUCommandBuffer(command))
		return 0;

	if (vertices == NULL)
		vertices = model->mesh->vertices;

	for (i = 0; i < model->mesh->vertex_count; i++) {
		memcpy(model->positions + i * 3, vertices[i].position,
			3 * sizeof(float));
		memcpy(model->colors + i * 3, vertices[i].color,
			3 * sizeof(float));

		if (model->projection_w != NULL)
			model->projection_w[i] = vertices[i].projection_w;
	}

	if (model->mesh->material_rgb != NULL)
		memcpy(model->material_rgb, model->mesh->material_rgb,
			model->mesh->vertex_count * 3 * sizeof(float));
	else {
		free(model->material_rgb);
		model->material_rgb = NULL;
	}

	return 1;
}

int
mh_gpu_renderer_update_mesh(struct mh_gpu_renderer *gpu, size_t index)
{
	if (!update_mesh(gpu, index, NULL, MH_SCENE_EFFECT_OPAQUE))
		return 0;

	gpu->meshes[index].effect = MH_SCENE_EFFECT_OPAQUE;
	gpu->meshes[index].lit = 0;

	return 1;
}

int
mh_gpu_renderer_mesh_lit(struct mh_gpu_renderer *gpu, size_t index,
	const struct mh_vertex *vertices)
{
	if (vertices == NULL ||
		!update_mesh(gpu, index, vertices, MH_SCENE_EFFECT_OPAQUE))
		return 0;

	gpu->meshes[index].effect = MH_SCENE_EFFECT_OPAQUE;
	gpu->meshes[index].lit = 1;

	return 1;
}

int
mh_gpu_renderer_mesh_unavailable(struct mh_gpu_renderer *gpu, size_t index,
	const struct mh_vertex *vertices)
{
	if (index >= gpu->mesh_count) {
		SDL_SetError("invalid GPU mesh index");
		return 0;
	}

	if (vertices == NULL &&
		gpu->meshes[index].effect == MH_SCENE_EFFECT_OPAQUE)
		return 1;

	if (!update_mesh(gpu, index, vertices, MH_SCENE_EFFECT_OPAQUE))
		return 0;

	gpu->meshes[index].effect = vertices != NULL
		? MH_SCENE_EFFECT_UNAVAILABLE
		: MH_SCENE_EFFECT_OPAQUE;
	gpu->meshes[index].lit = 0;

	return 1;
}

int
mh_gpu_renderer_mesh_skids(struct mh_gpu_renderer *gpu, size_t index)
{
	if (!update_mesh(gpu, index, NULL, MH_SCENE_EFFECT_SKIDS))
		return 0;

	gpu->meshes[index].effect = MH_SCENE_EFFECT_SKIDS;

	return 1;
}

int
mh_gpu_renderer_mesh_particles(struct mh_gpu_renderer *gpu, size_t index)
{
	if (!update_mesh(gpu, index, NULL, MH_SCENE_EFFECT_PARTICLES))
		return 0;

	gpu->meshes[index].effect = MH_SCENE_EFFECT_PARTICLES;

	return 1;
}

int
mh_gpu_renderer_mesh_shadows(struct mh_gpu_renderer *gpu, size_t index)
{
	if (!update_mesh(gpu, index, NULL, MH_SCENE_EFFECT_SHADOWS))
		return 0;

	gpu->meshes[index].effect = MH_SCENE_EFFECT_SHADOWS;

	return 1;
}

int
mh_gpu_renderer_mesh_sky(struct mh_gpu_renderer *gpu, size_t index)
{
	if (!update_mesh(gpu, index, NULL, MH_SCENE_EFFECT_SKY))
		return 0;

	gpu->meshes[index].effect = MH_SCENE_EFFECT_SKY;

	return 1;
}

int
mh_gpu_renderer_mesh_halos(struct mh_gpu_renderer *gpu, size_t index)
{
	if (!update_mesh(gpu, index, NULL, MH_SCENE_EFFECT_HALOS))
		return 0;

	gpu->meshes[index].effect = MH_SCENE_EFFECT_HALOS;

	return 1;
}

int
mh_gpu_upload_mesh(struct mh_gpu_renderer *gpu, size_t index,
	SDL_GPUTransferBuffer *transfer)
{
	SDL_GPUBufferCreateInfo info;
	SDL_GPUCommandBuffer *command;
	SDL_GPUCopyPass *copy_pass;
	SDL_GPUTextureTransferInfo texture_source;
	SDL_GPUTextureRegion texture_destination;
	const struct mh_image *image;
	struct mh_gpu_image *shared;
	size_t i;
	Uint32 size, offset;
	unsigned int level;
	struct mh_image mip;
	unsigned char *mapped;
	struct mh_gpu_mesh *model;

	model = &gpu->meshes[index];

	if (model->mesh->vertex_count > MH_U32_MAX / sizeof(struct mh_vertex)) {
		SDL_SetError("mesh exceeds GPU buffer size limit");
		return 0;
	}

	size = (Uint32)(model->mesh->vertex_count * sizeof(struct mh_vertex));
	memset(&info, 0, sizeof(info));
	info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	info.size = size;
	model->vertices = SDL_CreateGPUBuffer(gpu->device, &info);

	if (model->vertices == NULL)
		return mh_gpu_error("create vertex buffer", index, 0, size);

	model->positions =
		(float *)malloc(model->mesh->vertex_count * 3 * sizeof(float));
	model->colors =
		(float *)malloc(model->mesh->vertex_count * 3 * sizeof(float));

	if (gpu->powervr)
		model->projection_w = (float *)malloc(
			model->mesh->vertex_count * sizeof(float));

	if (model->positions == NULL || model->colors == NULL ||
		(gpu->powervr && model->projection_w == NULL)) {
		SDL_SetError("out of memory retaining GPU corners");
		return 0;
	}

	if (!mh_gpu_renderer_update_mesh(gpu, index))
		return 0;

	/*
	 * One startup transfer buffer holds this mesh's textures. Complete
	 * each batch before reusing it for another mesh: no per-texture
	 * allocations or unbounded queue of pending startup uploads.
	 */
	offset = 0;

	for (i = 0; i < gpu->image_count; i++)
		if (gpu->images[i].owner == index) {
			shared = &gpu->images[i];
			size = (Uint32)((size_t)shared->width * shared->height *
				MH_GPU_PIXEL_BYTES);
			shared->texture = mh_gpu_create_texture_levels(gpu,
				shared->width, shared->height,
				SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM,
				SDL_GPU_TEXTUREUSAGE_SAMPLER, shared->levels);

			if (shared->texture == NULL)
				return mh_gpu_error(
					"create texture", index, i, size);

			offset += size;
		}

	if (offset == 0)
		return 1;

	mapped = (unsigned char *)SDL_MapGPUTransferBuffer(
		gpu->device, transfer, false);

	if (mapped == NULL)
		return mh_gpu_error("map texture transfer buffer", index, 0, 0);

	for (i = 0; i < gpu->image_count; i++)
		if (gpu->images[i].owner == index) {
			shared = &gpu->images[i];
			image = shared->source;
			size = (Uint32)((size_t)shared->width * shared->height *
				MH_GPU_PIXEL_BYTES);

			if (MH_RENDER_BUILD_POWERVR && gpu->powervr &&
				shared->format != 0) {
				mip.width = shared->width;
				mip.height = shared->height;
				mip.rgba = mapped + shared->offset;

				if (mh_powervr_texture_prepare(image,
					(enum mh_texture_format)
						shared->format,
					&mip, NULL) != MH_ERROR_NONE) {
					SDL_UnmapGPUTransferBuffer(
						gpu->device, transfer);
					return SDL_SetError(
						"invalid PowerVR texture source");
				}
			} else
				memcpy(mapped + shared->offset, image->rgba,
					size);

			offset = shared->offset + size;

			for (level = 1; level < shared->levels; level++) {
				mip.width = image->width >> level;
				mip.height = image->height >> level;
				mip.rgba = mapped + offset;

				if (mh_mipmap_texture(shared->source,
					shared->format, level, &mip,
					NULL) != MH_ERROR_NONE) {
					SDL_UnmapGPUTransferBuffer(
						gpu->device, transfer);
					return SDL_SetError(
						"invalid GPU mip source");
				}

				offset += mip.width * mip.height *
					MH_GPU_PIXEL_BYTES;
			}
		}

	SDL_UnmapGPUTransferBuffer(gpu->device, transfer);
	command = SDL_AcquireGPUCommandBuffer(gpu->device);

	if (command == NULL)
		return mh_gpu_error(
			"acquire texture upload command", index, 0, offset);

	copy_pass = SDL_BeginGPUCopyPass(command);

	if (copy_pass == NULL) {
		SDL_CancelGPUCommandBuffer(command);
		return mh_gpu_error("begin texture upload", index, 0, offset);
	}

	for (i = 0; i < gpu->image_count; i++)
		if (gpu->images[i].owner == index) {
			shared = &gpu->images[i];
			memset(&texture_source, 0, sizeof(texture_source));
			texture_source.transfer_buffer = transfer;
			texture_source.offset = shared->offset;
			memset(&texture_destination, 0,
				sizeof(texture_destination));
			texture_destination.texture = shared->texture;
			texture_destination.d = 1;

			for (level = 0; level < shared->levels; level++) {
				texture_destination.mip_level = level;
				texture_destination.w = shared->width >> level;
				texture_destination.h = shared->height >> level;
				SDL_UploadToGPUTexture(copy_pass,
					&texture_source, &texture_destination,
					false);
				texture_source.offset += texture_destination.w *
					texture_destination.h *
					MH_GPU_PIXEL_BYTES;
			}
		}

	SDL_EndGPUCopyPass(copy_pass);

	if (!SDL_SubmitGPUCommandBuffer(command))
		return mh_gpu_error("submit texture upload", index, 0, offset);

	if (!SDL_WaitForGPUIdle(gpu->device))
		return mh_gpu_error("finish mesh upload", index, 0, offset);

	return 1;
}
