#ifndef MH_NETWORK_BACKEND_H
#define MH_NETWORK_BACKEND_H

#include "interfaces/network/network.h"

/*
 * Descriptor/table/context are borrowed unchanged through transport close.
 * Main-thread operations; queued callbacks must detach before close returns.
 * Open owns one token, cleans partial failure and preserves output. Packet
 * sends copy bytes before returning; poll returns one copied event or NONE.
 * Target tokens are immutable and borrowed only for the call. Deferred work
 * must copy data or retain its own references before returning. Target release
 * does not cancel transport work or invalidate retained diagnostics.
 * poll and flush never wait. Optional native waits and discovery are separate
 * capabilities: another provider need not implement raw UDP or blocking waits.
 *
 * Connect may defer readiness until a later CONNECT/DISCONNECT event. The
 * provider owns name resolution and any deferred work. No threads are
 * required by this contract.
 * Reliable delivery may use the underlying channel's guarantees; providers
 * need not reproduce native ACKs, sequence numbers or retry timers. Queued
 * storage must remain bounded, including any SDK-owned send buffering. A full
 * queue is a send failure, not permission to block or silently drop reliable
 * messages. Original native queue/timer rules belong to its implementation.
 * Diagnostics belong to the provider, not a freed token. They remain readable
 * until its next operation other than close, so failed connection construction
 * can close its transport and still report the error.
 */
struct mh_transport_provider {
	const struct mh_transport_ops		*ops;
	const struct mh_transport_discovery_ops	*discovery;
	const struct mh_transport_address_ops	*addresses;
	void					*context;
};

struct mh_transport_ops {
	enum mh_error_code (*open)(void *context, int server, const void *local,
		void	**output, struct mh_error *error_out);
	void	(*close)(void *context, void *transport);
	enum mh_error_code (*connect)(void *context, void *transport,
		const void	*target, struct mh_error *error_out);
	enum mh_error_code (*poll)(void *context, void *transport,
		struct mh_transport_event	*output, struct mh_error *error_out);
	enum mh_error_code (*send)(void *context, void *transport,
		unsigned long peer, const unsigned char *data, size_t size,
		int	reliable, struct mh_error *error_out);
	enum mh_error_code (*flush)(
		void	*context, void *transport, struct mh_error *error_out);
	enum mh_error_code (*disconnect)(void *context, void *transport,
		unsigned long	peer, struct mh_error *error_out);

	/* Optional local port; providers without ports leave this NULL. */
	unsigned int	(*port)(void *context, const void *transport);
	double	(*now)(void *context, void *transport);

	/* Optional native/diagnostic wait. Ordinary host steps use poll. */
	enum mh_error_code (*wait)(void *context, void *transport,
		unsigned int milliseconds, struct mh_transport_event *output,
		struct mh_error			*error_out);

	/* Optional connected-peer address; copy output only on success. */
	enum mh_error_code (*peer_address)(void *context, void *transport,
		unsigned long peer, struct mh_transport_address *output,
		struct mh_error			*error_out);
};

/*
 * Optional discovery sharing a transport token; never required for messaging.
 */
struct mh_transport_discovery_ops {
	enum mh_error_code (*advertise)(void *context, void *transport,
		const struct mh_discovery_advertisement *game,
		struct mh_error	*error_out);
	enum mh_error_code (*begin)(void *context, void *transport,
		const void *target, unsigned long token,
		struct mh_error	*error_out);
	enum mh_error_code (*poll)(void *context, void *transport, int *done,
		struct mh_error	*error_out);
	const struct mh_discovery_catalog *(*results)(
		void		*context, const void *transport);
};

/*
 * Optional address-book adapter; no lookup or I/O during prepare. Success owns
 * a copied private target token; failure frees partial state and preserves
 * output. Release frees that token without invalidating provider diagnostics.
 */
struct mh_transport_address_ops {
	enum mh_error_code (*prepare)(void *context, const char *host,
		unsigned int	port, void **output, struct mh_error *error_out);
	void			(*release)(void *context, void *target);
};

/*
 * Backend construction: success transfers token to the target wrapper, failure
 * leaves token with the caller. NULL release means token is borrowed through
 * target close. Release must not invalidate the provider's diagnostic text.
 * The provider descriptor/context is borrowed through target close. This is
 * independent of optional address preparation: any provider can wrap its own
 * invitation, channel description or other private target representation.
 */
enum mh_error_code	mh_transport_target_create(
	const struct mh_transport_provider *provider, void *token,
	void (*release)(void *, void *), struct mh_transport_target **output,
	struct mh_error *error_out);

/* No I/O; incomplete required tables leave the descriptor unchanged. */
enum mh_error_code	mh_transport_provider_init(
	struct mh_transport_provider *provider,
	const struct mh_transport_ops *ops,
	const struct mh_transport_discovery_ops *discovery,
	const struct mh_transport_address_ops *addresses, void *context,
	struct mh_error *error_out);
#endif
