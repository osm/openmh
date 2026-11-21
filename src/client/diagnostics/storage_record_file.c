#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_record_file.h"
#include "engine/storage/record_file.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_record_file_load(const char *directory, const char *track_name,
	unsigned int laps, struct mh_records *output,
	struct mh_error *error_out)
{
	return mh_record_file_load_from(mh_filesystem_user_data(),
		directory != NULL && directory[0] == 0 ? NULL : directory,
		track_name, laps, output, error_out);
}

enum mh_error_code
mh_record_file_save(const char *directory, const char *track_name,
	unsigned int laps, const struct mh_records *records,
	struct mh_error *error_out)
{
	return mh_record_file_save_to(mh_filesystem_user_data(),
		directory != NULL && directory[0] == 0 ? NULL : directory,
		track_name, laps, records, error_out);
}
