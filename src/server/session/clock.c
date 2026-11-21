#include <string.h>

#include "engine/core/limits.h"
#include "server/session/clock.h"

#define PLAYER_MASK ((1U << (MH_SERVER_RACERS + 1)) - 1U)
#define REMOTE_MASK (PLAYER_MASK & ~1U)
#define INTRODUCTION_MS 12000UL
#define FINISH_GRACE_MS 5000UL
#define SECOND_MS 1000UL
#define MINUTE_MS 60000UL
#define WARNING_HALF_MS 500UL

static unsigned long
clock_word(long double value)
{
	if (value < 0)
		return (0UL - (unsigned long)-value) & MH_U32_MAX;

	return (unsigned long)value & MH_U32_MAX;
}

void
mh_server_clock_reset(struct mh_server_clock *clock)
{
	if (clock != NULL)
		memset(clock, 0, sizeof(*clock));
}

static void
update_finish_anchor(struct mh_server_clock *clock,
	const struct mh_server_config *config,
	const struct mh_server_clock_sample *sample)
{
	unsigned int active_mask, complete_mask;
	unsigned long adjustment;

	active_mask = sample->racing & REMOTE_MASK;
	complete_mask = sample->finished;

	if (sample->mode == MH_SERVER_NORMAL && (active_mask & complete_mask) &&
		clock->finish_anchor_ms == 0)
		clock->finish_anchor_ms = (unsigned long)sample->now_ms;

	if (clock->all_finished)
		return;

	if (sample->mode == MH_SERVER_ELIMINATION)
		complete_mask |= sample->eliminated;

	if ((active_mask & ~complete_mask) != 0)
		return;

	clock->all_finished = 1;

	/* The final +5000 below makes this a ten-second delay, not five. */
	adjustment =
		(config->end_countdown_seconds * SECOND_MS - FINISH_GRACE_MS) &
		MH_U32_MAX;
	clock->finish_anchor_ms = clock_word(sample->now_ms - adjustment);
}

static void
finish_notice(const struct mh_server_clock *clock,
	const struct mh_server_config *config,
	const struct mh_server_clock_sample *sample,
	struct mh_server_clock_tick *tick)
{
	unsigned long deadline;

	if (sample->mode != MH_SERVER_NORMAL || clock->finish_anchor_ms == 0)
		return;

	tick->finish_recipients =
		sample->racing & ~sample->finished & REMOTE_MASK;

	if (tick->finish_recipients == 0)
		return;

	deadline = (clock->finish_anchor_ms +
			config->end_countdown_seconds * SECOND_MS) &
		MH_U32_MAX;

	if (sample->now_ms < deadline) {
		tick->finish_notice = MH_SERVER_CLOCK_TIME_LEFT;
		tick->remaining_seconds =
			((deadline - (unsigned long)sample->now_ms) &
				MH_U32_MAX) /
			SECOND_MS;
	} else
		tick->finish_notice = MH_SERVER_CLOCK_DID_NOT_FINISH;
}

static void
update_race_start(struct mh_server_clock *clock,
	const struct mh_server_clock_sample *sample,
	struct mh_server_clock_tick *tick)
{
	unsigned long deadline;

	deadline = (sample->countdown_ms + INTRODUCTION_MS) & MH_U32_MAX;

	if (sample->countdown_ms != 0 && clock->race_start_ms == 0 &&
		sample->now_ms > deadline) {
		clock->race_start_ms = (unsigned long)sample->now_ms;
		tick->started = 1;
	}

	if (clock->race_start_ms != 0)
		tick->elapsed_ms =
			clock_word(sample->now_ms - clock->race_start_ms);
}

static void
check_deadlines(const struct mh_server_clock *clock,
	const struct mh_server_config *config,
	const struct mh_server_clock_sample *sample,
	struct mh_server_clock_tick *tick)
{
	unsigned long finish_deadline, limit_ms, warning_ms;
	long double elapsed;

	finish_deadline = (clock->finish_anchor_ms +
				config->end_countdown_seconds * SECOND_MS +
				FINISH_GRACE_MS) &
		MH_U32_MAX;

	if (clock->finish_anchor_ms != 0 && sample->now_ms > finish_deadline) {
		tick->end = MH_SERVER_CLOCK_FINISHED;
		return;
	}

	if (clock->race_start_ms == 0)
		return;

	limit_ms = (config->max_race_minutes * MINUTE_MS) & MH_U32_MAX;
	warning_ms = (limit_ms - FINISH_GRACE_MS) & MH_U32_MAX;
	elapsed = sample->now_ms - clock->race_start_ms;

	if (elapsed > warning_ms) {
		tick->limit_recipients = sample->racing & REMOTE_MASK;

		if (tick->limit_recipients != 0)
			tick->limit_notice =
				(unsigned long)sample->now_ms % SECOND_MS <
					WARNING_HALF_MS
				? MH_SERVER_CLOCK_RACE_ENDING
				: MH_SERVER_CLOCK_BLANK;
	}

	if (elapsed > limit_ms)
		tick->end = MH_SERVER_CLOCK_TIME_LIMIT;
}

enum mh_error_code
mh_server_clock_step(struct mh_server_clock *clock,
	const struct mh_server_config *config,
	const struct mh_server_clock_sample *sample,
	struct mh_server_clock_tick *output, struct mh_error *error_out)
{
	struct mh_server_clock next;
	struct mh_server_clock_tick tick;

	if (clock == NULL || config == NULL || sample == NULL ||
		output == NULL ||
		(sample->mode != MH_SERVER_NORMAL &&
			sample->mode != MH_SERVER_ELIMINATION) ||
		config->end_countdown_seconds > MH_U32_MAX ||
		config->max_race_minutes > MH_U32_MAX ||
		clock->race_start_ms > MH_U32_MAX ||
		clock->finish_anchor_ms > MH_U32_MAX ||
		(clock->all_finished != 0 && clock->all_finished != 1) ||
		sample->racing > PLAYER_MASK ||
		sample->finished > PLAYER_MASK ||
		sample->eliminated > PLAYER_MASK ||
		sample->countdown_ms > MH_U32_MAX ||
		!(sample->now_ms >= clock->now_ms && sample->now_ms >= 0 &&
			sample->now_ms < MH_U32_MODULUS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated race clock");

	next = *clock;
	next.now_ms = sample->now_ms;
	memset(&tick, 0, sizeof(tick));
	update_finish_anchor(&next, config, sample);
	finish_notice(&next, config, sample, &tick);
	update_race_start(&next, sample, &tick);
	check_deadlines(&next, config, sample, &tick);
	*clock = next;
	*output = tick;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
