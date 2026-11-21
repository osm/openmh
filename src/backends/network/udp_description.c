#include <string.h>

#include "backends/network/udp_control.h"
#include "backends/network/udp_description.h"

enum mh_error_code
mh_udp_description_encode(const struct mh_discovery_advertisement *game,
	unsigned char *output, size_t capacity, size_t *written,
	struct mh_error *error_out)
{
	struct mh_udp_control control;
	struct mh_net_message message;
	unsigned char roster[MH_NET_WIRE_PACKET_LIMIT];
	size_t size;
	enum mh_error_code status;

	if (game == NULL || game->has_roster != 1)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"LAN/INET advertising requires a complete roster");

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_ROSTER;
	message.roster = game->roster;
	status = mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &message, roster,
		sizeof(roster), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&control, 0, sizeof(control));
	control.type = MH_UDP_DESCRIPTION;
	memcpy(control.name, game->name, sizeof(control.name));
	control.description = roster;
	control.description_size = size;

	return mh_udp_control_encode(
		&control, output, capacity, written, error_out);
}

enum mh_error_code
mh_udp_description_decode(const unsigned char *data, size_t size,
	struct mh_discovery_advertisement *output, struct mh_error *error_out)
{
	struct mh_udp_control control;
	struct mh_net_message message;
	struct mh_net_frame frame;
	struct mh_discovery_advertisement game;
	unsigned int player, flags;
	enum mh_error_code status;

	if (output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"missing session description output");

	status = mh_udp_control_decode(data, size, &control, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (control.type != MH_UDP_DESCRIPTION)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"expected LAN/INET description");

	frame.data = control.description;
	frame.size = control.description_size;
	frame.type = MH_NET_ROSTER;
	status = mh_net_wire_decode(
		MH_NET_WIRE_31_LOBBY, &frame, &message, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	memset(&game, 0, sizeof(game));
	memcpy(game.name, control.name, sizeof(game.name));
	game.has_roster = 1;
	game.roster = message.roster;
	memcpy(game.track, game.roster.track, sizeof(game.track));
	game.mode = game.roster.mode;
	game.laps = game.roster.laps;
	game.car_count = game.roster.car_count;
	game.password = MH_DISCOVERY_PASSWORD_UNKNOWN;

	for (player = 0; player < game.roster.player_count; player++) {
		flags = game.roster.players[player].flags;

		if (flags & MH_NET_FLAG_PRESENT)
			game.players++;

		if (flags & MH_NET_FLAG_RACING)
			game.racing = 1;
	}

	*output = game;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
