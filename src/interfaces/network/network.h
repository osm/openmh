#ifndef MH_INTERFACES_NETWORK_NETWORK_H
#define MH_INTERFACES_NETWORK_NETWORK_H

#include "engine/core/error.h"
#include "engine/network/discovery.h"
#include "engine/network/protocol.h"

struct mh_transport;
struct mh_transport_target;
struct mh_transport_provider;

enum {
	MH_TRANSPORT_ADDRESS_BYTES = 64
};

/* Copied numeric host text and host-order port; no socket or resolver state. */
struct mh_transport_address {
	char		host[MH_TRANSPORT_ADDRESS_BYTES];
	unsigned int	port;
};

enum mh_transport_event_type {
	MH_TRANSPORT_NONE,
	MH_TRANSPORT_CONNECT,
	MH_TRANSPORT_RECEIVE,
	MH_TRANSPORT_DISCONNECT,
	MH_TRANSPORT_INVALID,
	MH_TRANSPORT_ACTIVITY
};

struct mh_transport_event {
	struct mh_net_challenge		challenge;
	enum mh_transport_event_type	type;
	unsigned long			peer;
	int				reliable;
	size_t				size;

	/*
	 * Capacity for patched aggregates; each provider enforces its wire
	 * limit. RECEIVE carries packet bytes. DISCONNECT may carry a copied
	 * diagnostic string (size includes its NUL); size zero means no reason.
	 * Other event types leave size zero.
	 */
	unsigned char			data[MH_NET_WIRE_PACKET_LIMIT];
};

struct mh_transport_timing {
	unsigned int	samples;
	double		latest_ms, mean_ms;
};

/*
 * Borrow the selected provider until close. NULL means unavailable. Failed
 * open leaves output unchanged. Peer/event IDs and copied packet bytes are
 * portable; provider tokens, sockets and SDK types stay private. NULL local
 * selects the provider's default local endpoint. Targets must belong to the
 * selected provider. Preparation/connection may remain pending after open or
 * connect; polling advances it and reports failure. Close cancels pending work
 * without waiting. These operations do not require addresses or ports.
 */
enum mh_error_code	mh_transport_open_from(
	const struct mh_transport_provider *provider, int server,
	const struct mh_transport_target *local, struct mh_transport **output,
	struct mh_error *error_out);

enum mh_error_code	mh_transport_connect(struct mh_transport *transport,
	const struct mh_transport_target *target, struct mh_error *error_out);

/*
 * Target payloads belong to their creating provider and are immutable. Borrow
 * that provider until target close. Open/connect/search consume the target only
 * for the call; it can then be closed, or retained for reconnect. Closing a
 * target never cancels a connection; transport close cancels deferred work.
 */
void	mh_transport_target_close(struct mh_transport_target *target);

/* wait_ms=0 never waits; positive native waits are optional (<=1000 ms). */
enum mh_error_code	mh_transport_poll(struct mh_transport *transport,
	unsigned int wait_ms, struct mh_transport_event *out,
	struct mh_error *error_out);

/*
 * One complete message to one connected, nonzero peer ID. Providers support
 * payloads of 1..MH_NET_WIRE_PACKET_LIMIT bytes without exposing fragmentation.
 * Reliable=1 requests ordered retransmission and duplicate suppression within
 * this connection; reliable=0 permits loss and reordering. There is no ordering
 * guarantee between these classes. RECEIVE reports the delivery class.
 *
 * Success means the provider copied/accepted the bytes, not that the remote
 * game consumed them. Sends never wait. Providers bound pending storage and
 * return MH_ERROR_RANGE when full, without accepting the rejected message.
 * Other errors likewise leave that message unaccepted. There is no partial
 * send or implicit caller retry; the client session treats send failure as
 * peer failure. Close/disconnect can discard pending messages.
 */
enum mh_error_code	mh_transport_send(struct mh_transport *transport,
	unsigned long peer, const unsigned char *data, size_t size,
	int reliable, struct mh_error *error_out);

/* Submit queued messages without receiving events or waiting for replies. */
enum mh_error_code	mh_transport_flush(struct mh_transport *transport,
	struct mh_error *error_out);
enum mh_error_code	mh_transport_disconnect(struct mh_transport *transport,
	unsigned long peer, struct mh_error *error_out);

/*
 * Optional connected-peer query. No DNS or I/O; output survives later polls and
 * disconnects. Failure preserves output; unsupported providers return
 * MH_ERROR_UNSUPPORTED. The numeric host excludes a port or IPv6 brackets.
 */
enum mh_error_code	mh_transport_peer_address(
	const struct mh_transport *transport, unsigned long peer,
	struct mh_transport_address *output, struct mh_error *error_out);

/*
 * Optional OpenMH sequencing, enabled before the first connection event.
 * Both ends must enable it. Adds a 16-byte envelope within the provider limit;
 * ordinary packets carry ACKs, idle connections send bounded keepalives.
 * Reliability and payload ordering remain the provider's responsibility.
 * ACTIVITY is a valid keepalive with no application payload.
 */
enum mh_error_code	mh_transport_enable_channel(
	struct mh_transport *transport, struct mh_error *error_out);

/*
 * ACK-derived RTT with peer ACK scheduling delay removed. No samples means
 * unknown, not zero latency. Unsupported on transports without the envelope.
 */
enum mh_error_code	mh_transport_peer_timing(
	const struct mh_transport *transport, unsigned long peer,
	struct mh_transport_timing *output, struct mh_error *error_out);

/* Zero when unavailable, including providers without a local port. */
unsigned int	mh_transport_port(const struct mh_transport *transport);

/* Provider-relative elapsed milliseconds. */
double	mh_transport_now(struct mh_transport *transport);
void	mh_transport_close(struct mh_transport *transport);

/*
 * Optional addressed discovery. NULL disables advertising. Publishing metadata
 * does not create peers or consume gameplay slots. Main thread only; providers
 * own their wire format, search timing and required advertisement fields.
 */
enum mh_error_code	mh_transport_advertise(struct mh_transport *transport,
	const struct mh_discovery_advertisement *game,
	struct mh_error *error_out);

/*
 * Begin discovery using a target from this provider. A distinct nonzero 32-bit
 * request token identifies each search; it need not appear on the wire. Polling
 * advances preparation and the search, without waiting. Results may be NULL
 * during preparation. Providers without address-based discovery omit this
 * capability; ordinary transport/session operations do not depend on it.
 */
enum mh_error_code			 mh_transport_search_begin(
	struct mh_transport *transport,
	const struct mh_transport_target *target, unsigned long token,
	struct mh_error *error_out);
enum mh_error_code			 mh_transport_search_poll(
	struct mh_transport *transport, int *done, struct mh_error *error_out);
const struct mh_discovery_catalog	*mh_transport_search_results(
	const struct mh_transport *transport);

#endif
