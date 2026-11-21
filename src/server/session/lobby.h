#ifndef MH_SERVER_LOBBY_H
#define MH_SERVER_LOBBY_H

#include "server/session/admission.h"

struct mh_server_lobby {
	/* Internal slot zero is the server identity, never a driver. */
	struct mh_net_roster		roster;
	char				password[MH_NET_PASSWORD_BYTES];
	struct mh_server_admission	admission;
	enum mh_server_grid		grid;
	unsigned long			peers[MH_NET_WIRE_PLAYERS];
	unsigned long			pending_peers[MH_NET_WIRE_PLAYERS];
	double				contact_ms[MH_NET_WIRE_PLAYERS];
	unsigned long			ping_ms[MH_NET_WIRE_PLAYERS];
	double				heartbeat_ms;

	/* Waiting to refresh roster/results on lobby pong. */
	unsigned int			return_pending;

	/* Score-clear votes; internal slot zero forces a clear. */
	unsigned int			clear_votes;

	/*
	 * One replaceable kick target per player; zero = none.
	 */
	unsigned int			kick_votes[MH_NET_WIRE_PLAYERS];

	/* Reported position/lap differ from the last-race roster values. */
	unsigned int			reported_a[MH_NET_WIRE_PLAYERS];
	unsigned int			reported_b[MH_NET_WIRE_PLAYERS];
};

struct mh_server_lobby_tick {
	unsigned int	expired;
	int		heartbeat;
	unsigned long	stamp;
};

enum mh_error_code	mh_server_lobby_init(
	const struct mh_server_config *config, const char *track,
	struct mh_server_lobby *output, struct mh_error *error_out);
enum mh_error_code	mh_server_lobby_join(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned long peer,
	const struct mh_net_join *request, double now_ms,
	struct mh_net_message *reply, struct mh_error *error_out);

/*
 * Apply type 4/5 from the peer that reserved/owns its slot. Join produces a
 * roster broadcast; edit produces a profile update for other peers. Welcome
 * text and actual sends belong to the session coordinator. Failure preserves
 * state/output. Input message and output must not alias.
 */
enum mh_error_code	mh_server_lobby_profile(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned long peer,
	const struct mh_net_message *message, double now_ms,
	struct mh_net_message *broadcast, struct mh_error *error_out);

/* Copy the authoritative roster, suppressing the server's presence bits. */
void	mh_server_lobby_roster(const struct mh_server_lobby *lobby,
	struct mh_net_message *output);

/*
 * Membership portion of race return, after the caller has applied results and
 * scores. Retains result/profile fields and pass/recording flags. The race
 * owner separately resets voting/countdown state. Includes waiting players.
 */
void	mh_server_lobby_resume(struct mh_server_lobby *lobby);

/* Setup must wait for every pending lobby client, including passing players. */
int	mh_server_lobby_synchronized(const struct mh_server_lobby *lobby);

/*
 * Release reservations and driver presence for a disconnected transport peer.
 */
int	mh_server_lobby_disconnect(struct mh_server_lobby *lobby,
	unsigned long peer);

/*
 * Expiry precedes heartbeat delivery and incoming packets in the original loop.
 * The caller disconnects the returned slot mask before producing status.
 */
enum mh_error_code	mh_server_lobby_tick(struct mh_server_lobby *lobby,
	unsigned long timeout_seconds, double now_ms,
	struct mh_server_lobby_tick *output, struct mh_error *error_out);

/* Clocks only; the host clears return_pending after sending both refreshes. */
enum mh_error_code	mh_server_lobby_pong(struct mh_server_lobby *lobby,
	unsigned long peer, const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out);

/*
 * Type 12 acknowledges loading; type 11 during loading only refreshes contact.
 */
enum mh_error_code	mh_server_race_pong(struct mh_server_lobby *lobby,
	unsigned long peer, const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out);
void			mh_server_lobby_status(
	const struct mh_server_lobby *lobby, struct mh_net_message *output);

#endif
