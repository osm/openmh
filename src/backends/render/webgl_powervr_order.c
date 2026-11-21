#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_powervr_order.h"
#include "engine/render/powervr_order.h"

#define REGION_SIZE MH_POWERVR_REGION_SIZE
#define WINDINGS MH_POWERVR_WINDINGS
#define NO_ITEM MH_POWERVR_NO_ITEM

struct mh_webgl_order {
	struct mh_webgl_draw_call		*calls;
	struct mh_powervr_order_triangle	*triangles;
	struct mh_powervr_order_set		*sets;
	size_t	capacity, call_count, triangle_count;
	unsigned int				width, height;
};

void
mh_webgl_order_destroy(struct mh_webgl_order *order)
{
	if (order == NULL)
		return;

	free(order->calls);
	free(order->triangles);
	free(order->sets);
	free(order);
}

static enum mh_error_code
reserve(struct mh_webgl_order **output, size_t capacity,
	struct mh_error *error_out)
{
	struct mh_webgl_order *order;

	if (*output != NULL && (*output)->capacity >= capacity)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (capacity > (size_t)-1 / sizeof(*order->calls) ||
		capacity > (size_t)-1 / sizeof(*order->triangles) ||
		capacity > (size_t)-1 / WINDINGS / sizeof(*order->sets))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"PowerVR translucent storage overflow");

	order = calloc(1, sizeof(*order));

	if (order == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate PowerVR order");

	order->capacity = capacity ? capacity : 1;
	order->calls = malloc(order->capacity * sizeof(*order->calls));
	order->triangles = malloc(order->capacity * sizeof(*order->triangles));
	order->sets = malloc(order->capacity * WINDINGS * sizeof(*order->sets));

	if (order->calls == NULL || order->triangles == NULL ||
		order->sets == NULL) {
		mh_webgl_order_destroy(order);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate PowerVR translucent storage");
	}

	mh_webgl_order_destroy(*output);
	*output = order;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_order_begin(struct mh_webgl_order **output,
	const struct mh_webgl_scene *scene,
	const struct mh_webgl_materials *materials, unsigned int width,
	unsigned int height, struct mh_error *error_out)
{
	size_t capacity, i, triangles;
	enum mh_error_code status;

	if (output == NULL || scene == NULL || materials == NULL ||
		width == 0 || height == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR ordering frame");

	capacity = materials->vertex_count / 3;

	for (i = 0; i < scene->mesh_count; i++) {
		triangles = scene->meshes[i].geometry.count / 3;

		if (triangles > (size_t)-1 - capacity)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"PowerVR translucent capacity overflow");

		capacity += triangles;
	}

	status = reserve(output, capacity, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	(*output)->call_count = (*output)->triangle_count = 0;
	(*output)->width = width;
	(*output)->height = height;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
triangle_bounds(const struct mh_webgl_order *order,
	const struct mh_webgl_draw_call *call, const struct mh_webgl_mesh *mesh,
	const struct mh_webgl_materials *materials, int prepared,
	struct mh_powervr_order_triangle *triangle)
{
	struct mh_powervr_order_vertex vertices[3];
	struct mh_water_frame water;
	const struct mh_vertex *vertex;
	size_t first;
	unsigned int i;

	first = triangle->first;

	for (i = 0; i < 3; i++) {
		if (prepared) {
			vertex = &materials->vertices[first + i];
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

	water.phase = call->camera[MH_GPU_CAMERA_WATER_PHASE];
	water.scale = call->camera[MH_GPU_CAMERA_WATER_SCALE];
	water.frequency = call->camera[MH_GPU_CAMERA_WATER_FREQUENCY];

	return mh_powervr_order_bounds(vertices, order->width, order->height,
		call->camera[MH_GPU_CAMERA_WATER_ENABLED] > .5f ? &water : NULL,
		triangle);
}

enum mh_error_code
mh_webgl_order_defer(struct mh_webgl_order *order,
	const struct mh_webgl_draw_call *call, const struct mh_webgl_mesh *mesh,
	const struct mh_webgl_materials *materials, int prepared,
	struct mh_error *error_out)
{
	struct mh_powervr_order_triangle triangle;
	size_t limit, i;

	if (order == NULL || call == NULL || mesh == NULL ||
		(prepared && materials == NULL))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing PowerVR translucent call");

	limit = prepared ? materials->vertex_count : mesh->geometry.count;

	if (call->first % 3 != 0 || call->count % 3 != 0 ||
		call->first > limit || call->count > limit - call->first ||
		order->call_count >= order->capacity ||
		call->count / 3 > order->capacity - order->triangle_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid PowerVR translucent draw");

	order->calls[order->call_count] = *call;
	memset(&triangle, 0, sizeof(triangle));
	triangle.call = order->call_count++;

	for (i = call->first; i < call->first + call->count; i += 3) {
		triangle.first = i;

		if (triangle_bounds(
			order, call, mesh, materials, prepared, &triangle))
			order->triangles[order->triangle_count++] = triangle;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
draw_set(const struct mh_webgl_order *order,
	const struct mh_powervr_order_set *set,
	struct mh_webgl_drawing *drawing, struct mh_error *error_out)
{
	const struct mh_webgl_draw_call *call;
	const struct mh_powervr_order_triangle *triangle;
	enum mh_error_code status;
	size_t item;

	call = &order->calls[set->call];
	status = mh_webgl_draw_bind(drawing, call, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (item = set->first; item != NO_ITEM; item = triangle->next) {
		triangle = &order->triangles[item];
		status = mh_webgl_geometry_draw(
			call->geometry, triangle->first, 3, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
draw_regions(struct mh_webgl_order *order, struct mh_webgl_drawing *drawing,
	struct mh_error *error_out)
{
	unsigned int x, y, left, top, width, height;
	size_t head, item;
	enum mh_error_code status;

	for (y = 0; y < (order->height + REGION_SIZE - 1) / REGION_SIZE; y++)
		for (x = 0; x < (order->width + REGION_SIZE - 1) / REGION_SIZE;
			x++) {
			head = mh_powervr_order_region(order->triangles,
				order->triangle_count, order->sets,
				order->call_count, x, y);

			if (head == NO_ITEM)
				continue;

			left = x * REGION_SIZE;
			top = y * REGION_SIZE;
			width = order->width - left < REGION_SIZE
				? order->width - left
				: REGION_SIZE;
			height = order->height - top < REGION_SIZE
				? order->height - top
				: REGION_SIZE;

			/*
			 * Original region Y is top-down; WebGL scissors start
			 * at the bottom.
			 */
			glScissor((GLint)left,
				(GLint)(order->height - top - height),
				(GLsizei)width, (GLsizei)height);

			for (item = head; item != NO_ITEM;
				item = order->sets[item].next) {
				status = draw_set(order, &order->sets[item],
					drawing, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_order_draw(struct mh_webgl_order *order,
	struct mh_webgl_drawing *drawing, struct mh_error *error_out)
{
	GLint scissor[4];
	GLboolean enabled;
	enum mh_error_code status;

	if (order == NULL || order->triangle_count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	drawing->bound_valid = 0;
	enabled = glIsEnabled(GL_SCISSOR_TEST);
	glGetIntegerv(GL_SCISSOR_BOX, scissor);
	glEnable(GL_SCISSOR_TEST);
	status = draw_regions(order, drawing, error_out);
	glScissor(scissor[0], scissor[1], scissor[2], scissor[3]);

	if (!enabled)
		glDisable(GL_SCISSOR_TEST);

	return status;
}
