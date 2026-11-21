#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_highscore_file.h"
#include "engine/storage/highscore_file.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_highscore_file_load(const char *directory,
	const struct mh_highscore_query *query,
	struct mh_highscore_table *output, int *present,
	struct mh_error *error_out)
{
	return mh_highscore_file_load_from(mh_filesystem_user_data(),
		directory != NULL && directory[0] == 0 ? NULL : directory,
		query, output, present, error_out);
}

enum mh_error_code
mh_highscore_file_save(const char *directory,
	const struct mh_highscore_query *query,
	const struct mh_highscore_table *table, struct mh_error *error_out)
{
	return mh_highscore_file_save_to(mh_filesystem_user_data(),
		directory != NULL && directory[0] == 0 ? NULL : directory,
		query, table, error_out);
}

enum mh_error_code
mh_highscore_file_awards(const char *directory,
	struct mh_highscore_awards *awards, struct mh_error *error_out)
{
	return mh_highscore_file_awards_to(mh_filesystem_user_data(),
		directory != NULL && directory[0] == 0 ? NULL : directory,
		awards, error_out);
}
