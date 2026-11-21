#include <stdlib.h>
#include <string.h>

#include "backends/network/browser_internal.h"
#include "backends/network/reliable_delivery.h"
#include "interfaces/network/backend.h"

struct browser_transport {
	int				handle, started, announced, ended;
	double				origin;
	struct mh_reliable_delivery	*delivery;
};

static enum mh_error_code
send_datagram(void *context, const unsigned char *data, size_t size,
	struct mh_error *error_out)
{
	struct browser_transport *transport;
	int result;

	transport = context;
	result = mh_browser_net_send(transport->handle, data, size);

	if (result == 0)
		return mh_error_set(error_out, MH_ERROR_BACKPRESSURE,
			"browser data channel send queue is full");

	if (result < 0)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"browser data channel is closed");

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_transport(void *context, int server, const void *local, void **output,
	struct mh_error *error_out)
{
	struct browser_transport *transport;

	(void)context;

	if (server || local != NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"browser transport only connects to dedicated servers");

	transport = calloc(1, sizeof(*transport));

	if (transport == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate browser transport");

	transport->delivery =
		mh_reliable_delivery_create(send_datagram, transport);

	if (transport->delivery != NULL)
		transport->handle = mh_browser_net_open();

	if (transport->delivery == NULL || transport->handle == 0) {
		mh_reliable_delivery_destroy(transport->delivery);
		free(transport);
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate browser data channel");
	}

	transport->origin = mh_browser_net_now();
	*output = transport;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_transport(void *context, void *token)
{
	struct browser_transport *transport;

	(void)context;
	transport = token;
	mh_browser_net_close(transport->handle);
	mh_reliable_delivery_destroy(transport->delivery);
	free(transport);
}

static enum mh_error_code
connect_transport(void *context, void *token, const void *remote,
	struct mh_error *error_out)
{
	struct browser_transport *transport;
	const struct mh_browser_target *target;
	unsigned int i;

	(void)context;
	transport = token;
	target = remote;

	if (target == NULL || transport->started)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"browser transport requires a fresh connection target");

	transport->started = 1;

	for (i = 0; i < target->ice_count; i++)
		mh_browser_net_ice(transport->handle, target->ice[i].url,
			target->ice[i].username, target->ice[i].credential);

	mh_browser_net_connect(
		transport->handle, target->url, target->relay_only);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
poll_transport(void *context, void *token, struct mh_transport_event *output,
	struct mh_error *error_out)
{
	struct browser_transport *transport;
	struct mh_transport_event event;
	struct mh_reliable_payload payload;
	unsigned char datagram[MH_RELIABLE_DATAGRAM_BYTES];
	size_t size;
	unsigned int i;
	int state;

	(void)context;
	transport = token;
	memset(&event, 0, sizeof(event));
	state = mh_browser_net_state(transport->handle);

	if (transport->started && !transport->ended) {
		event.peer = BROWSER_SERVER_PEER;

		if (state == BROWSER_CLOSED) {
			event.type = MH_TRANSPORT_DISCONNECT;
			event.size = mh_browser_net_reason(transport->handle,
				event.data, sizeof(event.data));
			transport->ended = 1;
		} else if (state == BROWSER_CONNECTED &&
			!transport->announced) {
			event.type = MH_TRANSPORT_CONNECT;
			transport->announced = 1;
		} else if (state == BROWSER_CONNECTED) {
			(void)mh_reliable_delivery_tick(transport->delivery,
				mh_browser_net_ticks(), NULL);

			for (i = 0; i < BROWSER_POLL_MESSAGES; i++) {
				size = mh_browser_net_receive(transport->handle,
					datagram, sizeof(datagram));

				if (size == 0)
					break;

				if (mh_reliable_delivery_receive(
					transport->delivery, datagram, size,
					&payload, NULL) != MH_ERROR_NONE ||
					payload.size > sizeof(event.data)) {
					mh_browser_net_disconnect(
						transport->handle);
					break;
				}

				if (payload.size != 0) {
					event.type = MH_TRANSPORT_RECEIVE;
					event.reliable = payload.reliable;
					event.size = payload.size;
					memcpy(event.data, payload.data,
						payload.size);
					break;
				}
			}
		}
	}

	*output = event;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_transport(void *context, void *token, unsigned long peer,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct browser_transport *transport;

	(void)context;
	transport = token;

	if (peer != BROWSER_SERVER_PEER || !transport->announced ||
		transport->ended || (reliable != 0 && reliable != 1) ||
		mh_browser_net_state(transport->handle) != BROWSER_CONNECTED)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"browser server is not connected");

	return mh_reliable_delivery_send(transport->delivery, data, size,
		reliable, mh_browser_net_ticks(), error_out);
}

static enum mh_error_code
flush_transport(void *context, void *token, struct mh_error *error_out)
{
	struct browser_transport *transport;

	(void)context;
	transport = token;

	if (mh_browser_net_state(transport->handle) == BROWSER_CONNECTED)
		(void)mh_reliable_delivery_tick(
			transport->delivery, mh_browser_net_ticks(), NULL);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
disconnect_transport(void *context, void *token, unsigned long peer,
	struct mh_error *error_out)
{
	struct browser_transport *transport;

	(void)context;
	transport = token;

	if (peer == BROWSER_SERVER_PEER && transport->started)
		mh_browser_net_disconnect(transport->handle);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static double
now_transport(void *context, void *token)
{
	struct browser_transport *transport;

	(void)context;
	transport = token;

	return mh_browser_net_now() - transport->origin;
}

const struct mh_transport_provider *
mh_browser_network_provider(void)
{
	static const struct mh_transport_ops ops = {open_transport,
		close_transport, connect_transport, poll_transport,
		send_transport, flush_transport, disconnect_transport, NULL,
		now_transport, NULL, NULL};

	static const struct mh_transport_provider provider = {
		&ops, NULL, NULL, NULL};

	return &provider;
}
