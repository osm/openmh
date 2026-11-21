#ifndef MH_WEBRTC_STUN_H
#define MH_WEBRTC_STUN_H

#include "engine/core/error.h"

#define MH_STUN_HEADER_BYTES 20
#define MH_STUN_PACKET_BYTES 2048
#define MH_STUN_COOKIE 0x2112a442UL

/* STUN header/attribute fields and ICE authentication sizes (RFC 8489). */
#define MH_STUN_FINGERPRINT_XOR 0x5354554eUL
#define MH_STUN_CRC_POLYNOMIAL 0xedb88320UL

enum {
	MH_STUN_LENGTH_OFFSET = 2,
	MH_STUN_COOKIE_OFFSET = 4,
	MH_STUN_TRANSACTION_OFFSET = 8,
	MH_STUN_TRANSACTION_BYTES = 12,
	MH_STUN_ATTRIBUTE_BYTES = 4,
	MH_STUN_ATTRIBUTE_LENGTH = 2,
	MH_STUN_INTEGRITY_BYTES = 20,
	MH_STUN_FINGERPRINT_BYTES = 4,
	MH_STUN_PRIORITY_BYTES = 4,
	MH_STUN_ROLE_BYTES = 8,
	MH_STUN_USERNAME_MAX = 513,
	MH_STUN_PASSWORD_MAX = 256,
	MH_STUN_MAPPED_HEADER_BYTES = 4,
	MH_STUN_ERROR_HEADER_BYTES = 4,
	MH_STUN_ADDRESS_FAMILY = 1,
	MH_STUN_ADDRESS_PORT = 2,
	MH_STUN_IPV4 = 1,
	MH_STUN_IPV6 = 2,
	MH_STUN_IPV4_BYTES = 4,
	MH_STUN_IPV6_BYTES = 16,
	MH_STUN_IPV4_MAPPED_BYTES =
		MH_STUN_MAPPED_HEADER_BYTES + MH_STUN_IPV4_BYTES,
	MH_STUN_IPV6_MAPPED_BYTES =
		MH_STUN_MAPPED_HEADER_BYTES + MH_STUN_IPV6_BYTES,
	MH_STUN_ATTR_USERNAME = 0x0006,
	MH_STUN_ATTR_INTEGRITY = 0x0008,
	MH_STUN_ATTR_ERROR = 0x0009,
	MH_STUN_ATTR_XOR_MAPPED = 0x0020,
	MH_STUN_ATTR_PRIORITY = 0x0024,
	MH_STUN_ATTR_USE_CANDIDATE = 0x0025,
	MH_STUN_ATTR_FINGERPRINT = 0x8028,
	MH_STUN_ATTR_CONTROLLED = 0x8029,
	MH_STUN_ATTR_CONTROLLING = 0x802a,
	MH_STUN_OPTIONAL_ATTRIBUTE = 0x8000
};

enum mh_stun_type {
	MH_STUN_BINDING_REQUEST = 0x0001,
	MH_STUN_BINDING_SUCCESS = 0x0101,
	MH_STUN_BINDING_ERROR = 0x0111
};

struct mh_stun_message {
	const unsigned char	*data, *transaction, *username, *role, *mapped;
	size_t size, username_size, mapped_size, integrity_offset,
		fingerprint_offset;
	unsigned int		type;
	unsigned long		priority;
	int	has_priority, controlling, nominated, unknown_required;
};

/*
 * Parse one complete RFC 8489 binding datagram; borrow its unchanged bytes.
 * Validate framing and an optional CRC fingerprint, not HMAC authentication.
 * Attributes after MESSAGE-INTEGRITY are ignored except FINGERPRINT.
 * Unknown mandatory attributes before integrity are reported for rejection.
 * Integrity/fingerprint offsets are zero when absent. Duplicate interpreted
 * attributes and invalid known lengths fail. Errors preserve output.
 */
enum mh_error_code	mh_stun_read(const unsigned char *data, size_t size,
	struct mh_stun_message *output, struct mh_error *error_out);

/*
 * Decode XOR-MAPPED-ADDRESS to 4/16 network-order address bytes and a port.
 * Address capacity is always 16; size receives 4 or 16. Errors preserve output.
 */
enum mh_error_code	mh_stun_mapped(const struct mh_stun_message *message,
	unsigned char *address, size_t *size, unsigned int *port,
	struct mh_error *error_out);

#endif
