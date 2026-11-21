#include <string.h>

#include "server/session/setup.h"

#define SETUP_FLAGS                                                            \
	(MH_NET_PROFILE_FLAGS | MH_NET_FLAG_READY | MH_NET_FLAG_PASS)

static enum mh_error_code
prepare_setup(const struct mh_server_lobby *lobby,
	struct mh_server_setup *setup, struct mh_error *error_out)
{
	unsigned char bytes[MH_NET_WIRE_PACKET_LIMIT];
	struct mh_net_profile *profile;
	unsigned int i, flags, racer_count, position_mask, position;
	size_t size;

	memset(setup, 0, sizeof(*setup));
	setup->message.type = MH_NET_RACE_CONFIG;
	setup->message.roster = lobby->roster;
	racer_count = position_mask = 0;

	for (i = 0; i < lobby->roster.player_count; i++) {
		profile = &setup->message.roster.players[i];
		flags = profile->flags;

		if (flags & MH_NET_FLAG_RACING)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"dedicated setup requires the previous race to end");

		profile->flags &= SETUP_FLAGS;
		profile->value_a = lobby->reported_a[i];
		profile->value_b = 1;

		if (i == 0 || !(flags & MH_NET_FLAG_LOBBY) ||
			!(flags & MH_NET_FLAG_READY))
			continue;

		position = profile->value_a;

		if (position == 0 || position >= lobby->roster.player_count ||
			(position_mask & (1U << position)))
			return mh_error_set(error_out, MH_ERROR_STATE,
				"invalid assigned dedicated grid");

		setup->racers |= 1U << i;
		position_mask |= 1U << position;
		racer_count++;
	}

	if (position_mask != (1U << (racer_count + 1)) - 2 ||
		lobby->reported_a[0] != racer_count)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"dedicated setup requires a compact assigned grid");

	/* Temporarily hide the server while serializing the roster. */
	setup->message.roster.players[0].flags &= ~MH_NET_FLAG_PRESENT;

	return mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, &setup->message, bytes,
		sizeof(bytes), &size, error_out);
}

enum mh_error_code
mh_server_setup_begin(struct mh_server_lobby *lobby,
	struct mh_server_setup *output, struct mh_error *error_out)
{
	struct mh_server_setup setup;
	struct mh_net_profile *profile;
	unsigned int i, required;
	enum mh_error_code status;

	if (lobby == NULL || output == NULL || lobby->roster.player_count < 2 ||
		lobby->roster.player_count > MH_NET_WIRE_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated setup request");

	required = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;

	if ((lobby->roster.players[0].flags & required) != required)
		return mh_error_set(error_out, MH_ERROR_STATE,
			"dedicated setup requires the internal ready server");

	if (!mh_server_lobby_synchronized(lobby))
		return mh_error_set(error_out, MH_ERROR_STATE,
			"dedicated setup waits for lobby synchronization");

	status = prepare_setup(lobby, &setup, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Clear prior results even for drivers remaining in the lobby. */
	for (i = 0; i < lobby->roster.player_count; i++) {
		profile = &lobby->roster.players[i];
		profile->value_a = profile->value_b = 0;

		if (i != 0 && !(setup.racers & (1U << i)))
			continue;

		profile->flags &= ~(MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY |
			MH_NET_FLAG_PASS);
		profile->flags |= MH_NET_FLAG_RACING;
	}

	/* Mark only the internal server loaded after preparation. */
	lobby->roster.players[0].flags |= MH_NET_FLAG_READY;
	*output = setup;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_setup_step(const struct mh_server_lobby *lobby,
	struct mh_net_start *start, long double game_ms,
	unsigned int *send_mask, struct mh_error *error_out)
{
	unsigned int i, active_mask, ready_mask;

	if (lobby == NULL || lobby->roster.player_count < 2 ||
		lobby->roster.player_count > MH_NET_WIRE_PLAYERS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated start membership");

	active_mask = ready_mask = 0;

	for (i = 0; i < lobby->roster.player_count; i++) {
		if (lobby->roster.players[i].flags & MH_NET_FLAG_RACING)
			active_mask |= 1U << i;

		if (lobby->roster.players[i].flags & MH_NET_FLAG_READY)
			ready_mask |= 1U << i;
	}

	return mh_net_start_step_dedicated(start, active_mask, ready_mask,
		lobby->ping_ms, game_ms, send_mask, error_out);
}

enum mh_error_code
mh_server_setup_loaded(struct mh_server_lobby *lobby, unsigned long peer,
	const struct mh_net_message *message, struct mh_error *error_out)
{
	unsigned int player;

	if (lobby == NULL || message == NULL || message->type != MH_NET_LOADED)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid race loading notification");

	player = message->player;

	if (player == 0 || player >= MH_NET_WIRE_PLAYERS || peer == 0 ||
		lobby->peers[player] != peer)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"loaded player does not belong to sender");

	if (lobby->roster.players[player].flags & MH_NET_FLAG_RACING)
		lobby->roster.players[player].flags |= MH_NET_FLAG_READY;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
