#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/session_internal.h"
#include "engine/network/snapshot_schedule.h"
#include "engine/network/vehicle_message.h"

#define WORLD_LIMIT 1000000
#define STATUS_VALUE_MAX 255

void
mh_net_session_reset_replication(struct mh_net_session *session)
{
	unsigned int i, kept, source, destination;
	double received_ms[MH_NET_WIRE_PLAYERS];
	unsigned long ping_ms[MH_NET_WIRE_PLAYERS];

	/* Clear prediction without clearing receipt or ping times. */
	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++) {
		received_ms[i] = session->config.wire == MH_NET_WIRE_31_LOBBY
			? session->replication.history[i].received_ms
			: 0;
	}

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		ping_ms[i] = session->config.wire == MH_NET_WIRE_31_LOBBY
			? session->replication.status[i].ping_ms
			: 0;

	memset(&session->replication, 0, sizeof(session->replication));

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		session->replication.history[i].received_ms = received_ms[i];

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		session->replication.status[i].ping_ms = ping_ms[i];

	kept = 0;
	memset(&session->notice, 0, sizeof(session->notice));
	session->notice_revision = 0;

	/*
	 * Native boundary: discard unsent unreliable data from the previous
	 * phase. Reliable control/chat keeps its original order.
	 */
	for (i = 0; i < session->out_count; i++) {
		source = (session->out_head + i) % MH_NET_OUTBOX;

		if (!session->outgoing[source].reliable)
			continue;

		destination = (session->out_head + kept++) % MH_NET_OUTBOX;

		if (source != destination)
			session->outgoing[destination] =
				session->outgoing[source];
	}

	session->out_count = kept;
}

void
mh_net_session_remove_replica(
	struct mh_net_session *session, unsigned int player)
{
	unsigned int i;
	double received_ms;
	unsigned long ping_ms;

	received_ms = session->config.wire == MH_NET_WIRE_31_LOBBY
		? session->replication.history[player].received_ms
		: 0;
	ping_ms = session->config.wire == MH_NET_WIRE_31_LOBBY
		? session->replication.status[player].ping_ms
		: 0;
	mh_net_history_reset(&session->replication.history[player]);
	session->replication.history[player].received_ms = received_ms;
	memset(session->replication.position[player], 0,
		sizeof(session->replication.position[player]));
	memset(&session->replication.status[player], 0,
		sizeof(session->replication.status[player]));
	session->replication.status[player].ping_ms = ping_ms;
	session->replication.pending_status &= ~(1U << player);
	mh_net_batch_reset(&session->replication.pending_service[player]);
	session->replication.status_valid &= ~(1U << player);

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		if (session->replication.view[i] == player)
			session->replication.view[i] = i;
}

static enum mh_error_code
queue_patched_service(struct mh_net_session *session, unsigned int recipient,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	struct mh_net_batch *batch;
	unsigned int i;
	enum mh_error_code status;

	batch = &session->replication.pending_service[recipient];
	memset(&message, 0, sizeof(message));

	if (mh_net_protocol_has(
		session->protocol, MH_NET_PROTOCOL_APPLICATION_PING)) {
		message.type = MH_NET_PING;
		message.stamp = (unsigned long)session->now_ms;
		status = mh_net_wire_batch_append(
			session->config.wire, batch, &message, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	message.type = MH_NET_STATUS;
	memcpy(message.status, session->replication.status,
		sizeof(session->replication.status));

	if (session->roster.players[recipient].flags & MH_NET_FLAG_LOBBY)
		for (i = 0; i < MH_NET_PLAYERS; i++)
			message.status[i].flags &= MH_NET_PROFILE_FLAGS;

	return mh_net_wire_batch_append(
		session->config.wire, batch, &message, error_out);
}

enum mh_error_code
mh_net_session_update_status(
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_replication *replication;
	struct mh_net_connection *connection;
	struct mh_net_status *player_status;
	double received_ms;
	struct mh_net_message lobby_status;
	enum mh_error_code status;
	unsigned int i;
	unsigned long ping_ms;
	int patched_race;

	replication = &session->replication;
	patched_race = session->config.wire == MH_NET_WIRE_31_LOBBY &&
		(session->phase == MH_NET_WAITING ||
			session->phase == MH_NET_RACING);
	memset(&lobby_status, 0, sizeof(lobby_status));
	lobby_status.type = MH_NET_STATUS;
	replication->status_valid = 0;

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		player_status = &replication->status[i];

		if (i >= session->roster.player_count ||
			!(session->roster.players[i].flags &
				MH_NET_FLAG_PRESENT)) {
			ping_ms = session->config.wire == MH_NET_WIRE_31_LOBBY
				? player_status->ping_ms
				: 0;
			memset(player_status, 0, sizeof(*player_status));
			player_status->ping_ms = ping_ms;

			if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
				player_status->value_a =
					session->progress[i].position;
				player_status->value_b =
					session->progress[i].lap;
			}

			lobby_status.status[i] = *player_status;
			continue;
		}

		if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
			player_status->value_a = session->progress[i].position;
			player_status->value_b = session->progress[i].lap;
		}

		player_status->flags =
			session->roster.players[i].flags & MH_NET_PROFILE_FLAGS;
		replication->status_valid |= 1U << i;

		if ((player_status->flags & MH_NET_FLAG_RACING) &&
			session->roster.mode == MH_NET_MODE_ELIMINATION &&
			(replication->eliminated & (1U << i)))
			player_status->flags |= MH_NET_FLAG_UNAVAILABLE;

		if (session->config.wire == MH_NET_WIRE_F12)
			player_status->ping_ms = 0;

		if (i != session->player) {
			received_ms = replication->history[i].received_ms;

			if ((player_status->flags & MH_NET_FLAG_RACING) &&
				mh_net_snapshot_stale(session->config.wire,
					received_ms, session->now_ms))
				player_status->flags |= MH_NET_FLAG_UNAVAILABLE;

			connection = mh_net_session_connection(
				session, session->lobby.slots[i].peer);

			if (connection != NULL)
				player_status->ping_ms = connection->ping_ms;

			if (!patched_race &&
				(player_status->flags & MH_NET_FLAG_RACING))
				replication->pending_status |= 1U << i;
		}

		lobby_status.status[i] = *player_status;

		/*
		 * Lobby status has only membership/recording bits. Race status
		 * separately includes the stale/eliminated bit for racing
		 * peers.
		 */
		lobby_status.status[i].flags &= MH_NET_PROFILE_FLAGS;
	}

	for (i = 1; i < session->roster.player_count; i++) {
		if (!(session->roster.players[i].flags & MH_NET_FLAG_PRESENT))
			continue;

		/* Suppress lobby status until the return pong arrives. */
		if (session->config.wire == MH_NET_WIRE_31_LOBBY &&
			session->phase == MH_NET_LOBBY &&
			(session->return_pending & (1U << i)))
			continue;

		if (patched_race)
			status = queue_patched_service(session, i, error_out);
		else if (session->roster.players[i].flags & MH_NET_FLAG_LOBBY)
			status = mh_net_session_queue(session,
				session->lobby.slots[i].peer, &lobby_status, 0,
				0, error_out);
		else
			continue;

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (patched_race)
		session->race_service_ms = (unsigned long)session->now_ms;
	else
		replication->status_ms = (unsigned long)session->now_ms;
	replication->status_revision++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
append_snapshot(struct mh_net_session *session, unsigned int recipient,
	unsigned int source, struct mh_net_batch *batch,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	struct mh_net_replication *replication;
	const struct mh_net_vehicle *vehicle;
	enum mh_error_code status;
	unsigned int viewed_player;
	unsigned long stamp;
	int detailed;

	replication = &session->replication;

	if (source == session->player) {
		if (!replication->local_valid)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		vehicle = &replication->local;
	} else {
		/*
		 * Patched host producers also relay the zeroed initial history.
		 */
		if (replication->history[source].count == 0 &&
			session->config.wire == MH_NET_WIRE_F12)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		vehicle = &replication->history[source].entries[0].vehicle;
	}

	viewed_player = replication->view[recipient];

	if (viewed_player >= session->roster.player_count ||
		!(session->roster.players[viewed_player].flags &
			MH_NET_FLAG_RACING))
		viewed_player = recipient;

	detailed = mh_net_snapshot_detailed(replication->position[source],
		replication->position[viewed_player],
		replication->view_distance);

	if (detailed < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network presentation position");

	stamp = source == session->player
		? (unsigned long)session->now_ms
		: replication->history[source].entries[0].stamp;
	status = mh_net_vehicle_message(vehicle, source, stamp, detailed,
		source != session->player, &message, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_net_wire_batch_append(
		session->config.wire, batch, &message, error_out);
}

static void
update_client_ghostout(struct mh_net_session *session)
{
	struct mh_net_replication *replication;
	unsigned long latest, received, deadline;
	unsigned int i, changed;
	double limit;

	if (session->config.wire != MH_NET_WIRE_31_LOBBY)
		return;

	replication = &session->replication;
	latest = 0;

	for (i = 0; i < session->roster.player_count; i++) {
		if (i == session->player ||
			!(session->roster.players[i].flags &
				MH_NET_FLAG_RACING))
			continue;

		received = (unsigned long)replication->history[i].received_ms;

		if (received > latest)
			latest = received;
	}

	if (latest == 0)
		return;

	/* Load this wrapped deadline as signed i32. */
	deadline = (latest + MH_NET_PATCHED_GHOSTOUT_MS) & MH_U32_MAX;
	limit = (double)deadline;

	if (deadline & MH_U32_SIGN_BIT)
		limit -= (double)MH_U32_MODULUS;

	if (session->now_ms <= limit)
		return;

	changed = 0;

	for (i = 0; i < session->roster.player_count; i++) {
		if (!(session->roster.players[i].flags & MH_NET_FLAG_RACING))
			continue;

		if (!(replication->status[i].flags & MH_NET_FLAG_UNAVAILABLE)) {
			replication->status[i].flags |= MH_NET_FLAG_UNAVAILABLE;
			changed = 1;
		}
	}

	/* Recovery comes from the next host status, not a car receipt. */
	if (changed)
		replication->status_revision++;
}

static enum mh_error_code
queue_local_snapshot(struct mh_net_session *session,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	struct mh_net_batch batch;
	enum mh_error_code status;

	if (session->config.wire != MH_NET_WIRE_31_LOBBY)
		return mh_net_session_queue(
			session, 1, message, 0, 0, error_out);

	/* Append the car after previously queued replies. */
	batch = session->replication.pending_service[0];
	status = mh_net_wire_batch_append(
		session->config.wire, &batch, message, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_net_session_queue_batch(
			session, 1, &batch, 0, error_out);

	if (status == MH_ERROR_NONE)
		mh_net_batch_reset(&session->replication.pending_service[0]);

	return status;
}

static enum mh_error_code
send_local_snapshot(struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_replication *replication;
	struct mh_net_message message;
	enum mh_error_code status;
	int due;

	replication = &session->replication;

	if (!replication->local_valid)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	due = mh_net_snapshot_due(replication->sent_ms[session->player],
		session->roster.players[session->player].update_rate,
		session->now_ms);

	if (due < 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid local snapshot update rate");

	if (!due)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_CAR;
	message.player = session->player;
	message.view_player = replication->view[session->player];
	message.stamp = (unsigned long)session->now_ms;
	status = mh_net_vehicle_encode(&replication->local, message.car,
		MH_NET_VEHICLE_BYTES, error_out);

	if (status == MH_ERROR_NONE)
		status = queue_local_snapshot(session, &message, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	replication->sent_ms[session->player] = (unsigned long)session->now_ms;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_recipient_snapshots(struct mh_net_session *session, unsigned int recipient,
	struct mh_error *error_out)
{
	struct mh_net_replication *replication;
	struct mh_net_message message;
	struct mh_net_batch batch;
	unsigned int source;
	enum mh_error_code status;

	replication = &session->replication;
	batch = replication->pending_service[recipient];

	if (replication->pending_status & (1U << recipient)) {
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_STATUS;
		memcpy(message.status, replication->status,
			sizeof(replication->status));
		status = mh_net_wire_batch_append(
			session->config.wire, &batch, &message, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if ((session->roster.players[recipient].flags & MH_NET_FLAG_RACING) &&
		(session->ready_mask & (1U << recipient))) {
		for (source = 0; source < session->roster.player_count;
			source++) {
			if (recipient == source ||
				!(session->roster.players[source].flags &
					MH_NET_FLAG_RACING))
				continue;

			status = append_snapshot(
				session, recipient, source, &batch, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_net_session_queue_batch(session,
		session->lobby.slots[recipient].peer, &batch, 0, error_out);
}

enum mh_error_code
mh_net_session_replicate(
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_replication *replication;
	unsigned int recipient, membership;
	unsigned long service_ms;
	int due;
	enum mh_error_code status;

	replication = &session->replication;
	membership = session->config.wire == MH_NET_WIRE_31_LOBBY
		? MH_NET_FLAG_PRESENT
		: MH_NET_FLAG_RACING;

	if (!replication->configured ||
		(session->phase != MH_NET_WAITING &&
			session->phase != MH_NET_RACING))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (session->now_ms > (double)MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network race clock limit reached");

	if (!session->host) {
		update_client_ghostout(session);
		return send_local_snapshot(session, error_out);
	}

	service_ms = session->config.wire == MH_NET_WIRE_31_LOBBY
		? session->race_service_ms
		: replication->status_ms;

	if (mh_net_snapshot_due(
		service_ms, MH_NET_STATUS_RATE, session->now_ms) == 1) {
		status = mh_net_session_update_status(session, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	for (recipient = 1; recipient < session->roster.player_count;
		recipient++) {
		if (!(session->roster.players[recipient].flags & membership))
			continue;

		due = mh_net_snapshot_due(replication->sent_ms[recipient],
			session->roster.players[recipient].update_rate,
			session->now_ms);

		if (due < 0)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid remote snapshot update rate");

		if (!due)
			continue;

		status =
			send_recipient_snapshots(session, recipient, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		replication->sent_ms[recipient] =
			(unsigned long)session->now_ms;
		replication->pending_status &= ~(1U << recipient);
		mh_net_batch_reset(&replication->pending_service[recipient]);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_configure_replication(struct mh_net_session *session,
	double view_distance, const double position[][3],
	struct mh_error *error_out)
{
	struct mh_net_replication next;
	unsigned int i, j;

	if (session == NULL || session->phase != MH_NET_LOADING ||
		position == NULL ||
		session->roster.player_count >
			mh_net_session_capacity(session) ||
		!(view_distance > 0 && view_distance <= WORLD_LIMIT) ||
		(float)view_distance == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race replication setup");

	memset(&next, 0, sizeof(next));
	next.configured = 1;
	next.view_distance = (double)(float)view_distance;

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++) {
		next.view[i] = i;

		if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
			next.history[i].received_ms =
				session->replication.history[i].received_ms;
			next.status[i].ping_ms =
				session->replication.status[i].ping_ms;
		}

		if (i >= session->roster.player_count ||
			!(session->roster.players[i].flags &
				MH_NET_FLAG_RACING))
			continue;

		for (j = 0; j < 3; j++) {
			if (!(position[i][j] >= -WORLD_LIMIT &&
				position[i][j] <= WORLD_LIMIT))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid network grid position");

			next.position[i][j] = (double)(float)position[i][j];
		}
	}

	session->replication = next;

	/* Clear every progress object and initialize lap one. */
	if (session->host && session->config.wire == MH_NET_WIRE_31_LOBBY)
		for (i = 0; i < session->roster.player_count; i++) {
			session->progress[i].position = 0;
			session->progress[i].lap = 1;
		}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_publish(struct mh_net_session *session,
	const struct mh_net_vehicle *vehicle, unsigned int viewed_player,
	struct mh_error *error_out)
{
	unsigned char encoded[MH_NET_VEHICLE_BYTES];
	unsigned int i;
	enum mh_error_code status;

	if (session == NULL || !mh_net_session_in_race(session) ||
		!session->replication.configured || vehicle == NULL ||
		viewed_player >= session->roster.player_count ||
		!(session->roster.players[viewed_player].flags &
			MH_NET_FLAG_RACING))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid local snapshot publication");

	status = mh_net_vehicle_encode(
		vehicle, encoded, sizeof(encoded), error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < 3; i++)
		if (!(vehicle->position[i] >= -WORLD_LIMIT &&
			vehicle->position[i] <= WORLD_LIMIT))
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"local car outside network world");

	session->replication.local = *vehicle;
	session->replication.local_valid = 1;
	session->replication.view[session->player] = viewed_player;

	for (i = 0; i < 3; i++)
		session->replication.position[session->player][i] =
			(double)(float)vehicle->position[i];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_sample(struct mh_net_session *session, unsigned int player,
	double now_ms, struct mh_net_playback *output,
	struct mh_error *error_out)
{
	struct mh_net_history history;
	struct mh_net_prediction prediction;
	struct mh_net_playback result;
	enum mh_error_code status;
	unsigned int i;

	if (!mh_net_session_clock_valid(session, now_ms) ||
		!mh_net_session_in_race(session) ||
		!session->replication.configured || output == NULL ||
		player == session->player ||
		player >= session->roster.player_count ||
		!(session->roster.players[player].flags & MH_NET_FLAG_RACING))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid remote sample request");

	history = session->replication.history[player];
	prediction = session->config.prediction;
	status = mh_net_history_predict(
		&history, &prediction, now_ms, &result, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (result.updated)
		for (i = 0; i < 3; i++)
			if (!(result.vehicle.position[i] >= -WORLD_LIMIT &&
				result.vehicle.position[i] <= WORLD_LIMIT))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"remote playback outside world bounds");

	session->now_ms = now_ms;
	session->replication.history[player] = history;
	session->config.prediction = prediction;

	if (result.updated)
		memcpy(session->replication.position[player],
			result.vehicle.position,
			sizeof(result.vehicle.position));

	*output = result;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_status(struct mh_net_session *session, unsigned int player,
	unsigned int value_a, unsigned int value_b, struct mh_error *error_out)
{
	if (session == NULL || !session->host ||
		!mh_net_session_in_race(session) ||
		!session->replication.configured ||
		value_a > STATUS_VALUE_MAX || value_b > STATUS_VALUE_MAX ||
		player >= session->roster.player_count ||
		!(session->roster.players[player].flags & MH_NET_FLAG_RACING))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid host race status");

	session->replication.status[player].value_a = value_a;
	session->replication.status[player].value_b = value_b;

	if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
		session->progress[player].position = value_a;
		session->progress[player].lap = value_b;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
