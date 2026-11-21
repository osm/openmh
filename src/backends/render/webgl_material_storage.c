#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/render/webgl_material_queue.h"

static enum mh_error_code
reserve_items(struct mh_webgl_materials *materials, struct mh_error *error_out)
{
	struct mh_webgl_primitive *items;
	size_t capacity, limit;

	if (materials->count < materials->capacity)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	limit = (size_t)-1 / sizeof(*items);

	if (materials->count >= limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"too many WebGL material primitives");

	capacity = materials->capacity == 0	  ? 1024
		: materials->capacity > limit / 2 ? limit
						: materials->capacity * 2;
	items = realloc(materials->items, capacity * sizeof(*items));

	if (items == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot grow WebGL material lists");

	materials->items = items;
	materials->capacity = capacity;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_material_append(struct mh_webgl_materials *materials, size_t model,
	size_t draw, size_t first, size_t count, size_t list, int near_clipped,
	int prepared, struct mh_error *error_out)
{
	struct mh_webgl_primitive *item;
	enum mh_error_code status;
	size_t index;

	if (list >= MH_LIST_COUNT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebGL material list");

	if (!materials->openmh && list == MH_LIST_INSPECTION &&
		materials->tails[list] != MH_NO_PRIMITIVE) {
		item = &materials->items[materials->tails[list]];

		if (item->model == model && item->draw == draw &&
			item->prepared == prepared &&
			item->first + item->count == first) {
			item->count += count;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	status = reserve_items(materials, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	index = materials->count++;
	item = &materials->items[index];
	item->model = model;
	item->draw = draw;
	item->first = first;
	item->count = count;
	item->near_clipped = near_clipped;
	item->prepared = prepared;

	/* Shadow meshes already contain the producer's reversed quads. */
	if (list == MH_LIST_INSPECTION || list == MH_LIST_SHADOW) {
		item->next = MH_NO_PRIMITIVE;

		if (materials->tails[list] != MH_NO_PRIMITIVE)
			materials->items[materials->tails[list]].next = index;
		else
			materials->heads[list] = index;
		materials->tails[list] = index;
	} else {
		item->next = materials->heads[list];
		materials->heads[list] = index;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_material_reserve_vertices(struct mh_webgl_materials *materials,
	size_t count, struct mh_error *error_out)
{
	struct mh_vertex *vertices;
	struct mh_glenz_coordinates *coordinates;
	size_t capacity, limit;

	if (count <= materials->vertex_capacity)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	limit = INT_MAX / sizeof(*vertices) / 3 * 3;

	if (count > limit)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"prepared WebGL scene exceeds buffer limits");

	capacity =
		materials->vertex_capacity ? materials->vertex_capacity : 1536;

	while (capacity < count)
		capacity = capacity > limit / 2 ? limit : capacity * 2;

	vertices = malloc(capacity * sizeof(*vertices));
	coordinates = malloc(capacity * sizeof(*coordinates));

	if (vertices == NULL || coordinates == NULL) {
		free(vertices);
		free(coordinates);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot grow prepared WebGL vertices");
	}

	if (materials->vertex_count != 0) {
		memcpy(vertices, materials->vertices,
			materials->vertex_count * sizeof(*vertices));
		memcpy(coordinates, materials->coordinates,
			materials->vertex_count * sizeof(*coordinates));
	}

	free(materials->vertices);
	free(materials->coordinates);
	materials->vertices = vertices;
	materials->coordinates = coordinates;
	materials->vertex_capacity = capacity;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_webgl_material_upload(
	struct mh_webgl_materials *materials, struct mh_error *error_out)
{
	struct mh_webgl_geometry geometry;
	enum mh_error_code status;

	if (materials->vertex_count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (materials->vertex_capacity > materials->geometry.count) {
		memset(&geometry, 0, sizeof(geometry));
		status = mh_webgl_geometry_create(
			&geometry, materials->vertex_capacity, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_webgl_geometry_destroy(&materials->geometry);
		materials->geometry = geometry;
	}

	status = mh_webgl_geometry_vertices(&materials->geometry,
		materials->vertices, materials->vertex_count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_webgl_geometry_glenz(&materials->geometry,
			materials->coordinates, materials->vertex_count,
			error_out);

	return status;
}

void
mh_webgl_materials_destroy(struct mh_webgl_materials *materials)
{
	if (materials == NULL)
		return;

	mh_webgl_geometry_destroy(&materials->geometry);
	free(materials->items);
	free(materials->vertices);
	free(materials->coordinates);
	memset(materials, 0, sizeof(*materials));
}
