#include <math.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/core/pose.h"
#include "server/session/vehicle.h"

#define SECONDS_PER_MILLISECOND 0.001
#define KILOMETRES_PER_HOUR 3.6

void
mh_server_vehicle_reset(struct mh_server_vehicle *vehicle)
{
	if (vehicle != NULL)
		memset(vehicle, 0, sizeof(*vehicle));
}

enum mh_error_code
mh_server_vehicle_receive(struct mh_server_vehicle *vehicle,
	const struct mh_net_message *message, double now_ms, int *accepted,
	struct mh_error *error_out)
{
	struct mh_server_vehicle next;
	int received;
	enum mh_error_code status;

	if (vehicle == NULL || message == NULL || accepted == NULL ||
		message->player == 0 ||
		message->player >= MH_NET_WIRE_PLAYERS ||
		message->type != MH_NET_CAR ||
		message->view_player >= MH_NET_WIRE_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated vehicle update");

	next = *vehicle;
	status = mh_net_history_receive(&next.history, message->stamp,
		message->car, MH_NET_VEHICLE_BYTES, now_ms, &received,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (received)
		next.view_player = message->view_player;

	/* Refresh contact even for an ignored timestamp. */
	next.history.received_ms = (unsigned long)now_ms;
	*vehicle = next;
	*accepted = received;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
average_velocity(const struct mh_net_history *history, double velocity[3],
	struct mh_error *error_out)
{
	const struct mh_net_snapshot *newer, *older;
	long double seconds, difference;
	float sum, weight;
	unsigned int axis, i;

	/* Binary32(1/9), following nine binary32 accumulated sums. */
	weight = (float)(1.0 / (MH_NET_HISTORY - 1));

	for (axis = 0; axis < 3; axis++) {
		sum = 0;

		for (i = 0; i + 1 < MH_NET_HISTORY; i++) {
			newer = &history->entries[i];
			older = &history->entries[i + 1];

			if (newer->stamp <= older->stamp)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"unordered dedicated vehicle history");

			seconds = (long double)(newer->stamp - older->stamp) *
				(long double)SECONDS_PER_MILLISECOND;
			difference =
				(long double)newer->vehicle.position[axis] -
				older->vehicle.position[axis];
			sum = (float)(difference / seconds + (long double)sum);
		}

		velocity[axis] = (float)((long double)sum * weight);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned long
forward_speed(const struct mh_server_vehicle *vehicle)
{
	long double forward, speed;
	unsigned long word;

	/* Y product, X product, then Z; one binary32 store. */
	forward = (long double)vehicle->velocity[1] * vehicle->rotation[7] +
		(long double)vehicle->velocity[0] * vehicle->rotation[6];
	forward += (long double)vehicle->velocity[2] * vehicle->rotation[8];
	speed = (long double)fabs((double)(float)forward) *
		(long double)KILOMETRES_PER_HOUR;

	/* Original truncates to a 64-bit temporary, then uses its low word. */
	word = (unsigned long)fmod((double)speed, MH_U32_MODULUS);

	return word;
}

enum mh_error_code
mh_server_vehicle_sample(
	struct mh_server_vehicle *vehicle, struct mh_error *error_out)
{
	struct mh_server_vehicle next;
	const struct mh_net_vehicle *latest;
	unsigned long speed;
	enum mh_error_code status;

	if (vehicle == NULL || vehicle->history.count > MH_NET_HISTORY)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated vehicle history");

	if (vehicle->history.count == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	next = *vehicle;
	latest = &vehicle->history.entries[0].vehicle;
	memcpy(next.position, latest->position, sizeof(next.position));

	if (vehicle->history.count == MH_NET_HISTORY) {
		status = average_velocity(
			&vehicle->history, next.velocity, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_pose_matrix(
			latest->angles, next.rotation, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		speed = forward_speed(&next);

		if (speed > next.top_speed_kmh)
			next.top_speed_kmh = speed;
	}

	*vehicle = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
