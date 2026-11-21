#ifndef MH_BACKENDS_UDP_TRANSPORT_H
#define MH_BACKENDS_UDP_TRANSPORT_H

#include "backends/network/udp_challenge.h"
#include "backends/network/udp_control.h"
#include "backends/network/udp_multiplex.h"
#include "backends/network/udp_protocol.h"
#include "backends/network/udp_shared.h"
#include "interfaces/network/resolver.h"

#define MH_UDP_REPLY_BUCKETS 64
#define MH_UDP_PEERS 12
#define MH_UDP_FIRST_PEER 10
#define MH_UDP_SERVER_PEER 1
#define MH_UDP_POLL_PACKETS 32

enum mh_udp_phase {
	MH_UDP_IDLE,
	MH_UDP_RESOLVING,
	MH_UDP_CONNECTING,
	MH_UDP_CONNECTED
};

/*
 * Fixed slots keep delivery callback contexts stable. Legacy hosts own peer
 * sockets; clients and shared-port hosts borrow the transport socket.
 */
struct mh_udp_peer {
	int				congested;
	struct mh_net_challenge		challenge;
	struct mh_udp_socket		*socket;
	struct mh_udp_address		address;
	struct mh_reliable_delivery	*delivery;
	double				contact_ms;
	unsigned long			client_generation, server_generation;
};

enum mh_udp_search_state {
	MH_UDP_SEARCH_IDLE,
	MH_UDP_SEARCH_RESOLVING,
	MH_UDP_SEARCH_ACTIVE,
	MH_UDP_SEARCH_DONE
};

/* Owns its query socket and resolver; cancellation retains catalog results. */
struct mh_udp_search {
	unsigned long			generation;
	enum mh_udp_search_state	state;
	struct mh_udp_socket		*socket;
	struct mh_resolver		*lookup;
	struct mh_udp_address		address;
	double				deadline_ms;
	struct mh_discovery_catalog	catalog;
};

struct mh_udp_transport {
	unsigned char				cookie_key[MH_SHA256_BYTES];
	unsigned int	reply_count, reply_buckets[MH_UDP_REPLY_BUCKETS];
	double					reply_reset_ms;

	/* Borrowed and immutable until transport close. */
	const struct mh_udp_transport_config	*config;
	const struct mh_udp_protocol		*protocol;
	int					server;
	struct mh_udp_socket			*socket;
	struct mh_udp_address			local, target;
	struct mh_resolver			*bind_lookup, *target_lookup;
	struct mh_udp_peer			peers[MH_UDP_PEERS];
	enum mh_udp_phase			phase;
	unsigned int	attempts, next_peer, disconnected;
	unsigned long				clock_word;
	double					elapsed_ms, retry_ms;
	unsigned long				connection_generation;
	char					name[MH_UDP_NAME_BYTES];
	unsigned char				advertisement[MH_RELIABLE_DATAGRAM_BYTES];
	size_t					advertisement_size;
	struct mh_udp_search			search;
};

enum mh_error_code	mh_udp_receive_socket(
	struct mh_udp_transport *transport, struct mh_udp_address *remote,
	unsigned char *data, size_t capacity, size_t *size, int *received,
	struct mh_error *error_out);

extern const struct mh_transport_discovery_ops mh_udp_discovery_ops;
void			mh_udp_peer_finish(struct mh_udp_transport *transport,
	struct mh_udp_peer *peer);
enum mh_error_code	mh_udp_receive_delivery(
	struct mh_udp_transport *transport, unsigned int slot,
	const unsigned char *data, size_t size,
	struct mh_transport_event *event, struct mh_error *error_out);
void			mh_udp_search_cancel(
	struct mh_udp_transport *transport);
int			mh_udp_discovery_request(
	struct mh_udp_transport *transport, const struct mh_udp_address *source,
	const unsigned char *data, size_t size);
enum mh_error_code	mh_udp_resolve(struct mh_resolver **lookup,
	struct mh_udp_address *address, int *ready, struct mh_error *error_out);

void			mh_udp_sample_time(struct mh_udp_transport *transport);
void			mh_udp_peer_clear(struct mh_udp_transport *transport,
	struct mh_udp_peer *peer);
enum mh_error_code	mh_udp_bind_ready(struct mh_udp_transport *transport,
	int *ready, struct mh_error *error_out);
enum mh_error_code	mh_udp_connection_poll(
	struct mh_udp_transport *transport, struct mh_transport_event *event,
	struct mh_error *error_out);

#endif
