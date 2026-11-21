#include <stdlib.h>
#include <string.h>

#include "backends/network/webrtc_server_internal.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "interfaces/clock/backend.h"
#include "interfaces/network/backend.h"

void
mh_webrtc_server_time(struct mh_webrtc_server *server)
{
	unsigned long word;

	word = mh_clock_source_ticks(server->config.clock) & MH_U32_MAX;
	server->elapsed_ms +=
		(double)((word - server->clock_word) & MH_U32_MAX);
	server->clock_word = word;
}

void
mh_webrtc_server_expire(struct mh_webrtc_server *server)
{
	struct mh_webrtc_peer *peer;
	unsigned int i;

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++) {
		peer = &server->peers[i];

		if (peer->id == 0 || peer->closing)
			continue;

		if (!peer->announced &&
			server->elapsed_ms - peer->opened_ms >=
				RTC_OFFER_TIMEOUT_MS)
			mh_webrtc_peer_finish(
				peer, "WebRTC connection timed out");
		else if (peer->nominated &&
			server->elapsed_ms - peer->ice_ms >= RTC_ICE_IDLE_MS)
			mh_webrtc_peer_finish(
				peer, "WebRTC ICE checks timed out");
	}
}

static struct mh_webrtc_peer *
find_peer(struct mh_webrtc_server *server, unsigned long id)
{
	unsigned int i;

	if (id != 0)
		for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++)
			if (server->peers[i].id == id)
				return &server->peers[i];

	return NULL;
}

void
mh_webrtc_server_cancel(struct mh_webrtc_server *server, unsigned long id)
{
	struct mh_webrtc_peer *peer;

	if (server == NULL)
		return;

	peer = find_peer(server, id);

	if (peer != NULL && !peer->closing)
		mh_webrtc_peer_finish(peer, "WebRTC connection closed");
}

static enum mh_error_code
open_transport(void *context, int is_server, const void *local, void **output,
	struct mh_error *error_out)
{
	(void)context;

	if (!is_server || local == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"WebRTC server requires a prepared endpoint");

	*output = (void *)local;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_transport(void *context, void *token)
{
	struct mh_webrtc_server *server;
	unsigned int i;

	(void)context;
	server = token;

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++)
		mh_webrtc_peer_clear(&server->peers[i]);

	mh_dtls_identity_close(server->identity);
	mh_udp_socket_close(server->socket);
	free(server);
}

static enum mh_error_code
connect_transport(void *context, void *token, const void *target,
	struct mh_error *error_out)
{
	(void)context;

	(void)token;
	(void)target;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"WebRTC server only accepts browser connections");
}

static enum mh_error_code
poll_transport(void *context, void *token, struct mh_transport_event *output,
	struct mh_error *error_out)
{
	struct mh_webrtc_server *server;
	struct mh_webrtc_peer *peer;
	struct mh_transport_event event;
	struct mh_sctp_message message;
	struct mh_reliable_payload payload;
	unsigned int i, slot, received;
	enum mh_error_code status;

	(void)context;
	server = token;
	memset(&event, 0, sizeof(event));
	mh_webrtc_server_time(server);
	mh_webrtc_server_expire(server);

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++) {
		peer = &server->peers[i];

		if (peer->id == 0 || peer->closing)
			continue;

		if (peer->delivery != NULL)
			(void)mh_reliable_delivery_tick(
				peer->delivery, server->clock_word, NULL);

		if (mh_webrtc_peer_pump(server, peer, NULL) != MH_ERROR_NONE)
			mh_webrtc_peer_finish(peer, "WebRTC connection failed");
	}

	status = mh_webrtc_server_read(server, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++) {
		slot = server->next_peer;
		server->next_peer = (slot + 1) % MH_WEBRTC_SERVER_PEERS;
		peer = &server->peers[slot];

		if (peer->id == 0)
			continue;

		if (peer->closing) {
			event.type = MH_TRANSPORT_DISCONNECT;
			event.peer = peer->id;

			if (peer->reason != NULL &&
				mh_text_copy((char *)event.data,
					sizeof(event.data), peer->reason,
					NULL) == MH_ERROR_NONE) {
				event.size = strlen(peer->reason) + 1;
			}

			mh_webrtc_peer_clear(peer);
			break;
		}

		if (peer->delivery == NULL)
			continue;

		if (!peer->announced) {
			peer->announced = 1;
			event.type = MH_TRANSPORT_CONNECT;
			event.peer = peer->id;
			break;
		}

		for (received = 0; received < RTC_PUMP_RECORDS; received++) {
			if (!mh_sctp_receive(peer->sctp, &message))
				break;

			status = mh_reliable_delivery_receive(peer->delivery,
				message.data, message.size, &payload, NULL);

			if (status != MH_ERROR_NONE ||
				payload.size > sizeof(event.data)) {
				mh_webrtc_peer_finish(
					peer, "Invalid WebRTC game message");
				break;
			}

			if (mh_webrtc_peer_flush(server, peer, NULL) !=
				MH_ERROR_NONE) {
				mh_webrtc_peer_finish(
					peer, "WebRTC connection failed");
				break;
			}

			if (payload.size != 0) {
				event.type = MH_TRANSPORT_RECEIVE;
				event.peer = peer->id;
				event.reliable = payload.reliable;
				event.size = payload.size;
				memcpy(event.data, payload.data, payload.size);
				break;
			}
		}

		if (event.type != MH_TRANSPORT_NONE)
			break;
	}

	*output = event;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_transport(void *context, void *token, unsigned long id,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct mh_webrtc_server *server;
	struct mh_webrtc_peer *peer;

	(void)context;
	server = token;
	peer = find_peer(server, id);

	if (peer == NULL || !peer->announced || peer->closing ||
		(reliable != 0 && reliable != 1))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebRTC peer is not connected");

	mh_webrtc_server_time(server);

	return mh_reliable_delivery_send(peer->delivery, data, size, reliable,
		server->clock_word, error_out);
}

static enum mh_error_code
flush_transport(void *context, void *token, struct mh_error *error_out)
{
	struct mh_webrtc_server *server;
	struct mh_webrtc_peer *peer;
	unsigned int i;

	(void)context;
	server = token;
	mh_webrtc_server_time(server);
	mh_webrtc_server_expire(server);

	for (i = 0; i < MH_WEBRTC_SERVER_PEERS; i++) {
		peer = &server->peers[i];

		if (peer->delivery == NULL || peer->closing)
			continue;

		(void)mh_reliable_delivery_tick(
			peer->delivery, server->clock_word, NULL);

		if (mh_webrtc_peer_flush(server, peer, NULL) != MH_ERROR_NONE)
			mh_webrtc_peer_finish(peer, "WebRTC connection failed");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
disconnect_transport(void *context, void *token, unsigned long id,
	struct mh_error *error_out)
{
	(void)context;

	mh_webrtc_server_cancel(token, id);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
peer_address(void *context, void *token, unsigned long id,
	struct mh_transport_address *output, struct mh_error *error_out)
{
	struct mh_webrtc_peer *peer;

	(void)context;
	peer = find_peer(token, id);

	if (peer == NULL || !peer->announced || peer->closing)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"WebRTC peer is not connected");

	if (!peer->has_client)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"WebRTC signaling did not identify the client address");

	*output = peer->client;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
transport_port(void *context, const void *token)
{
	const struct mh_webrtc_server *server;

	(void)context;
	server = token;

	return mh_webrtc_server_port(server);
}

static double
transport_now(void *context, void *token)
{
	struct mh_webrtc_server *server;

	(void)context;
	server = token;
	mh_webrtc_server_time(server);

	return server->elapsed_ms;
}

enum mh_error_code
mh_webrtc_server_open(const struct mh_webrtc_server_config *config,
	struct mh_webrtc_server **server_out,
	struct mh_transport **transport_out, struct mh_error *error_out)
{
	static const struct mh_transport_ops ops = {open_transport,
		close_transport, connect_transport, poll_transport,
		send_transport, flush_transport, disconnect_transport,
		transport_port, transport_now, NULL, peer_address};

	static const struct mh_transport_provider provider = {
		&ops, NULL, NULL, NULL};

	struct mh_webrtc_server *server;
	struct mh_transport_target *target;
	struct mh_transport *transport;
	enum mh_error_code status;

	if (config == NULL || server_out == NULL || transport_out == NULL ||
		config->clock == NULL || config->clock->ops == NULL ||
		config->clock->ops->ticks == NULL ||
		config->bind_ipv4 > MH_U32_MAX ||
		config->advertised_ipv4 < RTC_IPV4_FIRST_UNICAST ||
		config->advertised_ipv4 >= RTC_IPV4_FIRST_MULTICAST ||
		config->port > MH_U16_MAX ||
		config->advertised_port > MH_U16_MAX ||
		(config->filter != NULL &&
			(config->filter->accept == NULL ||
				!mh_text_field_valid(config->filter->rejection,
					MH_TRANSPORT_REJECTION_BYTES))))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid WebRTC server configuration");

	server = calloc(1, sizeof(*server));

	if (server == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate WebRTC server");

	server->config = *config;
	server->local.ipv4 = config->bind_ipv4;
	server->local.port = config->port;
	server->clock_word = mh_clock_source_ticks(config->clock) & MH_U32_MAX;
	status = MH_ERROR_NONE;

	if (config->mux != NULL)
		mh_udp_mux_route(config->mux, mh_webrtc_udp_packet);
	else
		status = mh_udp_socket_open(&server->local, config->wait,
			&server->socket, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_dtls_identity_open(&server->identity, error_out);

	target = NULL;

	if (status == MH_ERROR_NONE)
		status = mh_transport_target_create(
			&provider, server, NULL, &target, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_transport_open_from(
			&provider, 1, target, &transport, error_out);

	mh_transport_target_close(target);

	if (status != MH_ERROR_NONE) {
		close_transport(NULL, server);
		return status;
	}

	*server_out = server;
	*transport_out = transport;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

unsigned int
mh_webrtc_server_port(const struct mh_webrtc_server *server)
{
	return server->config.mux != NULL ? mh_udp_mux_port(server->config.mux)
					: server->local.port;
}
