#include <limits.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/race_internal.h"
#include "engine/network/drive.h"
#include "engine/simulation/field_internal.h"

#define FRAME_ELAPSED_LIMIT 1e9

static enum mh_error_code
apply_pose(struct mh_drive *drive, const struct mh_demo_sample *sample,
	struct mh_error *error_out)
{
	struct mh_net_playback playback;
	struct mh_net_drive_sample applied;

	memset(&playback, 0, sizeof(playback));
	playback.ready = playback.updated = 1;
	playback.vehicle = sample->vehicle;
	memcpy(playback.world_velocity, sample->world_velocity,
		sizeof(playback.world_velocity));
	memcpy(playback.euler_rate, sample->euler_rate,
		sizeof(playback.euler_rate));

	return mh_net_drive_apply(drive, &playback, &applied, error_out);
}

enum mh_error_code
mh_demo_race_apply_samples(
	struct mh_demo_race *race, int initial, struct mh_error *error_out)
{
	const struct mh_demo_sample *sample;
	struct mh_drive *drive;
	unsigned int entry, slot, active_entries, unavailable_entries;
	double position[3], velocity[3];
	enum mh_error_code status;

	active_entries = unavailable_entries = 0;

	for (entry = 0; entry < race->layout.count; entry++) {
		slot = race->layout.recording[entry];
		drive = &race->field.cars[entry];

		if (slot == MH_DEMO_HUMAN_SLOT) {
			active_entries |= 1U << entry;
			continue;
		}

		sample = &race->playback.samples[slot];
		status = apply_pose(drive, sample, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (sample->active)
			active_entries |= 1U << entry;

		if (sample->active && sample->unavailable)
			unavailable_entries |= 1U << entry;

		if (initial) {
			status = mh_motion_display(&drive->vehicle.motion,
				position, velocity, error_out);

			if (status == MH_ERROR_NONE)
				status = mh_race_start(drive->route,
					drive->track->laps, position,
					&drive->race, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			drive->race.record_eligible = 0;
		}
	}

	if (active_entries == 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"recording has no active camera target");

	race->field.active_mask = active_entries;
	race->field.unavailable_mask = unavailable_entries;

	if (!(active_entries & (1U << race->field.viewed_entry))) {
		for (entry = 0; entry < race->layout.count; entry++)
			if (active_entries & (1U << entry))
				break;

		/*
		 * Preserve camera mode and notify the audio listener of the
		 * handoff.
		 */
		status = mh_field_select_view(&race->field, entry, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
collect_progress(const struct mh_race *live, unsigned long best_recorded_ms,
	struct mh_demo_progress *progress)
{
	memset(progress, 0, sizeof(*progress));

	progress->lap = live->lap;
	progress->split_notice = live->split_notice;
	progress->finished = live->finished;
	progress->now_ms = live->now_ms;
	progress->best_ms = live->best_ms;
	progress->lap_ms = live->lap_ms;
	progress->last_ms = live->last_ms;
	progress->split_ms = live->split_ms;
	progress->best_recorded_ms = best_recorded_ms;
	memcpy(progress->crossings, live->splits, sizeof(progress->crossings));
}

static void
apply_progress(struct mh_race *live, const struct mh_demo_progress *corrected)
{
	live->now_ms = corrected->now_ms;
	live->best_ms = corrected->best_ms;
	live->lap_ms = corrected->lap_ms;
	live->last_ms = corrected->last_ms;
	live->split_ms = corrected->split_ms;
	memcpy(live->splits, corrected->crossings, sizeof(live->splits));
}

static enum mh_error_code
correct_progress(struct mh_demo_race *race, struct mh_error *error_out)
{
	struct mh_demo_progress progress, corrected;
	struct mh_race *live;
	unsigned int entry, slot;
	enum mh_error_code status;

	if (!race->playback.active)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	for (entry = 0; entry < race->layout.count; entry++) {
		slot = race->layout.recording[entry];

		if (slot == MH_DEMO_HUMAN_SLOT ||
			!(race->field.active_mask & (1U << entry)))
			continue;

		live = &race->field.cars[entry].race;
		collect_progress(
			live, race->best_recorded_ms[entry], &progress);
		status = mh_demo_progress_correct(
			&race->playback.demo->players[slot], &progress,
			&corrected, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (corrected.now_ms > (unsigned long)MH_I32_MAX ||
			corrected.lap_ms > corrected.now_ms ||
			corrected.split_ms > (unsigned long)MH_I32_MAX ||
			corrected.last_ms > (unsigned long)MH_I32_MAX ||
			corrected.best_ms > (unsigned long)MH_I32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"recorded times exceed native race bounds");

		apply_progress(live, &corrected);
		race->best_recorded_ms[entry] = corrected.best_recorded_ms;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_race_frame_physics(struct mh_demo_race *race,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out)
{
	struct mh_demo_playback_event event;
	enum mh_error_code status;
	unsigned long clock_ms;
	unsigned int step_count;
	int ready;

	if (race == NULL || controls == NULL || steps == NULL ||
		!(elapsed >= 0 && elapsed <= FRAME_ELAPSED_LIMIT) ||
		race->field.count != race->layout.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded race frame");

	status = mh_field_frame_physics(&race->field, controls, elapsed, action,
		&step_count, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_frame_media_time(race->field.cars[0].frame.seconds,
			race->field.cars[0].frame.intro_ms, &ready, &clock_ms,
			error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (race->playback.benchmark) {
		status = mh_demo_benchmark_step(
			&race->benchmark, &race->playback, clock_ms, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	/* Physics, playback, then recording. */
	if (ready) {
		status = mh_demo_playback_step(
			&race->playback, clock_ms, &event, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (event.updated) {
			status = mh_demo_race_apply_samples(race, 0, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		if (event.started || event.ended) {
			race->notice = event.ended ? MH_DEMO_NOTICE_ENDED
						: MH_DEMO_NOTICE_STARTED;
			race->notice_ms = clock_ms;
		}
	}

	if (race->ticks > ULONG_MAX - step_count)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"recorded race slice count limit reached");

	race->ticks += step_count;
	*steps = step_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_race_frame_finish(struct mh_demo_race *race, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_field_frame_update(&race->field,
		race->field.cars[0].frame.time.elapsed, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_field_frame_progress(&race->field, error_out);

	/* Correct recorded times before ordering. */
	if (status == MH_ERROR_NONE)
		status = correct_progress(race, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_field_frame_results(&race->field, error_out);
}

enum mh_error_code
mh_demo_race_frame(struct mh_demo_race *race,
	const struct mh_engine_input *controls, long double elapsed,
	enum mh_frame_action action, unsigned int *steps,
	struct mh_error *error_out)
{
	struct mh_demo_race next;
	enum mh_error_code status;
	unsigned int step_count;

	if (race == NULL || controls == NULL || steps == NULL ||
		!(elapsed >= 0 && elapsed <= FRAME_ELAPSED_LIMIT) ||
		(action != MH_FRAME_RUN && action != MH_FRAME_SUSPEND &&
			action != MH_FRAME_MENU) ||
		race->field.count != race->layout.count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid recorded race frame");

	next = *race;
	step_count = 0;

	if (action == MH_FRAME_SUSPEND)
		next.field.accumulator = 0;
	else {
		status = mh_demo_race_frame_physics(&next, controls, elapsed,
			action, &step_count, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_demo_race_frame_finish(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*race = next;
	*steps = step_count;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
