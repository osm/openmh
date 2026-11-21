#include <string.h>

#include "engine/core/binary.h"
#include "engine/core/error.h"
#include "engine/network/protocol_internal.h"

enum mh_error_code
mh_net_frame_read(enum mh_net_wire wire, const unsigned char *data, size_t size,
	struct mh_net_frame *output, struct mh_error *error_out)
{
	struct mh_net_frame frame;

	if (data == NULL || size < MH_NET_HEADER_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"truncated network header");

	frame.type = (unsigned int)mh_read_u16le(data);
	frame.size = (size_t)mh_read_u16le(data + MH_NET_LENGTH_OFFSET);
	frame.data = data;

	if (frame.type == 0 || frame.size < MH_NET_HEADER_BYTES ||
		frame.size > size || frame.size > mh_net_wire_limit(wire))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network message length or type");

	*output = frame;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_wire_begin(enum mh_net_wire wire, const unsigned char *data, size_t size,
	struct mh_net_reader *output, struct mh_error *error_out)
{
	struct mh_net_reader reader;
	struct mh_net_frame frame;
	struct mh_net_message message;
	enum mh_error_code status;
	size_t offset;

	if (output == NULL || data == NULL || size == 0 ||
		size > mh_net_wire_limit(wire))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid network packet size");

	offset = 0;

	while (offset < size) {
		status = mh_net_frame_read(
			wire, data + offset, size - offset, &frame, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (mh_net_wire_supported(wire, frame.type)) {
			status = mh_net_wire_decode(
				wire, &frame, &message, error_out);

			if (status != MH_ERROR_NONE)
				return status;
		}

		offset += frame.size;
	}

	reader.data = data;
	reader.size = size;
	reader.offset = 0;
	*output = reader;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

int
mh_net_wire_next(enum mh_net_wire wire, struct mh_net_reader *reader,
	struct mh_net_frame *output)
{
	struct mh_net_frame frame;

	if (reader == NULL || output == NULL || reader->data == NULL ||
		mh_net_wire_limit(wire) == 0 ||
		reader->size > mh_net_wire_limit(wire) ||
		reader->offset > reader->size)
		return -1;

	if (reader->offset == reader->size)
		return 0;

	if (mh_net_frame_read(wire, reader->data + reader->offset,
		reader->size - reader->offset, &frame,
		NULL) != MH_ERROR_NONE)
		return -1;

	reader->offset += frame.size;
	*output = frame;

	return 1;
}

enum mh_error_code
mh_net_begin(const unsigned char *data, size_t size,
	struct mh_net_reader *output, struct mh_error *error_out)
{
	return mh_net_wire_begin(
		MH_NET_WIRE_F12, data, size, output, error_out);
}

int
mh_net_next(struct mh_net_reader *reader, struct mh_net_frame *output)
{
	return mh_net_wire_next(MH_NET_WIRE_F12, reader, output);
}

void
mh_net_batch_reset(struct mh_net_batch *batch)
{
	memset(batch, 0, sizeof(*batch));
}

enum mh_error_code
mh_net_wire_batch_append(enum mh_net_wire wire, struct mh_net_batch *batch,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	unsigned char data[MH_NET_PACKET_LIMIT];
	size_t size;
	enum mh_error_code status;

	if (batch == NULL || batch->size > MH_NET_BATCH_LIMIT)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid network batch");

	status = mh_net_wire_encode(
		wire, message, data, sizeof(data), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (size > MH_NET_BATCH_LIMIT - batch->size)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "network batch full");

	memcpy(batch->data + batch->size, data, size);
	batch->size += size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_batch_append(struct mh_net_batch *batch,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	return mh_net_wire_batch_append(
		MH_NET_WIRE_F12, batch, message, error_out);
}
