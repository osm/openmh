#include <stdlib.h>
#include <string.h>

#include "engine/assets/myo_decode.h"
#include "engine/assets/texture_names.h"
#include "engine/geometry/model.h"

static int
allocation_fits(unsigned long count, size_t element_size)
{
	return count <= (size_t)-1 / element_size;
}

static enum mh_error_code
allocate_model(const struct mh_myo *source, struct mh_model *model,
	struct mh_error *error_out)
{
	if (!allocation_fits(source->blocks[MH_MYO_VERTICES].count, 1) ||
		!allocation_fits(source->blocks[MH_MYO_PRIMITIVES].count,
			sizeof(*model->faces)) ||
		!allocation_fits(source->blocks[MH_MYO_TEXTURE_NAMES].count,
			sizeof(*model->textures)) ||
		!allocation_fits(
			source->auxiliary_count, sizeof(*model->halos)))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"decoded model is too large");

	model->vertex_count = (size_t)source->blocks[MH_MYO_VERTICES].count;
	model->face_count = (size_t)source->blocks[MH_MYO_PRIMITIVES].count;
	model->texture_count =
		(size_t)source->blocks[MH_MYO_TEXTURE_NAMES].count;
	model->halo_count = (size_t)source->auxiliary_count;
	model->radius = source->radius;
	memcpy(model->minimum, source->minimum, sizeof(model->minimum));
	memcpy(model->maximum, source->maximum, sizeof(model->maximum));

	if (model->face_count != 0)
		model->faces = (struct mh_geometry_face *)calloc(
			model->face_count, sizeof(*model->faces));

	if (model->texture_count != 0)
		model->textures = (char **)calloc(
			model->texture_count, sizeof(*model->textures));

	if (model->halo_count != 0)
		model->halos = (struct mh_geometry_halo *)calloc(
			model->halo_count, sizeof(*model->halos));

	if ((model->face_count != 0 && model->faces == NULL) ||
		(model->texture_count != 0 && model->textures == NULL) ||
		(model->halo_count != 0 && model->halos == NULL))
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory decoding model");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
decode_faces(const struct mh_myo *source, struct mh_model *model,
	struct mh_error *error_out)
{
	size_t index, cursor;
	enum mh_error_code status;

	cursor = 0;

	for (index = 0; index < model->face_count; index++) {
		status = mh_myo_next_face(
			source, &cursor, &model->faces[index], error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (model->faces[index].count == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated model geometry");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
decode_halos(const struct mh_myo *source, struct mh_model *model,
	struct mh_error *error_out)
{
	size_t index;

	for (index = 0; index < model->halo_count; index++)
		if (!mh_myo_halo(
			source, (unsigned long)index, &model->halos[index]))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid model halo");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_model_myo_expand(const struct mh_myo *source, struct mh_model *output,
	struct mh_error *error_out)
{
	struct mh_model model;
	enum mh_error_code status;

	if (source == NULL || source->data == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing model decode input");

	memset(&model, 0, sizeof(model));
	status = allocate_model(source, &model, error_out);

	if (status == MH_ERROR_NONE)
		status = decode_faces(source, &model, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_texture_names_copy(source->data +
				source->blocks[MH_MYO_TEXTURE_NAMES].offset,
			source->blocks[MH_MYO_TEXTURE_NAMES].size,
			model.texture_count, model.textures,
			"out of memory decoding model texture names",
			error_out);

	if (status == MH_ERROR_NONE)
		status = decode_halos(source, &model, error_out);

	if (status != MH_ERROR_NONE) {
		mh_model_free(&model);
		return status;
	}

	*output = model;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_model_myo_decode(const unsigned char *data, size_t size,
	struct mh_model *model, struct mh_error *error_out)
{
	struct mh_myo source;
	enum mh_error_code status;

	if (model == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing decoded model output");

	status = mh_myo_parse(data, size, &source, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_model_myo_expand(&source, model, error_out);
}
