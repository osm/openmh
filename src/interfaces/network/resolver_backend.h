#ifndef MH_NETWORK_RESOLVER_BACKEND_H
#define MH_NETWORK_RESOLVER_BACKEND_H

#include "interfaces/network/resolver.h"

/*
 * Tables/context are borrowed unchanged until all requests close. Open copies
 * the name and cleans partial failure. Poll/close never wait for resolution;
 * any detached work must not use the descriptor or its borrowed context.
 * Diagnostics belong to the provider, not a freed request, and remain valid
 * until its next operation other than close.
 */
struct mh_resolver_ops {
	enum mh_error_code (*open)(void *context, const char *host,
		void		**output, struct mh_error *error_out);
	enum mh_error_code (*poll)(void *context, void *request, int *done,
		unsigned long	*ipv4, struct mh_error *error_out);
	void			(*close)(void *context, void *request);
};

struct mh_resolver_provider {
	const struct mh_resolver_ops	*ops;
	void				*context;
};

enum mh_error_code	mh_resolver_provider_init(
	struct mh_resolver_provider *provider,
	const struct mh_resolver_ops *ops, void *context,
	struct mh_error *error_out);
#endif
