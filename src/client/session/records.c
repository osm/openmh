#include <stdlib.h>
#include <string.h>

#include "client/session/race_context.h"
#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/simulation/field.h"
#include "engine/storage/highscore_file.h"
#include "engine/storage/record_file.h"
#include "interfaces/storage/user_data.h"

#define SCORE_INPUT_NAME_SIZE 256

enum mh_error_code
mh_race_records_open_from(const struct mh_user_data *data,
	struct mh_race_context *context, struct mh_field *field,
	const char *directory, struct mh_error *error_out)
{
	struct mh_records records;
	char *user_directory, *record_directory;
	enum mh_error_code status;
	size_t length;

	if (data == NULL || context == NULL || field == NULL ||
		context->record_directory != NULL ||
		context->record_data != NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid record storage binding");

	user_directory = NULL;

	if (directory == NULL) {
		status = mh_user_data_directory(
			data, &user_directory, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		directory = user_directory;
	}

	length = strlen(directory);
	record_directory = (char *)malloc(length + 1);

	if (record_directory != NULL)
		memcpy(record_directory, directory, length + 1);

	free(user_directory);

	if (record_directory == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"out of memory binding records");

	status = mh_record_file_load_from(data, record_directory,
		context->scene.assets.track.name,
		context->scene.assets.track.laps, &records, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_bind_records(field, &records, 1, error_out);

	if (status != MH_ERROR_NONE) {
		free(record_directory);
		return status;
	}

	context->record_data = data;
	context->record_directory = record_directory;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scores_bind(struct mh_race_context *context, unsigned int mode,
	unsigned int difficulty, unsigned int record_class,
	const char *player_name, struct mh_highscore_history *history,
	struct mh_error *error_out)
{
	struct mh_race_scores next;
	size_t length;

	if (context == NULL || mode > MH_GAME_DEMO || difficulty > 2 ||
		record_class > 1 || player_name == NULL ||
		context->scores.bound)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score race binding");

	length = 0;

	while (length < SCORE_INPUT_NAME_SIZE && player_name[length] != 0)
		length++;

	if (length == SCORE_INPUT_NAME_SIZE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated high-score player name");

	memset(&next, 0, sizeof(next));
	next.bound = 1;
	next.mode = mode;
	next.difficulty = difficulty;
	next.record_class = record_class;

	if (length >= sizeof(next.player_name))
		length = sizeof(next.player_name) - 1;

	memcpy(next.player_name, player_name, length);
	next.history = history;

	if (history != NULL)
		history->completed = 0;

	context->scores = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_race_scores_frame(struct mh_race_context *context,
	const struct mh_field *field, struct mh_error *error_out)
{
	struct mh_highscore_history *history;

	if (context == NULL || field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing high-score race");

	if (!context->scores.bound)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (field->count == 0 || field->count > MH_RACE_MAX_CARS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score entrant");

	history = context->scores.history == NULL
		? &context->scores.local_history
		: context->scores.history;

	return mh_highscore_history_frame(history,
		(unsigned long)field->cars[0].race.finished,
		field->cars[0].race.now_ms, error_out);
}

void
mh_race_records_restarted(struct mh_race_context *context)
{
	if (context != NULL)
		memset(&context->scores.awards, 0,
			sizeof(context->scores.awards));
}

static enum mh_error_code
prepare_awards(struct mh_race_context *context, const struct mh_field *field,
	struct mh_error *error_out)
{
	struct mh_highscore_result result;
	const struct mh_highscore_history *history;

	if (field->cars[0].car == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing high-score car");

	history = context->scores.history == NULL
		? &context->scores.local_history
		: context->scores.history;
	memset(&result, 0, sizeof(result));
	result.completed = history->completed;
	result.mode = context->scores.mode;
	result.query.laps = context->scene.assets.track.laps;
	result.query.difficulty = context->scores.difficulty;
	result.query.record_class = context->scores.record_class;
	result.query.track = context->scene.assets.track.name;
	result.candidate.name = context->scores.player_name;
	result.candidate.name_size = sizeof(context->scores.player_name);
	result.candidate.car = field->cars[0].car->name;
	result.candidate.car_size = sizeof(field->cars[0].car->name);
	result.candidate.transmission = !field->cars[0].config.options.manual;
	result.candidate.lap_ms = field->cars[0].race.best_ms;
	result.candidate.race_ms = history->race_ms;

	return mh_highscore_awards_begin(
		&result, &context->scores.awards, error_out);
}

enum mh_error_code
mh_race_records_save(struct mh_race_context *context,
	const struct mh_field *field, struct mh_error *error_out)
{
	enum mh_error_code status;

	if (context == NULL || field == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing record session");

	status = mh_race_scores_frame(context, field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (context->record_directory == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (context->scores.bound &&
		(context->scores.mode == MH_GAME_ATTRACT ||
			context->scores.mode == MH_GAME_DEMO))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!field->records_bound)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"record storage has no bound book");

	status = mh_record_file_save_to(context->record_data,
		context->record_directory, context->scene.assets.track.name,
		context->scene.assets.track.laps, &field->records, error_out);

	if (status != MH_ERROR_NONE || !context->scores.bound)
		return status;

	if (!context->scores.awards.initialized)
		status = prepare_awards(context, field, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_highscore_file_awards_to(context->record_data,
			context->record_directory, &context->scores.awards,
			error_out);

	return status;
}
