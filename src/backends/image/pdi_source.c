#include <stdlib.h>
#include <string.h>

#include "backends/image/pdi_provider.h"
#include "engine/assets/pdi.h"
#include "engine/storage/asset_path.h"
#include "interfaces/image/collection_backend.h"
#include "interfaces/storage/resource.h"

struct pdi_collection {
	struct mh_file	file;
	struct mh_pdi	archive;
};

static enum mh_error_code
open_collection(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, void **output,
	struct mh_error *error_out)
{
	struct pdi_collection *collection;
	char path[1024];
	enum mh_error_code status;

	(void)context;
	status = mh_asset_path_from(
		resources, root, reference, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	collection = calloc(1, sizeof(*collection));

	if (collection == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening PDI images");

	status = mh_resource_source_read(
		resources, path, &collection->file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_pdi_parse(collection->file.data,
			collection->file.size, &collection->archive, error_out);

	if (status != MH_ERROR_NONE) {
		free(collection->file.data);
		free(collection);
		return status;
	}

	*output = collection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_image(void *context, void *state, const char *name, struct mh_file *output,
	struct mh_error *error_out)
{
	const struct pdi_collection *collection;
	struct mh_pdi_entry entry;
	struct mh_file file;

	(void)context;
	collection = state;

	if (!mh_pdi_find(&collection->archive, name, &entry))
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"image missing from PDI collection");

	file.data = malloc(entry.size == 0 ? 1 : entry.size);

	if (file.data == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory reading PDI image");

	file.size = entry.size;
	memcpy(file.data, entry.data, file.size);
	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_collection(void *context, void *state)
{
	struct pdi_collection *collection;

	(void)context;
	collection = state;
	free(collection->file.data);
	free(collection);
}

enum mh_error_code
mh_image_pdi_open(const unsigned char *data, size_t size,
	struct mh_image_collection **output, struct mh_error *error_out)
{
	struct pdi_collection *collection;
	enum mh_error_code status;

	if (data == NULL || size == 0 || output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing PDI image bytes");

	collection = calloc(1, sizeof(*collection));

	if (collection == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening PDI images");

	collection->file.data = malloc(size);

	if (collection->file.data == NULL) {
		free(collection);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying PDI images");
	}

	collection->file.size = size;
	memcpy(collection->file.data, data, size);
	status = mh_pdi_parse(
		collection->file.data, size, &collection->archive, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_image_collection_create(
			mh_image_pdi_source(), collection, output, error_out);

	if (status != MH_ERROR_NONE)
		close_collection(NULL, collection);

	return status;
}

const struct mh_image_source_provider *
mh_image_pdi_source(void)
{
	static const struct mh_image_source_ops ops = {
		open_collection, read_image, close_collection};

	static const struct mh_image_source_provider provider = {&ops, NULL};

	return &provider;
}
