#include <stdlib.h>
#include <string.h>

#include "backends/render/sdl_gpu_powervr_order.h"
#include "backends/render/shader_uniforms.h"
#include "engine/render/powervr_order.h"

#define REGION_SIZE MH_POWERVR_REGION_SIZE
#define WINDINGS MH_POWERVR_WINDINGS
#define NO_ITEM MH_POWERVR_NO_ITEM

struct translucent_call {
	SDL_GPUBuffer			*vertices;
	SDL_GPUTextureSamplerBinding	texture;
	float				camera[MH_GPU_CAMERA_FLOATS];
	float				material[MH_GPU_MATERIAL_FLOATS];
};

struct mh_gpu_powervr_order {
	struct translucent_call			*calls;
	struct mh_powervr_order_triangle	*triangles;
	struct mh_powervr_order_set		*sets;
	size_t	capacity, call_count, triangle_count;
};

void
mh_gpu_powervr_order_destroy(struct mh_gpu_renderer *gpu)
{
	struct mh_gpu_powervr_order *order;

	order = gpu->powervr_order;

	if (order == NULL)
		return;

	free(order->calls);
	free(order->triangles);
	free(order->sets);
	free(order);
	gpu->powervr_order = NULL;
}

int
mh_gpu_powervr_order_begin(struct mh_gpu_renderer *gpu)
{
	struct mh_gpu_powervr_order *order;
	size_t count;

	if (!gpu->powervr)
		return 1;

	count = gpu->prepared_count / 3;

	if (count > (size_t)-1 - gpu->primitive_capacity)
		return SDL_SetError("PowerVR translucent capacity overflow");

	count += gpu->primitive_capacity;

	if (gpu->powervr_order == NULL ||
		count > gpu->powervr_order->capacity) {
		if (count > (size_t)-1 / sizeof(*order->calls) ||
			count > (size_t)-1 / sizeof(*order->triangles) ||
			count > (size_t)-1 / WINDINGS / sizeof(*order->sets))
			return SDL_SetError(
				"PowerVR translucent storage overflow");

		order = (struct mh_gpu_powervr_order *)calloc(
			1, sizeof(*order));

		if (order == NULL)
			return SDL_SetError(
				"out of memory preparing PowerVR order");

		order->capacity = count ? count : 1;
		order->calls = (struct translucent_call *)malloc(
			order->capacity * sizeof(*order->calls));
		order->triangles = (struct mh_powervr_order_triangle *)malloc(
			order->capacity * sizeof(*order->triangles));
		order->sets = (struct mh_powervr_order_set *)malloc(
			order->capacity * WINDINGS * sizeof(*order->sets));

		if (order->calls == NULL || order->triangles == NULL ||
			order->sets == NULL) {
			free(order->calls);
			free(order->triangles);
			free(order->sets);
			free(order);
			return SDL_SetError(
				"out of memory preparing PowerVR order");
		}

		mh_gpu_powervr_order_destroy(gpu);
		gpu->powervr_order = order;
	}

	gpu->powervr_order->call_count = 0;
	gpu->powervr_order->triangle_count = 0;

	return 1;
}

static int
triangle_bounds(struct mh_gpu_renderer *gpu, size_t model, size_t first,
	int prepared, const float camera[MH_GPU_CAMERA_FLOATS],
	struct mh_powervr_order_triangle *triangle)
{
	struct mh_powervr_order_vertex vertices[3];
	struct mh_water_frame water;
	const struct mh_gpu_mesh *mesh;
	const struct mh_vertex *vertex;
	unsigned int i;

	mesh = &gpu->meshes[model];

	for (i = 0; i < 3; i++) {
		if (prepared) {
			vertex = &gpu->prepared_vertices[first + i];
			memcpy(vertices[i].xy, vertex->position,
				sizeof(vertices[i].xy));
			vertices[i].w = vertex->projection_w;
		} else {
			memcpy(vertices[i].xy,
				mesh->positions + (first + i) * 3,
				sizeof(vertices[i].xy));
			vertices[i].w = mesh->projection_w[first + i];
		}
	}

	water.phase = camera[MH_GPU_CAMERA_WATER_PHASE];
	water.scale = camera[MH_GPU_CAMERA_WATER_SCALE];
	water.frequency = camera[MH_GPU_CAMERA_WATER_FREQUENCY];

	return mh_powervr_order_bounds(vertices, gpu->width, gpu->height,
		camera[MH_GPU_CAMERA_WATER_ENABLED] > .5f ? &water : NULL,
		triangle);
}

int
mh_gpu_powervr_order_defer(struct mh_gpu_renderer *gpu, size_t model,
	size_t first, size_t count, int prepared,
	const SDL_GPUTextureSamplerBinding *texture,
	const float camera[MH_GPU_CAMERA_FLOATS],
	const float material[MH_GPU_MATERIAL_FLOATS])
{
	struct mh_gpu_powervr_order *order;
	struct translucent_call *call;
	struct mh_powervr_order_triangle triangle;
	size_t i, limit;

	order = gpu->powervr_order;
	limit = prepared ? gpu->prepared_count
			: gpu->meshes[model].mesh->vertex_count;

	if (order == NULL || count % 3 != 0 || first > limit ||
		count > limit - first || order->call_count >= order->capacity ||
		count / 3 > order->capacity - order->triangle_count)
		return SDL_SetError("invalid PowerVR translucent draw");

	call = &order->calls[order->call_count];
	call->vertices =
		prepared ? gpu->prepared_buffer : gpu->meshes[model].vertices;
	call->texture = *texture;
	memcpy(call->camera, camera, sizeof(call->camera));
	memcpy(call->material, material, sizeof(call->material));
	memset(&triangle, 0, sizeof(triangle));
	triangle.call = order->call_count++;

	for (i = first; i < first + count; i += 3) {
		triangle.first = i;

		if (triangle_bounds(gpu, model, i, prepared, camera, &triangle))
			order->triangles[order->triangle_count++] = triangle;
	}

	return 1;
}

static void
draw_set(struct mh_gpu_powervr_order *order,
	const struct mh_powervr_order_set *set, SDL_GPUCommandBuffer *command,
	SDL_GPURenderPass *pass)
{
	const struct translucent_call *call;
	const struct mh_powervr_order_triangle *triangle;
	SDL_GPUBufferBinding binding;
	size_t item;

	call = &order->calls[set->call];
	binding.buffer = call->vertices;
	binding.offset = 0;
	SDL_BindGPUVertexBuffers(pass, 0, &binding, 1);
	SDL_BindGPUFragmentSamplers(pass, 0, &call->texture, 1);
	SDL_PushGPUVertexUniformData(
		command, 0, call->camera, sizeof(call->camera));
	SDL_PushGPUVertexUniformData(
		command, 1, call->material, sizeof(call->material));

	for (item = set->first; item != NO_ITEM; item = triangle->next) {
		triangle = &order->triangles[item];
		SDL_DrawGPUPrimitives(pass, 3, 1, (Uint32)triangle->first, 0);
	}
}

void
mh_gpu_powervr_order_draw(struct mh_gpu_renderer *gpu,
	SDL_GPUCommandBuffer *command, SDL_GPURenderPass *pass)
{
	struct mh_gpu_powervr_order *order;
	SDL_Rect scissor;
	unsigned int x, y;
	size_t head, item;

	order = gpu->powervr_order;

	if (order == NULL || order->triangle_count == 0)
		return;

	SDL_BindGPUGraphicsPipeline(
		pass, mh_gpu_pipeline(gpu, 0, MH_GPU_PIPELINE_ALPHA));

	for (y = 0; y < (gpu->height + REGION_SIZE - 1) / REGION_SIZE; y++)
		for (x = 0; x < (gpu->width + REGION_SIZE - 1) / REGION_SIZE;
			x++) {
			head = mh_powervr_order_region(order->triangles,
				order->triangle_count, order->sets,
				order->call_count, x, y);

			if (head == NO_ITEM)
				continue;

			scissor.x = (int)(x * REGION_SIZE);
			scissor.y = (int)(y * REGION_SIZE);
			scissor.w = gpu->width - scissor.x < REGION_SIZE
				? (int)gpu->width - scissor.x
				: REGION_SIZE;
			scissor.h = gpu->height - scissor.y < REGION_SIZE
				? (int)gpu->height - scissor.y
				: REGION_SIZE;
			SDL_SetGPUScissor(pass, &scissor);

			for (item = head; item != NO_ITEM;
				item = order->sets[item].next)
				draw_set(order, &order->sets[item], command,
					pass);
		}

	scissor.x = scissor.y = 0;
	scissor.w = (int)gpu->width;
	scissor.h = (int)gpu->height;
	SDL_SetGPUScissor(pass, &scissor);
}
