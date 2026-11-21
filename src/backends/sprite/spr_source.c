#include <stdlib.h>
#include <string.h>

#include "backends/sprite/spr_provider.h"
#include "engine/assets/spr.h"
#include "engine/storage/asset_path.h"
#include "interfaces/sprite/collection_backend.h"
#include "interfaces/storage/resource.h"

struct spr_collection {
	struct mh_file		file;
	struct mh_spr		archive;
	enum mh_picture_format	format;
};

static void
close_collection(void *context, void *state)
{
	struct spr_collection *collection;

	(void)context;
	collection = state;
	mh_sprite_spr_free(&collection->archive);
	free(collection->file.data);
	free(collection);
}

static enum mh_error_code
open_collection(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, enum mh_picture_format format,
	void **output, struct mh_error *error_out)
{
	struct spr_collection *collection;
	char path[1024];
	enum mh_error_code status;

	(void)context;

	if (format != MH_PICTURE_INDEXED && format != MH_PICTURE_RGB555 &&
		format != MH_PICTURE_RGB565)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported SPR color model");

	status = mh_asset_path_from(
		resources, root, reference, path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	collection = calloc(1, sizeof(*collection));

	if (collection == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening SPR collection");

	collection->format = format;
	status = mh_resource_source_read(
		resources, path, &collection->file, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_sprite_spr_read(collection->file.data,
			collection->file.size, &collection->archive, error_out);

	if (status != MH_ERROR_NONE) {
		close_collection(NULL, collection);
		return status;
	}

	*output = collection;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
column_sprite(const struct spr_collection *collection, const char *name)
{
	/* Original GAME0..3 meter programs are column-coded; maps use rows. */
	return collection->format != MH_PICTURE_INDEXED &&
		strcmp(name, "meter") == 0;
}

static enum mh_error_code
sprite_info(void *context, void *state, const char *name,
	struct mh_sprite_info *output, struct mh_error *error_out)
{
	const struct spr_collection *collection;
	const struct mh_spr_entry *entry;
	struct mh_sprite_info info;

	(void)context;
	collection = state;
	entry = mh_sprite_spr_find(&collection->archive, name);

	if (entry == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"sprite missing from SPR collection");

	info.frames = entry->frames;
	info.width = entry->width;
	info.height = entry->height;

	if (column_sprite(collection, name)) {
		info.width = entry->height;
		info.height = entry->width;
	} else if (collection->format != MH_PICTURE_INDEXED) {
		if (info.width % 2 != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"odd SPR literal-color row width");

		info.width /= 2;
	}

	*output = info;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sprite_palette(void *context, void *state, struct mh_image_palette *output,
	struct mh_error *error_out)
{
	const struct spr_collection *collection;

	(void)context;
	collection = state;

	if (collection->format != MH_PICTURE_INDEXED)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"literal-color SPR has no indexed palette");

	output->count = MH_IMAGE_PALETTE_COLORS;
	memcpy(output->rgb,
		collection->file.data + collection->archive.palette_offset,
		sizeof(output->rgb));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sprite_indexed(void *context, void *state, const char *name, unsigned int frame,
	struct mh_sprite_indexed *output, struct mh_error *error_out)
{
	const struct spr_collection *collection;
	const struct mh_spr_entry *entry;

	(void)context;
	collection = state;

	if (collection->format != MH_PICTURE_INDEXED)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"literal-color SPR has no indexed view");

	entry = mh_sprite_spr_find(&collection->archive, name);

	if (entry == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"sprite missing from SPR collection");

	return mh_sprite_spr_decode_indexed(&collection->archive,
		(unsigned int)(entry - collection->archive.entries), frame,
		output, error_out);
}

static enum mh_error_code
sprite_rgba(void *context, void *state, const char *name, unsigned int frame,
	struct mh_image *output, struct mh_error *error_out)
{
	const struct spr_collection *collection;
	const struct mh_spr_entry *entry;
	unsigned int index;

	(void)context;
	collection = state;
	entry = mh_sprite_spr_find(&collection->archive, name);

	if (entry == NULL)
		return mh_error_set(error_out, MH_ERROR_NOT_FOUND,
			"sprite missing from SPR collection");

	index = (unsigned int)(entry - collection->archive.entries);

	if (collection->format == MH_PICTURE_INDEXED)
		return mh_sprite_spr_decode_rgba(
			&collection->archive, index, frame, output, error_out);

	return mh_sprite_spr_decode_literal_rgba(&collection->archive, index,
		frame, column_sprite(collection, name),
		collection->format == MH_PICTURE_RGB565 ? 6 : 5, output,
		error_out);
}

enum mh_error_code
mh_sprite_spr_open(const unsigned char *data, size_t size,
	enum mh_picture_format format, struct mh_sprite_collection **output,
	struct mh_error *error_out)
{
	struct spr_collection *collection;
	enum mh_error_code status;

	if (data == NULL || size == 0 || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing SPR collection bytes");

	if (format != MH_PICTURE_INDEXED && format != MH_PICTURE_RGB555 &&
		format != MH_PICTURE_RGB565)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported SPR color model");

	collection = calloc(1, sizeof(*collection));

	if (collection == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening SPR collection");

	collection->file.data = malloc(size);

	if (collection->file.data == NULL) {
		free(collection);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory copying SPR collection");
	}

	collection->format = format;
	collection->file.size = size;
	memcpy(collection->file.data, data, size);
	status = mh_sprite_spr_read(
		collection->file.data, size, &collection->archive, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_sprite_collection_create(
			mh_sprite_spr_source(), collection, output, error_out);

	if (status != MH_ERROR_NONE)
		close_collection(NULL, collection);

	return status;
}

const struct mh_sprite_source_provider *
mh_sprite_spr_source(void)
{
	static const struct mh_sprite_source_ops ops = {open_collection,
		sprite_info, sprite_palette, sprite_indexed, sprite_rgba,
		close_collection};

	static const struct mh_sprite_source_provider provider = {&ops, NULL};

	return &provider;
}
