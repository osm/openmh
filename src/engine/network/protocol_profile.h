#ifndef MH_NET_PROTOCOL_PROFILE_H
#define MH_NET_PROTOCOL_PROFILE_H

#include "engine/network/protocol.h"

/* Game dialects are independent of the transport and shared message layouts. */
enum mh_net_protocol_id {
	MH_NET_PROTOCOL_LEGACY,
	MH_NET_PROTOCOL_OPENMH
};

enum mh_net_protocol_feature {
	MH_NET_PROTOCOL_APPLICATION_PING = 1,
	MH_NET_PROTOCOL_LOADED_ACK = 2,
	MH_NET_PROTOCOL_TIMED_CHANNEL = 4
};

struct mh_net_protocol {
	const char		*name;
	unsigned long		version_bits;
	unsigned int		timing_version;
	unsigned int		features;
	int			(*valid_rate)(unsigned int rate);
	void (*version_rejection)(
		unsigned long	client, struct mh_net_message *reply);
};

const struct mh_net_protocol	*mh_net_protocol_get(
	enum mh_net_protocol_id id);
enum mh_net_protocol_id		 mh_net_protocol_default(void);
int				 mh_net_protocol_ping_message(
	unsigned int type);

/* Zero rate selects original timing at the public session boundary only. */
const struct mh_net_protocol	*mh_net_protocol_for_rate(unsigned int rate);
int				 mh_net_protocol_has(
	const struct mh_net_protocol *protocol,
	enum mh_net_protocol_feature feature);

/* Validate timing and fill an already initialized acceptance message. */
int	mh_net_protocol_accepts(const struct mh_net_protocol *protocol,
	unsigned int rate);
void	mh_net_protocol_accept(const struct mh_net_protocol *protocol,
	unsigned int rate, struct mh_net_message *reply);

#endif
