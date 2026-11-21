#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/finish.h"
#include "engine/network/session_internal.h"

static enum mh_error_code
start_host_frame(struct mh_net_session *session, long double game_ms,
	struct mh_error *error_out)
{
	struct mh_net_connection *connection;
	struct mh_net_message message;
	unsigned long ping_ms[MH_NET_PLAYERS];
	unsigned int i, active_mask, due_mask;
	enum mh_error_code status;

	memset(ping_ms, 0, sizeof(ping_ms));
	active_mask = 1;

	for (i = 1; i < MH_NET_PLAYERS; i++) {
		if (session->lobby.slots[i].phase != MH_NET_IN_RACE)
			continue;

		active_mask |= 1U << i;
		connection = mh_net_session_connection(
			session, session->lobby.slots[i].peer);

		if (connection != NULL)
			ping_ms[i] = connection->ping_ms;
	}

	status = mh_net_start_step_for(session->config.wire, &session->start,
		active_mask, session->ready_mask, ping_ms, game_ms, &due_mask,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_START;

	for (i = 1; i < MH_NET_PLAYERS; i++)
		if (due_mask & (1U << i)) {
			status = mh_net_session_queue(session,
				session->lobby.slots[i].peer, &message, 1, 0,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

	if (session->start.started_ms != 0)
		session->phase = MH_NET_RACING;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_start_frame(struct mh_net_session *session, long double game_ms,
	struct mh_error *error_out)
{
	struct mh_net_session next;
	enum mh_error_code status;

	if (session == NULL ||
		(session->phase != MH_NET_WAITING &&
			session->phase != MH_NET_RACING) ||
		!(game_ms >= 0 && game_ms < MH_U32_MODULUS &&
			game_ms >= session->start.now_ms))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network start frame");

	next = *session;

	if (next.host) {
		status = start_host_frame(&next, game_ms, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	} else {
		next.start.now_ms = game_ms;

		if (next.start_pending) {
			next.start.started_ms = (unsigned long)game_ms;
			next.start_pending = 0;
			next.phase = MH_NET_RACING;
		}
	}

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
assign_grid(struct mh_net_session *session, struct mh_error *error_out)
{
	unsigned int i, active_mask, positions[MH_NET_PLAYERS];
	enum mh_error_code status;

	active_mask = 0;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (session->lobby.slots[i].phase == MH_NET_IN_LOBBY)
			active_mask |= 1U << i;

	status = mh_net_grid(
		active_mask, &session->grid_seed, positions, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (active_mask & (1U << i)) {
			session->lobby.slots[i].profile.value_a = positions[i];
			session->lobby.slots[i].profile.value_b = 1;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
sync_race_roster(struct mh_net_session *session)
{
	unsigned int i;

	mh_net_session_sync_roster(session);

	if (session->config.wire != MH_NET_WIRE_31_LOBBY)
		return;

	/* Read the cached grid and force lap one, unlike type 6. */
	for (i = 0; i < session->roster.player_count; i++) {
		session->roster.players[i].value_a =
			session->lobby.slots[i].profile.value_a;
		session->roster.players[i].value_b = 1;
	}
}

static int
waiting_for_returners(const struct mh_net_session *session)
{
	unsigned int i;

	if (session->config.wire == MH_NET_WIRE_F12)
		return session->return_pending != 0;

	/* Ignore pending flags on nonmembers and the host. */
	for (i = 1; i < MH_NET_PLAYERS; i++)
		if (session->lobby.slots[i].phase == MH_NET_IN_LOBBY &&
			(session->return_pending & (1U << i)))
			return 1;

	return 0;
}

enum mh_error_code
mh_net_session_begin_race(struct mh_net_session *session, double now_ms,
	struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_message message;
	unsigned int i;
	enum mh_error_code status;

	if (!mh_net_session_clock_valid(session, now_ms) ||
		now_ms > (double)MH_U32_MAX || !session->host ||
		session->phase != MH_NET_LOBBY)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"only a lobby host can begin a race");

	if (waiting_for_returners(session))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"wait for players to return from game");

	for (i = 0; i < MH_NET_CONNECTIONS; i++)
		if (session->config.wire == MH_NET_WIRE_F12 &&
			session->connections[i].peer != 0 &&
			!session->connections[i].closing &&
			mh_net_session_player_for(session,
				session->connections[i].peer) == MH_NET_PLAYERS)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"wait for pending player admission before race setup");

	next = *session;
	next.now_ms = now_ms;
	status = assign_grid(&next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	sync_race_roster(&next);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_RACE_CONFIG;
	message.roster = next.roster;
	status = mh_net_session_send_audience(&next, &message, 0,
		next.config.wire == MH_NET_WIRE_31_LOBBY ? MH_NET_FLAG_LOBBY
							: MH_NET_FLAG_PRESENT,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (next.lobby.slots[i].phase == MH_NET_IN_LOBBY) {
			next.lobby.slots[i].phase = MH_NET_IN_RACE;
			next.lobby.slots[i].profile.flags =
				(next.lobby.slots[i].profile.flags &
					MH_NET_FLAG_RECORDING) |
				MH_NET_FLAG_RACING;
			next.lobby.slots[i].profile.value_b = 1;
		}

	for (i = 0; i < MH_NET_CONNECTIONS; i++) {
		next.connections[i].ping_pending = 0;

		/* Change membership without refreshing contact time. */
		if (next.config.wire == MH_NET_WIRE_F12)
			next.connections[i].contact_ms = now_ms;
	}

	sync_race_roster(&next);
	mh_net_start_reset(&next.start);
	next.start_pending = 0;
	next.ready_mask = 0;
	mh_net_session_reset_replication(&next);
	next.phase = MH_NET_LOADING;
	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_loaded(struct mh_net_session *session, double now_ms,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	enum mh_error_code status;

	if (!mh_net_session_clock_valid(session, now_ms) ||
		now_ms > (double)MH_U32_MAX || session->phase != MH_NET_LOADING)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"session is not loading a race");

	if (mh_net_protocol_has(
		session->protocol, MH_NET_PROTOCOL_LOADED_ACK) &&
		!session->host) {
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_LOADED;
		message.player = session->player;
		status = mh_net_session_queue(
			session, 1, &message, 1, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	session->now_ms = now_ms;
	session->phase = MH_NET_WAITING;

	/* Reset readiness, then mark only the host loaded. */
	if (session->config.wire == MH_NET_WIRE_31_LOBBY)
		session->ready_mask = session->host ? 1U << session->player : 0;
	else
		session->ready_mask |= 1U << session->player;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
return_patched_lobby(struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_message message;
	struct mh_net_lobby_slot *slot;
	unsigned int i;
	enum mh_error_code status;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_RACE;
	message.player = session->player;

	/* Only announce a local player still marked as racing. */
	if (session->roster.players[session->player].flags &
		MH_NET_FLAG_RACING) {
		if (session->host)
			status = mh_net_session_send_audience(session, &message,
				0, MH_NET_FLAG_RACING, error_out);
		else
			status = mh_net_session_queue(
				session, 1, &message, 1, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (session->host) {
		/* Distinguish prior lobby members from returning racers. */
		for (i = 0; i < MH_NET_PLAYERS; i++) {
			slot = &session->lobby.slots[i];

			if (slot->phase == MH_NET_IN_LOBBY)
				session->return_pending &= ~(1U << i);
			else if (slot->phase == MH_NET_IN_RACE) {
				slot->phase = MH_NET_IN_LOBBY;
				slot->profile.flags =
					(slot->profile.flags &
						MH_NET_FLAG_RECORDING) |
					MH_NET_FLAG_LOBBY;

				if (i != session->player)
					session->return_pending |= 1U << i;
			}
		}

		mh_net_session_sync_roster(session);
	} else {
		session->roster.players[session->player].flags =
			(session->roster.players[session->player].flags &
				MH_NET_FLAG_RECORDING) |
			MH_NET_FLAG_LOBBY;
		session->roster_revision++;
	}

	mh_net_session_reset_replication(session);
	mh_net_start_reset(&session->start);
	session->start_pending = 0;
	session->ready_mask = 0;
	session->phase = MH_NET_LOBBY;

	/* Restart lobby service at the return clock. */
	session->replication.status_ms = (unsigned long)session->now_ms;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_return_lobby(
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_message message;
	unsigned int i;
	enum mh_error_code status;

	if (session == NULL || !mh_net_session_in_race(session) ||
		session->player >= mh_net_session_capacity(session))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "session is not in a race");

	next = *session;

	if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
		status = return_patched_lobby(&next, error_out);

		if (status == MH_ERROR_NONE)
			*session = next;

		return status;
	}

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_RACE;
	message.player = session->player;
	mh_net_session_reset_replication(&next);

	if (!session->host) {
		status = mh_net_session_queue(
			&next, 1, &message, 1, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		next.roster.players[session->player].flags =
			(next.roster.players[session->player].flags &
				MH_NET_FLAG_RECORDING) |
			MH_NET_FLAG_LOBBY;
		next.phase = MH_NET_LOBBY;
		next.ready_mask = 0;
		mh_net_start_reset(&next.start);
		next.start_pending = 0;
		next.roster_revision++;
		*session = next;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_net_session_send_audience(
		&next, &message, 0, MH_NET_FLAG_RACING, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (next.lobby.slots[i].phase == MH_NET_IN_RACE) {
			if (i != 0)
				next.return_pending |= 1U << i;

			next.lobby.slots[i].phase = MH_NET_IN_LOBBY;
			next.lobby.slots[i].profile.flags =
				(next.lobby.slots[i].profile.flags &
					MH_NET_FLAG_RECORDING) |
				MH_NET_FLAG_LOBBY;
		}

	for (i = 0; i < MH_NET_CONNECTIONS; i++) {
		next.connections[i].ping_pending =
			next.connections[i].race_ping = 0;
		next.connections[i].ping_sent_ms = 0;
	}

	next.phase = MH_NET_LOBBY;
	next.ready_mask = 0;
	mh_net_start_reset(&next.start);
	next.start_pending = 0;
	mh_net_session_reset_replication(&next);
	status = mh_net_session_send_roster(&next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_finish_race(struct mh_net_session *session,
	const unsigned int positions[MH_NET_PLAYERS],
	struct mh_error *error_out)
{
	struct mh_net_session next;
	unsigned int i, racer_count, position_mask;
	enum mh_error_code status;

	if (session == NULL || positions == NULL || !session->host ||
		session->player != 0 || session->phase != MH_NET_RACING ||
		session->lobby.slots[0].phase != MH_NET_IN_RACE ||
		session->roster.mode > MH_NET_MODE_ELIMINATION)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race completion");

	racer_count = position_mask = 0;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (session->lobby.slots[i].phase == MH_NET_IN_RACE)
			racer_count++;

	if (racer_count == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "empty completed race");

	next = *session;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (next.lobby.slots[i].phase == MH_NET_IN_RACE) {
			if (positions[i] < 1 || positions[i] > racer_count ||
				(position_mask & (1U << positions[i])))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid completed race positions");

			position_mask |= 1U << positions[i];
			status = mh_net_finish_score(
				&next.lobby.slots[i].profile, positions[i],
				error_out);

			if (status != MH_ERROR_NONE)
				return status;

			if (next.config.wire == MH_NET_WIRE_31_LOBBY)
				next.progress[i].position = positions[i];
		}

	status = mh_net_session_return_lobby(&next, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next.config.profile.values[MH_NET_PROFILE_RACES] =
		next.roster.players[next.player].values[MH_NET_PROFILE_RACES];
	next.config.profile.values[MH_NET_PROFILE_POINTS] =
		next.roster.players[next.player].values[MH_NET_PROFILE_POINTS];
	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
