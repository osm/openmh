#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/demo/playback.h"

#define WORLD_POSITION_LIMIT 1000000
#define REFILL_FRAMES (MH_DEMO_BUFFER_FRAMES / 2)

struct playback_pair {
	struct mh_demo_frame	older, newer;
	unsigned int		index, next_index;
	int			passed_frame;
};

static int
is_boolean(int value)
{
	return value == 0 || value == 1;
}

static enum mh_error_code
decode_frame_sample(const struct mh_demo_frame *frame, unsigned int slot,
	struct mh_demo_sample *sample, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i;

	memset(sample, 0, sizeof(*sample));
	status = mh_net_vehicle_decode(frame->vehicles[slot],
		MH_DEMO_VEHICLE_BYTES, &sample->vehicle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 3; i++)
		if (!(sample->vehicle.position[i] >= -WORLD_POSITION_LIMIT &&
			sample->vehicle.position[i] <=
				WORLD_POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"demo position outside world bounds");

	sample->active = (frame->active_mask & (1U << slot)) != 0;
	sample->unavailable = (frame->unavailable_mask & (1U << slot)) != 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

/* Validate every frame and seed the private frame-zero samples. */
static enum mh_error_code
prepare_samples(struct mh_demo_playback *next, struct mh_error *error_out)
{
	const struct mh_demo *demo;
	struct mh_demo_frame frame;
	struct mh_demo_sample sample;
	enum mh_error_code status;
	unsigned long previous_ms;
	size_t frame_index;
	unsigned int slot, active_players;

	demo = next->demo;
	previous_ms = 0;
	status = MH_ERROR_NONE;

	for (frame_index = 0; frame_index < demo->frame_count; frame_index++) {
		status = mh_demo_frame_read(
			demo, frame_index, &frame, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (frame_index != 0 && frame.milliseconds <= previous_ms)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"demo timestamps are not increasing");

		previous_ms = frame.milliseconds;
		active_players = 0;

		for (slot = 0; slot < MH_DEMO_MAX_PLAYERS; slot++)
			if (demo->players[slot].active) {
				status = decode_frame_sample(
					&frame, slot, &sample, error_out);

				if (status != MH_ERROR_NONE)
					return status;

				if (frame_index == 0 &&
					active_players < next->sample_count) {
					if (next->ghost)
						sample.unavailable = 1;

					next->samples[slot] = sample;
				}

				active_players++;
			}
	}

	return status;
}

enum mh_error_code
mh_demo_playback_begin(const struct mh_demo *demo, int ghost,
	struct mh_demo_playback *output, struct mh_error *error_out)
{
	struct mh_demo_playback result;
	enum mh_error_code status;
	size_t frame_index;
	unsigned int slot, active_players, slots;

	slots = demo == NULL ? 0 : mh_demo_slots(demo->version);

	if (slots == 0 || output == NULL || !is_boolean(ghost) ||
		demo->frame_count < MH_DEMO_BUFFER_FRAMES ||
		demo->player_count < 1 || demo->player_count > slots ||
		demo->local_player >= slots ||
		!demo->players[demo->local_player].active)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo playback context");

	active_players = 0;

	for (slot = 0; slot < MH_DEMO_MAX_PLAYERS; slot++)
		if (demo->players[slot].active)
			active_players++;

	if (active_players != demo->player_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"inconsistent demo playback slots");

	memset(&result, 0, sizeof(result));
	result.demo = demo;
	result.ghost = ghost;
	result.sample_count = demo->player_count;

	if (ghost && result.sample_count == slots)
		result.sample_count--;

	result.active = 1;
	result.next_frame = MH_DEMO_BUFFER_FRAMES;

	for (frame_index = 0; frame_index < MH_DEMO_BUFFER_FRAMES;
		frame_index++)
		result.frame_indices[frame_index] = frame_index;

	status = prepare_samples(&result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
refill_half(struct mh_demo_playback *playback, unsigned int start)
{
	size_t count;
	unsigned int i;

	count = playback->demo->frame_count - playback->next_frame;

	if (count > REFILL_FRAMES)
		count = REFILL_FRAMES;

	for (i = 0; i < count; i++)
		playback->frame_indices[start + i] = playback->next_frame + i;

	playback->next_frame += count;

	/*
	 * fread sets EOF on a short read, not merely when the final complete
	 * block is returned. Unwritten slots retain their earlier contents.
	 */
	if (count < REFILL_FRAMES)
		playback->eof = 1;
}

static enum mh_error_code
select_pair(const struct mh_demo_playback *playback, unsigned long target_ms,
	struct playback_pair *pair, struct mh_error *error_out)
{
	unsigned int index, next_index;
	enum mh_error_code status;

	pair->passed_frame = 0;

	for (index = 0; index < MH_DEMO_BUFFER_FRAMES; index++) {
		next_index = (index + 1) % MH_DEMO_BUFFER_FRAMES;
		status = mh_demo_frame_read(playback->demo,
			playback->frame_indices[index], &pair->older,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (target_ms > pair->older.milliseconds)
			pair->passed_frame = 1;

		if (target_ms < pair->older.milliseconds)
			continue;

		status = mh_demo_frame_read(playback->demo,
			playback->frame_indices[next_index], &pair->newer,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (target_ms < pair->newer.milliseconds)
			break;
	}

	pair->index = index;
	pair->next_index = next_index;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
sample_players(struct mh_demo_playback *playback,
	const struct playback_pair *pair, struct mh_error *error_out)
{
	unsigned int slot, sampled_players;
	enum mh_error_code status;

	sampled_players = 0;

	for (slot = 0; slot < MH_DEMO_MAX_PLAYERS &&
		sampled_players < playback->sample_count;
		slot++) {
		if (!playback->demo->players[slot].active)
			continue;

		status = mh_demo_sample_pair(&pair->older, &pair->newer, slot,
			playback->target_ms, &playback->samples[slot],
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (playback->ghost)
			playback->samples[slot].unavailable = 1;

		sampled_players++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_demo_playback_step(struct mh_demo_playback *playback, unsigned long clock_ms,
	struct mh_demo_playback_event *event, struct mh_error *error_out)
{
	struct mh_demo_playback next;
	struct mh_demo_playback_event change;
	struct playback_pair pair;
	enum mh_error_code status;
	unsigned int expected_players, slots;

	slots = playback == NULL || playback->demo == NULL
		? 0
		: mh_demo_slots(playback->demo->version);

	if (slots == 0 || event == NULL || !is_boolean(playback->ghost) ||
		!is_boolean(playback->active) || !is_boolean(playback->eof) ||
		!is_boolean(playback->active_half) ||
		!is_boolean(playback->benchmark) ||
		playback->demo->player_count < 1 ||
		playback->demo->player_count > slots ||
		playback->next_frame < MH_DEMO_BUFFER_FRAMES ||
		playback->next_frame > playback->demo->frame_count ||
		clock_ms > MH_U32_MAX || clock_ms < playback->now_ms ||
		playback->anchor_ms > clock_ms)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo playback clock or state");

	expected_players = playback->demo->player_count;

	if (playback->ghost && expected_players == slots)
		expected_players--;

	if (playback->sample_count != expected_players)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid demo playback roster");

	next = *playback;
	memset(&change, 0, sizeof(change));
	change.ring_index = MH_DEMO_BUFFER_FRAMES;

	if (playback->active) {
		next.now_ms = clock_ms;

		if (next.anchor_ms == 0) {
			next.anchor_ms = clock_ms;
			next.target_ms = 0;
			change.started = 1;
		}

		next.target_ms =
			next.benchmark && next.demo->simulation_rate == 0
			? (next.target_ms + MH_DEMO_BENCHMARK_STEP_MS) &
				MH_U32_MAX
			: clock_ms - next.anchor_ms;
		status =
			select_pair(playback, next.target_ms, &pair, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (pair.index == MH_DEMO_BUFFER_FRAMES) {
			if (pair.passed_frame) {
				next.active = 0;
				change.ended = 1;
			}
		} else {
			change.updated = 1;
			change.ring_index = pair.index;
			change.older_frame =
				playback->frame_indices[pair.index];
			change.newer_frame =
				playback->frame_indices[pair.next_index];
			status = sample_players(&next, &pair, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (!next.eof) {
				if (pair.index >= REFILL_FRAMES &&
					next.active_half == 0) {
					refill_half(&next, 0);
					next.active_half = 1;
				} else if (pair.index < REFILL_FRAMES &&
					next.active_half == 1) {
					refill_half(&next, REFILL_FRAMES);
					next.active_half = 0;
				}
			}
		}
	}

	*playback = next;
	*event = change;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
