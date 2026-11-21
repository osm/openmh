#include <stdlib.h>

#include "interfaces/network/resolver_backend.h"

struct mh_resolver {
	const struct mh_resolver_provider	*provider;
	void					*request;
};

static int
complete_ops(const struct mh_resolver_ops *ops)
{
	return ops != NULL && ops->open != NULL && ops->poll != NULL &&
		ops->close != NULL;
}

enum mh_error_code
mh_resolver_provider_init(struct mh_resolver_provider *provider,
	const struct mh_resolver_ops *ops, void *context,
	struct mh_error *error_out)
{
	if (provider == NULL || !complete_ops(ops))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete resolver provider");

	provider->ops = ops;
	provider->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_resolver_open(const struct mh_resolver_provider *provider, const char *host,
	struct mh_resolver **output, struct mh_error *error_out)
{
	struct mh_resolver *resolver;
	enum mh_error_code status;

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"hostname resolution is unavailable");

	if (!complete_ops(provider->ops) || host == NULL || *host == 0 ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid hostname resolution request");

	resolver = calloc(1, sizeof(*resolver));

	if (resolver == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate hostname resolution request");

	resolver->provider = provider;
	status = provider->ops->open(
		provider->context, host, &resolver->request, error_out);

	if (status != MH_ERROR_NONE) {
		free(resolver);
		return status;
	}

	*output = resolver;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_resolver_poll(struct mh_resolver *resolver, int *done, unsigned long *ipv4,
	struct mh_error *error_out)
{
	if (resolver == NULL || done == NULL || ipv4 == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid hostname resolution poll");

	return resolver->provider->ops->poll(resolver->provider->context,
		resolver->request, done, ipv4, error_out);
}

void
mh_resolver_close(struct mh_resolver *resolver)
{
	if (resolver == NULL)
		return;

	resolver->provider->ops->close(
		resolver->provider->context, resolver->request);
	free(resolver);
}
