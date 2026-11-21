#ifndef MH_UDP_CHALLENGE_H
#define MH_UDP_CHALLENGE_H

#include "backends/network/udp_address.h"
#include "backends/network/udp_shared.h"

struct mh_udp_transport;
struct mh_udp_socket;

int			mh_udp_random(void *output, size_t size);
int			mh_udp_reply_allowed(struct mh_udp_transport *transport,
	unsigned long ipv4);
int			mh_udp_cookie_valid(
	const struct mh_udp_transport *transport,
	const struct mh_udp_address *source,
	const struct mh_shared_packet *request);
enum mh_error_code	mh_udp_shared_send(struct mh_udp_socket *socket,
	const struct mh_udp_address *remote,
	const struct mh_shared_packet *packet, struct mh_error *error_out);
void			mh_udp_challenge_reply(
	struct mh_udp_transport *transport, const struct mh_udp_address *source,
	struct mh_shared_packet packet);

#endif
