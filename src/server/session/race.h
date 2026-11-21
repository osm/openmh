#ifndef MH_SERVER_RACE_H
#define MH_SERVER_RACE_H

#include "engine/network/elimination.h"
#include "engine/network/start.h"
#include "server/session/clock.h"
#include "server/session/progress.h"
#include "server/session/vehicle.h"

#define MH_SERVER_NOTICE_BYTES 256

/* Owned by the host; courses referenced by progress outlive the host. */
struct mh_server_race {
	int				active;
	double				origin_ms;
	unsigned long			elapsed_ms, heartbeat_ms;
	unsigned long			sent_ms[MH_NET_WIRE_PLAYERS];
	unsigned int			notice_valid;
	char				notices[MH_NET_WIRE_PLAYERS][MH_SERVER_NOTICE_BYTES];
	struct mh_net_start		start;
	struct mh_server_clock		clock;
	struct mh_net_elimination	elimination;
	struct mh_server_progress	progress[MH_NET_WIRE_PLAYERS];
	struct mh_server_vehicle	vehicles[MH_NET_WIRE_PLAYERS];
	struct mh_server_result		results[MH_NET_WIRE_PLAYERS];
};

#endif
