#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/combined_provider.h"
#include "interfaces/network/backend.h"

#define CHECK(condition)                                                       \
	do {                                                                   \
		if (!(condition)) {                                            \
			fprintf(stderr, "combined transport:%d: %s\n",         \
				__LINE__, #condition);                         \
			exit(1);                                               \
		}                                                              \
	} while (0)

struct child {
	struct mh_transport_event	pending, sent;
	unsigned long			disconnected;
	unsigned int	polls, closes, flushes, disconnects, advertisements;
	enum mh_error_code	poll_error, send_error, disconnect_error;
	double				now;
};

static enum mh_error_code
open_child(void *context, int server, const void *local, void **output,
	struct mh_error *error_out)
{
	CHECK(server && local == NULL);

	*output = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_child(void *context, void *token)
{
	struct child *child;

	CHECK(context == token);
	child = context;
	child->closes++;
}

static enum mh_error_code
connect_child(void *context, void *token, const void *target,
	struct mh_error *error_out)
{
	(void)context;

	(void)token;
	(void)target;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED, "server only");
}

static enum mh_error_code
poll_child(void *context, void *token, struct mh_transport_event *output,
	struct mh_error *error_out)
{
	struct child *child;

	CHECK(context == token);
	child = context;
	child->polls++;

	if (child->poll_error != MH_ERROR_NONE)
		return mh_error_set(
			error_out, child->poll_error, "poll failed");

	*output = child->pending;
	memset(&child->pending, 0, sizeof(child->pending));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_child(void *context, void *token, unsigned long peer,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct child *child;

	CHECK(context == token);
	child = context;
	CHECK(size <= sizeof(child->sent.data));

	if (child->send_error != MH_ERROR_NONE)
		return mh_error_set(
			error_out, child->send_error, "send failed");

	child->sent.peer = peer;
	child->sent.size = size;
	child->sent.reliable = reliable;
	memcpy(child->sent.data, data, size);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
flush_child(void *context, void *token, struct mh_error *error_out)
{
	struct child *child;

	CHECK(context == token);
	child = context;
	child->flushes++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
disconnect_child(void *context, void *token, unsigned long peer,
	struct mh_error *error_out)
{
	struct child *child;

	CHECK(context == token);
	child = context;
	child->disconnects++;

	if (child->disconnect_error != MH_ERROR_NONE)
		return mh_error_set(error_out, child->disconnect_error,
			"disconnect failed");

	child->disconnected = peer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
port_child(void *context, const void *token)
{
	CHECK(context == token);

	return 4321;
}

static double
now_child(void *context, void *token)
{
	struct child *child;

	CHECK(context == token);
	child = context;

	return child->now;
}

static enum mh_error_code
address_child(void *context, void *token, unsigned long peer,
	struct mh_transport_address *output, struct mh_error *error_out)
{
	CHECK(context == token);

	memset(output, 0, sizeof(*output));
	strcpy(output->host, "127.0.0.1");
	output->port = (unsigned int)peer;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
advertise_child(void *context, void *token,
	const struct mh_discovery_advertisement *game,
	struct mh_error *error_out)
{
	struct child *child;

	CHECK(context == token && game == NULL);
	child = context;
	child->advertisements++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
search_child(void *context, void *token, const void *target,
	unsigned long request, struct mh_error *error_out)
{
	(void)request;

	return connect_child(context, token, target, error_out);
}

static enum mh_error_code
search_poll_child(
	void *context, void *token, int *done, struct mh_error *error_out)
{
	(void)done;

	return connect_child(context, token, NULL, error_out);
}

static const struct mh_discovery_catalog *
results_child(void *context, const void *token)
{
	(void)context;
	(void)token;

	return NULL;
}

static const struct mh_transport_ops operations = {open_child, close_child,
	connect_child, poll_child, send_child, flush_child, disconnect_child,
	port_child, now_child, NULL, address_child};
static const struct mh_transport_discovery_ops discovery = {
	advertise_child, search_child, search_poll_child, results_child};

static void
enqueue(struct child *child, enum mh_transport_event_type type,
	unsigned long peer)
{
	CHECK(child->pending.type == MH_TRANSPORT_NONE);

	memset(&child->pending, 0, sizeof(child->pending));
	child->pending.type = type;
	child->pending.peer = peer;

	if (type == MH_TRANSPORT_RECEIVE) {
		child->pending.size = sizeof(child->pending.data);
		child->pending.reliable = 1;
		memset(child->pending.data, 42, sizeof(child->pending.data));
	}
}

static struct mh_transport_event
poll_event(struct mh_transport *transport, enum mh_transport_event_type type)
{
	struct mh_transport_event event;

	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == type);

	return event;
}

int
main(void)
{
	struct child children[2];
	struct mh_transport_provider providers[2];
	struct mh_transport *handles[2], *combined;
	struct mh_transport_event event, before;
	struct mh_transport_address address;
	unsigned long primary, secondary, replacement;
	unsigned int i, polls, disconnects;

	memset(children, 0, sizeof(children));

	for (i = 0; i < 2; i++) {
		CHECK(mh_transport_provider_init(&providers[i], &operations,
			i == 0 ? &discovery : NULL, NULL, &children[i],
			NULL) == MH_ERROR_NONE);
		CHECK(mh_transport_open_from(&providers[i], 1, NULL,
			&handles[i], NULL) == MH_ERROR_NONE);
	}

	combined = handles[0];
	CHECK(mh_combined_transport_open(handles[0], handles[0], &combined,
		NULL) == MH_ERROR_INVALID);
	CHECK(combined == handles[0]);
	CHECK(mh_combined_transport_open(NULL, handles[1], &combined, NULL) ==
		MH_ERROR_INVALID);
	CHECK(combined == handles[0]);
	CHECK(mh_combined_transport_open(handles[0], handles[1], NULL, NULL) ==
		MH_ERROR_INVALID);
	CHECK(children[0].closes == 0 && children[1].closes == 0);
	CHECK(mh_combined_transport_open(handles[0], handles[1], &combined,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_poll(combined, 10, &event, NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(mh_transport_port(combined) == 4321);
	children[0].now = 1234;
	children[1].now = 9876;
	CHECK(mh_transport_now(combined) == 1234);
	CHECK(mh_transport_advertise(combined, NULL, NULL) == MH_ERROR_NONE);
	CHECK(children[0].advertisements == 1 &&
		children[1].advertisements == 0);
	CHECK(mh_transport_flush(combined, NULL) == MH_ERROR_NONE);
	CHECK(children[0].flushes == 1 && children[1].flushes == 1);

	/*
	 * Identical IDs from distinct children must remain distinct publicly.
	 */
	enqueue(&children[0], MH_TRANSPORT_CONNECT, 7);
	enqueue(&children[1], MH_TRANSPORT_CONNECT, 7);
	event = poll_event(combined, MH_TRANSPORT_CONNECT);
	primary = event.peer;
	event = poll_event(combined, MH_TRANSPORT_CONNECT);
	secondary = event.peer;
	CHECK(primary != 0 && secondary != 0 && primary != secondary);
	CHECK(children[0].polls == 1 && children[1].polls == 1);

	for (i = 0; i < 8; i++) {
		enqueue(&children[i % 2], MH_TRANSPORT_RECEIVE, 7);
		event = poll_event(combined, MH_TRANSPORT_RECEIVE);
		CHECK(event.peer == (i % 2 == 0 ? primary : secondary));
		CHECK(event.size == MH_NET_WIRE_PACKET_LIMIT && event.reliable);
		CHECK(event.data[0] == 42 && event.data[event.size - 1] == 42);
		CHECK(mh_transport_send(combined, event.peer, event.data,
			event.size, i % 2, NULL) == MH_ERROR_NONE);
		CHECK(children[i % 2].sent.peer == 7 &&
			children[i % 2].sent.size == MH_NET_WIRE_PACKET_LIMIT &&
			children[i % 2].sent.reliable == (int)(i % 2));
	}

	CHECK(mh_transport_peer_address(combined, secondary, &address, NULL) ==
		MH_ERROR_NONE);
	CHECK(address.port == 7 && strcmp(address.host, "127.0.0.1") == 0);
	children[1].send_error = MH_ERROR_RANGE;
	CHECK(mh_transport_send(combined, secondary, event.data, 1, 1, NULL) ==
		MH_ERROR_RANGE);
	children[1].send_error = MH_ERROR_NONE;

	/*
	 * Duplicate admission and unknown/stale messages do not create peers.
	 */
	enqueue(&children[0], MH_TRANSPORT_CONNECT, 7);
	enqueue(&children[1], MH_TRANSPORT_RECEIVE, 99);
	polls = children[0].polls + children[1].polls;
	(void)poll_event(combined, MH_TRANSPORT_NONE);
	CHECK(children[0].polls + children[1].polls == polls + 2);
	children[1].disconnect_error = MH_ERROR_IO;
	CHECK(mh_transport_disconnect(combined, secondary, NULL) ==
		MH_ERROR_IO);
	CHECK(mh_transport_send(combined, secondary, event.data, 1, 0, NULL) ==
		MH_ERROR_NONE);
	children[1].disconnect_error = MH_ERROR_NONE;
	CHECK(mh_transport_disconnect(combined, secondary, NULL) ==
		MH_ERROR_NONE);
	CHECK(children[1].disconnected == 7 && children[0].disconnected == 0);
	disconnects = children[1].disconnects;
	CHECK(mh_transport_disconnect(combined, secondary, NULL) ==
		MH_ERROR_NONE);
	CHECK(children[1].disconnects == disconnects);
	enqueue(&children[1], MH_TRANSPORT_RECEIVE, 7);
	(void)poll_event(combined, MH_TRANSPORT_NONE);
	CHECK(mh_transport_send(combined, secondary, event.data, 1, 0, NULL) ==
		MH_ERROR_STATE);
	enqueue(&children[1], MH_TRANSPORT_DISCONNECT, 7);
	children[1].pending.size = 5;
	memcpy(children[1].pending.data, "gone", 5);
	event = poll_event(combined, MH_TRANSPORT_DISCONNECT);
	CHECK(event.peer == secondary && event.size == 5 &&
		memcmp(event.data, "gone", 5) == 0);
	enqueue(&children[1], MH_TRANSPORT_CONNECT, 7);
	event = poll_event(combined, MH_TRANSPORT_CONNECT);
	replacement = event.peer;
	CHECK(replacement != primary && replacement != secondary);
	CHECK(mh_transport_send(combined, secondary, event.data, 1, 0, NULL) ==
		MH_ERROR_STATE);
	CHECK(mh_transport_send(combined, replacement, event.data, 1, 0,
		NULL) == MH_ERROR_NONE);

	/*
	 * A full mapping table rejects only the new connection and stays
	 * bounded.
	 */
	for (i = 2; i < MH_COMBINED_PEERS; i++) {
		enqueue(&children[0], MH_TRANSPORT_CONNECT, 100 + i);
		(void)poll_event(combined, MH_TRANSPORT_CONNECT);
	}

	enqueue(&children[0], MH_TRANSPORT_CONNECT, 999);
	(void)poll_event(combined, MH_TRANSPORT_NONE);
	CHECK(children[0].disconnected == 999);
	enqueue(&children[0], MH_TRANSPORT_DISCONNECT, 999);
	(void)poll_event(combined, MH_TRANSPORT_NONE);
	enqueue(&children[0], MH_TRANSPORT_DISCONNECT, 7);
	event = poll_event(combined, MH_TRANSPORT_DISCONNECT);
	CHECK(event.peer == primary);
	enqueue(&children[0], MH_TRANSPORT_CONNECT, 999);
	event = poll_event(combined, MH_TRANSPORT_CONNECT);
	CHECK(event.peer != primary && event.peer != replacement);

	/*
	 * Failures preserve caller events, including a malformed child event.
	 */
	memcpy(&before, &event, sizeof(before));
	children[0].poll_error = MH_ERROR_IO;
	children[1].poll_error = MH_ERROR_IO;
	CHECK(mh_transport_poll(combined, 0, &event, NULL) == MH_ERROR_IO);
	CHECK(memcmp(&event, &before, sizeof(event)) == 0);
	children[0].poll_error = MH_ERROR_NONE;
	children[1].poll_error = MH_ERROR_NONE;
	enqueue(&children[0], MH_TRANSPORT_CONNECT, 0);
	CHECK(mh_transport_poll(combined, 0, &event, NULL) == MH_ERROR_BACKEND);
	CHECK(memcmp(&event, &before, sizeof(event)) == 0);
	mh_transport_close(combined);
	CHECK(children[0].closes == 1 && children[1].closes == 1);

	return 0;
}
