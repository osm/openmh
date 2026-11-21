#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/filesystem_file_read.h"
#include "engine/assets/myo_decode.h"
#include "engine/assets/myw.h"
#include "engine/geometry/model.h"
#include "engine/geometry/world.h"

struct counts {
	unsigned long	models, worlds, faces, textures, halos;
	unsigned long	cells, lights, links, world_halos;
};

static int
same_face(const struct mh_geometry_face *a, const struct mh_geometry_face *b)
{
	unsigned int corner;

	if (a->count != b->count || a->type != b->type ||
		a->flags != b->flags || a->degenerate != b->degenerate ||
		a->texture != b->texture ||
		memcmp(a->color, b->color, sizeof(a->color)) != 0 ||
		memcmp(a->plane, b->plane, sizeof(a->plane)) != 0)
		return 0;

	for (corner = 0; corner < a->count; corner++) {
		if (memcmp(a->corners[corner].position,
			b->corners[corner].position,
			sizeof(a->corners[corner].position)) != 0 ||
			memcmp(a->corners[corner].normal,
				b->corners[corner].normal,
				sizeof(a->corners[corner].normal)) != 0 ||
			memcmp(a->corners[corner].uv, b->corners[corner].uv,
				sizeof(a->corners[corner].uv)) != 0 ||
			memcmp(a->corners[corner].color,
				b->corners[corner].color,
				sizeof(a->corners[corner].color)) != 0)
			return 0;
	}

	return 1;
}

static enum mh_error_code
compare_model(const struct mh_myo *source, const struct mh_model *model,
	struct mh_error *error_out)
{
	struct mh_geometry_face face;
	struct mh_geometry_halo halo;
	size_t index, cursor;
	enum mh_error_code status;

	if (model->vertex_count != source->blocks[MH_MYO_VERTICES].count ||
		model->face_count != source->blocks[MH_MYO_PRIMITIVES].count ||
		model->texture_count !=
			source->blocks[MH_MYO_TEXTURE_NAMES].count ||
		model->halo_count != source->auxiliary_count ||
		model->radius != source->radius ||
		memcmp(model->minimum, source->minimum,
			sizeof(model->minimum)) != 0 ||
		memcmp(model->maximum, source->maximum,
			sizeof(model->maximum)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"decoded model metadata differs");

	cursor = 0;

	for (index = 0; index < model->face_count; index++) {
		status = mh_myo_next_face(source, &cursor, &face, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!same_face(&face, &model->faces[index]))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"decoded model face differs");
	}

	status = mh_myo_next_face(source, &cursor, &face, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (face.count != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"decoded model lost a face");

	for (index = 0; index < model->texture_count; index++)
		if (strcmp(model->textures[index],
			mh_myo_texture_name(
				source, (unsigned long)index)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"decoded model texture differs");

	for (index = 0; index < model->halo_count; index++) {
		if (!mh_myo_halo(source, (unsigned long)index, &halo) ||
			halo.resource != model->halos[index].resource ||
			memcmp(halo.endpoints, model->halos[index].endpoints,
				sizeof(halo.endpoints)) != 0 ||
			memcmp(halo.color, model->halos[index].color,
				sizeof(halo.color)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"decoded model halo differs");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_model(const struct mh_myo *source, struct counts *counts,
	struct mh_error *error_out)
{
	struct mh_model model;
	enum mh_error_code status;

	status = mh_model_myo_expand(source, &model, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = compare_model(source, &model, error_out);

	if (status == MH_ERROR_NONE) {
		counts->models++;
		counts->faces += (unsigned long)model.face_count;
		counts->textures += (unsigned long)model.texture_count;
		counts->halos += (unsigned long)model.halo_count;
	}

	mh_model_free(&model);

	return status;
}

static int
same_light(const struct mh_point_light *a, const struct mh_point_light *b)
{
	return memcmp(a->position, b->position, sizeof(a->position)) == 0 &&
		memcmp(a->color, b->color, sizeof(a->color)) == 0 &&
		a->intensity == b->intensity && a->radius == b->radius;
}

/* Raw MYW cursors address eight-byte links; decoded cursors are indices. */
static int
same_cursor(unsigned long original, size_t decoded)
{
	return original == MH_MYW_NO_LIGHT ? decoded == MH_WORLD_NO_LIGHT
					: original / 8 == decoded;
}

static int
same_world_effects(const struct mh_myw *source, const struct mh_world *world)
{
	struct mh_point_light light;
	struct mh_directional_light directional;
	struct mh_geometry_halo halo;
	unsigned long cursor;
	size_t index, next;
	int has_directional;

	if (world->light_count != source->blocks[MH_MYW_LIGHTS].count ||
		world->link_count != source->blocks[MH_MYW_LINKS].count ||
		world->halo_count != source->blocks[MH_MYW_AUXILIARY].count)
		return 0;

	has_directional = mh_myw_directional(source, &directional);

	if (world->has_directional != has_directional)
		return 0;

	if (has_directional &&
		(directional.intensity != world->directional.intensity ||
			memcmp(directional.color, world->directional.color,
				sizeof(directional.color)) != 0 ||
			memcmp(directional.direction,
				world->directional.direction,
				sizeof(directional.direction)) != 0))
		return 0;

	for (index = 0; index < world->light_count; index++)
		if (!mh_myw_light(source, (unsigned long)index, &light) ||
			!same_light(&light, &world->lights[index]))
			return 0;

	for (index = 0; index < world->link_count; index++) {
		cursor = (unsigned long)index * 8;
		next = index;

		if (!mh_myw_light_next(source, &cursor, &light) ||
			!same_cursor(cursor, world->links[index].next) ||
			!same_light(&light,
				&world->lights[world->links[index].light]) ||
			!mh_world_light_next(world, &next, &light) ||
			next != world->links[index].next)
			return 0;
	}

	for (index = 0; index < world->halo_count; index++)
		if (!mh_myw_halo(source, (unsigned long)index, &halo) ||
			halo.resource != world->halos[index].resource ||
			memcmp(halo.endpoints, world->halos[index].endpoints,
				sizeof(halo.endpoints)) != 0 ||
			memcmp(halo.color, world->halos[index].color,
				sizeof(halo.color)) != 0)
			return 0;

	return 1;
}

static int
same_world_cells(const struct mh_myw *source, const struct mh_world *world)
{
	struct mh_myw_cell cell;
	const struct mh_world_cell *decoded;
	unsigned long cursor;
	size_t index;

	if (world->width != source->width || world->height != source->height ||
		world->cell_count != source->cell_count ||
		memcmp(world->cell_size, source->cell_size,
			sizeof(world->cell_size)) != 0 ||
		memcmp(world->grid_minimum, source->grid_minimum,
			sizeof(world->grid_minimum)) != 0 ||
		memcmp(world->minimum, source->minimum,
			sizeof(world->minimum)) != 0 ||
		memcmp(world->maximum, source->maximum,
			sizeof(world->maximum)) != 0)
		return 0;

	for (index = 0; index < world->cell_count; index++) {
		decoded = &world->cells[index];

		if (!mh_myw_cell(source, index, &cell) ||
			!mh_myw_light_head(source, index, &cursor) ||
			!same_cursor(cursor, decoded->light_head) ||
			decoded->halo_first != cell.halo_first ||
			decoded->halo_count != cell.halo_count ||
			decoded->model !=
				(cell.model == MH_MYW_NO_MODEL
						? MH_WORLD_NO_MODEL
						: (size_t)cell.model))
			return 0;
	}

	return 1;
}

static enum mh_error_code
compare_world_models(const struct mh_myw *source, const struct mh_world *world,
	struct counts *counts, struct mh_error *error_out)
{
	struct mh_myo model;
	struct mh_geometry_face face;
	const struct mh_world_model *decoded;
	float center[3], radius;
	size_t index, face_index, cursor;
	enum mh_error_code status;

	if (world->face_count != source->blocks[MH_MYW_PRIMITIVES].count ||
		world->model_count != source->blocks[MH_MYW_MODELS].count ||
		world->texture_count != source->blocks[MH_MYW_NAMES].count)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"decoded world counts differ");

	for (index = 0; index < world->model_count; index++) {
		decoded = &world->models[index];
		status = mh_myw_model(
			source, (unsigned long)index, &model, error_out);

		if (status == MH_ERROR_NONE)
			status = compare_model(
				&model, &decoded->geometry, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!mh_myw_model_bounds(
			source, (unsigned long)index, center, &radius) ||
			radius != decoded->radius ||
			memcmp(center, decoded->center, sizeof(center)) != 0 ||
			decoded->geometry.textures != world->textures)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"decoded world sphere or shared names differ");

		cursor = 0;

		for (face_index = 0; face_index < decoded->geometry.face_count;
			face_index++) {
			if (decoded->face_ids[face_index] !=
				model.blocks[MH_MYO_PRIMITIVES].offset + cursor)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"decoded world face identity differs");

			status = mh_myo_next_face(
				&model, &cursor, &face, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		counts->models++;
		counts->faces += (unsigned long)decoded->geometry.face_count;
		counts->textures +=
			(unsigned long)decoded->geometry.texture_count;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_world(const struct mh_file *file, const struct mh_myw *source,
	struct counts *counts, struct mh_error *error_out)
{
	struct mh_world world;
	enum mh_error_code status;

	status = mh_world_myw_decode(file->data, file->size, &world, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!same_world_cells(source, &world) ||
		!same_world_effects(source, &world))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"decoded world cells or effects differ");
	else
		status =
			compare_world_models(source, &world, counts, error_out);

	counts->worlds++;
	counts->cells += (unsigned long)world.cell_count;
	counts->lights += (unsigned long)world.light_count;
	counts->links += (unsigned long)world.link_count;
	counts->world_halos += (unsigned long)world.halo_count;
	mh_world_free(&world);

	return status;
}

static enum mh_error_code
check_file(const struct mh_file *file, struct counts *counts,
	struct mh_error *error_out)
{
	struct mh_myw world;
	struct mh_myo model;
	enum mh_error_code status;

	if (file->size < 4 || memcmp(file->data, "!WYM", 4) != 0) {
		status =
			mh_myo_parse(file->data, file->size, &model, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		return check_model(&model, counts, error_out);
	}

	status = mh_myw_parse(file->data, file->size, &world, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return check_world(file, &world, counts, error_out);
}

int
main(int argc, char *argv[])
{
	struct mh_file file;
	struct mh_error error;
	struct counts counts;
	enum mh_error_code status;
	int i;

	if (argc < 2) {
		fprintf(stderr,
			"Usage: mh-model-data-check FILE.MYO|FILE.MYW ...\n");
		return 2;
	}

	memset(&counts, 0, sizeof(counts));

	for (i = 1; i < argc; i++) {
		status = mh_file_read(argv[i], &file, &error);

		if (status == MH_ERROR_NONE) {
			status = check_file(&file, &counts, &error);
			free(file.data);
		}

		if (status != MH_ERROR_NONE) {
			fprintf(stderr, "%s: %s\n", argv[i], error.message);
			return 1;
		}
	}

	printf("Matched %lu models (including %lu worlds), %lu faces, %lu texture names, %lu attached halos\n",
		counts.models, counts.worlds, counts.faces, counts.textures,
		counts.halos);

	printf("Matched %lu world cells, %lu lights, %lu light links and %lu world halos\n",
		counts.cells, counts.lights, counts.links, counts.world_halos);

	return 0;
}
