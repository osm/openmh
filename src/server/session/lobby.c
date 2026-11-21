#include <math.h>
#include <string.h>

#include "engine/core/text.h"
#include "engine/network/driver.h"
#include "server/session/kick_votes.h"
#include "server/session/lobby.h"

#define SERVER_PROFILE_COLOR 0xe06820UL
#define CLOCK_LIMIT_MS 1e12

enum {
	DEFAULT_UPDATE_RATE = 15,
	PROFILE_MESSAGE_BYTES = 88,
	WIRE_BYTE_MAX = 255
};

int
mh_server_lobby_disconnect(struct mh_server_lobby *lobby, unsigned long peer)
{
	unsigned int i;
	int changed;

	if (peer == 0)
		return 0;

	changed = 0;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (lobby->pending_peers[i] == peer) {
			lobby->pending_peers[i] = 0;
			lobby->admission.reserved &= ~(1U << i);
		}

		if (lobby->peers[i] != peer)
			continue;

		/* Reset flags/results, retaining identity and scores. */
		changed |= (lobby->roster.players[i].flags &
				MH_NET_FLAG_PRESENT) != 0;
		lobby->roster.players[i].flags = 0;
		lobby->roster.players[i].value_a = 0;
		lobby->roster.players[i].value_b = 0;
		lobby->peers[i] = 0;
		lobby->return_pending &= ~(1U << i);
		lobby->clear_votes &= ~(1U << i);
		mh_server_kick_votes_remove(lobby, i);
	}

	return changed;
}

void
mh_server_lobby_roster(
	const struct mh_server_lobby *lobby, struct mh_net_message *output)
{
	/* Hide slot zero while packing, then restore its state. */
	memset(output, 0, sizeof(*output));

	output->type = MH_NET_ROSTER;
	output->roster = lobby->roster;
	output->roster.players[0].flags &= ~MH_NET_FLAG_PRESENT;
}

enum mh_error_code
mh_server_lobby_init(const struct mh_server_config *config, const char *track,
	struct mh_server_lobby *output, struct mh_error *error_out)
{
	struct mh_server_lobby lobby;
	struct mh_net_profile *server;
	size_t length;
	unsigned int i;

	if (config == NULL || track == NULL || output == NULL ||
		config->max_players > MH_SERVER_RACERS ||
		config->max_rate > WIRE_BYTE_MAX ||
		config->laps > WIRE_BYTE_MAX ||
		config->unlocked_cars > WIRE_BYTE_MAX ||
		(config->mode != MH_SERVER_NORMAL &&
			config->mode != MH_SERVER_ELIMINATION) ||
		memchr(config->password, 0, sizeof(config->password)) == NULL ||
		memchr(config->server_name, 0, sizeof(config->server_name)) ==
			NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated lobby configuration");

	length = 0;

	while (length < MH_NET_NAME_BYTES && track[length] != '\0')
		length++;

	if (length == 0 || length == MH_NET_NAME_BYTES)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated lobby track");

	memset(&lobby, 0, sizeof(lobby));
	memcpy(lobby.roster.track, track, length);
	lobby.roster.player_count = MH_NET_WIRE_PLAYERS;
	lobby.roster.mode = (unsigned int)config->mode;
	lobby.roster.laps = (unsigned int)config->laps;
	lobby.grid = config->grid;
	memcpy(lobby.password, config->password, sizeof(lobby.password));
	lobby.roster.car_count = (unsigned int)config->unlocked_cars;

	for (i = 0; i < MH_NET_WIRE_PLAYERS; i++)
		lobby.roster.players[i].update_rate = DEFAULT_UPDATE_RATE;

	/* Local server identity, not a selected car. */
	server = &lobby.roster.players[0];
	server->flags = MH_NET_FLAG_LOBBY | MH_NET_FLAG_READY;
	memcpy(server->name, config->server_name, sizeof(server->name));
	strcpy(server->team, "TSL");
	server->gear_mode = 1;

	for (i = MH_NET_PROFILE_COLOR; i <= MH_NET_PROFILE_CAR_COLOR3; i++)
		server->values[i] = SERVER_PROFILE_COLOR;

	*output = lobby;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_lobby_join(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned long peer,
	const struct mh_net_join *request, double now_ms,
	struct mh_net_message *reply, struct mh_error *error_out)
{
	enum mh_error_code status;
	unsigned int i, previous;

	if (lobby == NULL || config == NULL || peer == 0 ||
		config->max_players > MH_SERVER_RACERS)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated join peer");

	previous = 0;

	for (i = 1; i <= config->max_players; i++) {
		if (lobby->peers[i] == peer)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"driver already joined");

		if (lobby->pending_peers[i] == peer)
			previous = i;
	}

	status = mh_server_admission_join_timing(&lobby->admission,
		config->max_players, lobby->password, &lobby->roster,
		config->network_legacy ? 0
				: (unsigned int)config->simulation_rate,
		previous, request, now_ms, reply, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (reply->type == MH_NET_ACCEPT) {
		if (previous != 0)
			lobby->pending_peers[previous] = 0;

		lobby->pending_peers[reply->player] = peer;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
profile_owner(const struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned long peer,
	const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out)
{
	unsigned int player, i, present;

	player = message->player;

	if (player == 0 || player > config->max_players)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"dedicated profile slot outside racer capacity");

	present = lobby->roster.players[player].flags & MH_NET_FLAG_PRESENT;

	if (message->type == MH_NET_PROFILE_UPDATE) {
		if (present && lobby->peers[player] == peer)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"dedicated profile does not belong to sender");
	}

	if (present ||
		!mh_server_admission_reserved(
			&lobby->admission, player, now_ms) ||
		lobby->pending_peers[player] != peer)
		return mh_error_set(error_out, MH_ERROR_PERMISSION,
			"dedicated profile does not own its reservation");

	/* Reject an occupied requested slot or an already-bound peer. */
	for (i = 1; i <= config->max_players; i++) {
		if ((lobby->roster.players[i].flags & MH_NET_FLAG_PRESENT) &&
			lobby->peers[i] == peer)
			return mh_error_set(error_out, MH_ERROR_PERMISSION,
				"dedicated peer already has a profile");
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static void
join_profile(struct mh_server_lobby *lobby, unsigned long peer,
	unsigned int player, const struct mh_net_profile *incoming,
	double now_ms)
{
	struct mh_net_profile *profile;
	unsigned int recording;

	profile = &lobby->roster.players[player];
	recording = profile->flags & MH_NET_FLAG_RECORDING;
	*profile = *incoming;
	profile->flags = MH_NET_FLAG_LOBBY | recording;
	profile->values[MH_NET_PROFILE_RACES] = 0;
	profile->values[MH_NET_PROFILE_POINTS] = 0;
	profile->value_a = profile->value_b = 0;
	lobby->reported_a[player] = lobby->reported_b[player] = 1;
	lobby->peers[player] = peer;
	lobby->pending_peers[player] = 0;
	lobby->admission.reserved &= ~(1U << player);
	lobby->return_pending &= ~(1U << player);
	lobby->clear_votes &= ~(1U << player);
	lobby->kick_votes[player] = 0;
	lobby->contact_ms[player] = floor(now_ms);
}

static int
catalog_contains(const char names[][MH_SERVER_NAME_BYTES], unsigned int count,
	const char *name)
{
	unsigned int i;

	for (i = 0; i < count; i++)
		if (mh_ascii_equal(names[i], name))
			return 1;

	return count == 0;
}

enum mh_error_code
mh_server_lobby_profile(struct mh_server_lobby *lobby,
	const struct mh_server_config *config, unsigned long peer,
	const struct mh_net_message *message, double now_ms,
	struct mh_net_message *broadcast, struct mh_error *error_out)
{
	unsigned char bytes[PROFILE_MESSAGE_BYTES];
	const struct mh_net_profile *incoming;
	struct mh_net_profile *profile;
	size_t size;
	enum mh_error_code status;

	if (lobby == NULL || config == NULL || message == NULL ||
		broadcast == NULL || peer == 0 || !(now_ms >= 0) ||
		now_ms > CLOCK_LIMIT_MS ||
		config->max_players > MH_SERVER_RACERS ||
		config->max_rate > WIRE_BYTE_MAX ||
		(message->type != MH_NET_PROFILE_JOIN &&
			message->type != MH_NET_PROFILE_UPDATE))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid dedicated profile request");

	status = mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, message, bytes,
		sizeof(bytes), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	incoming = &message->profile;

	if (!mh_net_driver_text_valid(incoming))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid driver profile text");

	if (config->car_count > MH_CAR_CATALOG_LIMIT ||
		config->horn_count > MH_HORNS ||
		!catalog_contains(
			config->cars, config->car_count, incoming->car) ||
		!catalog_contains(
			config->horns, config->horn_count, incoming->horn))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"unknown driver car or horn");

	status = profile_owner(lobby, config, peer, message, now_ms, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (message->type == MH_NET_PROFILE_JOIN)
		join_profile(lobby, peer, message->player, &message->profile,
			now_ms);
	else {
		mh_net_driver_edit(
			&lobby->roster.players[message->player], incoming, 1);
		lobby->reported_a[message->player] = incoming->value_a;
		lobby->reported_b[message->player] = incoming->value_b;
	}

	profile = &lobby->roster.players[message->player];

	if (profile->update_rate == 0)
		profile->update_rate = 1;

	if (profile->update_rate > config->max_rate)
		profile->update_rate = (unsigned int)config->max_rate;

	if (message->type == MH_NET_PROFILE_JOIN)
		mh_server_lobby_roster(lobby, broadcast);
	else {
		*broadcast = *message;
		broadcast->profile = *profile;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
