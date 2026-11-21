#ifndef MH_BACKENDS_SIGNALING_PROVIDER_H
#define MH_BACKENDS_SIGNALING_PROVIDER_H

#include "interfaces/network/network.h"

struct mh_clock_source;
struct mh_signaling_server;

struct mh_signaling_config {
	const struct mh_clock_source	*clock;
	unsigned long			bind_ipv4, proxy_ipv4;
	unsigned int			port;
	int				trust_proxy;
	const char			*origin;
	void				*context;
	enum mh_error_code (*offer)(void *context, const unsigned char *data,
		size_t size, const struct mh_transport_address *client,
		char *answer, size_t capacity, size_t *written,
		unsigned long		*peer, struct mh_error *error_out);
	void	(*cancel)(void *context, unsigned long peer);
};

/*
 * Native HTTP SDP endpoint for /. Bind/proxy addresses are host-order
 * IPv4; port zero asks for an ephemeral listener. Clock, callbacks and context
 * are borrowed until close; origin is copied. Requires a single explicit
 * http(s) website origin, and HTTPS termination at a reverse proxy in
 * production.
 *
 * Client identity normally comes from TCP. Only trust_proxy plus an exact
 * proxy_ipv4 match permits X-Real-IP, which must then contain one numeric IP.
 * The trusted proxy must overwrite that header. Untrusted forwarding headers
 * never replace the TCP source address.
 *
 * Main-thread polling, no waits or callbacks after close. Failed answer
 * delivery cancels its peer; fully delivered answers leave lifetime with the
 * transport. Stop signaling before destroying the borrowed transport context.
 */
/* Validate the same origin syntax accepted by the listener. */
int	mh_signaling_origin_valid(const char *origin);

enum mh_error_code	mh_signaling_open(
	const struct mh_signaling_config *config,
	struct mh_signaling_server **output, struct mh_error *error_out);
enum mh_error_code	mh_signaling_poll(struct mh_signaling_server *server,
	struct mh_error *error_out);
unsigned int		mh_signaling_port(
	const struct mh_signaling_server *server);
void			mh_signaling_close(struct mh_signaling_server *server);

#endif
