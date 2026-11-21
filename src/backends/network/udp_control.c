#include <string.h>

#include "backends/network/udp_control.h"
#include "engine/core/binary.h"
#include "engine/core/limits.h"

enum control_size {
	HEADER_BYTES = 4,
	LENGTH_OFFSET = 2,
	HELLO_BYTES = 8,
	SESSION_HEADER_BYTES = 20
};

enum mh_error_code
mh_udp_control_decode(const unsigned char *data, size_t size,
	struct mh_udp_control *output, struct mh_error *error_out)
{
	struct mh_udp_control message;
	double version;

	if (data == NULL || output == NULL || size < HEADER_BYTES ||
		size > MH_RELIABLE_DATAGRAM_BYTES ||
		mh_read_u16le(data + LENGTH_OFFSET) != size)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid LAN/INET control length");

	memset(&message, 0, sizeof(message));
	message.type = (enum mh_udp_control_type)mh_read_u16le(data);

	switch (message.type) {
	case MH_UDP_HELLO:
		if (size != HELLO_BYTES ||
			!mh_read_f32le(data + HEADER_BYTES, &version))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid LAN/INET hello");

		message.version_bits = mh_read_u32le(data + HEADER_BYTES);
		break;

	case MH_UDP_ACCEPT:
	case MH_UDP_DESCRIPTION:
		if (size < SESSION_HEADER_BYTES ||
			(message.type == MH_UDP_ACCEPT &&
				size != SESSION_HEADER_BYTES) ||
			memchr(data + HEADER_BYTES, 0, MH_UDP_NAME_BYTES) ==
				NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid LAN/INET session name");

		memcpy(message.name, data + HEADER_BYTES, MH_UDP_NAME_BYTES);
		message.description = data + SESSION_HEADER_BYTES;
		message.description_size = size - SESSION_HEADER_BYTES;
		break;

	case MH_UDP_REJECT:
		if (size == HEADER_BYTES ||
			memchr(data + HEADER_BYTES, 0, size - HEADER_BYTES) !=
				data + size - 1)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid LAN/INET rejection");

		message.rejection = (const char *)data + HEADER_BYTES;
		break;

	case MH_UDP_QUERY:
		if (size != HEADER_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid LAN/INET query");

		break;

	default:
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unknown LAN/INET control type");
	}

	*output = message;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_udp_control_encode(const struct mh_udp_control *message,
	unsigned char *output, size_t capacity, size_t *written,
	struct mh_error *error_out)
{
	unsigned char data[MH_RELIABLE_DATAGRAM_BYTES];
	struct mh_udp_control checked;
	const char *end;
	size_t size, length;
	enum mh_error_code status;

	if (message == NULL || output == NULL || written == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing LAN/INET control message");

	memset(data, 0, sizeof(data));
	size = HEADER_BYTES;

	switch (message->type) {
	case MH_UDP_HELLO:
		if (message->version_bits > MH_U32_MAX)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"LAN/INET version exceeds wire width");

		size = HELLO_BYTES;
		mh_write_u32le(data + HEADER_BYTES, message->version_bits);
		break;

	case MH_UDP_ACCEPT:
	case MH_UDP_DESCRIPTION:
		end = memchr(message->name, 0, sizeof(message->name));

		if (end == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"unterminated LAN/INET session name");

		size = SESSION_HEADER_BYTES;
		memcpy(data + HEADER_BYTES, message->name,
			(size_t)(end - message->name));

		if (message->type == MH_UDP_DESCRIPTION) {
			if (message->description_size > sizeof(data) - size ||
				(message->description_size != 0 &&
					message->description == NULL))
				return mh_error_set(error_out, MH_ERROR_RANGE,
					"invalid LAN/INET description size");

			if (message->description_size != 0)
				memcpy(data + size, message->description,
					message->description_size);

			size += message->description_size;
		}

		break;

	case MH_UDP_REJECT:
		if (message->rejection == NULL)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"missing LAN/INET rejection text");

		for (length = 0; length < sizeof(data) - HEADER_BYTES; length++)
			if (message->rejection[length] == 0)
				break;

		if (length == sizeof(data) - HEADER_BYTES)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"LAN/INET rejection too long");

		memcpy(data + HEADER_BYTES, message->rejection, length + 1);
		size += length + 1;
		break;

	case MH_UDP_QUERY:
		break;
	default:
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unknown LAN/INET control type");
	}

	data[0] = (unsigned char)message->type;
	mh_write_u16le(data + LENGTH_OFFSET, (unsigned long)size);
	status = mh_udp_control_decode(data, size, &checked, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (size > capacity)
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"LAN/INET control output too small");

	memcpy(output, data, size);
	*written = size;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
