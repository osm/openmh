#ifndef MH_BACKENDS_NETWORK_BROWSER_PROVIDER_H
#define MH_BACKENDS_NETWORK_BROWSER_PROVIDER_H

#include "interfaces/network/network.h"

enum {
	MH_BROWSER_ICE_SERVERS = 8,
	MH_BROWSER_SIGNAL_URL_BYTES = 2048,
	MH_BROWSER_ICE_URL_BYTES = 1024,
	MH_BROWSER_ICE_CREDENTIAL_BYTES = 256
};

/* One STUN/TURN URL per entry; username/credential may be NULL. */
struct mh_browser_ice_server {
	const char	*url, *username, *credential;
};

struct mh_browser_network_config {
	const char				*signaling_url;
	const struct mh_browser_ice_server	*ice_servers;
	unsigned int				ice_server_count;
	int					relay_only;
};

const struct mh_transport_provider	*mh_browser_network_provider(void);

/*
 * Copy configuration without I/O. Success owns a target; failure preserves
 * output. Connect copies it again before starting asynchronous browser work.
 * The endpoint accepts POST application/sdp and returns an SDP answer. HTTPS
 * protects ICE credentials and certificate fingerprints in deployment.
 * Each transport accepts one connection attempt; reopen for another attempt.
 */
enum mh_error_code	mh_browser_network_target(
	const struct mh_browser_network_config *config,
	struct mh_transport_target **output, struct mh_error *error_out);

#endif
