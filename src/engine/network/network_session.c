#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "engine/core/error.h"
#include "engine/network/network_session.h"
#include "interfaces/network/network.h"

/* Native service bounds; independent of the session timers. */
#define POLL_WAIT_LIMIT_MS 1000
#define DRAIN_EVENT_LIMIT 256
#define DISCOVERY_IP_PROVIDER 2
#define DISCOVERY_LANINET_PROVIDER 4
#define ISSUE_MESSAGE_BYTES 512

struct mh_network {
	struct mh_transport	*transport;
	int			trace;
	struct mh_net_session	session;
	struct mh_chat		chat;
	char			name[MH_DISCOVERY_LABEL_BYTES];
	char			issue_message[ISSUE_MESSAGE_BYTES];
};

enum mh_error_code
mh_network_open_from(const struct mh_transport_provider *provider, int host,
	const struct mh_transport_target *local,
	const struct mh_transport_target *remote,
	const struct mh_net_session_config *config, struct mh_network **output,
	struct mh_error *error_out)
{
	struct mh_network *network;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing network output");

	network = (struct mh_network *)calloc(1, sizeof(*network));

	if (network == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate network session");

	strcpy(network->name, "Motorhead");
	status =
		mh_net_session_init(host, config, &network->session, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_transport_open_from(
			provider, host, local, &network->transport, error_out);

	if (status == MH_ERROR_NONE &&
		mh_net_protocol_has(network->session.protocol,
			MH_NET_PROTOCOL_TIMED_CHANNEL))
		status = mh_transport_enable_channel(
			network->transport, error_out);

	if (status == MH_ERROR_NONE && !host)
		status = mh_transport_connect(
			network->transport, remote, error_out);

	if (status != MH_ERROR_NONE) {
		mh_network_close(network);
		return status;
	}

	*output = network;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_network_set_trace(struct mh_network *network, int enabled)
{
	if (network != NULL)
		network->trace = enabled != 0;
}

struct mh_net_session *
mh_network_session(struct mh_network *network)
{
	return network == NULL ? NULL : &network->session;
}

enum mh_error_code
mh_network_set_name(struct mh_network *network, const char *name,
	struct mh_error *error_out)
{
	unsigned int i;

	if (network == NULL || name == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing network session name");

	for (i = 0; i < sizeof(network->name) && name[i] != 0; i++)
		if ((unsigned char)name[i] < ' ')
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid network session name");

	if (i == sizeof(network->name))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"network session name exceeds 15 bytes");

	memmove(network->name, name, i + 1);
	memset(network->name + i + 1, 0, sizeof(network->name) - i - 1);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

const char *
mh_network_name(const struct mh_network *network)
{
	return network == NULL ? NULL : network->name;
}

static void
refresh_advertisement(struct mh_network *network)
{
	struct mh_discovery_advertisement game;
	unsigned int i;

	if (!network->session.host)
		return;

	if (network->session.phase == MH_NET_CLOSED ||
		(network->session.config.provider != DISCOVERY_IP_PROVIDER &&
			network->session.config.provider !=
				DISCOVERY_LANINET_PROVIDER)) {
		mh_transport_advertise(network->transport, NULL, NULL);
		return;
	}

	memset(&game, 0, sizeof(game));
	memcpy(game.name, network->name, sizeof(game.name));
	memcpy(game.track, network->session.config.track, sizeof(game.track));
	game.password = network->session.config.password[0] != 0;
	game.racing = network->session.phase == MH_NET_LOADING ||
		network->session.phase == MH_NET_WAITING ||
		network->session.phase == MH_NET_RACING;
	game.mode = network->session.config.mode;
	game.laps = network->session.config.laps;
	game.car_count = network->session.config.car_count;
	game.has_roster = 1;
	game.roster = network->session.roster;

	for (i = 0; i < MH_NET_PLAYERS; i++)
		if (network->session.lobby.slots[i].phase == MH_NET_IN_LOBBY ||
			network->session.lobby.slots[i].phase == MH_NET_IN_RACE)
			game.players++;

	/*
	 * Unrepresentable discovery labels must not terminate an active race.
	 */
	if (mh_transport_advertise(network->transport, &game, NULL) !=
		MH_ERROR_NONE)
		mh_transport_advertise(network->transport, NULL, NULL);
}

struct mh_chat *
mh_network_chat_state(struct mh_network *network)
{
	return network == NULL ? NULL : &network->chat;
}

unsigned int
mh_network_port(const struct mh_network *network)
{
	return network == NULL ? 0 : mh_transport_port(network->transport);
}

double
mh_network_now(struct mh_network *network)
{
	return network == NULL ? 0 : mh_transport_now(network->transport);
}

static enum mh_error_code
remove_failed_peer(struct mh_network *network, unsigned long peer,
	struct mh_error reason, struct mh_network_issue *issue,
	struct mh_error *error_out)
{
	issue->peer = peer;

	mh_error_copy(&issue->reason, &reason, network->issue_message,
		sizeof(network->issue_message));
	mh_transport_disconnect(network->transport, peer, NULL);

	return mh_net_session_disconnected(
		&network->session, peer, mh_network_now(network), error_out);
}

static enum mh_error_code
send_pending(struct mh_network *network, struct mh_network_issue *issue,
	struct mh_error *error_out)
{
	const struct mh_net_outgoing *packet;
	struct mh_error error;
	enum mh_error_code status;
	unsigned long peer;

	while ((packet = mh_net_session_outgoing(&network->session)) != NULL) {
		peer = packet->peer;
		status = MH_ERROR_NONE;

		if (packet->size != 0)
			status = mh_transport_send(network->transport, peer,
				packet->data, packet->size, packet->reliable,
				&error);

		if (status == MH_ERROR_NONE && packet->disconnect) {
			if (network->trace) {
				printf("Network close request host=%d peer=%lu phase=%u at=%.0f\n",
					network->session.host, peer,
					(unsigned int)network->session.phase,
					mh_network_now(network));
				fflush(stdout);
			}

			status = mh_transport_disconnect(
				network->transport, peer, &error);
		}

		if (status != MH_ERROR_NONE) {
			status = remove_failed_peer(
				network, peer, error, issue, &error);

			if (status != MH_ERROR_NONE)
				return mh_error_forward(error_out, &error);
		} else
			mh_net_session_sent(&network->session);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_network_flush(struct mh_network *network, struct mh_network_issue *issue,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (network == NULL || issue == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid network flush");

	issue->peer = 0;
	issue->reason = mh_success;
	refresh_advertisement(network);
	status = send_pending(network, issue, error_out);

	return status == MH_ERROR_NONE
		? mh_transport_flush(network->transport, error_out)
		: status;
}

static enum mh_error_code
disconnect_session(struct mh_network *network,
	const struct mh_transport_event *event, double now_ms,
	struct mh_error *error_out)
{
	struct mh_net_session *session;
	enum mh_error_code status;
	size_t length;
	int connecting;

	if (event->size > sizeof(event->data) ||
		(event->size != 0 && event->data[event->size - 1] != 0))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid transport disconnect reason");

	session = &network->session;
	connecting = !session->host && session->phase == MH_NET_CONNECTING;
	status = mh_net_session_disconnected(
		session, event->peer, now_ms, error_out);

	if (status != MH_ERROR_NONE || !connecting || event->size <= 1)
		return status;

	length = event->size - 1;

	if (length >= sizeof(session->rejection))
		length = sizeof(session->rejection) - 1;

	memcpy(session->rejection, event->data, length);
	session->rejection[length] = 0;
	session->transport_rejection = 1;
	session->phase = MH_NET_REJECTED;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
dispatch_event(struct mh_network *network,
	const struct mh_transport_event *event, double now_ms,
	struct mh_error *error_out)
{
	struct mh_transport_timing timing;
	enum mh_error_code status;

	if (mh_net_protocol_has(
		network->session.protocol, MH_NET_PROTOCOL_TIMED_CHANNEL) &&
		(event->type == MH_TRANSPORT_RECEIVE ||
			event->type == MH_TRANSPORT_ACTIVITY)) {
		status = mh_transport_peer_timing(
			network->transport, event->peer, &timing, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = mh_net_session_contact(&network->session, event->peer,
			now_ms, (unsigned long)(timing.mean_ms + .5),
			timing.samples != 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (event->type == MH_TRANSPORT_CONNECT ||
		event->type == MH_TRANSPORT_DISCONNECT)
		if (network->trace) {
			printf("Network event host=%d peer=%lu event=%u phase=%u at=%.0f\n",
				network->session.host, event->peer,
				(unsigned int)event->type,
				(unsigned int)network->session.phase, now_ms);
			fflush(stdout);
		}

	switch (event->type) {
	case MH_TRANSPORT_CONNECT:
		return mh_net_session_connected_from(&network->session,
			event->peer, now_ms, &event->challenge, error_out);

	case MH_TRANSPORT_RECEIVE:
		return mh_net_session_receive(&network->session, event->peer,
			event->data, event->size, event->reliable, now_ms,
			error_out);

	case MH_TRANSPORT_DISCONNECT:
		return disconnect_session(network, event, now_ms, error_out);

	case MH_TRANSPORT_INVALID:
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid transport packet");

	default:
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}
}

static enum mh_error_code
poll_session(struct mh_network *network, unsigned int wait_ms, int first_poll,
	struct mh_network_issue *issue, int *received_event,
	struct mh_error *error_out)
{
	struct mh_transport_event event;
	struct mh_error error;
	enum mh_error_code status;
	double now_ms;
	int send_before_poll, entering_loading;

	if (network == NULL || issue == NULL || wait_ms > POLL_WAIT_LIMIT_MS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid network step");

	issue->peer = 0;
	issue->reason = mh_success;
	refresh_advertisement(network);
	send_before_poll =
		network->session.config.wire == MH_NET_WIRE_31_LOBBY &&
		(network->session.phase == MH_NET_WAITING ||
			network->session.phase == MH_NET_RACING);

	/* Drain host/client race input after periodic sends. */
	if (send_before_poll && first_poll) {
		status = mh_net_session_tick(
			&network->session, mh_network_now(network), &error);

		if (status != MH_ERROR_NONE)
			return mh_error_forward(error_out, &error);
	}

	status = send_pending(network, issue, &error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &error);

	status = mh_transport_poll(network->transport, wait_ms, &event, &error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &error);

	*received_event = event.type != MH_TRANSPORT_NONE;
	now_ms = mh_network_now(network);

	status = dispatch_event(network, &event, now_ms, &error);

	if (status != MH_ERROR_NONE) {
		status = remove_failed_peer(
			network, event.peer, error, issue, &error);

		if (status != MH_ERROR_NONE)
			return mh_error_forward(error_out, &error);
	}

	/* Setup ends the receive loop before handling Escape. */
	entering_loading =
		network->session.config.wire == MH_NET_WIRE_31_LOBBY &&
		network->session.phase == MH_NET_LOADING;

	if (!send_before_poll && !entering_loading)
		status = mh_net_session_tick(
			&network->session, mh_network_now(network), &error);

	if (status != MH_ERROR_NONE)
		return mh_error_forward(error_out, &error);

	return send_pending(network, issue, error_out);
}

enum mh_error_code
mh_network_step(struct mh_network *network, unsigned int wait_ms,
	struct mh_network_issue *issue, struct mh_error *error_out)
{
	int received_event;

	return poll_session(
		network, wait_ms, 1, issue, &received_event, error_out);
}

enum mh_error_code
mh_network_drain(struct mh_network *network, unsigned int limit,
	struct mh_network_issue *issue, struct mh_error *error_out)
{
	return mh_network_drain_observed(
		network, limit, issue, NULL, NULL, error_out);
}

enum mh_error_code
mh_network_drain_observed(struct mh_network *network, unsigned int limit,
	struct mh_network_issue *issue,
	enum mh_error_code (*observe)(
		void *context, struct mh_error *error_out),
	void *context, struct mh_error *error_out)
{
	unsigned int i;
	int received_event = 0;
	enum mh_error_code status;

	if (limit == 0 || limit > DRAIN_EVENT_LIMIT)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network drain limit");

	for (i = 0; i < limit; i++) {
		status = poll_session(
			network, 0, i == 0, issue, &received_event, error_out);

		if (status == MH_ERROR_NONE && observe != NULL)
			status = observe(context, error_out);

		if (status != MH_ERROR_NONE ||
			issue->reason.code != MH_ERROR_NONE || !received_event)
			return status;

		if (network->session.config.wire == MH_NET_WIRE_31_LOBBY &&
			network->session.phase == MH_NET_LOADING)
			break;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_network_close(struct mh_network *network)
{
	if (network == NULL)
		return;

	mh_transport_close(network->transport);
	free(network);
}
