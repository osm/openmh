/*
 * Consecutive physical races share history and a caller-owned score directory.
 */
#include <stdio.h>
#include <string.h>

#include "backends/storage/filesystem_provider.h"
#include "client/diagnostics/asset_horn_catalog.h"
#include "client/diagnostics/asset_profile_catalog.h"
#include "client/diagnostics/runtime_records.h"
#include "client/diagnostics/runtime_single_setup.h"
#include "client/race/race.h"
#include "client/settings/render_choice.h"
#include "engine/core/error.h"
#include "engine/game/highscore_awards.h"
#include "engine/storage/highscore_file.h"
#include "engine/storage/record_file.h"
#include "interfaces/audio/horn_source.h"

#define RACE_TICK_LIMIT 60000UL

static struct mh_profile_catalog catalog;
static struct mh_race_context context;
static struct mh_field field;
static struct mh_highscore_history history;
static struct mh_records saved_records[2];
static char saved_names[2][128];
static unsigned char saved_tables[2][3][MH_HIGHSCORE_BYTES];

static enum mh_error_code
check_tables(const char *name, int finished, unsigned int slot,
	struct mh_error *error_out)
{
	struct mh_highscore_query query;
	struct mh_highscore_table table;
	enum mh_error_code status;
	unsigned int kind;
	int present;

	memset(&query, 0, sizeof(query));
	query.track = context.scene.assets.track.name;
	query.laps = context.scene.assets.track.laps;
	query.difficulty = context.scores.difficulty;
	query.record_class = context.scores.record_class;

	for (kind = 0; kind < 3; kind++) {
		query.kind = kind;
		status = mh_highscore_file_load_from(context.record_data,
			context.record_directory, &query, &table, &present,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!finished) {
			if (present)
				return mh_error_set(error_out, MH_ERROR_FAILED,
					"unfinished course inherited a score");

			continue;
		}

		if (!present ||
			strncmp((const char *)table.rows[0].name, name,
				MH_HIGHSCORE_NAME_SIZE) != 0 ||
			table.rows[0].lap_ms != field.cars[0].race.best_ms ||
			table.rows[0].race_ms != field.cars[0].race.now_ms ||
			strncmp((const char *)table.rows[1].name, name,
				MH_HIGHSCORE_NAME_SIZE) == 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"course awards differ from the physical finisher");

		status = mh_highscore_write(&table, saved_tables[slot][kind],
			sizeof(saved_tables[slot][kind]), error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
check_course_records(const char *root, const char *directory,
	const struct mh_horn_catalog *horns, struct mh_settings *settings,
	unsigned int track, int finish, unsigned int slot,
	struct mh_error *error_out)
{
	struct mh_single_selection selection;
	struct mh_engine_input input;
	enum mh_error_code status;
	unsigned long random, tick;

	settings->track = track - 1;
	random = 0x46e110UL;
	status = mh_single_setup_load(root, &catalog, horns, settings, 0,
		&random, &context, &field, &selection, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_drive_enable_ai(&field.cars[0],
			&context.scene.assets.ai_path, 2, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_scores_bind(&context, settings->game_type,
			settings->difficulty, 1, settings->player_name,
			&history, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_records_open(
			&context, &field, directory, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (history.completed || field.records.has_lap ||
		field.records.has_race)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"fresh course retained another course's record state");

	memset(&input, 0, sizeof(input));

	for (tick = 0; status == MH_ERROR_NONE && finish &&
		tick < RACE_TICK_LIMIT && !field.cars[0].race.finished;
		tick++) {
		status = mh_field_frame_tick_duration(&field, &input,
			tick % 2 == 0, tick % 2 == 0 ? 2 * MH_DRIVE_DT : 0,
			error_out);

		if (status == MH_ERROR_NONE && tick % 2)
			status = mh_field_frame_update(
				&field, 1.0f / 60, error_out);
	}

	if (status == MH_ERROR_NONE && finish &&
		(!field.cars[0].race.finished || !field.records.has_lap ||
			!field.records.has_race))
		status = mh_error_set(error_out, MH_ERROR_FAILED,
			"physical race did not earn both checkpoint records");

	if (status == MH_ERROR_NONE)
		status = mh_race_records_save(&context, &field, error_out);

	/* Results acceptance and outer cleanup share the completed boundary. */
	if (status == MH_ERROR_NONE)
		status = mh_race_records_save(&context, &field, error_out);

	if (status == MH_ERROR_NONE)
		status = check_tables(
			settings->player_name, finish, slot, error_out);

	if (status == MH_ERROR_NONE && finish) {
		saved_records[slot] = field.records;
		strcpy(saved_names[slot], context.scene.assets.track.name);
	}

	if (status == MH_ERROR_NONE)
		printf("Course %u %s: finished %d, lap %lu, total %lu, retained %lu\n",
			track, context.scene.assets.track.name, finish,
			field.cars[0].race.best_ms, field.cars[0].race.now_ms,
			history.race_ms);

	return status;
}

static enum mh_error_code
reload_records(
	const char *directory, unsigned int slot, struct mh_error *error_out)
{
	struct mh_records records;
	unsigned char actual[MH_RECORD_RACE_BYTES],
		expected[MH_RECORD_RACE_BYTES];
	enum mh_error_code status;

	status = mh_record_file_load_from(mh_filesystem_user_data(), directory,
		saved_names[slot], 1, &records, error_out);

	if (status == MH_ERROR_NONE && (!records.has_lap || !records.has_race))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"course checkpoint records did not survive reload");

	if (status == MH_ERROR_NONE)
		status = mh_lap_record_write(
			&records.lap, actual, MH_RECORD_LAP_BYTES, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_lap_record_write(&saved_records[slot].lap, expected,
			MH_RECORD_LAP_BYTES, error_out);

	if (status == MH_ERROR_NONE &&
		memcmp(actual, expected, MH_RECORD_LAP_BYTES) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"lap record changed across courses");

	if (status == MH_ERROR_NONE)
		status = mh_race_record_write(
			&records.race, actual, sizeof(actual), error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_record_write(&saved_records[slot].race,
			expected, sizeof(expected), error_out);

	if (status == MH_ERROR_NONE &&
		memcmp(actual, expected, sizeof(actual)) != 0)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"race record changed across courses");

	return status;
}

static enum mh_error_code
reload_tables(
	const char *directory, unsigned int slot, struct mh_error *error_out)
{
	struct mh_highscore_query query;
	struct mh_highscore_table table;
	unsigned char actual[MH_HIGHSCORE_BYTES];
	enum mh_error_code status;
	unsigned int kind;
	int present;

	memset(&query, 0, sizeof(query));
	query.track = saved_names[slot];
	query.laps = 1;
	query.difficulty = 1;
	query.record_class = 1;

	for (kind = 0; kind < 3; kind++) {
		query.kind = kind;
		status = mh_highscore_file_load_from(mh_filesystem_user_data(),
			directory, &query, &table, &present, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_highscore_write(
				&table, actual, sizeof(actual), error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (!present ||
			memcmp(actual, saved_tables[slot][kind],
				sizeof(actual)) != 0)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"highscore table changed across courses");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
main(int argc, char *argv[])
{
	struct mh_horn_catalog horns;
	struct mh_settings settings;
	struct mh_error error;
	enum mh_error_code status;
	unsigned int track, first, second;
	unsigned long random;

	if ((argc != 3 && argc != 4) ||
		(argc == 4 && strcmp(argv[3], "forward") != 0)) {
		fprintf(stderr,
			"usage: %s ASSETS EMPTY_RECORD_DIRECTORY [forward]\n",
			argv[0]);
		return 2;
	}

	/*
	 * Patched RedRock/RedRockR, or two forward courses for original assets.
	 */
	first = 1;
	second = argc == 4 ? 2 : 9;
	random = 1;
	status = mh_horn_catalog_load(argv[1], &horns, &error);

	if (status == MH_ERROR_NONE)
		status = mh_profile_catalog_load(
			argv[1], &horns, &random, &catalog, &error);

	if (status == MH_ERROR_NONE)
		status = mh_settings_init(&settings, "Course record", 0, 0,
			MH_RENDER_CHOICE_GLIDE, &error);

	if (status == MH_ERROR_NONE) {
		settings.car = 0;
		settings.laps = 1;
		settings.game_type = 0;
		settings.difficulty = 1;
		settings.automatic = 1;
		settings.catch_up = 1;
		status = check_course_records(argv[1], argv[2], &horns,
			&settings, first, 1, 0, &error);
	}

	mh_race_context_free(&context);

	if (status == MH_ERROR_NONE) {
		strcpy(settings.player_name, "Fresh course");
		status = check_course_records(argv[1], argv[2], &horns,
			&settings, second, 0, 1, &error);
		mh_race_context_free(&context);
	}

	if (status == MH_ERROR_NONE) {
		strcpy(settings.player_name, "Second record");
		status = check_course_records(argv[1], argv[2], &horns,
			&settings, second, 1, 1, &error);
		mh_race_context_free(&context);
	}

	for (track = 0; status == MH_ERROR_NONE && track < 2; track++) {
		status = reload_records(argv[2], track, &error);

		if (status == MH_ERROR_NONE)
			status = reload_tables(argv[2], track, &error);
	}

	if (status != MH_ERROR_NONE)
		fprintf(stderr, "%s\n", error.message);

	return status != MH_ERROR_NONE;
}
