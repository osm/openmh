#include <limits.h>
#include <stdlib.h>
#include <string.h>

#include "backends/network/combined_provider.h"
#include "interfaces/network/backend.h"

struct combined_peer {
	unsigned long	id, child_id;
	unsigned int	child;
	int		closing;
};

struct combined_transport {
	struct mh_transport	*children[2];
	struct combined_peer	peers[MH_COMBINED_PEERS];
	unsigned int		next_child;
	unsigned long		last_id;
};

static enum mh_error_code
open_transport(void *context, int server, const void *local, void **output,
	struct mh_error *error_out)
{
	const struct combined_transport *configuration;
	struct combined_transport *transport;

	(void)context;
	configuration = local;

	if (!server || configuration == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"combined transport requires two server transports");

	transport = calloc(1, sizeof(*transport));

	if (transport == NULL)
		return mh_error_set(error_out, MH_ERROR_MEMORY,
			"cannot allocate combined transport");

	transport->children[0] = configuration->children[0];
	transport->children[1] = configuration->children[1];
	*output = transport;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
close_transport(void *context, void *token)
{
	struct combined_transport *transport;

	(void)context;
	transport = token;
	mh_transport_close(transport->children[1]);
	mh_transport_close(transport->children[0]);
	free(transport);
}

static enum mh_error_code
connect_transport(void *context, void *token, const void *target,
	struct mh_error *error_out)
{
	(void)context;

	(void)token;
	(void)target;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"combined transport only accepts server connections");
}

static struct combined_peer *
find_peer(struct combined_transport *transport, unsigned long id)
{
	unsigned int i;

	if (id != 0)
		for (i = 0; i < MH_COMBINED_PEERS; i++)
			if (transport->peers[i].id == id)
				return &transport->peers[i];

	return NULL;
}

static enum mh_error_code
translate_event(struct combined_transport *transport, unsigned int child,
	struct mh_transport_event *event, struct mh_error *error_out)
{
	struct combined_peer *peer, *available;
	unsigned int i;

	if (event->type == MH_TRANSPORT_NONE)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	if (event->peer == 0 || event->size > sizeof(event->data) ||
		(event->type != MH_TRANSPORT_CONNECT &&
			event->type != MH_TRANSPORT_RECEIVE &&
			event->type != MH_TRANSPORT_DISCONNECT))
		return mh_error_set(error_out, MH_ERROR_BACKEND,
			"invalid event from combined transport child");

	peer = NULL;
	available = NULL;

	for (i = 0; i < MH_COMBINED_PEERS; i++) {
		if (transport->peers[i].id == 0)
			available = &transport->peers[i];
		else if (transport->peers[i].child == child &&
			transport->peers[i].child_id == event->peer)
			peer = &transport->peers[i];
	}

	if (event->type == MH_TRANSPORT_CONNECT) {
		/* A repeated CONNECT cannot create a second public identity. */
		if (peer != NULL) {
			event->type = MH_TRANSPORT_NONE;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		if (available == NULL || transport->last_id == ULONG_MAX) {
			event->type = MH_TRANSPORT_NONE;
			return mh_transport_disconnect(
				transport->children[child], event->peer,
				error_out);
		}

		peer = available;
		peer->id = ++transport->last_id;
		peer->child = child;
		peer->child_id = event->peer;
		peer->closing = 0;
	}

	/*
	 * Unadmitted/rejected children cannot inject messages into a session.
	 */
	if (peer == NULL ||
		(peer->closing && event->type == MH_TRANSPORT_RECEIVE)) {
		event->type = MH_TRANSPORT_NONE;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	event->peer = peer->id;

	if (event->type == MH_TRANSPORT_DISCONNECT)
		memset(peer, 0, sizeof(*peer));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
poll_transport(void *context, void *token, struct mh_transport_event *output,
	struct mh_error *error_out)
{
	struct combined_transport *transport;
	struct mh_transport_event event;
	unsigned int visited, child;
	enum mh_error_code status;

	(void)context;
	transport = token;

	/* One event per child bounds work even if a child only emits junk. */
	for (visited = 0; visited < 2; visited++) {
		child = transport->next_child;
		transport->next_child = 1 - child;
		status = mh_transport_poll(
			transport->children[child], 0, &event, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		status = translate_event(transport, child, &event, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		if (event.type != MH_TRANSPORT_NONE) {
			*output = event;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	memset(output, 0, sizeof(*output));

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
send_transport(void *context, void *token, unsigned long id,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	struct combined_transport *transport;
	struct combined_peer *peer;

	(void)context;
	transport = token;
	peer = find_peer(transport, id);

	if (peer == NULL || peer->closing)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"combined transport peer is not connected");

	return mh_transport_send(transport->children[peer->child],
		peer->child_id, data, size, reliable, error_out);
}

static enum mh_error_code
flush_transport(void *context, void *token, struct mh_error *error_out)
{
	struct combined_transport *transport;
	enum mh_error_code status;

	(void)context;
	transport = token;
	status = mh_transport_flush(transport->children[0], error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_transport_flush(transport->children[1], error_out);
}

static enum mh_error_code
disconnect_transport(void *context, void *token, unsigned long id,
	struct mh_error *error_out)
{
	struct combined_transport *transport;
	struct combined_peer *peer;
	enum mh_error_code status;

	(void)context;
	transport = token;
	peer = find_peer(transport, id);

	if (peer == NULL || peer->closing)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_transport_disconnect(
		transport->children[peer->child], peer->child_id, error_out);

	if (status == MH_ERROR_NONE)
		peer->closing = 1;

	return status;
}

static unsigned int
transport_port(void *context, const void *token)
{
	const struct combined_transport *transport;

	(void)context;
	transport = token;

	return mh_transport_port(transport->children[0]);
}

static double
transport_now(void *context, void *token)
{
	struct combined_transport *transport;

	(void)context;
	transport = token;

	return mh_transport_now(transport->children[0]);
}

static enum mh_error_code
peer_address(void *context, void *token, unsigned long id,
	struct mh_transport_address *output, struct mh_error *error_out)
{
	struct combined_transport *transport;
	struct combined_peer *peer;

	(void)context;
	transport = token;
	peer = find_peer(transport, id);

	if (peer == NULL || peer->closing)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"combined transport peer is not connected");

	return mh_transport_peer_address(transport->children[peer->child],
		peer->child_id, output, error_out);
}

static enum mh_error_code
advertise(void *context, void *token,
	const struct mh_discovery_advertisement *game,
	struct mh_error *error_out)
{
	struct combined_transport *transport;

	(void)context;
	transport = token;

	return mh_transport_advertise(transport->children[0], game, error_out);
}

static enum mh_error_code
search_begin(void *context, void *token, const void *target,
	unsigned long request, struct mh_error *error_out)
{
	(void)context;

	(void)token;
	(void)target;
	(void)request;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"combined transport does not search for servers");
}

static enum mh_error_code
search_poll(void *context, void *token, int *done, struct mh_error *error_out)
{
	(void)context;
	(void)token;
	(void)done;

	return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
		"combined transport does not search for servers");
}

static const struct mh_discovery_catalog *
search_results(void *context, const void *token)
{
	(void)context;
	(void)token;

	return NULL;
}

static const struct mh_transport_ops operations = {open_transport,
	close_transport, connect_transport, poll_transport, send_transport,
	flush_transport, disconnect_transport, transport_port, transport_now,
	NULL, peer_address};
static const struct mh_transport_discovery_ops discovery = {
	advertise, search_begin, search_poll, search_results};
static const struct mh_transport_provider provider = {
	&operations, &discovery, NULL, NULL};

enum mh_error_code
mh_combined_transport_open(struct mh_transport *primary,
	struct mh_transport *secondary, struct mh_transport **output,
	struct mh_error *error_out)
{
	struct combined_transport configuration;
	struct mh_transport_target *target;
	enum mh_error_code status;

	if (primary == NULL || secondary == NULL || primary == secondary ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"combined transport requires two distinct children");

	memset(&configuration, 0, sizeof(configuration));
	configuration.children[0] = primary;
	configuration.children[1] = secondary;
	status = mh_transport_target_create(
		&provider, &configuration, NULL, &target, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status =
		mh_transport_open_from(&provider, 1, target, output, error_out);
	mh_transport_target_close(target);

	return status;
}
