#include <math.h>
#include <string.h>

#include "engine/core/limits.h"
#include "engine/network/session_internal.h"
#include "engine/network/session_timing.h"

enum mh_error_code
mh_net_session_ping(struct mh_net_session *session,
	struct mh_net_connection *connection, struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned int player;
	enum mh_error_code status;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PING;
	message.stamp =
		(unsigned long)fmod(session->now_ms, (double)MH_U32_MODULUS);
	status = mh_net_session_queue(
		session, connection->peer, &message, 0, 0, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	player = mh_net_session_player_for(session, connection->peer);
	connection->ping_stamp = message.stamp;
	connection->ping_sent_ms = session->now_ms;
	connection->ping_pending = 1;
	connection->race_ping =
		session->lobby.slots[player].phase == MH_NET_IN_RACE;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_lobby_pings(
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_connection *connection;
	enum mh_error_code status;
	unsigned int i, player;

	for (i = 0; i < MH_NET_CONNECTIONS; i++) {
		connection = &session->connections[i];

		if (connection->peer == 0 || connection->closing)
			continue;

		player = mh_net_session_player_for(session, connection->peer);

		if (player == MH_NET_PLAYERS ||
			!(session->lobby.slots[player].profile.flags &
				MH_NET_FLAG_LOBBY))
			continue;

		status = mh_net_session_ping(session, connection, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned long
round_trip_ms(double now_ms, unsigned long stamp)
{
	double elapsed;

	/* Truncate the signed difference to its low word. */
	elapsed = now_ms - (double)stamp;
	elapsed = elapsed < 0 ? ceil(elapsed) : floor(elapsed);

	return (unsigned long)fmod(
		elapsed + (double)MH_U32_MODULUS, (double)MH_U32_MODULUS);
}

enum mh_error_code
mh_net_session_pong(struct mh_net_session *session,
	struct mh_net_connection *connection, unsigned int player,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	enum mh_error_code status;

	/*
	 * An unreliable race pong may arrive after reliable
	 * return-to-lobby.
	 */
	if (message->type == MH_NET_PONG && !mh_net_session_in_race(session))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
		if (message->type == MH_NET_LOBBY_PONG) {
			/* Refresh the roster before clearing status flags. */
			if (session->return_pending & (1U << player)) {
				status = mh_net_session_send_roster(
					session, error_out);

				if (status != MH_ERROR_NONE)
					return status;
			}

			session->return_pending &= ~(1U << player);
		}

		connection->ping_ms =
			round_trip_ms(session->now_ms, message->stamp);
		session->replication.status[player].ping_ms =
			connection->ping_ms;
		connection->contact_ms = floor(session->now_ms);
		connection->ping_pending = 0;

		/* Any race pong marks a racing player ready. */
		if (message->type == MH_NET_PONG &&
			session->lobby.slots[player].phase == MH_NET_IN_RACE)
			session->ready_mask |= 1U << player;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (connection->ping_pending &&
		message->stamp == connection->ping_stamp) {
		connection->ping_ms = (unsigned long)(session->now_ms -
			connection->ping_sent_ms);
		connection->ping_pending = 0;

		if (message->type == MH_NET_LOBBY_PONG &&
			!connection->race_ping)
			session->return_pending &= ~(1U << player);

		if (message->type == MH_NET_PONG && connection->race_ping &&
			session->lobby.slots[player].phase == MH_NET_IN_RACE)
			session->ready_mask |= 1U << player;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_contact(struct mh_net_session *session, unsigned long peer,
	double now_ms, unsigned long rtt_ms, int sampled,
	struct mh_error *error_out)
{
	struct mh_net_connection *connection;
	unsigned int player;
	enum mh_error_code status;

	if (!mh_net_session_clock_valid(session, now_ms) ||
		!mh_net_protocol_has(
			session->protocol, MH_NET_PROTOCOL_TIMED_CHANNEL))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid OpenMH contact");

	if (!session->host) {
		if (peer != 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unexpected server contact");

		session->contact_ms = now_ms;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	connection = mh_net_session_connection(session, peer);

	if (connection == NULL || connection->closing)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	connection->contact_ms = floor(now_ms);

	if (sampled)
		connection->ping_ms = rtt_ms;

	player = mh_net_session_player_for(session, peer);

	if (player < MH_NET_PLAYERS) {
		if (sampled)
			session->replication.status[player].ping_ms = rtt_ms;

		if (session->return_pending & (1U << player)) {
			status = mh_net_session_send_roster(session, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			session->return_pending &= ~(1U << player);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
