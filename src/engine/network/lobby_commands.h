#ifndef MH_NET_LOBBY_COMMANDS_H
#define MH_NET_LOBBY_COMMANDS_H

#include "engine/core/error.h"
#include "engine/network/prediction.h"
#include "engine/network/protocol.h"

#define MH_LOBBY_COMMAND_EVENTS 32
#define MH_LOBBY_COMMAND_TEXT 512
/* Demo directory, 64-byte stem, extension and terminator. */
#define MH_LOBBY_COMMAND_DEMO_PATH_BYTES 75

/*
 * Presentation/controller state, separate from transport and fixed wire
 * fields. Native NUL slots permit safe display of full 16-byte identities.
 */
struct mh_lobby_command_player {
	char name[MH_NET_NAME_BYTES + 1], team[MH_NET_TEAM_BYTES],
		car[MH_NET_NAME_BYTES + 1], horn[MH_NET_NAME_BYTES + 1];
	unsigned long	color[4], rate, gear, recording, races, points;
	unsigned long	racing, lobby;
};

struct mh_lobby_command_state {
	enum mh_net_wire		wire;
	struct mh_net_prediction	prediction;
	unsigned int			host, player, count;
	unsigned long	confirm_clear, connected, quit, mode, laps;
	char track[MH_NET_NAME_BYTES + 1], password[MH_NET_PASSWORD_BYTES],
		demo_path[MH_LOBBY_COMMAND_DEMO_PATH_BYTES];
	struct mh_lobby_command_player	players[MH_NET_WIRE_PLAYERS];
};

struct mh_lobby_command_name {
	char	text[MH_NET_NAME_BYTES + 1];
};

struct mh_lobby_command_catalog {
	const struct mh_lobby_command_name	*tracks, *cars, *horns;
	unsigned int	track_count, track_limit, car_count, car_limit, horn_count;
};

enum mh_lobby_command_event_kind {
	MH_LOBBY_LOCAL,
	/* A preceding LOCAL event requests an echo; otherwise send only. */
	MH_LOBBY_SEND,
	MH_LOBBY_PROFILE,
	MH_LOBBY_ROSTER,
	MH_LOBBY_PALETTE,
	MH_LOBBY_KICK,
	MH_LOBBY_RECONNECT
};

enum mh_lobby_command_capability {
	MH_LOBBY_CAP_RECORDING = 1,
	MH_LOBBY_CAP_RECONNECT = 2,
	MH_LOBBY_CAP_SCROLL_KEYS = 4
};

struct mh_lobby_command_event {
	enum mh_lobby_command_event_kind	kind;

	/* Only KICK carries a target. */
	unsigned int				player;

	/* Optional capabilities required by help. */
	unsigned int				requirements;
	unsigned long				color;
	char					text[MH_LOBBY_COMMAND_TEXT];
};

struct mh_lobby_command_output {
	unsigned int			count;
	struct mh_lobby_command_event	events[MH_LOBBY_COMMAND_EVENTS];
};

/*
 * Parse a bounded byte draft, update controller values and emit original
 * ordered local/network/profile/roster/palette actions. Patched wire forwards
 * unknown commands without a LOCAL event; F1.2 reports them locally. Recognized
 * commands retain their local behavior. Whitespace tokenization has no quote
 * syntax. Numeric conversion uses 32-bit arithmetic, independent of native long
 * width and locale. Track/car limits are caller-resolved availability counts;
 * the original track failure notice compares with total count, not the search
 * limit. The caller supplies current score-clear confirmation and connection
 * flags. No sockets, asset access, file writes or application exit occur here.
 * Errors preserve state and output. Input may alias a state string.
 */
enum mh_error_code	mh_lobby_command(struct mh_lobby_command_state *state,
	const struct mh_lobby_command_catalog *catalog, const char *text,
	struct mh_lobby_command_output *output, struct mh_error *error_out);

#endif
