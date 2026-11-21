#ifndef MH_NET_DISCOVERY_H
#define MH_NET_DISCOVERY_H

#include "engine/core/error.h"
#include "engine/network/protocol.h"

#define MH_DISCOVERY_SESSIONS 9
#define MH_DISCOVERY_LABEL_BYTES 16

enum mh_discovery_password {
	MH_DISCOVERY_PASSWORD_NONE,
	MH_DISCOVERY_PASSWORD_REQUIRED,
	MH_DISCOVERY_PASSWORD_UNKNOWN
};

struct mh_discovery_advertisement {
	char	name[MH_DISCOVERY_LABEL_BYTES], track[MH_DISCOVERY_LABEL_BYTES];
	unsigned int	players, password, racing, mode, laps, car_count;

	/*
	 * Optional complete description; summary-only providers leave this
	 * zero.
	 */
	int			has_roster;
	struct mh_net_roster	roster;
};

struct mh_discovery_entry {
	unsigned long
		ipv4; /* Host-order numeric IPv4, independent of OS types. */
	unsigned int				port;
	struct mh_discovery_advertisement	game;
};

struct mh_discovery_catalog {
	unsigned int			count;
	struct mh_discovery_entry	entries[MH_DISCOVERY_SESSIONS];
};

/*
 * Keep the first nine endpoints (original effective list limit). Repeated
 * UDP replies update an existing endpoint without reordering. Endpoint
 * deduplication is native policy. Errors preserve the catalog.
 */
enum mh_error_code	mh_discovery_collect(
	struct mh_discovery_catalog *catalog, unsigned long ipv4,
	unsigned int port, const struct mh_discovery_advertisement *game,
	struct mh_error *error_out);

#endif
