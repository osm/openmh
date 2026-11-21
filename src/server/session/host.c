#include <string.h>

#include "server/commands/client.h"
#include "server/commands/information.h"
#include "server/session/host_internal.h"
#include "server/session/setup.h"

enum {
	EVENTS_PER_POLL = 32
};

static enum mh_error_code
broadcast_lobby(struct mh_server_host *host,
	const struct mh_net_message *message, unsigned long excluded,
	int reliable, struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(host->lobby.roster.players[i].flags &
			MH_NET_FLAG_LOBBY) ||
			host->lobby.peers[i] == excluded)
			continue;

		/* Ping still reaches clients awaiting roster/results. */
		if (message->type == MH_NET_STATUS &&
			(host->lobby.return_pending & (1U << i)))
			continue;

		status = mh_server_host_send(host, host->lobby.peers[i],
			message, reliable, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_host_disconnect(struct mh_server_host *host, unsigned long peer,
	const char *reason, struct mh_error *error_out)
{
	struct mh_net_message roster, departure;
	unsigned int player;
	enum mh_error_code status;

	for (player = 0; player < host->congested_count; player++)
		if (host->congested[player] == peer) {
			host->congested[player] =
				host->congested[--host->congested_count];
			break;
		}

	for (player = 1; player < MH_NET_WIRE_PLAYERS; player++)
		if (host->lobby.peers[player] == peer)
			break;

	status = mh_transport_disconnect(host->transport, peer, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!mh_server_lobby_disconnect(&host->lobby, peer))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	/* The removed slot retains its identity until the next admission. */
	mh_server_event_log(host->event_logger,
		host->lobby.roster.players[player].name, reason);

	memset(&departure, 0, sizeof(departure));
	departure.type = MH_NET_LEAVE_LOBBY;
	departure.player = player;
	status = mh_server_host_multicast(host,
		mh_server_host_members(host, MH_NET_FLAG_RACING), &departure, 1,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_server_lobby_roster(&host->lobby, &roster);
	status = broadcast_lobby(host, &roster, 0, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_server_host_advertise(host, error_out);
}

static enum mh_error_code
send_command_replies(struct mh_server_host *host,
	const struct mh_server_command_output *output,
	struct mh_error *error_out)
{
	const struct mh_server_command_reply *reply;
	struct mh_net_message message;
	unsigned int i, player;
	enum mh_error_code status;

	for (i = 0; i < output->count; i++) {
		reply = &output->replies[i];

		if (reply->log)
			mh_server_event_log(
				host->event_logger, reply->text, NULL);

		memset(&message, 0, sizeof(message));
		message.type = MH_NET_CHAT;
		message.color = reply->color;
		strcpy(message.text, reply->text);

		for (player = 1; player < MH_NET_WIRE_PLAYERS; player++) {
			if (!(reply->recipients & (1U << player)))
				continue;

			status = mh_server_host_send(host,
				host->lobby.peers[player], &message, 1,
				error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_chat(struct mh_server_host *host, unsigned long peer,
	const struct mh_net_message *message, int *closed,
	struct mh_error *error_out)
{
	struct mh_server_command_output output;
	struct mh_net_message roster;
	unsigned int membership;
	enum mh_error_code status;

	status = mh_server_client_command(&host->lobby, &host->ready,
		host->config, host->race.active, peer, message, &output,
		error_out);

	if (status != MH_ERROR_NONE) {
		*closed = 1;
		return mh_server_host_disconnect(host, peer,
			" disconnected (invalid client message).", error_out);
	}

	if (output.forward) {
		struct mh_net_message chat;

		chat = *message;
		strcpy(chat.text, output.chat);
		mh_server_event_log(host->event_logger, chat.text, NULL);
		membership = host->lobby.roster.players[message->player].flags;
		membership = membership & MH_NET_FLAG_LOBBY
			? MH_NET_FLAG_LOBBY
			: MH_NET_FLAG_RACING;

		return mh_server_host_multicast(host,
			mh_server_host_members(host, membership) &
				~(1U << message->player),
			&chat, 1, error_out);
	}

	status = send_command_replies(host, &output, error_out);

	if (status == MH_ERROR_NONE && output.refresh_roster) {
		mh_server_lobby_roster(&host->lobby, &roster);
		status = broadcast_lobby(host, &roster, 0, 1, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	return mh_server_host_advertise(host, error_out);
}

static enum mh_error_code
refresh_client(struct mh_server_host *host, unsigned int player,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	enum mh_error_code status;
	unsigned long peer;

	if (!(host->lobby.return_pending & (1U << player)))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	peer = host->lobby.peers[player];

	/* Ordered roster, statistics, then clear pending. */
	mh_server_lobby_roster(&host->lobby, &message);
	status = mh_server_host_send(host, peer, &message, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_LAST_RACE;
	message.statistics = host->last_race;
	status = mh_server_host_send(host, peer, &message, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	host->lobby.return_pending &= ~(1U << player);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
welcome_player(struct mh_server_host *host, unsigned int player,
	struct mh_error *error_out)
{
	struct mh_server_text welcome;
	char entered[MH_NET_NAME_BYTES + sizeof(" entered the lobby.")];
	unsigned int i;
	enum mh_error_code status;

	strcpy(entered, host->lobby.roster.players[player].name);
	strcat(entered, " entered the lobby.");
	mh_server_event_log(host->event_logger, entered, NULL);
	status = mh_server_host_notice(host,
		mh_server_host_members(host, MH_NET_FLAG_LOBBY), MH_NET_CHAT,
		entered, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_server_information_welcome(&host->lobby, host->config, &welcome);

	for (i = 0; i < welcome.count; i++) {
		status = mh_server_host_notice(host, 1U << player, MH_NET_CHAT,
			welcome.lines[i], error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_join(struct mh_server_host *host, unsigned long peer,
	const struct mh_net_message *message, double now, int *closed,
	struct mh_error *error_out)
{
	struct mh_net_message reply;
	enum mh_error_code status;

	status = mh_server_lobby_join(&host->lobby, host->config, peer,
		&message->join, now, &reply, error_out);

	if (status != MH_ERROR_NONE) {
		*closed = 1;
		return mh_server_host_disconnect(host, peer,
			" disconnected (invalid client message).", error_out);
	}

	status = mh_server_host_send(host, peer, &reply, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (reply.type == MH_NET_REJECT) {
		*closed = 1;
		return mh_server_host_disconnect(host, peer,
			" disconnected (join rejected).", error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_profile(struct mh_server_host *host, unsigned long peer,
	const struct mh_net_message *message, double now, int *closed,
	struct mh_error *error_out)
{
	struct mh_net_message reply;
	enum mh_error_code status;

	status = mh_server_lobby_profile(&host->lobby, host->config, peer,
		message, now, &reply, error_out);

	if (status != MH_ERROR_NONE) {
		*closed = 1;
		return mh_server_host_disconnect(host, peer,
			" disconnected (invalid client message).", error_out);
	}

	status = broadcast_lobby(host, &reply,
		message->type == MH_NET_PROFILE_UPDATE ? peer : 0, 1,
		error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (message->type == MH_NET_PROFILE_JOIN) {
		status = welcome_player(host, message->player, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_server_host_advertise(host, error_out);
}

static enum mh_error_code
receive_race(struct mh_server_host *host, unsigned long peer,
	const struct mh_net_message *message, double now, int *closed,
	struct mh_error *error_out)
{
	unsigned int player;
	int accepted;
	enum mh_error_code status;

	player = message->player;

	if (player == 0 || player >= MH_NET_WIRE_PLAYERS ||
		host->lobby.peers[player] != peer) {
		*closed = 1;
		return mh_server_host_disconnect(host, peer,
			" disconnected (invalid client message).", error_out);
	}

	if (mh_net_protocol_has(host->protocol, MH_NET_PROTOCOL_LOADED_ACK) &&
		message->type == MH_NET_LOADED)
		return mh_server_setup_loaded(
			&host->lobby, peer, message, error_out);

	if (message->type == MH_NET_PONG)
		return mh_server_race_pong(
			&host->lobby, peer, message, now, error_out);

	if (!(host->lobby.roster.players[player].flags & MH_NET_FLAG_RACING))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (message->type == MH_NET_LEAVE_RACE) {
		mh_server_event_log(host->event_logger,
			host->lobby.roster.players[player].name,
			" left the race.");
		host->lobby.roster.players[player].flags &=
			~(MH_NET_FLAG_RACING | MH_NET_FLAG_READY);
		host->lobby.roster.players[player].flags |= MH_NET_FLAG_LOBBY;
		host->lobby.return_pending |= 1U << player;
		return mh_server_host_advertise(host, error_out);
	}

	status = mh_server_vehicle_receive(&host->race.vehicles[player],
		message, now - host->race.origin_ms, &accepted, error_out);

	if (status != MH_ERROR_NONE) {
		*closed = 1;
		return mh_server_host_disconnect(host, peer,
			" disconnected (invalid client message).", error_out);
	}

	return status;
}

static enum mh_error_code
apply_message(struct mh_server_host *host, unsigned long peer,
	const struct mh_net_message *message, int *closed,
	struct mh_error *error_out)
{
	enum mh_error_code status;
	double now;

	now = mh_transport_now(host->transport);
	*closed = 0;

	if (!mh_net_protocol_has(
		host->protocol, MH_NET_PROTOCOL_APPLICATION_PING) &&
		mh_net_protocol_ping_message(message->type))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (message->type == MH_NET_CHAT)
		return receive_chat(host, peer, message, closed, error_out);

	if (message->type == MH_NET_JOIN)
		return receive_join(
			host, peer, message, now, closed, error_out);

	if (message->type == MH_NET_PROFILE_JOIN ||
		message->type == MH_NET_PROFILE_UPDATE)
		return receive_profile(
			host, peer, message, now, closed, error_out);

	if (host->race.active &&
		(message->type == MH_NET_PONG ||
			message->type == MH_NET_LOADED ||
			message->type == MH_NET_CAR ||
			message->type == MH_NET_LEAVE_RACE))
		return receive_race(
			host, peer, message, now, closed, error_out);

	if (message->type == MH_NET_LOBBY_PONG) {
		status = mh_server_lobby_pong(
			&host->lobby, peer, message, now, error_out);

		if (status != MH_ERROR_NONE) {
			*closed = 1;
			return mh_server_host_disconnect(host, peer,
				" disconnected (invalid client message).",
				error_out);
		}

		return refresh_client(host, message->player, error_out);
	}

	if (message->type == MH_NET_LEAVE_LOBBY && message->player != 0 &&
		host->lobby.peers[message->player] == peer) {
		*closed = 1;
		return mh_server_host_disconnect(host, peer,
			" disconnected (left the server).", error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_packet(struct mh_server_host *host,
	const struct mh_transport_event *event, struct mh_error *error_out)
{
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_message message;
	enum mh_error_code status;
	int closed;

	status = mh_net_wire_begin(
		MH_NET_WIRE_31_LOBBY, event->data, event->size, &reader, NULL);

	if (status != MH_ERROR_NONE)
		return mh_server_host_disconnect(host, event->peer,
			" disconnected (invalid packet).", error_out);

	while (mh_net_wire_next(MH_NET_WIRE_31_LOBBY, &reader, &frame) == 1) {
		if (mh_net_wire_decode(MH_NET_WIRE_31_LOBBY, &frame, &message,
			NULL) != MH_ERROR_NONE)
			continue;

		if (message.type == MH_NET_JOIN)
			message.join.challenge = event->challenge;

		if (message.type == MH_NET_LOADED && !event->reliable)
			return mh_server_host_disconnect(host, event->peer,
				" disconnected (unreliable loading notification).",
				error_out);

		status = apply_message(
			host, event->peer, &message, &closed, error_out);

		if (status != MH_ERROR_NONE || closed)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_host_init(struct mh_server_host *host,
	const struct mh_server_config *config, const char *track,
	struct mh_transport *transport, struct mh_error *error_out)
{
	enum mh_error_code status;

	memset(host, 0, sizeof(*host));
	host->config = config;
	host->protocol = mh_server_config_protocol(config);

	if (host->protocol == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"requested network protocol is not included in this build");

	host->transport = transport;
	host->random_state = 1;
	mh_server_bans_init(&host->bans, config);
	status = mh_server_lobby_init(config, track, &host->lobby, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (mh_net_protocol_has(
		host->protocol, MH_NET_PROTOCOL_TIMED_CHANNEL)) {
		status = mh_transport_enable_channel(transport, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_server_host_advertise(host, error_out);
}

static enum mh_error_code
tick_lobby(struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_server_lobby_tick tick;
	struct mh_net_message message;
	unsigned int i;
	enum mh_error_code status;

	status = mh_server_lobby_tick(&host->lobby,
		host->config->lobby_timeout_seconds,
		mh_transport_now(host->transport), &tick, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(tick.expired & (1U << i)))
			continue;

		status = mh_server_host_disconnect(
			host, host->lobby.peers[i], " timed out!", error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (!tick.heartbeat)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (mh_net_protocol_has(
		host->protocol, MH_NET_PROTOCOL_APPLICATION_PING)) {
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_PING;
		message.stamp = tick.stamp;
		status = broadcast_lobby(host, &message, 0, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	mh_server_lobby_status(&host->lobby, &message);

	return broadcast_lobby(host, &message, 0, 0, error_out);
}

static enum mh_error_code
receive_contact(struct mh_server_host *host, unsigned long peer,
	struct mh_error *error_out)
{
	struct mh_transport_timing timing;
	unsigned int player;
	enum mh_error_code status;

	if (!mh_net_protocol_has(host->protocol, MH_NET_PROTOCOL_TIMED_CHANNEL))
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_transport_peer_timing(
		host->transport, peer, &timing, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (player = 1; player < MH_NET_WIRE_PLAYERS; player++) {
		if (host->lobby.peers[player] != peer ||
			!(host->lobby.roster.players[player].flags &
				MH_NET_FLAG_PRESENT))
			continue;

		host->lobby.contact_ms[player] =
			mh_transport_now(host->transport);

		if (timing.samples != 0)
			host->lobby.ping_ms[player] =
				(unsigned long)(timing.mean_ms + .5);

		return refresh_client(host, player, error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_host_poll(struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_transport_event event;
	unsigned int i;
	enum mh_error_code status;

	mh_server_bans_expire(&host->bans, mh_transport_now(host->transport));
	status = mh_server_host_clear_scores(host, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_server_host_kick_votes(host, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_server_host_race_before(host, error_out);

	if (status == MH_ERROR_NONE)
		status = tick_lobby(host, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < EVENTS_PER_POLL; i++) {
		while (host->congested_count != 0) {
			status = mh_server_host_disconnect(host,
				host->congested[--host->congested_count],
				" disconnected (send queue full).", error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		status = mh_transport_poll(
			host->transport, 0, &event, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (event.type == MH_TRANSPORT_NONE)
			break;

		if (event.type == MH_TRANSPORT_RECEIVE ||
			event.type == MH_TRANSPORT_ACTIVITY) {
			status = receive_contact(host, event.peer, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		if (event.type == MH_TRANSPORT_INVALID)
			status = mh_server_host_disconnect(host, event.peer,
				" disconnected (invalid OpenMH channel).",
				error_out);
		else if (event.type == MH_TRANSPORT_RECEIVE)
			status = receive_packet(host, &event, error_out);
		else if (event.type == MH_TRANSPORT_DISCONNECT)
			status = mh_server_host_disconnect(host, event.peer,
				" disconnected (connection closed).",
				error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_server_host_race_after(host, error_out);
}
