#include <string.h>

#include "engine/core/error.h"
#include "engine/game/mode.h"
#include "engine/game/race_limits.h"
#include "engine/simulation/field.h"

#define CATCHUP_STRENGTH_LIMIT 100
#define DIAGNOSTIC_HZ 120
/* floor(original u32 millisecond limit * 120 / 1000), without overflow. */
#define DIAGNOSTIC_TICK_LIMIT 515396075UL
#define COUNTDOWN_MS 12000
#define CHECKPOINT_TWO_COLUMN 2

enum mh_error_code
mh_field_enable_catchup(struct mh_field *field, unsigned int difficulty,
	struct mh_error *error_out)
{
	const struct mh_track *track;
	unsigned int i;

	if (field == NULL || field->count < 1 ||
		field->count > MH_TRAFFIC_MAX_CARS || field->network ||
		field->playback || field->time_attack || field->attract ||
		field->active_mask != (1U << field->count) - 1 ||
		field->path == NULL || field->path->points == NULL ||
		field->path->count < 1 ||
		field->path->count > MH_AI_MAX_POINTS || difficulty < 1 ||
		difficulty > MH_TRACK_DIFFICULTIES ||
		field->tuning.ticks != 0 || field->tuning.initialized)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"catch-up requires a fresh local race and AI path");

	track = field->cars[0].track;

	if (track == NULL || !track->has_ai[difficulty - 1] ||
		!(track->ai[difficulty - 1][MH_TRACK_AI_CATCHUP] >= 0 &&
			track->ai[difficulty - 1][MH_TRACK_AI_CATCHUP] <=
				CATCHUP_STRENGTH_LIMIT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing track catch-up strength");

	for (i = 0; i < field->count; i++)
		if (field->cars[i].remote || field->cars[i].ticks != 0 ||
			field->cars[i].start_ticks != 0 ||
			field->cars[i].track != track ||
			field->cars[i].race.lap_limit < MH_RACE_MIN_LAPS)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"catch-up requires fresh timed local entrants");

	field->tuning.catchup = 1;
	field->tuning.strength =
		(float)track->ai[difficulty - 1][MH_TRACK_AI_CATCHUP];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
load_engine_tuning(const struct mh_engine_config *base,
	const struct mh_engine_config *config,
	struct mh_race_tuning_engine *engine)
{
	unsigned int gear;

	memset(engine, 0, sizeof(*engine));
	engine->gears = config->gears;

	for (gear = 0; gear <= MH_CAR_MAX_GEARS; gear++) {
		engine->base_ratio[gear] = (float)base->ratio[gear];
		engine->ratio[gear] = (float)config->ratio[gear];
	}

	engine->base_acceleration = (float)base->acceleration;
	engine->acceleration = (float)config->acceleration;
}

static void
store_engine_tuning(const struct mh_race_tuning_engine *engine,
	struct mh_engine_config *config)
{
	unsigned int gear;

	/* Unchanged reverse/unused channels keep native setup precision. */
	for (gear = 1; gear <= engine->gears; gear++)
		config->ratio[gear] = engine->ratio[gear];

	config->acceleration = engine->acceleration;
}

static enum mh_error_code
tune_car(const struct mh_field *field, struct mh_field_tuning *tuning,
	struct mh_engine_config *config, const struct mh_race_tuning_plan *plan,
	unsigned int active_count, unsigned int entry,
	struct mh_error *error_out)
{
	struct mh_race_tuning_engine engine;
	const struct mh_race *race;
	unsigned int rank;
	enum mh_error_code status;

	rank = field->standings.cars[entry].position;

	if (rank < 1 || rank > active_count ||
		config->gears != tuning->base[entry].gears)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid performance rank or changed gearbox");

	load_engine_tuning(&tuning->base[entry], config, &engine);

	if (plan->phase == MH_TUNING_LAUNCH)
		status = mh_race_tuning_launch(
			active_count, rank, plan->weight, &engine, error_out);
	else {
		race = &field->cars[entry].race;

		if (race->lap < 1 || race->lap > MH_RACE_MAX_LAPS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid catch-up lap");

		status = mh_race_tuning_car(&tuning->curve, rank, race->lap,
			race->finished, (unsigned long)field->path->count,
			(unsigned long)field->point[entry],
			race->splits[race->lap][CHECKPOINT_TWO_COLUMN],
			plan->weight, &engine, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	store_engine_tuning(&engine, config);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_field_tuning_frame(struct mh_field *field, struct mh_error *error_out)
{
	struct mh_field_tuning next;
	struct mh_engine_config configs[MH_RACE_TUNING_CARS];
	struct mh_race_tuning_plan plan;
	unsigned int i, active_count, mode;
	unsigned long go_ms;
	long double now_ms;
	enum mh_error_code status;

	if (field == NULL || field->count < 1 ||
		field->count > MH_RACE_MAX_CARS || field->active_mask == 0 ||
		field->active_mask >= (1U << field->count) ||
		(field->tuning.initialized != 0 &&
			field->tuning.initialized != 1) ||
		(field->tuning.catchup != 0 && field->tuning.catchup != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid field performance state");

	if (field->network)
		mode = MH_GAME_NETWORK;
	else if (field->playback)
		mode = (unsigned int)field->playback;
	else if (field->time_attack)
		mode = MH_GAME_TIME_ATTACK;
	else if (field->attract)
		mode = MH_GAME_ATTRACT;
	else
		mode = MH_GAME_QUICK_RACE;

	if (mode != MH_GAME_QUICK_RACE && mode != MH_GAME_ATTRACT) {
		for (i = 0; i < field->count; i++)
			field->cars[i].config.options.suppress_role_two_spin =
				0;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (field->count > MH_RACE_TUNING_CARS)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"ordinary race exceeds tuning capacity");

	if (!field->cars[0].frame.active &&
		field->tuning.ticks > DIAGNOSTIC_TICK_LIMIT)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"race tuning clock exceeds original range");

	next = field->tuning;

	for (i = 0; i < field->count; i++) {
		configs[i] = field->cars[i].config;

		if (!next.initialized)
			next.base[i] = configs[i];
	}

	next.initialized = 1;
	go_ms = field->cars[0].frame.active ? field->cars[0].frame.time.go_ms
		: field->cars[0].countdown  ? COUNTDOWN_MS
					: 0;
	now_ms = field->cars[0].frame.active
		? mh_drive_clock_ms(&field->cars[0])
		: (field->tuning.ticks / DIAGNOSTIC_HZ) * 1000 +
			(field->tuning.ticks % DIAGNOSTIC_HZ) *
				(1000.0 / DIAGNOSTIC_HZ);

	status = mh_race_tuning_schedule(mode, next.catchup, go_ms, now_ms,
		next.strength, field->active_mask, 0, &plan, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/*
	 * Run before the held-grid/menu physics gates. Sampled frames use GO=0
	 * until release; only fixed-tick diagnostics defer this pass.
	 */
	if ((!field->cars[0].frame.active && now_ms < go_ms) ||
		field->standings.count == 0)
		plan.apply_mask = 0;

	if (plan.apply_mask) {
		if (field->standings.count != field->count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing performance ranks");

		active_count = 0;

		for (i = 0; i < field->count; i++)
			if (field->active_mask & (1U << i))
				active_count++;

		if (plan.phase == MH_TUNING_CATCHUP) {
			if (field->path == NULL || field->path->count < 1 ||
				field->path->count > MH_AI_MAX_POINTS)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"missing catch-up AI points");

			status = mh_race_tuning_curve(&next.curve, active_count,
				field->standings.cars[0].position, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		for (i = 0; i < field->count; i++)
			if (plan.apply_mask & (1U << i)) {
				status = tune_car(field, &next, &configs[i],
					&plan, active_count, i, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}
	}

	for (i = 0; i < field->count; i++) {
		configs[i].options.suppress_role_two_spin =
			plan.suppress_role_two_spin;
		field->cars[i].config = configs[i];
	}

	field->tuning = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
