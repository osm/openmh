#ifndef MH_NETWORK_ADDRESS_H
#define MH_NETWORK_ADDRESS_H

#include "interfaces/network/network.h"

/*
 * Optional address-based target preparation, used by the native address book
 * and diagnostics. The provider copies the address; this call never resolves
 * names or waits for a connection. Providers without addresses return
 * UNSUPPORTED and construct targets through their own host integration.
 * Success transfers one target to the caller; failure preserves output.
 */
enum mh_error_code	mh_transport_target_address(
	const struct mh_transport_provider *provider, const char *host,
	unsigned int port, struct mh_transport_target **output,
	struct mh_error *error_out);

/* Address conveniences; temporary targets are released before return. */
enum mh_error_code	mh_transport_open_address(
	const struct mh_transport_provider *provider, int server,
	const char *host, unsigned int port, struct mh_transport **output,
	struct mh_error *error_out);
enum mh_error_code	mh_transport_connect_address(
	struct mh_transport *transport, const char *host, unsigned int port,
	struct mh_error *error_out);
enum mh_error_code	mh_transport_search_address(
	struct mh_transport *transport, const char *host, unsigned int port,
	unsigned long request, struct mh_error *error_out);

#endif
