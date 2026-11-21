#include <string.h>

#include "engine/core/text.h"
#include "engine/storage/control_internal.h"

struct target_scan {
	const struct control_file	*file;
	int				found, ambiguous;
};

static enum mh_directory_action
inspect_name(void *context, const char *name)
{
	struct target_scan *scan;

	scan = context;

	if (mh_ascii_compare(scan->file->filename, name) != 0)
		return MH_DIRECTORY_CONTINUE;

	if (scan->found || strcmp(scan->file->filename, name) != 0) {
		scan->ambiguous = 1;
		return MH_DIRECTORY_STOP;
	}

	scan->found = 1;

	return MH_DIRECTORY_CONTINUE;
}

static enum mh_error_code
validate_target(void *context, struct mh_data_directory *directory,
	struct mh_error *error_out)
{
	struct control_file *file;
	struct target_scan scan;
	enum mh_error_code status;

	file = context;
	memset(&scan, 0, sizeof(scan));
	scan.file = file;
	status = mh_data_directory_list(
		directory, inspect_name, &scan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (scan.ambiguous)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"ambiguous control profile filename");

	if (file->identity != NULL) {
		if (!scan.found)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"control profile changed; reload it before modifying");

		return mh_data_file_validate(file->identity, error_out);
	}

	if (scan.found)
		return mh_error_set(error_out, MH_ERROR_EXISTS,
			"control profile filename is already in use");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
save_file(struct mh_control_store *store, struct control_file *file,
	const struct mh_control_layout *layout, struct mh_error *error_out)
{
	char text[MH_CONTROL_TEXT_CAPACITY];
	size_t size;
	enum mh_error_code status;

	status = mh_control_layout_encode(
		layout, NULL, text, sizeof(text), &size, error_out);

	if (status == MH_ERROR_NONE)
		status =
			mh_data_directory_validate(store->directory, error_out);

	if (status == MH_ERROR_NONE)
		status = validate_target(file, store->directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (file->identity != NULL)
		status = mh_data_file_replace(file->identity, text, size,
			validate_target, file, error_out);
	else
		status = mh_data_directory_create(store->directory,
			file->filename, text, size, validate_target, file,
			&file->identity, error_out);

	if (status == MH_ERROR_NONE)
		file->from_user = 1;

	return status;
}

enum mh_error_code
mh_control_store_save(struct mh_control_store *store,
	struct mh_control_catalog *catalog, struct mh_error *error_out)
{
	unsigned int i;
	size_t size;
	enum mh_error_code status;

	if (store == NULL || catalog == NULL ||
		catalog->count != store->count || catalog->profiles == NULL ||
		catalog->selected >= catalog->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid control profile save catalog");

	if (store->directory == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_data_directory_validate(store->directory, error_out);

	for (i = 0; status == MH_ERROR_NONE && i < catalog->count; i++)
		if (catalog->profiles[i].dirty == 1) {
			status = mh_control_layout_encode(
				&catalog->profiles[i].layout, NULL, NULL, 0,
				&size, error_out);

			if (status == MH_ERROR_NONE &&
				size >= MH_CONTROL_TEXT_CAPACITY)
				status = mh_error_set(error_out, MH_ERROR_RANGE,
					"encoded control profile exceeds native bound");

			if (status == MH_ERROR_NONE)
				status = validate_target(&store->files[i],
					store->directory, error_out);
		}

	for (i = 0; status == MH_ERROR_NONE && i < catalog->count; i++)
		if (catalog->profiles[i].dirty == 1) {
			status = save_file(store, &store->files[i],
				&catalog->profiles[i].layout, error_out);

			if (status == MH_ERROR_NONE)
				catalog->profiles[i].dirty = 0;
		}

	return status;
}
