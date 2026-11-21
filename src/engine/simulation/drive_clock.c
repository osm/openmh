#include <limits.h>

#include "engine/core/error.h"
#include "engine/simulation/drive_internal.h"
#include "engine/simulation/tick_clock.h"

#define DIAGNOSTIC_THREE_TICKS 720
#define DIAGNOSTIC_TWO_TICKS 960
#define DIAGNOSTIC_ONE_TICKS 1200
#define DIAGNOSTIC_GO_TICKS 1440
#define DIAGNOSTIC_CUE_END_TICKS 1680
#define DIAGNOSTIC_RACE_TICK_LIMIT 257698037UL
#define DIAGNOSTIC_TICKS_PER_SECOND 120
#define DIAGNOSTIC_TICKS_PER_MS .12
#define NATIVE_CLOCK_EPOCH .001
#define GO_DELAY_MS 12000UL
#define GO_DELAY_MS_L 12000.0L
#define DIAGNOSTIC_CUE_END_MS 14000
#define RESTART_CLOCK_LIMIT 4294953.295

enum mh_start_phase
mh_drive_start_phase(const struct mh_drive *drive)
{
	if (drive->frame.active) {
		if (!drive->countdown)
			return MH_START_RACING;

		switch (drive->frame.time.cue) {
		case 3:
			return MH_START_THREE;
		case 2:
			return MH_START_TWO;
		case 1:
			return MH_START_ONE;
		case 0:
			return MH_START_GO;
		default:
			return MH_START_NONE;
		}
	}

	if (!drive->countdown || drive->start_ticks >= DIAGNOSTIC_CUE_END_TICKS)
		return MH_START_RACING;

	if (drive->start_ticks < DIAGNOSTIC_THREE_TICKS)
		return MH_START_READY;

	if (drive->start_ticks < DIAGNOSTIC_TWO_TICKS)
		return MH_START_THREE;

	if (drive->start_ticks < DIAGNOSTIC_ONE_TICKS)
		return MH_START_TWO;

	if (drive->start_ticks < DIAGNOSTIC_GO_TICKS)
		return MH_START_ONE;

	return MH_START_GO;
}

int
mh_drive_racing(const struct mh_drive *drive)
{
	return drive->frame.active ? drive->frame.time.go_ms != 0
				: !drive->countdown ||
			drive->start_ticks >= DIAGNOSTIC_GO_TICKS;
}

long double
mh_drive_clock_ms(const struct mh_drive *drive)
{
	return drive->frame.active ? (long double)drive->frame.seconds * 1000.0L
				: 1 + drive->seconds * 1000.0;
}

double
mh_drive_intro_ms(const struct mh_drive *drive)
{
	return drive->frame.active
		? (double)((long double)drive->frame.seconds * 1000.0L -
			drive->frame.intro_ms)
		: (double)drive->start_ticks * 1000.0 / 120.0;
}

enum mh_error_code
mh_drive_finish_tick(struct mh_drive *drive, struct mh_error *error_out)
{
	struct mh_drive next;
	double position[3], velocity[3];
	unsigned long milliseconds;
	int held;
	enum mh_error_code status;

	if (drive == NULL || drive->car == NULL || drive->col == NULL ||
		drive->track == NULL || drive->ticks == ULONG_MAX ||
		!(drive->seconds >= 0 &&
			drive->seconds <= MH_DRIVE_TIME_LIMIT - MH_DRIVE_DT))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driving presentation tick");

	next = *drive;

	/*
	 * Frame callers sample clocks once before physics, then update route
	 * progress once after frame recovery. Only the diagnostic path below
	 * advances clocks/progress at the end of each fixed slice.
	 */
	if (next.frame.active) {
		next.ticks++;
		*drive = next;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	held = next.countdown && next.start_ticks < DIAGNOSTIC_GO_TICKS;

	if (next.countdown && next.start_ticks < DIAGNOSTIC_CUE_END_TICKS)
		next.start_ticks++;

	if (!drive->race_frozen && drive->track->laps != 0) {
		if (!drive->race.finished &&
			next.race_ticks >= DIAGNOSTIC_RACE_TICK_LIMIT)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"race clock limit reached");

		if (!held && !drive->race.finished)
			next.race_ticks++;

		milliseconds = drive->race.finished
			? drive->race.now_ms
			: (next.race_ticks / DIAGNOSTIC_TICKS_PER_SECOND) *
					1000 +
				(next.race_ticks %
					DIAGNOSTIC_TICKS_PER_SECOND) *
					1000 / DIAGNOSTIC_TICKS_PER_SECOND;
		status = mh_motion_display(
			&next.vehicle.motion, position, velocity, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_race_step(&drive->race, position,
				milliseconds, &next.race, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else if (!drive->race_frozen) {
		next.race.lap_event = 0;
		next.race.split_event = 0;
	}

	next.ticks++;
	next.seconds += MH_DRIVE_DT;
	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_frame_sample(const struct mh_drive *drive, long double elapsed,
	struct mh_drive_frame *output, struct mh_error *error_out)
{
	struct mh_drive_frame next;
	long double now_ms;
	enum mh_error_code status;

	if (drive == NULL || output == NULL ||
		!(elapsed >= 0 && elapsed <= MH_DRIVE_TIME_LIMIT) ||
		!(drive->seconds >= 0 &&
			drive->seconds <= MH_DRIVE_TIME_LIMIT - elapsed))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driving frame clock");

	next = drive->frame;

	if (next.fixed_rate != 0 && !mh_sim_rate_valid(next.fixed_rate))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unsupported simulation rate");

	if (next.fixed_rate != 0 && elapsed != 0 &&
		elapsed != 1.0L / next.fixed_rate)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"fixed simulation requires a complete tick");

	if (next.pending && next.fixed_rate == 0) {
		if (next.pending != 1 || !next.active || elapsed != 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid staged frame clock");

		next.pending = 0;
		*output = next;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (!next.active) {
		next.seconds = drive->seconds + NATIVE_CLOCK_EPOCH;
		next.time.now_ms = (float)((long double)next.seconds * 1000.0L);
		next.active = 1;

		/*
		 * --ticks/input replays may precede an interactive window.
		 * Preserve their already-released countdown and monotonic
		 * published race time. This handoff is native policy, not an
		 * original clock acquisition.
		 */
		now_ms = (long double)next.seconds * 1000.0L;

		if (drive->countdown &&
			drive->start_ticks >= DIAGNOSTIC_GO_TICKS &&
			next.time.go_ms == 0 &&
			now_ms >= (long double)next.intro_ms + GO_DELAY_MS_L) {
			next.time.go_ms = next.intro_ms + GO_DELAY_MS;

			if (!drive->race.finished &&
				now_ms - next.time.go_ms < drive->race.now_ms) {
				if (now_ms - drive->race.now_ms < 1)
					return mh_error_set(error_out,
						MH_ERROR_RANGE,
						"diagnostic race clock exceeds session time");

				next.time.go_ms = (unsigned long)(now_ms -
					drive->race.now_ms);
			}
		}
	}

	if (!drive->countdown && next.time.go_ms == 0)
		next.time.go_ms = next.intro_ms;

	/*
	 * Counter division and scaling stay extended through this addition.
	 * Only the accumulated seconds receive a binary64 store.
	 */
	if (next.fixed_rate != 0) {
		if (next.fixed_ticks == 0)
			next.fixed_epoch = next.seconds;

		if (elapsed != 0) {
			if (next.fixed_ticks == MH_U32_MAX)
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"simulation tick limit reached");

			next.fixed_ticks++;
		}

		next.seconds = (double)((long double)next.fixed_epoch +
			(long double)next.fixed_ticks / next.fixed_rate);
		next.pending = 0;
	} else
		next.seconds = (double)((long double)next.seconds + elapsed);

	status = mh_frame_time(next.seconds, next.time.now_ms,
		drive->countdown ? next.intro_ms : 0, next.time.go_ms,
		&next.time, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Physics never subtracts the binary32 absolute millisecond clock. */
	if (next.fixed_rate != 0)
		next.time.elapsed = (float)elapsed;

	next.step = 0;
	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_clock_restart(
	struct mh_drive *drive, long double elapsed, struct mh_error *error_out)
{
	struct mh_drive next;

	if (drive == NULL ||
		!(elapsed >= 0 && elapsed <= MH_DRIVE_TIME_LIMIT) ||
		!(drive->seconds >= 0 &&
			drive->seconds <= MH_DRIVE_TIME_LIMIT - elapsed))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid restart interval");

	next = *drive;

	if (!next.frame.active)
		next.frame.seconds = next.seconds + NATIVE_CLOCK_EPOCH;

	next.frame.seconds =
		(double)((long double)next.frame.seconds + elapsed);
	next.seconds = (double)((long double)next.seconds + elapsed);

	if (!(next.frame.seconds >= NATIVE_CLOCK_EPOCH &&
		next.frame.seconds < RESTART_CLOCK_LIMIT))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid reset clock");

	next.frame.intro_ms =
		(unsigned long)((long double)next.frame.seconds * 1000.0L);
	next.frame.time.go_ms = next.countdown ? 0 : next.frame.intro_ms;
	next.frame.time.race_ms = 0;
	next.frame.time.cue = -1;
	next.frame.step = 0;

	if (next.frame.fixed_rate != 0) {
		next.frame.fixed_ticks = 0;
		next.frame.fixed_epoch = next.frame.seconds;
	}

	/*
	 * Restart does not reset the frame timestamp. The already sampled
	 * interval drives the remainder of this frame; next frame subtracts the
	 * retained timestamp, including time spent restarting. Fresh diagnostic
	 * starts keep their explicit initial-clock policy.
	 */
	if (!next.frame.active) {
		next.frame.time.now_ms =
			(float)((long double)next.frame.seconds * 1000.0L);
		next.frame.time.elapsed = 0;
	}

	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_frame_begin(
	struct mh_drive *drive, long double elapsed, struct mh_error *error_out)
{
	struct mh_drive next;
	double intro_elapsed_ms;
	enum mh_error_code status;

	if (drive == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing driving session");

	next = *drive;
	status = mh_drive_frame_sample(drive, elapsed, &next.frame, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.seconds = (double)((long double)next.seconds + elapsed);

	if (next.frame.fixed_rate != 0)
		next.seconds = next.frame.seconds;

	intro_elapsed_ms = mh_drive_intro_ms(&next);

	/*
	 * Retained trace field only. Frame gates use the sampled GO/cue state.
	 */
	if (!next.countdown || next.frame.intro_ms == 0 ||
		intro_elapsed_ms <= 0)
		next.start_ticks = 0;
	else if (intro_elapsed_ms >= DIAGNOSTIC_CUE_END_MS)
		next.start_ticks = DIAGNOSTIC_CUE_END_TICKS;
	else
		next.start_ticks = (unsigned int)(intro_elapsed_ms *
			DIAGNOSTIC_TICKS_PER_MS);

	next.accumulator = 0;
	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_drive_frame_finish(struct mh_drive *drive, struct mh_error *error_out)
{
	struct mh_drive next;
	double position[3], velocity[3];
	enum mh_error_code status;

	if (drive == NULL || drive->track == NULL || !drive->frame.active)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driving frame progress");

	next = *drive;

	if (!drive->race_frozen && drive->track->laps != 0) {
		status = mh_motion_display(
			&next.vehicle.motion, position, velocity, error_out);

		if (status == MH_ERROR_NONE)
			status = mh_race_step(&drive->race, position,
				drive->race.finished ? drive->race.now_ms
						: next.frame.time.race_ms,
				&next.race, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else if (!drive->race_frozen) {
		next.race.lap_event = 0;
		next.race.split_event = 0;
	}

	*drive = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
