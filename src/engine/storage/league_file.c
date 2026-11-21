#include <stdlib.h>

#include "engine/storage/league_codec.h"
#include "engine/storage/league_file.h"
#include "interfaces/storage/user_data.h"

static const char *
basename_key(const char *path)
{
	const char *key;

	if (path == NULL || *path == 0)
		return NULL;

	key = path;

	while (*path != 0) {
		if (*path == '/' || *path == '\\' || *path == ':')
			key = path + 1;

		path++;
	}

	return *key != 0 ? key : NULL;
}

enum mh_error_code
mh_league_file_load_from(const struct mh_user_data *data, const char *path,
	const struct mh_profile *profiles, unsigned int profile_count,
	struct mh_league_document *output, struct mh_error *error_out)
{
	struct mh_file file;
	const char *key;
	enum mh_error_code status;

	key = basename_key(path);

	if (key == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league file path/output");

	status = mh_user_data_read(
		data, path, MH_LEAGUE_FILE_LIMIT, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_league_bytes_decode(
		&file, key, profiles, profile_count, output, error_out);
	free(file.data);

	return status;
}

enum mh_error_code
mh_league_file_save_to(const struct mh_user_data *data, const char *path,
	const struct mh_league_document *document, struct mh_error *error_out)
{
	const char *key;
	struct mh_file file;
	enum mh_error_code status;

	key = basename_key(path);

	if (key == NULL || document == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid league file path/document");

	status = mh_league_bytes_encode(document, key, &file, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_user_data_replace(
		data, path, file.data, file.size, error_out);
	free(file.data);

	return status;
}

enum mh_error_code
mh_league_file_finish_to(const struct mh_user_data *data, const char *path,
	struct mh_league_race *race, const struct mh_standings *standings,
	unsigned long setting, struct mh_league_document *document,
	struct mh_error *error_out)
{
	struct mh_league_document next;
	struct mh_league_race transaction;
	enum mh_error_code status;

	if (race == NULL || document == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing league result transaction");

	next = *document;
	transaction = *race;
	status = mh_league_race_finish(
		&transaction, standings, setting, &next.league, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_league_file_save_to(data, path, &next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*document = next;
	*race = transaction;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_league_file_rollover_to(const struct mh_user_data *data, const char *path,
	const unsigned long *divisions, unsigned int track_count,
	struct mh_league_document *document, struct mh_error *error_out)
{
	struct mh_league_document next;
	struct mh_league_schedule schedule;
	enum mh_error_code status;

	if (document == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing league rollover document");

	status = mh_league_schedule(&document->league, divisions, track_count,
		&schedule, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!schedule.complete || schedule.count == 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"league season is not complete");

	if (schedule.outcome == MH_LEAGUE_CHAMPION)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"championship victory requires its completion flow");

	next = *document;
	status = mh_league_rollover(&next.league, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_league_schedule(&next.league, divisions,
			track_count, &schedule, error_out);

	if (status == MH_ERROR_NONE && schedule.count == 0)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"next league division has no tracks");

	if (status == MH_ERROR_NONE)
		status = mh_league_file_save_to(data, path, &next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*document = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
