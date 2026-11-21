#include <stdlib.h>
#include <string.h>

#include "backends/network/udp_target.h"
#include "backends/network/udp_transport.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "interfaces/clock/backend.h"

#define HOST_IDLE_MS 180000

void
mh_udp_sample_time(struct mh_udp_transport *transport)
{
	unsigned long word;

	word = mh_clock_source_ticks(transport->config->clock) & MH_U32_MAX;
	transport->elapsed_ms +=
		(double)((word - transport->clock_word) & MH_U32_MAX);
	transport->clock_word = word;
}

void
mh_udp_peer_clear(struct mh_udp_transport *transport, struct mh_udp_peer *peer)
{
	if (peer->socket != transport->socket)
		mh_udp_socket_close(peer->socket);

	mh_reliable_delivery_destroy(peer->delivery);
	memset(peer, 0, sizeof(*peer));
	peer->socket = NULL;
}

void
mh_udp_peer_finish(struct mh_udp_transport *transport, struct mh_udp_peer *peer)
{
	while (!peer->congested && mh_reliable_delivery_pending(peer->delivery))
		if (mh_reliable_delivery_flush_head(peer->delivery,
			transport->clock_word, NULL) != MH_ERROR_NONE)
			break;

	mh_udp_peer_clear(transport, peer);
}

static void
close_transport(void *context, void *token)
{
	struct mh_udp_transport *transport;
	unsigned int slot;

	(void)context;
	transport = token;
	mh_udp_sample_time(transport);
	mh_resolver_close(transport->bind_lookup);
	mh_resolver_close(transport->target_lookup);
	mh_udp_search_cancel(transport);

	for (slot = 0; slot < MH_UDP_PEERS; slot++)
		mh_udp_peer_finish(transport, &transport->peers[slot]);

	if (transport->server)
		mh_udp_mux_detach(transport->config->mux);

	mh_udp_socket_close(transport->socket);
	free(transport);
}

static enum mh_error_code
open_transport(void *context, int server, const void *local, void **output,
	struct mh_error *error_out)
{
	static const struct mh_udp_target defaults = {NULL, 0};
	const struct mh_udp_target *target;
	struct mh_udp_transport *transport;
	unsigned int slot;
	int ready;
	enum mh_error_code status;

	target = local == NULL ? &defaults : local;

	transport = calloc(1, sizeof(*transport));

	if (transport == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate LAN/INET transport");

	if (server &&
		!mh_udp_random(
			transport->cookie_key, sizeof(transport->cookie_key))) {
		free(transport);
		return mh_error_set(error_out, MH_ERROR_IO,
			"cannot initialize UDP challenge key");
	}

	transport->config = context;
	transport->protocol =
		mh_udp_protocol_get(transport->config->port_policy);
	transport->server = server;
	transport->socket = NULL;
	transport->search.socket = NULL;
	memcpy(transport->name, transport->config->name,
		sizeof(transport->name));

	transport->local.port = target->port;
	transport->clock_word =
		mh_clock_source_ticks(transport->config->clock) & MH_U32_MAX;

	for (slot = 0; slot < MH_UDP_PEERS; slot++)
		transport->peers[slot].socket = NULL;

	status = MH_ERROR_NONE;

	if (target->host != NULL && target->host[0] != 0)
		status = mh_resolver_open(transport->config->resolver,
			target->host, &transport->bind_lookup, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_udp_bind_ready(transport, &ready, error_out);

	if (status != MH_ERROR_NONE) {
		close_transport(context, transport);
		return status;
	}

	*output = transport;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
connect_transport(void *context, void *token, const void *destination,
	struct mh_error *error_out)
{
	const struct mh_udp_target *target;
	struct mh_udp_transport *transport;
	enum mh_error_code status;

	(void)context;
	transport = token;
	target = destination;

	if (target == NULL || transport->server ||
		transport->phase != MH_UDP_IDLE ||
		transport->search.state == MH_UDP_SEARCH_RESOLVING ||
		transport->search.state == MH_UDP_SEARCH_ACTIVE ||
		target->host == NULL || target->host[0] == 0 ||
		target->port == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid LAN/INET connection request");

	if (transport->protocol->shared_socket) {
		do {
			unsigned char generation[4];

			if (!mh_udp_random(generation, sizeof(generation)))
				return mh_error_set(error_out, MH_ERROR_IO,
					"cannot initialize UDP generation");

			transport->connection_generation =
				mh_read_u32le(generation);
		} while (transport->connection_generation == 0);
	}

	status = mh_resolver_open(transport->config->resolver, target->host,
		&transport->target_lookup, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	transport->target.port = target->port;
	transport->phase = MH_UDP_RESOLVING;
	transport->attempts = 0;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned long
peer_id(const struct mh_udp_transport *transport, unsigned int slot)
{
	return transport->server ? MH_UDP_FIRST_PEER + slot
				: MH_UDP_SERVER_PEER;
}

static struct mh_udp_peer *
find_peer(struct mh_udp_transport *transport, unsigned long id)
{
	unsigned int slot;

	if (!transport->server) {
		if (id != MH_UDP_SERVER_PEER)
			return NULL;

		slot = 0;
	} else {
		if (id < MH_UDP_FIRST_PEER ||
			id >= MH_UDP_FIRST_PEER + MH_UDP_PEERS)
			return NULL;

		slot = (unsigned int)(id - MH_UDP_FIRST_PEER);
	}

	return transport->peers[slot].delivery == NULL
		? NULL
		: &transport->peers[slot];
}

enum mh_error_code
mh_udp_receive_delivery(struct mh_udp_transport *transport, unsigned int slot,
	const unsigned char *data, size_t size,
	struct mh_transport_event *event, struct mh_error *error_out)
{
	struct mh_udp_peer *peer;
	struct mh_reliable_payload payload;
	enum mh_error_code status;

	peer = &transport->peers[slot];
	peer->contact_ms = transport->elapsed_ms;
	status = mh_reliable_delivery_receive(
		peer->delivery, data, size, &payload, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (payload.size != 0) {
		event->type = MH_TRANSPORT_RECEIVE;
		event->peer = peer_id(transport, slot);
		event->reliable = payload.reliable;
		event->challenge = peer->challenge;
		event->size = payload.size;
		memcpy(event->data, payload.data, payload.size);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
receive_peer(struct mh_udp_transport *transport, unsigned int slot,
	struct mh_transport_event *event, struct mh_error *error_out)
{
	unsigned char data[MH_SHARED_PACKET_BYTES + 1];
	struct mh_udp_peer *peer;
	struct mh_udp_address source;
	const unsigned char *delivery;
	size_t size;
	unsigned int visited;
	int received;
	enum mh_error_code status;

	peer = &transport->peers[slot];
	(void)mh_reliable_delivery_tick(
		peer->delivery, transport->clock_word, NULL);

	for (visited = 0; visited < MH_UDP_POLL_PACKETS; visited++) {
		status = mh_udp_socket_receive(peer->socket, &source, data,
			sizeof(data), &size, &received, error_out);

		if (status != MH_ERROR_NONE || !received)
			return status;

		if (!mh_udp_address_same(&source, &peer->address))
			continue;

		delivery = data;

		if (!transport->protocol->payload(peer, &delivery, &size))
			continue;

		if (size == 0 || size > MH_RELIABLE_DATAGRAM_BYTES)
			continue;

		status = mh_udp_receive_delivery(
			transport, slot, delivery, size, event, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (event->type == MH_TRANSPORT_NONE)
			continue;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
disconnected_peer(
	struct mh_udp_transport *transport, struct mh_transport_event *event)
{
	struct mh_udp_peer *peer;
	unsigned int slot;

	for (slot = 0; slot < MH_UDP_PEERS; slot++) {
		peer = &transport->peers[slot];

		if (transport->server && peer->delivery != NULL &&
			transport->elapsed_ms >
				peer->contact_ms + HOST_IDLE_MS) {
			mh_udp_peer_finish(transport, peer);
			transport->disconnected |= 1U << slot;
		}

		if (transport->disconnected & (1U << slot)) {
			transport->disconnected &= ~(1U << slot);
			event->type = MH_TRANSPORT_DISCONNECT;
			event->peer = peer_id(transport, slot);
			return 1;
		}
	}

	return 0;
}

static enum mh_error_code
poll_transport(void *context, void *token, struct mh_transport_event *output,
	struct mh_error *error_out)
{
	struct mh_udp_transport *transport;
	struct mh_transport_event event;
	unsigned int visited, slot;
	int ready;
	enum mh_error_code status;

	(void)context;
	transport = token;
	memset(&event, 0, sizeof(event));
	mh_udp_sample_time(transport);
	status = mh_udp_bind_ready(transport, &ready, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (!ready || disconnected_peer(transport, &event)) {
		*output = event;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (transport->server && transport->protocol->shared_socket) {
		for (slot = 0; slot < MH_UDP_PEERS; slot++)
			if (transport->peers[slot].delivery != NULL)
				(void)mh_reliable_delivery_tick(
					transport->peers[slot].delivery,
					transport->clock_word, NULL);

		status =
			transport->protocol->host(transport, &event, error_out);

		if (status == MH_ERROR_NONE)
			*output = event;

		return status;
	}

	status = mh_udp_connection_poll(transport, &event, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	for (visited = 0;
		event.type == MH_TRANSPORT_NONE && visited < MH_UDP_PEERS;
		visited++) {
		slot = transport->next_peer;
		transport->next_peer = (slot + 1) % MH_UDP_PEERS;

		if (transport->peers[slot].delivery == NULL)
			continue;

		status = receive_peer(transport, slot, &event, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	*output = event;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_transport(void *context, void *token, unsigned long id,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct mh_udp_transport *transport;
	struct mh_udp_peer *peer;
	enum mh_error_code status;

	(void)context;
	transport = token;
	peer = find_peer(transport, id);

	if (peer == NULL || (reliable != 0 && reliable != 1))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"LAN/INET peer is not connected");

	mh_udp_sample_time(transport);

	status = mh_reliable_delivery_send(peer->delivery, data, size, reliable,
		transport->clock_word, error_out);

	if (status == MH_ERROR_BACKPRESSURE)
		peer->congested = 1;

	return status;
}

static enum mh_error_code
flush_transport(void *context, void *token, struct mh_error *error_out)
{
	struct mh_udp_transport *transport;
	unsigned int slot;

	(void)context;
	transport = token;
	mh_udp_sample_time(transport);

	for (slot = 0; slot < MH_UDP_PEERS; slot++)
		if (transport->peers[slot].delivery != NULL)
			(void)mh_reliable_delivery_tick(
				transport->peers[slot].delivery,
				transport->clock_word, NULL);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
disconnect_transport(void *context, void *token, unsigned long id,
	struct mh_error *error_out)
{
	struct mh_udp_transport *transport;
	struct mh_udp_peer *peer;
	unsigned int slot;

	(void)context;
	transport = token;
	peer = find_peer(transport, id);

	if (peer != NULL) {
		mh_udp_sample_time(transport);
		slot = (unsigned int)(peer - transport->peers);
		mh_udp_peer_finish(transport, peer);
		transport->disconnected |= 1U << slot;
	}

	if (!transport->server && id == MH_UDP_SERVER_PEER) {
		if (transport->phase != MH_UDP_IDLE)
			transport->disconnected |= 1U;

		mh_resolver_close(transport->target_lookup);
		transport->target_lookup = NULL;
		transport->phase = MH_UDP_IDLE;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
peer_address(void *context, void *token, unsigned long id,
	struct mh_transport_address *output, struct mh_error *error_out)
{
	struct mh_transport_address address;
	const struct mh_udp_peer *peer;

	(void)context;
	peer = find_peer(token, id);

	if (peer == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"LAN/INET peer is not connected");

	mh_udp_address_export(&peer->address, &address);
	*output = address;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
transport_port(void *context, const void *token)
{
	const struct mh_udp_transport *transport;

	(void)context;
	transport = token;

	return transport->socket == NULL ? 0 : transport->local.port;
}

static double
transport_now(void *context, void *token)
{
	struct mh_udp_transport *transport;

	(void)context;
	transport = token;
	mh_udp_sample_time(transport);

	return transport->elapsed_ms;
}

enum mh_error_code
mh_udp_transport_init(struct mh_transport_provider *provider,
	const struct mh_udp_transport_config *config,
	struct mh_error *error_out)
{
	static const struct mh_transport_ops ops = {open_transport,
		close_transport, connect_transport, poll_transport,
		send_transport, flush_transport, disconnect_transport,
		transport_port, transport_now, NULL, peer_address};

	if (config == NULL || config->clock == NULL ||
		config->clock->ops == NULL ||
		config->clock->ops->ticks == NULL || config->resolver == NULL ||
		(config->port_policy != MH_UDP_PORTS_OFFSET &&
			config->port_policy != MH_UDP_PORTS_RANGE &&
			config->port_policy != MH_UDP_PORTS_SHARED) ||
		config->client_port_first > MH_U16_MAX ||
		config->client_port_last > MH_U16_MAX ||
		(config->filter != NULL &&
			(config->filter->accept == NULL ||
				config->filter->rejection == NULL ||
				strlen(config->filter->rejection) >=
					MH_TRANSPORT_REJECTION_BYTES)) ||
		memchr(config->name, 0, sizeof(config->name)) == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid LAN/INET provider configuration");

	if (mh_udp_protocol_get(config->port_policy) == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"requested network protocol is not included in this build");

	return mh_transport_provider_init(provider, &ops, &mh_udp_discovery_ops,
		&mh_udp_addresses, (void *)config, error_out);
}

/* Only the listening socket is shared; legacy peer sockets remain private. */
enum mh_error_code
mh_udp_receive_socket(struct mh_udp_transport *transport,
	struct mh_udp_address *remote, unsigned char *data, size_t capacity,
	size_t *size, int *received, struct mh_error *error_out)
{
	if (transport->server && transport->config->mux != NULL)
		return mh_udp_mux_receive(transport->config->mux,
			MH_UDP_MUX_PRIMARY, remote, data, capacity, size,
			received, error_out);

	return mh_udp_socket_receive(transport->socket, remote, data, capacity,
		size, received, error_out);
}
