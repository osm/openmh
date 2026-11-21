#include <stdlib.h>
#include <string.h>

#include "engine/assets/tbl.h"
#include "engine/assets/track_flag.h"
#include "engine/storage/track_flag_file.h"
#include "interfaces/storage/resource.h"
#include "interfaces/storage/user_data.h"

#define MARKER_FILE_LIMIT (16UL * 1024 * 1024)

static enum mh_error_code
validate_load(const char *path, const char *track_name, int *available,
	struct mh_error *error_out)
{
	int ignored;

	if (path == NULL || path[0] == 0 || available == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid track marker path/output");

	return mh_track_flag_validate(NULL, 0, track_name, &ignored, error_out);
}

static enum mh_error_code
decode_marker(const struct mh_file *file, const char *track_name,
	int *available, struct mh_error *error_out)
{
	unsigned char *decoded;
	size_t size;
	enum mh_error_code status;

	if (file->size < 3 || memcmp(file->data, "TBL", 3) != 0)
		return mh_track_flag_validate(file->data, file->size,
			track_name, available, error_out);

	status = mh_tbl_resource_decode(
		file->data, file->size, NULL, &decoded, &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_track_flag_validate(
		decoded, size, track_name, available, error_out);
	free(decoded);

	return status;
}

static enum mh_error_code
finish_load(enum mh_error_code status, struct mh_file *file,
	const char *track_name, int *available, struct mh_error *error_out)
{
	if (status == MH_ERROR_NOT_FOUND) {
		*available = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status != MH_ERROR_NONE)
		return status;

	status = decode_marker(file, track_name, available, error_out);
	free(file->data);

	return status;
}

enum mh_error_code
mh_track_flag_file_load_from(const struct mh_user_data *data, const char *path,
	const char *track_name, int *available, struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = validate_load(path, track_name, available, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_user_data_read(
		data, path, MARKER_FILE_LIMIT, &file, error_out);

	return finish_load(status, &file, track_name, available, error_out);
}

enum mh_error_code
mh_track_flag_resource_load_from(const struct mh_resource_source *source,
	const char *path, const char *track_name, int *available,
	struct mh_error *error_out)
{
	struct mh_file file;
	enum mh_error_code status;

	status = validate_load(path, track_name, available, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_resource_source_read_bounded(
		source, path, MARKER_FILE_LIMIT, &file, error_out);

	return finish_load(status, &file, track_name, available, error_out);
}
