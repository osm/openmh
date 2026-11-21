#include "backends/network/udp_transport.h"

/* Divide the 2000 ms connection budget into four attempts. */
#define CONNECT_ATTEMPTS 4
#define CONNECT_RETRY_MS 500

enum mh_error_code
mh_udp_resolve(struct mh_resolver **lookup, struct mh_udp_address *address,
	int *ready, struct mh_error *error_out)
{
	unsigned long ipv4;
	enum mh_error_code status;

	*ready = 1;

	if (*lookup == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_resolver_poll(*lookup, ready, &ipv4, NULL);

	if (status != MH_ERROR_NONE)
		return mh_error_set(
			error_out, status, "cannot resolve LAN/INET address");

	if (*ready) {
		address->ipv4 = ipv4;
		mh_resolver_close(*lookup);
		*lookup = NULL;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_udp_bind_ready(struct mh_udp_transport *transport, int *ready,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_udp_resolve(
		&transport->bind_lookup, &transport->local, ready, error_out);

	if (status == MH_ERROR_NONE && *ready && transport->socket == NULL) {
		status = mh_udp_socket_open(&transport->local,
			transport->config->wait, &transport->socket, error_out);

		if (status == MH_ERROR_NONE && transport->server &&
			transport->config->mux != NULL)
			mh_udp_mux_attach(transport->config->mux,
				transport->socket, &transport->local);
	}

	return status;
}

enum mh_error_code
mh_udp_connection_poll(struct mh_udp_transport *transport,
	struct mh_transport_event *event, struct mh_error *error_out)
{
	enum mh_error_code status;
	int ready;

	if (transport->server)
		return transport->protocol->host(transport, event, error_out);

	if (transport->phase == MH_UDP_RESOLVING) {
		status = mh_udp_resolve(&transport->target_lookup,
			&transport->target, &ready, error_out);

		if (status != MH_ERROR_NONE || !ready)
			return status;

		transport->phase = MH_UDP_CONNECTING;
		transport->retry_ms = transport->elapsed_ms;
	}

	if (transport->phase != MH_UDP_CONNECTING)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = transport->protocol->reply(transport, event, error_out);

	if (status != MH_ERROR_NONE || event->type != MH_TRANSPORT_NONE ||
		transport->elapsed_ms < transport->retry_ms)
		return status;

	if (transport->attempts == CONNECT_ATTEMPTS) {
		transport->phase = MH_UDP_IDLE;
		event->type = MH_TRANSPORT_DISCONNECT;
		event->peer = MH_UDP_SERVER_PEER;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	status = transport->protocol->hello(transport, error_out);

	transport->attempts++;
	transport->retry_ms = transport->elapsed_ms + CONNECT_RETRY_MS;

	return status;
}
