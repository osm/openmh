#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/game/highscore_awards.h"
#include "engine/game/highscore_store.h"
#include "engine/game/mode.h"

enum mh_error_code
mh_highscore_history_frame(struct mh_highscore_history *history,
	unsigned long finished, unsigned long race_ms,
	struct mh_error *error_out)
{
	if (history == NULL || finished > MH_U32_MAX || race_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score finish observation");

	if (finished != 0) {
		history->completed = 1;
		history->race_ms = race_ms;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
copy_text(char *output, size_t capacity, const char *text, size_t size,
	struct mh_error *error_out)
{
	size_t length;

	if (text == NULL || size == 0 || size > MH_HIGHSCORE_TEXT_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score award text");

	length = 0;

	while (length < size && text[length] != 0)
		length++;

	if (length == size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unterminated high-score award text");

	if (length >= capacity)
		length = capacity - 1;

	memcpy(output, text, length);
	output[length] = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
make_query(const struct mh_highscore_awards *awards, unsigned int index,
	struct mh_highscore_query *query)
{
	memset(query, 0, sizeof(*query));

	query->kind = awards->kinds[index];
	query->laps = awards->laps;
	query->difficulty = awards->difficulty;
	query->record_class = awards->record_class;
	query->track = awards->track;
}

enum mh_error_code
mh_highscore_awards_begin(const struct mh_highscore_result *result,
	struct mh_highscore_awards *awards, struct mh_error *error_out)
{
	struct mh_highscore_awards next;
	struct mh_highscore_query query;
	char filename[MH_HIGHSCORE_FILENAME_SIZE];
	enum mh_error_code status;
	unsigned int i;

	if (result == NULL || awards == NULL ||
		result->completed > MH_U32_MAX || result->mode > MH_U32_MAX ||
		result->candidate.lap_ms > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score result");

	memset(&next, 0, sizeof(next));
	next.initialized = 1;

	if (result->completed == 1) {
		if (result->mode == MH_GAME_QUICK_RACE)
			next.kinds[next.count++] = MH_HIGHSCORE_SINGLE;

		next.kinds[next.count++] = MH_HIGHSCORE_RACE;
	}

	if (result->candidate.lap_ms != 0)
		next.kinds[next.count++] = MH_HIGHSCORE_LAP;

	if (next.count == 0) {
		*awards = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (result->candidate.race_ms > MH_U32_MAX ||
		result->candidate.transmission > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score award values");

	next.laps = result->query.laps;
	next.difficulty = result->query.difficulty;
	next.record_class = result->query.record_class;
	next.lap_ms = result->candidate.lap_ms;
	next.race_ms = result->candidate.race_ms;
	next.transmission = result->candidate.transmission;
	status = copy_text(next.track, sizeof(next.track), result->query.track,
		MH_HIGHSCORE_TEXT_LIMIT, error_out);

	if (status == MH_ERROR_NONE)
		status = copy_text(next.name, sizeof(next.name),
			result->candidate.name, result->candidate.name_size,
			error_out);

	if (status == MH_ERROR_NONE)
		status = copy_text(next.car, sizeof(next.car),
			result->candidate.car, result->candidate.car_size,
			error_out);

	for (i = 0; status == MH_ERROR_NONE && i < next.count; i++) {
		make_query(&next, i, &query);
		status = mh_highscore_filename(
			&query, filename, sizeof(filename), error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	*awards = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_highscore_awards_save(struct mh_highscore_awards *awards,
	const struct mh_highscore_store *store, struct mh_error *error_out)
{
	struct mh_highscore_query query;
	struct mh_highscore_candidate candidate;
	struct mh_highscore_table table;
	enum mh_error_code status;
	unsigned int rank;
	int lap_column;

	if (awards == NULL || !awards->initialized ||
		awards->count > MH_HIGHSCORE_AWARD_LIMIT ||
		awards->next_kind > awards->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid high-score award boundary");

	if (awards->next_kind == awards->count)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (store == NULL || store->load == NULL || store->save == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing high-score storage");

	memset(&candidate, 0, sizeof(candidate));
	candidate.name = awards->name;
	candidate.name_size = sizeof(awards->name);
	candidate.car = awards->car;
	candidate.car_size = sizeof(awards->car);
	candidate.transmission = awards->transmission;
	candidate.lap_ms = awards->lap_ms;
	candidate.race_ms = awards->race_ms;

	while (awards->next_kind < awards->count) {
		make_query(awards, awards->next_kind, &query);

		if (!awards->pending) {
			status = store->load(
				store->context, &query, &table, error_out);
			lap_column = query.kind == MH_HIGHSCORE_LAP;

			if (status == MH_ERROR_NONE)
				status = mh_highscore_rank(&table, lap_column,
					lap_column ? candidate.lap_ms
						: candidate.race_ms,
					&rank, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_highscore_insert(
					&table, rank, &candidate, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			awards->ranks[awards->next_kind] = rank;

			if (rank == 0) {
				awards->next_kind++;
				continue;
			}

			awards->table = table;
			awards->pending = 1;
		}

		status = store->save(
			store->context, &query, &awards->table, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		awards->pending = 0;
		awards->next_kind++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
