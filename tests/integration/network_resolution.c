#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "backends/clock/system_provider.h"
#include "backends/network/posix_provider.h"
#include "backends/network/udp_transport_provider.h"
#include "backends/network/windows_provider.h"
#include "interfaces/clock/clock.h"
#include "interfaces/network/address.h"
#include "interfaces/network/backend.h"
#include "interfaces/network/resolver_backend.h"

#define LOOPBACK 0x7f000001UL
#define POLL_LIMIT 5000
/* Failed setup must not leave later checks using released transport handles. */
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "network resolution line %d: %s\n",    \
				__LINE__, #e);                                 \
			abort();                                               \
		}                                                              \
	} while (0)

struct fixture {
	unsigned int	opened, closed;
	int		ready, fail;
};

static enum mh_error_code
begin_lookup(void *context, const char *host, void **output,
	struct mh_error *error_out)
{
	struct fixture *fixture;
	char *name;

	fixture = context;
	name = malloc(strlen(host) + 1);

	if (name == NULL)
		return mh_error_set(
			error_out, MH_ERROR_MEMORY, "test allocation failed");

	strcpy(name, host);
	fixture->opened++;
	*output = name;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
poll_lookup(void *context, void *request, int *done, unsigned long *address,
	struct mh_error *error_out)
{
	struct fixture *fixture;

	fixture = context;
	CHECK(strcmp(request, "pending.example") == 0);

	if (fixture->fail)
		return mh_error_set(
			error_out, MH_ERROR_NOT_FOUND, "test name not found");

	if (fixture->ready)
		*address = LOOPBACK;

	*done = fixture->ready;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_lookup(void *context, void *request)
{
	struct fixture *fixture;

	fixture = context;
	fixture->closed++;
	free(request);
}

static const struct mh_resolver_ops resolver_ops = {
	begin_lookup, poll_lookup, close_lookup};

static void
pause_poll(void)
{
	CHECK(mh_clock_source_delay(mh_system_clock_source(), 1, NULL) ==
		MH_ERROR_NONE);
}

static void
connect_pair(const struct mh_transport_provider *provider,
	struct mh_transport *host, const char *name, struct fixture *fixture)
{
	struct mh_transport *client;
	struct mh_transport_event event;
	unsigned int i;
	unsigned long peer;
	int connected, received;
	unsigned char bytes[3];

	client = NULL;
	peer = 0;
	connected = received = 0;
	CHECK(mh_transport_open_address(provider, 0, NULL, 0, &client, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_transport_connect_address(client, name,
		mh_transport_port(host), NULL) == MH_ERROR_NONE);

	if (fixture != NULL) {
		CHECK(mh_transport_poll(client, 0, &event, NULL) ==
			MH_ERROR_NONE);
		CHECK(event.type == MH_TRANSPORT_NONE);
		fixture->ready = 1;
	}

	for (i = 0; i < POLL_LIMIT && (!connected || peer == 0); i++) {
		pause_poll();
		CHECK(mh_transport_poll(client, 0, &event, NULL) ==
			MH_ERROR_NONE);

		if (event.type == MH_TRANSPORT_CONNECT) {
			CHECK(event.peer == 1);
			connected = 1;
		}

		CHECK(mh_transport_poll(host, 0, &event, NULL) ==
			MH_ERROR_NONE);

		if (event.type == MH_TRANSPORT_CONNECT)
			peer = event.peer;
	}

	CHECK(connected && peer == 10);
	bytes[0] = 11;
	bytes[1] = 22;
	bytes[2] = 33;
	CHECK(mh_transport_send(client, 1, bytes, sizeof(bytes), 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_transport_flush(client, NULL) == MH_ERROR_NONE);

	for (i = 0; i < POLL_LIMIT && !received; i++) {
		pause_poll();
		CHECK(mh_transport_poll(client, 0, &event, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_transport_poll(host, 0, &event, NULL) ==
			MH_ERROR_NONE);

		if (event.type == MH_TRANSPORT_RECEIVE) {
			CHECK(event.peer == peer &&
				event.size == sizeof(bytes) && event.reliable);
			CHECK(memcmp(event.data, bytes, sizeof(bytes)) == 0);
			received = 1;
		}
	}

	CHECK(received);
	mh_transport_close(client);
}

static void
discovery(const struct mh_transport_provider *provider,
	struct mh_transport *host, struct fixture *fixture)
{
	struct mh_transport *query;
	struct mh_transport_event event;
	struct mh_discovery_advertisement game;
	const struct mh_discovery_catalog *results;
	unsigned int i;
	int done;

	memset(&game, 0, sizeof(game));
	strcpy(game.name, "Resolved game");
	game.has_roster = 1;
	strcpy(game.roster.track, "Track1.trk");
	game.roster.player_count = 8;
	game.roster.players[0].flags = MH_NET_FLAG_LOBBY;
	strcpy(game.roster.players[0].name, "Host driver");
	game.roster.laps = 3;
	game.roster.car_count = 10;
	CHECK(mh_transport_advertise(host, &game, NULL) == MH_ERROR_NONE);
	query = NULL;
	fixture->ready = 0;
	CHECK(mh_transport_open_address(provider, 0, NULL, 0, &query, NULL) ==
		MH_ERROR_NONE);
	CHECK(mh_transport_search_address(query, "pending.example",
		mh_transport_port(host), 123, NULL) == MH_ERROR_NONE);
	done = 42;
	CHECK(mh_transport_search_poll(query, &done, NULL) == MH_ERROR_NONE);
	CHECK(done == 0 && mh_transport_search_results(query) == NULL);
	fixture->ready = 1;

	for (i = 0; i < POLL_LIMIT && !done; i++) {
		pause_poll();
		CHECK(mh_transport_poll(host, 0, &event, NULL) ==
			MH_ERROR_NONE);
		CHECK(mh_transport_search_poll(query, &done, NULL) ==
			MH_ERROR_NONE);
	}

	results = mh_transport_search_results(query);
	CHECK(done && results != NULL && results->count == 1);

	if (results != NULL && results->count != 0)
		CHECK(strcmp(results->entries[0].game.name, "Resolved game") ==
			0);

	mh_transport_close(query);
}

static void
cancellation(
	const struct mh_transport_provider *provider, struct fixture *fixture)
{
	struct mh_transport *transport;
	struct mh_transport_event event;
	struct mh_error error;
	unsigned int before;
	int done;

	fixture->ready = 0;
	before = fixture->opened;
	transport = NULL;
	CHECK(mh_transport_open_address(provider, 0, "pending.example", 0,
		&transport, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_connect_address(transport, "pending.example", 3200,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_port(transport) == 0);
	CHECK(mh_transport_flush(transport, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == MH_TRANSPORT_NONE);
	mh_transport_close(transport);
	CHECK(fixture->opened == before + 2 &&
		fixture->closed == fixture->opened);

	CHECK(mh_transport_open_address(
		provider, 0, NULL, 0, &transport, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_connect_address(transport, "pending.example", 3200,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_disconnect(transport, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_poll(transport, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == MH_TRANSPORT_DISCONNECT && event.peer == 1);
	mh_transport_close(transport);

	CHECK(mh_transport_open_address(
		provider, 0, NULL, 0, &transport, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_search_address(transport, "pending.example", 3200, 1,
		NULL) == MH_ERROR_NONE);
	fixture->fail = 1;
	done = 42;
	CHECK(mh_transport_search_poll(transport, &done, &error) ==
		MH_ERROR_NOT_FOUND);
	CHECK(done == 42 &&
		strcmp(error.message, "cannot resolve LAN/INET address") == 0);
	mh_transport_close(transport);
	CHECK(strcmp(error.message, "cannot resolve LAN/INET address") == 0);
	CHECK(fixture->opened == fixture->closed);
	fixture->fail = 0;
	CHECK(mh_transport_open_address(provider, 1, "pending.example", 0,
		&transport, NULL) == MH_ERROR_NONE);
	fixture->fail = 1;
	event.peer = 99;
	CHECK(mh_transport_poll(transport, 0, &event, &error) ==
		MH_ERROR_NOT_FOUND);
	CHECK(event.peer == 99 &&
		strcmp(error.message, "cannot resolve LAN/INET address") == 0);
	mh_transport_close(transport);
	CHECK(strcmp(error.message, "cannot resolve LAN/INET address") == 0);

	fixture->fail = 0;
	CHECK(mh_transport_open_address(
		provider, 0, NULL, 0, &transport, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_connect_address(transport, "pending.example", 3200,
		NULL) == MH_ERROR_NONE);
	fixture->fail = 1;
	CHECK(mh_transport_poll(transport, 0, &event, &error) ==
		MH_ERROR_NOT_FOUND);
	CHECK(strcmp(error.message, "cannot resolve LAN/INET address") == 0);
	mh_transport_close(transport);
	CHECK(strcmp(error.message, "cannot resolve LAN/INET address") == 0);
	CHECK(fixture->opened == fixture->closed);
	fixture->fail = 0;
}

int
main(void)
{
	struct fixture fixture;
	struct mh_resolver_provider resolver;
	struct mh_transport_provider provider;
	struct mh_udp_transport_config config;
	struct mh_transport_target *target;
	char address[32];
	struct mh_transport *host;
	struct mh_transport_event event;
	unsigned int i;

	memset(&fixture, 0, sizeof(fixture));
	CHECK(mh_resolver_provider_init(&resolver, &resolver_ops, &fixture,
		NULL) == MH_ERROR_NONE);
	memset(&config, 0, sizeof(config));
	config.clock = mh_system_clock_source();
	config.resolver = &resolver;
	config.port_policy = MH_UDP_PORTS_RANGE;
	strcpy(config.name, "Resolution test");
	CHECK(mh_udp_transport_init(&provider, &config, NULL) == MH_ERROR_NONE);
	strcpy(address, "pending.example");
	CHECK(mh_transport_target_address(
		&provider, address, 0, &target, NULL) == MH_ERROR_NONE);
	strcpy(address, "changed-after-prepare");
	host = NULL;
	CHECK(mh_transport_open_from(&provider, 1, target, &host, NULL) ==
		MH_ERROR_NONE);
	mh_transport_target_close(target);
	CHECK(mh_transport_port(host) == 0);
	CHECK(mh_transport_poll(host, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(event.type == MH_TRANSPORT_NONE && fixture.closed == 0);
	fixture.ready = 1;
	CHECK(mh_transport_poll(host, 0, &event, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_port(host) != 0 && fixture.closed == 1);
	fixture.ready = 0;
	connect_pair(&provider, host, "pending.example", &fixture);
	discovery(&provider, host, &fixture);
	mh_transport_close(host);
	CHECK(fixture.opened == fixture.closed);
	cancellation(&provider, &fixture);

	/*
	 * Actual native lookup, without an external DNS server or test
	 * interposition.
	 */
#ifdef _WIN32
	config.resolver = mh_windows_resolver_provider();
#else
	config.resolver = mh_posix_resolver_provider();
#endif
	CHECK(mh_udp_transport_init(&provider, &config, NULL) == MH_ERROR_NONE);
	host = NULL;
	CHECK(mh_transport_open_address(&provider, 1, "localhost", 0, &host,
		NULL) == MH_ERROR_NONE);

	for (i = 0; i < POLL_LIMIT && mh_transport_port(host) == 0; i++) {
		CHECK(mh_transport_poll(host, 0, &event, NULL) ==
			MH_ERROR_NONE);
		pause_poll();
	}

	CHECK(mh_transport_port(host) != 0);
	connect_pair(&provider, host, "localhost", NULL);
	mh_transport_close(host);

	return 0;
}
