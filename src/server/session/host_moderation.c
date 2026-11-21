#include <stdio.h>
#include <string.h>

#include "engine/core/limits.h"
#include "server/session/host_internal.h"
#include "server/session/kick_votes.h"

#define MINUTE_MS 60000UL

enum mh_error_code
mh_server_host_kick(struct mh_server_host *host, unsigned int player,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned long peer;
	enum mh_error_code status;

	peer = host->lobby.peers[player];
	memset(&message, 0, sizeof(message));
	message.type = MH_NET_KICK;
	status = mh_server_host_send(host, peer, &message, 1, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	/* Flush the kick before removing the player, without a ban. */
	status = mh_transport_flush(host->transport, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_server_host_disconnect(
		host, peer, " disconnected (kicked).", error_out);
}

enum mh_error_code
mh_server_host_kick_votes(
	struct mh_server_host *host, struct mh_error *error_out)
{
	struct mh_transport_address address;
	char text[MH_SERVER_TEXT_BYTES];
	unsigned int player;
	unsigned long minutes, duration;
	long signed_minutes;
	int banned;
	enum mh_error_code status;

	if (host->race.active)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	player = mh_server_kick_majority(&host->lobby);

	if (player == 0)
		return mh_error_set(error_out, MH_ERROR_NONE, NULL);

	minutes = host->config->ban_minutes;
	duration = (minutes * MINUTE_MS) & MH_U32_MAX;
	signed_minutes = minutes > MH_I32_MAX
		? -1L - (long)(MH_U32_MAX - minutes)
		: (long)minutes;

	status = mh_transport_peer_address(host->transport,
		host->lobby.peers[player], &address, error_out);

	if (status != MH_ERROR_NONE && status != MH_ERROR_UNSUPPORTED)
		return status;

	banned = 0;

	if (status == MH_ERROR_NONE) {
		status = mh_server_bans_add(&host->bans, address.host, duration,
			mh_transport_now(host->transport), error_out);

		/* A full table still permits kicking this connection. */
		if (status != MH_ERROR_NONE && status != MH_ERROR_RANGE)
			return status;

		banned = status == MH_ERROR_NONE;
	}

	if (banned)
		sprintf(text,
			"%s was kicked and banned(%ld mins) by majority vote...",
			host->lobby.roster.players[player].name,
			signed_minutes);
	else
		sprintf(text, "%s was kicked by majority vote...",
			host->lobby.roster.players[player].name);
	mh_server_event_log(host->event_logger, text, NULL);
	status = mh_server_host_notice(host,
		mh_server_host_members(host, MH_NET_FLAG_LOBBY), MH_NET_CHAT,
		text, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_server_host_kick(host, player, error_out);
}
