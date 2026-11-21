#include <math.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/driver.h"
#include "engine/network/session_internal.h"
#include "engine/network/session_timing.h"

#define RACE_NOTICE_COLOR 0xe06820UL
#define LOBBY_STATE_FLAGS                                                      \
	(MH_NET_PROFILE_FLAGS | MH_NET_FLAG_READY | MH_NET_FLAG_PASS)

static int
setup_driver(const struct mh_net_profile *profile, int dedicated)
{
	return (profile->flags & MH_NET_FLAG_LOBBY) &&
		(!dedicated || (profile->flags & MH_NET_FLAG_READY));
}

static int
race_config_valid(const struct mh_net_roster *roster, int dedicated)
{
	unsigned int player, driver_count, position_mask, position;

	if (!mh_net_session_game_valid(
		roster, dedicated ? MH_NET_WIRE_PLAYERS : MH_NET_PLAYERS))
		return 0;

	driver_count = position_mask = 0;

	for (player = 0; player < roster->player_count; player++) {
		if (!(roster->players[player].flags & MH_NET_FLAG_PRESENT))
			continue;

		if ((roster->players[player].flags & MH_NET_FLAG_PRESENT) !=
			MH_NET_FLAG_LOBBY)
			return 0;

		if (setup_driver(&roster->players[player], dedicated))
			driver_count++;
	}

	for (player = 0; player < roster->player_count; player++)
		if (setup_driver(&roster->players[player], dedicated)) {
			position = roster->players[player].value_a;

			if (position == 0 || position > driver_count ||
				(position_mask & (1U << position)))
				return 0;

			position_mask |= 1U << position;
		}

	return driver_count != 0 &&
		(!dedicated || roster->players[0].value_a == driver_count);
}

static void
store_ready_flag(
	struct mh_net_session *session, unsigned int player, unsigned int flags)
{
	session->ready_mask &= ~(1U << player);

	if (flags & MH_NET_FLAG_READY)
		session->ready_mask |= 1U << player;
}

static void
store_profile_status(struct mh_net_session *session, unsigned int player,
	const struct mh_net_profile *profile)
{
	struct mh_net_status *status;

	status = &session->replication.status[player];

	if (session->config.wire == MH_NET_WIRE_F12 &&
		(player >= session->roster.player_count ||
			!(session->roster.players[player].flags &
				MH_NET_FLAG_PRESENT)))
		status->ping_ms = 0;

	/*
	 * Patched profile/roster copies leave ping and unavailable untouched.
	 */
	status->flags = session->config.wire == MH_NET_WIRE_31_LOBBY
		? (profile->flags & LOBBY_STATE_FLAGS) |
			(status->flags & MH_NET_FLAG_UNAVAILABLE)
		: profile->flags;

	status->value_a = profile->value_a;
	status->value_b = profile->value_b;

	if (!session->host && session->config.wire == MH_NET_WIRE_31_LOBBY)
		store_ready_flag(session, player, profile->flags);

	if (session->host && session->config.wire == MH_NET_WIRE_31_LOBBY) {
		status->value_a = session->progress[player].position;
		status->value_b = session->progress[player].lap;
	}

	session->replication.status_valid |= 1U << player;
}

static enum mh_error_code
receive_host_profile(struct mh_net_session *session, unsigned long peer,
	struct mh_net_message *message, struct mh_error *error_out)
{
	struct mh_net_connection *connection;
	enum mh_error_code status;

	if (mh_net_session_in_race(session) &&
		message->type == MH_NET_PROFILE_UPDATE) {
		if (mh_net_session_player_for(session, peer) != message->player)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid profile after race setup");

		/* Reliable update sent before receiving frozen setup. */
		if (session->lobby.slots[message->player].phase ==
			MH_NET_IN_RACE)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mh_net_session_in_race(session))
		status = mh_net_lobby_race_profile(&session->lobby, peer,
			message, session->now_ms, error_out);
	else
		status = mh_net_lobby_profile(&session->lobby, peer, message,
			session->now_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (session->cars.count != 0) {
		status = mh_net_car_admit(&session->cars,
			message->type == MH_NET_PROFILE_JOIN
				? session->config.car_count
				: session->cars.count,
			session->lobby.slots[message->player].profile.car,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
		message->type == MH_NET_PROFILE_UPDATE) {
		/*
		 * Apply the profile, then forward type 5 except to the sender.
		 */
		message->profile =
			session->lobby.slots[message->player].profile;
		store_profile_status(
			session, message->player, &message->profile);
		mh_net_session_sync_roster(session);
		return mh_net_session_send_audience(
			session, message, peer, MH_NET_FLAG_LOBBY, error_out);
	}

	if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
		message->type == MH_NET_PROFILE_JOIN) {
		connection = mh_net_session_connection(session, peer);
		connection->contact_ms = floor(session->now_ms);
		connection->ping_ms =
			session->replication.status[message->player].ping_ms;
	}

	if (message->type == MH_NET_PROFILE_JOIN &&
		!mh_net_session_in_race(session)) {
		status = mh_net_session_lobby_message(session,
			message->profile.name, " entered the lobby.", 0, 1, 1,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (message->type == MH_NET_PROFILE_JOIN &&
		mh_net_session_in_race(session)) {
		/* Publish the roster BEFORE the lobby text. */
		status = mh_net_session_send_roster(session, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		return mh_net_session_lobby_message(session,
			message->profile.name, " entered the lobby.", 0, 0, 1,
			error_out);
	}

	return mh_net_session_send_roster(session, error_out);
}

static enum mh_error_code
receive_race_config(struct mh_net_session *session,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	unsigned int i;
	int dedicated;

	dedicated = session->config.wire == MH_NET_WIRE_31_LOBBY &&
		!(message->roster.players[0].flags & MH_NET_FLAG_PRESENT);

	if (session->phase != MH_NET_LOBBY ||
		session->now_ms > (double)MH_U32_MAX ||
		session->player >= message->roster.player_count ||
		!race_config_valid(&message->roster, dedicated) ||
		(!dedicated &&
			!(message->roster.players[0].flags &
				MH_NET_FLAG_LOBBY)) ||
		!setup_driver(
			&message->roster.players[session->player], dedicated))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race configuration for client phase");

	session->roster = message->roster;

	/*
	 * Frozen-field normalization: select ready drivers, although type 9
	 * also retains passing lobby profiles. Keep those profiles out of the
	 * loaded car field; server status owns membership.
	 */
	for (i = 0; i < session->roster.player_count; i++)
		if (setup_driver(&session->roster.players[i], dedicated))
			session->roster.players[i].flags =
				(session->roster.players[i].flags &
					MH_NET_FLAG_RECORDING) |
				MH_NET_FLAG_RACING;

	mh_net_start_reset(&session->start);
	session->start_pending = 0;
	session->ready_mask = 0;
	mh_net_session_reset_replication(session);
	session->phase = MH_NET_LOADING;
	session->roster_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_roster(struct mh_net_session *session,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	const struct mh_net_profile *local_profile;
	unsigned int i;

	if ((session->phase != MH_NET_JOINING &&
		session->phase != MH_NET_LOBBY) ||
		session->player >= message->roster.player_count ||
		!mh_net_session_game_valid(
			&message->roster, mh_net_session_capacity(session)) ||
		(session->config.wire == MH_NET_WIRE_F12 &&
			!(message->roster.players[0].flags &
				MH_NET_FLAG_PRESENT)) ||
		!(message->roster.players[session->player].flags &
			MH_NET_FLAG_LOBBY))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid roster for client phase");

	for (i = 0; i < mh_net_session_capacity(session); i++) {
		if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
			/* Copy profiles without resetting snapshot history. */
			if (i >= message->roster.player_count) {
				session->replication.status_valid &= ~(1U << i);
				session->ready_mask &= ~(1U << i);
				continue;
			}

			store_profile_status(
				session, i, &message->roster.players[i]);

			continue;
		}

		if (i >= message->roster.player_count ||
			!(message->roster.players[i].flags &
				MH_NET_FLAG_PRESENT)) {
			mh_net_session_remove_replica(session, i);
			continue;
		}

		store_profile_status(session, i, &message->roster.players[i]);
	}

	session->roster = message->roster;
	local_profile = &session->roster.players[session->player];

	/*
	 * The first roster carries the host's admission fallback. Later
	 * rosters may echo older edits and must not reset our local
	 * choice.
	 */
	if (session->phase == MH_NET_JOINING)
		memcpy(session->config.profile.car, local_profile->car,
			MH_NET_NAME_BYTES);

	session->config.profile.values[MH_NET_PROFILE_RACES] =
		local_profile->values[MH_NET_PROFILE_RACES];
	session->config.profile.values[MH_NET_PROFILE_POINTS] =
		local_profile->values[MH_NET_PROFILE_POINTS];
	session->phase = MH_NET_LOBBY;
	session->roster_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_lobby_status(struct mh_net_session *session,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	struct mh_net_status *status;
	unsigned int i, flags, changed;

	/* Update every configured slot, including absent ones. */
	changed = 0;

	for (i = 0; i < session->roster.player_count; i++) {
		status = &session->replication.status[i];
		flags = message->status[i].flags & LOBBY_STATE_FLAGS;
		changed |= session->roster.players[i].flags != flags;
		session->roster.players[i].flags = flags;
		flags |= status->flags & MH_NET_FLAG_UNAVAILABLE;
		*status = message->status[i];
		status->flags = flags;
		session->replication.status_valid |= 1U << i;
		store_ready_flag(session, i, flags);
	}

	session->replication.status_revision++;

	if (changed)
		session->roster_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_status(struct mh_net_session *session,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	unsigned int i, membership, flags, changed;

	if (session->phase != MH_NET_LOBBY &&
		(!mh_net_session_in_race(session) ||
			!session->replication.configured))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (session->phase == MH_NET_LOBBY &&
		session->config.wire == MH_NET_WIRE_31_LOBBY)
		return receive_lobby_status(session, message, error_out);

	membership = session->phase == MH_NET_LOBBY ? MH_NET_FLAG_PRESENT
						: MH_NET_FLAG_RACING;
	changed = 0;

	/*
	 * F1.2/lobby status remains a presentation cache. Patched race status
	 * also carries membership; remote returns are not relayed as type 18.
	 */
	for (i = 0; i < mh_net_session_capacity(session); i++) {
		if (message->status[i].flags > MH_NET_STATUS_FLAGS)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid race status flags");

		/* Race membership is host-authoritative. */
		if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
			session->phase != MH_NET_LOBBY &&
			i < session->roster.player_count) {
			flags = message->status[i].flags & MH_NET_PROFILE_FLAGS;
			changed |= session->roster.players[i].flags != flags;
			session->roster.players[i].flags = flags;
			session->replication.status[i] = message->status[i];
			session->replication.status_valid |= 1U << i;
			continue;
		}

		if (i < session->roster.player_count &&
			(session->roster.players[i].flags & membership)) {
			session->replication.status[i] = message->status[i];
			session->replication.status_valid |= 1U << i;
		}
	}

	session->replication.status_revision++;

	if (changed)
		session->roster_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_race_departure(struct mh_net_session *session, unsigned int player,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	struct mh_net_connection *connection;
	unsigned long peer;
	enum mh_error_code status;

	if (player >= session->roster.player_count)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race departure");

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CHAT;
	message.player = session->player;
	message.color = RACE_NOTICE_COLOR;
	strcpy(message.text, session->roster.players[player].name);
	strcat(message.text, " left the lobby.");
	peer = session->host ? session->lobby.slots[player].peer : 1;
	mh_net_session_remove_racer(session, player);

	if (session->host) {
		connection = mh_net_session_connection(session, peer);

		if (connection != NULL)
			connection->closing = 1;

		status = mh_net_session_queue(
			session, peer, NULL, 1, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	/* Call lobby chat, without a local display or roster send. */

	return session->host
		? mh_net_session_send_audience(
			session, &message, 0, MH_NET_FLAG_LOBBY, error_out)
		: mh_net_session_queue(session, 1, &message, 1, 0, error_out);
}

static enum mh_error_code
receive_race_return(struct mh_net_session *session,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	unsigned int i;
	int dedicated;

	/* Crossed local and host returns. */
	if (session->phase == MH_NET_LOBBY)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (!mh_net_session_in_race(session) ||
		message->player >= session->roster.player_count ||
		message->player == session->player)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid race departure");

	if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
		message->player != 0)
		return mh_net_session_race_message(session,
			session->roster.players[message->player].name,
			" left the race.", error_out);

	dedicated = session->config.wire == MH_NET_WIRE_31_LOBBY &&
		!(session->roster.players[0].flags & MH_NET_FLAG_PRESENT);

	if (message->player == 0) {
		for (i = 0; i < session->roster.player_count; i++) {
			/*
			 * Preserve the dedicated console and departed slots
			 * until refresh.
			 */
			if (dedicated &&
				!(session->roster.players[i].flags &
					MH_NET_FLAG_PRESENT))
				continue;

			if (session->config.wire == MH_NET_WIRE_31_LOBBY ||
				(session->roster.players[i].flags &
					MH_NET_FLAG_RACING))
				session->roster.players[i].flags =
					(session->roster.players[i].flags &
						MH_NET_FLAG_RECORDING) |
					MH_NET_FLAG_LOBBY;
		}

		session->phase = MH_NET_LOBBY;
		session->ready_mask = 0;
		mh_net_start_reset(&session->start);
		session->start_pending = 0;
		mh_net_session_reset_replication(session);

		if (session->config.wire == MH_NET_WIRE_31_LOBBY)
			session->replication.status_ms =
				(unsigned long)session->now_ms;

		session->roster_revision++;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	mh_net_session_remove_replica(session, message->player);

	if (session->roster.players[message->player].flags & MH_NET_FLAG_RACING)
		session->roster.players[message->player].flags =
			(session->roster.players[message->player].flags &
				MH_NET_FLAG_RECORDING) |
			MH_NET_FLAG_LOBBY;

	session->roster_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_snapshot(struct mh_net_session *session,
	const struct mh_net_message *message, int *accepted,
	struct mh_error *error_out)
{
	struct mh_net_history *history;
	enum mh_error_code status;

	history = &session->replication.history[message->player];
	status = mh_net_history_receive(history, message->stamp, message->car,
		message->type == MH_NET_CAR ? MH_NET_VEHICLE_BYTES
					: MH_NET_VEHICLE_FAR_BYTES,
		session->now_ms, accepted, error_out);

	/* Refresh receipt time even for old or duplicate stamps. */
	if (status == MH_ERROR_NONE &&
		session->config.wire == MH_NET_WIRE_31_LOBBY)
		history->received_ms = floor(session->now_ms);

	return status;
}

static enum mh_error_code
receive_host_message(struct mh_net_session *session, unsigned long peer,
	struct mh_net_message *message, struct mh_error *error_out)
{
	struct mh_net_message reply;
	struct mh_net_connection *connection;
	enum mh_error_code status;
	unsigned int player;

	connection = mh_net_session_connection(session, peer);

	if (connection == NULL || connection->closing)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "unbound session sender");

	if (session->config.wire == MH_NET_WIRE_F12)
		connection->contact_ms = session->now_ms;

	if (message->type == MH_NET_JOIN) {
		if (session->phase != MH_NET_LOBBY &&
			!mh_net_session_in_race(session))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"session admission is not available");

		message->join.challenge = connection->challenge;
		status = mh_net_lobby_admit(&session->lobby, peer, message,
			session->now_ms, &reply, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		connection->closing = reply.type == MH_NET_REJECT;

		return mh_net_session_queue(session, peer, &reply, 1,
			connection->closing, error_out);
	}

	if (message->type == MH_NET_PROFILE_JOIN ||
		message->type == MH_NET_PROFILE_UPDATE)
		return receive_host_profile(session, peer, message, error_out);

	player = mh_net_session_player_for(session, peer);

	if (player == MH_NET_PLAYERS || message->player != player)
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"session player does not belong to sender");

	if (message->type == MH_NET_CAR) {
		int accepted;

		/* Phase-crossing unreliable traffic. */
		if (!mh_net_session_in_race(session) ||
			!session->replication.configured ||
			session->lobby.slots[player].phase != MH_NET_IN_RACE)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (message->view_player >= session->roster.player_count)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid client viewed player");

		status = receive_snapshot(
			session, message, &accepted, error_out);

		if (status == MH_ERROR_NONE && accepted) {
			if (session->roster.players[message->view_player]
					.flags &
				MH_NET_FLAG_RACING)
				session->replication.view[player] =
					message->view_player;
			else
				session->replication.view[player] = player;
		}

		return status;
	}

	if (message->type == MH_NET_CHAT) {
		char text[MH_NET_CHAT_BYTES];
		unsigned int flags;
		size_t capacity;

		capacity = session->config.wire == MH_NET_WIRE_F12
			? MH_NET_PACKET_LIMIT - MH_NET_CHAT_OVERHEAD + 1
			: sizeof(text);
		status = mh_net_driver_chat(
			session->lobby.slots[player].profile.name,
			message->text, text, capacity, NULL, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		strcpy(message->text, text);

		flags = session->lobby.slots[player].profile.flags &
			MH_NET_FLAG_PRESENT;

		if (session->lobby.slots[0].profile.flags & flags)
			mh_net_session_add_chat(session, message);

		return mh_net_session_send_audience(
			session, message, peer, flags, error_out);
	}

	if (!mh_net_protocol_has(
		session->protocol, MH_NET_PROTOCOL_APPLICATION_PING) &&
		mh_net_protocol_ping_message(message->type))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"legacy ping in OpenMH session");

	if (mh_net_protocol_has(
		session->protocol, MH_NET_PROTOCOL_LOADED_ACK) &&
		message->type == MH_NET_LOADED) {
		if (mh_net_session_in_race(session) &&
			session->lobby.slots[player].phase == MH_NET_IN_RACE)
			session->ready_mask |= 1U << player;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (message->type == MH_NET_LOBBY_PONG || message->type == MH_NET_PONG)
		return mh_net_session_pong(
			session, connection, player, message, error_out);

	if (message->type == MH_NET_LEAVE_RACE) {
		if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
			if (!mh_net_session_in_race(session))
				return mh_error_set(
					error_out, MH_ERROR_NONE, NULL);

			/* Announce before changing membership. */
			status = mh_net_session_race_message(session,
				session->roster.players[player].name,
				" left the race.", error_out);

			if (status != MH_ERROR_NONE)
				return status;

			session->lobby.slots[player].phase = MH_NET_IN_LOBBY;
			session->lobby.slots[player].profile.flags =
				(session->lobby.slots[player].profile.flags &
					MH_NET_FLAG_RECORDING) |
				MH_NET_FLAG_LOBBY;

			return mh_net_session_send_roster(session, error_out);
		}

		/*
		 * A return can cross the host's end-of-round broadcast. Already
		 * in the lobby is harmless; it must not become a disconnection.
		 */
		if (session->lobby.slots[player].phase == MH_NET_IN_LOBBY)
			return mh_net_session_send_roster(session, error_out);

		if (!mh_net_session_in_race(session))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"race departure outside race");

		session->lobby.slots[player].phase = MH_NET_IN_LOBBY;
		session->lobby.slots[player].profile.flags =
			(session->lobby.slots[player].profile.flags &
				MH_NET_FLAG_RECORDING) |
			MH_NET_FLAG_LOBBY;
		session->ready_mask &= ~(1U << player);
		mh_net_session_remove_replica(session, player);
		connection->ping_pending = connection->race_ping = 0;
		status = mh_net_session_send_audience(
			session, message, peer, MH_NET_FLAG_RACING, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		return mh_net_session_send_roster(session, error_out);
	}

	if (message->type == MH_NET_LEAVE_LOBBY) {
		if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
			mh_net_session_in_race(session))
			return receive_race_departure(
				session, player, error_out);

		status = mh_net_session_remove_player(
			session, peer, 1, error_out);
		connection->closing = 1;

		if (status != MH_ERROR_NONE)
			return status;

		return mh_net_session_queue(
			session, peer, NULL, 1, 1, error_out);
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "unexpected host lobby message");
}

static enum mh_error_code
close_client_lobby(
	struct mh_net_session *session, int kicked, struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned int i;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LEAVE_LOBBY;
	message.player = session->player;
	session->phase = MH_NET_CLOSED;

	if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
		/* Clear membership, retaining profile text and scores. */
		for (i = 0; i < session->roster.player_count; i++) {
			session->roster.players[i].flags = 0;
			session->replication.status[i].flags = 0;
		}

		session->roster_revision++;

		/* Queue type 17 before the kicked client's flush/close. */
		if (kicked)
			return mh_net_session_queue(
				session, 1, &message, 1, 1, error_out);
	}

	return mh_net_session_queue(session, 1, NULL, 1, 1, error_out);
}

static enum mh_error_code
receive_client_message(struct mh_net_session *session,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	struct mh_net_message reply;
	struct mh_net_profile profile;
	enum mh_error_code status;

	if (session->config.wire == MH_NET_WIRE_F12)
		session->contact_ms = session->now_ms;

	if (message->type == MH_NET_REJECT) {
		session->phase = MH_NET_REJECTED;
		session->transport_rejection = 0;
		session->rejection_reason = message->reason;
		memcpy(session->rejection, message->text,
			sizeof(session->rejection));
		session->rejection[sizeof(session->rejection) - 1] = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (message->type == MH_NET_ACCEPT) {
		if (!mh_net_protocol_accepts(
			session->protocol, message->simulation_rate))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"server and client simulation timing are incompatible");

		if (session->phase != MH_NET_JOINING || message->player == 0 ||
			session->player != mh_net_session_capacity(session))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unexpected slot assignment");

		session->player = message->player;
		session->config.simulation_rate = message->simulation_rate;
		memset(&reply, 0, sizeof(reply));
		reply.type = MH_NET_PROFILE_JOIN;
		reply.player = session->player;
		reply.profile = session->config.profile;
		status = mh_net_session_queue(
			session, 1, &reply, 1, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		return mh_net_session_lobby_message(session,
			"Connection accepted. ", "", 0, 1, 0, error_out);
	}

	if (message->type == MH_NET_KICK) {
		if (session->phase != MH_NET_LOBBY)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"kick outside lobby");

		status = mh_net_session_lobby_message(session,
			"You got kicked by the server!", "", 0, 1, 0,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;

		session->kicked = 1;

		return close_client_lobby(session, 1, error_out);
	}

	if (message->type == MH_NET_RACE_CONFIG)
		return receive_race_config(session, message, error_out);

	if (message->type == MH_NET_ROSTER)
		return receive_roster(session, message, error_out);

	if (message->type == MH_NET_LAST_RACE) {
		if (session->phase != MH_NET_LOBBY)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"last-race statistics outside lobby");

		/* The patched receiver retains all twelve fixed-width rows. */
		session->last_race = message->statistics;
		session->statistics_revision++;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	/*
	 * The unreliable channel can overtake acceptance and the reliable
	 * roster.
	 */
	if ((message->type == MH_NET_PING || message->type == MH_NET_STATUS) &&
		session->phase == MH_NET_JOINING)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/*
	 * Original lobby admission sends its type-7 welcome BEFORE type 6.
	 * Only the assigned connection's host text is valid at this boundary.
	 */
	if (message->type == MH_NET_CHAT && session->phase == MH_NET_JOINING &&
		session->player < mh_net_session_capacity(session) &&
		message->player == 0) {
		mh_net_session_add_chat(session, message);
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (session->phase != MH_NET_LOBBY && !mh_net_session_in_race(session))
		return mh_error_set(
			error_out, MH_ERROR_STATE, "client is not in session");

	if (!mh_net_protocol_has(
		session->protocol, MH_NET_PROTOCOL_APPLICATION_PING) &&
		message->type == MH_NET_PING)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"legacy ping in OpenMH session");

	if (message->type == MH_NET_PING) {
		if (session->config.wire == MH_NET_WIRE_31_LOBBY)
			session->contact_ms = floor(session->now_ms);

		memset(&reply, 0, sizeof(reply));
		reply.type = session->phase == MH_NET_WAITING ||
				session->phase == MH_NET_RACING
			? MH_NET_PONG
			: MH_NET_LOBBY_PONG;
		reply.player = session->player;
		reply.stamp = message->stamp;

		/*
		 * Legacy races batch replies with car updates.
		 */
		if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
			mh_net_protocol_has(session->protocol,
				MH_NET_PROTOCOL_APPLICATION_PING) &&
			reply.type == MH_NET_PONG)
			return mh_net_wire_batch_append(session->config.wire,
				&session->replication.pending_service[0],
				&reply, error_out);

		return mh_net_session_queue(
			session, 1, &reply, 0, 0, error_out);
	}

	if (message->type == MH_NET_START) {
		/*
		 * A queued host release can cross an individual return to
		 * lobby. The patched race receiver accepts every delivered
		 * START, including one received after the countdown began.
		 */
		if (session->phase == MH_NET_LOBBY ||
			(session->phase == MH_NET_RACING &&
				session->config.wire == MH_NET_WIRE_F12))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (session->phase != MH_NET_WAITING &&
			session->phase != MH_NET_RACING)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"start signal before race loaded");

		session->start_pending = 1;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (message->type == MH_NET_CAR || message->type == MH_NET_CAR_FAR) {
		int accepted;

		if (!mh_net_session_in_race(session) ||
			!session->replication.configured)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		if (message->player == session->player)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"host must not overwrite the local car");

		/*
		 * Reliable departures can overtake snapshots already in flight.
		 */
		if (message->player >= session->roster.player_count ||
			!(session->roster.players[message->player].flags &
				MH_NET_FLAG_RACING))
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		return receive_snapshot(session, message, &accepted, error_out);
	}

	if (message->type == MH_NET_STATUS)
		return receive_status(session, message, error_out);

	if (message->type == MH_NET_LEAVE_RACE)
		return receive_race_return(session, message, error_out);

	if (message->type == MH_NET_CHAT || message->type == MH_NET_NOTICE) {
		if (message->player >= session->roster.player_count ||
			(!(session->roster.players[message->player].flags &
				MH_NET_FLAG_PRESENT) &&
				!(session->config.wire ==
						MH_NET_WIRE_31_LOBBY &&
					message->player == 0)))
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"chat refers to absent player");

		if (message->type == MH_NET_NOTICE) {
			mh_net_session_set_notice(session, message);
		} else
			mh_net_session_add_chat(session, message);

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (message->type == MH_NET_PROFILE_UPDATE) {
		if (mh_net_session_in_race(session))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"profile update changes frozen race");

		if (message->player >= session->roster.player_count ||
			!(message->profile.flags & MH_NET_FLAG_LOBBY) ||
			message->profile.update_rate == 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid player profile update");

		profile = message->profile;

		/* A dedicated console rename must not create a driver. */
		if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
			message->player == 0 &&
			!(session->roster.players[0].flags &
				MH_NET_FLAG_PRESENT))
			profile.flags &= ~(MH_NET_FLAG_PRESENT |
				MH_NET_FLAG_READY | MH_NET_FLAG_PASS);

		store_profile_status(session, message->player, &profile);
		session->roster.players[message->player] = profile;
		session->roster_revision++;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (message->type == MH_NET_LEAVE_LOBBY) {
		if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
			mh_net_session_in_race(session))
			return receive_race_departure(
				session, message->player, error_out);

		if (message->player == 0)
			return close_client_lobby(session, 0, error_out);

		if (session->config.wire == MH_NET_WIRE_31_LOBBY)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		mh_net_session_remove_replica(session, message->player);
		memset(&session->roster.players[message->player], 0,
			sizeof(session->roster.players[0]));
		session->roster_revision++;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(
		error_out, MH_ERROR_INVALID, "unexpected client lobby message");
}

enum mh_error_code
mh_net_session_receive(struct mh_net_session *session, unsigned long peer,
	const unsigned char *data, size_t size, int reliable, double now_ms,
	struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_message message;
	enum mh_error_code status;

	if (!mh_net_session_clock_valid(session, now_ms) ||
		(reliable != 0 && reliable != 1) ||
		(!session->host && peer != 1) ||
		session->phase == MH_NET_REJECTED ||
		session->phase == MH_NET_CLOSED)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid session receive context");

	status = mh_net_wire_begin(
		session->config.wire, data, size, &reader, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *session;
	next.now_ms = now_ms;

	if (session->host && mh_net_session_connection(&next, peer) == NULL)
		return mh_error_set(
			error_out, MH_ERROR_FAILED, "unbound session sender");

	while (mh_net_wire_next(session->config.wire, &reader, &frame) == 1) {
		if (next.phase == MH_NET_REJECTED ||
			next.phase == MH_NET_CLOSED)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"message follows closed session");

		if (!mh_net_wire_supported(session->config.wire, frame.type))
			continue;

		if (!reliable && frame.type != MH_NET_PING &&
			frame.type != MH_NET_LOBBY_PONG &&
			frame.type != MH_NET_PONG && frame.type != MH_NET_CAR &&
			frame.type != MH_NET_CAR_FAR &&
			frame.type != MH_NET_STATUS)
			return mh_error_set(error_out, MH_ERROR_FAILED,
				"session control requires reliable delivery");

		status = mh_net_wire_decode(
			session->config.wire, &frame, &message, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (message.player >= mh_net_session_capacity(session))
			return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
				"player exceeds native session capacity");

		if ((message.type == MH_NET_CHAT ||
			message.type == MH_NET_NOTICE) &&
			strlen(message.text) >= MH_NET_CHAT_BYTES)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"received chat text exceeds original text buffer");

		status = session->host
			? receive_host_message(&next, peer, &message, error_out)
			: receive_client_message(&next, &message, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
