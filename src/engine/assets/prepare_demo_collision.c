#include <stdlib.h>
#include <string.h>

#include "engine/assets/col.h"
#include "engine/assets/executable_collision.h"
#include "engine/assets/prepare_demo_collision.h"
#include "engine/assets/prepare_demo_recipe.h"
#include "engine/storage/asset_path.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

enum {
	COLLISION_FILES = 2,
	COLLISION_PATH_BYTES = 1024
};

static enum mh_error_code
prepare_file(const struct mh_resource_source *source, const char *root,
	const struct mh_file *executable,
	const struct mh_collision_executable_file *file,
	char path[COLLISION_PATH_BYTES], struct mh_error *error_out)
{
	struct mh_col collision;
	char directory[COLLISION_PATH_BYTES];
	size_t length;
	int found;
	enum mh_error_code status;

	if (file->offset > executable->size ||
		file->size > executable->size - file->offset)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated embedded demo collision data");

	status = mh_col_parse(executable->data + file->offset, file->size,
		&collision, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_asset_path_from(source, root, file->directory, directory,
		sizeof(directory), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_asset_path_find_from(source, directory, file->name, path,
		COLLISION_PATH_BYTES, &found, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (found)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"demo collision destination already exists");

	length = strlen(directory);

	if (length >= COLLISION_PATH_BYTES - 1 ||
		strlen(file->name) >= COLLISION_PATH_BYTES - length - 1)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"demo collision destination is too long");

	memcpy(path, directory, length);
	path[length++] = '/';
	strcpy(path + length, file->name);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_collision_prepare(const struct mh_resource_source *source,
	const struct mh_user_data *data, const struct mh_demo_recipe *demo,
	const char *root, struct mh_error *error_out)
{
	const struct mh_collision_executable_layout *recipe;
	const struct mh_collision_executable_file *file;
	struct mh_file executable;
	char paths[COLLISION_FILES][COLLISION_PATH_BYTES];
	unsigned int i;
	enum mh_error_code status;

	if (source == NULL || data == NULL || demo == NULL ||
		demo->id == NULL || root == NULL || root[0] == '\0')
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo collision preparation request");

	recipe = mh_collision_executable_layout(demo->id);

	if (recipe == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(&executable, 0, sizeof(executable));
	status = mh_collision_executable_open(
		source, root, recipe, &executable, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < recipe->count; i++)
		status = prepare_file(source, root, &executable,
			&recipe->files[i], paths[i], error_out);

	for (i = 0; status == MH_ERROR_NONE && i < recipe->count; i++) {
		file = &recipe->files[i];
		status = mh_user_data_replace(data, paths[i],
			executable.data + file->offset, file->size, error_out);
	}

	free(executable.data);

	return status;
}
