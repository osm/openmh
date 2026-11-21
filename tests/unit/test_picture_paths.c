#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/storage/memory_user_data.h"
#include "engine/storage/picture_path.h"
#include "interfaces/storage/data_directory.h"

#define CHECK(x)                                                               \
	do {                                                                   \
		if (!(x)) {                                                    \
			fprintf(stderr, "picture paths:%d: %s\n", __LINE__,    \
				#x);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

static enum mh_error_code
user_root(void *context, char **output, struct mh_error *error_out)
{
	char *path;

	(void)context;
	path = malloc(sizeof("User/"));

	if (path == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "root allocation failed");

	strcpy(path, "User/");
	*output = path;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
protected_parent(void *context, const char *path, const char *resource_root,
	struct mh_data_directory **output, struct mh_error *error_out)
{
	(void)context;

	(void)path;
	(void)resource_root;
	(void)output;

	return mh_error_set(error_out, MH_ERROR_PERMISSION, "protected root");
}

int
main(void)
{
	const struct mh_user_data *data;
	struct mh_user_data selected;
	struct mh_user_data_ops ops;
	struct mh_data_directory *directory;
	char path[MH_PICTURE_DIRECTORY_SIZE],
		large[MH_PICTURE_DIRECTORY_SIZE + 1];

	CHECK(mh_memory_user_data_open(&data, NULL) == MH_ERROR_NONE);
	strcpy(path, "unchanged");
	directory = NULL;
	CHECK(mh_picture_directory_open_from(data, "Missing", "assets", path,
		&directory, NULL) == MH_ERROR_NOT_FOUND);
	CHECK(strcmp(path, "unchanged") == 0 && directory == NULL);
	CHECK(mh_user_data_check_directory(data, "Missing", NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mh_picture_directory_open_from(NULL, NULL, "assets", path,
		&directory, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(strcmp(path, "unchanged") == 0 && directory == NULL);
	memset(large, 'x', sizeof(large) - 1);
	large[sizeof(large) - 1] = 0;
	CHECK(mh_picture_directory_open_from(data, large, "assets", path,
		&directory, NULL) == MH_ERROR_RANGE);
	CHECK(strcmp(path, "unchanged") == 0 && directory == NULL);

	CHECK(mh_picture_directory_open_from(data, NULL, "assets", path,
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "") == 0);
	mh_data_directory_close(directory);
	CHECK(mh_user_data_check_directory(data, "Screenshots", NULL) ==
		MH_ERROR_NOT_FOUND);
	CHECK(mh_user_data_create_directory(data, "Screenshots", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_picture_directory_open_from(data, "Screenshots/", "assets",
		path, &directory, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "Screenshots") == 0);
	mh_data_directory_close(directory);
	CHECK(mh_picture_directory_open_from(data, "", "assets", path,
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(path[0] == 0);
	mh_data_directory_close(directory);

	selected = *data;
	ops = *data->ops;
	selected.ops = &ops;
	ops.directory = user_root;
	CHECK(mh_user_data_create_directory(data, "User", NULL) ==
		MH_ERROR_NONE);
	ops.open_directory = protected_parent;
	directory = NULL;
	strcpy(path, "unchanged");
	CHECK(mh_picture_directory_open_from(&selected, NULL, "assets", path,
		&directory, NULL) == MH_ERROR_PERMISSION);
	CHECK(strcmp(path, "unchanged") == 0 && directory == NULL);
	CHECK(mh_user_data_check_directory(data, "User/Screenshots", NULL) ==
		MH_ERROR_NOT_FOUND);
	ops.open_directory = NULL;
	CHECK(mh_picture_directory_open_from(&selected, NULL, "assets", path,
		&directory, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(strcmp(path, "unchanged") == 0 && directory == NULL);
	ops.open_directory = data->ops->open_directory;
	CHECK(mh_picture_directory_open_from(&selected, NULL, "assets", path,
		&directory, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(path, "User") == 0);
	mh_data_directory_close(directory);
	mh_memory_user_data_close(data);

	return 0;
}
