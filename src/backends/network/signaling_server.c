#include <stdlib.h>
#include <string.h>

#include "backends/network/signaling_provider.h"
#include "backends/network/signaling_socket.h"
#include "backends/network/webrtc_signaling.h"
#include "engine/core/limits.h"
#include "engine/core/text.h"
#include "interfaces/clock/backend.h"

enum {
	SIGNAL_CLIENTS = 16,
	SIGNAL_ACCEPT_BUDGET = 4,
	SIGNAL_REQUEST_TIMEOUT_MS = 5000
};

struct signal_client {
	struct mh_signal_socket		*socket;
	struct mh_transport_address	address;
	unsigned long			ipv4, opened, peer;
	unsigned char			input[MH_SIGNAL_REQUEST_BYTES + 1];
	unsigned char			output[MH_SIGNAL_RESPONSE_BYTES];
	size_t				received, response_size, sent;
};

struct mh_signaling_server {
	struct mh_signaling_config	config;
	char				origin[MH_SIGNAL_ORIGIN_BYTES];
	struct mh_signal_socket		*listener;
	struct signal_client		clients[SIGNAL_CLIENTS];
	unsigned int			port;
};

int
mh_signaling_origin_valid(const char *origin)
{
	return mh_signal_origin_valid(origin);
}

static void
close_client(struct mh_signaling_server *server, struct signal_client *client)
{
	if (client->peer != 0)
		server->config.cancel(server->config.context, client->peer);

	mh_signal_socket_close(client->socket);
	memset(client, 0, sizeof(*client));
}

void
mh_signaling_close(struct mh_signaling_server *server)
{
	unsigned int i;

	if (server == NULL)
		return;

	for (i = 0; i < SIGNAL_CLIENTS; i++)
		close_client(server, &server->clients[i]);

	mh_signal_socket_close(server->listener);
	free(server);
}

enum mh_error_code
mh_signaling_open(const struct mh_signaling_config *config,
	struct mh_signaling_server **output, struct mh_error *error_out)
{
	struct mh_signaling_server *server;
	enum mh_error_code status;

	if (config == NULL || output == NULL || config->clock == NULL ||
		config->clock->ops == NULL ||
		config->clock->ops->ticks == NULL ||
		config->bind_ipv4 > MH_U32_MAX ||
		config->proxy_ipv4 > MH_U32_MAX || config->port > MH_U16_MAX ||
		config->offer == NULL || config->cancel == NULL ||
		(config->trust_proxy != 0 && config->trust_proxy != 1) ||
		!mh_signal_origin_valid(config->origin))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid signaling configuration");

	server = calloc(1, sizeof(*server));

	if (server == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate signaling server");

	server->config = *config;
	status = mh_text_copy(server->origin, sizeof(server->origin),
		config->origin, error_out);
	server->config.origin = server->origin;

	if (status == MH_ERROR_NONE)
		status =
			mh_signal_socket_listen(config->bind_ipv4, config->port,
				&server->listener, &server->port, error_out);

	if (status != MH_ERROR_NONE) {
		mh_signaling_close(server);
		return status;
	}

	*output = server;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

unsigned int
mh_signaling_port(const struct mh_signaling_server *server)
{
	return server == NULL ? 0 : server->port;
}

static int
offer_status(enum mh_error_code status)
{
	switch (status) {
	case MH_ERROR_NONE:
		return MH_SIGNAL_OK;
	case MH_ERROR_INVALID:
	case MH_ERROR_UNSUPPORTED:
		return MH_SIGNAL_BAD_REQUEST;
	case MH_ERROR_PERMISSION:
		return MH_SIGNAL_FORBIDDEN;
	case MH_ERROR_RANGE:
	case MH_ERROR_MEMORY:
		return MH_SIGNAL_UNAVAILABLE;
	default:
		return MH_SIGNAL_INTERNAL_ERROR;
	}
}

static void
prepare_response(
	struct mh_signaling_server *server, struct signal_client *client)
{
	struct mh_signal_request request;
	struct mh_transport_address address;
	char answer[MH_RTC_SDP_BYTES + 1];
	const char *origin;
	size_t size;
	int status;
	enum mh_error_code result;

	status = mh_signal_request_read(
		client->input, client->received, &request);

	if (status == MH_SIGNAL_NEED_MORE)
		return;

	origin = NULL;
	size = 0;

	if (status == MH_SIGNAL_READY && request.origin[0] != 0) {
		if (strcmp(request.origin, server->origin) != 0)
			status = MH_SIGNAL_FORBIDDEN;
		else
			origin = server->origin;
	}

	if (status == MH_SIGNAL_READY && request.method == MH_SIGNAL_OPTIONS)
		status = MH_SIGNAL_NO_CONTENT;

	address = client->address;

	if (status == MH_SIGNAL_READY && server->config.trust_proxy &&
		client->ipv4 == server->config.proxy_ipv4) {
		if (!mh_signal_address_normalize(request.real_ip, address.host,
			sizeof(address.host)))
			status = MH_SIGNAL_BAD_REQUEST;

		address.port = 0;
	}

	if (status == MH_SIGNAL_READY) {
		result = server->config.offer(server->config.context,
			client->input + request.body_offset, request.body_size,
			&address, answer, sizeof(answer), &size, &client->peer,
			NULL);
		status = offer_status(result);

		if (result == MH_ERROR_NONE &&
			(size == 0 || size > MH_RTC_SDP_BYTES ||
				client->peer == 0))
			status = MH_SIGNAL_INTERNAL_ERROR;
	}

	if (status != MH_SIGNAL_OK) {
		if (client->peer != 0)
			server->config.cancel(
				server->config.context, client->peer);

		client->peer = 0;
		size = 0;
	}

	if (mh_signal_response_write(status, origin,
		(const unsigned char *)answer, size, client->output,
		sizeof(client->output), &client->response_size,
		NULL) != MH_ERROR_NONE)
		close_client(server, client);
}

static void
step_client(struct mh_signaling_server *server, struct signal_client *client,
	unsigned long now)
{
	size_t size;
	int eof;

	if (((now - client->opened) & MH_U32_MAX) >=
		SIGNAL_REQUEST_TIMEOUT_MS) {
		close_client(server, client);
		return;
	}

	if (client->response_size == 0) {
		if (mh_signal_socket_read(client->socket,
			client->input + client->received,
			sizeof(client->input) - client->received, &size,
			&eof, NULL) != MH_ERROR_NONE ||
			eof) {
			close_client(server, client);
			return;
		}

		client->received += size;

		if (size != 0)
			prepare_response(server, client);
	}

	if (client->socket == NULL || client->response_size == 0)
		return;

	if (mh_signal_socket_write(client->socket,
		client->output + client->sent,
		client->response_size - client->sent, &size,
		NULL) != MH_ERROR_NONE) {
		close_client(server, client);
		return;
	}

	client->sent += size;

	if (client->sent == client->response_size) {
		client->peer = 0;
		close_client(server, client);
	}
}

enum mh_error_code
mh_signaling_poll(
	struct mh_signaling_server *server, struct mh_error *error_out)
{
	struct signal_client *client;
	unsigned long now;
	unsigned int i, accepted_count;
	int accepted;
	enum mh_error_code status;

	if (server == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing signaling server");

	now = mh_clock_source_ticks(server->config.clock) & MH_U32_MAX;

	for (i = 0; i < SIGNAL_CLIENTS; i++)
		if (server->clients[i].socket != NULL)
			step_client(server, &server->clients[i], now);

	accepted_count = 0;

	for (i = 0; i < SIGNAL_CLIENTS && accepted_count < SIGNAL_ACCEPT_BUDGET;
		i++) {
		client = &server->clients[i];

		if (client->socket != NULL)
			continue;

		status = mh_signal_socket_accept(server->listener,
			&client->socket, &client->address, &client->ipv4,
			&accepted, error_out);

		if (status != MH_ERROR_NONE || !accepted)
			return status;

		client->opened = now;
		accepted_count++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
