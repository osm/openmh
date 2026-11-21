#include <string.h>

#include "backends/network/webrtc_sctp_packet.h"
#include "backends/network/webrtc_wire.h"
#include "engine/core/limits.h"

#define SCTP_CRC_POLYNOMIAL 0x82f63b78UL

enum {
	SCTP_DESTINATION = 2,
	SCTP_TAG = 4,
	SCTP_CHECKSUM = 8,
	SCTP_CHECKSUM_BYTES = 4
};

static size_t
chunk_bytes(const unsigned char *data, size_t available)
{
	size_t length, padded;

	if (available < MH_SCTP_CHUNK_HEADER_BYTES)
		return 0;

	length = mh_rtc_read16(data + MH_SCTP_CHUNK_LENGTH);
	padded = length + ((4 - (length & 3)) & 3);

	return length < MH_SCTP_CHUNK_HEADER_BYTES || padded > available
		? 0
		: padded;
}

static int
chunks_valid(const unsigned char *data, size_t size)
{
	size_t offset, length;

	if (data == NULL ||
		size < MH_SCTP_HEADER_BYTES + MH_SCTP_CHUNK_HEADER_BYTES ||
		size > MH_SCTP_PACKET_BYTES || (size & 3) != 0)
		return 0;

	for (offset = MH_SCTP_HEADER_BYTES; offset < size; offset += length) {
		length = chunk_bytes(data + offset, size - offset);

		if (length == 0)
			return 0;
	}

	return 1;
}

enum mh_error_code
mh_sctp_packet_read(const unsigned char *data, size_t size,
	struct mh_sctp_packet *output, struct mh_error *error_out)
{
	struct mh_sctp_packet next;
	unsigned long crc;
	unsigned int i;

	if (output == NULL || !chunks_valid(data, size))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SCTP packet framing");

	crc = mh_rtc_crc(data, size, SCTP_CHECKSUM, SCTP_CRC_POLYNOMIAL);

	/* SCTP stores the reflected checksum least-significant byte first. */
	for (i = 0; i < SCTP_CHECKSUM_BYTES; i++)
		if (data[SCTP_CHECKSUM + i] != (unsigned char)(crc >> (8 * i)))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid SCTP checksum");

	next.data = data;
	next.size = size;
	next.source = mh_rtc_read16(data);
	next.destination = mh_rtc_read16(data + SCTP_DESTINATION);
	next.tag = mh_rtc_read32(data + SCTP_TAG);

	if (next.source == 0 || next.destination == 0)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid SCTP port");

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_sctp_chunk_next(const struct mh_sctp_packet *packet, size_t *offset,
	struct mh_sctp_chunk *output)
{
	struct mh_sctp_chunk next;
	size_t length;

	if (packet == NULL || packet->data == NULL || offset == NULL ||
		output == NULL || packet->size > MH_SCTP_PACKET_BYTES ||
		*offset < MH_SCTP_HEADER_BYTES || *offset > packet->size ||
		(*offset & 3) != 0)
		return -1;

	if (*offset == packet->size)
		return 0;

	length = chunk_bytes(packet->data + *offset, packet->size - *offset);

	if (length == 0)
		return -1;

	next.type = packet->data[*offset];
	next.flags = packet->data[*offset + MH_SCTP_CHUNK_FLAGS];
	next.size =
		mh_rtc_read16(packet->data + *offset + MH_SCTP_CHUNK_LENGTH) -
		MH_SCTP_CHUNK_HEADER_BYTES;
	next.data = packet->data + *offset + MH_SCTP_CHUNK_HEADER_BYTES;
	*offset += length;
	*output = next;

	return 1;
}

enum mh_error_code
mh_sctp_packet_begin(unsigned int source, unsigned int destination,
	unsigned long tag, unsigned char *output, size_t capacity, size_t *size,
	struct mh_error *error_out)
{
	if (output == NULL || size == NULL || source == 0 ||
		source > MH_U16_MAX || destination == 0 ||
		destination > MH_U16_MAX || tag > MH_U32_MAX)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SCTP packet arguments");

	if (capacity < MH_SCTP_HEADER_BYTES)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"SCTP header exceeds buffer");

	mh_rtc_write16(output, source);
	mh_rtc_write16(output + SCTP_DESTINATION, destination);
	mh_rtc_write32(output + SCTP_TAG, tag);
	memset(output + SCTP_CHECKSUM, 0, SCTP_CHECKSUM_BYTES);
	*size = MH_SCTP_HEADER_BYTES;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sctp_chunk_append(unsigned char *output, size_t capacity, size_t *size,
	unsigned int type, unsigned int flags, const unsigned char *data,
	size_t length, struct mh_error *error_out)
{
	size_t padded;

	if (output == NULL || size == NULL || (data == NULL && length != 0) ||
		type > 255 || flags > 255 || *size < MH_SCTP_HEADER_BYTES ||
		(*size & 3) != 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SCTP chunk arguments");

	if (capacity > MH_SCTP_PACKET_BYTES)
		capacity = MH_SCTP_PACKET_BYTES;

	if (length > MH_SCTP_PACKET_BYTES || *size > capacity)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "SCTP chunk exceeds buffer");

	padded = MH_SCTP_CHUNK_HEADER_BYTES + length + ((4 - (length & 3)) & 3);

	if (padded > capacity - *size)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "SCTP chunk exceeds buffer");

	output[*size] = (unsigned char)type;
	output[*size + MH_SCTP_CHUNK_FLAGS] = (unsigned char)flags;
	mh_rtc_write16(output + *size + MH_SCTP_CHUNK_LENGTH,
		(unsigned int)(length + MH_SCTP_CHUNK_HEADER_BYTES));

	if (length != 0)
		memcpy(output + *size + MH_SCTP_CHUNK_HEADER_BYTES, data,
			length);

	memset(output + *size + MH_SCTP_CHUNK_HEADER_BYTES + length, 0,
		padded - length - MH_SCTP_CHUNK_HEADER_BYTES);
	*size += padded;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_sctp_packet_finish(
	unsigned char *output, size_t size, struct mh_error *error_out)
{
	unsigned long crc;
	unsigned int i;

	if (!chunks_valid(output, size))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid SCTP packet framing");

	crc = mh_rtc_crc(output, size, SCTP_CHECKSUM, SCTP_CRC_POLYNOMIAL);

	for (i = 0; i < SCTP_CHECKSUM_BYTES; i++)
		output[SCTP_CHECKSUM + i] = (unsigned char)(crc >> (8 * i));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
