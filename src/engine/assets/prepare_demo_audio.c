#include <stdlib.h>
#include <string.h>

#include "engine/assets/path_audio.h"
#include "engine/assets/path_directories.h"
#include "engine/assets/prepare_demo_audio.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

#define CRASH_BYTES_LIMIT (64UL * 1024 * 1024)

enum {
	AUDIO_PATH_BYTES = 1024,
	CRASH_ALIASES = 2
};

enum mh_error_code
mh_demo_audio_prepare(const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *recipe,
	const char *root, struct mh_error *error_out)
{
	static const char *const names[CRASH_ALIASES] = {
		MH_ASSET_SAMPLE_BARREL, MH_ASSET_SAMPLE_CRASH2};

	char directory[AUDIO_PATH_BYTES], path[AUDIO_PATH_BYTES];
	char destinations[CRASH_ALIASES][AUDIO_PATH_BYTES];
	struct mh_file sample;
	size_t length;
	unsigned int i;
	int found;
	enum mh_error_code status;

	if (source == NULL || data == NULL || recipe == NULL || root == NULL ||
		root[0] == '\0' ||
		(unsigned int)recipe->track_layout >= MH_DEMO_TRACK_LAYOUTS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo audio preparation request");

	if (recipe->track_layout != MH_DEMO_TRACK_SOFTWARE_FIRST)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_asset_path_from(source, root, MH_ASSET_SOUNDS_DIR,
		directory, sizeof(directory), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	length = strlen(directory);

	if (length > sizeof(path) - sizeof("/" MH_ASSET_SAMPLE_MAJCRASH))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo crash sample path is too long");

	for (i = 0; i < CRASH_ALIASES; i++) {
		status = mh_asset_path_find_from(source, directory, names[i],
			path, sizeof(path), &found, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (found)
			return mh_error_set(error_out, MH_ERROR_EXISTS,
				"demo crash sample destination already exists");

		memcpy(destinations[i], directory, length);
		destinations[i][length] = '/';
		strcpy(destinations[i] + length + 1, names[i]);
	}

	status = mh_asset_path_from(source, directory, MH_ASSET_SAMPLE_MAJCRASH,
		path, sizeof(path), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read_bounded(
		source, path, CRASH_BYTES_LIMIT, &sample, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < CRASH_ALIASES && status == MH_ERROR_NONE; i++)
		status = mh_user_data_replace(data, destinations[i],
			sample.data, sample.size, error_out);

	free(sample.data);

	return status;
}
