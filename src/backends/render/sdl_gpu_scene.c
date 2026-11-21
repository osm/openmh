#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_renderer_internal.h"
#include "backends/render/sdl_gpu_scene.h"
#include "engine/render/submission.h"

int
mh_gpu_renderer_scene_order(struct mh_gpu_renderer *gpu,
	const struct mh_renderer_scene_part *parts, size_t count)
{
	struct mh_renderer_scene_part *parts_copy;
	struct mh_error error;
	size_t i;

	if (gpu == NULL)
		return SDL_SetError("invalid scene submission order");

	if (mh_scene_submission_check(parts, count, gpu->mesh_count, &error) !=
		MH_ERROR_NONE)
		return SDL_SetError("%s", error.message);

	if (count == 0) {
		free(gpu->parts);
		gpu->parts = NULL;
		gpu->part_count = 0;
		return 1;
	}

	for (i = 0; i < gpu->mesh_count; i++)
		if (mh_scene_submission_mesh_check(gpu->meshes[i].mesh, i,
			parts, count, &error) != MH_ERROR_NONE)
			return SDL_SetError("%s", error.message);

	parts_copy = (struct mh_renderer_scene_part *)malloc(
		count * sizeof(*parts_copy));

	if (parts_copy == NULL)
		return SDL_SetError("out of memory retaining scene order");

	memcpy(parts_copy, parts, count * sizeof(*parts_copy));
	free(gpu->parts);
	gpu->parts = parts_copy;
	gpu->part_count = count;

	return 1;
}

int
mh_gpu_update_ranges(struct mh_gpu_renderer *gpu)
{
	struct mh_error error;
	size_t i;

	for (i = 0; i < gpu->mesh_count; i++) {
		if (mh_mesh_active_lod(gpu->meshes[i].mesh,
			&gpu->meshes[i].range, &error) != MH_ERROR_NONE)
			return SDL_SetError("%s", error.message);
	}

	for (i = 0; i < gpu->part_count; i++)
		if (!gpu->parts[i].active_lod &&
			mh_scene_span_check(
				gpu->meshes[gpu->parts[i].mesh].mesh,
				&gpu->parts[i].span, NULL) != MH_ERROR_NONE)
			return SDL_SetError(
				"scene mesh no longer contains its fixed spans");

	return 1;
}

size_t
mh_gpu_scene_part_count(const struct mh_gpu_renderer *gpu)
{
	return gpu->parts == NULL ? gpu->mesh_count : gpu->part_count;
}

size_t
mh_gpu_scene_part(const struct mh_gpu_renderer *gpu, size_t index,
	struct mh_mesh_span *span)
{
	const struct mh_mesh_lod *range;
	size_t model_index;

	model_index = gpu->parts == NULL ? index : gpu->parts[index].mesh;
	range = &gpu->meshes[model_index].range;

	if (gpu->parts != NULL && !gpu->parts[index].active_lod)
		*span = gpu->parts[index].span;
	else {
		span->draw_first = range->draw_first;
		span->draw_count = range->draw_count;
		span->primitive_first = range->primitive_first;
		span->primitive_count = range->primitive_count;
	}

	return model_index;
}
