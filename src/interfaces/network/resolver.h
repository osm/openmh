#ifndef MH_NETWORK_RESOLVER_H
#define MH_NETWORK_RESOLVER_H

#include "engine/core/error.h"

struct mh_resolver;
struct mh_resolver_provider;

/*
 * Resolve one IPv4 host without waiting for network completion. The request
 * borrows its provider through close and copies the host before open returns.
 * Failed open preserves output. Poll publishes a host-order 32-bit address
 * only when done; pending polls leave the address unchanged. Completion and
 * failure remain available until close. Close cancels delivery without waiting
 * for a native lookup: detached work must own everything it still accesses.
 * There are no callbacks into the caller. No threads are required by this API.
 */
enum mh_error_code	mh_resolver_open(
	const struct mh_resolver_provider *provider, const char *host,
	struct mh_resolver **output, struct mh_error *error_out);
enum mh_error_code	mh_resolver_poll(struct mh_resolver *resolver,
	int *done, unsigned long *ipv4, struct mh_error *error_out);
void			mh_resolver_close(struct mh_resolver *resolver);

#endif
