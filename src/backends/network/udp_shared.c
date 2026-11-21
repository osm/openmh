#include <string.h>

#include "backends/network/udp_shared.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"

#define SHARED_SIGNATURE "OMH1"

enum {
	SHARED_SIGNATURE_BYTES = sizeof(SHARED_SIGNATURE) - 1,
	SHARED_TYPE = SHARED_SIGNATURE_BYTES,
	SHARED_RESERVED = 5,
	SHARED_RESERVED_BYTES = 3,
	SHARED_CLIENT_GENERATION = 8,
	SHARED_SERVER_GENERATION = 12
};

static int
valid_packet(const struct mh_shared_packet *packet)
{
	if (packet->client_generation == 0 ||
		packet->client_generation > MH_U32_MAX ||
		packet->server_generation > MH_U32_MAX ||
		packet->size > MH_RELIABLE_DATAGRAM_BYTES ||
		(packet->size != 0 && packet->data == NULL))
		return 0;

	switch (packet->type) {
	case MH_SHARED_HELLO:
	case MH_SHARED_CHALLENGE:
	case MH_SHARED_CONFIRM:
	case MH_SHARED_QUERY:
	case MH_SHARED_QUERY_CHALLENGE:
	case MH_SHARED_QUERY_CONFIRM:
		return packet->server_generation == 0 &&
			packet->size == MH_NET_NONCE_BYTES;
	case MH_SHARED_ACCEPT:
		return packet->server_generation != 0 &&
			packet->size ==
			MH_UDP_NAME_BYTES + MH_NET_NONCE_BYTES &&
			memchr(packet->data, 0, MH_UDP_NAME_BYTES) != NULL;
	case MH_SHARED_REJECT:
		return packet->server_generation == 0 && packet->size > 1 &&
			memchr(packet->data, 0, packet->size) ==
			packet->data + packet->size - 1;
	case MH_SHARED_DATA:
		return packet->server_generation != 0 && packet->size != 0;
	default:
		return 0;
	}
}

enum mh_error_code
mh_shared_packet_read(const unsigned char *data, size_t size,
	struct mh_shared_packet *output, struct mh_error *error_out)
{
	struct mh_shared_packet packet;

	if (data == NULL || output == NULL || size < MH_SHARED_HEADER_BYTES ||
		size > MH_SHARED_PACKET_BYTES ||
		memcmp(data, SHARED_SIGNATURE, SHARED_SIGNATURE_BYTES) != 0 ||
		data[SHARED_RESERVED] != 0 || data[SHARED_RESERVED + 1] != 0 ||
		data[SHARED_RESERVED + 2] != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid OpenMH datagram header");

	packet.type = (enum mh_shared_type)data[SHARED_TYPE];
	packet.client_generation =
		mh_read_u32le(data + SHARED_CLIENT_GENERATION);
	packet.server_generation =
		mh_read_u32le(data + SHARED_SERVER_GENERATION);
	packet.data = data + MH_SHARED_HEADER_BYTES;
	packet.size = size - MH_SHARED_HEADER_BYTES;

	if (!valid_packet(&packet))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid OpenMH datagram payload");

	*output = packet;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_shared_packet_write(const struct mh_shared_packet *packet,
	unsigned char *output, size_t capacity, size_t *written,
	struct mh_error *error_out)
{
	size_t size;

	if (packet == NULL || output == NULL || written == NULL ||
		!valid_packet(packet))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid OpenMH datagram");

	size = MH_SHARED_HEADER_BYTES + packet->size;

	if (capacity < size)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"OpenMH datagram output too small");

	/* Permit in-place wrapping of a delivery packet. */
	if (packet->size != 0)
		memmove(output + MH_SHARED_HEADER_BYTES, packet->data,
			packet->size);

	memcpy(output, SHARED_SIGNATURE, SHARED_SIGNATURE_BYTES);
	output[SHARED_TYPE] = (unsigned char)packet->type;
	memset(output + SHARED_RESERVED, 0, SHARED_RESERVED_BYTES);
	mh_write_u32le(
		output + SHARED_CLIENT_GENERATION, packet->client_generation);
	mh_write_u32le(
		output + SHARED_SERVER_GENERATION, packet->server_generation);
	*written = size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
