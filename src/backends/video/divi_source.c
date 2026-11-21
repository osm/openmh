#include <string.h>

#include "backends/video/divi_provider.h"
#include "engine/assets/divi_data.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/video/source_backend.h"

#define COLLECTION_PATH_BYTES 1024

static enum mh_error_code
resolve_collection(void *context, const struct mh_resource_source *resources,
	const char *root, const char *reference, char *output, size_t capacity,
	int *found, struct mh_error *error_out)
{
	char path[COLLECTION_PATH_BYTES];
	size_t length;
	int present;
	enum mh_error_code status;

	(void)context;
	status = mh_asset_path_find_from(resources, root, reference, path,
		sizeof(path), &present, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (present) {
		length = strlen(path);

		if (length >= capacity)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"video collection reference is too long");

		memcpy(output, path, length + 1);
	}

	*found = present;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
read_clip(void *context, const struct mh_resource_source *resources,
	const char *reference, int collection, unsigned int index,
	struct mh_file *output, struct mh_error *error_out)
{
	(void)context;

	return collection ? mh_video_divi_read(resources, reference, index,
				output, error_out)
			: mh_resource_source_read(
				resources, reference, output, error_out);
}

const struct mh_video_source_provider *
mh_video_divi_source(void)
{
	static const struct mh_video_source_ops ops = {
		resolve_collection, read_clip};

	static const struct mh_video_source_provider provider = {&ops, NULL};

	return &provider;
}
