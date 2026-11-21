#include <stdlib.h>

#include "interfaces/sprite/collection_backend.h"

struct mh_sprite_collection {
	const struct mh_sprite_source_provider	*provider;
	void					*state;
};

enum mh_error_code
mh_sprite_collection_create(const struct mh_sprite_source_provider *provider,
	void *state, struct mh_sprite_collection **output,
	struct mh_error *error_out)
{
	struct mh_sprite_collection *collection;

	if (provider == NULL || provider->ops == NULL ||
		provider->ops->info == NULL || provider->ops->close == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sprite collection backend");

	collection = malloc(sizeof(*collection));

	if (collection == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening sprite collection");

	collection->provider = provider;
	collection->state = state;
	*output = collection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sprite_collection_open(const struct mh_sprite_source_provider *provider,
	const struct mh_resource_source *resources, const char *root,
	const char *reference, enum mh_picture_format format,
	struct mh_sprite_collection **output, struct mh_error *error_out)
{
	struct mh_sprite_collection *collection;
	enum mh_error_code status;

	if (provider == NULL || provider->ops == NULL ||
		provider->ops->open == NULL || provider->ops->info == NULL ||
		provider->ops->close == NULL || resources == NULL ||
		root == NULL || reference == NULL || reference[0] == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sprite collection request");

	collection = NULL;
	status = mh_sprite_collection_create(
		provider, NULL, &collection, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = provider->ops->open(provider->context, resources, root,
		reference, format, &collection->state, error_out);

	if (status != MH_ERROR_NONE) {
		free(collection);
		return status;
	}

	*output = collection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sprite_collection_info(const struct mh_sprite_collection *collection,
	const char *name, struct mh_sprite_info *output,
	struct mh_error *error_out)
{
	const struct mh_sprite_source_provider *provider;

	if (collection == NULL || name == NULL || name[0] == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sprite metadata request");

	provider = collection->provider;

	return provider->ops->info(
		provider->context, collection->state, name, output, error_out);
}

enum mh_error_code
mh_sprite_collection_palette(const struct mh_sprite_collection *collection,
	struct mh_image_palette *output, struct mh_error *error_out)
{
	const struct mh_sprite_source_provider *provider;

	if (collection == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid sprite palette request");

	provider = collection->provider;

	if (provider->ops->palette == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"sprite palette unavailable");

	return provider->ops->palette(
		provider->context, collection->state, output, error_out);
}

enum mh_error_code
mh_sprite_collection_indexed(const struct mh_sprite_collection *collection,
	const char *name, unsigned int frame, struct mh_sprite_indexed *output,
	struct mh_error *error_out)
{
	const struct mh_sprite_source_provider *provider;

	if (collection == NULL || name == NULL || name[0] == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid indexed sprite request");

	provider = collection->provider;

	if (provider->ops->indexed == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"indexed sprite view unavailable");

	return provider->ops->indexed(provider->context, collection->state,
		name, frame, output, error_out);
}

enum mh_error_code
mh_sprite_collection_rgba(const struct mh_sprite_collection *collection,
	const char *name, unsigned int frame, struct mh_image *output,
	struct mh_error *error_out)
{
	const struct mh_sprite_source_provider *provider;

	if (collection == NULL || name == NULL || name[0] == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid RGBA sprite request");

	provider = collection->provider;

	if (provider->ops->rgba == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"RGBA sprite view unavailable");

	return provider->ops->rgba(provider->context, collection->state, name,
		frame, output, error_out);
}

void
mh_sprite_collection_close(struct mh_sprite_collection *collection)
{
	const struct mh_sprite_source_provider *provider;

	if (collection == NULL)
		return;

	provider = collection->provider;
	provider->ops->close(provider->context, collection->state);
	free(collection);
}
