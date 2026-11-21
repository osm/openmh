#include <stdlib.h>
#include <string.h>

#include "backends/geometry/myo_provider.h"
#include "backends/geometry/myw_provider.h"
#include "engine/core/text.h"
#include "engine/geometry/model.h"
#include "engine/geometry/world.h"
#include "geometry_source.h"

static void *
copy_array(const void *data, size_t count, size_t width)
{
	void *copy;

	if (count == 0 || count > (size_t)-1 / width)
		return NULL;

	copy = malloc(count * width);

	if (copy != NULL)
		memcpy(copy, data, count * width);

	return copy;
}

/* The caller owns partial names even when allocation fails. */
static int
copy_names(char *const *names, size_t count, char ***output)
{
	char **copy;
	size_t i;

	*output = NULL;

	if (count == 0)
		return 1;

	copy = calloc(count, sizeof(*copy));
	*output = copy;

	if (copy == NULL)
		return 0;

	for (i = 0; i < count; i++) {
		copy[i] = copy_array(names[i], strlen(names[i]) + 1, 1);

		if (copy[i] == NULL)
			return 0;
	}

	return 1;
}

/* Model arrays are owned locally; the caller supplies texture ownership. */
static int
copy_model_arrays(const struct mh_model *source, struct mh_model *copy)
{
	*copy = *source;
	copy->textures = NULL;
	copy->faces = copy_array(
		source->faces, source->face_count, sizeof(*source->faces));
	copy->halos = copy_array(
		source->halos, source->halo_count, sizeof(*source->halos));

	return (copy->face_count == 0 || copy->faces != NULL) &&
		(copy->halo_count == 0 || copy->halos != NULL);
}

static enum mh_error_code
copy_model(const struct mh_model *source, struct mh_model *output,
	struct mh_error *error_out)
{
	struct mh_model copy;

	if (!copy_model_arrays(source, &copy) ||
		!copy_names(source->textures, source->texture_count,
			&copy.textures)) {
		mh_model_free(&copy);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying model fixture");
	}

	*output = copy;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
copy_world_models(const struct mh_world *source, struct mh_world *copy)
{
	const struct mh_world_model *original;
	struct mh_world_model *model;
	size_t i;

	if (copy->model_count == 0)
		return 1;

	copy->models = calloc(copy->model_count, sizeof(*copy->models));

	if (copy->models == NULL)
		return 0;

	for (i = 0; i < copy->model_count; i++) {
		original = &source->models[i];
		model = &copy->models[i];
		memcpy(model->center, original->center, sizeof(model->center));
		model->radius = original->radius;

		if (!copy_model_arrays(&original->geometry, &model->geometry))
			return 0;

		model->geometry.textures = copy->textures;
		model->face_ids = copy_array(original->face_ids,
			original->geometry.face_count,
			sizeof(*original->face_ids));

		if (model->geometry.face_count != 0 && model->face_ids == NULL)
			return 0;
	}

	return 1;
}

static enum mh_error_code
copy_world(const struct mh_world *source, struct mh_world *output,
	struct mh_error *error_out)
{
	struct mh_world copy;

	copy = *source;
	copy.models = NULL;
	copy.textures = NULL;
	copy.cells = copy_array(
		source->cells, source->cell_count, sizeof(*source->cells));
	copy.lights = copy_array(
		source->lights, source->light_count, sizeof(*source->lights));
	copy.links = copy_array(
		source->links, source->link_count, sizeof(*source->links));
	copy.halos = copy_array(
		source->halos, source->halo_count, sizeof(*source->halos));

	if ((copy.cell_count != 0 && copy.cells == NULL) ||
		(copy.light_count != 0 && copy.lights == NULL) ||
		(copy.link_count != 0 && copy.links == NULL) ||
		(copy.halo_count != 0 && copy.halos == NULL) ||
		!copy_names(source->textures, source->texture_count,
			&copy.textures) ||
		!copy_world_models(source, &copy)) {
		mh_world_free(&copy);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying world fixture");
	}

	*output = copy;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
model_decode(void *context, const unsigned char *data, size_t size,
	struct mh_model *output, struct mh_error *error_out)
{
	struct mh_test_geometry_source *source;
	size_t index;

	source = context;

	if (size != 2)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid alternate model fixture");

	index = data[0] + (size_t)data[1] * 256;

	if (index >= source->model_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing alternate model fixture");

	source->model_reads++;

	return copy_model(&source->models[index], output, error_out);
}

static enum mh_error_code
world_decode(void *context, const unsigned char *data, size_t size,
	struct mh_world *output, struct mh_error *error_out)
{
	struct mh_test_geometry_source *source;
	size_t index;

	source = context;

	if (size != 2)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid alternate world fixture");

	index = data[0] + (size_t)data[1] * 256;

	if (index >= source->world_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing alternate world fixture");

	source->world_reads++;

	return copy_world(&source->worlds[index], output, error_out);
}

void
mh_test_geometry_decoders(struct mh_test_geometry_source *source,
	struct mh_model_decoder *models, struct mh_world_decoder *worlds)
{
	models->context = source;

	models->extension = "fixturemodel";
	models->decode = model_decode;
	worlds->context = source;
	worlds->extension = "fixtureworld";
	worlds->decode = world_decode;
}

void
mh_test_geometry_source_free(struct mh_test_geometry_source *source)
{
	size_t i;

	for (i = 0; i < source->model_count; i++)
		mh_model_free(&source->models[i]);

	for (i = 0; i < source->world_count; i++)
		mh_world_free(&source->worlds[i]);

	memset(source, 0, sizeof(*source));
}

enum mh_error_code
mh_test_geometry_convert(struct mh_test_geometry_source *source,
	struct mh_resource_entry *entry, struct mh_error *error_out)
{
	const char *extension;
	char *path;
	unsigned char *data;
	size_t length, *count;
	int is_model;
	enum mh_error_code status;

	length = strlen(entry->path);

	if (length < 4)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	is_model = mh_ascii_equal(entry->path + length - 4, ".myo");

	if (!is_model && !mh_ascii_equal(entry->path + length - 4, ".myw"))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	extension = is_model ? ".fixturemodel" : ".fixtureworld";
	count = is_model ? &source->model_count : &source->world_count;

	if (*count == MH_TEST_GEOMETRY_ENTRIES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"too many geometry fixtures");

	path = malloc(length - 4 + strlen(extension) + 1);
	data = malloc(2);

	if (path == NULL || data == NULL) {
		free(path);
		free(data);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory converting geometry fixture");
	}

	if (is_model)
		status = mh_model_decode(mh_model_myo_decoder(), entry->data,
			entry->size, &source->models[*count], error_out);
	else
		status = mh_world_decode(mh_world_myw_decoder(), entry->data,
			entry->size, &source->worlds[*count], error_out);

	if (status != MH_ERROR_NONE) {
		free(path);
		free(data);
		return status;
	}

	memcpy(path, entry->path, length - 4);
	strcpy(path + length - 4, extension);
	data[0] = (unsigned char)(*count & 255);
	data[1] = (unsigned char)(*count >> 8);
	(*count)++;
	free((void *)entry->path);
	free((void *)entry->data);
	entry->path = path;
	entry->data = data;
	entry->size = 2;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
