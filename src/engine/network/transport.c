#include <stdlib.h>

#include "engine/network/transport_channel.h"

#define POLL_WAIT_LIMIT_MS 1000

static int
complete_ops(const struct mh_transport_ops *ops)
{
	return ops != NULL && ops->open != NULL && ops->close != NULL &&
		ops->connect != NULL && ops->poll != NULL &&
		ops->send != NULL && ops->flush != NULL &&
		ops->disconnect != NULL && ops->now != NULL;
}

enum mh_error_code
mh_transport_provider_init(struct mh_transport_provider *provider,
	const struct mh_transport_ops *ops,
	const struct mh_transport_discovery_ops *discovery,
	const struct mh_transport_address_ops *addresses, void *context,
	struct mh_error *error_out)
{
	if (provider == NULL || !complete_ops(ops) ||
		(addresses != NULL &&
			(addresses->prepare == NULL ||
				addresses->release == NULL)) ||
		(discovery != NULL &&
			(discovery->advertise == NULL ||
				discovery->begin == NULL ||
				discovery->poll == NULL ||
				discovery->results == NULL)))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"incomplete transport provider");

	provider->ops = ops;
	provider->discovery = discovery;
	provider->addresses = addresses;
	provider->context = context;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_transport_open_from(const struct mh_transport_provider *provider, int server,
	const struct mh_transport_target *local, struct mh_transport **output,
	struct mh_error *error_out)
{
	struct mh_transport *transport;
	enum mh_error_code status;

	if (provider == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"network transport is unavailable");

	if (!complete_ops(provider->ops) || output == NULL ||
		(server != 0 && server != 1) ||
		(local != NULL && local->provider != provider))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid transport initialization");

	transport = calloc(1, sizeof(*transport));

	if (transport == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate transport");

	transport->provider = provider;
	status = provider->ops->open(provider->context, server,
		local == NULL ? NULL : local->token, &transport->token,
		error_out);

	if (status != MH_ERROR_NONE) {
		free(transport);
		return status;
	}

	*output = transport;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_transport_close(struct mh_transport *transport)
{
	if (transport == NULL)
		return;

	transport->provider->ops->close(
		transport->provider->context, transport->token);
	free(transport->channels);
	free(transport);
}

enum mh_error_code
mh_transport_poll(struct mh_transport *transport, unsigned int wait_ms,
	struct mh_transport_event *output, struct mh_error *error_out)
{
	const struct mh_transport_provider *provider;
	enum mh_error_code status;

	if (transport == NULL || output == NULL || wait_ms > POLL_WAIT_LIMIT_MS)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid transport poll");

	provider = transport->provider;

	if (wait_ms != 0 && provider->ops->wait == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"transport waits are unavailable");

	if (transport->channels != NULL) {
		status = mh_transport_channel_idle(transport, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	status = wait_ms == 0
		? provider->ops->poll(provider->context, transport->token,
			output, error_out)
		: provider->ops->wait(provider->context, transport->token,
			wait_ms, output, error_out);

	if (status == MH_ERROR_NONE && transport->channels != NULL)
		status = mh_transport_channel_event(
			transport, output, error_out);

	return status;
}

enum mh_error_code
mh_transport_connect(struct mh_transport *transport,
	const struct mh_transport_target *target, struct mh_error *error_out)
{
	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	if (target == NULL || target->provider != transport->provider)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"transport target belongs to another provider");

	return transport->provider->ops->connect(transport->provider->context,
		transport->token, target->token, error_out);
}

enum mh_error_code
mh_transport_send(struct mh_transport *transport, unsigned long peer,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	if (transport->channels != NULL)
		return mh_transport_channel_send(
			transport, peer, data, size, reliable, error_out);

	return transport->provider->ops->send(transport->provider->context,
		transport->token, peer, data, size, reliable, error_out);
}

enum mh_error_code
mh_transport_flush(struct mh_transport *transport, struct mh_error *error_out)
{
	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	return transport->provider->ops->flush(
		transport->provider->context, transport->token, error_out);
}

enum mh_error_code
mh_transport_disconnect(struct mh_transport *transport, unsigned long peer,
	struct mh_error *error_out)
{
	enum mh_error_code status;

	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	status = transport->provider->ops->disconnect(
		transport->provider->context, transport->token, peer,
		error_out);

	if (status == MH_ERROR_NONE)
		mh_transport_channel_remove(transport, peer);

	return status;
}

enum mh_error_code
mh_transport_peer_address(const struct mh_transport *transport,
	unsigned long peer, struct mh_transport_address *output,
	struct mh_error *error_out)
{
	const struct mh_transport_provider *provider;

	if (transport == NULL || peer == 0 || output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid transport peer query");

	provider = transport->provider;

	if (provider->ops->peer_address == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"transport peer addresses are unavailable");

	return provider->ops->peer_address(
		provider->context, transport->token, peer, output, error_out);
}

enum mh_error_code
mh_transport_advertise(struct mh_transport *transport,
	const struct mh_discovery_advertisement *game,
	struct mh_error *error_out)
{
	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	if (transport->provider->discovery == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"network discovery is unavailable");

	return transport->provider->discovery->advertise(
		transport->provider->context, transport->token, game,
		error_out);
}

enum mh_error_code
mh_transport_search_begin(struct mh_transport *transport,
	const struct mh_transport_target *target, unsigned long token,
	struct mh_error *error_out)
{
	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	if (transport->provider->discovery == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"network discovery is unavailable");

	if (target == NULL || target->provider != transport->provider)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"transport target belongs to another provider");

	return transport->provider->discovery->begin(
		transport->provider->context, transport->token, target->token,
		token, error_out);
}

enum mh_error_code
mh_transport_search_poll(
	struct mh_transport *transport, int *done, struct mh_error *error_out)
{
	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	if (transport->provider->discovery == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"network discovery is unavailable");

	return transport->provider->discovery->poll(
		transport->provider->context, transport->token, done,
		error_out);
}

unsigned int
mh_transport_port(const struct mh_transport *transport)
{
	return transport == NULL || transport->provider->ops->port == NULL
		? 0
		: transport->provider->ops->port(
			transport->provider->context, transport->token);
}

double
mh_transport_now(struct mh_transport *transport)
{
	return transport == NULL
		? 0
		: transport->provider->ops->now(
			transport->provider->context, transport->token);
}

const struct mh_discovery_catalog *
mh_transport_search_results(const struct mh_transport *transport)
{
	return transport == NULL || transport->provider->discovery == NULL
		? NULL
		: transport->provider->discovery->results(
			transport->provider->context, transport->token);
}
