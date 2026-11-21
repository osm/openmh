#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/session_internal.h"
#include "engine/network/session_timing.h"
#include "engine/network/snapshot_schedule.h"

#define SESSION_CLOCK_LIMIT 1e12
#define ADMISSION_TIMEOUT_MS 5000
#define CONTACT_TIMEOUT_MS 120000
#define PATCHED_LOBBY_TIMEOUT_MS 60000
#define PATCHED_LOADING_TIMEOUT_MS 120000
#define PATCHED_RACING_TIMEOUT_MS 30000
#define PING_INTERVAL_MS 1000

int
mh_net_session_clock_valid(const struct mh_net_session *session, double now_ms)
{
	return session != NULL && now_ms >= session->now_ms &&
		now_ms <= SESSION_CLOCK_LIMIT;
}

struct mh_net_connection *
mh_net_session_connection(struct mh_net_session *session, unsigned long peer)
{
	unsigned int i;

	for (i = 0; i < MH_NET_CONNECTIONS; i++)
		if (session->connections[i].peer == peer && peer != 0)
			return &session->connections[i];

	return NULL;
}

enum mh_error_code
mh_net_session_remove_player(struct mh_net_session *session, unsigned long peer,
	int announce, struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned int player;
	enum mh_error_code status;

	player = mh_net_session_player_for(session, peer);

	if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
		mh_net_session_in_race(session) && player < MH_NET_PLAYERS) {
		/*
		 * A provider loss removes membership without inventing wire
		 * text.
		 */
		mh_net_session_remove_racer(session, player);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (announce && player < MH_NET_PLAYERS &&
		session->phase == MH_NET_LOBBY) {
		/* Do not queue text to a transport already closing. */
		status = mh_net_session_lobby_message(session,
			session->lobby.slots[player].profile.name,
			" left the lobby.", peer, 1, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_net_lobby_disconnect(&session->lobby, peer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (player == MH_NET_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	session->ready_mask &= ~(1U << player);
	session->return_pending &= ~(1U << player);
	mh_net_session_remove_replica(session, player);

	if (session->phase == MH_NET_CLOSED) {
		mh_net_session_sync_roster(session);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mh_net_session_in_race(session)) {
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_LEAVE_LOBBY;
		message.player = player;
		status = mh_net_session_send_audience(session, &message, peer,
			MH_NET_FLAG_PRESENT, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_net_session_send_roster(session, error_out);
}

static void
discard_peer_output(struct mh_net_session *session, unsigned long peer)
{
	struct mh_net_outgoing pending[MH_NET_OUTBOX];
	unsigned int i, count, index;

	count = 0;

	for (i = 0; i < session->out_count; i++) {
		index = (session->out_head + i) % MH_NET_OUTBOX;

		if (session->outgoing[index].peer != peer)
			pending[count++] = session->outgoing[index];
	}

	memcpy(session->outgoing, pending, count * sizeof(pending[0]));
	session->out_head = 0;
	session->out_count = count;
}

void
mh_net_session_remove_racer(struct mh_net_session *session, unsigned int player)
{
	struct mh_net_lobby_slot *slot;

	/* Clear membership/readiness, retaining status flags and history. */
	session->ready_mask &= ~(1U << player);
	session->roster.players[player].flags = 0;
	session->replication.status[player].flags = 0;
	session->roster_revision++;

	if (!session->host)
		return;

	slot = &session->lobby.slots[player];
	discard_peer_output(session, slot->peer);
	slot->peer = 0;
	slot->reserved_ms = 0;
	slot->phase = MH_NET_EMPTY;
	slot->profile.flags = 0;

	/* The host frees the removed peer's queued transport data. */
	session->replication.pending_status &= ~(1U << player);
	mh_net_batch_reset(&session->replication.pending_service[player]);
}

enum mh_error_code
mh_net_session_connected_from(struct mh_net_session *session,
	unsigned long peer, double now_ms,
	const struct mh_net_challenge *challenge, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_message message;
	enum mh_error_code status;
	unsigned int i;

	if (!mh_net_session_clock_valid(session, now_ms) || peer == 0 ||
		peer > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid session connection");

	next = *session;
	next.now_ms = now_ms;

	if (session->host) {
		if (peer == 1 ||
			(session->phase != MH_NET_LOBBY &&
				!mh_net_session_in_race(session)) ||
			mh_net_session_connection(&next, peer) != NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unexpected host connection");

		for (i = 0; i < MH_NET_CONNECTIONS; i++)
			if (next.connections[i].peer == 0)
				break;

		if (i == MH_NET_CONNECTIONS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"session connection capacity reached");

		next.connections[i].peer = peer;

		if (challenge != NULL)
			next.connections[i].challenge = *challenge;

		next.connections[i].connected_ms =
			next.connections[i].contact_ms = now_ms;
	} else {
		if (peer != 1 || session->phase != MH_NET_CONNECTING)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unexpected server connection");

		memset(&message, 0, sizeof(message));
		message.type = MH_NET_JOIN;
		message.join.provider = session->config.provider;
		message.join.version_bits = session->protocol->version_bits;

		if (challenge != NULL && challenge->active) {
			message.join.has_proof = 1;
			mh_sha256_hmac(session->config.password,
				strlen(session->config.password),
				challenge->nonce, sizeof(challenge->nonce),
				message.join.proof);
		} else
			memcpy(message.join.password, session->config.password,
				sizeof(message.join.password));

		status = mh_net_session_queue(
			&next, 1, &message, 1, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		next.phase = MH_NET_JOINING;
		next.contact_ms = now_ms;
	}

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_connected(struct mh_net_session *session, unsigned long peer,
	double now_ms, struct mh_error *error_out)
{
	return mh_net_session_connected_from(
		session, peer, now_ms, NULL, error_out);
}

enum mh_error_code
mh_net_session_disconnected(struct mh_net_session *session, unsigned long peer,
	double now_ms, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_connection *connection;
	enum mh_error_code status;

	if (!mh_net_session_clock_valid(session, now_ms) || peer == 0 ||
		(!session->host && peer != 1))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid session disconnect");

	next = *session;
	next.now_ms = now_ms;
	discard_peer_output(&next, peer);

	if (session->host) {
		connection = mh_net_session_connection(&next, peer);

		if (connection != NULL)
			memset(connection, 0, sizeof(*connection));

		status =
			mh_net_session_remove_player(&next, peer, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else if (next.phase != MH_NET_REJECTED)
		next.phase = MH_NET_CLOSED;

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
expire_connection(struct mh_net_session *session,
	struct mh_net_connection *connection, int *changed,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned int player;
	enum mh_error_code status;

	status = mh_net_session_queue(
		session, connection->peer, NULL, 1, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	player = mh_net_session_player_for(session, connection->peer);

	if (player < MH_NET_PLAYERS) {
		if (session->phase == MH_NET_LOBBY) {
			/* Timed-out members get a distinct line. */
			status = mh_net_session_lobby_message(session,
				session->lobby.slots[player].profile.name,
				" timed out!", connection->peer, 1, 1,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		session->ready_mask &= ~(1U << player);
		session->return_pending &= ~(1U << player);
		mh_net_session_remove_replica(session, player);

		if (mh_net_session_in_race(session)) {
			memset(&message, 0, sizeof(message));
			message.type = MH_NET_LEAVE_LOBBY;
			message.player = player;
			status = mh_net_session_send_audience(session, &message,
				connection->peer, MH_NET_FLAG_PRESENT,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		*changed = 1;
	}

	mh_net_lobby_disconnect(&session->lobby, connection->peer, NULL);
	connection->closing = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned long
contact_timeout(const struct mh_net_session *session, unsigned int player)
{
	if (session->phase != MH_NET_WAITING && session->phase != MH_NET_RACING)
		return PATCHED_LOBBY_TIMEOUT_MS;

	if (!session->host)
		return session->start.started_ms != 0
			? PATCHED_RACING_TIMEOUT_MS
			: PATCHED_LOADING_TIMEOUT_MS;

	if (!(session->roster.players[player].flags & MH_NET_FLAG_RACING))
		return PATCHED_LOBBY_TIMEOUT_MS;

	return (session->ready_mask & (1U << player))
		? PATCHED_RACING_TIMEOUT_MS
		: PATCHED_LOADING_TIMEOUT_MS;
}

static int
contact_expired(const struct mh_net_session *session, unsigned int player,
	double contact_ms)
{
	unsigned long deadline;

	if (session->config.wire == MH_NET_WIRE_F12)
		return session->now_ms - contact_ms > CONTACT_TIMEOUT_MS;

	/* Lobby and race departure notices. */
	deadline = ((unsigned long)fmod(contact_ms, (double)MH_U32_MODULUS) +
			contact_timeout(session, player)) &
		MH_U32_MAX;

	return session->now_ms > (double)deadline;
}

static enum mh_error_code
expire_racer(struct mh_net_session *session, unsigned int player,
	struct mh_net_connection *connection, struct mh_error *error_out)
{
	int racing;
	enum mh_error_code status;

	racing = session->lobby.slots[player].phase == MH_NET_IN_RACE;
	mh_net_session_remove_racer(session, player);
	connection->closing = 1;
	status = mh_net_session_queue(
		session, connection->peer, NULL, 1, 1, error_out);

	if (status != MH_ERROR_NONE || !racing)
		return status;

	/* Remove first, then notify remaining racers. */

	return mh_net_session_race_message(session,
		session->roster.players[player].name, " timed out!", error_out);
}

static enum mh_error_code
check_race_connections(
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_connection *connection;
	enum mh_net_lobby_phase phase;
	unsigned int pass, player;
	enum mh_error_code status;

	/* Scan racers in slot order, then lobby members. */
	for (pass = 0; pass < 2; pass++) {
		phase = pass == 0 ? MH_NET_IN_RACE : MH_NET_IN_LOBBY;

		for (player = 1; player < MH_NET_PLAYERS; player++) {
			if (session->lobby.slots[player].phase != phase)
				continue;

			connection = mh_net_session_connection(
				session, session->lobby.slots[player].peer);

			if (connection == NULL || connection->closing ||
				!contact_expired(session, player,
					connection->contact_ms))
				continue;

			status = expire_racer(
				session, player, connection, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static double
admission_time(const struct mh_net_session *session,
	const struct mh_net_connection *connection)
{
	double admission_ms;
	unsigned int player;

	admission_ms = connection->connected_ms;

	for (player = 1; player < MH_NET_PLAYERS; player++)
		if (session->lobby.slots[player].peer == connection->peer)
			admission_ms = session->lobby.slots[player].reserved_ms;

	return admission_ms;
}

static enum mh_error_code
check_host_connections(
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_connection *connection;
	enum mh_error_code status;
	double admission_ms;
	unsigned int i, player;
	int changed, patched_race;

	changed = 0;
	patched_race = session->config.wire == MH_NET_WIRE_31_LOBBY &&
		(session->phase == MH_NET_WAITING ||
			session->phase == MH_NET_RACING);

	if (patched_race) {
		status = check_race_connections(session, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (i = 0; i < MH_NET_CONNECTIONS; i++) {
		connection = &session->connections[i];

		if (connection->peer == 0 || connection->closing)
			continue;

		player = mh_net_session_player_for(session, connection->peer);

		if (patched_race && player < MH_NET_PLAYERS)
			continue;

		if (player == MH_NET_PLAYERS) {
			admission_ms = admission_time(session, connection);

			if (session->now_ms - admission_ms <=
				ADMISSION_TIMEOUT_MS)
				continue;
		} else if (!contact_expired(
				session, player, connection->contact_ms)) {
			if (session->config.wire == MH_NET_WIRE_31_LOBBY)
				continue;

			if (session->now_ms - connection->ping_sent_ms <=
				PING_INTERVAL_MS)
				continue;

			status = mh_net_session_ping(
				session, connection, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		status = expire_connection(
			session, connection, &changed, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_net_lobby_expire(&session->lobby, session->now_ms, NULL);

	if (changed)
		return mh_net_session_send_roster(session, error_out);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
expire_server(struct mh_net_session *session, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i;

	if (session->phase == MH_NET_LOBBY) {
		status = mh_net_session_lobby_message(session,
			"Server connection timed out!", "", 0, 1, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
		mh_net_session_in_race(session)) {
		for (i = 0; i < session->roster.player_count; i++)
			mh_net_session_remove_racer(session, i);

		status = mh_net_session_race_message(
			session, "Server connection timed out!", "", error_out);

		if (status != MH_ERROR_NONE)
			return status;

		discard_peer_output(session, 1);
		mh_net_batch_reset(&session->replication.pending_service[0]);
	}

	session->phase = MH_NET_CLOSED;

	return mh_net_session_queue(session, 1, NULL, 1, 1, error_out);
}

enum mh_error_code
mh_net_session_tick(struct mh_net_session *session, double now_ms,
	struct mh_error *error_out)
{
	struct mh_net_session next;
	enum mh_error_code status;

	if (!mh_net_session_clock_valid(session, now_ms))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid session clock");

	next = *session;
	next.now_ms = now_ms;

	/*
	 * Publish the complete tick only after liveness and replication
	 * succeed.
	 */
	if (next.host &&
		(next.phase == MH_NET_LOBBY || mh_net_session_in_race(&next))) {
		status = check_host_connections(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else if (!next.host &&
		(next.phase == MH_NET_JOINING || next.phase == MH_NET_LOBBY ||
			mh_net_session_in_race(&next)) &&
		contact_expired(&next, 0, next.contact_ms)) {
		status = expire_server(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (next.host && next.phase == MH_NET_LOBBY &&
		mh_net_snapshot_due(next.replication.status_ms,
			MH_NET_STATUS_RATE, now_ms) == 1) {
		/* Use one anchor for all lobby pings and status packets. */
		if (next.config.wire == MH_NET_WIRE_31_LOBBY &&
			mh_net_protocol_has(next.protocol,
				MH_NET_PROTOCOL_APPLICATION_PING)) {
			status = mh_net_session_lobby_pings(&next, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		status = mh_net_session_update_status(&next, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = mh_net_session_replicate(&next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_leave(struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_message message;
	enum mh_error_code status;
	unsigned int i, player;
	int send_departure;

	if (session == NULL ||
		(session->phase != MH_NET_LOBBY &&
			!mh_net_session_in_race(session)))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "session is not active");

	next = *session;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_LOBBY;
	message.player = session->player;

	if (session->host) {
		for (i = 0; i < MH_NET_CONNECTIONS; i++) {
			if (next.connections[i].peer == 0 ||
				next.connections[i].closing)
				continue;

			player = mh_net_session_player_for(
				&next, next.connections[i].peer);
			send_departure =
				next.config.wire == MH_NET_WIRE_31_LOBBY &&
				player < MH_NET_PLAYERS &&
				(next.lobby.slots[player].profile.flags &
					(mh_net_session_in_race(&next)
							? MH_NET_FLAG_RACING
							: MH_NET_FLAG_LOBBY));
			status = mh_net_session_queue(&next,
				next.connections[i].peer,
				send_departure ? &message : NULL, 1, 1,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			next.connections[i].closing = 1;
		}
	} else {
		status = mh_net_session_queue(
			&next, 1, &message, 1, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	next.phase = MH_NET_CLOSED;
	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_kick(struct mh_net_session *session, unsigned int player,
	struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_message message;
	struct mh_net_connection *connection;
	unsigned long peer;
	enum mh_error_code status;

	if (session == NULL || !session->host ||
		session->phase != MH_NET_LOBBY || player == 0 ||
		player >= MH_NET_PLAYERS ||
		session->lobby.slots[player].phase != MH_NET_IN_LOBBY)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lobby kick");

	next = *session;
	peer = next.lobby.slots[player].peer;
	connection = mh_net_session_connection(&next, peer);

	if (connection == NULL || connection->closing)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing player connection");

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_KICK;
	status = mh_net_session_queue(&next, peer, &message, 1, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Flush the kick then remove the player without leave chat. */
	connection->closing = 1;
	status = mh_net_session_remove_player(
		&next, peer, next.config.wire == MH_NET_WIRE_F12, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
