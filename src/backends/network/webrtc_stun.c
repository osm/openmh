#include <string.h>

#include "backends/network/webrtc_stun.h"
#include "backends/network/webrtc_wire.h"

static int
attribute(struct mh_stun_message *message, unsigned int type,
	const unsigned char *value, size_t size)
{
	switch (type) {
	case MH_STUN_ATTR_USERNAME:
		if (message->username != NULL || size == 0 ||
			size > MH_STUN_USERNAME_MAX ||
			memchr(value, 0, size) != NULL)
			return 0;

		message->username = value;
		message->username_size = size;
		break;

	case MH_STUN_ATTR_PRIORITY:
		if (message->has_priority || size != MH_STUN_PRIORITY_BYTES)
			return 0;

		message->has_priority = 1;
		message->priority = mh_rtc_read32(value);
		break;

	case MH_STUN_ATTR_USE_CANDIDATE:
		if (message->nominated || size != 0)
			return 0;

		message->nominated = 1;
		break;

	case MH_STUN_ATTR_CONTROLLED:
	case MH_STUN_ATTR_CONTROLLING:
		if (message->role != NULL || size != MH_STUN_ROLE_BYTES)
			return 0;

		message->role = value;
		message->controlling = type == MH_STUN_ATTR_CONTROLLING;
		break;

	case MH_STUN_ATTR_XOR_MAPPED:
		if (message->mapped != NULL ||
			(size != MH_STUN_IPV4_MAPPED_BYTES &&
				size != MH_STUN_IPV6_MAPPED_BYTES) ||
			value[0] != 0 ||
			(size == MH_STUN_IPV4_MAPPED_BYTES
					? value[MH_STUN_ADDRESS_FAMILY] !=
						MH_STUN_IPV4
					: value[MH_STUN_ADDRESS_FAMILY] !=
						MH_STUN_IPV6))
			return 0;

		message->mapped = value;
		message->mapped_size = size;
		break;

	case MH_STUN_ATTR_ERROR:
		if (size < MH_STUN_ERROR_HEADER_BYTES)
			return 0;

		break;

	default:
		if (type < MH_STUN_OPTIONAL_ATTRIBUTE)
			message->unknown_required = 1;

		break;
	}

	return 1;
}

enum mh_error_code
mh_stun_read(const unsigned char *data, size_t size,
	struct mh_stun_message *output, struct mh_error *error_out)
{
	struct mh_stun_message next;
	size_t offset, length, padded;
	unsigned int type;
	unsigned long fingerprint;

	if (data == NULL || output == NULL || size < MH_STUN_HEADER_BYTES ||
		size > MH_STUN_PACKET_BYTES || (size & 3) != 0 ||
		mh_rtc_read32(data + MH_STUN_COOKIE_OFFSET) != MH_STUN_COOKIE ||
		mh_rtc_read16(data + MH_STUN_LENGTH_OFFSET) !=
			size - MH_STUN_HEADER_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid STUN header");

	memset(&next, 0, sizeof(next));
	next.type = mh_rtc_read16(data);

	if (next.type != MH_STUN_BINDING_REQUEST &&
		next.type != MH_STUN_BINDING_SUCCESS &&
		next.type != MH_STUN_BINDING_ERROR)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"unsupported STUN method or class");

	next.data = data;
	next.size = size;
	next.transaction = data + MH_STUN_TRANSACTION_OFFSET;

	for (offset = MH_STUN_HEADER_BYTES; offset < size; offset += padded) {
		if (size - offset < MH_STUN_ATTRIBUTE_BYTES)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated STUN attribute");

		type = mh_rtc_read16(data + offset);
		length =
			mh_rtc_read16(data + offset + MH_STUN_ATTRIBUTE_LENGTH);
		padded = MH_STUN_ATTRIBUTE_BYTES + length +
			((4 - (length & 3)) & 3);

		if (padded > size - offset)
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"truncated STUN attribute value");

		if (type == MH_STUN_ATTR_FINGERPRINT) {
			if (length != MH_STUN_FINGERPRINT_BYTES ||
				padded != size - offset)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid STUN fingerprint position");

			fingerprint = mh_rtc_crc(data, offset, (size_t)-1,
					MH_STUN_CRC_POLYNOMIAL) ^
				MH_STUN_FINGERPRINT_XOR;

			if (fingerprint !=
				mh_rtc_read32(data + offset +
					MH_STUN_ATTRIBUTE_BYTES))
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid STUN fingerprint");

			next.fingerprint_offset = offset;
		} else if (next.integrity_offset != 0) {
			/* Do not expose unauthenticated trailing attributes. */
			continue;
		} else if (type == MH_STUN_ATTR_INTEGRITY) {
			if (length != MH_STUN_INTEGRITY_BYTES)
				return mh_error_set(error_out, MH_ERROR_INVALID,
					"invalid STUN integrity length");

			next.integrity_offset = offset;
		} else if (!attribute(&next, type,
				data + offset + MH_STUN_ATTRIBUTE_BYTES,
				length))
			return mh_error_set(error_out, MH_ERROR_INVALID,
				"invalid STUN attribute");
	}

	*output = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_stun_mapped(const struct mh_stun_message *message, unsigned char *address,
	size_t *size, unsigned int *port, struct mh_error *error_out)
{
	unsigned char decoded[MH_STUN_IPV6_BYTES];
	size_t i, length;

	if (message == NULL || address == NULL || size == NULL ||
		port == NULL || message->mapped == NULL ||
		message->data == NULL ||
		(message->mapped_size != MH_STUN_IPV4_MAPPED_BYTES &&
			message->mapped_size != MH_STUN_IPV6_MAPPED_BYTES))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing STUN mapped address");

	length = message->mapped_size - MH_STUN_MAPPED_HEADER_BYTES;

	for (i = 0; i < length; i++)
		decoded[i] = message->mapped[i + MH_STUN_MAPPED_HEADER_BYTES] ^
			message->data[i + MH_STUN_COOKIE_OFFSET];

	memcpy(address, decoded, length);
	*size = length;
	*port = mh_rtc_read16(message->mapped + MH_STUN_ADDRESS_PORT) ^
		(unsigned int)(MH_STUN_COOKIE >> 16);

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
