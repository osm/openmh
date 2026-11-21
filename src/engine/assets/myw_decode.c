#include <stdlib.h>
#include <string.h>

#include "engine/assets/myw.h"
#include "engine/assets/myw_layout.h"
#include "engine/assets/texture_names.h"
#include "engine/core/binary.h"
#include "engine/geometry/world.h"

static void *
allocate_array(size_t count, size_t element_size)
{
	if (count == 0 || count > (size_t)-1 / element_size)
		return NULL;

	return calloc(count, element_size);
}

static enum mh_error_code
allocate_world(const struct mh_myw *source, struct mh_world *world,
	struct mh_error *error_out)
{
	world->width = (size_t)source->width;

	world->height = (size_t)source->height;
	world->cell_count = source->cell_count;
	world->face_count = (size_t)source->blocks[MH_MYW_PRIMITIVES].count;
	world->model_count = (size_t)source->blocks[MH_MYW_MODELS].count;
	world->texture_count = (size_t)source->blocks[MH_MYW_NAMES].count;
	world->light_count = (size_t)source->blocks[MH_MYW_LIGHTS].count;
	world->link_count = (size_t)source->blocks[MH_MYW_LINKS].count;
	world->halo_count = (size_t)source->blocks[MH_MYW_AUXILIARY].count;
	memcpy(world->cell_size, source->cell_size, sizeof(world->cell_size));
	memcpy(world->grid_minimum, source->grid_minimum,
		sizeof(world->grid_minimum));
	memcpy(world->minimum, source->minimum, sizeof(world->minimum));
	memcpy(world->maximum, source->maximum, sizeof(world->maximum));

	world->models = (struct mh_world_model *)allocate_array(
		world->model_count, sizeof(*world->models));
	world->cells = (struct mh_world_cell *)allocate_array(
		world->cell_count, sizeof(*world->cells));
	world->textures = (char **)allocate_array(
		world->texture_count, sizeof(*world->textures));
	world->lights = (struct mh_point_light *)allocate_array(
		world->light_count, sizeof(*world->lights));
	world->links = (struct mh_world_light_link *)allocate_array(
		world->link_count, sizeof(*world->links));
	world->halos = (struct mh_geometry_halo *)allocate_array(
		world->halo_count, sizeof(*world->halos));

	if ((world->model_count != 0 && world->models == NULL) ||
		world->cells == NULL ||
		(world->texture_count != 0 && world->textures == NULL) ||
		(world->light_count != 0 && world->lights == NULL) ||
		(world->link_count != 0 && world->links == NULL) ||
		(world->halo_count != 0 && world->halos == NULL))
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding world");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
decode_model(const struct mh_myo *source, struct mh_world_model *model,
	struct mh_error *error_out)
{
	struct mh_model *geometry;
	size_t index, cursor;
	enum mh_error_code status;

	geometry = &model->geometry;
	geometry->vertex_count = (size_t)source->blocks[MH_MYO_VERTICES].count;
	geometry->face_count = (size_t)source->blocks[MH_MYO_PRIMITIVES].count;
	geometry->radius = source->radius;
	memcpy(geometry->minimum, source->minimum, sizeof(geometry->minimum));
	memcpy(geometry->maximum, source->maximum, sizeof(geometry->maximum));
	geometry->faces = (struct mh_geometry_face *)allocate_array(
		geometry->face_count, sizeof(*geometry->faces));
	model->face_ids = (size_t *)allocate_array(
		geometry->face_count, sizeof(*model->face_ids));

	if (geometry->face_count != 0 &&
		(geometry->faces == NULL || model->face_ids == NULL))
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding world model");

	cursor = 0;

	for (index = 0; index < geometry->face_count; index++) {
		/*
		 * Original A3D tags are stable primitive locations, plus quad
		 * half.
		 */
		model->face_ids[index] =
			source->blocks[MH_MYO_PRIMITIVES].offset + cursor;
		status = mh_myo_next_face(
			source, &cursor, &geometry->faces[index], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
decode_models(const struct mh_myw *source, struct mh_world *world,
	struct mh_error *error_out)
{
	struct mh_myo slice;
	struct mh_world_model *model;
	size_t index;
	enum mh_error_code status;

	for (index = 0; index < world->model_count; index++) {
		model = &world->models[index];
		status = mh_myw_model(
			source, (unsigned long)index, &slice, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!mh_myw_model_bounds(source, (unsigned long)index,
			model->center, &model->radius))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid world model bounds");

		model->geometry.textures = world->textures;
		model->geometry.texture_count = world->texture_count;
		status = decode_model(&slice, model, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static size_t
light_link_index(unsigned long offset)
{
	return offset == MH_MYW_NO_LIGHT ? MH_WORLD_NO_LIGHT
					: (size_t)offset / LINK_BYTES;
}

static void
decode_cells(const struct mh_myw *source, struct mh_world *world)
{
	struct mh_myw_cell cell;
	struct mh_world_cell *decoded;
	unsigned long cursor;
	size_t index;

	for (index = 0; index < world->cell_count; index++) {
		mh_myw_cell(source, index, &cell);
		mh_myw_light_head(source, index, &cursor);
		decoded = &world->cells[index];
		decoded->model = cell.model == MH_MYW_NO_MODEL
			? MH_WORLD_NO_MODEL
			: (size_t)cell.model;
		decoded->halo_first = (size_t)cell.halo_first;
		decoded->halo_count = (size_t)cell.halo_count;
		decoded->light_head = light_link_index(cursor);
	}
}

static void
decode_effects(const struct mh_myw *source, struct mh_world *world)
{
	const unsigned char *record;
	size_t index;

	world->has_directional =
		mh_myw_directional(source, &world->directional);

	for (index = 0; index < world->light_count; index++)
		mh_myw_light(
			source, (unsigned long)index, &world->lights[index]);

	for (index = 0; index < world->halo_count; index++)
		mh_myw_halo(source, (unsigned long)index, &world->halos[index]);

	for (index = 0; index < world->link_count; index++) {
		record = source->data + source->blocks[MH_MYW_LINKS].offset +
			index * LINK_BYTES;
		world->links[index].light =
			(size_t)mh_read_u32le(record) / LIGHT_BYTES;
		world->links[index].next =
			light_link_index(mh_read_u32le(record + LINK_NEXT));
	}
}

enum mh_error_code
mh_world_myw_decode(const unsigned char *data, size_t size,
	struct mh_world *output, struct mh_error *error_out)
{
	struct mh_myw source;
	struct mh_world world;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing decoded world output");

	status = mh_myw_parse(data, size, &source, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&world, 0, sizeof(world));
	status = allocate_world(&source, &world, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_texture_names_copy(
			source.data + source.blocks[MH_MYW_NAMES].offset,
			source.blocks[MH_MYW_NAMES].size, world.texture_count,
			world.textures,
			"out of memory decoding world texture names",
			error_out);

	if (status == MH_ERROR_NONE)
		status = decode_models(&source, &world, error_out);

	if (status != MH_ERROR_NONE) {
		mh_world_free(&world);
		return status;
	}

	decode_cells(&source, &world);
	decode_effects(&source, &world);
	*output = world;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
