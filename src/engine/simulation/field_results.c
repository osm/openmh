#include <string.h>

#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/game/race_records.h"
#include "engine/simulation/field_internal.h"

enum mh_error_code
mh_field_result_estimates_abort(const struct mh_field *field, int aborted,
	unsigned long *random,
	struct mh_result_estimate output[MH_RACE_MAX_CARS],
	struct mh_error *error_out)
{
	struct mh_result_estimate rows[MH_RACE_MAX_CARS];
	unsigned int i, mode;
	unsigned long seed;
	const struct mh_route *route;
	const struct mh_race *race;
	float route_duration;
	enum mh_error_code status;

	if (field == NULL || output == NULL || random == NULL ||
		field->attract || (aborted != 0 && aborted != 1) ||
		field->count < 1 || field->count > MH_RACE_MAX_CARS ||
		field->standings.count != field->count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid result preparation field");

	if (aborted &&
		(field->network || field->active_mask == 0 ||
			field->active_mask >= (1U << field->count) ||
			(!field->playback &&
				field->active_mask !=
					(1U << field->count) - 1) ||
			(field->playback && field->playback != MH_GAME_GHOST &&
				field->playback != MH_GAME_DEMO)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid local abort roster");

	route_duration = 0;

	if (!aborted && !field->network) {
		route = field->cars[0].race.route;

		if (route == NULL || route->motion.keys == NULL ||
			route->motion.key_count == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing result preparation route");

		route_duration =
			(float)route->motion.keys[route->motion.key_count - 1]
				.time;
	}

	memset(rows, 0, sizeof(rows));

	for (i = 0; i < field->count; i++) {
		race = &field->cars[i].race;
		rows[i].active = (field->active_mask & (1U << i)) != 0;
		rows[i].finished = race->finished;
		rows[i].position = field->standings.cars[i].position;
		rows[i].lap = race->lap;
		rows[i].lap_limit = race->lap_limit;
		rows[i].route_parameter = (float)race->parameter;
		rows[i].total_ms = race->now_ms;
		rows[i].best_ms = race->best_ms;
	}

	seed = *random;

	if (field->network)
		mode = MH_GAME_NETWORK;
	else if (field->playback)
		mode = (unsigned int)field->playback;
	else if (field->time_attack)
		mode = MH_GAME_TIME_ATTACK;
	else
		mode = MH_GAME_QUICK_RACE;

	if (aborted)
		status = mh_result_abort_times(
			rows, field->count, 0, mode, &seed, error_out);
	else
		status = mh_result_estimate_times(rows, field->count, mode,
			route_duration, &seed, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(output, rows, sizeof(rows));
	*random = seed;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_result_estimates(const struct mh_field *field, unsigned long *random,
	struct mh_result_estimate output[MH_RACE_MAX_CARS],
	struct mh_error *error_out)
{
	return mh_field_result_estimates_abort(
		field, 0, random, output, error_out);
}

enum mh_error_code
mh_field_rank(struct mh_field *field, int grid, struct mh_error *error_out)
{
	struct mh_standing_sample samples[MH_STANDINGS_MAX_CARS];
	const struct mh_race *race;
	unsigned int i;
	enum mh_error_code status;

	if (field->cars[0].race.lap_limit == 0) {
		memset(&field->standings, 0, sizeof(field->standings));
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	for (i = 0; i < field->count; i++) {
		race = &field->cars[i].race;

		/*
		 * Sampled frames rank actual progress even with GO=0. Explicit
		 * fixed-tick diagnostics retain their grid-order policy.
		 */
		if (grid && !field->cars[0].frame.active)
			samples[i].score =
				MH_FIELD_GRID_SLOTS - field->cars[i].slot;
		else {
			status = mh_race_score(
				race, &samples[i].score, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		samples[i].finished = race->finished;
		samples[i].total_ms = race->now_ms;
		samples[i].best_ms = race->best_ms;
		samples[i].best_lap = race->best_lap;

		if (!(field->active_mask & (1U << i)) && !race->finished)
			samples[i].score = 0;
	}

	if (field->network || field->playback)
		return mh_standings_rank_active(samples, field->count,
			field->active_mask, &field->standings, error_out);

	return mh_standings_rank(
		samples, field->count, &field->standings, error_out);
}

enum mh_error_code
mh_field_classify(struct mh_field *field, struct mh_error *error_out)
{
	struct mh_field next;
	enum mh_error_code status;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || field->active_mask == 0 ||
		field->active_mask >= (1U << field->count))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid field classification");

	next = *field;
	status = mh_field_rank(
		&next, !mh_drive_racing(&next.cars[0]), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*field = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_bind_records(struct mh_field *field, const struct mh_records *records,
	unsigned int eligible_mask, struct mh_error *error_out)
{
	unsigned char data[MH_RECORD_RACE_BYTES];
	unsigned int i;
	enum mh_error_code status;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || records == NULL ||
		(eligible_mask & ~field->active_mask) != 0 ||
		eligible_mask >= (1U << field->count) ||
		(records->has_lap != 0 && records->has_lap != 1) ||
		(records->has_race != 0 && records->has_race != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid field record binding");

	for (i = 0; i < field->count; i++)
		if (field->cars[i].ticks != 0 ||
			field->cars[i].race.lap_limit == 0 ||
			field->cars[i].race.now_ms != 0 ||
			field->cars[i].race.finished ||
			(field->cars[i].remote && (eligible_mask & (1U << i))))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"record binding requires fresh timed local entrants");

	status = mh_lap_record_write(
		&records->lap, data, MH_RECORD_LAP_BYTES, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_race_record_write(
			&records->race, data, MH_RECORD_RACE_BYTES, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	field->records = *records;
	field->records_bound = 1;
	field->record_mask = eligible_mask;

	for (i = 0; i < field->count; i++)
		field->cars[i].race.record_eligible = (eligible_mask >> i) & 1U;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_records_update(struct mh_field *field, struct mh_error *error_out)
{
	struct mh_record_notices notices;
	struct mh_race *race;
	unsigned int i;
	enum mh_error_code status;

	if (!field->records_bound)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (i = 0; i < field->count; i++) {
		race = &field->cars[i].race;

		if (!(field->active_mask & (1U << i)) ||
			field->cars[i].remote || field->cars[i].race_frozen ||
			!race->lap_event)
			continue;

		status = mh_race_records_accept(&field->records, race,
			race->record_eligible, &field->records, &notices,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		race->lap_notice = notices.lap;
		race->race_notice = notices.race;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_frame_results(struct mh_field *field, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_field_records_update(field, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_field_rank(
		field, !mh_drive_racing(&field->cars[0]), error_out);
}
