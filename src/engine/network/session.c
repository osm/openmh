#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/core/simulation_rate.h"
#include "engine/network/session_internal.h"

unsigned int
mh_net_session_capacity(const struct mh_net_session *session)
{
	if (session == NULL)
		return 0;

	return !session->host && session->config.wire == MH_NET_WIRE_31_LOBBY
		? MH_NET_WIRE_PLAYERS
		: MH_NET_PLAYERS;
}

int
mh_net_session_in_race(const struct mh_net_session *session)
{
	return session->phase == MH_NET_LOADING ||
		session->phase == MH_NET_WAITING ||
		session->phase == MH_NET_RACING;
}

static int
slot_active(const struct mh_net_lobby_slot *slot)
{
	return slot->phase == MH_NET_IN_LOBBY || slot->phase == MH_NET_IN_RACE;
}

unsigned int
mh_net_session_player_for(
	const struct mh_net_session *session, unsigned long peer)
{
	unsigned int i;

	for (i = 1; i < MH_NET_PLAYERS; i++)
		if (session->lobby.slots[i].peer == peer &&
			slot_active(&session->lobby.slots[i]))
			return i;

	return MH_NET_PLAYERS;
}

enum mh_error_code
mh_net_session_queue(struct mh_net_session *session, unsigned long peer,
	const struct mh_net_message *message, int reliable, int disconnect,
	struct mh_error *error_out)
{
	struct mh_net_outgoing *outgoing;
	enum mh_error_code status;

	if (session->out_count == MH_NET_OUTBOX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network session outbox full");

	outgoing = &session->outgoing[(session->out_head + session->out_count) %
		MH_NET_OUTBOX];
	outgoing->size = 0;

	if (message != NULL) {
		status = mh_net_wire_encode(session->config.wire, message,
			outgoing->data, sizeof(outgoing->data), &outgoing->size,
			error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	outgoing->peer = peer;
	outgoing->reliable = reliable;
	outgoing->disconnect = disconnect;
	session->out_count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_queue_batch(struct mh_net_session *session, unsigned long peer,
	const struct mh_net_batch *batch, int reliable,
	struct mh_error *error_out)
{
	struct mh_net_outgoing *outgoing;

	if (batch->size == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (session->out_count == MH_NET_OUTBOX)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network session outbox full");

	outgoing = &session->outgoing[(session->out_head + session->out_count) %
		MH_NET_OUTBOX];
	outgoing->peer = peer;
	outgoing->reliable = reliable;
	outgoing->disconnect = 0;
	outgoing->size = batch->size;
	memcpy(outgoing->data, batch->data, batch->size);
	session->out_count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_send_audience(struct mh_net_session *session,
	const struct mh_net_message *message, unsigned long except,
	unsigned int flags, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	for (i = 1; i < MH_NET_PLAYERS; i++) {
		if (!slot_active(&session->lobby.slots[i]) ||
			session->lobby.slots[i].peer == except ||
			!(session->lobby.slots[i].profile.flags & flags))
			continue;

		status = mh_net_session_queue(session,
			session->lobby.slots[i].peer, message, 1, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_net_session_sync_roster(struct mh_net_session *session)
{
	unsigned int i;

	memset(&session->roster, 0, sizeof(session->roster));
	memcpy(session->roster.track, session->config.track,
		sizeof(session->roster.track));
	session->roster.mode = session->config.mode;
	session->roster.laps = session->config.laps;
	session->roster.player_count = MH_NET_PLAYERS;
	session->roster.car_count = session->config.car_count;

	for (i = 0; i < MH_NET_PLAYERS; i++) {
		if (slot_active(&session->lobby.slots[i]) ||
			session->config.wire == MH_NET_WIRE_31_LOBBY)
			session->roster.players[i] =
				session->lobby.slots[i].profile;

		/* Publish progress even for inactive configured slots. */
		if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
			session->roster.players[i].value_a =
				session->progress[i].position;
			session->roster.players[i].value_b =
				session->progress[i].lap;
		}
	}

	session->roster_revision++;
}

enum mh_error_code
mh_net_session_send_roster(
	struct mh_net_session *session, struct mh_error *error_out)
{
	struct mh_net_message message;

	mh_net_session_sync_roster(session);
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_ROSTER;
	message.roster = session->roster;

	return mh_net_session_send_audience(
		session, &message, 0, MH_NET_FLAG_LOBBY, error_out);
}

int
mh_net_session_game_valid(
	const struct mh_net_roster *roster, unsigned int capacity)
{
	unsigned int i;

	if (roster->player_count == 0 || roster->player_count > capacity ||
		roster->track[0] == 0 ||
		roster->mode > MH_NET_MODE_ELIMINATION || roster->laps == 0 ||
		roster->car_count == 0)
		return 0;

	for (i = 0; i < roster->player_count; i++)
		if ((roster->players[i].flags & MH_NET_FLAG_PRESENT) != 0 &&
			roster->players[i].update_rate == 0)
			return 0;

	return 1;
}

enum mh_error_code
mh_net_session_init(int host, const struct mh_net_session_config *config,
	struct mh_net_session *out, struct mh_error *error_out)
{
	struct mh_net_session session;
	struct mh_net_message message;
	unsigned char packet[MH_NET_SESSION_PACKET_BYTES];
	size_t size;
	enum mh_error_code status;

	if (config == NULL || out == NULL || (host != 0 && host != 1) ||
		(config->wire != MH_NET_WIRE_F12 &&
			config->wire != MH_NET_WIRE_31_LOBBY) ||
		config->grid_seed > MH_U32_MAX ||
		!mh_net_protocol_accepts(
			mh_net_protocol_for_rate(config->simulation_rate),
			config->simulation_rate) ||
		(config->simulation_rate != 0 &&
			config->wire != MH_NET_WIRE_31_LOBBY) ||
		config->prediction.minimum > MH_NET_PREDICTION_LIMIT ||
		config->prediction.maximum > MH_NET_PREDICTION_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid session initialization");

	memset(&session, 0, sizeof(session));
	session.host = host;
	session.config = *config;
	session.protocol = mh_net_protocol_for_rate(config->simulation_rate);
	session.grid_seed = config->grid_seed;
	status = mh_net_lobby_init(config->provider, config->password,
		&config->profile, &session.lobby, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_net_session_sync_roster(&session);
	session.lobby.simulation_rate = config->simulation_rate;
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_ROSTER;
	message.roster = session.roster;
	status = mh_net_wire_encode(config->wire, &message, packet,
		sizeof(packet), &size, error_out);

	if (status != MH_ERROR_NONE ||
		!mh_net_session_game_valid(&session.roster, MH_NET_PLAYERS))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid session game settings");

	session.player = host ? 0 : mh_net_session_capacity(&session);
	session.phase = host ? MH_NET_LOBBY : MH_NET_CONNECTING;

	if (!host) {
		memset(&session.roster, 0, sizeof(session.roster));
		session.roster_revision = 0;
	}

	*out = session;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_session_cars(struct mh_net_session *session, const char *const *names,
	unsigned int count, struct mh_error *error_out)
{
	struct mh_net_car_catalog catalog;
	enum mh_error_code status;
	unsigned int i;

	if (session == NULL || !session->host ||
		session->phase != MH_NET_LOBBY ||
		session->config.car_count == 0 ||
		session->config.car_count > count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid host car catalog binding");

	for (i = 0; i < MH_NET_CONNECTIONS; i++)
		if (session->connections[i].peer != 0)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"cannot replace catalog with connected peers");

	status = mh_net_car_catalog_init(names, count, &catalog, error_out);

	if (status == MH_ERROR_NONE)
		session->cars = catalog;

	return status;
}

static enum mh_error_code
publish_local_profile(struct mh_net_session *session,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	unsigned int state;

	if (session->host) {
		session->lobby.slots[0].profile = session->config.profile;
		session->lobby.slots[0].profile.flags =
			(session->config.profile.flags &
				MH_NET_FLAG_RECORDING) |
			MH_NET_FLAG_LOBBY;

		if (session->config.wire == MH_NET_WIRE_F12)
			return mh_net_session_send_roster(session, error_out);

		mh_net_session_sync_roster(session);

		return mh_net_session_send_audience(
			session, message, 0, MH_NET_FLAG_LOBBY, error_out);
	}

	if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
		/* Forward to other members; the sender gets no echo. */
		state = session->roster.players[session->player].flags &
			(MH_NET_FLAG_READY | MH_NET_FLAG_PASS);
		session->roster.players[session->player] =
			session->config.profile;
		session->roster.players[session->player].flags |= state;
		session->roster_revision++;
	}

	return mh_net_session_queue(session, 1, message, 1, 0, error_out);
}

enum mh_error_code
mh_net_session_profile(struct mh_net_session *session,
	const struct mh_net_profile *profile, struct mh_error *error_out)
{
	struct mh_net_session next;
	struct mh_net_message message;
	unsigned char bytes[MH_NET_PACKET_LIMIT];
	size_t size;
	enum mh_error_code status;

	if (session == NULL || profile == NULL ||
		session->phase != MH_NET_LOBBY || profile->update_rate == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid local profile update");

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PROFILE_UPDATE;
	message.player = session->player;
	message.profile = *profile;

	if (session->config.wire == MH_NET_WIRE_31_LOBBY) {
		message.profile.flags =
			(profile->flags & MH_NET_FLAG_RECORDING) |
			MH_NET_FLAG_LOBBY;

		/* Write these bytes independently of local state. */
		message.profile.value_a = message.profile.value_b = 1;
	}

	status = mh_net_wire_encode(session->config.wire, &message, bytes,
		sizeof(bytes), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	next = *session;
	next.config.profile = *profile;

	if (session->config.wire == MH_NET_WIRE_31_LOBBY)
		next.config.profile.flags = message.profile.flags;

	status = publish_local_profile(&next, &message, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	*session = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const struct mh_net_outgoing *
mh_net_session_outgoing(const struct mh_net_session *session)
{
	return session == NULL || session->out_count == 0
		? NULL
		: &session->outgoing[session->out_head];
}

void
mh_net_session_sent(struct mh_net_session *session)
{
	if (session == NULL || session->out_count == 0)
		return;

	session->out_head = (session->out_head + 1) % MH_NET_OUTBOX;
	session->out_count--;
}
