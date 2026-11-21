/*
 * Native LAN/INET transport with in-process datagrams and an explicit clock.
 * Connection, discovery and reliable delivery use the production code. Only
 * socket I/O, name resolution and time acquisition are replaced.
 */
#include <stdlib.h>
#include <string.h>

#include "backends/network/udp_provider.h"
#include "backends/network/udp_shared.h"
#include "backends/network/udp_socket.h"
#include "backends/network/udp_transport_provider.h"
#include "client/diagnostics/runtime_network.h"
#include "engine/network/network_address.h"
#include "interfaces/clock/backend.h"
#include "interfaces/network/resolver_backend.h"
#include "net_virtual_io.h"

#define SOCKET_CAPACITY 64
#define PACKET_CAPACITY 2048
#define DATAGRAM_CAPACITY MH_SHARED_PACKET_BYTES
#define FIRST_PORT 20000
#define LOOPBACK_ADDRESS 0x7f000001UL

struct mh_udp_socket {
	struct mh_udp_wait	*wait;
	int			used;
	struct mh_udp_address	address;
};

struct virtual_packet {
	int			used;
	struct mh_udp_socket	*destination;
	struct mh_udp_address	source;
	size_t			size;
	unsigned long		due;
	unsigned char		data[DATAGRAM_CAPACITY];
};

static struct mh_udp_socket sockets[SOCKET_CAPACITY];
static struct virtual_packet packets[PACKET_CAPACITY];
static unsigned long ticks, sent, dropped;
static unsigned int drop_every, duplicate_every, delay_ms;
static unsigned int next_port = FIRST_PORT;
static struct mh_transport_provider providers[2];
static int shared_port;

void
mh_test_net_shared_port(void)
{
	shared_port = 1;
}

void
mh_test_net_faults(
	unsigned int drop, unsigned int duplicate, unsigned int delay)
{
	drop_every = drop;

	duplicate_every = duplicate;
	delay_ms = delay;
}

unsigned long
mh_test_net_drops(void)
{
	return dropped;
}

void
mh_test_net_advance(unsigned int milliseconds)
{
	ticks += milliseconds;
}

static int
port_used(unsigned int port)
{
	unsigned int i;

	for (i = 0; i < SOCKET_CAPACITY; i++)
		if (sockets[i].used && sockets[i].address.port == port)
			return 1;

	return 0;
}

enum mh_error_code
mh_udp_socket_open(struct mh_udp_address *local, struct mh_udp_wait *wait,
	struct mh_udp_socket **output, struct mh_error *error_out)
{
	struct mh_udp_address bound;
	enum mh_error_code status;
	unsigned int i;

	bound = *local;

	if (bound.port == 0) {
		do {
			bound.port = next_port++;
		} while (port_used(bound.port));
	} else if (port_used(bound.port))
		return mh_error_set(
			error_out, MH_ERROR_IO, "virtual port already bound");

	for (i = 0; i < SOCKET_CAPACITY; i++)
		if (!sockets[i].used)
			break;

	if (i == SOCKET_CAPACITY)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"virtual socket capacity reached");

	status = mh_udp_wait_add(wait, &sockets[i], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	sockets[i].wait = wait;
	bound.ipv4 = LOOPBACK_ADDRESS;
	sockets[i].address = bound;
	sockets[i].used = 1;
	*local = bound;
	*output = &sockets[i];

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_udp_socket_close(struct mh_udp_socket *socket)
{
	unsigned int i;

	if (socket == NULL)
		return;

	mh_udp_wait_remove(socket->wait, socket);
	socket->used = 0;

	for (i = 0; i < PACKET_CAPACITY; i++)
		if (packets[i].destination == socket)
			packets[i].used = 0;
}

enum mh_error_code
mh_udp_socket_send(struct mh_udp_socket *socket,
	const struct mh_udp_address *remote, const unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	struct virtual_packet *packet;
	unsigned int i, copy, copies;
	struct mh_udp_socket *destination;

	if (size > DATAGRAM_CAPACITY)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"virtual datagram exceeds capacity");

	sent++;

	if (drop_every != 0 && sent % drop_every == 0) {
		dropped++;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	destination = NULL;

	for (i = 0; i < SOCKET_CAPACITY; i++)
		if (sockets[i].used &&
			mh_udp_address_same(&sockets[i].address, remote)) {
			destination = &sockets[i];
			break;
		}

	if (destination == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	copies = duplicate_every != 0 && sent % duplicate_every == 0 ? 2 : 1;

	for (copy = 0; copy < copies; copy++) {
		for (i = 0; i < PACKET_CAPACITY; i++)
			if (!packets[i].used)
				break;

		if (i == PACKET_CAPACITY)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"virtual datagram queue is full");

		packet = &packets[i];
		packet->used = 1;
		packet->source = socket->address;
		packet->destination = destination;
		packet->size = size;
		packet->due = ticks + (sent % 3) * delay_ms + copy;
		memcpy(packet->data, data, size);
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_udp_socket_receive(struct mh_udp_socket *socket,
	struct mh_udp_address *remote, unsigned char *data, size_t capacity,
	size_t *size, int *received, struct mh_error *error_out)
{
	struct virtual_packet *packet;
	unsigned int i;

	*received = 0;
	*size = 0;

	for (i = 0; i < PACKET_CAPACITY; i++)
		if (packets[i].used && packets[i].destination == socket &&
			packets[i].due <= ticks)
			break;

	if (i == PACKET_CAPACITY)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	packet = &packets[i];

	packet->used = 0;
	*remote = packet->source;
	*size = packet->size > capacity ? capacity : packet->size;
	memcpy(data, packet->data, *size);
	*received = 1;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned long
clock_ticks(void *context)
{
	(void)context;

	return ticks;
}

static enum mh_error_code
resolve_open(void *context, const char *host, void **output,
	struct mh_error *error_out)
{
	(void)context;

	if (strcmp(host, "127.0.0.1") != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"virtual address must be loopback");

	*output = NULL;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
resolve_poll(void *context, void *request, int *done, unsigned long *ipv4,
	struct mh_error *error_out)
{
	(void)context;

	(void)request;
	*done = 1;
	*ipv4 = LOOPBACK_ADDRESS;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
resolve_close(void *context, void *request)
{
	(void)context;
	(void)request;
}

static const struct mh_transport_provider *
virtual_provider(int shared)
{
	static const struct mh_clock_ops clock_ops = {
		NULL, NULL, clock_ticks, NULL, NULL};

	static const struct mh_clock_source clock = {&clock_ops, NULL};
	static const struct mh_resolver_ops resolver_ops = {
		resolve_open, resolve_poll, resolve_close};

	static const struct mh_resolver_provider resolver = {
		&resolver_ops, NULL};

	static struct mh_udp_transport_config configurations[2];
	struct mh_transport_provider *provider;
	struct mh_udp_transport_config *config;

	provider = &providers[shared];
	config = &configurations[shared];

	if (provider->ops == NULL) {
		config->clock = &clock;
		config->resolver = &resolver;
		config->port_policy =
			shared ? MH_UDP_PORTS_SHARED : MH_UDP_PORTS_OFFSET;
		strcpy(config->name, "Motorhead");

		if (mh_udp_transport_init(provider, config, NULL) !=
			MH_ERROR_NONE)
			return NULL;
	}

	return provider;
}

enum mh_error_code
mh_network_open(int host, const char *bind_host, const char *server_host,
	unsigned int port, const struct mh_net_session_config *config,
	struct mh_network **output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_network_open_address(
		virtual_provider(shared_port ||
			(config != NULL && config->simulation_rate != 0)),
		host, bind_host, server_host, port, config, output, error_out);

	return status;
}
