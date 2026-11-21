#include <string.h>

#include "engine/core/error.h"
#include "engine/core/limits.h"
#include "engine/network/driver.h"
#include "engine/network/join.h"
#include "engine/network/lobby.h"

#define PROVIDER_MAX 255
#define LOBBY_CLOCK_LIMIT 1e12
#define RESERVATION_TIMEOUT_MS 5000

#define REJECT_FULL 1

static int
peer_valid(unsigned long peer)
{
	return peer >= 2 && peer <= MH_U32_MAX;
}

static int
clock_valid(const struct mh_net_lobby *lobby, double now_ms)
{
	return lobby != NULL && now_ms >= lobby->now_ms &&
		now_ms <= LOBBY_CLOCK_LIMIT;
}

static enum mh_error_code
validate_message(
	const struct mh_net_message *message, struct mh_error *error_out)
{
	unsigned char bytes[MH_NET_PACKET_LIMIT];
	size_t size;

	return mh_net_encode(message, bytes, sizeof(bytes), &size, error_out);
}

enum mh_error_code
mh_net_lobby_init(unsigned int provider, const char *password,
	const struct mh_net_profile *host, struct mh_net_lobby *output,
	struct mh_error *error_out)
{
	struct mh_net_lobby lobby;
	struct mh_net_message message;
	size_t length;

	if (provider > PROVIDER_MAX || password == NULL || host == NULL ||
		output == NULL)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby initialization");

	length = 0;

	while (length < MH_NET_PASSWORD_BYTES && password[length] != 0)
		length++;

	if (length == MH_NET_PASSWORD_BYTES || host->update_rate == 0)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby password or update rate");

	memset(&message, 0, sizeof(message));
	message.type = MH_NET_PROFILE_JOIN;
	message.profile = *host;

	if (validate_message(&message, NULL) != MH_ERROR_NONE)
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid host profile");

	memset(&lobby, 0, sizeof(lobby));
	lobby.provider = provider;
	memcpy(lobby.password, password, length);
	lobby.slots[0].phase = MH_NET_IN_LOBBY;
	lobby.slots[0].peer = 1;
	lobby.slots[0].profile = *host;
	lobby.slots[0].profile.flags = MH_NET_FLAG_LOBBY;
	*output = lobby;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_lobby_expire(
	struct mh_net_lobby *lobby, double now_ms, struct mh_error *error_out)
{
	unsigned int i;

	if (!clock_valid(lobby, now_ms))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lobby clock");

	for (i = 1; i < MH_NET_PLAYERS; i++) {
		if (lobby->slots[i].phase == MH_NET_RESERVED &&
			now_ms - lobby->slots[i].reserved_ms >
				RESERVATION_TIMEOUT_MS)
			memset(&lobby->slots[i], 0, sizeof(lobby->slots[i]));
	}

	lobby->now_ms = now_ms;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static unsigned int
reserve_slot(struct mh_net_lobby *lobby, unsigned long peer, double now_ms)
{
	unsigned int i;

	/* Retries reuse an existing slot without refreshing its reservation. */
	for (i = 1; i < MH_NET_PLAYERS; i++)
		if (lobby->slots[i].phase != MH_NET_EMPTY &&
			lobby->slots[i].peer == peer)
			return i;

	for (i = 1; i < MH_NET_PLAYERS; i++) {
		if (lobby->slots[i].phase != MH_NET_EMPTY)
			continue;

		lobby->slots[i].phase = MH_NET_RESERVED;
		lobby->slots[i].peer = peer;
		lobby->slots[i].reserved_ms = now_ms;

		return i;
	}

	return MH_NET_PLAYERS;
}

enum mh_error_code
mh_net_lobby_admit(struct mh_net_lobby *lobby, unsigned long peer,
	const struct mh_net_message *request, double now_ms,
	struct mh_net_message *reply, struct mh_error *error_out)
{
	struct mh_net_lobby next;
	struct mh_net_message message;
	unsigned int slot;
	enum mh_error_code status;

	if (!clock_valid(lobby, now_ms) || !peer_valid(peer) ||
		request == NULL || reply == NULL ||
		request->type != MH_NET_JOIN ||
		validate_message(request, NULL) != MH_ERROR_NONE)
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby admission request");

	next = *lobby;
	mh_net_lobby_expire(&next, now_ms, NULL);

	status = mh_net_join_check_timing(next.provider, next.password,
		next.simulation_rate, &request->join, &message, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	if (message.type == MH_NET_ACCEPT) {
		slot = reserve_slot(&next, peer, now_ms);

		if (slot == MH_NET_PLAYERS) {
			message.type = MH_NET_REJECT;
			message.reason = REJECT_FULL;
			strcpy(message.text, "Server full!");
		} else {
			message.type = MH_NET_ACCEPT;
			message.player = slot;
		}
	}

	*lobby = next;
	*reply = message;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
apply_profile(struct mh_net_lobby *lobby, unsigned long peer,
	const struct mh_net_message *message, double now_ms, int racing,
	struct mh_error *error_out)
{
	struct mh_net_lobby next;
	struct mh_net_lobby_slot *slot;

	if (!clock_valid(lobby, now_ms) || !peer_valid(peer) ||
		message == NULL ||
		(message->type != MH_NET_PROFILE_JOIN &&
			message->type != MH_NET_PROFILE_UPDATE) ||
		message->player == 0 ||
		validate_message(message, NULL) != MH_ERROR_NONE ||
		message->profile.update_rate == 0 ||
		!mh_net_driver_text_valid(&message->profile))
		return mh_error_set(error_out, MH_ERROR_INVALID,
			"invalid lobby profile request");

	next = *lobby;
	mh_net_lobby_expire(&next, now_ms, NULL);
	slot = &next.slots[message->player];

	if (slot->peer != peer ||
		(message->type == MH_NET_PROFILE_JOIN &&
			slot->phase != MH_NET_RESERVED) ||
		(message->type == MH_NET_PROFILE_UPDATE &&
			slot->phase != MH_NET_IN_LOBBY))
		return mh_error_set(error_out, MH_ERROR_FAILED,
			"lobby profile does not belong to sender or phase");

	if (message->type == MH_NET_PROFILE_UPDATE) {
		mh_net_driver_edit(&slot->profile, &message->profile, !racing);
		slot->profile.value_a = message->profile.value_a;
		slot->profile.value_b = message->profile.value_b;
	} else {
		slot->profile = message->profile;
		slot->profile.flags = MH_NET_FLAG_LOBBY;
		slot->profile.values[MH_NET_PROFILE_RACES] =
			slot->profile.values[MH_NET_PROFILE_POINTS] = 0;
		slot->profile.value_a = slot->profile.value_b = 1;
	}

	slot->phase = MH_NET_IN_LOBBY;
	*lobby = next;

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_net_lobby_profile(struct mh_net_lobby *lobby, unsigned long peer,
	const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out)
{
	return apply_profile(lobby, peer, message, now_ms, 0, error_out);
}

enum mh_error_code
mh_net_lobby_race_profile(struct mh_net_lobby *lobby, unsigned long peer,
	const struct mh_net_message *message, double now_ms,
	struct mh_error *error_out)
{
	return apply_profile(lobby, peer, message, now_ms, 1, error_out);
}

enum mh_error_code
mh_net_lobby_disconnect(struct mh_net_lobby *lobby, unsigned long peer,
	struct mh_error *error_out)
{
	unsigned int i;

	if (lobby == NULL || !peer_valid(peer))
		return mh_error_set(
			error_out, MH_ERROR_INVALID, "invalid lobby peer");

	for (i = 1; i < MH_NET_PLAYERS; i++) {
		if (lobby->slots[i].peer == peer) {
			memset(&lobby->slots[i], 0, sizeof(lobby->slots[i]));
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);
		}
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
