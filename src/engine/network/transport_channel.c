#include <stdlib.h>
#include <string.h>

#include "engine/network/transport_channel.h"

static struct mh_channel_peer *
find_peer(const struct mh_transport *transport, unsigned long id)
{
	unsigned int i;

	if (transport->channels != NULL && id != 0)
		for (i = 0; i < MH_CHANNEL_PEERS; i++)
			if (transport->channels->peers[i].id == id)
				return &transport->channels->peers[i];

	return NULL;
}

enum mh_error_code
mh_transport_enable_channel(
	struct mh_transport *transport, struct mh_error *error_out)
{
	if (transport == NULL)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "missing transport");

	if (transport->channels == NULL) {
		transport->channels = calloc(1, sizeof(*transport->channels));

		if (transport->channels == NULL)
			return mh_error_set(error_out, MH_ERROR_MEMORY,
				"cannot allocate OpenMH channels");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_transport_peer_timing(const struct mh_transport *transport, unsigned long id,
	struct mh_transport_timing *output, struct mh_error *error_out)
{
	struct mh_channel_peer *peer;
	struct mh_transport_timing timing;

	if (transport == NULL || output == NULL || id == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid transport timing query");

	if (transport->channels == NULL)
		return mh_error_set(error_out, MH_ERROR_UNSUPPORTED,
			"transport timing unavailable");

	peer = find_peer(transport, id);

	if (peer == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"transport peer is not connected");

	memset(&timing, 0, sizeof(timing));
	timing.samples = peer->channel.sample_count;
	timing.latest_ms = peer->channel.latest_ms;

	if (timing.samples != 0)
		timing.mean_ms = peer->channel.sum_ms / timing.samples;

	*output = timing;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

void
mh_transport_channel_remove(struct mh_transport *transport, unsigned long id)
{
	struct mh_channel_peer *peer;

	peer = find_peer(transport, id);

	if (peer != NULL)
		memset(peer, 0, sizeof(*peer));
}

enum mh_error_code
mh_transport_channel_send(struct mh_transport *transport, unsigned long id,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out)
{
	unsigned char packet[MH_NET_WIRE_PACKET_LIMIT];
	struct mh_channel_peer *peer;
	double now;
	enum mh_error_code status;

	peer = find_peer(transport, id);

	if (peer == NULL)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"transport peer is not connected");

	if (size > sizeof(packet) - MH_CHANNEL_HEADER_BYTES ||
		(size != 0 && data == NULL))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"OpenMH channel payload too large");

	now = mh_transport_now(transport);
	mh_channel_header(&peer->channel, now, packet);

	if (size != 0)
		memcpy(packet + MH_CHANNEL_HEADER_BYTES, data, size);

	status = transport->provider->ops->send(transport->provider->context,
		transport->token, id, packet, size + MH_CHANNEL_HEADER_BYTES,
		reliable, error_out);

	/* Skip a stale snapshot or keepalive when the carrier is saturated. */
	if (status == MH_ERROR_BACKPRESSURE && !reliable) {
		peer->channel.sent_ms = now;
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	if (status == MH_ERROR_NONE)
		mh_channel_sent(&peer->channel, now, reliable);

	return status;
}

enum mh_error_code
mh_transport_channel_event(struct mh_transport *transport,
	struct mh_transport_event *event, struct mh_error *error_out)
{
	struct mh_channel_peer *peer;
	unsigned int i;
	double now;

	if (event->type == MH_TRANSPORT_CONNECT) {
		if (find_peer(transport, event->peer) != NULL)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		for (i = 0; i < MH_CHANNEL_PEERS; i++)
			if (transport->channels->peers[i].id == 0)
				break;

		if (event->peer == 0 || i == MH_CHANNEL_PEERS)
			return mh_error_set(error_out, MH_ERROR_RANGE,
				"OpenMH channel peer limit reached");

		peer = &transport->channels->peers[i];
		memset(peer, 0, sizeof(*peer));
		peer->id = event->peer;
		peer->channel.sent_ms = mh_transport_now(transport);
	} else if (event->type == MH_TRANSPORT_DISCONNECT)
		mh_transport_channel_remove(transport, event->peer);
	else if (event->type == MH_TRANSPORT_RECEIVE) {
		peer = find_peer(transport, event->peer);
		now = mh_transport_now(transport);

		if (peer == NULL || event->size > sizeof(event->data) ||
			mh_channel_receive(&peer->channel, event->data,
				event->size, event->reliable, now,
				NULL) != MH_ERROR_NONE) {
			event->type = MH_TRANSPORT_INVALID;
			event->size = 0;
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}

		event->size -= MH_CHANNEL_HEADER_BYTES;
		memmove(event->data, event->data + MH_CHANNEL_HEADER_BYTES,
			event->size);

		if (event->size == 0)
			event->type = MH_TRANSPORT_ACTIVITY;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_transport_channel_idle(
	struct mh_transport *transport, struct mh_error *error_out)
{
	struct mh_channel_peer *peer;
	unsigned int i;
	double now;
	enum mh_error_code status;

	now = mh_transport_now(transport);

	for (i = 0; i < MH_CHANNEL_PEERS; i++) {
		peer = &transport->channels->peers[i];

		if (peer->id == 0 ||
			now - peer->channel.sent_ms < MH_CHANNEL_IDLE_MS)
			continue;

		status = mh_transport_channel_send(
			transport, peer->id, NULL, 0, 0, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
