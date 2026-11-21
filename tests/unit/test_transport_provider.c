#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/input/sdl_provider.h"
#include "backends/render/sdl_canvas_provider.h"
#include "backends/storage/filesystem_provider.h"
#include "backends/window/sdl_provider.h"
#include "engine/core/binary.h"
#include "engine/network/endpoint.h"
#include "engine/network/network_session.h"
#include "interfaces/network/address.h"
#include "interfaces/network/backend.h"
#ifdef MH_TEST_HOST_NETWORK
#include "client/application/multiplayer_internal.h"
#include "client/application/race_frontend_internal.h"
#include "client/session/network_game.h"
#include "client/settings/render_choice.h"
#include "engine/audio/race_profile.h"
#include "engine/clock/game_timer.h"
#include "interfaces/input/backend.h"
#include "interfaces/render/backend.h"
#include "interfaces/render/canvas_backend.h"
#include "interfaces/render/service.h"
#include "interfaces/storage/resource_backend.h"
#include "interfaces/window/backend.h"
#endif

#define EVENT_LIMIT 64
#define STEP_LIMIT 64

static int failures;
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

/* A connected-message provider: no sockets, ports, discovery or waits. */
struct endpoint {
	unsigned int			index;
	unsigned int			head;
	unsigned int			count;
	int				connecting;
	struct mh_transport_event	events[EVENT_LIMIT];
};

struct fixture {
	struct endpoint		*endpoints[2];
	unsigned int		attempts;
	unsigned int		opens;
	unsigned int		closes;
	unsigned int		targets, released_targets;
	unsigned int		polls;
	unsigned int		sends;
	int			fail_open;
	int			fail_connect;
	enum mh_error_code	fail_send;
	double			now;
	double			poll_elapsed;
	char			diagnostic[80];
};

static void
endpoint_parsing(void)
{
	static const char *const invalid[] = {NULL, "", ":16000",
		"host:", "host:0", "host:65536", "host:-1", "host:+1",
		"host:1x", "host:99999999999999999999", "host:1:2", "host: 1",
		"host:1 ", "host/path", "bad host", "[::1]:16000"};

	struct mh_network_endpoint endpoint, saved;
	char long_host[MH_NETWORK_HOST_BYTES + 1];
	unsigned int i;

	CHECK(mh_network_endpoint_parse("server.example", 16000, &endpoint,
		NULL) == MH_ERROR_NONE);
	CHECK(strcmp(endpoint.host, "server.example") == 0 &&
		endpoint.port == 16000);
	CHECK(mh_network_endpoint_parse(
		"127.0.0.1:1", 16000, &endpoint, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(endpoint.host, "127.0.0.1") == 0 && endpoint.port == 1);
	CHECK(mh_network_endpoint_parse("server.example:65535", 16000,
		&endpoint, NULL) == MH_ERROR_NONE);
	CHECK(strcmp(endpoint.host, "server.example") == 0 &&
		endpoint.port == 65535);
	saved = endpoint;

	for (i = 0; i < sizeof(invalid) / sizeof(invalid[0]); i++) {
		CHECK(mh_network_endpoint_parse(invalid[i], 16000, &endpoint,
			NULL) == MH_ERROR_INVALID);
		CHECK(memcmp(&endpoint, &saved, sizeof(endpoint)) == 0);
	}

	memset(long_host, 'a', sizeof(long_host));
	long_host[sizeof(long_host) - 1] = 0;
	CHECK(mh_network_endpoint_parse(long_host, 16000, &endpoint, NULL) ==
		MH_ERROR_RANGE);
	CHECK(memcmp(&endpoint, &saved, sizeof(endpoint)) == 0);
	long_host[sizeof(long_host) - 2] = 0;
	CHECK(mh_network_endpoint_parse(long_host, 0, &endpoint, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(endpoint.host, long_host) == 0 && endpoint.port == 0);
}

static void
release_target(void *context, void *token)
{
	struct fixture *fixture;

	fixture = context;
	fixture->released_targets++;
	free(token);
}

static enum mh_error_code
make_target(const struct mh_transport_provider *provider,
	struct mh_transport_target **output, struct mh_error *error_out)
{
	unsigned int *index;
	enum mh_error_code status;

	index = malloc(sizeof(*index));

	if (index == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"test target allocation failed");

	*index = 0;
	status = mh_transport_target_create(
		provider, index, release_target, output, error_out);

	if (status != MH_ERROR_NONE)
		free(index);
	else
		((struct fixture *)provider->context)->targets++;

	return status;
}

static enum mh_error_code
open_network(const struct mh_transport_provider *provider, int host,
	const struct mh_net_session_config *config, struct mh_network **output,
	struct mh_error *error_out)
{
	struct mh_transport_target *target;
	enum mh_error_code status;

	target = NULL;
	status = host ? MH_ERROR_NONE
		: make_target(provider, &target, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_network_open_from(provider, host, NULL, target,
			config, output, error_out);

	/* Deferred connection must survive release of its prepared target. */
	mh_transport_target_close(target);

	return status;
}

static enum mh_error_code
queue_event(struct endpoint *endpoint, enum mh_transport_event_type type,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct mh_transport_event *event;

	if (endpoint == NULL)
		return mh_error_set(
			error_out, MH_ERROR_STATE, "test endpoint unavailable");

	if (endpoint->count == EVENT_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "test endpoint queue full");

	event = &endpoint->events[(endpoint->head + endpoint->count) %
		EVENT_LIMIT];
	memset(event, 0, sizeof(*event));
	event->type = type;
	event->peer = endpoint->index == 0 ? 2 : 1;
	event->size = size;
	event->reliable = reliable;

	if (size != 0)
		memcpy(event->data, data, size);

	endpoint->count++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
open_endpoint(void *context, int server, const void *local, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct endpoint *endpoint;
	unsigned int index;

	fixture = context;
	index = server ? 0 : 1;
	(void)local;

	fixture->attempts++;

	if (fixture->fail_open)
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, "test open refused");

	if (fixture->endpoints[index] != NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"test endpoint already open");

	endpoint = calloc(1, sizeof(*endpoint));

	if (endpoint == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test allocation failed");

	endpoint->index = index;
	fixture->endpoints[index] = endpoint;
	fixture->opens++;
	*output = endpoint;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_endpoint(void *context, void *token)
{
	struct fixture *fixture;
	struct endpoint *endpoint;

	fixture = context;
	endpoint = token;
	fixture->endpoints[endpoint->index] = NULL;
	fixture->closes++;
	free(endpoint);
}

static enum mh_error_code
connect_endpoint(void *context, void *token, const void *target,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct endpoint *endpoint;

	fixture = context;
	endpoint = token;
	CHECK(target != NULL && *(const unsigned int *)target == 0);

	if (fixture->fail_connect) {
		strcpy(fixture->diagnostic, "test connection refused");
		return mh_error_set(
			error_out, MH_ERROR_BACKEND, fixture->diagnostic);
	}

	endpoint->connecting = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
poll_endpoint(void *context, void *token, struct mh_transport_event *output,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct endpoint *endpoint;

	fixture = context;
	endpoint = token;
	fixture->polls++;
	fixture->now += fixture->poll_elapsed;
	memset(output, 0, sizeof(*output));

	if (endpoint->count != 0) {
		*output = endpoint->events[endpoint->head];
		endpoint->head = (endpoint->head + 1) % EVENT_LIMIT;
		endpoint->count--;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_endpoint(void *context, void *token, unsigned long peer,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct endpoint *endpoint;

	fixture = context;
	endpoint = token;
	CHECK(peer == (endpoint->index == 0 ? 2UL : 1UL));
	CHECK(size <= MH_NET_WIRE_PACKET_LIMIT);
	fixture->sends++;

	if (fixture->fail_send != MH_ERROR_NONE) {
		strcpy(fixture->diagnostic, "test send failed");
		return mh_error_set(
			error_out, fixture->fail_send, fixture->diagnostic);
	}

	return queue_event(fixture->endpoints[1 - endpoint->index],
		MH_TRANSPORT_RECEIVE, data, size, reliable, error_out);
}

static enum mh_error_code
flush_endpoint(void *context, void *token, struct mh_error *error_out)
{
	(void)context;
	(void)token;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
disconnect_endpoint(void *context, void *token, unsigned long peer,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	struct endpoint *endpoint;

	fixture = context;
	endpoint = token;
	CHECK(peer == (endpoint->index == 0 ? 2UL : 1UL));

	/* A valid provider may replace its diagnostic on the next operation. */
	strcpy(fixture->diagnostic, "disconnect replaced diagnostic");

	return queue_event(fixture->endpoints[1 - endpoint->index],
		MH_TRANSPORT_DISCONNECT, NULL, 0, 0, error_out);
}

static double
now_endpoint(void *context, void *token)
{
	struct fixture *fixture;

	fixture = context;
	(void)token;

	return fixture->now;
}

static const struct mh_transport_ops ops = {open_endpoint, close_endpoint,
	connect_endpoint, poll_endpoint, send_endpoint, flush_endpoint,
	disconnect_endpoint, NULL, now_endpoint, NULL, NULL};

static void
complete_connection(struct fixture *fixture)
{
	CHECK(fixture->endpoints[1] != NULL &&
		fixture->endpoints[1]->connecting);
	CHECK(queue_event(fixture->endpoints[0], MH_TRANSPORT_CONNECT, NULL, 0,
		0, NULL) == MH_ERROR_NONE);
	CHECK(queue_event(fixture->endpoints[1], MH_TRANSPORT_CONNECT, NULL, 0,
		0, NULL) == MH_ERROR_NONE);
	fixture->endpoints[1]->connecting = 0;
}

static void
message_capacity(void)
{
	struct fixture fixture;
	struct mh_transport_provider provider;
	struct mh_transport *host, *client;
	struct mh_transport_target *target;
	struct mh_transport_event event;
	unsigned char bytes[MH_NET_WIRE_PACKET_LIMIT];
	unsigned int i;

	memset(&fixture, 0, sizeof(fixture));
	host = client = NULL;
	target = NULL;
	CHECK(mh_transport_provider_init(&provider, &ops, NULL, NULL, &fixture,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_open_from(&provider, 1, NULL, &host, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_transport_open_from(&provider, 0, NULL, &client, NULL) ==
		MH_ERROR_NONE);

	if (host == NULL || client == NULL)
		goto done;

	CHECK(make_target(&provider, &target, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_connect(client, target, NULL) == MH_ERROR_NONE);
	mh_transport_target_close(target);
	complete_connection(&fixture);
	CHECK(mh_transport_poll(host, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == MH_TRANSPORT_CONNECT);
	CHECK(mh_transport_poll(client, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == MH_TRANSPORT_CONNECT);

	for (i = 0; i < EVENT_LIMIT; i++) {
		memset(bytes, (int)i, sizeof(bytes));
		CHECK(mh_transport_send(host, 2, bytes, sizeof(bytes), i % 2,
			NULL) == MH_ERROR_NONE);
	}

	/* The caller can reuse its bytes while the receiver has not polled. */
	memset(bytes, 0xff, sizeof(bytes));
	CHECK(mh_transport_send(host, 2, bytes, 1, 1, NULL) == MH_ERROR_RANGE);
	CHECK(mh_transport_flush(host, NULL) == MH_ERROR_NONE);

	for (i = 0; i < EVENT_LIMIT; i++) {
		CHECK(mh_transport_poll(client, 0, &event, NULL) ==
			MH_ERROR_NONE);
		CHECK(event.type == MH_TRANSPORT_RECEIVE && event.peer == 1);
		CHECK(event.reliable == (int)(i % 2));
		CHECK(event.size == sizeof(bytes));
		memset(bytes, (int)i, sizeof(bytes));
		CHECK(memcmp(event.data, bytes, sizeof(bytes)) == 0);
	}

	/*
	 * Refusal added no event; draining releases capacity for another send.
	 */
	CHECK(mh_transport_poll(client, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == MH_TRANSPORT_NONE);
	bytes[0] = 0xa5;
	CHECK(mh_transport_send(host, 2, bytes, 1, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_poll(client, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == MH_TRANSPORT_RECEIVE && event.size == 1 &&
		event.data[0] == 0xa5);

done:
	mh_transport_close(client);
	mh_transport_close(host);
	CHECK(fixture.opens == fixture.closes);
	CHECK(fixture.targets == fixture.released_targets);
}

static struct mh_net_session_config
session_config(int client)
{
	struct mh_net_session_config result;

	memset(&result, 0, sizeof(result));
	result.provider = 2;
	strcpy(result.password, "hello");
	strcpy(result.track, "Track1.trk");
	result.laps = 3;
	result.car_count = 10;
	strcpy(result.profile.name, client ? "Client" : "Host");
	strcpy(result.profile.car, "Kobra.car");
	strcpy(result.profile.horn, "Horn1.chf");
	result.profile.update_rate = 15;

	return result;
}

static void
step_pair(struct fixture *fixture, struct mh_network *host,
	struct mh_network *client)
{
	struct mh_network_issue issue;
	unsigned int i;

	for (i = 0; i < STEP_LIMIT; i++) {
		fixture->now += 10;
		CHECK(mh_network_step(host, 0, &issue, NULL) == MH_ERROR_NONE);
		CHECK(issue.reason.code == MH_ERROR_NONE);
		CHECK(mh_network_step(client, 0, &issue, NULL) ==
			MH_ERROR_NONE);
		CHECK(issue.reason.code == MH_ERROR_NONE);
	}
}

static void
session_round(enum mh_error_code send_failure)
{
	struct fixture fixture;
	struct mh_transport_provider provider;
	struct mh_net_session_config settings;
	struct mh_network *host, *client;
	struct mh_net_session *host_session, *client_session;
	struct mh_network_issue issue;

	memset(&fixture, 0, sizeof(fixture));
	host = client = NULL;
	CHECK(mh_transport_provider_init(&provider, &ops, NULL, NULL, &fixture,
		NULL) == MH_ERROR_NONE);
	settings = session_config(0);
	CHECK(open_network(&provider, 1, &settings, &host, NULL) ==
		MH_ERROR_NONE);
	settings = session_config(1);
	CHECK(open_network(&provider, 0, &settings, &client, NULL) ==
		MH_ERROR_NONE);

	if (host == NULL || client == NULL)
		goto done;

	host_session = mh_network_session(host);
	client_session = mh_network_session(client);
	CHECK(mh_network_port(host) == 0);
	CHECK(mh_network_step(client, 0, &issue, NULL) == MH_ERROR_NONE);
	CHECK(client_session->phase != MH_NET_LOBBY);
	complete_connection(&fixture);
	step_pair(&fixture, host, client);
	CHECK(host_session->phase == MH_NET_LOBBY);
	CHECK(client_session->phase == MH_NET_LOBBY);
	CHECK(mh_net_session_chat(client_session, "provider chat", 0x112233UL,
		NULL) == MH_ERROR_NONE);
	step_pair(&fixture, host, client);
	CHECK(host_session->chat_count != 0);

	if (host_session->chat_count != 0)
		CHECK(strcmp(host_session->chat[host_session->chat_count - 1]
				.text,
			"Client: provider chat") == 0);

	if (send_failure != MH_ERROR_NONE) {
		CHECK(mh_net_session_chat(host_session, "fail this send", 0,
			NULL) == MH_ERROR_NONE);
		fixture.fail_send = send_failure;
		CHECK(mh_network_flush(host, &issue, NULL) == MH_ERROR_NONE);
		CHECK(issue.peer == 2 && issue.reason.code == send_failure);
		CHECK(issue.reason.message != NULL &&
			strcmp(issue.reason.message, "test send failed") == 0);
		CHECK(strcmp(fixture.diagnostic,
			"disconnect replaced diagnostic") == 0);
	} else {
		CHECK(mh_net_session_leave(client_session, NULL) ==
			MH_ERROR_NONE);
		step_pair(&fixture, host, client);
		CHECK(client_session->phase == MH_NET_CLOSED);
		CHECK(host_session->phase == MH_NET_LOBBY);
	}

	CHECK(fixture.sends != 0);

done:
	mh_network_close(client);
	mh_network_close(host);
	CHECK(fixture.opens == fixture.closes);
	CHECK(fixture.targets == fixture.released_targets);
}

static void
patched_loading_boundary(void)
{
	struct fixture fixture;
	struct mh_transport_provider provider;
	struct mh_net_session_config settings;
	struct mh_network *host, *client;
	struct mh_net_session *session;
	struct mh_network_issue issue;
	struct mh_net_message ping;
	unsigned char packet[8];
	size_t size;
	unsigned int polls;

	memset(&fixture, 0, sizeof(fixture));
	host = client = NULL;
	CHECK(mh_transport_provider_init(&provider, &ops, NULL, NULL, &fixture,
		NULL) == MH_ERROR_NONE);
	settings = session_config(0);
	settings.provider = MH_NET_PROVIDER_LANINET;
	settings.wire = MH_NET_WIRE_31_LOBBY;
	CHECK(open_network(&provider, 1, &settings, &host, NULL) ==
		MH_ERROR_NONE);
	strcpy(settings.profile.name, "Client");
	CHECK(open_network(&provider, 0, &settings, &client, NULL) ==
		MH_ERROR_NONE);

	if (host == NULL || client == NULL)
		goto done;

	complete_connection(&fixture);
	step_pair(&fixture, host, client);
	session = mh_network_session(host);
	CHECK(mh_net_session_begin_race(session, fixture.now, NULL) ==
		MH_ERROR_NONE);
	polls = fixture.polls;
	CHECK(mh_network_flush(host, &issue, NULL) == MH_ERROR_NONE);
	CHECK(issue.reason.code == MH_ERROR_NONE && fixture.polls == polls);
	CHECK(fixture.endpoints[1]->count == 1);
	memset(&ping, 0, sizeof(ping));
	ping.type = MH_NET_PING;
	ping.stamp = 70000;
	CHECK(mh_net_wire_encode(settings.wire, &ping, packet, sizeof(packet),
		&size, NULL) == MH_ERROR_NONE);
	CHECK(queue_event(fixture.endpoints[1], MH_TRANSPORT_RECEIVE, packet,
		size, 0, NULL) == MH_ERROR_NONE);
	session = mh_network_session(client);
	session->contact_ms = 1;
	fixture.now = 70000;
	CHECK(mh_network_drain(client, 16, &issue, NULL) == MH_ERROR_NONE);
	CHECK(issue.reason.code == MH_ERROR_NONE && fixture.polls == polls + 1);
	CHECK(session->phase == MH_NET_LOADING && session->contact_ms == 1);
	CHECK(fixture.endpoints[1]->count == 1 &&
		fixture.endpoints[0]->count == 0);

	/* No timeout tick or ping reply occurs between setup and loading. */
	CHECK(mh_net_session_loaded(session, fixture.now, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_network_step(client, 0, &issue, NULL) == MH_ERROR_NONE);
	CHECK(issue.reason.code == MH_ERROR_NONE &&
		session->phase == MH_NET_WAITING &&
		session->contact_ms == 70000);
	CHECK(session->replication.pending_service[0].size == 9);

done:
	mh_network_close(client);
	mh_network_close(host);
	CHECK(fixture.opens == fixture.closes);
	CHECK(fixture.targets == fixture.released_targets);
}

static void
patched_service_order(int host_side, int drain)
{
	struct fixture fixture;
	struct mh_transport_provider provider;
	struct mh_net_session_config settings;
	struct mh_network *host, *client;
	struct mh_net_session *session;
	struct mh_network_issue issue;
	struct mh_net_message incoming;
	struct mh_net_vehicle vehicle;
	const struct mh_transport_event *sent;
	double positions[MH_NET_PLAYERS][3];
	unsigned char packet[9];
	size_t size;
	unsigned int i, replies, local, remote;
	double sent_ms;

	memset(&fixture, 0, sizeof(fixture));
	host = client = NULL;
	CHECK(mh_transport_provider_init(&provider, &ops, NULL, NULL, &fixture,
		NULL) == MH_ERROR_NONE);
	settings = session_config(0);
	settings.provider = 4;
	settings.wire = MH_NET_WIRE_31_LOBBY;
	CHECK(open_network(&provider, 1, &settings, &host, NULL) ==
		MH_ERROR_NONE);
	strcpy(settings.profile.name, "Client");
	CHECK(open_network(&provider, 0, &settings, &client, NULL) ==
		MH_ERROR_NONE);

	if (host == NULL || client == NULL)
		goto done;

	complete_connection(&fixture);
	step_pair(&fixture, host, client);
	local = host_side ? 0 : 1;
	remote = 1 - local;
	session = mh_network_session(host_side ? host : client);
	CHECK(session->phase == MH_NET_LOBBY && session->player == local);
	CHECK(fixture.endpoints[0]->count == 0 &&
		fixture.endpoints[1]->count == 0);

	/* Isolate loaded service with an explicit racing roster. */
	session->phase = MH_NET_LOADING;
	session->roster.players[0].flags = MH_NET_FLAG_RACING;
	session->roster.players[1].flags = MH_NET_FLAG_RACING;

	if (host_side)
		for (i = 0; i < 2; i++) {
			session->lobby.slots[i].phase = MH_NET_IN_RACE;
			session->lobby.slots[i].profile.flags =
				MH_NET_FLAG_RACING;
		}

	memset(positions, 0, sizeof(positions));
	memset(&vehicle, 0, sizeof(vehicle));
	CHECK(mh_net_session_configure_replication(session, 180,
		(const double (*)[3])positions, NULL) == MH_ERROR_NONE);
	CHECK(mh_net_session_publish(session, &vehicle, local, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_net_session_loaded(session, fixture.now, NULL) ==
		MH_ERROR_NONE);
	memset(&incoming, 0, sizeof(incoming));
	incoming.type = host_side ? MH_NET_PONG : MH_NET_PING;
	incoming.player = remote;
	replies = drain ? 2 : 1;

	for (i = 0; i < replies; i++) {
		incoming.stamp = 100 + i;
		CHECK(mh_net_wire_encode(settings.wire, &incoming, packet,
			sizeof(packet), &size, NULL) == MH_ERROR_NONE);
		CHECK(queue_event(fixture.endpoints[local],
			MH_TRANSPORT_RECEIVE, packet, size, 0,
			NULL) == MH_ERROR_NONE);
	}

	sent_ms = host_side ? 1001 : 1000;
	fixture.now = sent_ms;
	fixture.poll_elapsed = 100;

	if (drain)
		CHECK(mh_network_drain(host_side ? host : client, 16, &issue,
			NULL) == MH_ERROR_NONE);
	else
		CHECK(mh_network_step(host_side ? host : client, 0, &issue,
			NULL) == MH_ERROR_NONE);

	CHECK(issue.reason.code == MH_ERROR_NONE);
	CHECK(fixture.endpoints[remote]->count == 1);
	sent = &fixture.endpoints[remote]
			->events[fixture.endpoints[remote]->head];
	CHECK(!sent->reliable);
	CHECK(session->replication.sent_ms[1] == (unsigned long)sent_ms);

	if (host_side) {
		/*
		 * This poll's readiness must not add cars to the earlier send.
		 */
		CHECK(sent->size == 103 && sent->data[0] == MH_NET_PING &&
			sent->data[8] == MH_NET_STATUS);
		CHECK(mh_read_u32le(sent->data + 4) == (unsigned long)sent_ms);
		CHECK(session->ready_mask == 3);
	} else {
		CHECK(sent->size == 40 && sent->data[0] == MH_NET_CAR);
		CHECK(mh_read_u32le(sent->data + 6) == (unsigned long)sent_ms);
		CHECK(session->replication.pending_service[0].size ==
			9 * replies);
	}

	CHECK(mh_network_flush(host_side ? host : client, &issue, NULL) ==
		MH_ERROR_NONE);
	CHECK(fixture.endpoints[remote]->count == 1);
	fixture.poll_elapsed = 0;
	CHECK(mh_network_step(host_side ? host : client, 0, &issue, NULL) ==
		MH_ERROR_NONE);
	CHECK(issue.reason.code == MH_ERROR_NONE);
	CHECK(fixture.endpoints[remote]->count == 2);
	sent = &fixture.endpoints[remote]
			->events[(fixture.endpoints[remote]->head + 1) %
				EVENT_LIMIT];

	if (host_side)
		replies = 0;

	CHECK(sent->size == 40 + 9 * replies && !sent->reliable);

	for (i = 0; i < replies; i++) {
		CHECK(sent->data[9 * i] == MH_NET_PONG);
		CHECK(mh_read_u32le(sent->data + 9 * i + 5) == 100 + i);
	}

	CHECK(sent->data[9 * replies] == MH_NET_CAR);
	CHECK(mh_read_u32le(sent->data + 9 * replies + 6) ==
		(unsigned long)fixture.now);
	CHECK(session->replication.pending_service[0].size == 0);

done:
	mh_network_close(client);
	mh_network_close(host);
	CHECK(fixture.opens == fixture.closes);
	CHECK(fixture.targets == fixture.released_targets);
}

static void
ownership_round(void)
{
	struct fixture first, second;
	struct mh_transport_provider first_provider, second_provider, invalid;
	struct mh_transport_ops incomplete;
	struct mh_transport_address address, saved_address;
	struct mh_transport_target *target, *unchanged;
	struct mh_transport *first_transport, *second_transport, *output;
	struct mh_transport_event event;
	struct mh_network *network;
	struct mh_net_session_config settings;
	struct mh_error error;
	int done;

	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	CHECK(mh_transport_provider_init(&first_provider, &ops, NULL, NULL,
		&first, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_provider_init(&second_provider, &ops, NULL, NULL,
		&second, NULL) == MH_ERROR_NONE);
	invalid = first_provider;
	incomplete = ops;
	incomplete.send = NULL;
	CHECK(mh_transport_provider_init(&invalid, &incomplete, NULL, NULL,
		&second, NULL) == MH_ERROR_INVALID);
	CHECK(invalid.ops == first_provider.ops && invalid.context == &first);
	first_transport = second_transport = NULL;
	CHECK(mh_transport_open_from(&first_provider, 1, NULL, &first_transport,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_open_from(&second_provider, 1, NULL,
		&second_transport, NULL) == MH_ERROR_NONE);
	output = first_transport;
	CHECK(mh_transport_open_from(NULL, 1, NULL, &output, NULL) ==
			MH_ERROR_UNSUPPORTED &&
		output == first_transport);
	target = NULL;
	CHECK(make_target(&second_provider, &target, NULL) == MH_ERROR_NONE);
	unchanged = target;
	CHECK(mh_transport_target_address(&first_provider, "unused", 0, &target,
		NULL) == MH_ERROR_UNSUPPORTED &&
		target == unchanged);
	CHECK(mh_transport_open_from(&first_provider, 0, target, &output,
		NULL) == MH_ERROR_INVALID &&
		output == first_transport);
	CHECK(mh_transport_connect(first_transport, target, NULL) ==
		MH_ERROR_INVALID);
	CHECK(first.attempts == 1 && first.opens == 1);
	mh_transport_target_close(target);
	CHECK(second.targets == 1 && second.released_targets == 1);

	first.fail_open = 1;
	CHECK(mh_transport_open_from(&first_provider, 0, NULL, &output, NULL) ==
			MH_ERROR_BACKEND &&
		output == first_transport);
	first.fail_open = 0;

	if (first_transport != NULL && second_transport != NULL) {
		first.now = 123;
		second.now = 456;
		CHECK(mh_transport_now(first_transport) == 123 &&
			mh_transport_now(second_transport) == 456);
		memset(&event, 0, sizeof(event));
		event.peer = 99;
		CHECK(mh_transport_poll(first_transport, 1, &event, NULL) ==
			MH_ERROR_UNSUPPORTED);
		CHECK(event.peer == 99 && first.polls == 0);
		CHECK(mh_transport_poll(first_transport, 0, &event, NULL) ==
			MH_ERROR_NONE);
		CHECK(event.type == MH_TRANSPORT_NONE && first.polls == 1);
		memset(&address, 0, sizeof(address));
		strcpy(address.host, "unchanged");
		address.port = 42;
		saved_address = address;
		CHECK(mh_transport_peer_address(first_transport, 1, &address,
			NULL) == MH_ERROR_UNSUPPORTED);
		CHECK(!memcmp(&address, &saved_address, sizeof(address)));
		done = 42;
		CHECK(mh_transport_search_begin(first_transport, NULL, 1,
			NULL) == MH_ERROR_UNSUPPORTED);
		CHECK(mh_transport_search_poll(first_transport, &done, NULL) ==
			MH_ERROR_UNSUPPORTED);
		CHECK(done == 42 &&
			mh_transport_search_results(first_transport) == NULL);
		CHECK(mh_transport_advertise(first_transport, NULL, NULL) ==
			MH_ERROR_UNSUPPORTED);
	}

	mh_transport_close(first_transport);
	CHECK(first.opens == first.closes && second.closes == 0);
	mh_transport_close(second_transport);
	CHECK(second.opens == second.closes);

	settings = session_config(1);

	if (mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) == NULL) {
		settings.wire = MH_NET_WIRE_31_LOBBY;
		settings.simulation_rate = 120;
	}

	network = NULL;
	first.fail_connect = 1;
	CHECK(open_network(&first_provider, 0, &settings, &network, &error) ==
		MH_ERROR_BACKEND);
	CHECK(network == NULL && first.opens == first.closes);
	CHECK(error.message != NULL &&
		strcmp(error.message, "test connection refused") == 0);
	first.fail_connect = 0;
	CHECK(open_network(&first_provider, 0, &settings, &network, NULL) ==
		MH_ERROR_NONE);

	/*
	 * Cancel before asynchronous connection completion, then reuse
	 * provider.
	 */
	mh_network_close(network);
	CHECK(first.endpoints[1] == NULL && first.opens == first.closes);
	network = NULL;
	CHECK(open_network(&first_provider, 0, &settings, &network, NULL) ==
		MH_ERROR_NONE);
	mh_network_close(network);
	CHECK(first.opens == first.closes);
	CHECK(first.targets == first.released_targets);
}

#ifdef MH_TEST_HOST_NETWORK
static void
frontend_ownership(void)
{
	static struct mh_application app;
	struct fixture fixture;
	struct mh_transport_provider provider;
	struct mh_transport_target *first, *second;
	struct mh_race_frontend *race;
	struct mh_race_request request;
	struct mh_multiplayer_snapshot snapshot;
	struct mh_car car;
	struct mh_track track;
	struct mh_control_profile profile;

	memset(&app, 0, sizeof(app));
	memset(&fixture, 0, sizeof(fixture));
	memset(&car, 0, sizeof(car));
	memset(&track, 0, sizeof(track));
	memset(&profile, 0, sizeof(profile));
	strcpy(car.name, "Kobra");
	strcpy(track.name, "Fixture");
	strcpy(profile.layout.name, "Keyboard");
	CHECK(mh_application_use_race_frontend(&app, NULL) == MH_ERROR_NONE);
	race = app.race_frontend;
	race->cars.entries = &car;
	race->cars.count = 1;
	race->tracks.entries = &track;
	race->tracks.count = 1;
	race->controls.profiles = &profile;
	race->controls.count = 1;
	race->car_available[0] = race->track_available[0] = 1;
	race->horns.count = 1;
	strcpy(race->horns.horns[0].name, "Horn");
	race->state = MH_FRONTEND_READY;
	app.phase = APPLICATION_RACE_FRONTEND;
	app.status = MH_APP_RUNNING;
	app.preferences = &app.defaults;
	memset(&request, 0, sizeof(request));
	strcpy(request.name, "Driver");
	request.car = request.track = request.laps = 1;
	request.width = 640;
	request.height = 480;
	request.renderer = mh_render_choice_default();
	request.audio_profile = mh_race_audio_default();
	CHECK(mh_transport_provider_init(&provider, &ops, NULL, NULL, &fixture,
		NULL) == MH_ERROR_NONE);
	CHECK(make_target(&provider, &first, NULL) == MH_ERROR_NONE);
	CHECK(make_target(&provider, &second, NULL) == MH_ERROR_NONE);
	CHECK(!mh_application_network_available(&app));
	CHECK(mh_application_join_network(&app, first, &request, "", NULL) ==
		MH_ERROR_UNSUPPORTED);
	CHECK(fixture.released_targets == 0);
	app.session_options.network = &provider;
	CHECK(mh_application_network_available(&app));
	CHECK(mh_application_join_network(&app, first, &request, "", NULL) ==
		MH_ERROR_NONE);
	mh_application_network_snapshot(&app, &snapshot);
	CHECK(snapshot.state == MH_NETWORK_CONNECTING &&
		snapshot.chat_count == 0);
	CHECK(fixture.opens == 0);
	CHECK(mh_application_join_network(&app, second, &request, "", NULL) ==
		MH_ERROR_STATE);
	CHECK(mh_application_start_race(&app, &request, NULL) ==
		MH_ERROR_STATE);
	CHECK(mh_application_bind_key(&app, 0, 0, "A", NULL) == MH_ERROR_STATE);
	CHECK(mh_application_network_player(&app, "Name", "", 0xffffff, NULL) ==
		MH_ERROR_STATE);
	CHECK(mh_application_network_show_fps(&app, 1, NULL) == MH_ERROR_STATE);
	CHECK(mh_application_network_chat(&app, "early", NULL, NULL) ==
		MH_ERROR_STATE);
	CHECK(mh_application_leave_network(&app, NULL) == MH_ERROR_NONE);
	CHECK(mh_multiplayer_step(&app) == MH_APP_RUNNING);
	mh_application_network_snapshot(&app, &snapshot);
	CHECK(snapshot.state == MH_NETWORK_OFFLINE &&
		fixture.released_targets == 1);
	CHECK(fixture.opens == 0 && mh_race_frontend_idle(&app));
	CHECK(mh_application_join_network(&app, second, &request, "", NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_application_network_active(&app));
	mh_multiplayer_suspend(&app);
	mh_application_network_snapshot(&app, &snapshot);
	CHECK(snapshot.state == MH_NETWORK_FAILED);
	CHECK(!mh_application_network_active(&app));
	CHECK(strstr(mh_application_race_error(&app), "suspended") != NULL);
	mh_multiplayer_close(&app);
	CHECK(fixture.targets == fixture.released_targets &&
		fixture.opens == 0);
	free(race);
	app.race_frontend = NULL;
}

/* The selected provider also reaches the actual graphical session owner. */
static void
host_selection(void)
{
	static const struct mh_window_system windows = {0};
	static const struct mh_canvas_provider canvases = {0};
	static const struct mh_render_set renderers = {0};
	static const struct mh_input_provider inputs = {0};
	struct mh_game_timer timer;
	struct fixture fixture;
	struct mh_transport_target *target;
	struct mh_transport_provider provider;
	struct mh_session_options options;
	struct mh_network_game_request request;
	struct mh_network_game_control control;
	struct mh_network *host;
	struct mh_net_session_config host_config;
	struct mh_network_issue issue;
	struct mh_horn_catalog horns;
	const char *cars[1], *tracks[1];
	unsigned int i;
	struct mh_lobby_settings_catalog catalog;
	struct mh_settings settings;
	struct mh_menu_audio *audio;
	struct mh_activity activity;
	struct mh_error error;
	unsigned long random;
	int quit;

	memset(&fixture, 0, sizeof(fixture));
	memset(&options, 0, sizeof(options));
	memset(&request, 0, sizeof(request));
	memset(&catalog, 0, sizeof(catalog));
	memset(&settings, 0, sizeof(settings));
	memset(&timer, 0, sizeof(timer));
	CHECK(mh_transport_provider_init(&provider, &ops, NULL, NULL, &fixture,
		NULL) == MH_ERROR_NONE);
	options.network = &provider;
	options.windows = &windows;
	options.canvases = &canvases;
	options.renderers = &renderers;
	options.inputs = &inputs;
	options.resources = mh_filesystem_resources();
	options.timer = &timer;
	CHECK(make_target(&provider, &target, NULL) == MH_ERROR_NONE);
	request.remote = target;
	request.name = "Provider";
	request.track_limit = 1;
	request.config = session_config(1);
	audio = NULL;
	random = 17;
	quit = 0;
	fixture.fail_open = 1;
	mh_activity_init(&activity);
	CHECK(mh_network_game_open("missing-provider-assets", &request,
		&options, &catalog, &settings, &random, &audio, &quit,
		&activity, NULL) == MH_ERROR_NONE);
	CHECK(fixture.attempts == 1 && fixture.opens == 0);
	CHECK(mh_activity_step(&activity) == MH_APP_FAILED);
	CHECK(mh_activity_error(&activity, &error) == MH_ERROR_BACKEND);
	CHECK(strcmp(error.message, "test open refused") == 0);
	mh_activity_close(&activity);

	/*
	 * Failed asset preparation retains then cancels a pending connection.
	 */
	fixture.fail_open = 0;
	CHECK(mh_network_game_open("missing-provider-assets", &request,
		&options, &catalog, &settings, &random, &audio, &quit,
		&activity, NULL) == MH_ERROR_NONE);
	CHECK(fixture.opens == 1 && fixture.closes == 0);
	CHECK(fixture.endpoints[1] != NULL && fixture.endpoints[1]->connecting);
	mh_activity_close(&activity);
	CHECK(fixture.closes == 1 && fixture.endpoints[1] == NULL);

	/*
	 * External lobbies need no menu assets and retain the same connection.
	 */
	memset(&control, 0, sizeof(control));
	memset(&horns, 0, sizeof(horns));
	horns.count = 1;
	strcpy(horns.horns[0].name, "Horn1.chf");
	cars[0] = "Kobra.car";
	tracks[0] = "Track1.trk";
	catalog.cars.names = cars;
	catalog.cars.count = 1;
	catalog.tracks.names = tracks;
	catalog.tracks.count = 1;
	catalog.horns = &horns;
	request.control = &control;
	host_config = session_config(0);
	CHECK(open_network(&provider, 1, &host_config, &host, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_network_game_open("missing-provider-assets", &request,
		&options, &catalog, &settings, &random, &audio, &quit,
		&activity, NULL) == MH_ERROR_NONE);
	CHECK(control.network != NULL);
	complete_connection(&fixture);

	for (i = 0; i < STEP_LIMIT; i++) {
		fixture.now += 10;
		CHECK(mh_network_step(host, 0, &issue, NULL) == MH_ERROR_NONE);
		CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	}

	CHECK(mh_network_session(control.network)->phase == MH_NET_LOBBY);
	CHECK(mh_net_session_chat(mh_network_session(control.network),
		"external lobby chat", 0, NULL) == MH_ERROR_NONE);

	for (i = 0; i < STEP_LIMIT; i++) {
		fixture.now += 10;
		CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
		CHECK(mh_network_step(host, 0, &issue, NULL) == MH_ERROR_NONE);
	}

	CHECK(strcmp(mh_network_session(host)
			->chat[mh_network_session(host)->chat_count - 1]
			.text,
		"Client: external lobby chat") == 0);
	control.leave = 1;
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	fixture.now += 300;
	CHECK(mh_activity_step(&activity) == MH_APP_DONE);
	CHECK(control.network != NULL);
	mh_activity_close(&activity);
	CHECK(control.network == NULL && !control.racing);
	mh_network_close(host);

	/* Cancel an asynchronous admission without touching frontend assets. */
	memset(&control, 0, sizeof(control));
	CHECK(mh_network_game_open("missing-provider-assets", &request,
		&options, &catalog, &settings, &random, &audio, &quit,
		&activity, NULL) == MH_ERROR_NONE);
	CHECK(control.network != NULL);
	mh_activity_close(&activity);
	CHECK(control.network == NULL && fixture.opens == fixture.closes);

	/* Admission is timed from this attempt, not the provider's epoch. */
	memset(&control, 0, sizeof(control));
	fixture.now = 500000;
	CHECK(mh_network_game_open("missing-provider-assets", &request,
		&options, &catalog, &settings, &random, &audio, &quit,
		&activity, NULL) == MH_ERROR_NONE);
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	fixture.now += 119999;
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	fixture.now++;
	CHECK(mh_activity_step(&activity) == MH_APP_RUNNING);
	fixture.now += 300;
	CHECK(mh_activity_step(&activity) == MH_APP_FAILED);
	CHECK(mh_activity_error(&activity, &error) == MH_ERROR_STATE);
	CHECK(strcmp(error.message, "network admission timed out") == 0);
	mh_activity_close(&activity);
	CHECK(control.network == NULL && fixture.opens == fixture.closes);

	/*
	 * Backend diagnostics survive connection teardown and later mutations.
	 */
	memset(&control, 0, sizeof(control));
	fixture.fail_connect = 1;
	CHECK(mh_network_game_open("missing-provider-assets", &request,
		&options, &catalog, &settings, &random, &audio, &quit,
		&activity, NULL) == MH_ERROR_NONE);
	strcpy(fixture.diagnostic, "replaced backend diagnostic");
	CHECK(mh_activity_step(&activity) == MH_APP_FAILED);
	CHECK(mh_activity_error(&activity, &error) == MH_ERROR_BACKEND);
	CHECK(strcmp(error.message, "test connection refused") == 0);
	mh_activity_close(&activity);
	CHECK(control.network == NULL && fixture.opens == fixture.closes);
	fixture.fail_connect = 0;

	/* A compiled native backend does not override unavailable selection. */
	options.network = NULL;
	CHECK(mh_network_game_open("missing-provider-assets", &request,
		&options, &catalog, &settings, &random, &audio, &quit,
		&activity, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(!mh_activity_is_open(&activity));
	CHECK(fixture.opens == fixture.closes && audio == NULL);
	mh_transport_target_close(target);
	CHECK(fixture.targets == fixture.released_targets);
}
#endif

int
main(void)
{
	endpoint_parsing();
	ownership_round();
	message_capacity();

	if (mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) == NULL)
		return failures != 0;

#ifdef MH_TEST_HOST_NETWORK
	frontend_ownership();
	host_selection();
#endif
	session_round(MH_ERROR_NONE);
	session_round(MH_ERROR_BACKEND);
	session_round(MH_ERROR_RANGE);
	patched_loading_boundary();
	patched_service_order(0, 0);
	patched_service_order(0, 1);
	patched_service_order(1, 0);
	patched_service_order(1, 1);

	return failures == 0 ? 0 : 1;
}
