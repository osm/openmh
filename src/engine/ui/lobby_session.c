#include <string.h>

#include "engine/core/error.h"
#include "engine/ui/lobby_session.h"

static unsigned long
player_ping(const struct mh_net_session *session, unsigned int player)
{
	const struct mh_net_connection *connection;
	unsigned long peer;
	unsigned int i;

	if (player == session->player ||
		!(session->roster.players[player].flags & MH_NET_FLAG_PRESENT))
		return 0;

	peer = session->lobby.slots[player].peer;

	for (i = 0; i < MH_NET_CONNECTIONS; i++) {
		connection = &session->connections[i];

		if (connection->peer != 0 && !connection->closing &&
			connection->peer == peer)
			return connection->ping_ms;
	}

	return 0;
}

enum mh_error_code
mh_lobby_session_roster(const struct mh_net_session *session,
	const char *session_name, unsigned int password_mode,
	struct mh_lobby_roster *output, struct mh_error *error_out)
{
	struct mh_lobby_roster view;
	struct mh_lobby_roster_player *player;
	const struct mh_net_profile *profile;
	const struct mh_net_status *status;
	unsigned int i, length;

	if (session == NULL || session_name == NULL || output == NULL ||
		password_mode > 1 || session->phase != MH_NET_LOBBY ||
		session->roster.player_count == 0 ||
		session->roster.player_count >
			mh_net_session_capacity(session) ||
		session->player >= session->roster.player_count)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid live lobby roster");

	for (length = 0; length < sizeof(view.session) && session_name[length];
		length++) {
	}

	if (length == sizeof(view.session))
		return mh_error_set(error_out, MH_ERROR_RANGE,
			"lobby session name exceeds original field");

	memset(&view, 0, sizeof(view));
	view.layout = session->config.wire == MH_NET_WIRE_31_LOBBY
		? MH_LOBBY_ROSTER_31
		: MH_LOBBY_ROSTER_F12;
	view.count = session->roster.player_count;
	view.player = session->player;
	view.password_mode = password_mode;
	view.mode = session->roster.mode;
	view.laps = session->roster.laps;
	memcpy(view.session, session_name, length);
	memcpy(view.password, session->config.password,
		sizeof(session->config.password));
	memcpy(view.track, session->roster.track,
		sizeof(session->roster.track));

	for (i = 0; i < view.count; i++) {
		player = &view.players[i];
		profile = &session->roster.players[i];
		player->racing = (profile->flags & MH_NET_FLAG_RACING) != 0;
		player->lobby = (profile->flags & MH_NET_FLAG_LOBBY) != 0;
		player->ready = (profile->flags & MH_NET_FLAG_READY) != 0;
		player->pass = (profile->flags & MH_NET_FLAG_PASS) != 0;
		player->recording =
			(profile->flags & MH_NET_FLAG_RECORDING) != 0;
		player->races = profile->values[MH_NET_PROFILE_RACES];
		player->points = profile->values[MH_NET_PROFILE_POINTS];
		player->position = profile->value_a;
		player->lap = profile->value_b;

		if (session->replication.status_valid & (1U << i)) {
			status = &session->replication.status[i];
			player->position = status->value_a;
			player->lap = status->value_b;
			player->ping = status->ping_ms;
		}

		if (session->host)
			player->ping = player_ping(session, i);

		if (i == session->player) {
			profile = &session->config.profile;
			player->recording =
				(profile->flags & MH_NET_FLAG_RECORDING) != 0;
		}

		memcpy(player->name, profile->name, sizeof(profile->name));
		memcpy(player->team, profile->team, sizeof(player->team) - 1);
		memcpy(player->car, profile->car, sizeof(profile->car));
		player->gear = profile->gear_mode;
		player->color = profile->values[MH_NET_PROFILE_COLOR];
	}

	*output = view;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
