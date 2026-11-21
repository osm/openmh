#ifndef MH_NET_TRANSPORT_CHANNEL_H
#define MH_NET_TRANSPORT_CHANNEL_H

#include "engine/network/channel.h"
#include "engine/network/transport_internal.h"

#define MH_CHANNEL_PEERS 32

struct mh_channel_peer {
	unsigned long		id;
	struct mh_channel	channel;
};

struct mh_transport_channels {
	struct mh_channel_peer	peers[MH_CHANNEL_PEERS];
};

enum mh_error_code	mh_transport_channel_send(
	struct mh_transport *transport, unsigned long peer,
	const unsigned char *data, size_t size, int reliable,
	struct mh_error *error_out);
enum mh_error_code	mh_transport_channel_event(
	struct mh_transport *transport, struct mh_transport_event *event,
	struct mh_error *error_out);
enum mh_error_code	mh_transport_channel_idle(
	struct mh_transport *transport, struct mh_error *error_out);
void			mh_transport_channel_remove(
	struct mh_transport *transport, unsigned long peer);

#endif
