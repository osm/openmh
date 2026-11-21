#ifndef MH_NET_UDP_CONTROL_H
#define MH_NET_UDP_CONTROL_H

#include "backends/network/reliable_delivery.h"

#define MH_UDP_VERSION_BITS 0x3fa66666UL
#define MH_UDP_NAME_BYTES 16

enum mh_udp_control_type {
	MH_UDP_HELLO = 1,
	MH_UDP_ACCEPT,
	MH_UDP_REJECT,
	MH_UDP_QUERY,
	MH_UDP_DESCRIPTION
};

struct mh_udp_control {
	enum mh_udp_control_type	type;
	unsigned long			version_bits;
	char				name[MH_UDP_NAME_BYTES];
	const char			*rejection;
	const unsigned char		*description;
	size_t				description_size;
};

/*
 * Control datagrams have no delivery prefix. Decode copies the name and borrows
 * rejection/description bytes; keep the input alive while using those views.
 * Errors preserve output.
 */
enum mh_error_code	mh_udp_control_decode(const unsigned char *data,
	size_t size, struct mh_udp_control *output, struct mh_error *error_out);

/* Initialize name padding; do not reproduce uninitialized original bytes. */
enum mh_error_code	mh_udp_control_encode(
	const struct mh_udp_control *message, unsigned char *output,
	size_t capacity, size_t *written, struct mh_error *error_out);

#endif
