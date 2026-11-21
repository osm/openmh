#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/storage_league_file.h"
#include "engine/storage/league_file.h"
#include "interfaces/storage/user_data_backend.h"

enum mh_error_code
mh_league_file_load(const char *path, const struct mh_profile *ai,
	unsigned int ai_count, struct mh_league_document *output,
	struct mh_error *error_out)
{
	return mh_league_file_load_from(mh_filesystem_user_data(), path, ai,
		ai_count, output, error_out);
}

enum mh_error_code
mh_league_file_save(const char *path, const struct mh_league_document *doc,
	struct mh_error *error_out)
{
	return mh_league_file_save_to(
		mh_filesystem_user_data(), path, doc, error_out);
}

enum mh_error_code
mh_league_file_finish(const char *path, struct mh_league_race *race,
	const struct mh_standings *standings, unsigned long setting,
	struct mh_league_document *doc, struct mh_error *error_out)
{
	return mh_league_file_finish_to(mh_filesystem_user_data(), path, race,
		standings, setting, doc, error_out);
}

enum mh_error_code
mh_league_file_rollover(const char *path, const unsigned long *divisions,
	unsigned int track_count, struct mh_league_document *doc,
	struct mh_error *error_out)
{
	return mh_league_file_rollover_to(mh_filesystem_user_data(), path,
		divisions, track_count, doc, error_out);
}
