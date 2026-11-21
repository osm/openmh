#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_track_flag_file.h"
#include "engine/storage/track_flag_file.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_track_flag_file_load(const char *path, const char *track_name,
	int *available, struct mh_error *error_out)
{
	return mh_track_flag_file_load_from(mh_filesystem_user_data(), path,
		track_name, available, error_out);
}

enum mh_error_code
mh_track_flag_file_save(const char *asset_root, const char *directory,
	const char *track_name, struct mh_error *error_out)
{
	return mh_track_flag_file_save_to(mh_filesystem_user_data(), asset_root,
		directory != NULL && directory[0] == 0 ? NULL : directory,
		track_name, error_out);
}
