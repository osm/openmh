#include <stdio.h>
#include <string.h>

#include "backends/network/udp_legacy.h"
#include "backends/network/udp_transport.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"

#define VERSION_BYTES 4
#define VERSION_TEXT_BYTES 512

static enum mh_error_code
send_control(struct mh_udp_socket *socket, const struct mh_udp_address *target,
	const struct mh_udp_control *message, struct mh_error *error_out)
{
	unsigned char data[MH_RELIABLE_DATAGRAM_BYTES];
	size_t size;
	enum mh_error_code status;

	status = mh_udp_control_encode(
		message, data, sizeof(data), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_udp_socket_send(socket, target, data, size, error_out);
}

static void
reject(struct mh_udp_transport *transport, const struct mh_udp_address *target,
	const char *text)
{
	struct mh_udp_control message;

	memset(&message, 0, sizeof(message));
	message.type = MH_UDP_REJECT;
	message.rejection = text;
	(void)send_control(transport->socket, target, &message, NULL);
}

static enum mh_error_code
open_peer_socket(struct mh_udp_transport *transport, unsigned int slot,
	struct mh_udp_socket **output)
{
	const struct mh_udp_transport_config *config;
	struct mh_udp_address local;
	unsigned int first, last, port;
	enum mh_error_code status;

	config = transport->config;
	local = transport->local;

	if (config->port_policy == MH_UDP_PORTS_OFFSET) {
		port = (local.port + MH_UDP_FIRST_PEER + slot) & MH_U16_MAX;
		local.port = port;
		return mh_udp_socket_open(&local, config->wait, output, NULL);
	}

	/* Try candidate ports in a fixed order. */
	first = config->client_port_first;
	last = config->client_port_last;

	if (first == 0) {
		local.port = 0;
		return mh_udp_socket_open(&local, config->wait, output, NULL);
	}

	if (last < first)
		last = first;

	status = MH_ERROR_IO;

	for (port = first; port <= last; port++) {
		local.port = port;
		status = mh_udp_socket_open(&local, config->wait, output, NULL);

		if (status == MH_ERROR_NONE)
			break;
	}

	return status;
}

static enum mh_error_code
accept_peer(struct mh_udp_transport *transport,
	const struct mh_udp_address *source, struct mh_transport_event *event,
	struct mh_error *error_out)
{
	struct mh_udp_peer *peer;
	struct mh_udp_control message;
	unsigned int slot;

	for (slot = 0; slot < MH_UDP_PEERS; slot++)
		if (transport->peers[slot].delivery == NULL)
			break;

	if (slot == MH_UDP_PEERS) {
		reject(transport, source, "Connection denied: Server full!");
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	peer = &transport->peers[slot];

	/* Bind failure consumes no slot. A later hello may retry. */
	if (open_peer_socket(transport, slot, &peer->socket) != MH_ERROR_NONE) {
		if (transport->config->port_policy == MH_UDP_PORTS_RANGE &&
			transport->config->client_port_first != 0)
			reject(transport, source,
				"Connection denied: No client port!");

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	peer->address = *source;
	peer->delivery =
		mh_reliable_delivery_create(transport->protocol->send, peer);

	if (peer->delivery == NULL) {
		mh_udp_peer_clear(transport, peer);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate LAN/INET peer");
	}

	peer->contact_ms = transport->elapsed_ms;
	memset(&message, 0, sizeof(message));
	message.type = MH_UDP_ACCEPT;
	memcpy(message.name, transport->name, sizeof(message.name));

	/*
	 * Original admission succeeds even when sending the acceptance fails.
	 */
	(void)send_control(peer->socket, source, &message, NULL);
	event->type = MH_TRANSPORT_CONNECT;
	event->peer = MH_UDP_FIRST_PEER + slot;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
host_control(struct mh_udp_transport *transport,
	struct mh_transport_event *event, struct mh_error *error_out)
{
	unsigned char data[MH_RELIABLE_DATAGRAM_BYTES + 1], bits[VERSION_BYTES];
	struct mh_udp_address source;
	struct mh_udp_control message;
	char rejection[VERSION_TEXT_BYTES];
	struct mh_transport_address address;
	const struct mh_transport_filter *filter;
	double version;
	size_t size;
	unsigned int packet;
	int received;
	enum mh_error_code status;

	for (packet = 0; packet < MH_UDP_POLL_PACKETS; packet++) {
		status = mh_udp_receive_socket(transport, &source, data,
			sizeof(data), &size, &received, error_out);

		if (status != MH_ERROR_NONE || !received)
			return status;

		if (mh_udp_discovery_request(transport, &source, data, size))
			continue;

		if (mh_udp_control_decode(data, size, &message, NULL) !=
			MH_ERROR_NONE)
			continue;

		if (message.type != MH_UDP_HELLO)
			continue;

		if (message.version_bits != MH_UDP_VERSION_BITS) {
			mh_write_u32le(bits, message.version_bits);
			(void)mh_read_f32le(bits, &version);

			/*
			 * Finite binary32 fits this fixed decimal diagnostic
			 * buffer.
			 */
			sprintf(rejection,
				"Connection denied: Wrong mhsock version, Server v%2.2f, Client v%2.2f.",
				1.3, version);
			reject(transport, &source, rejection);
			continue;
		}

		filter = transport->config->filter;

		if (filter != NULL) {
			mh_udp_address_export(&source, &address);

			if (!filter->accept(filter->context, &address,
				transport->elapsed_ms)) {
				reject(transport, &source, filter->rejection);
				continue;
			}
		}

		return accept_peer(transport, &source, event, error_out);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
client_reply(struct mh_udp_transport *transport,
	struct mh_transport_event *event, struct mh_error *error_out)
{
	unsigned char data[MH_RELIABLE_DATAGRAM_BYTES + 1];
	struct mh_udp_address source;
	struct mh_udp_control message;
	struct mh_udp_peer *peer;
	size_t size;
	unsigned int packet;
	int received;
	enum mh_error_code status;

	for (packet = 0; packet < MH_UDP_POLL_PACKETS; packet++) {
		status = mh_udp_receive_socket(transport, &source, data,
			sizeof(data), &size, &received, error_out);

		if (status != MH_ERROR_NONE || !received)
			return status;

		/*
		 * Acceptance teaches the peer port, but never a different host.
		 */
		if (source.ipv4 != transport->target.ipv4 ||
			mh_udp_control_decode(data, size, &message, NULL) !=
				MH_ERROR_NONE)
			continue;

		if (message.type == MH_UDP_REJECT) {
			if (source.port != transport->target.port)
				continue;

			transport->phase = MH_UDP_IDLE;
			event->type = MH_TRANSPORT_DISCONNECT;
			event->peer = MH_UDP_SERVER_PEER;
			event->size = strlen(message.rejection) + 1;
			memcpy(event->data, message.rejection, event->size);

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (message.type != MH_UDP_ACCEPT)
			continue;

		peer = &transport->peers[0];
		peer->socket = transport->socket;
		peer->address = source;
		peer->delivery = mh_reliable_delivery_create(
			transport->protocol->send, peer);

		if (peer->delivery == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot allocate LAN/INET peer");

		transport->phase = MH_UDP_CONNECTED;
		event->type = MH_TRANSPORT_CONNECT;
		event->peer = MH_UDP_SERVER_PEER;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
hello(struct mh_udp_transport *transport, struct mh_error *error_out)
{
	struct mh_udp_control message;

	memset(&message, 0, sizeof(message));
	message.type = MH_UDP_HELLO;
	message.version_bits = MH_UDP_VERSION_BITS;

	return send_control(
		transport->socket, &transport->target, &message, error_out);
}

static enum mh_error_code
send_payload(void *context, const unsigned char *data, size_t size,
	struct mh_error *error_out)
{
	struct mh_udp_peer *peer;

	peer = context;

	return mh_udp_socket_send(
		peer->socket, &peer->address, data, size, error_out);
}

static int
read_payload(const struct mh_udp_peer *peer, const unsigned char **data,
	size_t *size)
{
	(void)peer;

	(void)data;
	(void)size;

	return 1;
}

const struct mh_udp_protocol *
mh_udp_legacy_protocol(void)
{
	static const struct mh_udp_protocol protocol = {0, host_control,
		client_reply, hello, send_payload, read_payload};

	return &protocol;
}
