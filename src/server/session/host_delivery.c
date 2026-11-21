#include <string.h>

#include "server/session/host_internal.h"

#define CHAT_COLOR 0xe06820UL
#define NOTICE_COLOR 0xffffffUL

enum mh_error_code
mh_server_host_send(struct mh_server_host *host, unsigned long peer,
	const struct mh_net_message *message, int reliable,
	struct mh_error *error_out)
{
	unsigned char bytes[MH_NET_WIRE_PACKET_LIMIT];
	size_t size;
	enum mh_error_code status;
	unsigned int i;

	for (i = 0; i < host->congested_count; i++)
		if (host->congested[i] == peer)
			return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	status = mh_net_wire_encode(MH_NET_WIRE_31_LOBBY, message, bytes,
		sizeof(bytes), &size, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	status = mh_transport_send(
		host->transport, peer, bytes, size, reliable, error_out);

	if (status == MH_ERROR_BACKPRESSURE) {
		/* One incoming peer plus at most one send per driver slot. */
		if (host->congested_count == MH_NET_WIRE_PLAYERS)
			return mh_error_set(error_out, MH_ERROR_STATE,
				"too many pending peer disconnections");

		host->congested[host->congested_count++] = peer;

		return mh_error_set(error_out, MH_ERROR_NONE, NULL);
	}

	return status;
}

enum mh_error_code
mh_server_host_advertise(
	struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_discovery_advertisement game;
	struct mh_net_message roster;
	enum mh_error_code status;

	mh_server_lobby_roster(&host->lobby, &roster);
	memset(&game, 0, sizeof(game));
	memcpy(game.name, host->config->session_name, sizeof(game.name));
	game.has_roster = 1;
	game.roster = roster.roster;

	/* Advertise configured capacity, unlike broadcasts. */
	game.roster.player_count = (unsigned int)host->config->max_players + 1;

	status = mh_transport_advertise(host->transport, &game, error_out);

	if (status == MH_ERROR_UNSUPPORTED)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	return status;
}

unsigned int
mh_server_host_members(const struct mh_server_host *host, unsigned int flags)
{
	unsigned int i, members;

	members = 0;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++)
		if (host->lobby.roster.players[i].flags & flags)
			members |= 1U << i;

	return members;
}

enum mh_error_code
mh_server_host_multicast(struct mh_server_host *host, unsigned int recipients,
	const struct mh_net_message *message, int reliable,
	struct mh_error *error_out)
{
	unsigned int i;
	enum mh_error_code status;

	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(recipients & (1U << i)))
			continue;

		status = mh_server_host_send(host, host->lobby.peers[i],
			message, reliable, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

enum mh_error_code
mh_server_host_notice(struct mh_server_host *host, unsigned int recipients,
	unsigned int type, const char *text, struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned int i;
	enum mh_error_code status;

	if (strlen(text) >= MH_SERVER_NOTICE_BYTES)
		return mh_error_set(
			error_out, MH_ERROR_RANGE, "dedicated notice too long");

	memset(&message, 0, sizeof(message));
	message.type = type;
	message.color = type == MH_NET_NOTICE ? NOTICE_COLOR : CHAT_COLOR;
	strcpy(message.text, text);

	if (type != MH_NET_NOTICE)
		return mh_server_host_multicast(
			host, recipients, &message, 1, error_out);

	/* Send only changed text/color through reliable delivery. */
	for (i = 1; i < MH_NET_WIRE_PLAYERS; i++) {
		if (!(recipients & (1U << i)) ||
			((host->race.notice_valid & (1U << i)) &&
				!strcmp(host->race.notices[i], text)))
			continue;

		status = mh_server_host_send(
			host, host->lobby.peers[i], &message, 1, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		strcpy(host->race.notices[i], text);
		host->race.notice_valid |= 1U << i;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}
