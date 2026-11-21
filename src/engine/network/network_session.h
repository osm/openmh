#ifndef MH_NET_NETWORK_SESSION_H
#define MH_NET_NETWORK_SESSION_H

#include "engine/core/error.h"
#include "engine/input/chat.h"
#include "engine/network/session.h"

struct mh_network;
struct mh_transport_provider;
struct mh_transport_target;

/* Reason text is borrowed until the next step/flush/drain or close. */
struct mh_network_issue {
	unsigned long	peer;
	struct mh_error	reason;
};

/*
 * Main-thread owner of a portable session, its chat draft and one transport.
 * On success, output owns the connection; close releases it. Failure preserves
 * output. Accessors return borrowed state that remains valid until close.
 * Local/remote targets belong to the provider; NULL local uses its default.
 * Client requires a remote target. Targets are borrowed only during open;
 * asynchronous work belongs to the transport. A failed peer does not kill host.
 * The returned session may be passed to its local profile/chat/leave APIs.
 */
/* Explicit provider selection; borrowed until network close. */
enum mh_error_code	mh_network_open_from(
	const struct mh_transport_provider *provider, int host,
	const struct mh_transport_target *local,
	const struct mh_transport_target *remote,
	const struct mh_net_session_config *config, struct mh_network **output,
	struct mh_error *error_out);

/* Enable event diagnostics for this owner; disabled by default. */
void	mh_network_set_trace(struct mh_network *network, int enabled);

struct mh_net_session	*mh_network_session(struct mh_network *network);

/*
 * Display/discovery session name, <=15 legacy bytes (empty allowed).
 * Default Motorhead. Clients may supply the selected discovery name; this
 * metadata does not alter the original application-message protocol.
 */
enum mh_error_code	mh_network_set_name(struct mh_network *network,
	const char *name, struct mh_error *error_out);
const char	       *mh_network_name(const struct mh_network *network);

/* Draft history survives race/lobby transitions on this connection. */
struct mh_chat	*mh_network_chat_state(struct mh_network *network);
unsigned int	 mh_network_port(const struct mh_network *network);
double		 mh_network_now(struct mh_network *network);

/*
 * Submit pending application/transport output without receiving or ticking
 * the session. Used before blocking race resource loading. Existing peer
 * send failures follow the ordinary removal/issue policy.
 */
enum mh_error_code	mh_network_flush(struct mh_network *network,
	struct mh_network_issue *issue, struct mh_error *error_out);

/*
 * At most one received event per call; call again with wait_ms=0 to drain.
 * Peer errors are reported in issue; fatal local failures return an error.
 * Refreshes discovery and sends pending output, polls and dispatches one event,
 * then ticks the session using a fresh transport time and sends resulting
 * output. Loaded patched sessions instead tick/send before polling, so new
 * race input affects subsequent sends. A full transport queue disconnects
 * that slow peer (bounded native policy). Completed I/O and session events are
 * not rolled back if a later stage fails. wait_ms is capped at 1000; zero does
 * not wait.
 */
enum mh_error_code	mh_network_step(struct mh_network *network,
	unsigned int wait_ms, struct mh_network_issue *issue,
	struct mh_error *error_out);

/*
 * Nonblocking bounded drain for a render frame, stops at the first idle poll.
 * limit must be 1..256. Fatal errors return; a peer issue stops the drain
 * after the current step so it is not lost.
 * Loaded patched sessions tick once before the drain, not between its events.
 */
enum mh_error_code	mh_network_drain(struct mh_network *network,
	unsigned int limit, struct mh_network_issue *issue,
	struct mh_error *error_out);

/*
 * Observe after each successful step, including the final idle poll or a
 * reported peer issue, before the bounded chat window can roll over. Callback
 * errors stop the drain without consuming another event; the completed step
 * remains committed. The observer and its context are borrowed for this call.
 */
enum mh_error_code	mh_network_drain_observed(struct mh_network *network,
	unsigned int limit, struct mh_network_issue *issue,
	enum mh_error_code (*observe)( void *context, struct mh_error *error_out),
	void *context, struct mh_error *error_out);
void			mh_network_close(struct mh_network *network);

#endif
