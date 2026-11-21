#include <stdlib.h>
#include <string.h>

#include "backends/network/udp_multiplex.h"

struct datagram {
	struct mh_udp_address	remote;
	size_t			size;
	unsigned char		data[MH_UDP_MUX_PACKET_BYTES];
};

struct packet_queue {
	struct datagram	packets[MH_UDP_MUX_QUEUE_PACKETS];
	unsigned int	head, count;
};

struct mh_udp_mux {
	struct mh_udp_socket	*socket;
	struct mh_udp_address	local;
	int	(*secondary)(const unsigned char *data, size_t size);
	struct packet_queue	queues[MH_UDP_MUX_CHANNELS];
	struct datagram		incoming;
};

enum mh_error_code
mh_udp_mux_create(struct mh_udp_mux **output, struct mh_error *error_out)
{
	struct mh_udp_mux *mux;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing UDP multiplexer output");

	mux = calloc(1, sizeof(*mux));

	if (mux == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate UDP multiplexer");

	*output = mux;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_udp_mux_close(struct mh_udp_mux *mux)
{
	free(mux);
}

int
mh_udp_mux_pending(const struct mh_udp_mux *mux)
{
	return mux != NULL &&
		(mux->queues[MH_UDP_MUX_PRIMARY].count != 0 ||
			mux->queues[MH_UDP_MUX_SECONDARY].count != 0);
}

void
mh_udp_mux_route(struct mh_udp_mux *mux,
	int (*secondary)(const unsigned char *data, size_t size))
{
	mux->secondary = secondary;
}

void
mh_udp_mux_attach(struct mh_udp_mux *mux, struct mh_udp_socket *socket,
	const struct mh_udp_address *local)
{
	mux->socket = socket;

	mux->local = *local;
}

void
mh_udp_mux_detach(struct mh_udp_mux *mux)
{
	if (mux != NULL) {
		mux->socket = NULL;
		mux->local.port = 0;
		memset(mux->queues, 0, sizeof(mux->queues));
	}
}

unsigned int
mh_udp_mux_port(const struct mh_udp_mux *mux)
{
	return mux == NULL ? 0 : mux->local.port;
}

enum mh_error_code
mh_udp_mux_send(struct mh_udp_mux *mux, const struct mh_udp_address *remote,
	const unsigned char *data, size_t size, struct mh_error *error_out)
{
	if (mux == NULL || mux->socket == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"shared UDP socket is not bound");

	return mh_udp_socket_send(mux->socket, remote, data, size, error_out);
}

static void
copy_packet(const struct datagram *packet, struct mh_udp_address *remote,
	unsigned char *data, size_t capacity, size_t *size, int *received)
{
	*remote = packet->remote;

	*size = packet->size < capacity ? packet->size : capacity;
	memcpy(data, packet->data, *size);
	*received = 1;
}

enum mh_error_code
mh_udp_mux_receive(struct mh_udp_mux *mux, unsigned int channel,
	struct mh_udp_address *remote, unsigned char *data, size_t capacity,
	size_t *size, int *received, struct mh_error *error_out)
{
	struct packet_queue *queue;
	struct datagram *packet;
	unsigned int i, destination, tail;
	int available;
	enum mh_error_code status;

	if (mux == NULL || channel >= MH_UDP_MUX_CHANNELS || remote == NULL ||
		data == NULL || capacity == 0 || size == NULL ||
		received == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid shared UDP receive");

	*size = 0;
	*received = 0;
	queue = &mux->queues[channel];

	if (queue->count != 0) {
		copy_packet(&queue->packets[queue->head], remote, data,
			capacity, size, received);
		queue->head = (queue->head + 1) % MH_UDP_MUX_QUEUE_PACKETS;
		queue->count--;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (mux->socket == NULL)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	packet = &mux->incoming;

	for (i = 0; i < MH_UDP_MUX_POLL_PACKETS; i++) {
		status = mh_udp_socket_receive(mux->socket, &packet->remote,
			packet->data, sizeof(packet->data), &packet->size,
			&available, error_out);

		if (status != MH_ERROR_NONE || !available)
			return status;

		destination = mux->secondary != NULL &&
				mux->secondary(packet->data, packet->size)
			? MH_UDP_MUX_SECONDARY
			: MH_UDP_MUX_PRIMARY;

		if (destination == channel) {
			copy_packet(
				packet, remote, data, capacity, size, received);
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		queue = &mux->queues[destination];

		if (queue->count == MH_UDP_MUX_QUEUE_PACKETS)
			continue;

		tail = (queue->head + queue->count) % MH_UDP_MUX_QUEUE_PACKETS;
		queue->packets[tail].remote = packet->remote;
		queue->packets[tail].size = packet->size;
		memcpy(queue->packets[tail].data, packet->data, packet->size);
		queue->count++;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
