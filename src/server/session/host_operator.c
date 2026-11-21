#include <string.h>

#include "server/session/host_internal.h"

static enum mh_error_code
apply_action(struct mh_server_host *host,
	struct mh_server_operator_output *output, struct mh_error *error_out)
{
	struct mh_transport_address address;
	enum mh_error_code status;

	if (output->action == MH_SERVER_OPERATOR_KICK) {
		status = mh_server_host_kick(host, output->player, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (output->action == MH_SERVER_OPERATOR_USER) {
		status = mh_transport_peer_address(host->transport,
			host->lobby.peers[output->player], &address, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		mh_server_information_user(
			&host->lobby, output->player, &address, &output->text);
	}

	if (output->action != MH_SERVER_OPERATOR_NONE && host->race.active) {
		status = mh_server_host_abort_race(host, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	if (output->action == MH_SERVER_OPERATOR_CLEAR) {
		host->lobby.clear_votes |= 1U;
		status = mh_server_host_clear_scores(host, error_out);

		if (status != MH_ERROR_NONE)
			return status;

		strcpy(output->text.lines[0], "Score and gridstats cleared...");
		output->text.count = 1;
	}

	if (output->action == MH_SERVER_OPERATOR_START) {
		status = mh_server_ready_force(&host->ready,
			mh_transport_now(host->transport), error_out);

		if (status == MH_ERROR_NONE)
			status = mh_server_host_countdown_notice(
				host, error_out);

		if (status != MH_ERROR_NONE)
			return status;
	}

	return mh_error_set(error_out, MH_ERROR_NONE, NULL);
}

static enum mh_error_code
deliver_output(struct mh_server_host *host,
	const struct mh_server_operator_output *output,
	struct mh_error *error_out)
{
	struct mh_net_message message;
	unsigned int audience;
	enum mh_error_code status;

	status = MH_ERROR_NONE;

	if (output->chat) {
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_CHAT;
		message.color = host->lobby.roster.players[0]
					.values[MH_NET_PROFILE_COLOR];
		strcpy(message.text, output->text.lines[0]);
		audience = host->race.active ? MH_NET_FLAG_RACING
					: MH_NET_FLAG_LOBBY;
		status = mh_server_host_multicast(host,
			mh_server_host_members(host, audience), &message, 1,
			error_out);
	}

	if (status == MH_ERROR_NONE && output->refresh_identity) {
		memset(&message, 0, sizeof(message));
		message.type = MH_NET_PROFILE_UPDATE;
		message.profile = host->lobby.roster.players[0];

		/* These profile bytes are fixed, not results. */
		message.profile.value_a = message.profile.value_b = 1;
		status = mh_server_host_multicast(host,
			mh_server_host_members(host, MH_NET_FLAG_LOBBY),
			&message, 1, error_out);
	}

	if (status == MH_ERROR_NONE && output->refresh_roster) {
		mh_server_lobby_roster(&host->lobby, &message);
		status = mh_server_host_multicast(host,
			mh_server_host_members(host, MH_NET_FLAG_LOBBY),
			&message, 1, error_out);
	}

	if (status == MH_ERROR_NONE)
		status = mh_server_host_advertise(host, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return mh_transport_flush(host->transport, error_out);
}

enum mh_error_code
mh_server_host_command(struct mh_server_host *host, const char *line,
	struct mh_server_operator_output *output, struct mh_error *error_out)
{
	enum mh_error_code status;

	status = mh_server_operator_command(&host->lobby, host->config,
		host->race.active, line, output, error_out);

	if (status == MH_ERROR_NONE)
		status = apply_action(host, output, error_out);

	if (status != MH_ERROR_NONE)
		return status;

	return deliver_output(host, output, error_out);
}
