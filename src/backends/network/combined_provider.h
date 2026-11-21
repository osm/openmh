#ifndef MH_BACKENDS_NETWORK_COMBINED_PROVIDER_H
#define MH_BACKENDS_NETWORK_COMBINED_PROVIDER_H

#include "interfaces/network/network.h"

enum {
	MH_COMBINED_PEERS = 64
};

/*
 * Combine two already-open server transports into one event stream. Success
 * transfers both transports; failure preserves output and caller ownership.
 * Their provider descriptors/contexts must still outlive the combined owner.
 * Do not use or close the children separately after a successful transfer.
 *
 * Primary supplies the clock, listen port and discovery advertisement. Both
 * children are polled fairly, without waits. Targets/searches are unsupported.
 * Children must emit DISCONNECT after a successful local disconnect and before
 * reusing that child peer ID. This releases the connection's mapping even when
 * queued receives are discarded while it closes.
 * Peer IDs are translated independently of child IDs and never reused during
 * this owner's lifetime. Up to MH_COMBINED_PEERS connections may coexist;
 * excess connections are disconnected without allocating more state.
 */
enum mh_error_code	mh_combined_transport_open(struct mh_transport *primary,
	struct mh_transport *secondary, struct mh_transport **output,
	struct mh_error *error_out);

#endif
