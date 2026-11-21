#ifndef MH_CLIENT_MULTIPLAYER_H
#define MH_CLIENT_MULTIPLAYER_H

#include "client/application/race_frontend.h"
#include "engine/network/session.h"
#include "interfaces/catalog/catalog.h"

struct mh_transport_target;

enum mh_multiplayer_state {
	MH_NETWORK_OFFLINE,
	MH_NETWORK_CONNECTING,
	MH_NETWORK_LOBBY,
	MH_NETWORK_LOADING,
	MH_NETWORK_RACING,
	MH_NETWORK_LEAVING,
	MH_NETWORK_FAILED
};

/* Copied view: no borrowed connection or asset pointers. */
struct mh_multiplayer_snapshot {
	enum mh_multiplayer_state	state;
	unsigned int			player;
	unsigned int	car_count, car_ids[MH_CAR_CATALOG_LIMIT];
	struct mh_net_roster		roster;
	struct mh_net_chat_line		chat[MH_NET_CHAT_LINES];
	unsigned int			chat_count;
	unsigned long			chat_revision, results_revision;
	struct mh_net_chat_line		notice;
	unsigned long			notice_revision, statistics_revision;
	struct mh_net_statistics	last_race;
	int				finished, show_fps;
	struct mh_standings		results;
};

int	mh_application_network_active(const struct mh_application *app);
int	mh_application_network_available(const struct mh_application *app);

/*
 * Queue a client connection using the application's selected provider.
 * Success transfers target ownership; failure leaves it with the caller.
 * Copy settings/password before returning. An active connection excludes
 * single-player launches and binding edits. No sockets or browser types here.
 */
enum mh_error_code	mh_application_join_network(struct mh_application *app,
	struct mh_transport_target *target,
	const struct mh_race_request *settings, const char *password,
	struct mh_error *error_out);
void			mh_application_network_snapshot(
	const struct mh_application *app,
	struct mh_multiplayer_snapshot *output);

/* Update lobby identity atomically; an empty team clears the team tag. */
enum mh_error_code	mh_application_network_player(
	struct mh_application *app, const char *name, const char *team,
	unsigned long color, struct mh_error *error_out);

/*
 * Local display preference for subsequent races; does not change server state.
 */
enum mh_error_code	mh_application_network_show_fps(
	struct mh_application *app, int enabled, struct mh_error *error_out);

/*
 * Native lobby commands and ordinary chat; optional output holds local replies.
 */
enum mh_error_code	mh_application_network_chat(struct mh_application *app,
	const char *text, struct mh_lobby_command_output *output,
	struct mh_error *error_out);

/*
 * Submit the native dedicated-server /ready or /notready command. These move
 * through uncommitted between ready and passing; the next roster confirms the
 * resulting choice. This is a command, not an immediate boolean assignment.
 */
enum mh_error_code	mh_application_network_ready(struct mh_application *app,
	int ready, struct mh_error *error_out);

/* Toggle confirmed readiness, including the server's passing transition. */
enum mh_error_code	mh_application_network_toggle_ready(
	struct mh_application *app, struct mh_error *error_out);
enum mh_error_code	mh_application_network_return(
	struct mh_application *app, struct mh_error *error_out);
enum mh_error_code	mh_application_leave_network(struct mh_application *app,
	struct mh_error *error_out);

#endif
