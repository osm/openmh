#ifndef MH_CLIENT_HOSTS_NETWORK_H
#define MH_CLIENT_HOSTS_NETWORK_H

struct mh_transport_provider;

/* Borrowed descriptor; NULL when the host omits networking. */
const struct mh_transport_provider	*mh_host_network(void);
const struct mh_transport_provider	*mh_host_legacy_network(void);

#endif
