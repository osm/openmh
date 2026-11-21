#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/history.h"

#define WORLD_POSITION_LIMIT 1000000
/* Desired older entry range; reanchoring only affects the next sample. */
#define STABLE_OLDER_MIN 2
#define STABLE_OLDER_MAX 5
#define RECENT_ANCHOR_INDEX 2
#define LAGGING_ANCHOR_INDEX 3
#define ANGLE_WRAP_THRESHOLD 4
#define ANGLE_TURN 6.283185482025146484375L

static int
clock_valid(const struct mh_net_history *history, double now_ms)
{
	return history != NULL && history->count <= MH_NET_HISTORY &&
		now_ms >= history->now_ms && now_ms <= (double)MH_U32_MAX;
}

void
mh_net_history_reset(struct mh_net_history *history)
{
	if (history != NULL)
		memset(history, 0, sizeof(*history));
}

enum mh_error_code
mh_net_history_receive(struct mh_net_history *history, unsigned long stamp,
	const unsigned char *body, size_t size, double now_ms, int *accepted,
	struct mh_error *error_out)
{
	struct mh_net_vehicle vehicle;
	enum mh_error_code status;
	unsigned int i;

	if (!clock_valid(history, now_ms) || accepted == NULL ||
		stamp > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid snapshot receive context");

	status = mh_net_vehicle_decode(body, size, &vehicle, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 3; i++)
		if (!(vehicle.position[i] >= -WORLD_POSITION_LIMIT &&
			vehicle.position[i] <= WORLD_POSITION_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"snapshot outside world bounds");

	if (stamp == 0 ||
		(history->count != 0 && stamp <= history->entries[0].stamp)) {
		*accepted = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	memmove(history->entries + 1, history->entries,
		(MH_NET_HISTORY - 1) * sizeof(history->entries[0]));
	history->entries[0].stamp = stamp;
	history->entries[0].vehicle = vehicle;

	if (history->count < MH_NET_HISTORY)
		history->count++;

	history->now_ms = now_ms;
	history->received_ms = (unsigned long)now_ms;
	*accepted = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static double
mix_channel(double newer, double older, long double ratio)
{
	return (double)(float)(((long double)newer - (long double)older) *
			ratio +
		(long double)older);
}

static void
interpolate_angle(double newer_angle, double older_angle, long double ratio,
	long double seconds, double *angle, double *rate)
{
	long difference;

	difference = (long)(newer_angle - older_angle);

	/*
	 * Original truncates to integer BEFORE comparing abs(delta) to
	 * pi.
	 */
	if (difference >= ANGLE_WRAP_THRESHOLD ||
		difference <= -ANGLE_WRAP_THRESHOLD) {
		if (newer_angle < older_angle)
			newer_angle = (double)(float)((long double)newer_angle +
				ANGLE_TURN);
		else
			older_angle = (double)(float)((long double)older_angle +
				ANGLE_TURN);
	}

	*angle = mix_channel(newer_angle, older_angle, ratio);
	*rate = (double)(float)(((long double)newer_angle - older_angle) /
		seconds);
}

static void
interpolate_snapshot(struct mh_net_playback *output,
	const struct mh_net_snapshot *newer_snapshot,
	const struct mh_net_snapshot *older_snapshot,
	unsigned long target_stamp, int patched)
{
	const struct mh_net_vehicle *newer, *older;
	struct mh_net_vehicle *vehicle;
	long double ratio, stored_ratio, seconds;
	unsigned int i;

	newer = &newer_snapshot->vehicle;
	older = &older_snapshot->vehicle;
	vehicle = &output->vehicle;
	*vehicle = *newer;
	ratio = (long double)(target_stamp - older_snapshot->stamp) /
		(long double)(newer_snapshot->stamp - older_snapshot->stamp);
	stored_ratio = (long double)(float)ratio;

	if (patched)
		ratio = stored_ratio;

	seconds = (long double)(newer_snapshot->stamp - older_snapshot->stamp) *
		(long double).001;

	for (i = 0; i < 4; i++) {
		vehicle->extension[i] = mix_channel(
			newer->extension[i], older->extension[i], ratio);
		vehicle->auxiliary[i] = mix_channel(
			newer->auxiliary[i], older->auxiliary[i], stored_ratio);
	}

	vehicle->steering =
		mix_channel(newer->steering, older->steering, stored_ratio);
	vehicle->rpm = mix_channel(newer->rpm, older->rpm, stored_ratio);
	vehicle->slide = mix_channel(newer->slide, older->slide, stored_ratio);

	for (i = 0; i < 2; i++)
		vehicle->wheel_speed[i] = mix_channel(newer->wheel_speed[i],
			older->wheel_speed[i], stored_ratio);

	for (i = 0; i < 3; i++) {
		vehicle->position[i] = mix_channel(
			newer->position[i], older->position[i], stored_ratio);
		output->world_velocity[i] =
			(double)(float)(((long double)newer->position[i] -
						older->position[i]) /
				seconds);
		interpolate_angle(newer->angles[i], older->angles[i],
			stored_ratio, seconds, &vehicle->angles[i],
			&output->euler_rate[i]);
	}
}

static enum mh_error_code
sample_history(struct mh_net_history *history,
	const struct mh_net_prediction *prediction, double now_ms,
	struct mh_net_playback *output, struct mh_error *error_out)
{
	struct mh_net_history next;
	struct mh_net_playback result;
	unsigned int i, anchor_index, minimum, maximum, midpoint;
	long double remote_time;
	unsigned long target_stamp;

	if (!clock_valid(history, now_ms) || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid snapshot sample context");

	for (i = 0; i < history->count; i++) {
		if (history->entries[i].stamp == 0 ||
			history->entries[i].stamp > MH_U32_MAX ||
			(i > 0 &&
				history->entries[i - 1].stamp <=
					history->entries[i].stamp))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid snapshot history order");
	}

	minimum = prediction == NULL ? STABLE_OLDER_MIN : prediction->minimum;
	maximum = prediction == NULL ? STABLE_OLDER_MAX : prediction->maximum;
	midpoint = prediction == NULL ? RECENT_ANCHOR_INDEX
				: (minimum + maximum) / 2;

	/* Both CFG limits can be 10; do not read the following player field. */
	if (midpoint >= MH_NET_HISTORY)
		midpoint = MH_NET_HISTORY - 1;

	next = *history;
	next.now_ms = now_ms;
	result = history->playback;
	result.updated = 0;
	result.stale = history->count == 0 ||
		now_ms - history->received_ms > MH_NET_STALE_MS;
	result.older_index = MH_NET_HISTORY;

	if (history->count == MH_NET_HISTORY && next.local_anchor == 0) {
		next.local_anchor = (unsigned long)now_ms;
		next.remote_anchor = history->entries[midpoint].stamp;
	}

	if (next.remote_anchor != 0) {
		if (history->count != MH_NET_HISTORY ||
			next.local_anchor > now_ms ||
			next.remote_anchor > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid snapshot playback anchor");

		remote_time = (long double)now_ms - next.local_anchor +
			next.remote_anchor;

		if (remote_time >= MH_U32_MODULUS)
			remote_time -= MH_U32_MODULUS;

		target_stamp = (unsigned long)remote_time;
		result.target_stamp = target_stamp;

		for (i = 1; i < MH_NET_HISTORY; i++)
			if (target_stamp >= history->entries[i].stamp &&
				(prediction == NULL ||
					target_stamp <
						history->entries[i - 1].stamp))
				break;

		result.older_index = i;

		if (i < MH_NET_HISTORY) {
			if (i < minimum || i > maximum) {
				next.local_anchor = (unsigned long)now_ms;

				if (prediction != NULL)
					anchor_index = midpoint;
				else if (i < STABLE_OLDER_MIN)
					anchor_index = RECENT_ANCHOR_INDEX;
				else
					anchor_index = LAGGING_ANCHOR_INDEX;

				next.remote_anchor =
					history->entries[anchor_index].stamp;
			}

			interpolate_snapshot(&result, &history->entries[i - 1],
				&history->entries[i], target_stamp,
				prediction != NULL);
			result.ready = result.updated = 1;
		} else if (prediction != NULL &&
			next.sampled_stamp != history->entries[0].stamp) {
			next.local_anchor = (unsigned long)now_ms;
			next.remote_anchor = history->entries[midpoint].stamp;
		}

		next.sampled_stamp = history->entries[0].stamp;
	}

	next.playback = result;
	*history = next;
	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_history_sample(struct mh_net_history *history, double now_ms,
	struct mh_net_playback *output, struct mh_error *error_out)
{
	return sample_history(history, NULL, now_ms, output, error_out);
}

static unsigned int
prediction_bound(unsigned int value)
{
	if (value < 1)
		return 1;

	return value > MH_NET_PREDICTION_LIMIT ? MH_NET_PREDICTION_LIMIT
					: value;
}

enum mh_error_code
mh_net_history_predict(struct mh_net_history *history,
	struct mh_net_prediction *prediction, double now_ms,
	struct mh_net_playback *output, struct mh_error *error_out)
{
	struct mh_net_prediction next;
	enum mh_error_code status;

	if (prediction == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing snapshot prediction limits");

	next.minimum = prediction_bound(prediction->minimum);
	next.maximum = prediction_bound(prediction->maximum);

	if (next.minimum > next.maximum)
		next.minimum = next.maximum;

	status = sample_history(history, &next, now_ms, output, error_out);

	if (status == MH_ERROR_NONE)
		*prediction = next;

	return status;
}
