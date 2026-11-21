#include <stdlib.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/storage/picture_store.h"
#include "interfaces/storage/data_directory.h"

/* Space for full RGBA captures and encoder overhead, independent of codec. */
#define PICTURE_MAX_FILE_SIZE 134217728UL

enum {
	PICTURE_PREFIX_SIZE = 5,
	PICTURE_INDEX_DIGITS = 10,
	PICTURE_EXTENSION_MAX = 8
};

struct mh_picture_store {
	struct mh_data_directory	*directory;
};

struct picture_scan {
	const char	*name;
	int		found;
};

void
mh_picture_store_close(struct mh_picture_store *store)
{
	if (store == NULL)
		return;

	mh_data_directory_close(store->directory);
	free(store);
}

enum mh_error_code
mh_picture_store_open_in(struct mh_data_directory *directory,
	struct mh_picture_store **output, struct mh_error *error_out)
{
	struct mh_picture_store *store;
	enum mh_error_code status;

	if (directory == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid screenshot directory");

	status = mh_data_directory_validate(directory, NULL);

	if (status != MH_ERROR_NONE)
		return mh_error_set(error_out, status,
			"screenshot directory is unavailable");

	store = (struct mh_picture_store *)malloc(sizeof(*store));

	if (store == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening screenshot directory");

	store->directory = directory;
	*output = store;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
valid_filename(const char *name)
{
	const char *suffix;
	unsigned int length;

	if (name == NULL ||
		(strncmp(name, "motor", PICTURE_PREFIX_SIZE) != 0 &&
			strncmp(name, "lobby", PICTURE_PREFIX_SIZE) != 0))
		return 0;

	suffix = name + PICTURE_PREFIX_SIZE;

	while (*suffix >= '0' && *suffix <= '9' &&
		suffix - name < PICTURE_PREFIX_SIZE + PICTURE_INDEX_DIGITS)
		suffix++;

	if (suffix == name + PICTURE_PREFIX_SIZE || *suffix++ != '.')
		return 0;

	for (length = 0; suffix[length] != 0; length++)
		if (length >= PICTURE_EXTENSION_MAX ||
			!((suffix[length] >= 'a' && suffix[length] <= 'z') ||
				(suffix[length] >= '0' &&
					suffix[length] <= '9')))
			return 0;

	return length != 0;
}

static enum mh_directory_action
visit_picture(void *context, const char *name)
{
	struct picture_scan *scan;

	scan = context;

	if (!mh_ascii_equal(scan->name, name))
		return MH_DIRECTORY_CONTINUE;

	scan->found = 1;

	return MH_DIRECTORY_STOP;
}

static enum mh_error_code
picture_exists(struct mh_data_directory *directory, const char *name,
	int *present, struct mh_error *error_out)
{
	struct picture_scan scan;
	enum mh_error_code status;

	status = mh_data_directory_validate(directory, NULL);

	if (status != MH_ERROR_NONE)
		return mh_error_set(error_out, status,
			"screenshot directory is unavailable");

	scan.name = name;
	scan.found = 0;
	status = mh_data_directory_list(directory, visit_picture, &scan, NULL);

	if (status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, status, "cannot read screenshot directory");

	*present = scan.found;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_picture_store_exists(struct mh_picture_store *store, const char *name,
	int *present, struct mh_error *error_out)
{
	if (!valid_filename(name) || present == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid screenshot filename");

	if (store == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing screenshot directory");

	return picture_exists(store->directory, name, present, error_out);
}

static enum mh_error_code
check_destination(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	const char *name;
	enum mh_error_code status;
	int present;

	name = context;
	present = 0;
	status = picture_exists(directory, name, &present, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (present)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"screenshot destination appeared during capture");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_picture_store_save(struct mh_picture_store *store, const char *name,
	const struct mh_file *picture, struct mh_error *error_out)
{
	struct mh_data_file *file;
	enum mh_error_code status;
	int present;

	if (picture == NULL || picture->data == NULL || picture->size == 0 ||
		picture->size > PICTURE_MAX_FILE_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid screenshot bytes");

	present = 0;
	status = mh_picture_store_exists(store, name, &present, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (present)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"screenshot destination already exists");

	file = NULL;
	status = mh_data_directory_create(store->directory, name, picture->data,
		picture->size, check_destination, (void *)name, &file, NULL);
	mh_data_file_close(file);

	/* Diagnostics survive capture cleanup, including directory close. */
	if (status != MH_ERROR_NONE)
		return mh_error_set(error_out, status,
			status == MH_ERROR_EXISTS
				? "screenshot destination appeared during capture"
				: "cannot publish screenshot");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
