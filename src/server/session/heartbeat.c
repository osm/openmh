#include <math.h>
#include <string.h>

#include "engine/core/limits.h"
#include "server/session/lobby.h"

#define CLOCK_LIMIT 1e12

enum {
	HEARTBEAT_MS = 1000,
	MILLISECONDS_PER_SECOND = 1000
};

static unsigned long
clock_word(double value)
{
	if (value < 0)
		return (0UL - (unsigned long)fmod(-value, MH_U32_MODULUS)) &
			MH_U32_MAX;

	return (unsigned long)fmod(value, MH_U32_MODULUS);
}

static unsigned int
expired_players(const struct mh_server_lobby *lobby,
	unsigned long timeout_seconds, double now_ms)
{
	unsigned int i, expired;
	double timeout;

	timeout = (double)timeout_seconds * MILLISECONDS_PER_SECOND;
	expired = 0;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(lobby->roster.players[i].flags & MH_NET_FLAG_LOBBY))
			continue;

		if (now_ms - lobby->contact_ms[i] > timeout)
			expired |= 1U << i;
	}

	return expired;
}

enum mh_error_code
mh_server_lobby_tick(struct mh_server_lobby *lobby,
	unsigned long timeout_seconds, double now_ms,
	struct mh_server_lobby_tick *output, struct mh_error *error_out)
{
	struct mh_server_lobby_tick tick;

	if (lobby == NULL || output == NULL || timeout_seconds > MH_U32_MAX ||
		!(now_ms >= 0) || now_ms > CLOCK_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated lobby tick");

	memset(&tick, 0, sizeof(tick));
	tick.expired = expired_players(lobby, timeout_seconds, now_ms);

	if (now_ms - lobby->heartbeat_ms > HEARTBEAT_MS) {
		tick.heartbeat = 1;
		tick.stamp = clock_word(now_ms);
		lobby->heartbeat_ms = floor(now_ms);
	}

	*output = tick;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_pong(struct mh_server_lobby *lobby, unsigned long peer,
	const struct mh_net_message *message, unsigned int type, double now_ms,
	struct mh_error *error_out)
{
	unsigned int player;

	if (lobby == NULL || message == NULL || message->type != type ||
		message->stamp > MH_U32_MAX || !(now_ms >= 0) ||
		now_ms > CLOCK_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated lobby pong");

	player = message->player;

	if (player == 0 || player >= MH_NET_WIRE_PLAYERS || peer == 0 ||
		lobby->peers[player] != peer ||
		!(lobby->roster.players[player].flags & MH_NET_FLAG_PRESENT))
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"dedicated pong does not belong to sender");

	/* Retain the low word of truncating conversion. */
	lobby->ping_ms[player] = clock_word(now_ms - (double)message->stamp);
	lobby->contact_ms[player] = floor(now_ms);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_lobby_pong(struct mh_server_lobby *lobby, unsigned long peer,
	const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out)
{
	return receive_pong(
		lobby, peer, message, MH_NET_LOBBY_PONG, now_ms, error_out);
}

enum mh_error_code
mh_server_race_pong(struct mh_server_lobby *lobby, unsigned long peer,
	const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = receive_pong(
		lobby, peer, message, MH_NET_PONG, now_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* A race pong marks only a current racer loaded. */
	if (lobby->roster.players[message->player].flags & MH_NET_FLAG_RACING)
		lobby->roster.players[message->player].flags |=
			MH_NET_FLAG_READY;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_server_lobby_status(
	const struct mh_server_lobby *lobby, struct mh_net_message *output)
{
	const struct mh_net_profile *profile;
	unsigned int i;

	memset(output, 0, sizeof(*output));
	output->type = MH_NET_STATUS;

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++) {
		profile = &lobby->roster.players[i];
		output->status[i].flags = profile->flags &
			(MH_NET_FLAG_PRESENT | MH_NET_FLAG_RECORDING |
				MH_NET_FLAG_READY | MH_NET_FLAG_PASS);
		output->status[i].ping_ms = lobby->ping_ms[i];
		output->status[i].value_a = profile->value_a;
		output->status[i].value_b = profile->value_b;
	}

	output->status[0].flags &= ~MH_NET_FLAG_PRESENT;
}
