#include <stdio.h>
#include <string.h>

#include "backends/network/udp_multiplex.h"

#define FIXTURE_PACKETS (MH_UDP_MUX_POLL_PACKETS + 4)
#define CHECK(e)                                                               \
	do {                                                                   \
		if (!(e)) {                                                    \
			fprintf(stderr, "UDP multiplex:%d: %s\n", __LINE__,    \
				#e);                                           \
			return 1;                                              \
		}                                                              \
	} while (0)

struct mh_udp_socket {
	unsigned int		count, next, reads, sends;
	unsigned char		tags[FIXTURE_PACKETS];
	struct mh_udp_address	destination;
};

enum mh_error_code
mh_udp_socket_receive(struct mh_udp_socket *socket,
	struct mh_udp_address *remote, unsigned char *data, size_t capacity,
	size_t *size, int *received, struct mh_error *error_out)
{
	unsigned int index;

	socket->reads++;
	*size = 0;
	*received = socket->next < socket->count;

	if (*received) {
		index = socket->next++;
		remote->ipv4 = 0x7f000001UL;
		remote->port = 20000 + index;
		*size = capacity < 2 ? capacity : 2;
		data[0] = socket->tags[index];

		if (*size == 2)
			data[1] = (unsigned char)index;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_udp_socket_send(struct mh_udp_socket *socket,
	const struct mh_udp_address *remote, const unsigned char *data,
	size_t size, struct mh_error *error_out)
{
	(void)data;

	(void)size;
	socket->destination = *remote;
	socket->sends++;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static int
secondary(const unsigned char *data, size_t size)
{
	return size != 0 && data[0] == 'W';
}

static int
exercise(void)
{
	struct mh_udp_mux *mux;
	struct mh_udp_socket socket;
	struct mh_udp_address local, remote;
	unsigned char data[8];
	size_t size;
	unsigned int i, reads;
	int received;

	memset(&socket, 0, sizeof(socket));
	local.ipv4 = 0;
	local.port = 16666;
	CHECK(mh_udp_mux_create(&mux, NULL) == MH_ERROR_NONE);
	mh_udp_mux_route(mux, secondary);
	CHECK(mh_udp_mux_port(mux) == 0 && !mh_udp_mux_pending(mux));
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_PRIMARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	CHECK(!received && size == 0);
	CHECK(mh_udp_mux_send(mux, &local, data, 1, NULL) == MH_ERROR_STATE);
	mh_udp_mux_attach(mux, &socket, &local);
	CHECK(mh_udp_mux_port(mux) == local.port);

	/*
	 * Interleaved traffic retains per-consumer order and source addresses.
	 */
	memcpy(socket.tags, "WNWN", 4);
	socket.count = 4;
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_PRIMARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	CHECK(received && size == 2 && data[0] == 'N' && data[1] == 1 &&
		remote.port == 20001);
	CHECK(mh_udp_mux_pending(mux));
	reads = socket.reads;
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_SECONDARY, &remote, data, 1,
		&size, &received, NULL) == MH_ERROR_NONE);
	CHECK(received && size == 1 && data[0] == 'W' && remote.port == 20000 &&
		socket.reads == reads);
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_PRIMARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	CHECK(received && data[1] == 3);
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_SECONDARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	CHECK(received && data[1] == 2 && !mh_udp_mux_pending(mux));
	CHECK(mh_udp_mux_send(mux, &remote, data, size, NULL) == MH_ERROR_NONE);
	CHECK(socket.sends == 1 && socket.destination.port == 20002);

	/* A flooded other channel cannot make one read unbounded. */
	memset(&socket, 0, sizeof(socket));
	memset(socket.tags, 'W', sizeof(socket.tags));
	socket.count = FIXTURE_PACKETS;
	socket.tags[MH_UDP_MUX_POLL_PACKETS] = 'N';
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_PRIMARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	CHECK(!received && socket.reads == MH_UDP_MUX_POLL_PACKETS);
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_PRIMARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	CHECK(received && data[0] == 'N');
	reads = socket.reads;

	for (i = 0; i < MH_UDP_MUX_QUEUE_PACKETS; i++) {
		CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_SECONDARY, &remote,
			data, sizeof(data), &size, &received,
			NULL) == MH_ERROR_NONE);
		CHECK(received && data[1] == i && socket.reads == reads);
	}

	/* Detach discards queued packets; the socket remains caller-owned. */
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_PRIMARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	mh_udp_mux_detach(mux);
	CHECK(mh_udp_mux_port(mux) == 0 && !mh_udp_mux_pending(mux));
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_SECONDARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	CHECK(!received);
	mh_udp_mux_attach(mux, &socket, &local);
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_SECONDARY, &remote, data,
		sizeof(data), &size, &received, NULL) == MH_ERROR_NONE);
	CHECK(!received);
	CHECK(mh_udp_mux_receive(mux, MH_UDP_MUX_CHANNELS, &remote, data,
		sizeof(data), &size, &received,
		NULL) == MH_ERROR_INVALID);
	mh_udp_mux_close(mux);

	return 0;
}

int
main(void)
{
	return exercise();
}
