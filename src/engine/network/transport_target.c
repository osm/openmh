#include <stdlib.h>

#include "engine/network/transport_internal.h"
#include "interfaces/network/address.h"

enum mh_error_code
mh_transport_target_create(const struct mh_transport_provider *provider,
	void *token, void (*release)(void *, void *),
	struct mh_transport_target **output, struct mh_error *error_out)
{
	struct mh_transport_target *target;

	if (provider == NULL || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid transport target owner");

	target = malloc(sizeof(*target));

	if (target == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate transport target");

	target->provider = provider;
	target->token = token;
	target->release = release;
	*output = target;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_transport_target_close(struct mh_transport_target *target)
{
	if (target == NULL)
		return;

	if (target->release != NULL)
		target->release(target->provider->context, target->token);

	free(target);
}

enum mh_error_code
mh_transport_target_address(const struct mh_transport_provider *provider,
	const char *host, unsigned int port,
	struct mh_transport_target **output, struct mh_error *error_out)
{
	void *token;
	enum mh_error_code status;

	if (provider == NULL || provider->addresses == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"address-based network targets are unavailable");

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing transport target output");

	status = provider->addresses->prepare(
		provider->context, host, port, &token, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_transport_target_create(provider, token,
		provider->addresses->release, output, error_out);

	if (status != MH_ERROR_NONE)
		provider->addresses->release(provider->context, token);

	return status;
}

enum mh_error_code
mh_transport_open_address(const struct mh_transport_provider *provider,
	int server, const char *host, unsigned int port,
	struct mh_transport **output, struct mh_error *error_out)
{
	struct mh_transport_target *target;
	enum mh_error_code status;

	target = NULL;
	status = mh_transport_target_address(
		provider, host, port, &target, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_transport_open_from(
			provider, server, target, output, error_out);

	mh_transport_target_close(target);

	return status;
}

enum mh_error_code
mh_transport_connect_address(struct mh_transport *transport, const char *host,
	unsigned int port, struct mh_error *error_out)
{
	struct mh_transport_target *target;
	enum mh_error_code status;

	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	target = NULL;
	status = mh_transport_target_address(
		transport->provider, host, port, &target, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_transport_connect(transport, target, error_out);

	mh_transport_target_close(target);

	return status;
}

enum mh_error_code
mh_transport_search_address(struct mh_transport *transport, const char *host,
	unsigned int port, unsigned long request, struct mh_error *error_out)
{
	struct mh_transport_target *target;
	enum mh_error_code status;

	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	target = NULL;
	status = mh_transport_target_address(
		transport->provider, host, port, &target, error_out);

	if (status == MH_ERROR_NONE)
		status = mh_transport_search_begin(
			transport, target, request, error_out);

	mh_transport_target_close(target);

	return status;
}
