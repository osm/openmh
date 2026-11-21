#include "client/diagnostics/runtime_records.h"
#include "client/hosts/storage.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_race_records_open(struct mh_race_context *context, struct mh_field *field,
	const char *directory, struct mh_error *error_out)
{
	const struct mh_user_data *data;

	if (directory != NULL && directory[0] == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid record path or lap count");

	data = mh_host_user_data();

	if (data == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"native record storage is unavailable");

	return mh_race_records_open_from(
		data, context, field, directory, error_out);
}
