#include <stdlib.h>

#include "interfaces/image/collection_backend.h"

struct mh_image_collection {
	const struct mh_image_source_provider	*provider;
	void					*state;
};

enum mh_error_code
mh_image_collection_create(const struct mh_image_source_provider *provider,
	void *state, struct mh_image_collection **output,
	struct mh_error *error_out)
{
	struct mh_image_collection *collection;

	if (provider == NULL || provider->ops == NULL ||
		provider->ops->read == NULL || provider->ops->close == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid image collection backend");

	collection = malloc(sizeof(*collection));

	if (collection == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening image collection");

	collection->provider = provider;
	collection->state = state;
	*output = collection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_collection_open(const struct mh_image_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, struct mh_image_collection **output,
	struct mh_error *error_out)
{
	struct mh_image_collection *collection;
	enum mh_error_code status;

	if (provider == NULL || provider->ops == NULL ||
		provider->ops->open == NULL || provider->ops->read == NULL ||
		provider->ops->close == NULL || resources == NULL ||
		root == NULL || reference == NULL || reference[0] == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid image collection request");

	collection = NULL;
	status = mh_image_collection_create(
		provider, NULL, &collection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = provider->ops->open(provider->context, resources, root,
		reference, &collection->state, error_out);

	if (status != MH_ERROR_NONE) {
		free(collection);
		return status;
	}

	*output = collection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_image_collection_read(const struct mh_image_collection *collection,
	const char *name, struct mh_file *output, struct mh_error *error_out)
{
	const struct mh_image_source_provider *provider;
	struct mh_file file;
	enum mh_error_code status;

	if (collection == NULL || name == NULL || name[0] == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid image member request");

	provider = collection->provider;
	file.data = NULL;
	file.size = 0;
	status = provider->ops->read(
		provider->context, collection->state, name, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (file.data == NULL || file.size == 0) {
		free(file.data);
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"empty image collection member");
	}

	*output = file;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_image_collection_close(struct mh_image_collection *collection)
{
	const struct mh_image_source_provider *provider;

	if (collection == NULL)
		return;

	provider = collection->provider;
	provider->ops->close(provider->context, collection->state);
	free(collection);
}
