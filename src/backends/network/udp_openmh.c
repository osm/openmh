#include <string.h>

#include "backends/network/udp_openmh.h"
#include "backends/network/udp_transport.h"
#include "engine/core/binary.h"

static void
reject_peer(struct mh_udp_transport *transport,
	const struct mh_udp_address *remote, unsigned long generation,
	const char *reason)
{
	struct mh_shared_packet packet;

	memset(&packet, 0, sizeof(packet));
	packet.type = MH_SHARED_REJECT;
	packet.client_generation = generation;
	packet.data = (const unsigned char *)reason;
	packet.size = strlen(reason) + 1;
	(void)mh_udp_shared_send(transport->socket, remote, &packet, NULL);
}

static void
accept_reply(struct mh_udp_transport *transport, const struct mh_udp_peer *peer)
{
	struct mh_shared_packet packet;

	unsigned char acceptance[MH_UDP_NAME_BYTES + MH_NET_NONCE_BYTES];

	memcpy(acceptance, transport->name, MH_UDP_NAME_BYTES);
	memcpy(acceptance + MH_UDP_NAME_BYTES, peer->challenge.nonce,
		MH_NET_NONCE_BYTES);
	packet.type = MH_SHARED_ACCEPT;
	packet.client_generation = peer->client_generation;
	packet.server_generation = peer->server_generation;
	packet.data = acceptance;
	packet.size = sizeof(acceptance);
	(void)mh_udp_shared_send(
		transport->socket, &peer->address, &packet, NULL);
}

static enum mh_error_code
accept_peer(struct mh_udp_transport *transport,
	const struct mh_udp_address *remote, unsigned long generation,
	struct mh_transport_event *event, struct mh_error *error_out)
{
	struct mh_udp_peer *peer;
	struct mh_transport_address address;
	const struct mh_transport_filter *filter;
	unsigned int slot;

	filter = transport->config->filter;

	if (filter != NULL) {
		mh_udp_address_export(remote, &address);

		if (!filter->accept(
			filter->context, &address, transport->elapsed_ms)) {
			reject_peer(transport, remote, generation,
				filter->rejection);
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	for (slot = 0; slot < MH_UDP_PEERS; slot++)
		if (transport->peers[slot].delivery == NULL)
			break;

	if (slot == MH_UDP_PEERS) {
		reject_peer(transport, remote, generation,
			"Connection denied: Server full!");
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	peer = &transport->peers[slot];

	do {
		if (!mh_udp_random(peer->challenge.nonce,
			sizeof(peer->challenge.nonce)))
			return mh_error_set(error_out, MH_ERROR_IO,
				"cannot initialize UDP peer challenge");

		peer->server_generation = mh_read_u32le(peer->challenge.nonce);
	} while (peer->server_generation == 0);

	peer->challenge.active = 1;
	peer->delivery =
		mh_reliable_delivery_create(transport->protocol->send, peer);

	if (peer->delivery == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate OpenMH peer");

	peer->socket = transport->socket;
	peer->address = *remote;
	peer->client_generation = generation;
	peer->contact_ms = transport->elapsed_ms;
	accept_reply(transport, peer);
	event->challenge = peer->challenge;
	event->type = MH_TRANSPORT_CONNECT;
	event->peer = MH_UDP_FIRST_PEER + slot;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
host(struct mh_udp_transport *transport, struct mh_transport_event *event,
	struct mh_error *error_out)
{
	unsigned char data[MH_SHARED_PACKET_BYTES + 1];
	struct mh_udp_address remote;
	struct mh_shared_packet packet;

	struct mh_udp_peer *peer;
	unsigned int visited, slot;
	size_t size;
	int received;
	enum mh_error_code status;

	for (visited = 0; visited < MH_UDP_POLL_PACKETS; visited++) {
		status = mh_udp_receive_socket(transport, &remote, data,
			sizeof(data), &size, &received, error_out);

		if (status != MH_ERROR_NONE || !received)
			return status;

		if (mh_udp_discovery_request(transport, &remote, data, size))
			continue;

		if (mh_shared_packet_read(data, size, &packet, NULL) !=
			MH_ERROR_NONE)
			continue;

		if (packet.type == MH_SHARED_HELLO ||
			packet.type == MH_SHARED_CONFIRM) {
			if (!mh_udp_reply_allowed(transport, remote.ipv4))
				continue;

			if (packet.type == MH_SHARED_HELLO) {
				mh_udp_challenge_reply(
					transport, &remote, packet);
				continue;
			}

			if (!mh_udp_cookie_valid(transport, &remote, &packet))
				continue;
		}

		for (slot = 0; slot < MH_UDP_PEERS; slot++)
			if (transport->peers[slot].delivery != NULL &&
				mh_udp_address_same(
					&transport->peers[slot].address,
					&remote))
				break;

		if (slot == MH_UDP_PEERS) {
			if (packet.type == MH_SHARED_CONFIRM)
				return accept_peer(transport, &remote,
					packet.client_generation, event,
					error_out);

			continue;
		}

		peer = &transport->peers[slot];

		if (packet.type == MH_SHARED_CONFIRM) {
			if (packet.client_generation ==
				peer->client_generation) {
				accept_reply(transport, peer);
				continue;
			}

			/*
			 * Retire the old session before admitting a reconnect
			 * retry.
			 */
			mh_udp_peer_finish(transport, peer);
			transport->disconnected |= 1U << slot;

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (packet.type != MH_SHARED_DATA ||
			packet.client_generation != peer->client_generation ||
			packet.server_generation != peer->server_generation)
			continue;

		status = mh_udp_receive_delivery(transport, slot, packet.data,
			packet.size, event, error_out);

		if (status != MH_ERROR_NONE || event->type != MH_TRANSPORT_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
hello(struct mh_udp_transport *transport, struct mh_error *error_out)
{
	static const unsigned char padding[MH_NET_NONCE_BYTES] = {0};
	struct mh_shared_packet packet;

	memset(&packet, 0, sizeof(packet));
	packet.type = MH_SHARED_HELLO;
	packet.data = padding;
	packet.size = sizeof(padding);
	packet.client_generation = transport->connection_generation;

	return mh_udp_shared_send(
		transport->socket, &transport->target, &packet, error_out);
}

static enum mh_error_code
reply(struct mh_udp_transport *transport, struct mh_transport_event *event,
	struct mh_error *error_out)
{
	unsigned char data[MH_SHARED_PACKET_BYTES + 1];
	struct mh_udp_address remote;
	struct mh_shared_packet packet;
	struct mh_udp_peer *peer;
	size_t size;
	unsigned int visited;
	int received;
	enum mh_error_code status;

	for (visited = 0; visited < MH_UDP_POLL_PACKETS; visited++) {
		status = mh_udp_receive_socket(transport, &remote, data,
			sizeof(data), &size, &received, error_out);

		if (status != MH_ERROR_NONE || !received)
			return status;

		if (!mh_udp_address_same(&remote, &transport->target) ||
			mh_shared_packet_read(data, size, &packet, NULL) !=
				MH_ERROR_NONE ||
			packet.client_generation !=
				transport->connection_generation)
			continue;

		if (packet.type == MH_SHARED_CHALLENGE) {
			packet.type = MH_SHARED_CONFIRM;
			return mh_udp_shared_send(
				transport->socket, &remote, &packet, error_out);
		}

		if (packet.type == MH_SHARED_REJECT) {
			transport->phase = MH_UDP_IDLE;
			event->type = MH_TRANSPORT_DISCONNECT;
			event->peer = MH_UDP_SERVER_PEER;

			/*
			 * Oversized remote diagnostics are discarded, never
			 * truncated.
			 */
			if (packet.size <= sizeof(event->data)) {
				event->size = packet.size;
				memcpy(event->data, packet.data, packet.size);
			}

			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (packet.type != MH_SHARED_ACCEPT)
			continue;

		peer = &transport->peers[0];
		peer->delivery = mh_reliable_delivery_create(
			transport->protocol->send, peer);

		if (peer->delivery == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot allocate OpenMH peer");

		peer->socket = transport->socket;
		peer->address = remote;
		peer->client_generation = packet.client_generation;
		peer->server_generation = packet.server_generation;
		peer->challenge.active = 1;
		memcpy(peer->challenge.nonce, packet.data + MH_UDP_NAME_BYTES,
			MH_NET_NONCE_BYTES);
		transport->phase = MH_UDP_CONNECTED;
		event->challenge = peer->challenge;
		event->type = MH_TRANSPORT_CONNECT;
		event->peer = MH_UDP_SERVER_PEER;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_payload(void *context, const unsigned char *data, size_t size,
	struct mh_error *error_out)
{
	struct mh_udp_peer *peer;
	struct mh_shared_packet packet;
	unsigned char encoded[MH_SHARED_PACKET_BYTES];
	size_t written;
	enum mh_error_code status;

	peer = context;

	packet.type = MH_SHARED_DATA;
	packet.client_generation = peer->client_generation;
	packet.server_generation = peer->server_generation;
	packet.data = data;
	packet.size = size;
	status = mh_shared_packet_write(
		&packet, encoded, sizeof(encoded), &written, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_udp_socket_send(
		peer->socket, &peer->address, encoded, written, error_out);
}

static int
read_payload(const struct mh_udp_peer *peer, const unsigned char **data,
	size_t *size)
{
	struct mh_shared_packet packet;

	if (mh_shared_packet_read(*data, *size, &packet, NULL) !=
			MH_ERROR_NONE ||
		packet.type != MH_SHARED_DATA ||
		packet.client_generation != peer->client_generation ||
		packet.server_generation != peer->server_generation)
		return 0;

	*data = packet.data;
	*size = packet.size;

	return 1;
}

const struct mh_udp_protocol *
mh_udp_openmh_protocol(void)
{
	static const struct mh_udp_protocol protocol = {
		1, host, reply, hello, send_payload, read_payload};

	return &protocol;
}
