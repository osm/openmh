#define _POSIX_C_SOURCE 200112L

#include <sys/socket.h>

#include <arpa/inet.h>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "backends/network/posix_provider.h"
#include "backends/network/udp_control.h"
#include "backends/network/udp_description.h"
#include "backends/network/udp_provider.h"
#include "backends/network/udp_shared.h"
#include "backends/network/udp_transport_provider.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"
#include "engine/network/protocol_profile.h"
#include "interfaces/clock/backend.h"
#include "interfaces/network/address.h"

static int failures;
static unsigned long ticks;

struct filter_state {
	int				allowed;
	unsigned int			calls;
	struct mh_transport_address	address;
	double				now_ms;
};

static int
filter_address(void *context, const struct mh_transport_address *address,
	double now_ms)
{
	struct filter_state *state;

	state = context;
	state->calls++;
	state->address = *address;
	state->now_ms = now_ms;

	return state->allowed;
}

#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "line %d: %s\n", __LINE__, #e);        \
			failures++;                                            \
		}                                                              \
	} while (0)

static unsigned long
clock_ticks(void *context)
{
	(void)context;

	return ticks;
}

static struct mh_clock_counter
clock_counter(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = ticks;
	value.high = 0;

	return value;
}

static struct mh_clock_counter
clock_frequency(void *context)
{
	struct mh_clock_counter value;

	(void)context;
	value.low = 1000;
	value.high = 0;

	return value;
}

static struct mh_udp_wait socket_wait;

static void
initialize_provider(struct mh_udp_transport_config *config,
	struct mh_transport_provider *output)
{
	static const struct mh_clock_ops ops = {
		clock_counter, clock_frequency, clock_ticks, NULL, NULL};

	static const struct mh_clock_source source = {&ops, NULL};

	memset(config, 0, sizeof(*config));
	config->port_policy =
		mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) != NULL
		? MH_UDP_PORTS_OFFSET
		: MH_UDP_PORTS_SHARED;
	config->clock = &source;
	config->wait = &socket_wait;
	config->resolver = mh_posix_resolver_provider();
	strcpy(config->name, "Fixture");
	CHECK(mh_udp_transport_init(output, config, NULL) == MH_ERROR_NONE);
}

static int
poll_event(struct mh_transport *transport, struct mh_transport_event *event)
{
	struct mh_error error;

	memset(event, 0, sizeof(*event));

	if (mh_transport_poll(transport, 0, event, &error) != MH_ERROR_NONE) {
		fprintf(stderr, "transport poll: %s\n", error.message);
		failures++;
	}

	return event->type;
}

static int
raw_socket(struct sockaddr_in *address, unsigned int port)
{
	int fd;
	socklen_t length;

	fd = socket(AF_INET, SOCK_DGRAM, 0);

	if (fd < 0)
		return -1;

	memset(address, 0, sizeof(*address));
	address->sin_family = AF_INET;
	address->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	address->sin_port = htons((unsigned short)port);
	length = sizeof(*address);

	if (fcntl(fd, F_SETFL, O_NONBLOCK) < 0 ||
		bind(fd, (struct sockaddr *)address, sizeof(*address)) < 0 ||
		getsockname(fd, (struct sockaddr *)address, &length) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

static void
raw_send(int fd, const struct sockaddr_in *destination,
	const unsigned char *data, size_t size)
{
	CHECK(sendto(fd, data, size, 0, (const struct sockaddr *)destination,
		sizeof(*destination)) == (ssize_t)size);
}

static size_t
raw_receive(int fd, struct sockaddr_in *source, unsigned char *data,
	size_t capacity)
{
	socklen_t length;
	ssize_t count;

	length = sizeof(*source);
	count = recvfrom(
		fd, data, capacity, 0, (struct sockaddr *)source, &length);

	return count < 0 ? 0 : (size_t)count;
}

static void
prepare_advertisement(struct mh_discovery_advertisement *game)
{
	memset(game, 0, sizeof(*game));
	strcpy(game->name, "Advertised");
	game->has_roster = 1;
	strcpy(game->roster.track, "Fixture Track");
	game->roster.player_count = 8;
	game->roster.laps = 5;
	game->roster.car_count = 10;
	game->roster.players[0].flags = MH_NET_FLAG_LOBBY;
	strcpy(game->roster.players[0].name, "Host driver");
}

static void
native_search(struct mh_transport *host, struct mh_transport *client)
{
	struct mh_discovery_advertisement game;
	char endpoint[MH_TRANSPORT_ADDRESS_BYTES];
	const struct mh_discovery_catalog *catalog;
	struct mh_transport_event event;
	unsigned int i;
	int done;

	prepare_advertisement(&game);
	CHECK(mh_transport_advertise(host, &game, NULL) == MH_ERROR_NONE);
	sprintf(endpoint, "127.0.0.1:%u", mh_transport_port(host));
	CHECK(mh_transport_search_address(client, endpoint, 1, 1, NULL) ==
		MH_ERROR_NONE);
	done = 0;

	for (i = 0; i < 100 && !done; i++) {
		CHECK(mh_transport_search_poll(client, &done, NULL) ==
			MH_ERROR_NONE);
		CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
		ticks++;
	}

	CHECK(done);
	catalog = mh_transport_search_results(client);
	CHECK(catalog != NULL && catalog->count == 1);

	if (catalog != NULL && catalog->count == 1) {
		CHECK(catalog->entries[0].port == mh_transport_port(host));
		CHECK(catalog->entries[0].ipv4 == 0x7f000001UL);
		CHECK(strcmp(catalog->entries[0].game.name, "Advertised") == 0);
		CHECK(catalog->entries[0].game.password ==
			MH_DISCOVERY_PASSWORD_UNKNOWN);
		CHECK(catalog->entries[0].game.players == 1);
		CHECK(catalog->entries[0].game.has_roster == 1);
		CHECK(strcmp(catalog->entries[0].game.roster.players[0].name,
			"Host driver") == 0);
	}
}

static void
native_pair(int shared)
{
	struct mh_udp_transport_config config;
	struct mh_transport_provider provider;
	struct mh_transport *host, *client;
	struct mh_transport_event event;
	struct mh_transport_address address, saved;
	char endpoint[MH_TRANSPORT_ADDRESS_BYTES];
	unsigned char data[2206];
	unsigned int i, host_connected, client_connected, received;

	initialize_provider(&config, &provider);

	if (shared)
		config.port_policy = MH_UDP_PORTS_SHARED;

	host = NULL;
	client = NULL;
	ticks = 100;
	CHECK(mh_transport_open_address(&provider, 1, "127.0.0.1", 0, &host,
		NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_open_address(&provider, 0, "127.0.0.1", 0, &client,
		NULL) == MH_ERROR_NONE);

	if (host == NULL || client == NULL)
		goto done;

	native_search(host, client);
	sprintf(endpoint, "127.0.0.1:%u", mh_transport_port(host));
	CHECK(mh_transport_connect_address(client, endpoint, 1, NULL) ==
		MH_ERROR_NONE);
	host_connected = client_connected = 0;

	for (i = 0; i < 100 && (!host_connected || !client_connected); i++) {
		if (poll_event(client, &event) == MH_TRANSPORT_CONNECT) {
			CHECK(event.peer == 1);
			client_connected++;
		}

		if (poll_event(host, &event) == MH_TRANSPORT_CONNECT) {
			CHECK(event.peer == 10);
			host_connected++;
		}

		ticks++;
	}

	CHECK(host_connected == 1 && client_connected == 1);
	CHECK(mh_transport_peer_address(host, 10, &address, NULL) ==
		MH_ERROR_NONE);
	CHECK(!strcmp(address.host, "127.0.0.1") &&
		address.port == mh_transport_port(client));
	CHECK(mh_transport_peer_address(client, 1, &address, NULL) ==
		MH_ERROR_NONE);
	CHECK(!strcmp(address.host, "127.0.0.1") &&
		address.port ==
			(shared ? mh_transport_port(host)
				: ((mh_transport_port(host) + 10) &
					MH_U16_MAX)));
	saved = address;
	CHECK(mh_transport_peer_address(client, 10, &address, NULL) ==
		MH_ERROR_STATE);
	CHECK(!memcmp(&saved, &address, sizeof(address)));
	memset(data, 0x59, sizeof(data));
	CHECK(mh_transport_send(client, 1, data, sizeof(data), 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_RECEIVE);
	CHECK(event.peer == 10 && event.reliable && event.size == sizeof(data));
	CHECK(memcmp(event.data, data, sizeof(data)) == 0);
	CHECK(poll_event(client, &event) == MH_TRANSPORT_NONE);
	CHECK(mh_transport_send(host, 10, data, 25, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_send(host, 10, data, 1103, 1, NULL) ==
		MH_ERROR_NONE);
	received = 0;

	for (i = 0; i < 20; i++) {
		if (poll_event(client, &event) == MH_TRANSPORT_RECEIVE) {
			CHECK(event.size == (received == 0 ? 25 : 1103));
			received++;
		}

		CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
		ticks++;
	}

	CHECK(received == 2);
	CHECK(mh_transport_send(client, 1, data, 12, 0, NULL) == MH_ERROR_NONE);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_RECEIVE &&
		!event.reliable);
	CHECK(mh_transport_disconnect(host, 10, NULL) == MH_ERROR_NONE);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_DISCONNECT &&
		event.peer == 10);
	CHECK(mh_transport_peer_address(host, 10, &address, NULL) ==
		MH_ERROR_STATE);
	CHECK(!memcmp(&saved, &address, sizeof(address)));
	CHECK(mh_transport_disconnect(client, 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_peer_address(client, 1, &address, NULL) ==
		MH_ERROR_STATE);
	CHECK(!memcmp(&saved, &address, sizeof(address)));
	CHECK(poll_event(client, &event) == MH_TRANSPORT_DISCONNECT &&
		event.peer == 1);

done:
	mh_transport_close(client);
	mh_transport_close(host);
}

static void
confirm_challenge(struct mh_transport *host, int fd, struct sockaddr_in *target,
	enum mh_shared_type type, unsigned long generation)
{
	static const unsigned char padding[MH_NET_NONCE_BYTES] = {0};
	struct mh_shared_packet packet;
	struct mh_transport_event event;
	struct sockaddr_in source;
	unsigned char data[MH_SHARED_HEADER_BYTES + MH_NET_NONCE_BYTES];
	size_t size;

	memset(&packet, 0, sizeof(packet));
	packet.type = type;
	packet.client_generation = generation;
	packet.data = padding;
	packet.size = sizeof(padding);
	CHECK(mh_shared_packet_write(&packet, data, sizeof(data), &size,
		NULL) == MH_ERROR_NONE);
	raw_send(fd, target, data, size);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	size = raw_receive(fd, &source, data, sizeof(data));
	CHECK(size == sizeof(data));

	if (size != sizeof(data) ||
		mh_shared_packet_read(data, size, &packet, NULL) !=
			MH_ERROR_NONE)
		return;

	CHECK(packet.client_generation == generation);
	CHECK(packet.type ==
		(type == MH_SHARED_QUERY ? MH_SHARED_QUERY_CHALLENGE
					: MH_SHARED_CHALLENGE));
	packet.type = type == MH_SHARED_QUERY ? MH_SHARED_QUERY_CONFIRM
					: MH_SHARED_CONFIRM;
	CHECK(mh_shared_packet_write(&packet, data, sizeof(data), &size,
		NULL) == MH_ERROR_NONE);
	raw_send(fd, target, data, size);
}

static void
raw_query(struct mh_transport *host)
{
	static const unsigned char query[] = {4, 0, 4, 0};
	struct mh_discovery_advertisement game, decoded;
	struct mh_transport_event event;
	struct sockaddr_in local, target, source;
	unsigned char data[1124];
	size_t size;
	int fd;

	fd = raw_socket(&local, 0);
	CHECK(fd >= 0);

	if (fd < 0)
		return;

	target = local;
	target.sin_port = htons((unsigned short)mh_transport_port(host));
	prepare_advertisement(&game);
	CHECK(mh_transport_advertise(host, &game, NULL) == MH_ERROR_NONE);

	/* A rejected replacement must preserve the working advertisement. */
	game.has_roster = 0;
	CHECK(mh_transport_advertise(host, &game, NULL) != MH_ERROR_NONE);
	raw_send(fd, &target, query, sizeof(query));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	CHECK(raw_receive(fd, &source, data, sizeof(data)) == 0);
	confirm_challenge(host, fd, &target, MH_SHARED_QUERY, 1);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	size = raw_receive(fd, &source, data, sizeof(data));
	CHECK(size == 1123 && data[0] == 5 && data[1] == 0 && data[2] == 0x63 &&
		data[3] == 4);
	CHECK(source.sin_port == target.sin_port);
	CHECK(mh_udp_description_decode(data, size, &decoded, NULL) ==
		MH_ERROR_NONE);
	CHECK(strcmp(decoded.name, "Advertised") == 0);
	CHECK(decoded.roster.player_count == 8 && decoded.players == 1);
	CHECK(mh_transport_advertise(host, NULL, NULL) == MH_ERROR_NONE);
	raw_send(fd, &target, query, sizeof(query));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	CHECK(raw_receive(fd, &source, data, sizeof(data)) == 0);
	close(fd);
}

static void
raw_clients(void)
{
	static const unsigned char hello[] = {
		1, 0, 8, 0, 0x66, 0x66, 0xa6, 0x3f};

	static const unsigned char wrong[] = {1, 0, 8, 0, 0, 0, 0x80, 0x3f};
	struct mh_udp_transport_config config;
	struct mh_transport_filter filter;
	struct filter_state gate;
	struct mh_transport_provider provider;
	struct mh_transport *host;
	struct mh_transport_event event;
	struct mh_udp_control control;
	struct sockaddr_in local, listen_address, source, peers[12];
	unsigned char data[8193], ack;
	int clients[13];
	unsigned int i;
	size_t size;

	initialize_provider(&config, &provider);
	memset(&gate, 0, sizeof(gate));
	filter.accept = filter_address;
	filter.context = &gate;
	filter.rejection = "Address denied";
	config.filter = &filter;
	CHECK(mh_udp_transport_init(&provider, &config, NULL) == MH_ERROR_NONE);
	host = NULL;
	ticks = 100;

	for (i = 0; i < 13; i++)
		clients[i] = -1;

	CHECK(mh_transport_open_address(&provider, 1, "127.0.0.1", 0, &host,
		NULL) == MH_ERROR_NONE);

	if (host == NULL)
		goto done;

	raw_query(host);
	CHECK(gate.calls == 0);
	memset(&listen_address, 0, sizeof(listen_address));
	listen_address.sin_family = AF_INET;
	listen_address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	listen_address.sin_port =
		htons((unsigned short)mh_transport_port(host));

	for (i = 0; i < 13; i++) {
		clients[i] = raw_socket(&local, 0);
		CHECK(clients[i] >= 0);

		if (clients[i] < 0)
			goto done;
	}

	raw_send(clients[0], &listen_address, wrong, sizeof(wrong));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	size = raw_receive(clients[0], &source, data, sizeof(data));
	CHECK(mh_udp_control_decode(data, size, &control, NULL) ==
		MH_ERROR_NONE);
	CHECK(control.type == MH_UDP_REJECT &&
		source.sin_port == listen_address.sin_port);
	CHECK(strcmp((const char *)data + 4,
		"Connection denied: Wrong mhsock version, Server v1.30, Client v1.00.") ==
		0);
	CHECK(gate.calls == 0);
	raw_send(clients[0], &listen_address, hello, sizeof(hello));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	size = raw_receive(clients[0], &source, data, sizeof(data));
	CHECK(mh_udp_control_decode(data, size, &control, NULL) ==
		MH_ERROR_NONE);
	CHECK(control.type == MH_UDP_REJECT &&
		strcmp(control.rejection, "Address denied") == 0);
	CHECK(gate.calls == 1 && strcmp(gate.address.host, "127.0.0.1") == 0 &&
		gate.address.port != 0 && gate.now_ms == 0);
	gate.allowed = 1;

	for (i = 0; i < 12; i++) {
		raw_send(clients[i], &listen_address, hello, sizeof(hello));
		CHECK(poll_event(host, &event) == MH_TRANSPORT_CONNECT &&
			event.peer == i + 10);
		size = raw_receive(clients[i], &peers[i], data, sizeof(data));
		CHECK(size == 20 && data[0] == 2 && data[2] == 20);
		CHECK(strcmp((const char *)data + 4, "Advertised") == 0);
		CHECK(ntohs(peers[i].sin_port) ==
			mh_transport_port(host) + i + 10);
	}

	gate.allowed = 0;
	raw_send(clients[12], &listen_address, hello, sizeof(hello));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	size = raw_receive(clients[12], &source, data, sizeof(data));
	CHECK(mh_udp_control_decode(data, size, &control, NULL) ==
		MH_ERROR_NONE);
	CHECK(control.type == MH_UDP_REJECT &&
		strcmp(control.rejection, "Address denied") == 0);
	gate.allowed = 1;
	raw_send(clients[12], &listen_address, hello, sizeof(hello));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	size = raw_receive(clients[12], &source, data, sizeof(data));
	CHECK(size == 36 && data[0] == 3);
	CHECK(strcmp((const char *)data + 4,
		"Connection denied: Server full!") == 0);
	CHECK(mh_transport_send(host, 10, hello, sizeof(hello), 1, NULL) ==
		MH_ERROR_NONE);
	size = raw_receive(clients[0], &source, data, sizeof(data));
	CHECK(size == 9 && data[0] == 2 && memcmp(data + 1, hello, 8) == 0);
	ack = 2;

	/* Foreign socket cannot acknowledge or inject into an admitted peer. */
	raw_send(clients[1], &peers[0], &ack, 1);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	ticks += 501;
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	CHECK(raw_receive(clients[0], &source, data, sizeof(data)) == 9);
	raw_send(clients[0], &peers[0], &ack, 1);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	memset(data, 0x5a, sizeof(data));
	data[0] = 2;
	raw_send(clients[0], &peers[0], data, 8192);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_RECEIVE &&
		event.size == 8191);
	CHECK(event.reliable && event.data[8190] == 0x5a);
	CHECK(raw_receive(clients[0], &source, data, sizeof(data)) == 1 &&
		data[0] == 2);

	/*
	 * Same reliable sequence: acknowledge again, but do not deliver twice.
	 */
	data[0] = 2;
	data[1] = 42;
	raw_send(clients[0], &peers[0], data, 2);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	CHECK(raw_receive(clients[0], &source, data, sizeof(data)) == 1);

	/*
	 * Oversized datagrams must be dropped before ACK/sequence state
	 * changes.
	 */
	memset(data, 0x55, sizeof(data));
	data[0] = 3;
	raw_send(clients[0], &peers[0], data, sizeof(data));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	CHECK(raw_receive(clients[0], &source, data, sizeof(data)) == 0);
	raw_send(clients[0], &peers[0], data, 2);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_RECEIVE &&
		event.size == 1);
	CHECK(raw_receive(clients[0], &source, data, sizeof(data)) == 1 &&
		data[0] == 3);
	CHECK(mh_transport_disconnect(host, 10, NULL) == MH_ERROR_NONE);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_DISCONNECT &&
		event.peer == 10);
	raw_send(clients[12], &listen_address, hello, sizeof(hello));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_CONNECT &&
		event.peer == 10);
	CHECK(raw_receive(clients[12], &source, data, sizeof(data)) == 20);

	/*
	 * Other slots last received at elapsed zero; expiration is strictly
	 * later.
	 */
	ticks = 180100;
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	ticks++;
	CHECK(poll_event(host, &event) == MH_TRANSPORT_DISCONNECT &&
		event.peer == 11);

done:
	for (i = 0; i < 13; i++)
		if (clients[i] >= 0)
			close(clients[i]);

	mh_transport_close(host);
}

static void
raw_discovery(void)
{
	static const unsigned char query[] = {4, 0, 4, 0};
	struct mh_udp_transport_config config;
	struct mh_transport_provider provider;
	struct mh_transport *client;
	struct mh_discovery_advertisement game;
	const struct mh_discovery_catalog *catalog;
	struct sockaddr_in address, other, destination, source;
	unsigned char data[1123], reply[1123];
	size_t size;
	int listener, foreign, done;

	initialize_provider(&config, &provider);
	client = NULL;
	foreign = -1;
	ticks = 100;
	listener = raw_socket(&address, 0);
	CHECK(listener >= 0);

	if (listener < 0)
		return;

	foreign = raw_socket(&other, 0);
	CHECK(foreign >= 0);
	CHECK(mh_transport_open_address(&provider, 0, "127.0.0.1", 0, &client,
		NULL) == MH_ERROR_NONE);

	if (foreign < 0 || client == NULL)
		goto done;

	CHECK(mh_transport_search_address(client, NULL, ntohs(address.sin_port),
		1, NULL) == MH_ERROR_UNSUPPORTED);
	CHECK(mh_transport_search_address(client, "127.0.0.1",
		ntohs(address.sin_port), 1, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_search_results(client) == NULL);
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		!done);
	CHECK(raw_receive(listener, &destination, data, sizeof(data)) ==
		MH_SHARED_HEADER_BYTES + MH_NET_NONCE_BYTES);
	CHECK(raw_receive(listener, &destination, data, sizeof(data)) ==
		sizeof(query));
	CHECK(memcmp(data, query, sizeof(query)) == 0);
	CHECK(ntohs(destination.sin_port) != mh_transport_port(client));
	prepare_advertisement(&game);
	CHECK(mh_udp_description_encode(&game, reply, sizeof(reply), &size,
		NULL) == MH_ERROR_NONE);

	/* Valid bytes from a different endpoint must not finish the query. */
	raw_send(foreign, &destination, reply, size);
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		!done);
	raw_send(listener, &destination, reply, size - 1);
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		!done);
	raw_send(listener, &destination, reply, size);
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		done);
	catalog = mh_transport_search_results(client);
	CHECK(catalog != NULL && catalog->count == 1);
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		done);

	/*
	 * A new search clears results and times out if neither query receives a
	 * reply.
	 */
	CHECK(mh_transport_search_address(client, "127.0.0.1",
		ntohs(address.sin_port), 2, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_search_results(client) == NULL);
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		!done);
	CHECK(raw_receive(listener, &destination, data, sizeof(data)) ==
		MH_SHARED_HEADER_BYTES + MH_NET_NONCE_BYTES);
	CHECK(raw_receive(listener, &destination, data, sizeof(data)) ==
		sizeof(query));
	CHECK(memcmp(data, query, sizeof(query)) == 0);
	ticks += 1499;
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		!done);
	CHECK(raw_receive(listener, &source, data, sizeof(data)) == 0);
	ticks++;
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		done);
	catalog = mh_transport_search_results(client);
	CHECK(catalog != NULL && catalog->count == 0);
	CHECK(raw_receive(listener, &source, data, sizeof(data)) == 0);

	/* Close also cancels an in-progress query and its separate socket. */
	CHECK(mh_transport_search_address(client, "127.0.0.1",
		ntohs(address.sin_port), 3, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_search_poll(client, &done, NULL) == MH_ERROR_NONE &&
		!done);

done:
	mh_transport_close(client);
	close(listener);

	if (foreign >= 0)
		close(foreign);
}

static void
raw_server(void)
{
	static const unsigned char hello[] = {
		1, 0, 8, 0, 0x66, 0x66, 0xa6, 0x3f};

	static const unsigned char accepted[] = {2, 0, 20, 0, 'O', 'r', 'i',
		'g', 'i', 'n', 'a', 'l', 0, 0, 0, 0, 0, 0, 0, 0};

	struct mh_udp_transport_config config;
	struct mh_transport_provider provider;
	struct mh_transport *client;
	struct mh_transport_event event;
	struct sockaddr_in listen_address, peer_address, source, client_address;
	unsigned char data[128], payload[] = {2, 7, 0, 4, 0};
	int listener, peer;
	unsigned int i;

	initialize_provider(&config, &provider);
	client = NULL;
	peer = -1;
	ticks = 100;
	listener = raw_socket(&listen_address, 0);
	CHECK(listener >= 0);

	if (listener < 0)
		return;

	peer = raw_socket(&peer_address, ntohs(listen_address.sin_port) + 10);
	CHECK(peer >= 0);
	CHECK(mh_transport_open_address(&provider, 0, "127.0.0.1", 0, &client,
		NULL) == MH_ERROR_NONE);

	if (peer < 0 || client == NULL)
		goto done;

	CHECK(mh_transport_connect_address(client, "127.0.0.1",
		ntohs(listen_address.sin_port), NULL) == MH_ERROR_NONE);

	for (i = 0; i < 4; i++) {
		CHECK(poll_event(client, &event) == MH_TRANSPORT_NONE);
		CHECK(raw_receive(listener, &client_address, data,
			sizeof(data)) == sizeof(hello));
		CHECK(memcmp(data, hello, sizeof(hello)) == 0);
		ticks += 499;
		CHECK(poll_event(client, &event) == MH_TRANSPORT_NONE);
		CHECK(raw_receive(listener, &source, data, sizeof(data)) == 0);
		ticks++;
	}

	CHECK(poll_event(client, &event) == MH_TRANSPORT_DISCONNECT &&
		event.peer == 1);
	CHECK(mh_transport_connect_address(client, "127.0.0.1",
		ntohs(listen_address.sin_port), NULL) == MH_ERROR_NONE);
	CHECK(poll_event(client, &event) == MH_TRANSPORT_NONE);
	CHECK(raw_receive(listener, &client_address, data, sizeof(data)) ==
		sizeof(hello));
	raw_send(peer, &client_address, accepted, sizeof(accepted));
	CHECK(poll_event(client, &event) == MH_TRANSPORT_CONNECT &&
		event.peer == 1);
	CHECK(mh_transport_send(client, 1, hello, sizeof(hello), 1, NULL) ==
		MH_ERROR_NONE);
	CHECK(raw_receive(peer, &source, data, sizeof(data)) == 9 &&
		data[0] == 2);
	CHECK(raw_receive(listener, &source, data, sizeof(data)) == 0);
	raw_send(listener, &client_address, payload, sizeof(payload));
	CHECK(poll_event(client, &event) == MH_TRANSPORT_NONE);
	raw_send(peer, &client_address, payload, sizeof(payload));
	CHECK(poll_event(client, &event) == MH_TRANSPORT_RECEIVE &&
		event.peer == 1);
	CHECK(event.reliable && event.size == 4 &&
		memcmp(event.data, payload + 1, 4) == 0);
	CHECK(raw_receive(peer, &source, data, sizeof(data)) == 1 &&
		data[0] == 2);

	/* Retire the client queue, then verify no retry is sent. */
	raw_send(peer, &client_address, payload, 1);
	CHECK(poll_event(client, &event) == MH_TRANSPORT_NONE);
	ticks += 501;
	CHECK(poll_event(client, &event) == MH_TRANSPORT_NONE);
	CHECK(raw_receive(peer, &source, data, sizeof(data)) == 0);

done:
	mh_transport_close(client);
	close(listener);

	if (peer >= 0)
		close(peer);
}

static void
dedicated_ports(int dynamic)
{
	static const unsigned char hello[] = {
		1, 0, 8, 0, 0x66, 0x66, 0xa6, 0x3f};

	static const char no_port[] = "Connection denied: No client port!";

	struct mh_udp_transport_config config;
	struct mh_transport_provider provider;
	struct mh_transport *host;
	struct mh_transport_event event;
	struct sockaddr_in reserved, target, source, local;
	unsigned char data[128];
	unsigned int first, i, attempt;
	int held[3], fd;
	size_t size;

	host = NULL;
	fd = -1;
	first = 0;
	held[0] = held[1] = held[2] = -1;

	/* Reserve a free contiguous test range without assuming fixed ports. */
	for (attempt = 0; attempt < 100; attempt++) {
		held[0] = raw_socket(&reserved, 0);

		if (held[0] < 0)
			break;

		first = ntohs(reserved.sin_port);

		if (first <= MH_U16_MAX - 2) {
			held[1] = raw_socket(&reserved, first + 1);
			held[2] = raw_socket(&reserved, first + 2);

			if (held[1] >= 0 && held[2] >= 0)
				break;
		}

		for (i = 0; i < 3; i++) {
			if (held[i] >= 0)
				close(held[i]);

			held[i] = -1;
		}
	}

	CHECK(held[0] >= 0 && held[1] >= 0 && held[2] >= 0);

	if (held[0] < 0 || held[1] < 0 || held[2] < 0)
		goto done;

	initialize_provider(&config, &provider);
	config.port_policy = MH_UDP_PORTS_RANGE;
	config.client_port_first = dynamic ? 0 : first;
	config.client_port_last = dynamic ? 0 : first + 2;
	CHECK(mh_udp_transport_init(&provider, &config, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_open_address(&provider, 1, "127.0.0.1", 0, &host,
		NULL) == MH_ERROR_NONE);
	fd = raw_socket(&local, 0);
	CHECK(host != NULL && fd >= 0);

	if (host == NULL || fd < 0)
		goto done;

	target = local;
	target.sin_port = htons((unsigned short)mh_transport_port(host));

	for (i = 1; i < 3; i++) {
		close(held[i]);
		held[i] = -1;
	}

	for (i = 0; i < 2; i++) {
		raw_send(fd, &target, hello, sizeof(hello));
		CHECK(poll_event(host, &event) == MH_TRANSPORT_CONNECT &&
			event.peer == 10 + i);
		size = raw_receive(fd, &source, data, sizeof(data));
		CHECK(size == 20 && data[0] == 2);
		CHECK(source.sin_port != target.sin_port &&
			source.sin_port != 0);

		if (!dynamic)
			CHECK(ntohs(source.sin_port) == first + i + 1);
	}

	if (!dynamic) {
		raw_send(fd, &target, hello, sizeof(hello));
		CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
		size = raw_receive(fd, &source, data, sizeof(data));
		CHECK(size == 4 + sizeof(no_port) && data[0] == 3 &&
			memcmp(data + 4, no_port, sizeof(no_port)) == 0);
		CHECK(source.sin_port == target.sin_port);
	}

	CHECK(mh_transport_disconnect(host, 10, NULL) == MH_ERROR_NONE);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_DISCONNECT &&
		event.peer == 10);
	raw_send(fd, &target, hello, sizeof(hello));
	CHECK(poll_event(host, &event) == MH_TRANSPORT_CONNECT &&
		event.peer == 10);
	CHECK(raw_receive(fd, &source, data, sizeof(data)) == 20 &&
		data[0] == 2);

	if (!dynamic)
		CHECK(ntohs(source.sin_port) == first + 1);

done:
	mh_transport_close(host);
	if (fd >= 0)
		close(fd);

	for (i = 0; i < 3; i++)
		if (held[i] >= 0)
			close(held[i]);
}

static void
shared_clients(void)
{
	struct mh_udp_transport_config config;
	struct mh_transport_provider provider;
	struct mh_transport *host;
	struct mh_transport_event event;
	struct mh_shared_packet packet, reply;
	struct sockaddr_in local[3], target, source;
	unsigned char data[MH_SHARED_PACKET_BYTES + 1],
		payload[MH_RELIABLE_DATAGRAM_BYTES];
	unsigned char old_packet[MH_SHARED_PACKET_BYTES];
	unsigned long generations[3], previous;
	unsigned int i;
	size_t size, encoded, old_size;
	int fd[3];

	host = NULL;

	for (i = 0; i < 3; i++)
		fd[i] = -1;

	initialize_provider(&config, &provider);
	config.port_policy = MH_UDP_PORTS_SHARED;
	CHECK(mh_udp_transport_init(&provider, &config, NULL) == MH_ERROR_NONE);
	CHECK(mh_transport_open_address(&provider, 1, "127.0.0.1", 0, &host,
		NULL) == MH_ERROR_NONE);

	if (host == NULL)
		goto done;

	raw_query(host);

	for (i = 0; i < 3; i++) {
		fd[i] = raw_socket(&local[i], 0);
		CHECK(fd[i] >= 0);

		if (fd[i] < 0)
			goto done;

		target = local[i];
		target.sin_port =
			htons((unsigned short)mh_transport_port(host));
		confirm_challenge(host, fd[i], &target, MH_SHARED_HELLO, i + 1);
		CHECK(poll_event(host, &event) == MH_TRANSPORT_CONNECT &&
			event.peer == 10 + i);
		size = raw_receive(fd[i], &source, data, sizeof(data));
		CHECK(size ==
				MH_SHARED_HEADER_BYTES + MH_UDP_NAME_BYTES +
					MH_NET_NONCE_BYTES &&
			memcmp(data, "OMH1\x02\0\0\0", 8) == 0);
		CHECK(source.sin_port == target.sin_port);
		CHECK(mh_read_u32le(data + 8) == i + 1);
		generations[i] = mh_read_u32le(data + 12);
		CHECK(generations[i] != 0);

		/*
		 * Lost acceptance: retry acknowledges the same peer, not a new
		 * slot.
		 */
		confirm_challenge(host, fd[i], &target, MH_SHARED_HELLO, i + 1);
		CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
		CHECK(raw_receive(fd[i], &source, data, sizeof(data)) ==
			MH_SHARED_HEADER_BYTES + MH_UDP_NAME_BYTES +
				MH_NET_NONCE_BYTES);
		CHECK(mh_read_u32le(data + 12) == generations[i]);
	}

	CHECK(generations[0] != generations[1] &&
		generations[1] != generations[2]);

	for (i = 0; i < 3; i++) {
		memset(payload, (int)(i + 1), sizeof(payload));
		payload[0] = 0;
		packet.type = MH_SHARED_DATA;
		packet.client_generation = i + 1;
		packet.server_generation = generations[i];
		packet.data = payload;
		packet.size = sizeof(payload);
		CHECK(mh_shared_packet_write(&packet, data, sizeof(data),
			&encoded, NULL) == MH_ERROR_NONE);
		raw_send(fd[i], &target, data, encoded);
		CHECK(poll_event(host, &event) == MH_TRANSPORT_RECEIVE &&
			event.peer == 10 + i);
		CHECK(event.size == MH_NET_WIRE_PACKET_LIMIT &&
			!event.reliable &&
			memcmp(event.data, payload + 1, event.size) == 0);
		CHECK(mh_transport_send(host, 10 + i, event.data, event.size, 1,
			NULL) == MH_ERROR_NONE);
		size = raw_receive(fd[i], &source, data, sizeof(data));
		CHECK(source.sin_port == target.sin_port);
		CHECK(mh_shared_packet_read(data, size, &reply, NULL) ==
			MH_ERROR_NONE);
		CHECK(reply.type == MH_SHARED_DATA &&
			reply.size == MH_RELIABLE_DATAGRAM_BYTES &&
			reply.client_generation == i + 1 &&
			reply.server_generation == generations[i]);
		CHECK(reply.data[0] != 0 &&
			memcmp(reply.data + 1, payload + 1,
				sizeof(payload) - 1) == 0);

		/* ACK on the same socket retires the reliable queue. */
		packet.data = reply.data;
		packet.size = 1;
		CHECK(mh_shared_packet_write(&packet, data, sizeof(data),
			&encoded, NULL) == MH_ERROR_NONE);
		raw_send(fd[i], &target, data, encoded);
		CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	}

	ticks += 501;
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);

	for (i = 0; i < 3; i++)
		CHECK(raw_receive(fd[i], &source, data, sizeof(data)) == 0);

	/* Wrong endpoint/generation and oversized envelopes are discarded. */
	packet.client_generation = 1;
	packet.server_generation = generations[0];
	packet.data = payload;
	packet.size = sizeof(payload);
	CHECK(mh_shared_packet_write(&packet, old_packet, sizeof(old_packet),
		&old_size, NULL) == MH_ERROR_NONE);
	raw_send(fd[1], &target, old_packet, old_size);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	memcpy(data, old_packet, old_size);
	data[12] ^= 1;
	raw_send(fd[0], &target, data, old_size);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	memcpy(data, old_packet, old_size);
	data[old_size] = 0;
	raw_send(fd[0], &target, data, old_size + 1);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);

	/* Reconnecting at the same endpoint retires the old session first. */
	confirm_challenge(host, fd[0], &target, MH_SHARED_HELLO, 4);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_DISCONNECT &&
		event.peer == 10);
	confirm_challenge(host, fd[0], &target, MH_SHARED_HELLO, 4);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_CONNECT &&
		event.peer == 10);
	CHECK(raw_receive(fd[0], &source, data, sizeof(data)) ==
		MH_SHARED_HEADER_BYTES + MH_UDP_NAME_BYTES +
			MH_NET_NONCE_BYTES);
	previous = generations[0];
	generations[0] = mh_read_u32le(data + 12);
	CHECK(previous != generations[0]);
	raw_send(fd[0], &target, old_packet, old_size);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_NONE);
	CHECK(mh_transport_disconnect(host, 10, NULL) == MH_ERROR_NONE);
	CHECK(poll_event(host, &event) == MH_TRANSPORT_DISCONNECT &&
		event.peer == 10);
	CHECK(mh_transport_send(host, 11, payload, 4, 0, NULL) ==
		MH_ERROR_NONE);
	CHECK(raw_receive(fd[1], &source, data, sizeof(data)) ==
		MH_SHARED_HEADER_BYTES + 5);
	CHECK(source.sin_port == target.sin_port);

done:
	mh_transport_close(host);
	for (i = 0; i < 3; i++)
		if (fd[i] >= 0)
			close(fd[i]);
}

int
main(void)
{
	if (mh_net_protocol_get(MH_NET_PROTOCOL_LEGACY) != NULL) {
		native_pair(0);
		raw_clients();
		raw_server();
		raw_discovery();
		dedicated_ports(0);
		dedicated_ports(1);
	}

	if (mh_net_protocol_get(MH_NET_PROTOCOL_OPENMH) != NULL) {
		native_pair(1);
		shared_clients();
	}

	if (socket_wait.count != 0) {
		fprintf(stderr,
			"Closed transports left sockets in the wait set\n");
		failures++;
	}

	return failures != 0;
}
