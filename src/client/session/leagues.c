#include <stdlib.h>
#include <string.h>

#include "client/session/leagues.h"
#include "engine/core/error.h"
#include "interfaces/storage/data_directory.h"
#include "interfaces/storage/user_data.h"

struct mh_leagues {
	struct mh_league_catalog	catalog;
	struct mh_league_store		*store;
	char stored_filenames[MH_LEAGUE_CATALOG_MAX_ENTRIES]
			[MH_LEAGUE_CATALOG_FILENAME_SIZE];
};

void
mh_leagues_close(struct mh_leagues *leagues)
{
	if (leagues == NULL)
		return;

	mh_league_catalog_close(&leagues->catalog);
	mh_league_store_close(leagues->store);
	free(leagues);
}

static enum mh_error_code
validate_roster(
	const struct mh_league_document *document, struct mh_error *error_out)
{
	struct mh_league_roster roster;
	unsigned int i;

	for (i = 0; i < MH_LEAGUE_ENTRIES; i++)
		if (document->league.entries[i].driver >= MH_LEAGUE_ENTRIES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid League profile identity");

	return mh_league_roster(&document->league, &roster, error_out);
}

enum mh_error_code
mh_leagues_open_from(const struct mh_user_data *data, const char *asset_root,
	const char *directory, const struct mh_league_creation *creation,
	const char *selected_name, unsigned long *random,
	struct mh_leagues **output, struct mh_error *error_out)
{
	struct mh_leagues *leagues;
	struct mh_data_directory *held;
	enum mh_error_code status;
	unsigned long seed;
	unsigned int i;

	if (creation == NULL || random == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League frontend input");

	leagues = (struct mh_leagues *)calloc(1, sizeof(*leagues));

	if (leagues == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory opening League frontend");

	seed = *random;
	status = MH_ERROR_NONE;

	if (directory != NULL) {
		status = mh_user_data_open_directory(
			data, directory, asset_root, &held, error_out);

		if (status == MH_ERROR_NONE) {
			status = mh_league_store_open_in(held,
				creation->profiles, creation->count,
				&leagues->catalog, &leagues->store, error_out);

			if (status != MH_ERROR_NONE)
				mh_data_directory_close(held);
		}
	}

	for (i = 0; status == MH_ERROR_NONE && i < leagues->catalog.count;
		i++) {
		status = validate_roster(
			&leagues->catalog.entries[i].document, error_out);
		strcpy(leagues->stored_filenames[i],
			leagues->catalog.entries[i].filename);
	}

	if (status == MH_ERROR_NONE && leagues->catalog.count == 0)
		status = mh_league_catalog_create(&leagues->catalog, creation,
			"Default", &seed, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_league_catalog_restore(
			&leagues->catalog, selected_name, error_out);

	if (status != MH_ERROR_NONE) {
		mh_leagues_close(leagues);
		return status;
	}

	*output = leagues;
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_leagues_reload_from(const struct mh_user_data *data, const char *asset_root,
	const char *directory, const struct mh_league_creation *creation,
	const char *selected_name, unsigned long *random,
	struct mh_leagues **leagues, struct mh_error *error_out)
{
	struct mh_leagues *fresh;
	enum mh_error_code status;

	if (leagues == NULL || *leagues == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing League frontend to reload");

	fresh = NULL;
	status = mh_leagues_open_from(data, asset_root, directory, creation,
		selected_name, random, &fresh, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_leagues_close(*leagues);
	*leagues = fresh;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_league_catalog *
mh_leagues_catalog(const struct mh_leagues *leagues)
{
	return leagues == NULL ? NULL : &leagues->catalog;
}

enum mh_error_code
mh_leagues_select(struct mh_leagues *leagues, unsigned int index,
	struct mh_error *error_out)
{
	if (leagues == NULL || index >= leagues->catalog.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League selection");

	leagues->catalog.selected = index;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_leagues_create(struct mh_leagues *leagues,
	const struct mh_league_creation *creation, unsigned long *random,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (leagues == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing League frontend");

	status = mh_league_catalog_create(
		&leagues->catalog, creation, NULL, random, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memmove(leagues->stored_filenames + 1, leagues->stored_filenames,
		(leagues->catalog.count - 1) *
			sizeof(leagues->stored_filenames[0]));
	memset(leagues->stored_filenames[0], 0,
		sizeof(leagues->stored_filenames[0]));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_leagues_set_name(struct mh_leagues *leagues, const unsigned char *name,
	struct mh_error *error_out)
{
	if (leagues == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing League frontend");

	return mh_league_catalog_assign_name(
		&leagues->catalog, name, error_out);
}

enum mh_error_code
mh_leagues_remove(struct mh_leagues *leagues, unsigned int index,
	const struct mh_league_creation *creation, unsigned long *random,
	struct mh_error *error_out)
{
	struct mh_league_catalog next;
	unsigned long seed;
	unsigned int count;
	enum mh_error_code status;

	if (leagues == NULL || index >= leagues->catalog.count ||
		random == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid League deletion");

	seed = *random;
	count = leagues->catalog.count;
	memset(&next, 0, sizeof(next));
	status = mh_league_catalog_copy(&leagues->catalog, &next, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_league_catalog_remove(
			&next, index, creation, &seed, error_out);

	if (status == MH_ERROR_NONE && leagues->store != NULL &&
		leagues->stored_filenames[index][0] != 0)
		status = mh_league_store_remove(leagues->store,
			leagues->stored_filenames[index], error_out);

	if (status != MH_ERROR_NONE) {
		mh_league_catalog_close(&next);
		return status;
	}

	mh_league_catalog_close(&leagues->catalog);
	leagues->catalog = next;

	if (count == 1)
		memset(leagues->stored_filenames[0], 0,
			sizeof(leagues->stored_filenames[0]));
	else if (index + 1 < count)
		memmove(leagues->stored_filenames + index,
			leagues->stored_filenames + index + 1,
			(count - index - 1) *
				sizeof(leagues->stored_filenames[0]));

	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
save_entry(struct mh_leagues *leagues, unsigned int index,
	const struct mh_league_document *document, struct mh_error *error_out)
{
	enum mh_error_code status;
	const char *name;

	name = leagues->stored_filenames[index][0] == 0
		? leagues->catalog.entries[index].filename
		: leagues->stored_filenames[index];
	status = leagues->store == NULL
		? mh_error_set(error_out, MH_ERROR_NONE, NULL)
		: mh_league_store_save(
			leagues->store, name, document, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (leagues->stored_filenames[index][0] == 0)
		strcpy(leagues->stored_filenames[index], name);

	leagues->catalog.entries[index].document = *document;
	leagues->catalog.entries[index].dirty = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_leagues_save(struct mh_leagues *leagues, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	if (leagues == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing League frontend");

	for (i = 0; i < leagues->catalog.count; i++)
		if (leagues->catalog.entries[i].dirty) {
			status = save_entry(leagues, i,
				&leagues->catalog.entries[i].document,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_leagues_finish_race(struct mh_leagues *leagues, unsigned int index,
	struct mh_league_race *race, const struct mh_standings *standings,
	unsigned long setting, struct mh_error *error_out)
{
	struct mh_league_document document;
	struct mh_league_race transaction;
	enum mh_error_code status;

	if (leagues == NULL || index >= leagues->catalog.count || race == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League result destination");

	document = leagues->catalog.entries[index].document;
	transaction = *race;
	status = mh_league_race_finish(
		&transaction, standings, setting, &document.league, error_out);

	if (status == MH_ERROR_NONE)
		status = save_entry(leagues, index, &document, error_out);

	if (status == MH_ERROR_NONE)
		*race = transaction;

	return status;
}

enum mh_error_code
mh_leagues_rollover(struct mh_leagues *leagues, unsigned int index,
	const unsigned long *divisions, unsigned int track_count,
	struct mh_error *error_out)
{
	struct mh_league_document document;
	struct mh_league_schedule schedule;
	enum mh_error_code status;

	if (leagues == NULL || index >= leagues->catalog.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid League rollover destination");

	document = leagues->catalog.entries[index].document;
	status = mh_league_schedule(
		&document.league, divisions, track_count, &schedule, error_out);

	if (status == MH_ERROR_NONE &&
		(!schedule.complete || schedule.count == 0 ||
			schedule.outcome == MH_LEAGUE_CHAMPION))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"League does not have an ordinary completed season");

	if (status == MH_ERROR_NONE)
		status = mh_league_rollover(&document.league, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_league_schedule(&document.league, divisions,
			track_count, &schedule, error_out);

	if (status == MH_ERROR_NONE && schedule.count == 0)
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"next League division has no tracks");

	if (status != MH_ERROR_NONE)
		return status;

	return save_entry(leagues, index, &document, error_out);
}
