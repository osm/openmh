#ifndef MH_NET_SESSION_INTERNAL_H
#define MH_NET_SESSION_INTERNAL_H

#include "engine/core/error.h"
#include "engine/network/session.h"

#define MH_NET_STATUS_RATE 1

/* Shared controller predicates; in_race requires a non-NULL session. */
int				 mh_net_session_clock_valid(
	const struct mh_net_session *session, double now_ms);
int				 mh_net_session_in_race(
	const struct mh_net_session *session);
struct mh_net_connection	*mh_net_session_connection(
	struct mh_net_session *session, unsigned long peer);

/*
 * Private queue operations mutate the caller's working copy. Public session
 * operations own rollback; these helpers do not publish a transaction.
 * A NULL message queues only the delivery/disconnect metadata.
 */
enum mh_error_code	mh_net_session_queue(struct mh_net_session *session,
	unsigned long peer, const struct mh_net_message *message, int reliable,
	int disconnect, struct mh_error *error_out);
enum mh_error_code	mh_net_session_queue_batch(
	struct mh_net_session *session, unsigned long peer,
	const struct mh_net_batch *batch, int reliable,
	struct mh_error *error_out);

/*
 * Receive handlers share these operations with local controls and lifecycle
 * transitions. They operate on the receive/tick caller's working copy;
 * decoded messages and player indices must already be validated.
 */
unsigned int	mh_net_session_player_for(const struct mh_net_session *session,
	unsigned long peer);
void		mh_net_session_reset_replication(
	struct mh_net_session *session);
void		mh_net_session_remove_replica(struct mh_net_session *session,
	unsigned int player);

/* Patched race membership removal retains history, profile and return state. */
void			mh_net_session_remove_racer(
	struct mh_net_session *session, unsigned int player);
enum mh_error_code	mh_net_session_send_audience(
	struct mh_net_session *session, const struct mh_net_message *message,
	unsigned long except, unsigned int flags, struct mh_error *error_out);
void			mh_net_session_sync_roster(
	struct mh_net_session *session);
enum mh_error_code	mh_net_session_send_roster(
	struct mh_net_session *session, struct mh_error *error_out);
enum mh_error_code	mh_net_session_lobby_message(
	struct mh_net_session *session, const char *name, const char *suffix,
	unsigned long except, int local, int remote,
	struct mh_error *error_out);
enum mh_error_code	mh_net_session_race_message(
	struct mh_net_session *session, const char *name, const char *suffix,
	struct mh_error *error_out);
enum mh_error_code	mh_net_session_remove_player(
	struct mh_net_session *session, unsigned long peer, int announce,
	struct mh_error *error_out);
int			mh_net_session_game_valid(
	const struct mh_net_roster *roster, unsigned int capacity);
void			mh_net_session_add_chat(struct mh_net_session *session,
	const struct mh_net_message *message);
void			mh_net_session_set_notice(
	struct mh_net_session *session, const struct mh_net_message *message);

/*
 * Called on tick's working copy after liveness/roster processing. Status
 * refresh serves lobby peers too; replication sends only in WAITING/RACING.
 */
enum mh_error_code	mh_net_session_update_status(
	struct mh_net_session *session, struct mh_error *error_out);
enum mh_error_code	mh_net_session_replicate(struct mh_net_session *session,
	struct mh_error *error_out);

#endif
