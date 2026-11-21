#include <string.h>

#include "backends/network/udp_description.h"
#include "backends/network/udp_target.h"
#include "backends/network/udp_transport.h"
#include "engine/core/limits.h"

/* Nonblocking discovery query budget. */
#define SEARCH_TIMEOUT_MS 1500

void
mh_udp_search_cancel(struct mh_udp_transport *transport)
{
	mh_resolver_close(transport->search.lookup);
	transport->search.lookup = NULL;
	mh_udp_socket_close(transport->search.socket);
	transport->search.socket = NULL;
	transport->search.state = MH_UDP_SEARCH_IDLE;
}

int
mh_udp_discovery_request(struct mh_udp_transport *transport,
	const struct mh_udp_address *source, const unsigned char *data,
	size_t size)
{
	struct mh_shared_packet packet;

	if (size != MH_SHARED_HEADER_BYTES + MH_NET_NONCE_BYTES ||
		mh_shared_packet_read(data, size, &packet, NULL) !=
			MH_ERROR_NONE ||
		(packet.type != MH_SHARED_QUERY &&
			packet.type != MH_SHARED_QUERY_CONFIRM))
		return 0;

	if (transport->advertisement_size == 0 ||
		!mh_udp_reply_allowed(transport, source->ipv4))
		return 1;

	if (packet.type == MH_SHARED_QUERY)
		mh_udp_challenge_reply(transport, source, packet);
	else if (mh_udp_cookie_valid(transport, source, &packet))
		(void)mh_udp_socket_send(transport->socket, source,
			transport->advertisement, transport->advertisement_size,
			NULL);

	return 1;
}

static enum mh_error_code
advertise(void *context, void *token,
	const struct mh_discovery_advertisement *game,
	struct mh_error *error_out)
{
	struct mh_udp_transport *transport;
	unsigned char data[MH_RELIABLE_DATAGRAM_BYTES];
	size_t size;
	enum mh_error_code status;

	(void)context;
	transport = token;

	if (!transport->server)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"LAN/INET advertising requires a host");

	if (game == NULL) {
		transport->advertisement_size = 0;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = mh_udp_description_encode(
		game, data, sizeof(data), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memcpy(transport->advertisement, data, size);
	transport->advertisement_size = size;
	memcpy(transport->name, game->name, sizeof(transport->name));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
begin_search(void *context, void *token, const void *destination,
	unsigned long request, struct mh_error *error_out)
{
	const struct mh_udp_target *target;
	struct mh_udp_transport *transport;
	struct mh_resolver *lookup;
	enum mh_error_code status;

	(void)context;
	transport = token;
	target = destination;

	if (target == NULL || transport->server ||
		transport->phase != MH_UDP_IDLE || target->port == 0 ||
		request == 0 || request > MH_U32_MAX)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid LAN/INET search");

	if (target->host == NULL || target->host[0] == 0)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"LAN/INET discovery requires a host address");

	lookup = NULL;
	status = mh_resolver_open(
		transport->config->resolver, target->host, &lookup, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	mh_udp_search_cancel(transport);
	memset(&transport->search, 0, sizeof(transport->search));
	transport->search.socket = NULL;
	transport->search.lookup = lookup;

	transport->search.generation = request;
	transport->search.address.port = target->port;
	transport->search.state = MH_UDP_SEARCH_RESOLVING;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_query(struct mh_udp_transport *transport, struct mh_error *error_out)
{
	static const unsigned char padding[MH_NET_NONCE_BYTES] = {0};
	struct mh_shared_packet request;
	struct mh_udp_control query;
	unsigned char data[4];
	size_t size;
	struct mh_udp_address local;
	enum mh_error_code status;

	memset(&query, 0, sizeof(query));
	query.type = MH_UDP_QUERY;
	status = mh_udp_control_encode(
		&query, data, sizeof(data), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	local = transport->local;
	local.port = 0;
	status = mh_udp_socket_open(&local, transport->config->wait,
		&transport->search.socket, error_out);

	memset(&request, 0, sizeof(request));
	request.type = MH_SHARED_QUERY;
	request.client_generation = transport->search.generation;
	request.data = padding;
	request.size = sizeof(padding);

	if (status == MH_ERROR_NONE)
		status = mh_udp_shared_send(transport->search.socket,
			&transport->search.address, &request, error_out);

	/* An original host only understands its four-byte discovery query. */
	if (status == MH_ERROR_NONE && !transport->protocol->shared_socket)
		status = mh_udp_socket_send(transport->search.socket,
			&transport->search.address, data, size, error_out);

	if (status != MH_ERROR_NONE) {
		mh_udp_search_cancel(transport);
		return status;
	}

	transport->search.state = MH_UDP_SEARCH_ACTIVE;
	transport->search.deadline_ms =
		transport->elapsed_ms + SEARCH_TIMEOUT_MS;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
finish_search(struct mh_udp_transport *transport)
{
	mh_udp_search_cancel(transport);
	transport->search.state = MH_UDP_SEARCH_DONE;
}

static enum mh_error_code
receive_description(
	struct mh_udp_transport *transport, struct mh_error *error_out)
{
	unsigned char data[MH_RELIABLE_DATAGRAM_BYTES + 1];
	struct mh_udp_address source;
	struct mh_discovery_advertisement game;
	struct mh_shared_packet packet_reply;
	size_t size;
	unsigned int packet;
	int received;
	enum mh_error_code status;

	for (packet = 0; packet < MH_UDP_POLL_PACKETS; packet++) {
		status = mh_udp_socket_receive(transport->search.socket,
			&source, data, sizeof(data), &size, &received,
			error_out);

		if (status != MH_ERROR_NONE || !received)
			return status;

		if (!mh_udp_address_same(&source, &transport->search.address))
			continue;

		if (mh_shared_packet_read(data, size, &packet_reply, NULL) ==
				MH_ERROR_NONE &&
			packet_reply.type == MH_SHARED_QUERY_CHALLENGE &&
			packet_reply.client_generation ==
				transport->search.generation) {
			packet_reply.type = MH_SHARED_QUERY_CONFIRM;
			status = mh_udp_shared_send(transport->search.socket,
				&source, &packet_reply, error_out);

			if (status != MH_ERROR_NONE)
				return status;

			continue;
		}

		if (mh_udp_description_decode(data, size, &game, NULL) !=
			MH_ERROR_NONE)
			continue;

		status = mh_discovery_collect(&transport->search.catalog,
			source.ipv4, source.port, &game, NULL);

		if (status != MH_ERROR_NONE)
			continue;

		finish_search(transport);
		break;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
poll_search(void *context, void *token, int *done, struct mh_error *error_out)
{
	struct mh_udp_transport *transport;
	int ready;
	enum mh_error_code status;

	(void)context;
	transport = token;

	if (done == NULL || transport->search.state == MH_UDP_SEARCH_IDLE)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"LAN/INET search is not active");

	mh_udp_sample_time(transport);
	status = mh_udp_bind_ready(transport, &ready, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (ready && transport->search.state == MH_UDP_SEARCH_RESOLVING) {
		status = mh_udp_resolve(&transport->search.lookup,
			&transport->search.address, &ready, error_out);

		if (status == MH_ERROR_NONE && ready)
			status = send_query(transport, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	if (transport->search.state == MH_UDP_SEARCH_ACTIVE) {
		if (transport->elapsed_ms >= transport->search.deadline_ms)
			finish_search(transport);
		else
			status = receive_description(transport, error_out);
	}

	if (status != MH_ERROR_NONE)
		return status;

	*done = transport->search.state == MH_UDP_SEARCH_DONE;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static const struct mh_discovery_catalog *
search_results(void *context, const void *token)
{
	const struct mh_udp_transport *transport;

	(void)context;
	transport = token;

	return transport->search.state == MH_UDP_SEARCH_RESOLVING
		? NULL
		: &transport->search.catalog;
}

const struct mh_transport_discovery_ops mh_udp_discovery_ops = {
	advertise, begin_search, poll_search, search_results};
